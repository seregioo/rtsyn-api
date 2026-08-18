#include <rtsyn/api.hpp>

#include "rtsyn/internal/api/commands.hpp"
#include "rtsyn/internal/api/http.hpp"
#include "rtsyn/internal/api/serialization.hpp"
#include "rtsyn/internal/api/telemetry.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <httplib.h>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <thread>
#include <tuple>
#include <vector>

namespace rtsyn::api {

namespace {

std::optional<rtsyn_abi_value_type_t>
parse_value_type(const std::string &body)
{
    if (const auto value_type = internal::parse_string_field(body, "value_type"))
    {
        if (*value_type == "f32")
        {
            return RTSYN_ABI_VALUE_F32;
        }
        if (*value_type == "f64")
        {
            return RTSYN_ABI_VALUE_F64;
        }
        if (*value_type == "i64")
        {
            return RTSYN_ABI_VALUE_I64;
        }
        if (*value_type == "u64")
        {
            return RTSYN_ABI_VALUE_U64;
        }
        if (*value_type == "string")
        {
            return RTSYN_ABI_VALUE_STRING;
        }
        return std::nullopt;
    }

    const auto value_type = internal::parse_u64_field(body, "value_type");
    if (!value_type || *value_type > RTSYN_ABI_VALUE_INVALID)
    {
        return std::nullopt;
    }

    const auto abi_value_type = static_cast<rtsyn_abi_value_type_t>(*value_type);
    return rtsyn_abi_value_is_valid(abi_value_type) ? std::optional{abi_value_type}
                                                   : std::nullopt;
}

std::uint16_t
parse_csv_value_kind(const std::string &kind)
{
    if (kind == "port")
    {
        return RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT;
    }
    if (kind == "state")
    {
        return RTSYN_SPSC_TELEMETRY_VALUE_KIND_STATE;
    }
    return RTSYN_SPSC_TELEMETRY_VALUE_KIND_NONE;
}

const char *
runtime_state_name(std::uint8_t state)
{
    switch (state)
    {
        case 0:
            return "init";
        case 1:
        case 2:
        case 3:
            return "process";
        case 4:
            return "stop";
        case 5:
            return "fini";
        default:
            return "unknown";
    }
}

std::optional<std::vector<internal::TelemetryCsvValueSelector>>
parse_csv_value_selectors(const std::string &body)
{
    const std::regex field("\"values\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch match;
    if (!std::regex_search(body, match, field))
    {
        return std::nullopt;
    }

    std::vector<internal::TelemetryCsvValueSelector> selectors;
    const std::string items = match[1].str();
    const std::regex item(
        "\\{\\s*\"node_id\"\\s*:\\s*([0-9]+)\\s*,\\s*\"value_id\"\\s*:\\s*([0-9]+)\\s*,\\s*\"kind\"\\s*:\\s*\"(port|state)\"\\s*\\}");
    std::size_t consumed = 0;
    for (std::sregex_iterator it(items.begin(), items.end(), item), end; it != end; ++it)
    {
        const auto position = static_cast<std::size_t>(it->position());
        const auto separator = items.substr(consumed, position - consumed);
        if (separator.find_first_not_of(" \t\r\n,") != std::string::npos)
        {
            return std::nullopt;
        }

        const auto node_id = std::stoull((*it)[1].str());
        const auto value_id = std::stoull((*it)[2].str());
        if (node_id > UINT32_MAX || value_id > UINT32_MAX)
        {
            return std::nullopt;
        }

        selectors.push_back({static_cast<std::uint32_t>(node_id),
                             static_cast<std::uint32_t>(value_id),
                             parse_csv_value_kind((*it)[3].str())});
        consumed = position + static_cast<std::size_t>(it->length());
    }
    if (items.substr(consumed).find_first_not_of(" \t\r\n,") != std::string::npos)
    {
        return std::nullopt;
    }
    return selectors;
}

std::optional<rtsyn_spsc_command_param_value_t>
parse_param_value(const std::string &body, rtsyn_abi_value_type_t value_type)
{
    rtsyn_spsc_command_param_value_t value = {};
    switch (value_type)
    {
        case RTSYN_ABI_VALUE_F32:
        {
            const auto parsed = internal::parse_f64_field(body, "value");
            if (!parsed)
            {
                return std::nullopt;
            }
            value.f32 = static_cast<float>(*parsed);
            return value;
        }
        case RTSYN_ABI_VALUE_F64:
        {
            const auto parsed = internal::parse_f64_field(body, "value");
            if (!parsed)
            {
                return std::nullopt;
            }
            value.f64 = *parsed;
            return value;
        }
        case RTSYN_ABI_VALUE_I64:
        {
            const auto parsed = internal::parse_i64_field(body, "value");
            if (!parsed)
            {
                return std::nullopt;
            }
            value.i64 = *parsed;
            return value;
        }
        case RTSYN_ABI_VALUE_U64:
        {
            const auto parsed = internal::parse_u64_field(body, "value");
            if (!parsed)
            {
                return std::nullopt;
            }
            value.u64 = *parsed;
            return value;
        }
        case RTSYN_ABI_VALUE_STRING:
        {
            const auto parsed = internal::parse_string_field(body, "value");
            if (!parsed)
            {
                return std::nullopt;
            }
            std::snprintf(value.string, sizeof(value.string), "%s", parsed->c_str());
            return value;
        }
        default:
            return std::nullopt;
    }
}

std::string
runtime_state_path_for_port(int port)
{
    return "/tmp/rtsyn-api-runtime-state-" + std::to_string(port) + ".bin";
}

std::string
json_escape_snapshot(const std::string &input)
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
telemetry_sample_string(const rtsyn_spsc_telemetry_value_t &value)
{
    const auto end = std::find(value.data.string,
                               value.data.string + RTSYN_SPSC_TELEMETRY_VALUE_STRING_MAX_SIZE,
                               '\0');
    return std::string(value.data.string, end);
}

std::string
telemetry_value_payload_json(const rtsyn_spsc_telemetry_value_t &value)
{
    std::ostringstream out;
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
            out << "\"" << json_escape_snapshot(telemetry_sample_string(value)) << "\"";
            break;
        default:
            out << "null";
            break;
    }
    return out.str();
}

const char *
telemetry_value_kind_name_snapshot(std::uint16_t value_kind)
{
    switch (value_kind)
    {
        case RTSYN_SPSC_TELEMETRY_VALUE_KIND_PORT:
            return "port";
        case RTSYN_SPSC_TELEMETRY_VALUE_KIND_STATE:
            return "state";
        default:
            return "none";
    }
}

struct RuntimeConnectionSnapshot {
    std::uint32_t connection_id = UINT32_MAX;
    std::uint32_t source_node_id = UINT32_MAX;
    std::uint32_t source_port_id = UINT32_MAX;
    std::uint32_t destination_node_id = UINT32_MAX;
    std::uint32_t destination_port_id = UINT32_MAX;
};

struct RuntimeParamSnapshot {
    std::uint32_t node_id = UINT32_MAX;
    std::uint32_t param_id = UINT32_MAX;
    rtsyn_abi_value_type_t value_type = RTSYN_ABI_VALUE_INVALID;
    rtsyn_spsc_command_param_value_t value = {};
};

struct RuntimeParamKey {
    std::uint32_t node_id = UINT32_MAX;
    std::uint32_t param_id = UINT32_MAX;

