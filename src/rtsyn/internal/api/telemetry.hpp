/**
 * @file rtsyn/internal/api/telemetry.hpp
 * @brief RTSyn API telemetry consumer helpers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef RTSYN_INTERNAL_API_TELEMETRY_HPP
#define RTSYN_INTERNAL_API_TELEMETRY_HPP

#include <cstddef>
#include <string>
#include <vector>

#include <rtsyn/api.hpp>

namespace rtsyn::api::internal {

struct TelemetryDrain {
    DrainResult result;
    std::vector<rtsyn_spsc_telemetry_message_t> events;
};

bool append_values_event(const std::string &path, rtsyn_spsc_telemetry_values_t *values,
                         const rtsyn_spsc_telemetry_message_t &event, std::size_t *value_count);

TelemetryDrain drain_telemetry(rtsyn_spsc_telemetry_queue_t *queue,
                               rtsyn_spsc_telemetry_values_t *values,
                               const std::string &values_path, std::size_t budget);

} // namespace rtsyn::api::internal

#endif // RTSYN_INTERNAL_API_TELEMETRY_HPP
