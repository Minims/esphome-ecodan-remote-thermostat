"""Compile the real temperature setter and response handlers without ESPHome."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "components/ecodan/ecodan.cpp").read_text()
# Isolate the setter because the rest of ecodan.cpp needs the ESPHome runtime.
setter = source[source.index("    void EcodanHeatpump::set_room_thermostat_current_temp("):
                source.index("    void EcodanHeatpump::publish_state(")]
responses = (ROOT / "components/ecodan/response.cpp").read_text().replace('#include "ecodan.h"', "")
harness = r'''
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <queue>
#include "proto.h"
#include "status.h"
#define ESP_LOGI(...)
namespace esphome::ecodan {
class EcodanHeatpump {
 public:
  bool proxy_mode_ = false;
  bool connected = false;
  Status status;
  std::queue<Message> cmdQueue;
  void set_room_thermostat_current_temp(float, uint8_t);
  void handle_initial_get_response(Message&);
  void handle_set_response(Message&);
  void handle_connect_response(Message&);
  void handle_response(Message&);
};
SETTER
}
RESPONSES
int main() {
  using namespace esphome::ecodan;
  EcodanHeatpump pump;
  pump.set_room_thermostat_current_temp(19.5f, 0);
  pump.set_room_thermostat_current_temp(21.0f, 7);
  assert(pump.status.CurrentRoomTemperatures[0] == 19.5f);
  assert(pump.status.CurrentRoomTemperatures[7] == 21.0f);
  pump.set_room_thermostat_current_temp(NAN, 0);
  assert(pump.status.CurrentRoomTemperatures[0] == 0xff);
  for (int room = 8; room <= 255; ++room) {
    pump.set_room_thermostat_current_temp(20.0f, room);
    pump.set_room_thermostat_current_temp(NAN, room);
  }
  for (float target : pump.status.TargetRoomTemperatures) assert(target == 0xff);
  assert(pump.status.CurrentRoomTemperatures[7] == 21.0f);
  pump.proxy_mode_ = true;
  pump.set_room_thermostat_current_temp(22.0f, 7);
  assert(pump.status.CurrentRoomTemperatures[7] == 21.0f);
  Message initial{MsgType::THERMOSTAT_INITIAL_GET_RES, GetType::THERMOSTAT_STATE_RES};
  initial.set_float8_v3(19.5f, 3);
  initial.set_float8_v3(21.0f, 13);
  pump.handle_response(initial);
  assert(pump.status.Zone1SetTemperature == 19.5f);
  assert(pump.status.Zone2SetTemperature == 21.0f);
  Message set{MsgType::THERMOSTAT_CURRENT_TEMP_SET_RES, GetType::THERMOSTAT_STATE_RES};
  set.set_float8_v3(20.0f, 3);
  set.set_float8_v3(22.0f, 12);
  pump.cmdQueue.emplace();
  pump.handle_response(set);
  assert(pump.cmdQueue.empty());
  assert(pump.status.Zone1SetTemperature == 20.0f);
  assert(pump.status.Zone2SetTemperature == 22.0f);
}
'''.replace("SETTER", setter).replace("RESPONSES", responses)
with tempfile.TemporaryDirectory(prefix="ecodan-tests-") as tmp:
    path = Path(tmp) / "test.cpp"
    binary = Path(tmp) / "test"
    path.write_text(harness)
    subprocess.run([
        "c++", "-std=c++17", "-O2", "-Werror=type-limits", "-Werror=array-bounds",
        "-Werror=empty-body", "-Werror=implicit-fallthrough", "-fsanitize=address,undefined",
        "-I", str(ROOT / "components/ecodan"), str(path), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
print("OK: températures, indices invalides, mode proxy et réponses sans avertissement")
