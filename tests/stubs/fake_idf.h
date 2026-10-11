#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

using portMUX_TYPE = std::mutex;
inline thread_local std::function<void()> fake_before_lock;
inline void fake_enter_critical(std::mutex *mutex) {
  auto hook = std::move(fake_before_lock);
  fake_before_lock = nullptr;
  if (hook) hook();
  mutex->lock();
}
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(m) fake_enter_critical(m)
#define portEXIT_CRITICAL(m) (m)->unlock()
constexpr int pdPASS = 1, pdTRUE = 1;
inline std::atomic<uint32_t> fake_now{0};
inline uint32_t millis() { return fake_now.load(); }
inline std::thread proxy_thread;
inline thread_local bool fake_task_thread = false;
inline std::atomic<unsigned> fake_task_iterations{0};
inline std::function<void()> fake_task_delay_hook;
inline int xTaskCreate(void (*fn)(void *), const char *, int, void *self, int, void *) {
  proxy_thread = std::thread([=] { fake_task_thread = true; fn(self); });
  return pdPASS;
}
inline int xPortGetCoreID() { return 0; }
inline int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, int size, void *self, int priority, void *handle, int) {
  return xTaskCreate(fn, name, size, self, priority, handle);
}
inline void vTaskDelay(int) {
  if (fake_task_thread) {
    ++fake_task_iterations;
    auto hook = fake_task_delay_hook;
    if (hook) hook();
  }
  std::this_thread::sleep_for(std::chrono::microseconds(100));
}
inline void vTaskDelete(void *) {}
using uart_port_t = int;
constexpr int UART_NUM_MAX = 3, ESP_OK = 0;
constexpr int UART_DATA_8_BITS = 8, UART_PARITY_EVEN = 2, UART_STOP_BITS_1 = 1;
constexpr int UART_HW_FLOWCTRL_DISABLE = 0, UART_SCLK_DEFAULT = 0, UART_PIN_NO_CHANGE = -1;
constexpr int UART_FIFO_OVF = 1, UART_BUFFER_FULL = 2, UART_PARITY_ERR = 3, UART_FRAME_ERR = 4, UART_BREAK = 5;
struct uart_config_t { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; };
struct uart_event_t { int type; };
struct FakePort {
  std::mutex mutex;
  std::deque<uint8_t> rx;
  std::deque<uint8_t> fifo;
  std::vector<uint8_t> tx;
  std::deque<uart_event_t> events;
  int write_limit = 32;
  int rx_threshold = 120;
  bool fail_next_read = false;
};
inline FakePort fake_ports[2];
inline std::function<void(int, uint8_t)> fake_read_hook;
using QueueHandle_t = FakePort *;
inline bool uart_is_driver_installed(int) { return true; }
inline int uart_driver_delete(int) { return ESP_OK; }
inline int uart_driver_install(int port, int, int, int, QueueHandle_t *queue, int) {
  *queue = &fake_ports[port]; return ESP_OK;
}
inline int uart_param_config(int, const uart_config_t *) { return ESP_OK; }
inline int uart_set_pin(int, int, int, int, int) { return ESP_OK; }
inline int uart_set_rx_full_threshold(int index, int threshold) {
  fake_ports[index].rx_threshold = threshold;
  return ESP_OK;
}
inline int uart_set_rx_timeout(int, int) { return ESP_OK; }
inline int uart_get_buffered_data_len(int index, size_t *size) {
  std::lock_guard<std::mutex> lock(fake_ports[index].mutex);
  *size = fake_ports[index].rx.size();
  return ESP_OK;
}
inline int xQueueReceive(QueueHandle_t port, uart_event_t *event, int) {
  std::lock_guard<std::mutex> lock(port->mutex);
  if (port->events.empty()) return 0;
  *event = port->events.front(); port->events.pop_front(); return pdTRUE;
}
inline int uart_read_bytes(int index, uint8_t *byte, int, int) {
  auto &port = fake_ports[index];
  std::lock_guard<std::mutex> lock(port.mutex);
  if (port.fail_next_read) { port.fail_next_read = false; return -1; }
  if (port.rx.empty()) return 0;
  *byte = port.rx.front(); port.rx.pop_front();
  auto hook = fake_read_hook;
  if (hook) hook(index, *byte);
  return 1;
}
inline int uart_tx_chars(int index, const char *bytes, uint32_t size) {
  auto &port = fake_ports[index];
  std::lock_guard<std::mutex> lock(port.mutex);
  size = std::min<uint32_t>(port.write_limit, size);
  auto data = reinterpret_cast<const uint8_t *>(bytes);
  port.tx.insert(port.tx.end(), data, data + size);
  return size;
}
namespace esphome::uart {
class UARTComponent {};
class IDFUARTComponent : public UARTComponent {
 public:
  explicit IDFUARTComponent(int port) : port_(port) {}
  uint8_t get_hw_serial_number() const { return port_; }
 private:
  int port_;
};
}
