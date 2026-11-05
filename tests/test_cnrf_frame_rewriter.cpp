#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "cnrf_frame_rewriter.h"

namespace {

constexpr std::array<std::uint8_t, 22> kNormalFrame = {
    0xfc, 0x48, 0x04, 0x03, 0x10, 0x27, 0xa8, 0xa8, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0x03, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2e};

// Captured on the PAR-WR61R-E: setpoint 16.5 C, RC1 room temperature 23 C.
constexpr std::array<std::uint8_t, 22> kSetpointFrame = {
    0xfc, 0x49, 0x04, 0x03, 0x10, 0x28, 0xa1, 0xae, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0xff, 0x00, 0x00, 0x00, 0x30};

std::uint8_t checksum(const std::array<std::uint8_t, 22> &frame) {
  std::uint8_t sum = 0;
  for (std::size_t i = 0; i < frame.size() - 1; ++i) {
    sum = static_cast<std::uint8_t>(sum + frame[i]);
  }
  return static_cast<std::uint8_t>(0xfc - sum);
}

template <std::size_t N>
std::array<std::uint8_t, N> rewrite(
    const std::array<std::uint8_t, N> &input, CnrfFrameRewriter &rewriter,
    bool enabled, float temperature) {
  std::array<std::uint8_t, N> output{};
  std::size_t written = 0;
  for (std::size_t i = 0; i < input.size(); ++i) {
    static std::uint32_t now = 0;
    const auto chunk = rewriter.process(input[i], now++, enabled, temperature);
    for (std::size_t j = 0; j < chunk.size; ++j) output.at(written++) = chunk.bytes[j];
  }
  assert(written == N);
  return output;
}

void test_passthrough() {
  CnrfFrameRewriter rewriter;
  const auto output = rewrite(kNormalFrame, rewriter, false, 16.0F);
  assert(output == kNormalFrame);
  assert(rewriter.temperature_frames() == 1);
  assert(rewriter.modified_frames() == 0);
}

void test_rc1_override_and_checksum() {
  CnrfFrameRewriter rewriter(1);
  const auto output = rewrite(kNormalFrame, rewriter, true, 16.0F);
  assert(output[6] == 0xa0);
  assert(output[7] == 0xa8);
  assert(output[21] == 0x36);
  assert(rewriter.modified_frames() == 1);
  assert(std::fabs(rewriter.last_original_temperature() - 20.0F) < 0.01F);
  assert(std::fabs(rewriter.last_forwarded_temperature() - 16.0F) < 0.01F);
}

void test_rc2_override() {
  CnrfFrameRewriter rewriter(2);
  const auto output = rewrite(kNormalFrame, rewriter, true, 17.0F);
  assert(output[6] == 0xa8);
  assert(output[7] == 0xa2);
  assert(output[21] == 0x34);
}

void test_half_degree_quantization() {
  assert(CnrfFrameRewriter::encode_temperature(16.3F) == 0xa1);
  assert(CnrfFrameRewriter::encode_temperature(16.2F) == 0xa0);
}

void test_invalid_temperature_is_ignored() {
  CnrfFrameRewriter rewriter;
  const auto output = rewrite(kNormalFrame, rewriter, true, 31.0F);
  assert(output == kNormalFrame);
  assert(rewriter.modified_frames() == 0);
}

void test_other_packet_is_untouched() {
  auto input = kNormalFrame;
  input[1] = 0x49;
  input[21] = static_cast<std::uint8_t>(input[21] - 1);
  CnrfFrameRewriter rewriter;
  const auto output = rewrite(input, rewriter, true, 16.0F);
  assert(output == input);
}

void test_captured_setpoints_keep_requested_temperature() {
  for (const auto setpoint : {0xa1, 0xa0}) {
    auto input = kSetpointFrame;
    input[6] = setpoint;
    input[21] = setpoint == 0xa1 ? 0x30 : 0x31;
    auto expected = input;
    expected[7] = 0xa4;  // 18 C, independent of the requested setpoint.
    expected[21] = setpoint == 0xa1 ? 0x3a : 0x3b;
    CnrfFrameRewriter rewriter;
    assert(rewrite(input, rewriter, true, 18) == expected);
    assert(rewriter.last_original_temperature() == 23);
    assert(rewriter.last_forwarded_temperature() == 18);
  }
}

void test_setpoint_controller_positions_and_absence() {
  for (const auto controller : {1, 2, 8}) {
    const std::size_t position = controller == 1 ? 7 : controller == 2 ? 8 : 14;
    auto input = kSetpointFrame;
    input[position] = 0xa8;
    input[21] = checksum(input);
    CnrfFrameRewriter rewriter(controller);
    const auto output = rewrite(input, rewriter, true, 18);
    assert(output[position] == 0xa4 && output[21] == checksum(output));
    for (std::size_t i = 0; i < input.size(); ++i)
      if (i != position && i != 21) assert(output[i] == input[i]);
    input[position] = 0xff;
    input[21] = checksum(input);
    assert(rewrite(input, rewriter, true, 18) == input);
  }
}

void test_setpoint_passthrough_guards() {
  CnrfFrameRewriter rewriter;
  assert(rewrite(kSetpointFrame, rewriter, false, 18) == kSetpointFrame);
  assert(rewrite(kSetpointFrame, rewriter, true, NAN) == kSetpointFrame);
  auto corrupt = kSetpointFrame;
  corrupt[21] ^= 1;
  assert(rewrite(corrupt, rewriter, true, 18) == corrupt);
  auto mismatched = kSetpointFrame;
  mismatched[1] = 0x48;
  mismatched[21] = checksum(mismatched);
  assert(rewrite(mismatched, rewriter, true, 18) == mismatched);
}

void test_full_temperature_range() {
  CnrfFrameRewriter rewriter;
  for (std::uint8_t original = 0x94; original <= 0xbc; ++original) {
    for (std::uint8_t target = 0x94; target <= 0xbc; ++target) {
      auto input = kNormalFrame;
      input[6] = original;
      input[21] = checksum(input);

      const float target_temperature =
          CnrfFrameRewriter::decode_temperature(target);
      const auto output = rewrite(input, rewriter, true, target_temperature);

      assert(output[6] == target);
      assert(output[21] == checksum(output));
      for (std::size_t i = 0; i < output.size(); ++i) {
        if (i != 6 && i != 21) {
          assert(output[i] == input[i]);
        }
      }
    }
  }
}

void test_missing_controller_is_not_created() {
  auto input = kNormalFrame;
  input[6] = 0xff;
  input[21] = checksum(input);
  CnrfFrameRewriter rewriter;
  const auto output = rewrite(input, rewriter, true, 16.0F);
  assert(output == input);
}

void append(std::vector<std::uint8_t> &out, const CnrfFrameRewriter::Output &chunk) {
  out.insert(out.end(), chunk.bytes.begin(), chunk.bytes.begin() + chunk.size);
}

void test_setpoint_interruption() {
  for (std::size_t split = 1; split < kSetpointFrame.size(); ++split) {
    CnrfFrameRewriter rewriter;
    std::vector<std::uint8_t> output;
    for (std::size_t i = 0; i < split; ++i)
      append(output, rewriter.process(kSetpointFrame[i], i, true, 18));
    assert(output.empty());
    append(output, rewriter.tick(split + 20, true));
    assert(output == std::vector<std::uint8_t>(kSetpointFrame.begin(), kSetpointFrame.begin() + split));
  }
  CnrfFrameRewriter rewriter;
  std::vector<std::uint8_t> output;
  for (std::size_t i = 0; i < kSetpointFrame.size(); ++i) {
    if (i == 10) rewriter.invalidate();
    append(output, rewriter.process(kSetpointFrame[i], i, true, 18));
  }
  assert(output == std::vector<std::uint8_t>(kSetpointFrame.begin(), kSetpointFrame.end()));
}

void test_stream_boundaries() {
  for (std::size_t split = 1; split < kNormalFrame.size(); ++split) {
    CnrfFrameRewriter r;
    std::vector<std::uint8_t> out;
    std::uint32_t now = 0;
    for (std::size_t i = 0; i < split; ++i)
      append(out, r.process(kNormalFrame[i], now++, true, 16));
    assert(out.empty());
    append(out, r.tick(now + 20, true));
    assert(out == std::vector<std::uint8_t>(kNormalFrame.begin(), kNormalFrame.begin() + split));
    now += 21;
    for (auto byte : kNormalFrame) append(out, r.process(byte, now++, true, 16));
    assert(out.size() == split + 22 && out[split + 6] == 0xa0 && out.back() == 0x36);
  }
  for (int mode = 0; mode < 3; ++mode) {
    CnrfFrameRewriter r;
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < 22; ++i) {
      if (i == 10 && mode == 2) r.invalidate();
      bool enabled = mode == 0 ? i < 10 : mode == 1 ? i >= 10 : true;
      append(out, r.process(kNormalFrame[i], i, enabled, 16));
    }
    assert(out == std::vector<std::uint8_t>(kNormalFrame.begin(), kNormalFrame.end()));
  }
  CnrfFrameRewriter r;
  std::vector<std::uint8_t> out;
  for (std::size_t i = 0; i < 22; ++i)
    append(out, r.process(kNormalFrame[i], i, true, i == 0 ? 16 : 25));
  assert(out[6] == 0xa0);
}

