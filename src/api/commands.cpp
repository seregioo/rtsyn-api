#include "rtsyn/internal/api/commands.hpp"

#include <chrono>
#include <cstdio>

namespace rtsyn::api::internal {

std::uint64_t
now_ns()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

bool
push_global_command(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq, GlobalCommand command)
{
    rtsyn_spsc_command_message_t message = {};
    message.seq = seq;
    message.timestamp_ns = now_ns();
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_GLOBAL_COMMAND;
    message.data.global_command.command = static_cast<std::uint8_t>(command);
    return rtsyn_spsc_command_try_push(queue, &message);
}

bool
push_plugin_update(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq, std::uint32_t plugin_id,
                   std::uint8_t plugin_state)
{
    rtsyn_spsc_command_message_t message = {};
    message.seq = seq;
    message.timestamp_ns = now_ns();
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_PLUGIN_UPDATE;
    message.data.plugin_update.plugin_id = plugin_id;
    message.data.plugin_update.plugin_state = plugin_state;
    return rtsyn_spsc_command_try_push(queue, &message);
}

bool
push_port_values_request(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                         std::uint32_t plugin_id, bool send, std::uint64_t portsyn_mask)
{
    rtsyn_spsc_command_message_t message = {};
    message.seq = seq;
    message.timestamp_ns = now_ns();
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VALUES;
    message.data.plugin_request_ports.plugin_id = plugin_id;
    message.data.plugin_request_ports.send = send;
    message.data.plugin_request_ports.portsyn_mask = portsyn_mask;
    return rtsyn_spsc_command_try_push(queue, &message);
}

bool
push_variables_request(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                       std::uint32_t plugin_id, bool send, std::uint64_t variable_mask)
{
    rtsyn_spsc_command_message_t message = {};
    message.seq = seq;
    message.timestamp_ns = now_ns();
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VARIABLES;
    message.data.plugin_request_variables.plugin_id = plugin_id;
    message.data.plugin_request_variables.send = send;
    message.data.plugin_request_variables.variable_mask = variable_mask;
    return rtsyn_spsc_command_try_push(queue, &message);
}

bool
push_load_node(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
               rtsyn_abi_node_type_t node_type, const std::string &module_path)
{
    if (!rtsyn_abi_node_type_is_valid(node_type) || module_path.empty()
        || module_path.size() >= RTSYN_SPSC_COMMAND_MODULE_PATH_MAX_SIZE)
    {
        return false;
    }

    rtsyn_spsc_command_message_t message = {};
    message.seq = seq;
    message.timestamp_ns = now_ns();
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE;
    message.data.load_node.node_type = node_type;
    std::snprintf(message.data.load_node.module_path, sizeof(message.data.load_node.module_path),
                  "%s", module_path.c_str());
    return rtsyn_spsc_command_try_push(queue, &message);
}

bool
push_add_node(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
              rtsyn_abi_node_type_t node_type, const std::string &node_name)
{
    if (!rtsyn_abi_node_type_is_valid(node_type) || node_name.empty()
        || node_name.size() >= RTSYN_SPSC_COMMAND_NODE_NAME_MAX_SIZE)
    {
        return false;
    }

    rtsyn_spsc_command_message_t message = {};
    message.seq = seq;
    message.timestamp_ns = now_ns();
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE;
    message.data.add_node.node_type = node_type;
    std::snprintf(message.data.add_node.node_name, sizeof(message.data.add_node.node_name), "%s",
                  node_name.c_str());
    return rtsyn_spsc_command_try_push(queue, &message);
}

bool
push_add_connection(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                    std::uint32_t connection_id, std::uint32_t source_node_id,
                    std::uint32_t source_port_id, std::uint32_t destination_node_id,
                    std::uint32_t destination_port_id)
{
    rtsyn_spsc_command_message_t message = {};
    message.seq = seq;
    message.timestamp_ns = now_ns();
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_CONNECTION;
    message.data.add_connection.connection_id = connection_id;
    message.data.add_connection.source_node_id = source_node_id;
    message.data.add_connection.source_port_id = source_port_id;
    message.data.add_connection.destination_node_id = destination_node_id;
    message.data.add_connection.destination_port_id = destination_port_id;
    return rtsyn_spsc_command_try_push(queue, &message);
}

bool
push_remove_connection(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                       std::uint32_t connection_id)
{
    rtsyn_spsc_command_message_t message = {};
    message.seq = seq;
    message.timestamp_ns = now_ns();
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REMOVE_CONNECTION;
    message.data.remove_connection.connection_id = connection_id;
    return rtsyn_spsc_command_try_push(queue, &message);
}

bool
push_set_param(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq, std::uint32_t node_id,
               std::uint32_t param_id, rtsyn_abi_value_type_t value_type,
               const rtsyn_spsc_command_param_value_t &value)
{
    rtsyn_spsc_command_message_t message = {};
    message.seq = seq;
    message.timestamp_ns = now_ns();
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_PARAM;
    message.data.set_param.node_id = node_id;
    message.data.set_param.param_id = param_id;
    message.data.set_param.value_type = value_type;
    message.data.set_param.value = value;
    return rtsyn_spsc_command_try_push(queue, &message);
}

bool
push_runtime_period(rtsyn_spsc_command_queue_t *queue, std::uint64_t seq,
                    std::uint64_t period_ns)
{
    if (period_ns == 0)
    {
        return false;
    }

    rtsyn_spsc_command_message_t message = {};
    message.seq = seq;
    message.timestamp_ns = now_ns();
    message.type = RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_PERIOD;
    message.data.set_runtime_period.period_ns = period_ns;
    return rtsyn_spsc_command_try_push(queue, &message);
}

} // namespace rtsyn::api::internal
