#include "rtsyn/internal/api/serialization.hpp"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace rtsyn::api::internal {

namespace {

std::string
json_escape(const std::string &input)
{
    std::ostringstream out;
    for (const char c : input)
    {
        switch (c)
        {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << c;
                break;
        }
    }
    return out.str();
}

std::string
sample_string(const rtsyn_spsc_telemetry_value_t &value)
{
    const auto end = std::find(value.data.string,
                               value.data.string + RTSYN_SPSC_TELEMETRY_VALUE_STRING_MAX_SIZE,
                               '\0');
    return std::string(value.data.string, end);
}

} // namespace

std::string
command_type_name(rtsyn_spsc_command_message_type_t type)
{
    switch (type)
    {
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_NONE:
            return "none";
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_PLUGIN_UPDATE:
            return "plugin_update";
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VALUES:
            return "request_port_values";
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VARIABLES:
            return "request_port_variables";
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE:
            return "load_node";
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE:
            return "add_node";
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_PARAM:
            return "set_param";
        case RTSYN_SPSC_COMMAND_MESSAGE_TYPE_GLOBAL_COMMAND:
            return "global_command";
        default:
            return "unknown";
    }
}

std::string
telemetry_type_name(rtsyn_spsc_telemetry_message_type_t type)
{
    switch (type)
    {
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_NONE:
            return "none";
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CYCLE_BEGIN:
            return "cycle_begin";
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CYCLE_END:
            return "cycle_end";
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_NODE_STATUS:
            return "node_status";
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_VALUES_WRITTEN:
            return "values_written";
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CIRCUIT_SNAPSHOT:
            return "circuit_snapshot";
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_MEASUREMENT:
            return "measurement";
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_DROPPED:
            return "dropped";
        default:
            return "unknown";
    }
}

std::string
telemetry_source_name(rtsyn_spsc_telemetry_source_t source)
{
    switch (source)
    {
        case RTSYN_SPSC_TELEMETRY_SOURCE_NONE:
            return "none";
        case RTSYN_SPSC_TELEMETRY_SOURCE_DEVICE:
            return "device";
        case RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN:
            return "plugin";
        default:
            return "unknown";
    }
}

static const char *
telemetry_value_kind_name(uint16_t value_kind)
{
    switch (value_kind)
    {
        case RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT:
            return "port";
        case RTSYN_SPSC_TELEMETRY_VALUE_KIND_STATE:
            return "state";
        case RTSYN_SPSC_TELEMETRY_VALUE_KIND_NONE:
        default:
            return "none";
    }
}

std::string
telemetry_node_status_name(rtsyn_spsc_telemetry_node_status_t status)
{
    switch (status)
    {
        case RTSYN_SPSC_TELEMETRY_NODE_STATUS_NONE:
            return "none";
        case RTSYN_SPSC_TELEMETRY_NODE_STATUS_OK:
            return "ok";
        case RTSYN_SPSC_TELEMETRY_NODE_STATUS_ERROR:
            return "error";
        case RTSYN_SPSC_TELEMETRY_NODE_STATUS_SKIPPED:
            return "skipped";
        default:
            return "unknown";
    }
}

std::string
value_type_name(rtsyn_abi_value_type_t type)
{
    switch (type)
    {
        case RTSYN_ABI_VALUE_F32:
            return "f32";
        case RTSYN_ABI_VALUE_F64:
            return "f64";
        case RTSYN_ABI_VALUE_I64:
            return "i64";
        case RTSYN_ABI_VALUE_U64:
            return "u64";
        case RTSYN_ABI_VALUE_STRING:
            return "string";
        default:
            return "invalid";
    }
}

namespace {

std::string
node_type_name(rtsyn_abi_node_type_t type)
{
    switch (type)
    {
        case RTSYN_ABI_NODE_PLUGIN:
            return "plugin";
        case RTSYN_ABI_NODE_DEVICE:
            return "device";
        case RTSYN_ABI_NODE_TRANSMISOR:
            return "transmisor";
        default:
            return "invalid";
    }
}

std::string
port_direction_name(rtsyn_abi_port_direction_t direction)
{
    return direction == RTSYN_ABI_PORT_DIRECTION_OUT ? "output" : "input";
}

} // namespace

