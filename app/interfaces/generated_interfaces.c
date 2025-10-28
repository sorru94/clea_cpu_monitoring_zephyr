/**
 * @file generated_interfaces.c
 * @brief Contains automatically generated interfaces.
 *
 * @warning Do not modify this file manually.
 */

#include "generated_interfaces.h"

// Interface names should resemble as closely as possible their respective .json file names.
// NOLINTBEGIN(readability-identifier-naming)

static const astarte_mapping_t com_example_poc_AmbientTemp_mappings[1] = {

    {
        .endpoint = "/temp",
        .type = ASTARTE_MAPPING_TYPE_DOUBLE,
        .reliability = ASTARTE_MAPPING_RELIABILITY_UNRELIABLE,
        .explicit_timestamp = true,
        .allow_unset = false,
    },
};

const astarte_interface_t com_example_poc_AmbientTemp = {
    .name = "com.example.poc.AmbientTemp",
    .major_version = 0,
    .minor_version = 1,
    .type = ASTARTE_INTERFACE_TYPE_DATASTREAM,
    .ownership = ASTARTE_INTERFACE_OWNERSHIP_DEVICE,
    .aggregation = ASTARTE_INTERFACE_AGGREGATION_INDIVIDUAL,
    .mappings = com_example_poc_AmbientTemp_mappings,
    .mappings_length = 1U,
};

static const astarte_mapping_t com_example_poc_CpuMetrics_mappings[1] = {

    {
        .endpoint = "/loadavg",
        .type = ASTARTE_MAPPING_TYPE_DOUBLE,
        .reliability = ASTARTE_MAPPING_RELIABILITY_UNRELIABLE,
        .explicit_timestamp = true,
        .allow_unset = false,
    },
};

const astarte_interface_t com_example_poc_CpuMetrics = {
    .name = "com.example.poc.CpuMetrics",
    .major_version = 0,
    .minor_version = 1,
    .type = ASTARTE_INTERFACE_TYPE_DATASTREAM,
    .ownership = ASTARTE_INTERFACE_OWNERSHIP_DEVICE,
    .aggregation = ASTARTE_INTERFACE_AGGREGATION_INDIVIDUAL,
    .mappings = com_example_poc_CpuMetrics_mappings,
    .mappings_length = 1U,
};

static const astarte_mapping_t com_example_poc_CpuTemp_mappings[1] = {

    {
        .endpoint = "/temp",
        .type = ASTARTE_MAPPING_TYPE_DOUBLE,
        .reliability = ASTARTE_MAPPING_RELIABILITY_UNRELIABLE,
        .explicit_timestamp = true,
        .allow_unset = false,
    },
};

const astarte_interface_t com_example_poc_CpuTemp = {
    .name = "com.example.poc.CpuTemp",
    .major_version = 0,
    .minor_version = 1,
    .type = ASTARTE_INTERFACE_TYPE_DATASTREAM,
    .ownership = ASTARTE_INTERFACE_OWNERSHIP_DEVICE,
    .aggregation = ASTARTE_INTERFACE_AGGREGATION_INDIVIDUAL,
    .mappings = com_example_poc_CpuTemp_mappings,
    .mappings_length = 1U,
};

// NOLINTEND(readability-identifier-naming)
