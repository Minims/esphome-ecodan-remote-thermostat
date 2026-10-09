#pragma once
#include <cmath>
#include <cstdint>

class OverrideState {
 public:
  static constexpr uint32_t DEFAULT_TIMEOUT_MS = 600000;

  explicit OverrideState(uint32_t timeout_ms = DEFAULT_TIMEOUT_MS) : timeout_ms_(timeout_ms) {}

  void set_timeout(uint32_t timeout_ms) { timeout_ms_ = timeout_ms; }

  bool accept(float temperature, int32_t sample_age_ms, uint32_t now) {
    if (!std::isfinite(temperature) || temperature < 10 || temperature > 30 ||
        sample_age_ms < 0 || sample_age_ms >= static_cast<int32_t>(timeout_ms_)) return false;
    temperature_ = std::round(temperature * 2) / 2;
    accepted_at_ = now;
    lifetime_ = timeout_ms_ - sample_age_ms;
    requested_ = active_ = true;
    return true;
  }
  void disable() { requested_ = active_ = false; }
  bool effective(uint32_t now) {
    if (active_ && static_cast<uint32_t>(now - accepted_at_) >= lifetime_) active_ = false;
    return active_;
  }
  uint32_t remaining_ms(uint32_t now) {
    return effective(now) ? lifetime_ - static_cast<uint32_t>(now - accepted_at_) : 0;
  }
  bool requested() const { return requested_; }
  float temperature() const { return temperature_; }

 private:
  bool requested_{false}, active_{false};
  float temperature_{NAN};
  uint32_t timeout_ms_{DEFAULT_TIMEOUT_MS}, accepted_at_{0}, lifetime_{0};
};
