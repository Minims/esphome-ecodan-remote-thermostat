#include "cnrf_proxy.h"
#ifdef USE_ECODAN_CNRF_PROXY
#include <algorithm>
#include "esphome/core/hal.h"
#include "esphome/components/uart/uart_component_esp_idf.h"
#include "hal/uart_ll.h"

namespace esphome::ecodan {

bool CnrfProxy::configure_port(unsigned index, uart::UARTComponent *component) {
  if (component == nullptr) return false;
  auto *idf = static_cast<uart::IDFUARTComponent *>(component);
  ports_[index] = static_cast<uart_port_t>(idf->get_hw_serial_number());
  auto port = ports_[index];
  if (port >= UART_NUM_MAX || !uart_is_driver_installed(port)) return false;
  // ESPHome installs no event queue; the proxy takes exclusive ownership of the driver.
  if (uart_driver_delete(port) != ESP_OK) return false;
  // One event per byte: leave room for errors when the RX buffer fills up.
  if (uart_driver_install(port, 512, 0, EVENT_QUEUE_SIZE, &events_[index], 0) != ESP_OK) return false;
  uart_config_t config{};
  config.baud_rate = 2400;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_EVEN;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  return uart_param_config(port, &config) == ESP_OK &&
         uart_set_pin(port, pins_[index * 2], pins_[index * 2 + 1], UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) == ESP_OK &&
         uart_set_rx_full_threshold(port, 1) == ESP_OK && uart_set_rx_timeout(port, 2) == ESP_OK;
}

bool CnrfProxy::start(uart::UARTComponent *ftc, uart::UARTComponent *receiver) {
  if (!configure_port(0, ftc) || !configure_port(1, receiver)) return false;
  stopped_ = false;
  // Share the UART ISR core to inspect the FIFO and RX buffer without concurrent transfers.
  if (xTaskCreatePinnedToCore(task_entry, "cnrf_proxy", 6144, this, 2, nullptr, xPortGetCoreID()) != pdPASS) {
    stopped_ = true;
    return false;
  }
  return true;
}

void CnrfProxy::stop() {
  stopping_ = true;
  while (!stopped_) vTaskDelay(1);
}

bool CnrfProxy::apply(float temperature, int32_t sample_age_ms) {
  portENTER_CRITICAL(&mutex_);
  bool accepted = status_.ready && override_.accept(temperature, sample_age_ms, millis());
  if (accepted) status_.uart_fault = false;
  portEXIT_CRITICAL(&mutex_);
  return accepted;
}

void CnrfProxy::disable() {
  portENTER_CRITICAL(&mutex_);
  override_.disable();
  ++disable_generation_;
  portEXIT_CRITICAL(&mutex_);
}

CnrfProxyStatus CnrfProxy::status() {
  portENTER_CRITICAL(&mutex_);
  auto copy = status_;
  const auto now = millis();
  copy.requested = override_.requested();
  copy.effective = override_.effective(now);
  copy.remaining_ms = override_.remaining_ms(now);
  copy.override_timeout_ms = override_.timeout_ms();
  copy.target_temperature = override_.temperature();
  portEXIT_CRITICAL(&mutex_);
  return copy;
}

void CnrfProxy::fault(CnrfFrameRewriter &rewriter, CnrfProxyStatus &status, bool overflow) {
  rewriter.invalidate();
  ++status.uart_errors;
  if (overflow) ++status.overflows;
  portENTER_CRITICAL(&mutex_);
  override_.disable();
  ++disable_generation_;
  status_.uart_fault = true;
  portEXIT_CRITICAL(&mutex_);
}

void CnrfProxy::handle_errors(unsigned index, CnrfFrameRewriter &rewriter, CnrfProxyStatus &status) {
  uart_event_t event;
  for (unsigned i = 0; i < EVENT_QUEUE_SIZE && xQueueReceive(events_[index], &event, 0) == pdTRUE; ++i) {
    if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL)
      fault(rewriter, status, true);
    else if (event.type == UART_PARITY_ERR || event.type == UART_FRAME_ERR || event.type == UART_BREAK)
      fault(rewriter, status);
  }
}