    bool operator<(const RuntimeParamKey &other) const
    {
        return std::tie(node_id, param_id) < std::tie(other.node_id, other.param_id);
    }
};

struct LatestTelemetryValueKey {
    std::uint32_t node_id = UINT32_MAX;
    std::uint32_t value_id = UINT32_MAX;
    std::uint16_t value_kind = RTSYN_SPSC_TELEMETRY_VALUE_KIND_NONE;

    bool operator<(const LatestTelemetryValueKey &other) const
    {
        return std::tie(node_id, value_id, value_kind)
               < std::tie(other.node_id, other.value_id, other.value_kind);
    }
};

std::string
param_value_payload_json(const RuntimeParamSnapshot &param)
{
    std::ostringstream out;
    switch (param.value_type)
    {
        case RTSYN_ABI_VALUE_F32:
            out << param.value.f32;
            break;
        case RTSYN_ABI_VALUE_F64:
            out << param.value.f64;
            break;
        case RTSYN_ABI_VALUE_I64:
            out << param.value.i64;
            break;
        case RTSYN_ABI_VALUE_U64:
            out << param.value.u64;
            break;
        case RTSYN_ABI_VALUE_STRING:
            out << "\"" << json_escape_snapshot(std::string(param.value.string)) << "\"";
            break;
        default:
            out << "null";
            break;
    }
    return out.str();
}

} // namespace

class Api::Impl {
  public:
    explicit Impl(Config config) : config_(std::move(config)) { load_runtime_nodes_from_disk(); }

    ~Impl() { stop(); }

    bool valid() const
    {
        return config_.command_queue && config_.result_queue && config_.telemetry_queue
               && config_.telemetry_values && config_.max_telemetry_events_per_drain > 0 && config_.port > 0
               && config_.port <= 65535 && !config_.values_path.empty();
    }

    bool start()
    {
        if (!valid() || running_.exchange(true))
        {
            return false;
        }

        configure_routes();
        telemetry_thread_ = std::thread([this] { telemetry_loop(); });
        server_thread_ = std::thread([this] {
            if (!server_.listen(config_.bind_host, config_.port))
            {
                running_.store(false);
            }
        });
        return true;
    }

    void stop()
    {
        running_.store(false);
        server_.stop();
        if (telemetry_thread_.joinable())
        {
            telemetry_thread_.join();
        }
        if (values_stream_.is_open())
        {
            values_stream_.flush();
            values_stream_.close();
        }
        if (server_thread_.joinable())
        {
            server_thread_.join();
        }
        std::lock_guard lock(csv_mutex_);
        (void)drain_telemetry_locked();
        (void)internal::flush_csv_sink(csv_sink_);
    }

    bool running() const { return running_.load(); }

    void wait()
    {
        if (server_thread_.joinable())
        {
            server_thread_.join();
        }
        running_.store(false);
        if (telemetry_thread_.joinable())
        {
            telemetry_thread_.join();
        }
    }

    bool push_global_command(GlobalCommand command)
    {
        return internal::push_global_command(config_.command_queue, next_seq(), command);
    }

    bool push_plugin_update(std::uint32_t plugin_id, std::uint8_t plugin_state)
    {
        return internal::push_plugin_update(config_.command_queue, next_seq(), plugin_id,
                                            plugin_state);
    }

    bool load_plugin(const std::string &module_path)
    {
        return internal::push_load_node(config_.command_queue, next_seq(), RTSYN_ABI_NODE_PLUGIN,
                                        module_path);
    }

    bool add_plugin(const std::string &node_name)
    {
        return internal::push_add_node(config_.command_queue, next_seq(), RTSYN_ABI_NODE_PLUGIN,
                                       node_name);
    }

    bool load_device(const std::string &module_path)
    {
        return internal::push_load_node(config_.command_queue, next_seq(), RTSYN_ABI_NODE_DEVICE,
                                        module_path);
    }

    bool add_device(const std::string &node_name)
    {
        return internal::push_add_node(config_.command_queue, next_seq(), RTSYN_ABI_NODE_DEVICE,
                                       node_name);
    }

    bool add_connection(std::uint32_t connection_id, std::uint32_t source_node_id,
                        std::uint32_t source_port_id, std::uint32_t destination_node_id,
                        std::uint32_t destination_port_id)
    {
        return internal::push_add_connection(config_.command_queue, next_seq(), connection_id,
                                             source_node_id, source_port_id, destination_node_id,
                                             destination_port_id);
    }

    bool remove_connection(std::uint32_t connection_id)
    {
        return internal::push_remove_connection(config_.command_queue, next_seq(), connection_id);
    }

    bool request_port_values(std::uint32_t plugin_id, bool send, std::uint64_t portsyn_mask)
    {
        return internal::push_port_values_request(config_.command_queue, next_seq(), plugin_id,
                                                  send, portsyn_mask);
    }

    bool request_variables(std::uint32_t plugin_id, bool send, std::uint64_t variable_mask)
    {
        return internal::push_variables_request(config_.command_queue, next_seq(), plugin_id, send,
                                                variable_mask);
    }

    bool set_param(std::uint32_t node_id, std::uint32_t param_id,
                   rtsyn_abi_value_type_t value_type,
                   const rtsyn_spsc_command_param_value_t &value)
    {
        return internal::push_set_param(config_.command_queue, next_seq(), node_id, param_id,
                                        value_type, value);
    }

    bool set_runtime_period(std::uint64_t period_ns)
    {
        return internal::push_runtime_period(config_.command_queue, next_seq(), period_ns);
    }

    bool set_runtime_priority(std::int32_t priority)
    {
        return internal::push_runtime_priority(config_.command_queue, next_seq(), priority);
    }

    bool set_runtime_deadline_tolerance(std::uint64_t tolerance_ns)
    {
        return internal::push_runtime_deadline_tolerance(config_.command_queue, next_seq(),
                                                        tolerance_ns);
    }

    bool configure_csv_values_file(const std::string &path, const std::vector<std::string> &names,
                                   const std::vector<std::uint32_t> &value_ids)
    {
        std::vector<internal::TelemetryCsvValueSelector> values;
        values.reserve(value_ids.size());
        for (const auto value_id : value_ids)
        {
            values.push_back({UINT32_MAX, value_id, RTSYN_SPSC_TELEMETRY_VALUE_KIND_NONE});
        }
        return configure_csv_telemetry_file(path, names, values, {});
    }

    bool configure_csv_telemetry_file(const std::string &path,
                                      const std::vector<std::string> &names,
                                      const std::vector<internal::TelemetryCsvValueSelector> &values,
                                      const std::vector<std::string> &measurement_fields)
    {
        internal::TelemetryCsvSink sink = {};
        sink.path = path;
        sink.names = names;
        sink.values = values;
        sink.measurement_fields = measurement_fields;
        sink.enabled = true;
        if (!internal::initialize_csv_sink_file(sink))
        {
            return false;
        }

        std::lock_guard lock(csv_mutex_);
        csv_sink_ = std::move(sink);
        return true;
    }

