#include "ecodan.h"
#include <string>
#include <algorithm>

namespace esphome {
namespace ecodan 
{
    void EcodanHeatpump::handle_initial_get_response(Message& res)
    {
        switch (res.payload_type<GetType>())
        {
        case GetType::THERMOSTAT_STATE_RES:
            if (res.type() == MsgType::THERMOSTAT_INITIAL_GET_RES) {
                status.Zone1SetTemperature = res.get_float8_v3(3);
                status.Zone2SetTemperature = res.get_float8_v3(13);
                status.set_operation_mode(res[2]);
                status.set_heating_cooling_mode(res[4]);
                status.RcMasterZone2 = res[14];
                status.RcMasterZone1 = res[9] & 0xf;
                status.update_target_temperatures();
            }
            break;
        default:
            if (static_cast<uint8_t>(res.payload_type<GetType>()) != 0) {
                ESP_LOGI(TAG, "Unknown response type received on serial port: %u", static_cast<uint8_t>(res.payload_type<GetType>()));
            }
            break;
        }
    }

    void EcodanHeatpump::handle_set_response(Message& res)
    {
        //ESP_LOGW(TAG, res.debug_dump_packet().c_str());
        if (!cmdQueue.empty())
            cmdQueue.pop();

        switch (res.payload_type<GetType>())
        {
        case GetType::THERMOSTAT_STATE_RES:
            status.Zone1SetTemperature = res.get_float8_v3(3);
            status.Zone2SetTemperature = res.get_float8_v3(12);
            status.set_operation_mode(res[2]);
            status.set_heating_cooling_mode(res[4]);
            status.RcMasterZone2 = res[13];
            status.RcMasterZone1 = res[9] & 0xf;
            status.update_target_temperatures();
            break;
        default:
            if (static_cast<uint8_t>(res.payload_type<GetType>()) != 0) {
                ESP_LOGI(TAG, "Unknown response type received on serial port: %u", static_cast<uint8_t>(res.payload_type<GetType>()));
            }
            break;
        }        
    }    

    void EcodanHeatpump::handle_connect_response(Message& res)
    {
        ESP_LOGI(TAG, "connection reply received from heat pump");

        connected = true;
    }

    void EcodanHeatpump::handle_response(Message& res) {
        switch (res.type())
        {
        case MsgType::THERMOSTAT_HOTWATER_SET_RES:
        case MsgType::THERMOSTAT_HOLIDAY_SET_RES:
        case MsgType::THERMOSTAT_TARGET_TEMP_SET_RES:
        case MsgType::THERMOSTAT_CURRENT_TEMP_SET_RES:
            handle_set_response(res);
            break;
        case MsgType::THERMOSTAT_INITIAL_GET_RES:
            handle_initial_get_response(res);
            break;

        case MsgType::CONNECT_RES:
            handle_connect_response(res);
            break;
        default:
            ESP_LOGI(TAG, "Unknown serial message type received: %#x", static_cast<uint8_t>(res.type()));
            break;
        }
    }
}
}
