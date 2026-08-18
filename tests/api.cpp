#include <gtest/gtest.h>

#include <chrono>
#include <httplib.h>
#include <memory>
#include <rtsyn/api.hpp>
#include <thread>

class ApiTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        commands_ = std::make_unique<rtsyn_spsc_command_queue_t>();
        results_ = std::make_unique<rtsyn_spsc_result_queue_t>();
        telemetry_ = std::make_unique<rtsyn_spsc_telemetry_queue_t>();
        values_ = std::make_unique<rtsyn_spsc_telemetry_values_t>();
        rtsyn_spsc_command_init(commands_.get());
        rtsyn_spsc_result_init(results_.get());
        rtsyn_spsc_telemetry_init(telemetry_.get());
        rtsyn_spsc_telemetry_values_init(values_.get());
    }

    rtsyn::api::Config config()
    {
        rtsyn::api::Config config;
        config.command_queue = commands_.get();
        config.result_queue = results_.get();
        config.telemetry_queue = telemetry_.get();
        config.telemetry_values = values_.get();
        config.port = 18080;
        config.values_path = "/tmp/rtsyn-api-test-values";
        return config;
    }

    std::unique_ptr<rtsyn_spsc_command_queue_t> commands_;
    std::unique_ptr<rtsyn_spsc_result_queue_t> results_;
    std::unique_ptr<rtsyn_spsc_telemetry_queue_t> telemetry_;
    std::unique_ptr<rtsyn_spsc_telemetry_values_t> values_;
};

TEST_F(ApiTest, RejectsMissingQueues)
{
    rtsyn::api::Config invalid;
    rtsyn::api::Api api(invalid);

    EXPECT_FALSE(api.valid());
}

TEST_F(ApiTest, PushesGlobalCommand)
{
    rtsyn::api::Api api(config());

    ASSERT_TRUE(api.valid());
    ASSERT_TRUE(api.push_global_command(rtsyn::api::GlobalCommand::pause));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.seq, 1U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_GLOBAL_COMMAND);
    EXPECT_EQ(message.data.global_command.command, 2U);
}

TEST_F(ApiTest, PushesRequestCommands)
{
    rtsyn::api::Api api(config());

    ASSERT_TRUE(api.request_port_values(7, true, 0x12));
    ASSERT_TRUE(api.request_variables(8, false, 0x34));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.seq, 1U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VALUES);
    EXPECT_EQ(message.data.plugin_request_ports.plugin_id, 7U);
    EXPECT_TRUE(message.data.plugin_request_ports.send);
    EXPECT_EQ(message.data.plugin_request_ports.portsyn_mask, 0x12U);

    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.seq, 2U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_PORT_VARIABLES);
    EXPECT_EQ(message.data.plugin_request_variables.plugin_id, 8U);
    EXPECT_FALSE(message.data.plugin_request_variables.send);
    EXPECT_EQ(message.data.plugin_request_variables.variable_mask, 0x34U);
}

TEST_F(ApiTest, PushesSetParamCommand)
{
    rtsyn::api::Api api(config());
    rtsyn_spsc_command_param_value_t value = {};
    value.i64 = 19;

    ASSERT_TRUE(api.set_param(8, 3, RTSYN_ABI_VALUE_I64, value));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.seq, 1U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_PARAM);
    EXPECT_EQ(message.data.set_param.node_id, 8U);
    EXPECT_EQ(message.data.set_param.param_id, 3U);
    EXPECT_EQ(message.data.set_param.value_type, RTSYN_ABI_VALUE_I64);
    EXPECT_EQ(message.data.set_param.value.i64, 19);
}

TEST_F(ApiTest, PushesRuntimePeriodCommand)
{
    rtsyn::api::Api api(config());

    ASSERT_TRUE(api.set_runtime_period(500000));
    EXPECT_FALSE(api.set_runtime_period(0));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.seq, 1U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_PERIOD);
    EXPECT_EQ(message.data.set_runtime_period.period_ns, 500000U);
    EXPECT_FALSE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
}

TEST_F(ApiTest, PushesLoadAndAddNodeCommands)
{
    rtsyn::api::Api api(config());

    ASSERT_TRUE(api.load_plugin("/tmp/plugin.so"));
    ASSERT_TRUE(api.add_plugin("plugin-module"));
    ASSERT_TRUE(api.load_device("/tmp/device.so"));
    ASSERT_TRUE(api.add_device("device-module"));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE);
    EXPECT_EQ(message.data.load_node.node_type, RTSYN_ABI_NODE_PLUGIN);
    EXPECT_STREQ(message.data.load_node.module_path, "/tmp/plugin.so");

    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE);
    EXPECT_EQ(message.data.add_node.node_type, RTSYN_ABI_NODE_PLUGIN);
    EXPECT_STREQ(message.data.add_node.node_name, "plugin-module");

    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE);
    EXPECT_EQ(message.data.load_node.node_type, RTSYN_ABI_NODE_DEVICE);
    EXPECT_STREQ(message.data.load_node.module_path, "/tmp/device.so");

    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE);
    EXPECT_EQ(message.data.add_node.node_type, RTSYN_ABI_NODE_DEVICE);
    EXPECT_STREQ(message.data.add_node.node_name, "device-module");
}

