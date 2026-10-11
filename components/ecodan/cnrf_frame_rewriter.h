#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

class CnrfFrameRewriter {
 public:
  struct Output {
    std::array<std::uint8_t, 22> bytes{};
    std::size_t size{0};
  };

  explicit CnrfFrameRewriter(std::uint8_t controller = 1) : controller_(controller) {}

  Output process(std::uint8_t input, std::uint32_t now, bool enabled, float temperature) {
    Output out;
    if (!enabled) invalidate();
    last_byte_ = now;
    if (quarantine_) {
      out.bytes[out.size++] = input;
      return out;
    }
    if (count_ == 0) {
      if (input != 0xfc && input != 0x02) {
        out.bytes[out.size++] = input;
        return out;
      }
      alternate_header_ = input == 0x02;
      candidate_ = !alternate_header_;
      eligible_ = candidate_ && enabled && valid_temperature(temperature) && controller_ >= 1 && controller_ <= 8;
      holding_ = eligible_;
      target_ = temperature;
      length_ = 0;
      sum_ = 0;
    }
    const std::size_t index = count_++;
    if (index < frame_.size()) frame_[index] = input;
    sum_ = static_cast<std::uint8_t>(sum_ + input);
    if (index == 1 && input != 0x48) candidate_ = false;
    if (!alternate_header_ && ((index == 2 && input != 0x04) || (index == 3 && input != 0x03))) {
      candidate_ = false;
      // Malformed header: do not trust embedded FC bytes until an idle gap.
      quarantine_ = true;
    }
    if (!alternate_header_ && index == 4) {
      length_ = input + 6U;
      if (input != 0x10) candidate_ = false;
    }
    if (alternate_header_ && index == 6) length_ = input + 8U;
    if (index == 5 && input != 0x27) candidate_ = false;

    if (holding_ && !candidate_) {
      release(out);
    } else if (!holding_) {
      out.bytes[out.size++] = input;
    }

    if (length_ != 0 && count_ == length_) {
      if (candidate_) {
        if (sum_ == 0xfc) {
          ++temperature_frames_;
          if (controller_ >= 1 && controller_ <= 8) {
            const auto position = 5U + controller_;
            const auto original = frame_[position];
            last_original_temperature_ = original == 0xff ? NAN : decode_temperature(original);
            last_forwarded_temperature_ = last_original_temperature_;
            if (eligible_ && original != 0xff) {
              const auto replacement = encode_temperature(target_);
              if (replacement != original) {
                frame_[position] = replacement;
                frame_[21] = static_cast<std::uint8_t>(frame_[21] + original - replacement);
                ++modified_frames_;
              }
              last_forwarded_temperature_ = decode_temperature(replacement);
            }
          }
        } else {
          ++invalid_frames_;
        }
      }
      release(out);
      reset();
    }
    return out;
  }

  Output tick(std::uint32_t now, bool enabled) {
    // The caller must verify that both the FIFO and RX buffer are empty.
    Output out;
    if (!enabled) invalidate();
    if ((count_ != 0 || quarantine_) && static_cast<std::uint32_t>(now - last_byte_) >= 20) {
      release(out);
      reset();
    }
    return out;
  }

  void invalidate() { eligible_ = false; }
  std::uint32_t temperature_frames() const { return temperature_frames_; }
  std::uint32_t modified_frames() const { return modified_frames_; }
  std::uint32_t invalid_frames() const { return invalid_frames_; }
  float last_original_temperature() const { return last_original_temperature_; }
  float last_forwarded_temperature() const { return last_forwarded_temperature_; }

  static bool valid_temperature(float temperature) {
    return std::isfinite(temperature) && temperature >= 10 && temperature <= 30;
  }
  static std::uint8_t encode_temperature(float temperature) {
    return static_cast<std::uint8_t>(std::lround(temperature * 2) + 128);
  }
  static float decode_temperature(std::uint8_t encoded) { return (encoded - 128) / 2.0F; }

 private:
  void release(Output &out) {
    if (!holding_) return;
    for (std::size_t i = 0; i < count_; ++i) out.bytes[out.size++] = frame_[i];
    holding_ = false;
  }
  void reset() {
    count_ = 0;
    quarantine_ = false;
    holding_ = false;
    eligible_ = false;
  }

  const std::uint8_t controller_;
  std::array<std::uint8_t, 22> frame_{};
  std::size_t count_{0}, length_{0};
  bool alternate_header_{false};
  bool candidate_{false}, eligible_{false}, holding_{false}, quarantine_{false};
  float target_{NAN};
  std::uint8_t sum_{0};
  std::uint32_t last_byte_{0};
  std::uint32_t temperature_frames_{0}, modified_frames_{0}, invalid_frames_{0};
  float last_original_temperature_{NAN}, last_forwarded_temperature_{NAN};
};
