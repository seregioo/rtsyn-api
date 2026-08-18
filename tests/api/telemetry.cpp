#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <rtsyn/api/defaults.h>

#include "rtsyn/internal/api/telemetry.hpp"

TEST(ApiTelemetryTest, DrainsValuesWrittenEventToFileAndReleasesValues)
{
    const std::string path = "/tmp/rtsyn-api-telemetry-test-values";
    std::remove(path.c_str());

    rtsyn_spsc_telemetry_queue_t queue = {};
    rtsyn_spsc_telemetry_values_t values = {};
    rtsyn_spsc_telemetry_init(&queue);
    rtsyn_spsc_telemetry_values_init(&values);

    rtsyn_spsc_telemetry_value_t sample = {};
    sample.cycle_id = 7;
    sample.timestamp_ns = 1000;
    sample.node_id = 3;
    sample.value_id = 4;
    sample.source = RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN;
    sample.value_type = RTSYN_ABI_VALUE_U64;
    sample.data.u64 = 99;

    rtsyn_spsc_telemetry_message_t event = {};
    event.seq = 12;
    event.data.values_written.cycle_id = 7;
    event.data.values_written.node_id = 3;
    event.data.values_written.source = RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN;
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_publish_values(&queue, &values, &event, &sample, 1));

    auto drain = rtsyn::api::internal::drain_telemetry(&queue, &values, path, 4);

    EXPECT_EQ(drain.result.event_count, 1U);
    EXPECT_EQ(drain.result.value_count, 1U);
    EXPECT_EQ(rtsyn_spsc_telemetry_values_size(&values), 0U);

    std::ifstream stream(path);
    std::string line;
    ASSERT_TRUE(std::getline(stream, line));
    EXPECT_NE(line.find("\"event_seq\":12"), std::string::npos);
    EXPECT_NE(line.find("\"value\":99"), std::string::npos);

    std::remove(path.c_str());
}

TEST(ApiTelemetryTest, HonorsDrainBudget)
{
    rtsyn_spsc_telemetry_queue_t queue = {};
    rtsyn_spsc_telemetry_values_t values = {};
    rtsyn_spsc_telemetry_init(&queue);
    rtsyn_spsc_telemetry_values_init(&values);

    rtsyn_spsc_telemetry_message_t event = {};
    event.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CYCLE_BEGIN;
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_push(&queue, &event));
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_push(&queue, &event));

    auto drain =
        rtsyn::api::internal::drain_telemetry(&queue, &values, RTSYN_API_DEFAULT_VALUES_FILE, 1);

    EXPECT_EQ(drain.result.event_count, 1U);
    EXPECT_EQ(rtsyn_spsc_telemetry_size(&queue), 1U);
}
