#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include "byte_queue.h"

int main() {
  ByteQueue queue;
  std::vector<uint8_t> expected, actual;
  for (unsigned cycle = 0; cycle < 20; ++cycle) {
    while (queue.free() > 0) {
      auto value = static_cast<uint8_t>(expected.size());
      assert(queue.push(&value, 1));
      expected.push_back(value);
    }
    uint8_t excess = 42;
    assert(!queue.push(&excess, 1));
    for (unsigned capacity : {0U, 1U, 7U, 32U, 512U}) {
      while (capacity && queue.size()) {
        size_t n = std::min<size_t>(capacity, queue.contiguous_size());
        actual.insert(actual.end(), queue.data(), queue.data() + n);
        queue.consume(n);
        capacity -= n;
      }
    }
  }
  while (queue.size()) {
    actual.push_back(*queue.data());
    queue.consume(1);
  }
  assert(actual == expected);
  std::cout << "File UART: écritures nulles/partielles, saturation et ordre OK\n";
}