    void stop_csv_telemetry_file()
    {
        std::lock_guard lock(csv_mutex_);
        (void)internal::flush_csv_sink(csv_sink_);
        csv_sink_ = {};
    }

    DrainResult drain_telemetry()
    {
        std::lock_guard csv_lock(csv_mutex_);
        return drain_telemetry_locked();
    }

    DrainResult drain_telemetry_locked()
    {
        internal::TelemetryCsvSink *csv_sink = nullptr;
        {
            if (csv_sink_.enabled)
            {
                csv_sink = &csv_sink_;
            }
        }

        const auto drain = internal::drain_telemetry(config_.telemetry_queue,
                                                     config_.telemetry_values, config_.values_path,
                                                     config_.max_telemetry_events_per_drain,
                                                     csv_sink, &values_stream_);
        remember_events(drain.events);
        remember_latest_values(drain.values);
        total_events_.fetch_add(drain.result.event_count);
        total_values_.fetch_add(drain.result.value_count);
        failed_value_events_.fetch_add(drain.result.failed_value_event_count);
        return drain.result;
    }

    std::string recent_events_json() const
    {
        std::lock_guard lock(events_mutex_);
        std::ostringstream out;
        out << "[";
        for (std::size_t i = 0; i < recent_events_.size(); ++i)
        {
            if (i != 0)
            {
                out << ",";
            }
            out << internal::telemetry_message_to_json(recent_events_[i]);
        }
        out << "]";
        return out.str();
    }

    std::string latest_measurement_json() const
    {
        std::lock_guard lock(events_mutex_);
        if (!latest_measurement_)
        {
            return "{\"available\":false}";
        }

        std::string json = internal::telemetry_message_to_json(*latest_measurement_);
        if (!json.empty() && json.back() == '}')
        {
            json.pop_back();
        }
        json += ",\"available\":true}";
        return json;
    }

    std::string runtime_nodes_json()
    {
        drain_result_snapshots();
        (void)request_live_runtime_nodes_snapshot();

        std::lock_guard lock(runtime_nodes_mutex_);
        std::ostringstream out;
        out << "{\"nodes\":[";
        std::size_t index = 0;
        for (const auto &[_, node] : runtime_nodes_)
        {
            if (index++ != 0)
            {
                out << ',';
            }
            auto json = internal::result_message_to_json(node);
            if (!json.empty() && json.back() == '}')
            {
                json.pop_back();
            }
            const auto state = runtime_node_states_.find(node.node_id);
            out << json << ",\"runtime_state\":\""
                << runtime_state_name(state == runtime_node_states_.end() ? 4 : state->second)
                << "\"}";
        }
        out << "],\"loaded_descriptors\":[";
        index = 0;
        for (const auto &[_, descriptor] : loaded_descriptors_)
        {
            if (index++ != 0)
            {
                out << ',';
            }
            out << internal::result_message_to_json(descriptor);
        }
        out << "],\"connections\":[";
        index = 0;
        for (const auto &[_, connection] : runtime_connections_)
        {
            if (index++ != 0)
            {
                out << ',';
            }
            out << "{\"connection_id\":" << connection.connection_id
                << ",\"source_node_id\":" << connection.source_node_id
                << ",\"source_port_id\":" << connection.source_port_id
                << ",\"destination_node_id\":" << connection.destination_node_id
                << ",\"destination_port_id\":" << connection.destination_port_id << "}";
        }
        out << "],\"param_values\":[";
        index = 0;
        for (const auto &[_, param] : runtime_params_)
        {
            if (index++ != 0)
            {
                out << ',';
            }
            out << "{\"node_id\":" << param.node_id << ",\"param_id\":" << param.param_id
                << ",\"value_type\":\"" << internal::value_type_name(param.value_type)
                << "\",\"value\":" << param_value_payload_json(param) << "}";
        }
        out << "],\"latest_values\":[";
        {
            std::lock_guard events_lock(events_mutex_);
            index = 0;
            for (const auto &[_, value] : latest_values_)
            {
                if (index++ != 0)
                {
                    out << ',';
                }
                out << "{\"cycle_id\":" << value.cycle_id
                    << ",\"timestamp_ns\":" << value.timestamp_ns
                    << ",\"node_id\":" << value.node_id
                    << ",\"value_id\":" << value.value_id
                    << ",\"kind\":\"" << telemetry_value_kind_name_snapshot(value.value_kind)
                    << "\",\"source\":\"" << internal::telemetry_source_name(value.source)
                    << "\",\"value_type\":\"" << internal::value_type_name(value.value_type)
                    << "\",\"value\":" << telemetry_value_payload_json(value) << "}";
            }
        }
        out << "]}";
        return out.str();
    }

    std::string status_json() const
    {
        std::ostringstream out;
        out << "{\"running\":" << (running() ? "true" : "false")
            << ",\"command_queue_size\":" << rtsyn_spsc_command_size(config_.command_queue)
            << ",\"result_queue_size\":" << rtsyn_spsc_result_size(config_.result_queue)
            << ",\"telemetry_queue_size\":" << rtsyn_spsc_telemetry_size(config_.telemetry_queue)
            << ",\"telemetry_values_size\":"
            << rtsyn_spsc_telemetry_values_size(config_.telemetry_values)
            << ",\"events_consumed\":" << total_events_.load()
            << ",\"values_written\":" << total_values_.load()
            << ",\"failed_value_events\":" << failed_value_events_.load()
            << ",\"values_path\":\"" << config_.values_path << "\"";
        {
            std::lock_guard lock(csv_mutex_);
            if (csv_sink_.enabled)
            {
                out << ",\"csv_values_path\":\"" << csv_sink_.path << "\"";
            }
        }
        out << "}";
        return out.str();
    }

  private:
    std::uint64_t next_seq() { return command_seq_.fetch_add(1) + 1; }

