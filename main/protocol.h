#pragma once
#include <stdint.h>

#define PROTOCOL_VERSION 1

typedef struct {
    uint8_t version;
    uint32_t sequence;
    float current_rms_a;
    float sensor_voltage_rms_v;
} current_data_packet_t;
