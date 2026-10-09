#include <cassert>
#include <iostream>
#include "cnrf_proxy.h"

const std::vector<uint8_t> frame = {0xfc,0x48,0x04,0x03,0x10,0x27,0xa8,0xa8,0xff,0xff,0xff,0xff,0xff,0xff,0x03,0xff,0,0,0,0,0,0x2e};
void feed(int index, const std::vector<uint8_t> &bytes) {
  auto &port = fake_ports[index];
  std::lock_guard<std::mutex> lock(port.mutex);
  port.rx.insert(port.rx.end(), bytes.begin(), bytes.end());
}
std::vector<uint8_t> output(int index) {
  auto &port = fake_ports[index];
  std::lock_guard<std::mutex> lock(port.mutex);
  return port.tx;
}
template<typename Predicate> void await(Predicate ready) {
  auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!ready()) {
    assert(std::chrono::steady_clock::now() < limit);
    vTaskDelay(1);
  }
}
int main() {
  using namespace esphome::ecodan;
  esphome::uart::IDFUARTComponent ftc(0), receiver(1);
  CnrfProxy proxy;
  proxy.configure(1, 39, 38, 7, 8);
  assert(!proxy.apply(16, 0));
  assert(proxy.start(&ftc, &receiver));
  await([&] { return proxy.status().ready; });
  feed(0, frame); feed(1, frame);
  await([&] { return output(0).size() == 22 && output(1).size() == 22; });
  assert(output(0) == frame && output(1) == frame);
  assert(proxy.apply(16, 540000));
  feed(1, frame);
  await([&] { return output(0).size() == 44; });
  auto bytes = output(0);
  assert(bytes[28] == 0xa0 && bytes[43] == 0x36);
  assert(!proxy.apply(NAN, 0) && proxy.status().remaining_ms == 60000);
  fake_now = 60000;
  await([&] { return !proxy.status().effective; });
  feed(1, frame);
  await([&] { return output(0).size() == 66; });
  bytes = output(0);
  assert(std::equal(frame.begin(), frame.end(), bytes.begin() + 44));
  assert(proxy.apply(16, 0));
  {
    std::lock_guard<std::mutex> lock(fake_ports[1].mutex);
    fake_ports[1].events.push_back({UART_PARITY_ERR});
  }
  await([&] { return proxy.status().uart_fault; });
  assert(!proxy.status().effective);
  feed(1, frame);
  await([&] { return output(0).size() == 88; });
  bytes = output(0);
  assert(std::equal(frame.begin(), frame.end(), bytes.begin() + 66));
  assert(proxy.apply(17, 0) && !proxy.status().uart_fault);
  proxy.disable();
  assert(proxy.apply(16, 0));
  const auto before_disable_race = output(0).size();
  fake_read_hook = [&](int index, uint8_t byte) {
    if (index == 1 && byte == frame.back()) {
      fake_read_hook = nullptr;
      proxy.disable();
    }
  };
  feed(1, frame);
  await([&] { return output(0).size() == before_disable_race + frame.size(); });
  bytes = output(0);
  assert(std::equal(frame.begin(), frame.end(), bytes.begin() + before_disable_race));
  assert(!proxy.status().effective);
  // 1 000 trames dans chaque sens, émission partielle et snapshots concurrents.
  const auto before_bulk_ftc = output(0).size();
  const auto before_bulk_receiver = output(1).size();
  std::vector<uint8_t> bulk;
  for (unsigned i = 0; i < 1000; ++i) bulk.insert(bulk.end(), frame.begin(), frame.end());
  {
    std::lock_guard<std::mutex> lock(fake_ports[0].mutex);
    fake_ports[0].write_limit = 0;
  }
  feed(0, bulk); feed(1, bulk);
  await([&] { return output(1).size() == before_bulk_receiver + bulk.size(); });
  assert(output(0).size() == before_bulk_ftc); // l'autre sens avance même si TX FTC ne progresse pas
  {
    std::lock_guard<std::mutex> lock(fake_ports[0].mutex);
    fake_ports[0].write_limit = 7;
  }
  await([&] { return output(0).size() == before_bulk_ftc + bulk.size(); });
  bytes = output(0);
  assert(std::equal(bulk.begin(), bulk.end(), bytes.begin() + before_bulk_ftc));
  bytes = output(1);
  assert(std::equal(bulk.begin(), bulk.end(), bytes.begin() + before_bulk_receiver));
  proxy.stop();
  proxy_thread.join();
  assert(!proxy.status().ready && !proxy.apply(16, 0));
  std::cout << "Tâche proxy: 1 000 trames/sens, expiration, erreur UART, backpressure OK\n";
}