    std::optional<rtsyn_spsc_result_message_t>
    wait_for_result(std::uint64_t seq, rtsyn_spsc_command_message_type_t command_type,
                    std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
    {
        std::lock_guard lock(result_mutex_);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        rtsyn_spsc_result_message_t result = {};
        while (std::chrono::steady_clock::now() < deadline)
        {
            for (auto it = pending_results_.begin(); it != pending_results_.end(); ++it)
            {
                if (it->seq == seq && it->command_type == command_type)
                {
                    result = *it;
                    pending_results_.erase(it);
                    return result;
                }
            }

            while (rtsyn_spsc_result_try_pop(config_.result_queue, &result))
            {
                if (result.seq == seq && result.command_type == command_type)
                {
                    return result;
                }
                pending_results_.push_back(result);
                while (pending_results_.size() > 128)
                {
                    pending_results_.pop_front();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return std::nullopt;
    }

    void remember_events(const std::vector<rtsyn_spsc_telemetry_message_t> &events)
    {
        std::lock_guard lock(events_mutex_);
        for (const auto &event : events)
        {
            if (event.type == RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_MEASUREMENT)
            {
                latest_measurement_ = event;
            }
            recent_events_.push_back(event);
            while (recent_events_.size() > RTSYN_API_DEFAULT_RECENT_TELEMETRY_EVENTS)
            {
                recent_events_.pop_front();
            }
        }
    }

    void remember_latest_values(const std::vector<rtsyn_spsc_telemetry_value_t> &values)
    {
        if (values.empty())
        {
            return;
        }

        std::lock_guard lock(events_mutex_);
        for (const auto &value : values)
        {
            latest_values_[{value.node_id, value.value_id, value.value_kind}] = value;
        }
    }

    void telemetry_loop()
    {
        while (running_.load())
        {
            (void)drain_telemetry();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(RTSYN_API_DEFAULT_TELEMETRY_DRAIN_PERIOD_MS));
        }
    }

    void configure_routes()
    {
        server_.Get(RTSYN_API_ENDPOINT_HEALTH,
                    [this](const httplib::Request &, httplib::Response &response) {
            response.set_content(status_json(), "application/json");
        });

        server_.Get(RTSYN_API_ENDPOINT_CAPABILITIES,
                    [](const httplib::Request &, httplib::Response &response) {
                        static constexpr const char *capabilities_json =
                            R"({"commands":{"global":true,"plugin":true,"device":true,"node_remove":true,"connections":true,"telemetry":true,"params":true,"runtime_period":true,"runtime_priority":true,"runtime_deadline_tolerance":true,"csv_values":true,"csv_measurements":true},"runtime":{"nodes":true}})";
                        response.set_content(capabilities_json, "application/json");
                    });

        server_.Get(RTSYN_API_ENDPOINT_RUNTIME_NODES,
                    [this](const httplib::Request &, httplib::Response &response) {
                        response.set_content(runtime_nodes_json(), "application/json");
                    });

        server_.Get(RTSYN_API_ENDPOINT_TELEMETRY_EVENTS,
                    [this](const httplib::Request &, httplib::Response &response) {
                        (void)drain_telemetry();
                        response.set_content(recent_events_json(), "application/json");
                    });

        server_.Get(RTSYN_API_ENDPOINT_TELEMETRY_VALUES_FILE,
                    [this](const httplib::Request &, httplib::Response &response) {
                        response.set_content("{\"path\":\"" + config_.values_path + "\"}",
                                             "application/json");
                    });

        server_.Post(RTSYN_API_ENDPOINT_TELEMETRY_CSV_FILE,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         const auto enabled = internal::parse_bool_field(request.body, "enabled");
                         if (enabled && !*enabled)
                         {
                             stop_csv_telemetry_file();
                             response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
                             response.set_content("{\"accepted\":true,\"enabled\":false}",
                                                  "application/json");
                             return;
                         }

                         const auto path = internal::parse_string_field(request.body, "path");
                         const auto names = internal::parse_string_array_field(request.body, "names");
                         std::optional<std::vector<internal::TelemetryCsvValueSelector>>
                             value_selectors = parse_csv_value_selectors(request.body);
                         if (!value_selectors)
                         {
                             const auto value_ids =
                                 internal::parse_u64_array_field(request.body, "value_ids");
                             if (!value_ids)
                             {
                                 response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                                 response.set_content(
                                     internal::http_error_json("invalid csv telemetry value ids"),
                                     "application/json");
                                 return;
                             }

                             std::vector<internal::TelemetryCsvValueSelector> selectors;
                             selectors.reserve(value_ids->size());
                             for (const auto id : *value_ids)
                             {
                                 if (id > UINT32_MAX)
                                 {
                                     response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                                     response.set_content(
                                         internal::http_error_json(
                                             "invalid csv telemetry value id"),
                                         "application/json");
                                     return;
                                 }
                                 selectors.push_back(
                                     {UINT32_MAX, static_cast<std::uint32_t>(id),
                                      RTSYN_SPSC_TELEMETRY_VALUE_KIND_NONE});
                             }
                             value_selectors = std::move(selectors);
                         }
                         std::optional<std::vector<std::string>> measurement_fields;
                         if (request.body.find("\"measurement_fields\"") != std::string::npos)
                         {
                             measurement_fields = internal::parse_string_array_field(
                                 request.body, "measurement_fields");
                             if (!measurement_fields)
                             {
                                 response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                                 response.set_content(
                                     internal::http_error_json(
                                         "invalid csv telemetry measurement fields"),
                                     "application/json");
                                 return;
                             }
                         }
                         else
                         {
                             measurement_fields = std::vector<std::string> {};
                         }

                         if (!path || path->empty() || !names || !value_selectors
                             || names->empty()
                             || names->size()
                                    != value_selectors->size() + measurement_fields->size())
                         {
                             response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                             response.set_content(
                                 internal::http_error_json("invalid csv telemetry request"),
                                 "application/json");
                             return;
                         }

                         if (!configure_csv_telemetry_file(*path, *names, *value_selectors,
                                                           *measurement_fields))
                         {
                             response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
                             response.set_content(
                                 internal::http_error_json("failed to open csv telemetry file"),
                                 "application/json");
                             return;
                         }

                         response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
                         response.set_content("{\"accepted\":true}", "application/json");
                     });

        server_.Get(RTSYN_API_ENDPOINT_MEASUREMENTS,
                    [this](const httplib::Request &, httplib::Response &response) {
                        (void)drain_telemetry();
                        response.set_content(latest_measurement_json(), "application/json");
                    });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_GLOBAL,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         const auto command = internal::parse_global_command(request.body);
                         if (!command)
                         {
                             response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                             response.set_content(internal::http_error_json("invalid command"),
                                                  "application/json");
                             return;
                         }
                         if (!push_global_command(*command))
                         {
                             response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
                             response.set_content(internal::http_error_json("command queue full"),
                                                  "application/json");
                             return;
                         }
                         response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
                         response.set_content("{\"accepted\":true}", "application/json");
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_PLUGIN,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         const auto plugin_id = internal::parse_u64_field(request.body, "plugin_id");
                         const auto state = internal::parse_u64_field(request.body, "plugin_state");
                         if (!plugin_id || !state || *plugin_id > UINT32_MAX || *state > UINT8_MAX)
                         {
                             response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                             response.set_content(internal::http_error_json("invalid plugin command"),
                                                  "application/json");
                             return;
                         }
                         if (!push_plugin_update(static_cast<std::uint32_t>(*plugin_id),
                                                 static_cast<std::uint8_t>(*state)))
                         {
                             response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
                             response.set_content(internal::http_error_json("command queue full"),
                                                  "application/json");
                             return;
                         }
                         remember_runtime_node_state(static_cast<std::uint32_t>(*plugin_id),
                                                     static_cast<std::uint8_t>(*state));
                         response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
                         response.set_content("{\"accepted\":true}", "application/json");
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_LOAD_PLUGIN,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         handle_node_load_request(request, response, RTSYN_ABI_NODE_PLUGIN);
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_ADD_PLUGIN,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         handle_node_add_request(request, response, RTSYN_ABI_NODE_PLUGIN);
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_LOAD_DEVICE,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         handle_node_load_request(request, response, RTSYN_ABI_NODE_DEVICE);
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_ADD_DEVICE,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         handle_node_add_request(request, response, RTSYN_ABI_NODE_DEVICE);
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_REMOVE_NODE,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         handle_node_remove_request(request, response);
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_ADD_CONNECTION,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         const auto connection_id =
                             internal::parse_u64_field(request.body, "connection_id");
                         const auto source_node_id =
                             internal::parse_u64_field(request.body, "source_node_id");
                         const auto source_port_id =
                             internal::parse_u64_field(request.body, "source_port_id");
                         const auto destination_node_id =
                             internal::parse_u64_field(request.body, "destination_node_id");
                         const auto destination_port_id =
                             internal::parse_u64_field(request.body, "destination_port_id");
                         if (!connection_id || !source_node_id || !source_port_id
                             || !destination_node_id || !destination_port_id
                             || *connection_id >= UINT32_MAX || *source_node_id >= UINT32_MAX
                             || *source_port_id >= UINT32_MAX
                             || *destination_node_id >= UINT32_MAX
                             || *destination_port_id >= UINT32_MAX)
                         {
                             response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                             response.set_content(
                                 internal::http_error_json("invalid connection command"),
                                 "application/json");
                             return;
                         }
                         if (!add_connection(static_cast<std::uint32_t>(*connection_id),
                                             static_cast<std::uint32_t>(*source_node_id),
                                             static_cast<std::uint32_t>(*source_port_id),
                                             static_cast<std::uint32_t>(*destination_node_id),
                                             static_cast<std::uint32_t>(*destination_port_id)))
                         {
                             response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
                             response.set_content(internal::http_error_json("command queue full"),
                                                  "application/json");
                             return;
                         }
                         remember_runtime_connection(
                             {static_cast<std::uint32_t>(*connection_id),
                              static_cast<std::uint32_t>(*source_node_id),
                              static_cast<std::uint32_t>(*source_port_id),
                              static_cast<std::uint32_t>(*destination_node_id),
                              static_cast<std::uint32_t>(*destination_port_id)});
                         response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
                         response.set_content("{\"accepted\":true}", "application/json");
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_REMOVE_CONNECTION,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         const auto connection_id =
                             internal::parse_u64_field(request.body, "connection_id");
                         if (!connection_id || *connection_id >= UINT32_MAX)
                         {
                             response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                             response.set_content(
                                 internal::http_error_json("invalid connection command"),
                                 "application/json");
                             return;
                         }
                         if (!remove_connection(static_cast<std::uint32_t>(*connection_id)))
                         {
                             response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
                             response.set_content(internal::http_error_json("command queue full"),
                                                  "application/json");
                             return;
                         }
                         forget_runtime_connection(static_cast<std::uint32_t>(*connection_id));
                         response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
                         response.set_content("{\"accepted\":true}", "application/json");
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_PORT_VALUES,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         const auto plugin_id = internal::parse_u64_field(request.body, "plugin_id");
                         const auto send = internal::parse_bool_field(request.body, "send");
                         const auto mask = internal::parse_u64_field(request.body, "portsyn_mask");
                         if (!plugin_id || !send || !mask || *plugin_id > UINT32_MAX)
                         {
                             response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                             response.set_content(internal::http_error_json("invalid port request"),
                                                  "application/json");
                             return;
                         }
                         if (!request_port_values(static_cast<std::uint32_t>(*plugin_id), *send,
                                                  *mask))
                         {
                             response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
                             response.set_content(internal::http_error_json("command queue full"),
                                                  "application/json");
                             return;
                         }
                         response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
                         response.set_content("{\"accepted\":true}", "application/json");
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_VARIABLES,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         const auto plugin_id = internal::parse_u64_field(request.body, "plugin_id");
                         const auto send = internal::parse_bool_field(request.body, "send");
                         const auto mask = internal::parse_u64_field(request.body, "variable_mask");
                         if (!plugin_id || !send || !mask || *plugin_id > UINT32_MAX)
                         {
                             response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                             response.set_content(
                                 internal::http_error_json("invalid variable request"),
                                 "application/json");
                             return;
                         }
                         if (!request_variables(static_cast<std::uint32_t>(*plugin_id), *send,
                                                *mask))
                         {
                             response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
                             response.set_content(internal::http_error_json("command queue full"),
                                                  "application/json");
                             return;
                         }
                         response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
                         response.set_content("{\"accepted\":true}", "application/json");
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_SET_PARAM,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         const auto node_id = internal::parse_u64_field(request.body, "node_id");
                         const auto param_id = internal::parse_u64_field(request.body, "param_id");
                         const auto value_type = parse_value_type(request.body);
                         if (!node_id || !param_id || !value_type || *node_id > UINT32_MAX
                             || *param_id > UINT32_MAX)
                         {
                             response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                             response.set_content(
                                 internal::http_error_json("invalid param command"),
                                 "application/json");
                             return;
                         }

                         const auto value = parse_param_value(request.body, *value_type);
                         if (!value)
                         {
                             response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                             response.set_content(
                                 internal::http_error_json("invalid param value"),
                                 "application/json");
                             return;
                         }

                         if (!set_param(static_cast<std::uint32_t>(*node_id),
                                        static_cast<std::uint32_t>(*param_id), *value_type,
                                        *value))
                         {
                             response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
                             response.set_content(internal::http_error_json("command queue full"),
                                                  "application/json");
                             return;
                         }
                         remember_runtime_param({static_cast<std::uint32_t>(*node_id),
                                                 static_cast<std::uint32_t>(*param_id),
                                                 *value_type,
                                                 *value});
                         response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
                         response.set_content("{\"accepted\":true}", "application/json");
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_RUNTIME_PERIOD,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         const auto period_ns = internal::parse_u64_field(request.body, "period_ns");
                         if (!period_ns || *period_ns == 0)
                         {
                             response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                             response.set_content(
                                 internal::http_error_json("invalid runtime period"),
                                 "application/json");
                             return;
                         }
                         if (!set_runtime_period(*period_ns))
                         {
                             response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
                             response.set_content(internal::http_error_json("command queue full"),
                                                  "application/json");
                             return;
                         }
                         response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
                         response.set_content("{\"accepted\":true}", "application/json");
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_RUNTIME_PRIORITY,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         const auto priority = internal::parse_u64_field(request.body, "priority");
                         if (!priority || *priority > 99)
                         {
                             response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                             response.set_content(
                                 internal::http_error_json("invalid runtime priority"),
                                 "application/json");
                             return;
                         }
                         if (!set_runtime_priority(static_cast<std::int32_t>(*priority)))
                         {
                             response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
                             response.set_content(internal::http_error_json("command queue full"),
                                                  "application/json");
                             return;
                         }
                         response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
                         response.set_content("{\"accepted\":true}", "application/json");
                     });

        server_.Post(RTSYN_API_ENDPOINT_COMMAND_RUNTIME_DEADLINE_TOLERANCE,
                     [this](const httplib::Request &request, httplib::Response &response) {
                         const auto tolerance_ns =
                             internal::parse_u64_field(request.body, "tolerance_ns");
                         if (!tolerance_ns)
                         {
                             response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
                             response.set_content(
                                 internal::http_error_json("invalid runtime deadline tolerance"),
                                 "application/json");
                             return;
                         }
                         if (!set_runtime_deadline_tolerance(*tolerance_ns))
                         {
                             response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
                             response.set_content(internal::http_error_json("command queue full"),
                                                  "application/json");
                             return;
                         }
                         response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
                         response.set_content("{\"accepted\":true}", "application/json");
                     });
    }