std::string
result_message_to_json(const rtsyn_spsc_result_message_t &message)
{
    std::ostringstream out;
    out << "{\"seq\":" << message.seq << ",\"command_type\":\""
        << command_type_name(message.command_type) << "\",\"success\":"
        << (message.status == RTSYN_SPSC_RESULT_STATUS_OK ? "true" : "false")
        << ",\"status_code\":" << message.status_code << ",\"node_id\":" << message.node_id
        << ",\"node\":{\"name\":\"" << json_escape(message.node.name)
        << "\",\"node_type\":\"" << node_type_name(message.node.node_type) << "\",\"ports\":[";

    const std::uint32_t port_count =
        std::min(message.node.port_count, (std::uint32_t)RTSYN_SPSC_RESULT_PORT_CAPACITY);
    for (std::uint32_t i = 0; i < port_count; i++)
    {
        const auto &port = message.node.ports[i];
        if (i > 0)
        {
            out << ',';
        }
        out << "{\"id\":" << port.id << ",\"name\":\"" << json_escape(port.name)
            << "\",\"direction\":\"" << port_direction_name(port.direction)
            << "\",\"value_type\":\"" << value_type_name(port.value_type) << "\"}";
    }

    out << "],\"params\":[";
    const std::uint32_t param_count =
        std::min(message.node.param_count, (std::uint32_t)RTSYN_SPSC_RESULT_PARAM_CAPACITY);
    for (std::uint32_t i = 0; i < param_count; i++)
    {
        const auto &param = message.node.params[i];
        if (i > 0)
        {
            out << ',';
        }
        out << "{\"id\":" << param.id << ",\"name\":\"" << json_escape(param.name)
            << "\",\"description\":\"" << json_escape(param.description)
            << "\",\"value_type\":\"" << value_type_name(param.value_type) << "\"}";
    }

    out << "],\"states\":[";
    const std::uint32_t state_count =
        std::min(message.node.state_count, (std::uint32_t)RTSYN_SPSC_RESULT_STATE_CAPACITY);
    for (std::uint32_t i = 0; i < state_count; i++)
    {
        const auto &state = message.node.states[i];
        if (i > 0)
        {
            out << ',';
        }
        out << "{\"id\":" << state.id << ",\"name\":\"" << json_escape(state.name)
            << "\",\"description\":\"" << json_escape(state.description)
            << "\",\"value_type\":\"" << value_type_name(state.value_type) << "\"}";
    }

    out << "]}}";
    return out.str();
}

