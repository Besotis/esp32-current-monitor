#pragma once

#include <stdint.h>

#define CURRENT_PROTOCOL_MAGIC       0x43555252u
#define CURRENT_PROTOCOL_VERSION     2u

typedef struct
{
    uint32_t magic;
    uint8_t version;
    uint8_t reserved[3];

    uint32_t session_id;
    uint32_t sequence;

    float current_rms_a;
    float sensor_voltage_rms_v;
    float offset_voltage_v;
} current_data_packet_t;
