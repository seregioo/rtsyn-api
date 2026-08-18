/**
 * @file rtsyn/api.hpp
 * @author Sergio Hidalgo (sergiohg.dev@gmail.com)
 * @brief Public C++ API facade for the RTSyn HTTP bridge.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @copyright Copyright (c) Sergio Hidalgo 2026
 */
#ifndef RTSYN_API_HPP
#define RTSYN_API_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <rtsyn/api/defaults.h>
#include <string>

extern "C" {
#include <rtsyn/spsc/command/spsc.h>
#include <rtsyn/spsc/result/spsc.h>
#include <rtsyn/spsc/telemetry/spsc.h>
#include <rtsyn/spsc/telemetry/values.h>
}

namespace rtsyn::api {

enum class GlobalCommand : std::uint8_t {
    none = 0,
    stop = 1,
    pause = 2,
    resume = 3,
};

struct Config {
    rtsyn_spsc_command_queue_t *command_queue = nullptr;
    rtsyn_spsc_result_queue_t *result_queue = nullptr;
    rtsyn_spsc_telemetry_queue_t *telemetry_queue = nullptr;
    rtsyn_spsc_telemetry_values_t *telemetry_values = nullptr;
    std::string bind_host = RTSYN_API_DEFAULT_HOST;
    int port = RTSYN_API_DEFAULT_PORT;
    std::string values_path = RTSYN_API_DEFAULT_VALUES_FILE;
    std::size_t max_telemetry_events_per_drain =
        RTSYN_API_DEFAULT_MAX_TELEMETRY_EVENTS_PER_DRAIN;
};

struct DrainResult {
    std::size_t event_count = 0;
    std::size_t value_count = 0;
    std::size_t failed_value_event_count = 0;
};

class Api {
  public:
    explicit Api(Config config);
    ~Api();

    Api(const Api &) = delete;
    Api &operator=(const Api &) = delete;

    bool valid() const;
    bool start();
    void stop();
    bool running() const;
    void wait();

    bool push_global_command(GlobalCommand command);
    bool push_plugin_update(std::uint32_t plugin_id, std::uint8_t plugin_state);
    bool load_plugin(const std::string &module_path);
    bool add_plugin(const std::string &node_name);
    bool load_device(const std::string &module_path);
    bool add_device(const std::string &node_name);
    bool add_connection(std::uint32_t connection_id, std::uint32_t source_node_id,
                        std::uint32_t source_port_id, std::uint32_t destination_node_id,
                        std::uint32_t destination_port_id);
    bool remove_connection(std::uint32_t connection_id);
    bool request_port_values(std::uint32_t plugin_id, bool send, std::uint64_t portsyn_mask);
    bool request_variables(std::uint32_t plugin_id, bool send, std::uint64_t variable_mask);
    bool set_param(std::uint32_t node_id, std::uint32_t param_id,
                   rtsyn_abi_value_type_t value_type,
                   const rtsyn_spsc_command_param_value_t &value);
    bool set_runtime_period(std::uint64_t period_ns);

    DrainResult drain_telemetry();
    std::string recent_events_json() const;
    std::string latest_measurement_json() const;
    std::string status_json() const;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rtsyn::api

#endif // RTSYN_API_HPP
