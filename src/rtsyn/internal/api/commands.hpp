/**
 * @file rtsyn/internal/api/commands.hpp
 * @brief RTSyn API command producer helpers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef RTSYN_INTERNAL_API_COMMANDS_HPP
#define RTSYN_INTERNAL_API_COMMANDS_HPP

#include <cstdint>
#include <string>

#include <rtsyn/api.hpp>

namespace rtsyn::api::internal {

std::uint64_t now_ns();

bool push_global_command(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                         GlobalCommand command);
bool push_plugin_update(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                        std::uint32_t plugin_id, std::uint8_t plugin_state);
bool push_port_values_request(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                              std::uint32_t plugin_id, bool send, std::uint64_t portsyn_mask);
bool push_variables_request(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                            std::uint32_t plugin_id, bool send, std::uint64_t variable_mask);
bool push_load_node(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                    rtsyn_abi_node_type_t node_type, const std::string &module_path);
bool push_add_node(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                   rtsyn_abi_node_type_t node_type, const std::string &node_name);
bool push_add_connection(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                         std::uint32_t connection_id, std::uint32_t source_node_id,
                         std::uint32_t source_port_id, std::uint32_t destination_node_id,
                         std::uint32_t destination_port_id);
bool push_remove_connection(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                            std::uint32_t connection_id);
bool push_set_param(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq, std::uint32_t node_id,
                    std::uint32_t param_id, rtsyn_abi_value_type_t value_type,
                    const rtsyn_spsc_command_param_value_t &value);

} // namespace rtsyn::api::internal

#endif // RTSYN_INTERNAL_API_COMMANDS_HPP