    void handle_node_load_request(const httplib::Request &request, httplib::Response &response,
                                  rtsyn_abi_node_type_t node_type)
    {
        const auto module_path = internal::parse_string_field(request.body, "module_path");
        if (!module_path || module_path->empty()
            || module_path->size() >= RTSYN_SPSC_COMMAND_MODULE_PATH_MAX_SIZE)
        {
            response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
            response.set_content(internal::http_error_json("invalid module path"),
                                 "application/json");
            return;
        }

        const std::uint64_t seq = next_seq();
        bool accepted = false;
        if (node_type == RTSYN_ABI_NODE_PLUGIN)
        {
            accepted = internal::push_load_node(config_.command_queue, seq, RTSYN_ABI_NODE_PLUGIN,
                                                *module_path);
        } else if (node_type == RTSYN_ABI_NODE_DEVICE)
        {
            accepted = internal::push_load_node(config_.command_queue, seq, RTSYN_ABI_NODE_DEVICE,
                                                *module_path);
        }

        if (!accepted)
        {
            response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
            response.set_content(internal::http_error_json("command queue full"),
                                 "application/json");
            return;
        }

        const auto result = wait_for_result(seq, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE,
                                            std::chrono::seconds(20));
        if (!result)
        {
            response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
            response.set_content("{\"accepted\":true,\"pending\":true}", "application/json");
            return;
        }

        response.status = result->status == RTSYN_SPSC_RESULT_STATUS_OK
                              ? RTSYN_API_HTTP_STATUS_ACCEPTED
                              : RTSYN_API_HTTP_STATUS_UNAVAILABLE;
        if (result->status == RTSYN_SPSC_RESULT_STATUS_OK)
        {
            remember_loaded_descriptor(*module_path, *result);
        }
        response.set_content(internal::result_message_to_json(*result), "application/json");
    }

