#include <gtest/gtest.h>

#include <cstdio>

#include "rtsyn/internal/api/serialization.hpp"

TEST(ApiSerializationTest, SerializesTelemetryEvent)
{
    rtsyn_spsc_telemetry_message_t event = {};
    event.seq = 10;
    event.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CYCLE_BEGIN;
    event.data.cycle_begin.cycle_id = 3;
    event.data.cycle_begin.scheduled_timestamp_ns = 900;

    const auto json = rtsyn::api::internal::telemetry_message_to_json(event);

    EXPECT_NE(json.find("\"type\":\"cycle_begin\""), std::string::npos);
    EXPECT_NE(json.find("\"cycle_id\":3"), std::string::npos);
}

TEST(ApiSerializationTest, SerializesMeasurementTelemetryEvent)
{
    rtsyn_spsc_telemetry_message_t event = {};
    event.seq = 11;
    event.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_MEASUREMENT;
    event.data.measurement.cycle_id = 3;
    event.data.measurement.period_ns = 1000;
    event.data.measurement.actual_period_ns = 1100;
    event.data.measurement.latency_ns = 100;
    event.data.measurement.missed_cycle = 1;
    event.data.measurement.devices_read_ns = 10;
    event.data.measurement.plugins_time_ns = 20;
    event.data.measurement.devices_write_ns = 30;

    const auto json = rtsyn::api::internal::telemetry_message_to_json(event);

    EXPECT_NE(json.find("\"type\":\"measurement\""), std::string::npos);
    EXPECT_NE(json.find("\"latency_ns\":100"), std::string::npos);
    EXPECT_NE(json.find("\"missed_cycle\":true"), std::string::npos);
    EXPECT_NE(json.find("\"devices_read_ns\":10"), std::string::npos);
}

TEST(ApiSerializationTest, SerializesTelemetryValue)
{
    rtsyn_spsc_telemetry_message_t event = {};
    event.seq = 2;

    rtsyn_spsc_telemetry_value_t value = {};
    value.cycle_id = 1;
    value.node_id = 5;
    value.value_id = 6;
    value.value_kind = RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT;
    value.source = RTSYN_SPSC_TELEMETRY_SOURCE_DEVICE;
    value.value_type = RTSYN_ABI_VALUE_F64;
    value.data.f64 = 42.5;

    const auto json = rtsyn::api::internal::telemetry_value_to_json(event, value);

    EXPECT_NE(json.find("\"event_seq\":2"), std::string::npos);
    EXPECT_NE(json.find("\"kind\":\"port\""), std::string::npos);
    EXPECT_NE(json.find("\"source\":\"device\""), std::string::npos);
    EXPECT_NE(json.find("\"value\":42.5"), std::string::npos);
}

TEST(ApiSerializationTest, SerializesCommandResultDescriptor)
{
    rtsyn_spsc_result_message_t result = {};
    result.seq = 12;
    result.command_type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE;
    result.status = RTSYN_SPSC_RESULT_STATUS_OK;
    result.node_id = 7;
    result.node.node_type = RTSYN_ABI_NODE_PLUGIN;
    result.node.port_count = 1;
    snprintf(result.node.name, sizeof(result.node.name), "%s", "adder");
    result.node.ports[0].id = 0;
    result.node.ports[0].direction = RTSYN_ABI_PORT_DIRECTION_IN;
    result.node.ports[0].value_type = RTSYN_ABI_VALUE_F64;
    snprintf(result.node.ports[0].name, sizeof(result.node.ports[0].name), "%s", "left");

    const auto json = rtsyn::api::internal::result_message_to_json(result);

    EXPECT_NE(json.find("\"command_type\":\"add_node\""), std::string::npos);
    EXPECT_NE(json.find("\"success\":true"), std::string::npos);
    EXPECT_NE(json.find("\"node_id\":7"), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"adder\""), std::string::npos);
    EXPECT_NE(json.find("\"value_type\":\"f64\""), std::string::npos);
}
