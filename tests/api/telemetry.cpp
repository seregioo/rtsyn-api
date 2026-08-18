#include <chrono>
#include <cstdio>
#include <filesystem>
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

TEST(ApiTelemetryTest, DrainsSelectedValuesToCsvSink)
{
    const std::string json_path = "/tmp/rtsyn-api-telemetry-test-values-json";
    const std::string csv_path = "/tmp/rtsyn-api-telemetry-test-values.csv";
    std::remove(json_path.c_str());
    std::remove(csv_path.c_str());

    rtsyn_spsc_telemetry_queue_t queue = {};
    rtsyn_spsc_telemetry_values_t values = {};
    rtsyn_spsc_telemetry_init(&queue);
    rtsyn_spsc_telemetry_values_init(&values);

    rtsyn::api::internal::TelemetryCsvSink sink = {};
    sink.path = csv_path;
    sink.names = {"left", "right"};
    sink.values = {{3, 4, RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT},
                   {3, 8, RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT}};
    sink.enabled = true;
    ASSERT_TRUE(rtsyn::api::internal::initialize_csv_sink_file(sink));

    rtsyn_spsc_telemetry_value_t samples[3] = {};
    samples[0].cycle_id = 7;
    samples[0].timestamp_ns = 1000;
    samples[0].node_id = 3;
    samples[0].value_id = 4;
    samples[0].sample_offset = 0;
    samples[0].value_kind = RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT;
    samples[0].source = RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN;
    samples[0].value_type = RTSYN_ABI_VALUE_F64;
    samples[0].data.f64 = 1.25;

    samples[1] = samples[0];
    samples[1].value_id = 8;
    samples[1].data.f64 = 2.5;

    samples[2] = samples[0];
    samples[2].value_id = 99;
    samples[2].data.f64 = 9.9;

    rtsyn_spsc_telemetry_message_t event = {};
    event.seq = 12;
    event.data.values_written.cycle_id = 7;
    event.data.values_written.node_id = 3;
    event.data.values_written.source = RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN;
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_publish_values(&queue, &values, &event, samples, 3));

    auto drain = rtsyn::api::internal::drain_telemetry(&queue, &values, json_path, 4, &sink);

    EXPECT_EQ(drain.result.event_count, 1U);
    EXPECT_EQ(drain.result.value_count, 3U);
    EXPECT_EQ(rtsyn_spsc_telemetry_values_size(&values), 0U);
    ASSERT_TRUE(rtsyn::api::internal::flush_csv_sink(sink));

    std::ifstream stream(csv_path);
    std::string header;
    std::string row;
    ASSERT_TRUE(std::getline(stream, header));
    ASSERT_TRUE(std::getline(stream, row));
    EXPECT_EQ(header, "cycle_id,timestamp_ns,left,right");
    EXPECT_NE(row.find("7,1000"), std::string::npos);
    EXPECT_NE(row.find(",1.25,2.5"), std::string::npos);

    std::remove(json_path.c_str());
    std::remove(csv_path.c_str());
}

TEST(ApiTelemetryTest, CsvSinkCreatesMissingParentDirectory)
{
    const auto dir = std::filesystem::temp_directory_path()
                     / ("rtsyn-api-csv-missing-parent-"
                        + std::to_string(std::chrono::steady_clock::now()
                                             .time_since_epoch()
                                             .count()));
    const auto csv_path = dir / "nested" / "values.csv";
    std::filesystem::remove_all(dir);

    rtsyn::api::internal::TelemetryCsvSink sink = {};
    sink.path = csv_path.string();
    sink.names = {"left"};
    sink.values = {{3, 4, RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT}};
    sink.enabled = true;

    ASSERT_TRUE(rtsyn::api::internal::initialize_csv_sink_file(sink));
    EXPECT_TRUE(std::filesystem::exists(csv_path));

    std::ifstream stream(csv_path);
    std::string header;
    ASSERT_TRUE(std::getline(stream, header));
    EXPECT_EQ(header, "cycle_id,timestamp_ns,left");

    std::filesystem::remove_all(dir);
}

