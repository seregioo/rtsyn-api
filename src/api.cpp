#include <rtsyn/api.hpp>

#include "rtsyn/internal/api/commands.hpp"
#include "rtsyn/internal/api/http.hpp"
#include "rtsyn/internal/api/serialization.hpp"
#include "rtsyn/internal/api/telemetry.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <httplib.h>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

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

} // namespace

class Api::Impl {
  public:
    explicit Impl(Config config) : config_(std::move(config)) {}

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
        if (server_thread_.joinable())
        {
            server_thread_.join();
        }
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

    DrainResult drain_telemetry()
    {
        const auto drain = internal::drain_telemetry(config_.telemetry_queue,
                                                     config_.telemetry_values, config_.values_path,
                                                     config_.max_telemetry_events_per_drain);
        remember_events(drain.events);
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
            << ",\"values_path\":\"" << config_.values_path << "\"}";
        return out.str();
    }

  private:
    std::uint64_t next_seq() { return command_seq_.fetch_add(1) + 1; }

    std::optional<rtsyn_spsc_result_message_t>
    wait_for_result(std::uint64_t seq, rtsyn_spsc_command_message_type_t command_type)
    {
        std::lock_guard lock(result_mutex_);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
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
                        response.set_content(
                            "{\"commands\":{\"global\":true,\"plugin\":true,\"device\":true,"
                            "\"connections\":true,\"telemetry\":true,\"params\":true,"
                            "\"runtime_period\":true}}",
                            "application/json");
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

        const auto result = wait_for_result(seq, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE);
        if (!result)
        {
            response.status = RTSYN_API_HTTP_STATUS_ACCEPTED;
            response.set_content("{\"accepted\":true,\"pending\":true}", "application/json");
            return;
        }

        response.status = result->status == RTSYN_SPSC_RESULT_STATUS_OK
                              ? RTSYN_API_HTTP_STATUS_ACCEPTED
                              : RTSYN_API_HTTP_STATUS_UNAVAILABLE;
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
        response.set_content(internal::result_message_to_json(*result), "application/json");
    }

    Config config_;
    httplib::Server server_;
    std::thread server_thread_;
    std::thread telemetry_thread_;
    std::atomic_bool running_ = false;
    std::atomic_uint64_t command_seq_ = 0;
    std::atomic_uint64_t total_events_ = 0;
    std::atomic_uint64_t total_values_ = 0;
    std::atomic_uint64_t failed_value_events_ = 0;
    mutable std::mutex events_mutex_;
    std::mutex result_mutex_;
    std::deque<rtsyn_spsc_telemetry_message_t> recent_events_;
    std::optional<rtsyn_spsc_telemetry_message_t> latest_measurement_;
    std::deque<rtsyn_spsc_result_message_t> pending_results_;
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