void CnrfProxy::transmit(unsigned index, ByteQueue &queue, CnrfFrameRewriter &rewriter, CnrfProxyStatus &status) {
  if (!queue.size()) return;
  // Without an IDF TX buffer, tx_chars writes only what fits in the FIFO without waiting.
  int written = uart_tx_chars(ports_[index], reinterpret_cast<const char *>(queue.data()), std::min<std::size_t>(32, queue.contiguous_size()));
  if (written < 0) { fault(rewriter, status); return; }
  queue.consume(written);
  if (index == 0) status.ftc_tx_bytes += written;
  else status.receiver_tx_bytes += written;
}

void CnrfProxy::run() {
  CnrfFrameRewriter rewriter(controller_);
  ByteQueue to_ftc, to_receiver;
  CnrfProxyStatus local;
  local.ready = true;
  uint32_t generation = 0;
  while (!stopping_) {
    handle_errors(0, rewriter, local);
    handle_errors(1, rewriter, local);
    transmit(0, to_ftc, rewriter, local);
    transmit(1, to_receiver, rewriter, local);
    for (unsigned i = 0; i < 32 && to_receiver.free(); ++i) {
      uint8_t byte;
      int count = uart_read_bytes(ports_[0], &byte, 1, 0);
      if (count < 0) { fault(rewriter, local); break; }
      if (count == 0) break;
      to_receiver.push(&byte, 1);
      ++local.ftc_bytes;
      local.ftc_last_rx = millis();
      local.ftc_seen = true;
    }
    for (unsigned i = 0; i < 32 && to_ftc.free() >= 22; ++i) {
      uint8_t byte;
      int count = uart_read_bytes(ports_[1], &byte, 1, 0);
      if (count < 0) { fault(rewriter, local); break; }
      if (count == 0) break;
      handle_errors(0, rewriter, local);
      handle_errors(1, rewriter, local);
      portENTER_CRITICAL(&mutex_);
      const auto now = millis();
      if (generation != disable_generation_) {
        generation = disable_generation_;
        rewriter.invalidate();
      }
      const auto enabled = override_.effective(now);
      const auto temperature = override_.temperature();
      auto out = rewriter.process(byte, now, enabled, temperature);
      portEXIT_CRITICAL(&mutex_);
      if (!to_ftc.push(out.bytes.data(), out.size)) fault(rewriter, local, true);
      ++local.receiver_bytes;
      local.receiver_last_rx = now;
      local.receiver_seen = true;
    }
    if (to_ftc.free() >= 22) {
      CnrfFrameRewriter::Output out;
      size_t buffered = 0;
      portENTER_CRITICAL(&mutex_);
      const auto now = millis();
      if (generation != disable_generation_) {
        generation = disable_generation_;
        rewriter.invalidate();
      }
      const auto enabled = override_.effective(now);
      const auto result = uart_get_buffered_data_len(ports_[1], &buffered);
      if (result == ESP_OK && buffered == 0 && uart_ll_get_rxfifo_len(UART_LL_GET_HW(ports_[1])) == 0)
        out = rewriter.tick(now, enabled);
      portEXIT_CRITICAL(&mutex_);
      if (result != ESP_OK) fault(rewriter, local);
      if (!to_ftc.push(out.bytes.data(), out.size)) fault(rewriter, local, true);
    }
    local.valid_frames = rewriter.temperature_frames();
    local.modified_frames = rewriter.modified_frames();
    local.invalid_frames = rewriter.invalid_frames();
    local.original_temperature = rewriter.last_original_temperature();
    local.output_temperature = rewriter.last_forwarded_temperature();
    portENTER_CRITICAL(&mutex_);
    local.uart_fault = status_.uart_fault;
    status_ = local;
    portEXIT_CRITICAL(&mutex_);
    vTaskDelay(1);
  }
  portENTER_CRITICAL(&mutex_);
  status_.ready = false;
  override_.disable();
  portEXIT_CRITICAL(&mutex_);
  stopped_ = true;
  vTaskDelete(nullptr);
}

}  // namespace esphome::ecodan
#endif