TEST(ApiTelemetryTest, CsvSinkMatchesNodeIdValueIdAndKind)
{
    const std::string json_path = "/tmp/rtsyn-api-telemetry-test-selector-json";
    const std::string csv_path = "/tmp/rtsyn-api-telemetry-test-selector.csv";
    std::remove(json_path.c_str());
    std::remove(csv_path.c_str());

    rtsyn_spsc_telemetry_queue_t queue = {};
    rtsyn_spsc_telemetry_values_t values = {};
    rtsyn_spsc_telemetry_init(&queue);
    rtsyn_spsc_telemetry_values_init(&values);

    rtsyn::api::internal::TelemetryCsvSink sink = {};
    sink.path = csv_path;
    sink.names = {"adder_state_result"};
    sink.values = {{9, 0, RTSYN_SPSC_TELEMETRY_VALUE_KIND_STATE}};
    sink.enabled = true;
    ASSERT_TRUE(rtsyn::api::internal::initialize_csv_sink_file(sink));

    rtsyn_spsc_telemetry_value_t samples[3] = {};
    samples[0].cycle_id = 17;
    samples[0].timestamp_ns = 1000;
    samples[0].node_id = 3;
    samples[0].value_id = 0;
    samples[0].value_kind = RTSYN_SPSC_TELEMETRY_VALUE_KIND_STATE;
    samples[0].source = RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN;
    samples[0].value_type = RTSYN_ABI_VALUE_F64;
    samples[0].data.f64 = 1.0;

    samples[1] = samples[0];
    samples[1].node_id = 9;
    samples[1].data.f64 = 2.0;

    samples[2] = samples[1];
    samples[2].value_kind = RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT;
    samples[2].data.f64 = 3.0;

    rtsyn_spsc_telemetry_message_t event = {};
    event.data.values_written.cycle_id = 17;
    event.data.values_written.node_id = 9;
    event.data.values_written.source = RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN;
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_publish_values(&queue, &values, &event, samples, 3));

    auto drain = rtsyn::api::internal::drain_telemetry(&queue, &values, json_path, 4, &sink);

    EXPECT_EQ(drain.result.event_count, 1U);
    ASSERT_TRUE(rtsyn::api::internal::flush_csv_sink(sink));

    std::ifstream stream(csv_path);
    std::string header;
    std::string row;
    ASSERT_TRUE(std::getline(stream, header));
    ASSERT_TRUE(std::getline(stream, row));
    EXPECT_EQ(header, "cycle_id,timestamp_ns,adder_state_result");
    EXPECT_EQ(row, "17,1000,2");
    EXPECT_FALSE(std::getline(stream, row));

    std::remove(json_path.c_str());
    std::remove(csv_path.c_str());
}

TEST(ApiTelemetryTest, CsvSinkGroupsSeparateValueEventsByCycle)
{
    const std::string json_path = "/tmp/rtsyn-api-telemetry-test-group-json";
    const std::string csv_path = "/tmp/rtsyn-api-telemetry-test-group.csv";
    std::remove(json_path.c_str());
    std::remove(csv_path.c_str());

    rtsyn_spsc_telemetry_queue_t queue = {};
    rtsyn_spsc_telemetry_values_t values = {};
    rtsyn_spsc_telemetry_init(&queue);
    rtsyn_spsc_telemetry_values_init(&values);

    rtsyn::api::internal::TelemetryCsvSink sink = {};
    sink.path = csv_path;
    sink.names = {"adder_state_result", "forwarder_output_out"};
    sink.values = {{9, 0, RTSYN_SPSC_TELEMETRY_VALUE_KIND_STATE},
                   {3, 0, RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT}};
    sink.enabled = true;
    ASSERT_TRUE(rtsyn::api::internal::initialize_csv_sink_file(sink));

    rtsyn_spsc_telemetry_value_t adder = {};
    adder.cycle_id = 21;
    adder.timestamp_ns = 1000;
    adder.node_id = 9;
    adder.value_id = 0;
    adder.value_kind = RTSYN_SPSC_TELEMETRY_VALUE_KIND_STATE;
    adder.source = RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN;
    adder.value_type = RTSYN_ABI_VALUE_F64;
    adder.data.f64 = 3.0;

    rtsyn_spsc_telemetry_value_t forwarder = adder;
    forwarder.node_id = 3;
    forwarder.value_kind = RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT;
    forwarder.data.f64 = 1.0;

    rtsyn_spsc_telemetry_message_t event = {};
    event.data.values_written.cycle_id = 21;
    event.data.values_written.source = RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN;
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_publish_values(&queue, &values, &event, &adder, 1));
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_publish_values(&queue, &values, &event, &forwarder, 1));

    auto drain = rtsyn::api::internal::drain_telemetry(&queue, &values, json_path, 4, &sink);

    EXPECT_EQ(drain.result.event_count, 2U);
    ASSERT_TRUE(rtsyn::api::internal::flush_csv_sink(sink));

    std::ifstream stream(csv_path);
    std::string header;
    std::string row;
    ASSERT_TRUE(std::getline(stream, header));
    ASSERT_TRUE(std::getline(stream, row));
    EXPECT_EQ(header, "cycle_id,timestamp_ns,adder_state_result,forwarder_output_out");
    EXPECT_EQ(row, "21,1000,3,1");
    EXPECT_FALSE(std::getline(stream, row));

    std::remove(json_path.c_str());
    std::remove(csv_path.c_str());
}

