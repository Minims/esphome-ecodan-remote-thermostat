#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include "override_state.h"

int main() {
  OverrideState state;
  assert(!state.effective(0));
  assert(state.accept(16.3F, 540000, 1000));
  assert(state.temperature() == 16.5F);
  assert(state.remaining_ms(1000) == 60000);
  for (float invalid : {NAN, INFINITY, 9.9F, 30.1F}) {
    assert(!state.accept(invalid, 0, 2000));
    assert(state.remaining_ms(2000) == 59000);
  }
  assert(!state.accept(16, -1, 2000));
  assert(!state.accept(16, 600000, 2000));
  assert(!state.accept(16, INT32_MAX, 2000));
  assert(state.accept(16.3F, 570000, 31000));
  assert(state.effective(60999));
  assert(!state.effective(61000));
  assert(!state.effective(31000)); // A millis wrap must not reactivate an expired override.
  assert(state.remaining_ms(31000) == 0);
  assert(state.accept(10, 0, UINT32_MAX - 100));
  assert(state.effective(200));
  assert(state.remaining_ms(200) == 599699);
  state.disable();
  assert(!state.effective(201) && !state.requested());
  assert(state.accept(30, 599999, 201));
  assert(state.effective(201) && !state.effective(202));

  OverrideState fifteen_minutes{900000};
  assert(fifteen_minutes.accept(20, 840000, 0));
  assert(fifteen_minutes.remaining_ms(0) == 60000);
  assert(!fifteen_minutes.accept(20, 900000, 0));
  std::cout << "Override: validation, fraîcheur, expiration et wrap OK\n";
}
