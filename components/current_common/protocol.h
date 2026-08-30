#pragma once

#include <stdint.h>

#define CURRENT_PROTOCOL_MAGIC       0x43555252u
#define CURRENT_PROTOCOL_VERSION     3u

typedef struct
{
    uint32_t magic;
    uint8_t version;
    uint8_t reserved[3];

    uint32_t session_id;
    uint32_t sequence;

    float current_l1_a;
    float current_l2_a;
    float current_l3_a;

    float sensor_l1_voltage_rms_v;
    float sensor_l2_voltage_rms_v;
    float sensor_l3_voltage_rms_v;

    float offset_l1_voltage_v;
    float offset_l2_voltage_v;
    float offset_l3_voltage_v;
} current_data_packet_t;