void test_unknown_frames_and_noise() {
  std::vector<std::uint8_t> bytes = {0x01, 0x02, 0xfc, 0x49, 0x04, 0x03, 22};
  bytes.insert(bytes.end(), kNormalFrame.begin(), kNormalFrame.end());
  bytes.push_back(0x00);
  CnrfFrameRewriter r;
  std::vector<std::uint8_t> out;
  std::uint32_t now = 0;
  for (auto byte : bytes) append(out, r.process(byte, now++, true, 16));
  append(out, r.tick(now + 20, true));
  assert(out == bytes && r.modified_frames() == 0);
  for (std::size_t pos : {1U, 2U, 3U, 4U, 5U}) {
    auto frame = kNormalFrame;
    frame[pos] ^= 1;
    CnrfFrameRewriter other;
    out.clear();
    for (auto b : frame) append(out, other.process(b, now++, true, 16));
    append(out, other.tick(now + 20, true));
    assert(out == std::vector<std::uint8_t>(frame.begin(), frame.end()));
  }
  std::vector<std::uint8_t> alternate = {0x02, 0xff, 0xff, 0, 0, 0, 22};
  alternate.insert(alternate.end(), kNormalFrame.begin(), kNormalFrame.end());
  alternate.push_back(0);
  CnrfFrameRewriter variant;
  out.clear();
  for (auto b : alternate) append(out, variant.process(b, now++, true, 16));
  append(out, variant.tick(now + 20, true));
  assert(out == alternate && variant.modified_frames() == 0);
  CnrfFrameRewriter transparent;
  for (auto b : kNormalFrame) {
    auto chunk = transparent.process(b, now++, false, 16);
    assert(chunk.size == 1 && chunk.bytes[0] == b);
  }
  assert(transparent.temperature_frames() == 1);
}

