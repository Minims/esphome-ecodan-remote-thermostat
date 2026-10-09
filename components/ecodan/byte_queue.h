#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

class ByteQueue {
 public:
  std::size_t size() const { return size_; }
  std::size_t free() const { return bytes_.size() - size_; }
  const uint8_t *data() const { return bytes_.data() + head_; }
  std::size_t contiguous_size() const { return std::min(size_, bytes_.size() - head_); }
  bool push(const uint8_t *data, std::size_t size) {
    if (size > free()) return false;
    for (std::size_t i = 0; i < size; ++i) bytes_[(head_ + size_ + i) % bytes_.size()] = data[i];
    size_ += size;
    return true;
  }
  void consume(std::size_t size) {
    size = std::min(size, size_);
    head_ = (head_ + size) % bytes_.size();
    size_ -= size;
  }

 private:
  std::array<uint8_t, 512> bytes_{};
  std::size_t head_{0}, size_{0};
};
