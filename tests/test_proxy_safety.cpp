#include <cassert>
#include <iostream>
#include <string>
#include "cnrf_proxy.h"

using esphome::ecodan::CnrfProxy;
const std::vector<uint8_t> normal_frame = {
    0xfc,0x48,0x04,0x03,0x10,0x27,0xa8,0xa8,0xff,0xff,0xff,
    0xff,0xff,0xff,0x03,0xff,0,0,0,0,0,0x2e};

const std::vector<uint8_t> setpoint_frame = {
    0xfc,0x49,0x04,0x03,0x10,0x28,0xa1,0xae,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0x00,0x01,0xff,0x00,0x00,0x00,0x30};

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

void run_case(const std::string &name, bool setpoint = false) {
  const auto &temperature_frame = setpoint ? setpoint_frame : normal_frame;
  const size_t room_position = setpoint ? 7 : 6;
  const uint8_t checksum16 = setpoint ? 0x3e : 0x36;
  const uint8_t checksum19 = setpoint ? 0x38 : 0x30;
  fake_now = 1000;
  fake_read_hook = nullptr;
  fake_task_delay_hook = nullptr;
  for (auto &port : fake_ports) {
    port.rx.clear(); port.fifo.clear(); port.tx.clear(); port.events.clear();
    port.write_limit = 32;
    port.fail_enable_interrupt = false;
  }
  const bool queued_case = name == "queued_expiry" || name == "queued_disable" || name == "queued_replace" ||
                           name == "queued_timeout" || name == "queued_fault" || name == "partial_expiry";
  std::atomic<bool> resume_tx{false};
  if (queued_case) {
    fake_ports[0].write_limit = name == "partial_expiry" ? 7 : 0;
    fake_task_delay_hook = [&] {
      std::lock_guard<std::mutex> lock(fake_ports[0].mutex);
      if (resume_tx) fake_ports[0].write_limit = 7;
      else if (!fake_ports[0].tx.empty()) fake_ports[0].write_limit = 0;
    };
  }
  esphome::uart::IDFUARTComponent ftc(0), receiver(1);
  CnrfProxy proxy;
  const bool fault_before_tx = name == "fault_before_tx_ftc" || name == "fault_before_tx_receiver";
  std::atomic<bool> fault_injected{false};
  if (fault_before_tx) {
    fake_task_delay_hook = [&] {
      if (fault_injected || proxy.status().modified_frames != 1) return;
      fake_before_lock = [&] {
        assert(output(0).empty());
        auto &port = fake_ports[name == "fault_before_tx_ftc" ? 0 : 1];
        std::lock_guard<std::mutex> lock(port.mutex);
        port.events.push_back({UART_PARITY_ERR});
        fault_injected = true;
      };
    };
  }
  proxy.configure(1, 39, 38, 7, 8, 900000);
  if (name == "interrupt_failure_ftc" || name == "interrupt_failure_receiver") {
    fake_ports[name == "interrupt_failure_ftc" ? 0 : 1].fail_enable_interrupt = true;
    assert(!proxy.start(&ftc, &receiver));
    assert(!proxy.status().ready && !proxy.apply(16, 0));
    std::cout << "Proxy regression: " << name << (setpoint ? " (setpoint)" : " (measurement)") << " OK\n";
    return;
  }
  assert(proxy.start(&ftc, &receiver));
  await([&] { return proxy.status().ready; });
  assert(proxy.status().override_timeout_ms == 900000);

  if (fault_before_tx) {
    assert(proxy.apply(16, 0));
    deliver(1, temperature_frame);
    await([&] { return output(0).size() == temperature_frame.size() && proxy.status().uart_fault; });
    assert(fault_injected && !proxy.status().effective);
    assert(output(0) == temperature_frame);
  } else if (queued_case) {
    assert(proxy.apply(16, 899950));
    deliver(1, temperature_frame);
    await([&] { return proxy.status().modified_frames == 1; });
    if (name == "partial_expiry") {
      await([&] { return output(0).size() == 7; });
      cycle();
    } else assert(output(0).empty());
    if (name == "queued_disable") proxy.disable();
    else if (name == "queued_replace") assert(proxy.apply(19, 0));
    else if (name == "queued_timeout") assert(proxy.set_timeout(300));
    else if (name == "queued_fault") {
      {
        std::lock_guard<std::mutex> lock(fake_ports[1].mutex);
        fake_ports[1].events.push_back({UART_PARITY_ERR});
      }
      await([&] { return proxy.status().uart_fault; });
    } else fake_now = 1050;
    cycle();
    if (name != "queued_replace") assert(!proxy.status().effective);
    resume_tx = true;
    await([&] { return output(0).size() == 22; });
    auto expected = temperature_frame;
    if (name == "partial_expiry") { expected[room_position] = 0xa0; expected[21] = checksum16; }
    assert(output(0) == expected);
    deliver(1, temperature_frame);
    await([&] { return output(0).size() == 44; });
    expected = temperature_frame;
    if (name == "queued_replace") { expected[room_position] = 0xa6; expected[21] = checksum19; }
    const auto bytes = output(0);
    assert(std::equal(expected.begin(), expected.end(), bytes.begin() + 22));
  } else if (name == "queued_order") {
    assert(proxy.apply(16, 0));
    {
      std::lock_guard<std::mutex> lock(fake_ports[0].mutex);
      fake_ports[0].write_limit = 0;
    }
    std::vector<uint8_t> stream{0x11, 0x12};
    stream.insert(stream.end(), temperature_frame.begin(), temperature_frame.end());
    stream.push_back(0x13);
    stream.insert(stream.end(), temperature_frame.begin(), temperature_frame.end());
    deliver(1, stream);
    await([&] { return proxy.status().modified_frames == 1; });
    proxy.disable();
    {
      std::lock_guard<std::mutex> lock(fake_ports[0].mutex);
      fake_ports[0].write_limit = 1;
    }
    await([&] { return output(0).size() == stream.size(); });
    assert(output(0) == stream);
  } else if (name == "timeout_runtime") {
    assert(proxy.apply(16, 0));
    for (float invalid : {NAN, INFINITY, -1.0F, 0.0F, 0.5F, 2073601.0F}) {
      assert(!proxy.set_timeout(invalid));
      assert(proxy.status().override_timeout_ms == 900000);
      assert(proxy.status().effective);
    }
    assert(proxy.set_timeout(900));
    assert(proxy.status().effective);
    assert(proxy.set_timeout(300));
    assert(proxy.status().override_timeout_ms == 300000);
    assert(!proxy.status().effective && !proxy.status().requested);
    assert(!proxy.apply(16, 300000));
    assert(proxy.apply(16, 299000));
    fake_now = 1999;
    assert(proxy.status().remaining_ms == 1);
    fake_now = 2000;
    assert(!proxy.status().effective);
    assert(proxy.set_timeout(900));
    assert(!proxy.status().effective);
    assert(proxy.apply(16, 840000));
    assert(proxy.status().remaining_ms == 60000);
    assert(proxy.set_timeout(1));
    assert(proxy.apply(16, 0));
    assert(proxy.status().remaining_ms == 1000);
    assert(proxy.set_timeout(2073600));
    assert(proxy.apply(16, 2073599000));
    assert(proxy.status().remaining_ms == 1000);
  } else if (name == "timeout_pending") {
    assert(proxy.apply(16, 0));
    unsigned received = 0;
    fake_read_hook = [&](int index, uint8_t) {
      if (index != 1 || ++received != 11) return;
      assert(proxy.set_timeout(300));
      assert(proxy.apply(19, 0));
    };
    deliver(1, temperature_frame);
    await([&] { return output(0).size() == 22; });
    assert(output(0) == temperature_frame);
    deliver(1, temperature_frame);
    await([&] { return output(0).size() == 44; });
    auto expected = temperature_frame;
    expected[room_position] = 0xa6; expected[21] = checksum19;
    const auto bytes = output(0);
    assert(std::equal(expected.begin(), expected.end(), bytes.begin() + 22));
  } else if (name == "fifo") {
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
      bytes[i * 22 + room_position] = 0xa0;
      bytes[i * 22 + 21] = checksum16;
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
  } else if (name == "renew_expired" || name == "replace_pending" || name == "reject_pending") {
    assert(proxy.apply(16, name == "renew_expired" ? 899950 : 0));
    unsigned received = 0;
    fake_read_hook = [&](int index, uint8_t) {
      if (index != 1 || ++received != 11) return;
      fake_now = 1060;
      if (name == "reject_pending") assert(!proxy.apply(NAN, 0));
      else assert(proxy.apply(19, 0));
    };
    deliver(1, temperature_frame);
    await([&] { return output(0).size() == 22; });
    auto expected = temperature_frame;
    if (name == "reject_pending") { expected[room_position] = 0xa0; expected[21] = checksum16; }
    assert(output(0) == expected);
    deliver(1, temperature_frame);
    await([&] { return output(0).size() == 44; });
    if (name != "reject_pending") { expected[room_position] = 0xa6; expected[21] = checksum19; }
    const auto bytes = output(0);
    assert(std::equal(expected.begin(), expected.end(), bytes.begin() + 22));
  } else if (name == "frame_error_ftc" || name == "frame_error_receiver") {
    assert(proxy.apply(16, 0));
    deliver(1, {temperature_frame.begin(), temperature_frame.end() - 1});
    await([&] { return proxy.status().receiver_bytes == 21; });
    {
      auto &port = fake_ports[name == "frame_error_ftc" ? 0 : 1];
      std::lock_guard<std::mutex> lock(port.mutex);
      if (port.interrupt_mask & UART_INTR_FRAM_ERR) port.events.push_back({UART_FRAME_ERR});
    }
    deliver(1, {temperature_frame.back()});
    await([&] { return output(0).size() == 22; });
    assert(output(0) == temperature_frame);
    assert(proxy.status().uart_fault && !proxy.status().effective);
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
  std::cout << "Proxy regression: " << name << (setpoint ? " (setpoint)" : " (measurement)") << " OK\n";
}

int main(int argc, char **argv) {
  if (argc == 2) run_case(argv[1]);
  else for (const auto &name : {"fifo", "delayed", "continuous", "idle", "queued_error", "race_byte", "race_idle",
                           "renew_expired", "replace_pending", "reject_pending", "frame_error_ftc",
                           "frame_error_receiver", "interrupt_failure_ftc", "interrupt_failure_receiver",
                           "timeout_runtime", "timeout_pending", "queued_expiry", "queued_disable", "queued_replace",
                           "queued_timeout", "queued_fault", "partial_expiry", "queued_order",
                           "fault_before_tx_ftc", "fault_before_tx_receiver"})
    {
      run_case(name);
      run_case(name, true);
    }
}