    void handle_node_add_request(const httplib::Request &request, httplib::Response &response,
                                 rtsyn_abi_node_type_t node_type)
    {
        const auto node_name = internal::parse_string_field(request.body, "node_name");
        if (!node_name || node_name->empty()
            || node_name->size() >= RTSYN_SPSC_COMMAND_NODE_NAME_MAX_SIZE)
        {
            response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
            response.set_content(internal::http_error_json("invalid node name"),
                                 "application/json");
            return;
        }

        const std::uint64_t seq = next_seq();
        bool accepted = false;
        if (node_type == RTSYN_ABI_NODE_PLUGIN)
        {
            accepted =
                internal::push_add_node(config_.command_queue, seq, RTSYN_ABI_NODE_PLUGIN,
                                        *node_name);
        } else if (node_type == RTSYN_ABI_NODE_DEVICE)
        {
            accepted =
                internal::push_add_node(config_.command_queue, seq, RTSYN_ABI_NODE_DEVICE,
                                        *node_name);
        }

        if (!accepted)
        {
            response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
            response.set_content(internal::http_error_json("command queue full"),
                                 "application/json");
            return;
        }

        const auto result = wait_for_result(seq, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE);
        if (!result)
        {
            response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
            response.set_content("{\"accepted\":true,\"pending\":true}", "application/json");
            return;
        }

        response.status = result->status == RTSYN_SPSC_RESULT_STATUS_OK
                              ? RTSYN_API_HTTP_STATUS_ACCEPTED
                              : RTSYN_API_HTTP_STATUS_UNAVAILABLE;
        if (result->status == RTSYN_SPSC_RESULT_STATUS_OK)
        {
            remember_runtime_node(*result);
        }
        response.set_content(internal::result_message_to_json(*result), "application/json");
    }

    void handle_node_remove_request(const httplib::Request &request, httplib::Response &response)
    {
        const auto node_id = internal::parse_u64_field(request.body, "node_id");
        if (!node_id || *node_id >= UINT32_MAX)
        {
            response.status = RTSYN_API_HTTP_STATUS_BAD_REQUEST;
            response.set_content(internal::http_error_json("invalid node id"), "application/json");
            return;
        }

        const std::uint64_t seq = next_seq();
        if (!internal::push_remove_node(config_.command_queue, seq,
                                        static_cast<std::uint32_t>(*node_id)))
        {
            response.status = RTSYN_API_HTTP_STATUS_UNAVAILABLE;
            response.set_content(internal::http_error_json("command queue full"),
                                 "application/json");
            return;
        }

        const auto result = wait_for_result(seq, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REMOVE_NODE);
        if (!result)
        {
            response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
            response.set_content("{\"accepted\":true,\"pending\":true}", "application/json");
            return;
        }

        response.status = result->status == RTSYN_SPSC_RESULT_STATUS_OK
                              ? RTSYN_API_HTTP_STATUS_ACCEPTED
                              : RTSYN_API_HTTP_STATUS_UNAVAILABLE;
        if (result->status == RTSYN_SPSC_RESULT_STATUS_OK)
        {
            forget_runtime_node(result->node_id);
        }
        response.set_content(internal::result_message_to_json(*result), "application/json");
    }

    void remember_loaded_descriptor(const std::string &module_path,
                                    const rtsyn_spsc_result_message_t &result)
    {
        {
            std::lock_guard lock(runtime_nodes_mutex_);
            loaded_descriptors_[result.node.name] = result;
            loaded_module_paths_[result.node.name] = module_path;
            remove_runtime_nodes_by_descriptor_locked(result.node.name);
        }
        persist_runtime_nodes();
    }

    void forget_runtime_node(std::uint32_t node_id)
    {
        {
            std::lock_guard lock(runtime_nodes_mutex_);
            runtime_nodes_.erase(node_id);
            runtime_node_states_.erase(node_id);
            remove_runtime_connections_for_node_locked(node_id);
            remove_runtime_params_for_node_locked(node_id);
        }
        forget_latest_values_for_node(node_id);
        persist_runtime_nodes();
    }

    void remember_runtime_node(const rtsyn_spsc_result_message_t &result)
    {
        {
            std::lock_guard lock(runtime_nodes_mutex_);
            runtime_nodes_[result.node_id] = result;
            runtime_node_states_[result.node_id] = 4;
        }
        persist_runtime_nodes();
    }

    void remember_runtime_node_state(std::uint32_t node_id, std::uint8_t state)
    {
        {
            std::lock_guard lock(runtime_nodes_mutex_);
            if (runtime_nodes_.contains(node_id))
            {
                runtime_node_states_[node_id] = state;
            }
        }
        persist_runtime_nodes();
    }

    void remember_runtime_connection(const RuntimeConnectionSnapshot &connection)
    {
        std::lock_guard lock(runtime_nodes_mutex_);
        runtime_connections_[connection.connection_id] = connection;
    }

    void forget_runtime_connection(std::uint32_t connection_id)
    {
        std::lock_guard lock(runtime_nodes_mutex_);
        runtime_connections_.erase(connection_id);
    }

    void remember_runtime_param(const RuntimeParamSnapshot &param)
    {
        std::lock_guard lock(runtime_nodes_mutex_);
        runtime_params_[{param.node_id, param.param_id}] = param;
    }

