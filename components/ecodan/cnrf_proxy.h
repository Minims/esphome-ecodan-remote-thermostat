#pragma once
#include "esphome/core/defines.h"
#ifdef USE_ECODAN_CNRF_PROXY

#include <atomic>
#include "esphome/components/uart/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "byte_queue.h"
#include "cnrf_frame_rewriter.h"
#include "override_state.h"

namespace esphome::ecodan {

struct CnrfProxyStatus {
  bool ready{false}, uart_fault{false}, requested{false}, effective{false};
  bool ftc_seen{false}, receiver_seen{false};
  uint32_t ftc_bytes{0}, receiver_bytes{0}, ftc_tx_bytes{0}, receiver_tx_bytes{0};
  uint32_t ftc_last_rx{0}, receiver_last_rx{0};
  uint32_t valid_frames{0}, modified_frames{0}, invalid_frames{0};
  uint32_t uart_errors{0}, overflows{0}, remaining_ms{0}, override_timeout_ms{0};
  float target_temperature{NAN}, original_temperature{NAN}, output_temperature{NAN};
};

class CnrfProxy {
 public:
  void configure(uint8_t controller, int ftc_tx, int ftc_rx, int receiver_tx, int receiver_rx,
                 uint32_t override_timeout_ms = OverrideState::DEFAULT_TIMEOUT_MS) {
    controller_ = controller;
    pins_[0] = ftc_tx; pins_[1] = ftc_rx; pins_[2] = receiver_tx; pins_[3] = receiver_rx;
    override_.set_timeout(override_timeout_ms);
  }
  bool start(uart::UARTComponent *ftc, uart::UARTComponent *receiver);
  void stop();
  bool apply(float temperature, int32_t sample_age_ms);
  void disable();
  CnrfProxyStatus status();

 private:
  static constexpr unsigned EVENT_QUEUE_SIZE = 544;
  static void task_entry(void *self) { static_cast<CnrfProxy *>(self)->run(); }
  void run();
  bool configure_port(unsigned index, uart::UARTComponent *component);
  void handle_errors(unsigned index, CnrfFrameRewriter &rewriter, CnrfProxyStatus &status);
  void fault(CnrfFrameRewriter &rewriter, CnrfProxyStatus &status, bool overflow = false);
  void transmit(unsigned index, ByteQueue &queue, CnrfFrameRewriter &rewriter, CnrfProxyStatus &status);

  portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
  OverrideState override_;
  CnrfProxyStatus status_;
  uint32_t disable_generation_{0};
  std::atomic<bool> stopping_{false}, stopped_{true};
  uart_port_t ports_[2]{UART_NUM_MAX, UART_NUM_MAX};
  QueueHandle_t events_[2]{nullptr, nullptr};
  int pins_[4]{};
  uint8_t controller_{1};
};

}  // namespace esphome::ecodan
#endif
