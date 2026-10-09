#include "ecodan.h"

#include "esphome.h"

#include <functional>
#include <queue>

namespace esphome {
namespace ecodan 
{       
    void EcodanHeatpump::set_room_thermostat_target_temp(float temp, ClimateRoomIdentifier room) {
        if (proxy_mode_) return;
        if (temp != NAN) {
            auto room_index = static_cast<uint8_t>(room);
            status.TargetRoomTemperatures[room_index] = temp;

            Message cmd{MsgType::THERMOSTAT_TARGET_TEMP_SET, SetType::THERMOSTAT_TEMPERATURE_SETTINGS};
            
            // set new target temp
            cmd.set_float8_v3(round_nearest_half(status.TargetRoomTemperatures[room_index]), 1);
            
            // include current temps in msg
            for (auto i = 0; i < MAX_REMOTE_THERMOSTATS; i++) {
                if (CHECK_BIT(static_cast<uint8_t>(status.RcMask), i)){
                    auto roundedTemp = round_nearest_half(status.CurrentRoomTemperatures[i]);
                    cmd.set_float8_v3(roundedTemp, 2+i);
                    
                }
                else {
                    cmd[2+i] = 0xff;
                }
            }
            cmd[11] = static_cast<uint8_t>(status.RcMask);
            cmd[12] = 0xff;

            // for z2 writes, we need to enable byte 14
            if (status.RcMasterZone2 != 0x0 && room_index+1 == status.RcMasterZone2) {
                cmd[14] = 1;   
            }
            schedule_cmd(cmd);
        }
    }

    bool EcodanHeatpump::schedule_cmd(Message& cmd)
    {   
        if (proxy_mode_) return false;
        cmdQueue.emplace(std::move(cmd));
        return dispatch_next_cmd();
    }

    void EcodanHeatpump::update_room_mask(uint8_t room_identifier) {
        auto room = static_cast<ClimateRoomIdentifier>(room_identifier);
        switch(room) {
            case ClimateRoomIdentifier::ROOM_0:
                status.RcMask |= Status::RCMASK::RC0;
            break;
            case ClimateRoomIdentifier::ROOM_1:
                status.RcMask |= Status::RCMASK::RC1;
            break;
            case ClimateRoomIdentifier::ROOM_2:
                status.RcMask |= Status::RCMASK::RC2;
            break;
            case ClimateRoomIdentifier::ROOM_3:
                status.RcMask |= Status::RCMASK::RC3;
            break;
            case ClimateRoomIdentifier::ROOM_4:
                status.RcMask |= Status::RCMASK::RC4;
            break;
            case ClimateRoomIdentifier::ROOM_5:
                status.RcMask |= Status::RCMASK::RC5;
            break;
            case ClimateRoomIdentifier::ROOM_6:
                status.RcMask |= Status::RCMASK::RC6;
            break;
            case ClimateRoomIdentifier::ROOM_7:
                status.RcMask |= Status::RCMASK::RC7;
            break;
        }
    }

    #define MAX_STATUS_CMD_SIZE 2
    Message statusCmdQueue[MAX_STATUS_CMD_SIZE] = {
        // initial request
        Message{MsgType::THERMOSTAT_INITIAL_GET, GetType::THERMOSTAT_STATE_A},
        //Message{MsgType::THERMOSTAT_INITIAL_GET, std::array<char, PAYLOAD_SIZE> { static_cast<char>(GetType::THERMOSTAT_STATE_A), 0x03, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        Message{MsgType::THERMOSTAT_CURRENT_TEMP_SET, GetType::THERMOSTAT_STATE_B} 
    };

    bool EcodanHeatpump::dispatch_next_status_cmd()
    {
        if (proxy_available())
            return true;
        
        auto static cmdIndex = 0;
        Message& cmd = statusCmdQueue[cmdIndex];
        cmdIndex = (cmdIndex + 1) % MAX_STATUS_CMD_SIZE;

        if (cmd.type() == MsgType::THERMOSTAT_INITIAL_GET) {
            cmd[1] = static_cast<uint8_t>(status.RcMask);
            cmd[2] = 0xff;
        } else if (cmd.type() == MsgType::THERMOSTAT_CURRENT_TEMP_SET) {
            // fill all current thermostat values
            for (auto i = 0; i < MAX_REMOTE_THERMOSTATS; i++) {
                if (CHECK_BIT(static_cast<uint8_t>(status.RcMask), i)){
                    if (status.CurrentRoomTemperatures[i] != 0xff) {
                        auto roundedTemp = round_nearest_half(status.CurrentRoomTemperatures[i]);
                        cmd.set_float8_v3(roundedTemp, 1+i);
                    }
                    else {
                        // default 20c if sensor did not have a valid value to prevent P1 errors
                        cmd.set_float8_v3(20.0f, 1+i);
                    }
                }
                else {
                    cmd[1+i] = 0xff;
                }
            }
            cmd[9] = static_cast<uint8_t>(status.RcMask);
            cmd[10] = 0xff;
        }

        if (!serial_tx(uart_, cmd))
        {
            ESP_LOGI(TAG, "Unable to dispatch status update request, flushing queued requests...");
            cmdIndex = 0;
            connected = false;
            return false;
        }

        return true;
    }

    bool EcodanHeatpump::dispatch_next_cmd()
    {
        if (cmdQueue.empty())
        {
            return true;
        }
        
        //ESP_LOGI(TAG, msg.debug_dump_packet().c_str());

        if (!serial_tx(uart_, cmdQueue.front()))
        {
            ESP_LOGI(TAG, "Unable to dispatch status update request, flushing queued requests...");
            connected = false;
            return false;
        }

        return true;
    }

    bool EcodanHeatpump::begin_connect()
    {
        Message cmd{MsgType::CONNECT_CMD};
        uint8_t payload[2] = {0xCA, 0x01 };
        cmd.write_payload(payload, sizeof(payload));

        ESP_LOGI(TAG, "Attempt to tx CONNECT_CMD!");
        if (!serial_tx(uart_, cmd))
        {
            ESP_LOGI(TAG, "Failed to tx CONNECT_CMD!");
            return false;
        }

        return true;
    }

}}