    void forget_latest_values_for_node(std::uint32_t node_id)
    {
        std::lock_guard lock(events_mutex_);
        for (auto it = latest_values_.begin(); it != latest_values_.end();)
        {
            if (it->first.node_id == node_id)
            {
                it = latest_values_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void drain_result_snapshots()
    {
        std::vector<rtsyn_spsc_result_message_t> results;
        {
            std::lock_guard lock(result_mutex_);
            results.reserve(pending_results_.size());
            for (const auto &result : pending_results_)
            {
                results.push_back(result);
            }

            rtsyn_spsc_result_message_t result = {};
            while (rtsyn_spsc_result_try_pop(config_.result_queue, &result))
            {
                results.push_back(result);
                pending_results_.push_back(result);
                while (pending_results_.size() > 128)
                {
                    pending_results_.pop_front();
                }
            }
        }

        if (results.empty())
        {
            return;
        }

        bool changed = false;
        {
            std::lock_guard lock(runtime_nodes_mutex_);
            for (const auto &result : results)
            {
                changed = remember_result_snapshot_locked(result) || changed;
            }
        }
        if (changed)
        {
            persist_runtime_nodes();
        }
    }

    bool request_live_runtime_nodes_snapshot()
    {
        const std::uint64_t seq = next_seq();
        if (!internal::push_runtime_nodes_request(config_.command_queue, seq))
        {
            return false;
        }

        {
            std::lock_guard lock(runtime_nodes_mutex_);
            runtime_nodes_.clear();
            runtime_node_states_.clear();
            runtime_connections_.clear();
        }

        bool changed = false;
        bool completed = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        while (std::chrono::steady_clock::now() < deadline && !completed)
        {
            rtsyn_spsc_result_message_t result = {};
            {
                std::lock_guard lock(result_mutex_);
                for (auto it = pending_results_.begin(); it != pending_results_.end();)
                {
                    if (it->seq == seq
                        && it->command_type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES)
                    {
                        result = *it;
                        it = pending_results_.erase(it);
                        completed = merge_runtime_snapshot_result(result) || completed;
                        changed = result.node_id != UINT32_MAX
                                  || (result.status_code != UINT32_MAX
                                      && result.connection.connection_id != UINT32_MAX)
                                  || changed;
                    }
                    else
                    {
                        ++it;
                    }
                }

                while (rtsyn_spsc_result_try_pop(config_.result_queue, &result))
                {
                    if (result.seq == seq
                        && result.command_type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES)
                    {
                        completed = merge_runtime_snapshot_result(result) || completed;
                        changed = result.node_id != UINT32_MAX
                                  || (result.status_code != UINT32_MAX
                                      && result.connection.connection_id != UINT32_MAX)
                                  || changed;
                    }
                    else
                    {
                        pending_results_.push_back(result);
                        while (pending_results_.size() > 128)
                        {
                            pending_results_.pop_front();
                        }
                    }
                }
            }

            if (!completed)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        if (changed)
        {
            persist_runtime_nodes();
        }
        return completed;
    }

    bool merge_runtime_snapshot_result(const rtsyn_spsc_result_message_t &result)
    {
        if (result.node_id == UINT32_MAX && result.status_code == UINT32_MAX)
        {
            return true;
        }

        std::lock_guard lock(runtime_nodes_mutex_);
        (void)remember_result_snapshot_locked(result);
        return false;
    }

    bool remember_result_snapshot_locked(const rtsyn_spsc_result_message_t &result)
    {
        if (result.status != RTSYN_SPSC_RESULT_STATUS_OK)
        {
            return false;
        }

        if (result.command_type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE)
        {
            loaded_descriptors_[result.node.name] = result;
            remove_runtime_nodes_by_descriptor_locked(result.node.name);
            return true;
        }

        if (result.command_type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REMOVE_NODE)
        {
            runtime_nodes_.erase(result.node_id);
            runtime_node_states_.erase(result.node_id);
            return true;
        }

        if (result.command_type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES
            && result.node_id == UINT32_MAX
            && result.status_code != UINT32_MAX
            && result.connection.connection_id != UINT32_MAX)
        {
            runtime_connections_[result.connection.connection_id] = {
                result.connection.connection_id,
                result.connection.source_node_id,
                result.connection.source_port_id,
                result.connection.destination_node_id,
                result.connection.destination_port_id,
            };
            return true;
        }

        if (result.command_type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE
            || result.command_type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES)
        {
            runtime_nodes_[result.node_id] = result;
            if (result.command_type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES)
            {
                runtime_node_states_[result.node_id] = (std::uint8_t)result.status_code;
            }
            else if (!runtime_node_states_.contains(result.node_id))
            {
                runtime_node_states_[result.node_id] = 4;
            }
            return true;
        }
        return false;
    }

    void persist_runtime_nodes() const
    {
        std::lock_guard lock(runtime_nodes_mutex_);
        std::ofstream out(runtime_state_path_for_port(config_.port), std::ios::binary);
        if (!out)
        {
            return;
        }

        const char magic[8] = {'R', 'T', 'S', 'Y', 'N', 'R', 'S', '1'};
        const std::uint32_t version = 1;
        const auto node_count = static_cast<std::uint32_t>(runtime_nodes_.size());
        const auto descriptor_count = static_cast<std::uint32_t>(loaded_descriptors_.size());
        out.write(magic, sizeof(magic));
        out.write(reinterpret_cast<const char *>(&version), sizeof(version));
        out.write(reinterpret_cast<const char *>(&node_count), sizeof(node_count));
        out.write(reinterpret_cast<const char *>(&descriptor_count), sizeof(descriptor_count));

        for (const auto &[node_id, node] : runtime_nodes_)
        {
            const std::uint8_t state =
                runtime_node_states_.contains(node_id) ? runtime_node_states_.at(node_id) : 4;
            out.write(reinterpret_cast<const char *>(&node), sizeof(node));
            out.write(reinterpret_cast<const char *>(&state), sizeof(state));
        }

        for (const auto &[name, descriptor] : loaded_descriptors_)
        {
            const auto path = loaded_module_paths_.contains(name) ? loaded_module_paths_.at(name)
                                                                 : std::string{};
            const auto path_size = static_cast<std::uint32_t>(path.size());
            out.write(reinterpret_cast<const char *>(&descriptor), sizeof(descriptor));
            out.write(reinterpret_cast<const char *>(&path_size), sizeof(path_size));
            out.write(path.data(), path.size());
        }
    }

    void load_runtime_nodes_from_disk()
    {
        std::ifstream in(runtime_state_path_for_port(config_.port), std::ios::binary);
        if (!in)
        {
            return;
        }

        char magic[8] = {};
        std::uint32_t version = 0;
        std::uint32_t node_count = 0;
        std::uint32_t descriptor_count = 0;
        in.read(magic, sizeof(magic));
        in.read(reinterpret_cast<char *>(&version), sizeof(version));
        in.read(reinterpret_cast<char *>(&node_count), sizeof(node_count));
        in.read(reinterpret_cast<char *>(&descriptor_count), sizeof(descriptor_count));
        const char expected_magic[8] = {'R', 'T', 'S', 'Y', 'N', 'R', 'S', '1'};
        if (!in || version != 1 || memcmp(magic, expected_magic, sizeof(magic)) != 0)
        {
            return;
        }

        std::lock_guard lock(runtime_nodes_mutex_);
        for (std::uint32_t i = 0; i < node_count && i < 1024; i++)
        {
            rtsyn_spsc_result_message_t node = {};
            std::uint8_t state = 4;
            in.read(reinterpret_cast<char *>(&node), sizeof(node));
            in.read(reinterpret_cast<char *>(&state), sizeof(state));
            if (!in)
            {
                return;
            }
            runtime_nodes_[node.node_id] = node;
            runtime_node_states_[node.node_id] = state;
        }

        for (std::uint32_t i = 0; i < descriptor_count && i < 1024; i++)
        {
            rtsyn_spsc_result_message_t descriptor = {};
            std::uint32_t path_size = 0;
            in.read(reinterpret_cast<char *>(&descriptor), sizeof(descriptor));
            in.read(reinterpret_cast<char *>(&path_size), sizeof(path_size));
            if (!in || path_size >= RTSYN_SPSC_COMMAND_MODULE_PATH_MAX_SIZE)
            {
                return;
            }
            std::string path(path_size, '\0');
            in.read(path.data(), path.size());
            if (!in)
            {
                return;
            }
            loaded_descriptors_[descriptor.node.name] = descriptor;
            loaded_module_paths_[descriptor.node.name] = path;
        }
    }

    void remove_runtime_nodes_by_descriptor_locked(const char *descriptor_name)
    {
        for (auto it = runtime_nodes_.begin(); it != runtime_nodes_.end();)
        {
            if (std::string(it->second.node.name) == descriptor_name)
            {
                const auto node_id = it->first;
                runtime_node_states_.erase(it->first);
                it = runtime_nodes_.erase(it);
                remove_runtime_connections_for_node_locked(node_id);
                remove_runtime_params_for_node_locked(node_id);
                forget_latest_values_for_node(node_id);
            }
            else
            {
                ++it;
            }
        }
    }

    void remove_runtime_connections_for_node_locked(std::uint32_t node_id)
    {
        for (auto it = runtime_connections_.begin(); it != runtime_connections_.end();)
        {
            if (it->second.source_node_id == node_id || it->second.destination_node_id == node_id)
            {
                it = runtime_connections_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void remove_runtime_params_for_node_locked(std::uint32_t node_id)
    {
        for (auto it = runtime_params_.begin(); it != runtime_params_.end();)
        {
            if (it->first.node_id == node_id)
            {
                it = runtime_params_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    Config config_;
    httplib::Server server_;
    std::thread server_thread_;
    std::thread telemetry_thread_;
    std::ofstream values_stream_;
    std::atomic_bool running_ = false;
    std::atomic_uint64_t command_seq_ = 0;
    std::atomic_uint64_t total_events_ = 0;
    std::atomic_uint64_t total_values_ = 0;
    std::atomic_uint64_t failed_value_events_ = 0;
    mutable std::mutex events_mutex_;
    mutable std::mutex csv_mutex_;
    mutable std::mutex runtime_nodes_mutex_;
    std::mutex result_mutex_;
    std::deque<rtsyn_spsc_telemetry_message_t> recent_events_;
    std::optional<rtsyn_spsc_telemetry_message_t> latest_measurement_;
    std::map<LatestTelemetryValueKey, rtsyn_spsc_telemetry_value_t> latest_values_;
    internal::TelemetryCsvSink csv_sink_;
    std::deque<rtsyn_spsc_result_message_t> pending_results_;
    std::map<std::uint32_t, rtsyn_spsc_result_message_t> runtime_nodes_;
    std::map<std::uint32_t, std::uint8_t> runtime_node_states_;
    std::map<std::uint32_t, RuntimeConnectionSnapshot> runtime_connections_;
    std::map<RuntimeParamKey, RuntimeParamSnapshot> runtime_params_;
    std::map<std::string, rtsyn_spsc_result_message_t> loaded_descriptors_;
    std::map<std::string, std::string> loaded_module_paths_;
};

Api::Api(Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}

Api::~Api() = default;

bool
Api::valid() const
{
    return impl_->valid();
}

bool
Api::start()
{
    return impl_->start();
}

void
Api::stop()
{
    impl_->stop();
}

bool
Api::running() const
{
    return impl_->running();
}

void
Api::wait()
{
    impl_->wait();
}

bool
Api::push_global_command(GlobalCommand command)
{
    return impl_->push_global_command(command);
}

bool
Api::push_plugin_update(std::uint32_t plugin_id, std::uint8_t plugin_state)
{
    return impl_->push_plugin_update(plugin_id, plugin_state);
}

bool
Api::load_plugin(const std::string &module_path)
{
    return impl_->load_plugin(module_path);
}

bool
Api::add_plugin(const std::string &node_name)
{
    return impl_->add_plugin(node_name);
}

bool
Api::load_device(const std::string &module_path)
{
    return impl_->load_device(module_path);
}

bool
Api::add_device(const std::string &node_name)
{
    return impl_->add_device(node_name);
}

bool
Api::add_connection(std::uint32_t connection_id, std::uint32_t source_node_id,
                    std::uint32_t source_port_id, std::uint32_t destination_node_id,
                    std::uint32_t destination_port_id)
{
    return impl_->add_connection(connection_id, source_node_id, source_port_id, destination_node_id,
                                 destination_port_id);
}

bool
Api::remove_connection(std::uint32_t connection_id)
{
    return impl_->remove_connection(connection_id);
}

bool
Api::request_port_values(std::uint32_t plugin_id, bool send, std::uint64_t portsyn_mask)
{
    return impl_->request_port_values(plugin_id, send, portsyn_mask);
}

bool
Api::request_variables(std::uint32_t plugin_id, bool send, std::uint64_t variable_mask)
{
    return impl_->request_variables(plugin_id, send, variable_mask);
}

bool
Api::set_param(std::uint32_t node_id, std::uint32_t param_id,
               rtsyn_abi_value_type_t value_type,
               const rtsyn_spsc_command_param_value_t &value)
{
    return impl_->set_param(node_id, param_id, value_type, value);
}

bool
Api::set_runtime_period(std::uint64_t period_ns)
{
    return impl_->set_runtime_period(period_ns);
}

bool
Api::set_runtime_priority(std::int32_t priority)
{
    return impl_->set_runtime_priority(priority);
}

bool
Api::set_runtime_deadline_tolerance(std::uint64_t tolerance_ns)
{
    return impl_->set_runtime_deadline_tolerance(tolerance_ns);
}

bool
Api::configure_csv_values_file(const std::string &path, const std::vector<std::string> &names,
                               const std::vector<std::uint32_t> &value_ids)
{
    return impl_->configure_csv_values_file(path, names, value_ids);
}

DrainResult
Api::drain_telemetry()
{
    return impl_->drain_telemetry();
}

std::string
Api::recent_events_json() const
{
    return impl_->recent_events_json();
}

std::string
Api::latest_measurement_json() const
{
    return impl_->latest_measurement_json();
}

std::string
Api::status_json() const
{
    return impl_->status_json();
}

} // namespace rtsyn::api
