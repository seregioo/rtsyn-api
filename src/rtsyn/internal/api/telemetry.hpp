/**
 * @file rtsyn/internal/api/telemetry.hpp
 * @brief RTSyn API telemetry consumer helpers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef RTSYN_INTERNAL_API_TELEMETRY_HPP
#define RTSYN_INTERNAL_API_TELEMETRY_HPP

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <rtsyn/api.hpp>

namespace rtsyn::api::internal {

struct TelemetryDrain {
    DrainResult result;
    std::vector<rtsyn_spsc_telemetry_message_t> events;
    std::vector<rtsyn_spsc_telemetry_value_t> values;
};

struct TelemetryCsvValueSelector {
    std::uint32_t node_id = 0;
    std::uint32_t value_id = 0;
    std::uint16_t value_kind = RTSYN_SPSC_TELEMETRY_VALUE_KIND_NONE;
};

struct TelemetryCsvSink {
    std::string path;
    std::vector<std::string> names;
    std::vector<TelemetryCsvValueSelector> values;
    std::vector<std::string> measurement_fields;
    std::ofstream stream;
    std::uint64_t pending_cycle_id = 0;
    std::uint64_t pending_timestamp_ns = 0;
    std::vector<std::string> pending_columns;
    bool pending_valid = false;
    bool enabled = false;
};

bool initialize_csv_sink_file(TelemetryCsvSink &sink);
bool flush_csv_sink(TelemetryCsvSink &sink);

std::optional<std::vector<rtsyn_spsc_telemetry_value_t>>
append_values_event(const std::string &path, rtsyn_spsc_telemetry_values_t *values,
                    const rtsyn_spsc_telemetry_message_t &event, std::size_t *value_count,
                    TelemetryCsvSink *csv_sink = nullptr, std::ofstream *values_stream = nullptr);

TelemetryDrain drain_telemetry(rtsyn_spsc_telemetry_queue_t *queue,
                               rtsyn_spsc_telemetry_values_t *values,
                               const std::string &values_path, std::size_t budget,
                               TelemetryCsvSink *csv_sink = nullptr,
                               std::ofstream *values_stream = nullptr);

} // namespace rtsyn::api::internal

#endif // RTSYN_INTERNAL_API_TELEMETRY_HPP