TEST_F(ApiTest, PushesAddAndRemoveConnectionCommands)
{
    rtsyn::api::Api api(config());

    ASSERT_TRUE(api.add_connection(5, 1, 2, 3, 4));
    ASSERT_TRUE(api.remove_connection(5));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.seq, 1U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_CONNECTION);
    EXPECT_EQ(message.data.add_connection.connection_id, 5U);
    EXPECT_EQ(message.data.add_connection.source_node_id, 1U);
    EXPECT_EQ(message.data.add_connection.source_port_id, 2U);
    EXPECT_EQ(message.data.add_connection.destination_node_id, 3U);
    EXPECT_EQ(message.data.add_connection.destination_port_id, 4U);

    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.seq, 2U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REMOVE_CONNECTION);
    EXPECT_EQ(message.data.remove_connection.connection_id, 5U);
}

TEST_F(ApiTest, HttpRoutesPushAddAndRemoveConnectionCommands)
{
    auto config = this->config();
    config.bind_host = "127.0.0.1";
    config.port = 18187;
    rtsyn::api::Api api(config);
    ASSERT_TRUE(api.start());

    httplib::Client client("127.0.0.1", config.port);
    for (int i = 0; i < 50; ++i)
    {
        if (client.Get(RTSYN_API_ENDPOINT_HEALTH))
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    auto add = client.Post(RTSYN_API_ENDPOINT_COMMAND_ADD_CONNECTION,
                           "{\"connection_id\":9,\"source_node_id\":1,\"source_port_id\":2,"
                           "\"destination_node_id\":3,\"destination_port_id\":4}",
                           "application/json");
    auto remove = client.Post(RTSYN_API_ENDPOINT_COMMAND_REMOVE_CONNECTION, "{\"connection_id\":9}",
                              "application/json");
    api.stop();

    ASSERT_TRUE(add);
    ASSERT_TRUE(remove);
    EXPECT_EQ(add->status, RTSYN_API_HTTP_STATUS_ACCEPTED);
    EXPECT_EQ(remove->status, RTSYN_API_HTTP_STATUS_ACCEPTED);

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_CONNECTION);
    EXPECT_EQ(message.data.add_connection.connection_id, 9U);
    EXPECT_EQ(message.data.add_connection.source_node_id, 1U);
    EXPECT_EQ(message.data.add_connection.source_port_id, 2U);
    EXPECT_EQ(message.data.add_connection.destination_node_id, 3U);
    EXPECT_EQ(message.data.add_connection.destination_port_id, 4U);

    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REMOVE_CONNECTION);
    EXPECT_EQ(message.data.remove_connection.connection_id, 9U);
}

TEST_F(ApiTest, CapabilitiesRouteDoesNotPushCommands)
{
    auto config = this->config();
    config.bind_host = "127.0.0.1";
    config.port = 18188;
    rtsyn::api::Api api(config);
    ASSERT_TRUE(api.start());

    httplib::Client client("127.0.0.1", config.port);
    for (int i = 0; i < 50; ++i)
    {
        if (client.Get(RTSYN_API_ENDPOINT_HEALTH))
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    auto capabilities = client.Get(RTSYN_API_ENDPOINT_CAPABILITIES);
    api.stop();

    ASSERT_TRUE(capabilities);
    EXPECT_EQ(capabilities->status, 200);
    EXPECT_NE(capabilities->body.find("\"plugin\":true"), std::string::npos);

    rtsyn_spsc_command_message_t message = {};
    EXPECT_FALSE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
}

TEST_F(ApiTest, DrainsTelemetryIntoRecentEvents)
{
    rtsyn::api::Api api(config());

    rtsyn_spsc_telemetry_message_t event = {};
    event.seq = 9;
    event.timestamp_ns = 100;
    event.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_NODE_STATUS;
    event.data.node_status.cycle_id = 4;
    event.data.node_status.node_id = 11;
    event.data.node_status.source = RTSYN_SPSC_TELEMETRY_SOURCE_PLUGIN;
    event.data.node_status.status = RTSYN_SPSC_TELEMETRY_NODE_STATUS_OK;
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_push(telemetry_.get(), &event));

    const auto result = api.drain_telemetry();

    EXPECT_EQ(result.event_count, 1U);
    EXPECT_EQ(result.value_count, 0U);
    EXPECT_NE(api.recent_events_json().find("\"node_id\":11"), std::string::npos);
    EXPECT_NE(api.status_json().find("\"events_consumed\":1"), std::string::npos);
}

TEST_F(ApiTest, LatestMeasurementJsonReportsUnavailableBeforeMeasurement)
{
    rtsyn::api::Api api(config());

    EXPECT_EQ(api.latest_measurement_json(), "{\"available\":false}");
}

TEST_F(ApiTest, DrainsTelemetryIntoLatestMeasurement)
{
    rtsyn::api::Api api(config());

    rtsyn_spsc_telemetry_message_t event = {};
    event.type = RTSYN_SPSC_TELEMETRY_MESSAGE_TYPE_MEASUREMENT;
    event.data.measurement.cycle_id = 7;
    event.data.measurement.period_ns = 1000;
    event.data.measurement.actual_period_ns = 1200;
    event.data.measurement.latency_ns = 200;
    event.data.measurement.missed_cycle = 1;
    event.data.measurement.devices_read_ns = 10;
    event.data.measurement.plugins_time_ns = 20;
    event.data.measurement.devices_write_ns = 30;
    ASSERT_TRUE(rtsyn_spsc_telemetry_try_push(telemetry_.get(), &event));

    const auto result = api.drain_telemetry();

    EXPECT_EQ(result.event_count, 1U);
    const auto json = api.latest_measurement_json();
    EXPECT_NE(json.find("\"available\":true"), std::string::npos);
    EXPECT_NE(json.find("\"type\":\"measurement\""), std::string::npos);
    EXPECT_NE(json.find("\"cycle_id\":7"), std::string::npos);
    EXPECT_NE(json.find("\"latency_ns\":200"), std::string::npos);
}