TEST(ApiTelemetryTest, CsvSinkWritesSelectedMeasurementFields)
{
    const std::string json_path = "/tmp/rtsyn-api-test-telemetry-measurement.jsonl";
    const std::string csv_path = "/tmp/rtsyn-api-test-telemetry-measurement.csv";
    std::remove(json_path.c_str());
    std::remove(csv_path.c_str());

    rtsyn_spsc_telemetry_queue_t queue = {};
    rtsyn_spsc_telemetry_values_t values = {};
    rtsyn_spsc_telemetry_init(&queue);
    rtsyn_spsc_telemetry_values_init(&values);

    rtsyn::api::internal::TelemetryCsvSink sink = {};
    sink.path = csv_path;
    sink.names = {"left", "latency", "missed", "dropped"};
    sink.values = {{3, 4, RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT}};
    sink.measurement_fields = {"latency_ns", "missed_cycle", "telemetry_dropped_events"};
    sink.enabled = true;
    ASSERT_TRUE(rtsyn::api::internal::initialize_csv_sink_file(sink));

    rtsyn_spsc_telemetry_message_t event = {};
    event.seq = 13;
    event.timestamp_ns = 777;
    event.dropped_event_count = 3;
    event.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_MEASUREMENT;
    event.data.measurement.cycle_id = 9;
    event.data.measurement.latency_ns = 42;
    event.data.measurement.missed_cycle = 1;
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_push(&queue, &event));

    auto drain = rtsyn::api::internal::drain_telemetry(&queue, &values, json_path, 4, &sink);

    EXPECT_EQ(drain.result.event_count, 1U);
    ASSERT_TRUE(rtsyn::api::internal::flush_csv_sink(sink));

    std::ifstream stream(csv_path);
    std::string header;
    std::string row;
    ASSERT_TRUE(std::getline(stream, header));
    ASSERT_TRUE(std::getline(stream, row));
    EXPECT_EQ(header, "cycle_id,timestamp_ns,left,latency,missed,dropped");
    EXPECT_EQ(row, "9,777,,42,1,3");

    std::remove(json_path.c_str());
    std::remove(csv_path.c_str());
}

TEST(ApiTelemetryTest, CsvSinkOmitsSampleOffsetForMeasurementOnlyFiles)
{
    const std::string json_path = "/tmp/rtsyn-api-test-telemetry-measurement-only.jsonl";
    const std::string csv_path = "/tmp/rtsyn-api-test-telemetry-measurement-only.csv";
    std::remove(json_path.c_str());
    std::remove(csv_path.c_str());

    rtsyn_spsc_telemetry_queue_t queue = {};
    rtsyn_spsc_telemetry_values_t values = {};
    rtsyn_spsc_telemetry_init(&queue);
    rtsyn_spsc_telemetry_values_init(&values);

    rtsyn::api::internal::TelemetryCsvSink sink = {};
    sink.path = csv_path;
    sink.names = {"latency"};
    sink.measurement_fields = {"latency_ns"};
    sink.enabled = true;
    ASSERT_TRUE(rtsyn::api::internal::initialize_csv_sink_file(sink));

    rtsyn_spsc_telemetry_message_t event = {};
    event.timestamp_ns = 777;
    event.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_MEASUREMENT;
    event.data.measurement.cycle_id = 9;
    event.data.measurement.latency_ns = 42;
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_push(&queue, &event));

    auto drain = rtsyn::api::internal::drain_telemetry(&queue, &values, json_path, 4, &sink);

    EXPECT_EQ(drain.result.event_count, 1U);
    ASSERT_TRUE(rtsyn::api::internal::flush_csv_sink(sink));

    std::ifstream stream(csv_path);
    std::string header;
    std::string row;
    ASSERT_TRUE(std::getline(stream, header));
    ASSERT_TRUE(std::getline(stream, row));
    EXPECT_EQ(header, "cycle_id,timestamp_ns,latency");
    EXPECT_EQ(row, "9,777,42");

    std::remove(json_path.c_str());
    std::remove(csv_path.c_str());
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