void test_bad_checksum_diagnostics_and_wrap() {
  auto frame = kNormalFrame;
  frame[21] ^= 1;
  CnrfFrameRewriter r;
  assert(rewrite(frame, r, true, 16) == frame);
  assert(r.invalid_frames() == 1 && r.temperature_frames() == 0);
  assert(std::isnan(r.last_original_temperature()));
  CnrfFrameRewriter wrap;
  assert(wrap.process(0xfc, UINT32_MAX - 5, true, 16).size == 0);
  auto out = wrap.tick(14, true);
  assert(out.size == 1 && out.bytes[0] == 0xfc);
}

}  // namespace

int main() {
  test_captured_setpoints_keep_requested_temperature();
  test_setpoint_controller_positions_and_absence();
  test_setpoint_passthrough_guards();
  test_setpoint_interruption();
  // An override must never repair a corrupted incoming frame.
  auto corrupt = kNormalFrame;
  corrupt[21] ^= 1;
  CnrfFrameRewriter corrupt_rewriter;
  assert(rewrite(corrupt, corrupt_rewriter, true, 16.0F) == corrupt);
  test_stream_boundaries();
  test_unknown_frames_and_noise();
  test_bad_checksum_diagnostics_and_wrap();
  test_passthrough();
  test_rc1_override_and_checksum();
  test_rc2_override();
  test_half_degree_quantization();
  test_invalid_temperature_is_ignored();
  test_other_packet_is_untouched();
  test_full_temperature_range();
  test_missing_controller_is_not_created();
  std::cout << "All CNRF frame rewriter tests passed.\n";
  return 0;
}
