#include <cassert>
#include <iostream>
#include <string>
#include "cnrf_proxy.h"

using esphome::ecodan::CnrfProxy;
const std::vector<uint8_t> temperature_frame = {
    0xfc,0x48,0x04,0x03,0x10,0x27,0xa8,0xa8,0xff,0xff,0xff,
    0xff,0xff,0xff,0x03,0xff,0,0,0,0,0,0x2e};

template<typename Predicate> void await(Predicate ready) {
  const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!ready()) {
    assert(std::chrono::steady_clock::now() < limit);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
}
void cycle() {
  const auto before = fake_task_iterations.load();
  await([&] { return fake_task_iterations > before + 4; });
}
std::vector<uint8_t> output(int index) {
  std::lock_guard<std::mutex> lock(fake_ports[index].mutex);
  return fake_ports[index].tx;
}
void deliver(int index, const std::vector<uint8_t> &bytes) {
  std::lock_guard<std::mutex> lock(fake_ports[index].mutex);
  fake_ports[index].rx.insert(fake_ports[index].rx.end(), bytes.begin(), bytes.end());
}

void run_case(const std::string &name) {
  fake_now = 1000;
  fake_read_hook = nullptr;
  fake_task_delay_hook = nullptr;
  for (auto &port : fake_ports) {
    port.rx.clear(); port.fifo.clear(); port.tx.clear(); port.events.clear();
    port.write_limit = 32;
  }
  esphome::uart::IDFUARTComponent ftc(0), receiver(1);
  CnrfProxy proxy;
  proxy.configure(1, 39, 38, 7, 8, 900000);
  assert(proxy.start(&ftc, &receiver));
  await([&] { return proxy.status().ready; });
  assert(proxy.status().override_timeout_ms == 900000);

  if (name == "fifo") {
    for (int index : {0, 1}) {
      std::lock_guard<std::mutex> lock(fake_ports[index].mutex);
      auto &port = fake_ports[index];
      port.fifo.push_back(0x01);
      if (port.fifo.size() >= static_cast<size_t>(port.rx_threshold)) {
        port.rx.insert(port.rx.end(), port.fifo.begin(), port.fifo.end());
        port.fifo.clear();
      }
    }
    cycle();
    assert(output(0) == std::vector<uint8_t>{0x01});
    assert(output(1) == std::vector<uint8_t>{0x01});
  } else if (name == "delayed") {
    assert(proxy.apply(16, 0));
    std::vector<uint8_t> outer(166, 0x11);
    outer[0] = 0xfc; outer[1] = 0x49; outer[2] = 0x04; outer[3] = 0x03;
    outer[4] = 0xa0; outer[5] = 0x28;
    std::copy(temperature_frame.begin(), temperature_frame.end(), outer.begin() + 120);
    uint8_t sum = 0;
    for (size_t index = 0; index + 1 < outer.size(); ++index) sum += outer[index];
    outer.back() = static_cast<uint8_t>(0xfc - sum);
    deliver(1, {outer.begin(), outer.begin() + 120});
    await([&] { return proxy.status().receiver_bytes == 120; });
    {
      std::lock_guard<std::mutex> lock(fake_ports[1].mutex);
      fake_ports[1].fifo.insert(fake_ports[1].fifo.end(), outer.begin() + 120, outer.end());
    }
    fake_now = 1220;
    cycle();
    {
      std::lock_guard<std::mutex> lock(fake_ports[1].mutex);
      auto &port = fake_ports[1];
      port.rx.insert(port.rx.end(), port.fifo.begin(), port.fifo.end());
      port.fifo.clear();
    }
    await([&] { return output(0).size() == outer.size(); });
    assert(output(0) == outer);
    assert(proxy.status().modified_frames == 0);
  } else if (name == "continuous") {
    assert(proxy.apply(16, 0));
    std::vector<uint8_t> bytes;
    for (unsigned i = 0; i < 10; ++i)
      bytes.insert(bytes.end(), temperature_frame.begin(), temperature_frame.end());
    unsigned received = 0;
    fake_read_hook = [&](int index, uint8_t) {
      if (index == 1 && ++received == 121) fake_now = 1220;
    };
    deliver(1, bytes);
    await([&] { return output(0).size() == bytes.size(); });
    assert(proxy.status().modified_frames == 10);
    for (unsigned i = 0; i < 10; ++i) {
      bytes[i * 22 + 6] = 0xa0;
      bytes[i * 22 + 21] = 0x36;
    }
    assert(output(0) == bytes);
  } else if (name == "idle") {
    assert(proxy.apply(16, 0));
    const std::vector<uint8_t> prefix(temperature_frame.begin(), temperature_frame.begin() + 10);
    deliver(1, prefix);
    await([&] { return proxy.status().receiver_bytes == 10; });
    fake_now = 1019;
    cycle();
    assert(output(0).empty());
    fake_now = 1020;
    await([&] { return output(0) == prefix; });
    deliver(1, temperature_frame);
    await([&] { return output(0).size() == 32; });
    assert(proxy.status().modified_frames == 1);
  } else if (name == "queued_error") {
    assert(proxy.apply(16, 0));
    deliver(1, {temperature_frame.begin(), temperature_frame.end() - 1});
    await([&] { return proxy.status().receiver_bytes == 21; });
    {
      std::lock_guard<std::mutex> lock(fake_ports[1].mutex);
      for (unsigned i = 0; i < 500; ++i) fake_ports[1].events.push_back({0});
      fake_ports[1].events.push_back({UART_PARITY_ERR});
      fake_ports[1].rx.push_back(temperature_frame.back());
    }
    await([&] { return output(0).size() == 22; });
    assert(output(0) == temperature_frame);
    assert(proxy.status().uart_fault && !proxy.status().effective);
  } else if (name == "race_byte" || name == "race_idle") {
    std::atomic<bool> injected{false};
    auto inject = [&] {
      fake_now = 1001;
      assert(proxy.apply(19, 0));
      injected = true;
    };
    fake_read_hook = [&](int index, uint8_t) {
      if (index != 1) return;
      fake_read_hook = nullptr;
      if (name == "race_byte") fake_before_lock = inject;
      else fake_task_delay_hook = [&] {
        fake_task_delay_hook = nullptr;
        fake_before_lock = inject;
      };
    };
    deliver(1, {0x01});
    await([&] { return injected.load(); });
    cycle();
    const auto status = proxy.status();
    assert(status.effective && status.remaining_ms == 900000);
  }
  proxy.stop();
  proxy_thread.join();
  fake_read_hook = nullptr;
  fake_task_delay_hook = nullptr;
  std::cout << "Proxy regression: " << name << " OK\n";
}

int main(int argc, char **argv) {
  if (argc == 2) run_case(argv[1]);
  else for (const auto &name : {"fifo", "delayed", "continuous", "idle", "queued_error", "race_byte", "race_idle"})
    run_case(name);
}