std::string
telemetry_message_to_json(const rtsyn_spsc_telemetry_message_t &message)
{
    std::ostringstream out;
    out << "{\"seq\":" << message.seq << ",\"timestamp_ns\":" << message.timestamp_ns
        << ",\"dropped_event_count\":" << message.dropped_event_count << ",\"type\":\""
        << telemetry_type_name(message.type) << "\"";

    switch (message.type)
    {
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CYCLE_BEGIN:
            out << ",\"cycle_id\":" << message.data.cycle_begin.cycle_id
                << ",\"scheduled_timestamp_ns\":"
                << message.data.cycle_begin.scheduled_timestamp_ns;
            break;
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CYCLE_END:
            out << ",\"cycle_id\":" << message.data.cycle_end.cycle_id << ",\"started_at_ns\":"
                << message.data.cycle_end.started_at_ns << ",\"finished_at_ns\":"
                << message.data.cycle_end.finished_at_ns << ",\"processed_device_count\":"
                << message.data.cycle_end.processed_device_count
                << ",\"processed_plugin_count\":"
                << message.data.cycle_end.processed_plugin_count << ",\"error_count\":"
                << message.data.cycle_end.error_count << ",\"overrun_count\":"
                << message.data.cycle_end.overrun_count;
            break;
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_NODE_STATUS:
            out << ",\"cycle_id\":" << message.data.node_status.cycle_id << ",\"node_id\":"
                << message.data.node_status.node_id << ",\"source\":\""
                << telemetry_source_name(message.data.node_status.source) << "\",\"status\":\""
                << telemetry_node_status_name(message.data.node_status.status)
                << "\",\"status_code\":" << message.data.node_status.status_code
                << ",\"started_at_ns\":" << message.data.node_status.started_at_ns
                << ",\"finished_at_ns\":" << message.data.node_status.finished_at_ns;
            break;
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_VALUES_WRITTEN:
            out << ",\"cycle_id\":" << message.data.values_written.cycle_id
                << ",\"node_id\":" << message.data.values_written.node_id << ",\"source\":\""
                << telemetry_source_name(message.data.values_written.source)
                << "\",\"values_start_index\":"
                << message.data.values_written.values_start_index << ",\"value_count\":"
                << message.data.values_written.value_count;
            break;
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_CIRCUIT_SNAPSHOT:
            out << ",\"cycle_id\":" << message.data.circuit_snapshot.cycle_id
                << ",\"circuit_revision\":"
                << message.data.circuit_snapshot.circuit_revision << ",\"values_start_index\":"
                << message.data.circuit_snapshot.values_start_index << ",\"value_count\":"
                << message.data.circuit_snapshot.value_count;
            break;
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_MEASUREMENT:
            out << ",\"cycle_id\":" << message.data.measurement.cycle_id
                << ",\"period_ns\":" << message.data.measurement.period_ns
                << ",\"actual_period_ns\":" << message.data.measurement.actual_period_ns
                << ",\"latency_ns\":" << message.data.measurement.latency_ns
                << ",\"missed_cycle\":"
                << (message.data.measurement.missed_cycle != 0 ? "true" : "false")
                << ",\"devices_read_ns\":" << message.data.measurement.devices_read_ns
                << ",\"plugins_time_ns\":" << message.data.measurement.plugins_time_ns
                << ",\"devices_write_ns\":" << message.data.measurement.devices_write_ns;
            break;
        case RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_DROPPED:
            out << ",\"cycle_id\":" << message.data.dropped.cycle_id
                << ",\"dropped_events\":" << message.data.dropped.dropped_event_count
                << ",\"dropped_values\":" << message.data.dropped.dropped_value_count;
            break;
        default:
            break;
    }

    out << "}";
    return out.str();
}

std::string
telemetry_value_to_json(const rtsyn_spsc_telemetry_message_t &event,
                        const rtsyn_spsc_telemetry_value_t &value)
{
    std::ostringstream out;
    out << "{\"event_seq\":" << event.seq << ",\"cycle_id\":" << value.cycle_id
        << ",\"timestamp_ns\":" << value.timestamp_ns << ",\"node_id\":" << value.node_id
        << ",\"value_id\":" << value.value_id << ",\"sample_offset\":" << value.sample_offset
        << ",\"kind\":\"" << telemetry_value_kind_name(value.value_kind) << "\",\"source\":\""
        << telemetry_source_name(value.source) << "\",\"value_type\":\""
        << value_type_name(value.value_type) << "\",\"value\":";

    switch (value.value_type)
    {
        case RTSYN_ABI_VALUE_F32:
            out << value.data.f32;
            break;
        case RTSYN_ABI_VALUE_F64:
            out << value.data.f64;
            break;
        case RTSYN_ABI_VALUE_I64:
            out << value.data.i64;
            break;
        case RTSYN_ABI_VALUE_U64:
            out << value.data.u64;
            break;
        case RTSYN_ABI_VALUE_STRING:
            out << "\"" << json_escape(sample_string(value)) << "\"";
            break;
        default:
            out << "null";
            break;
    }

    out << "}";
    return out.str();
}

} // namespace rtsyn::api::internal
