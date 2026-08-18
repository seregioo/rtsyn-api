/**
 * @file rtsyn/internal/api/serialization.hpp
 * @brief RTSyn API JSON serialization helpers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef RTSYN_INTERNAL_API_SERIALIZATION_HPP
#define RTSYN_INTERNAL_API_SERIALIZATION_HPP

#include <string>

extern "C" {
#include <rtsyn/spsc/command/message.h>
#include <rtsyn/spsc/result/message.h>
#include <rtsyn/spsc/telemetry/message.h>
#include <rtsyn/spsc/telemetry/values.h>
}

namespace rtsyn::api::internal {

std::string command_type_name(rtsyn_spsc_command_message_type_t type);
std::string telemetry_type_name(rtsyn_spsc_telemetry_message_type_t type);
std::string telemetry_source_name(rtsyn_spsc_telemetry_source_t source);
std::string telemetry_node_status_name(rtsyn_spsc_telemetry_node_status_t status);
std::string value_type_name(rtsyn_abi_value_type_t type);

std::string telemetry_message_to_json(const rtsyn_spsc_telemetry_message_t &message);
std::string telemetry_value_to_json(const rtsyn_spsc_telemetry_message_t &event,
                                    const rtsyn_spsc_telemetry_value_t &value);
std::string result_message_to_json(const rtsyn_spsc_result_message_t &message);

} // namespace rtsyn::api::internal

#endif // RTSYN_INTERNAL_API_SERIALIZATION_HPP
