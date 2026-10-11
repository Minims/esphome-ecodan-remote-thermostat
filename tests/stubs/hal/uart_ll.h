#pragma once
#include "fake_idf.h"
#define UART_LL_GET_HW(port) (port)
inline size_t uart_ll_get_rxfifo_len(int port) {
  std::lock_guard<std::mutex> lock(fake_ports[port].mutex);
  return fake_ports[port].fifo.size();
}
