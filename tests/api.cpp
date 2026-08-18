#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <fstream>
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

static std::string
runtime_state_path_for_test_port(int port)
{
    return "/tmp/rtsyn-api-runtime-state-" + std::to_string(port) + ".bin";
}

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

TEST_F(ApiTest, PushesRuntimePriorityCommand)
{
    rtsyn::api::Api api(config());

    ASSERT_TRUE(api.set_runtime_priority(42));
    EXPECT_FALSE(api.set_runtime_priority(-1));
    EXPECT_FALSE(api.set_runtime_priority(100));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.seq, 1U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_PRIORITY);
    EXPECT_EQ(message.data.set_runtime_priority.priority, 42);
    EXPECT_FALSE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
}

TEST_F(ApiTest, PushesRuntimeDeadlineToleranceCommand)
{
    rtsyn::api::Api api(config());

    ASSERT_TRUE(api.set_runtime_deadline_tolerance(25000));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.seq, 1U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_DEADLINE_TOLERANCE);
    EXPECT_EQ(message.data.set_runtime_deadline_tolerance.tolerance_ns, 25000U);
    EXPECT_FALSE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
}

TEST_F(ApiTest, ConfiguresCsvValuesFile)
{
    const std::string path = "/tmp/rtsyn-api-test-configured-values.csv";
    std::remove(path.c_str());
    rtsyn::api::Api api(config());

    ASSERT_TRUE(api.configure_csv_values_file(path, {"left", "right"}, {1, 2}));
    EXPECT_FALSE(api.configure_csv_values_file(path, {"left"}, {1, 2}));

    std::ifstream stream(path);
    std::string header;
    ASSERT_TRUE(std::getline(stream, header));
    EXPECT_EQ(header, "cycle_id,timestamp_ns,left,right");

    std::remove(path.c_str());
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

TEST_F(ApiTest, RuntimeNodesRouteReportsApiMirroredNodes)
{
    auto config = this->config();
    config.bind_host = "127.0.0.1";
    config.port = 18189;
    std::remove(runtime_state_path_for_test_port(config.port).c_str());
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

    std::thread engine([this] {
        for (int handled = 0; handled < 4;)
        {
            rtsyn_spsc_command_message_t command = {};
            if (!rtsyn_spsc_command_try_pop(commands_.get(), &command))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            rtsyn_spsc_result_message_t result = {};
            result.seq = command.seq;
            result.command_type = command.type;
            result.status = RTSYN_SPSC_RESULT_STATUS_OK;
            result.node_id =
                command.type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE
                    || command.type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES
                    ? 7
                    : UINT32_MAX;
            result.status_code =
                command.type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES ? 2 : 0;
            result.node.node_type = RTSYN_ABI_NODE_PLUGIN;
            result.node.port_count = 1;
            snprintf(result.node.name, sizeof(result.node.name), "%s", "adder");
            result.node.ports[0].id = 0;
            result.node.ports[0].direction = RTSYN_ABI_PORT_DIRECTION_OUT;
            result.node.ports[0].value_type = RTSYN_ABI_VALUE_F64;
            snprintf(result.node.ports[0].name, sizeof(result.node.ports[0].name), "%s", "sum");
            ASSERT_TRUE(rtsyn_spsc_result_try_push(results_.get(), &result));
            if (command.type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES)
            {
                rtsyn_spsc_result_message_t done = {};
                done.seq = command.seq;
                done.command_type = command.type;
                done.status = RTSYN_SPSC_RESULT_STATUS_OK;
                done.status_code = UINT32_MAX;
                done.node_id = UINT32_MAX;
                ASSERT_TRUE(rtsyn_spsc_result_try_push(results_.get(), &done));
            }
            handled++;
        }
    });

    auto load = client.Post(RTSYN_API_ENDPOINT_COMMAND_LOAD_PLUGIN,
                            "{\"module_path\":\"/tmp/libadder.so\"}", "application/json");
    auto add = client.Post(RTSYN_API_ENDPOINT_COMMAND_ADD_PLUGIN, "{\"node_name\":\"adder\"}",
                           "application/json");
    ASSERT_TRUE(load);
    ASSERT_TRUE(add);
    EXPECT_EQ(load->status, RTSYN_API_HTTP_STATUS_ACCEPTED);
    EXPECT_EQ(add->status, RTSYN_API_HTTP_STATUS_ACCEPTED);

    auto start = client.Post(RTSYN_API_ENDPOINT_COMMAND_PLUGIN,
                             "{\"plugin_id\":7,\"plugin_state\":1}", "application/json");
    auto nodes = client.Get(RTSYN_API_ENDPOINT_RUNTIME_NODES);
    engine.join();
    api.stop();

    ASSERT_TRUE(start);
    ASSERT_TRUE(nodes);
    EXPECT_EQ(start->status, RTSYN_API_HTTP_STATUS_ACCEPTED);
    EXPECT_EQ(nodes->status, 200);
    EXPECT_NE(nodes->body.find("\"node_id\":7"), std::string::npos);
    EXPECT_NE(nodes->body.find("\"name\":\"adder\""), std::string::npos);
    EXPECT_NE(nodes->body.find("\"runtime_state\":\"process\""), std::string::npos);
    EXPECT_NE(nodes->body.find("\"loaded_descriptors\""), std::string::npos);
}

TEST_F(ApiTest, RuntimeNodesRouteDrainsLateAddResults)
{
    auto config = this->config();
    config.bind_host = "127.0.0.1";
    config.port = 18190;
    std::remove(runtime_state_path_for_test_port(config.port).c_str());
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

    auto add = client.Post(RTSYN_API_ENDPOINT_COMMAND_ADD_PLUGIN, "{\"node_name\":\"adder\"}",
                           "application/json");
    ASSERT_TRUE(add);
    EXPECT_EQ(add->status, RTSYN_API_HTTP_STATUS_ACCEPTED);
    EXPECT_NE(add->body.find("\"pending\":true"), std::string::npos);

    rtsyn_spsc_command_message_t command = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &command));
    ASSERT_EQ(command.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE);

    rtsyn_spsc_result_message_t result = {};
    result.seq = command.seq;
    result.command_type = command.type;
    result.status = RTSYN_SPSC_RESULT_STATUS_OK;
    result.node_id = 9;
    result.node.node_type = RTSYN_ABI_NODE_PLUGIN;
    snprintf(result.node.name, sizeof(result.node.name), "%s", "adder");
    ASSERT_TRUE(rtsyn_spsc_result_try_push(results_.get(), &result));

    std::thread snapshot_engine([this] {
        while (true)
        {
            rtsyn_spsc_command_message_t command = {};
            if (!rtsyn_spsc_command_try_pop(commands_.get(), &command))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            ASSERT_EQ(command.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES);

            rtsyn_spsc_result_message_t snapshot = {};
            snapshot.seq = command.seq;
            snapshot.command_type = command.type;
            snapshot.status = RTSYN_SPSC_RESULT_STATUS_OK;
            snapshot.status_code = 4;
            snapshot.node_id = 9;
            snapshot.node.node_type = RTSYN_ABI_NODE_PLUGIN;
            snprintf(snapshot.node.name, sizeof(snapshot.node.name), "%s", "adder");
            ASSERT_TRUE(rtsyn_spsc_result_try_push(results_.get(), &snapshot));

            rtsyn_spsc_result_message_t connection = {};
            connection.seq = command.seq;
            connection.command_type = command.type;
            connection.status = RTSYN_SPSC_RESULT_STATUS_OK;
            connection.node_id = UINT32_MAX;
            connection.connection.connection_id = 33;
            connection.connection.source_node_id = 2;
            connection.connection.source_port_id = 1;
            connection.connection.destination_node_id = 9;
            connection.connection.destination_port_id = 0;
            ASSERT_TRUE(rtsyn_spsc_result_try_push(results_.get(), &connection));

            rtsyn_spsc_result_message_t done = {};
            done.seq = command.seq;
            done.command_type = command.type;
            done.status = RTSYN_SPSC_RESULT_STATUS_OK;
            done.status_code = UINT32_MAX;
            done.node_id = UINT32_MAX;
            ASSERT_TRUE(rtsyn_spsc_result_try_push(results_.get(), &done));
            break;
        }
    });

    auto nodes = client.Get(RTSYN_API_ENDPOINT_RUNTIME_NODES);
    snapshot_engine.join();
    api.stop();

    ASSERT_TRUE(nodes);
    EXPECT_EQ(nodes->status, 200);
    EXPECT_NE(nodes->body.find("\"node_id\":9"), std::string::npos);
    EXPECT_NE(nodes->body.find("\"name\":\"adder\""), std::string::npos);
    EXPECT_NE(nodes->body.find("\"connections\""), std::string::npos);
    EXPECT_NE(nodes->body.find("\"connection_id\":33"), std::string::npos);
    EXPECT_EQ(nodes->body.find("\"connection_id\":0"), std::string::npos);
    EXPECT_NE(nodes->body.find("\"source_node_id\":2"), std::string::npos);
    EXPECT_NE(nodes->body.find("\"destination_node_id\":9"), std::string::npos);
}

TEST_F(ApiTest, RuntimeNodesRouteRestoresPersistedMirrorAfterApiRestart)
{
    auto config = this->config();
    config.bind_host = "127.0.0.1";
    config.port = 18191;
    std::remove(runtime_state_path_for_test_port(config.port).c_str());

    {
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

        std::thread engine([this] {
            for (int handled = 0; handled < 2;)
            {
                rtsyn_spsc_command_message_t command = {};
                if (!rtsyn_spsc_command_try_pop(commands_.get(), &command))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                rtsyn_spsc_result_message_t result = {};
                result.seq = command.seq;
                result.command_type = command.type;
                result.status = RTSYN_SPSC_RESULT_STATUS_OK;
                result.node_id =
                    command.type == RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE ? 11 : UINT32_MAX;
                result.node.node_type = RTSYN_ABI_NODE_PLUGIN;
                snprintf(result.node.name, sizeof(result.node.name), "%s", "adder");
                ASSERT_TRUE(rtsyn_spsc_result_try_push(results_.get(), &result));
                handled++;
            }
        });

        auto load = client.Post(RTSYN_API_ENDPOINT_COMMAND_LOAD_PLUGIN,
                                "{\"module_path\":\"/tmp/libadder.so\"}", "application/json");
        auto add = client.Post(RTSYN_API_ENDPOINT_COMMAND_ADD_PLUGIN, "{\"node_name\":\"adder\"}",
                               "application/json");
        ASSERT_TRUE(load);
        ASSERT_TRUE(add);
        EXPECT_EQ(load->status, RTSYN_API_HTTP_STATUS_ACCEPTED);
        EXPECT_EQ(add->status, RTSYN_API_HTTP_STATUS_ACCEPTED);
        engine.join();
        api.stop();
    }

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
    std::thread snapshot_engine([this] {
        while (true)
        {
            rtsyn_spsc_command_message_t command = {};
            if (!rtsyn_spsc_command_try_pop(commands_.get(), &command))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            ASSERT_EQ(command.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REQUEST_RUNTIME_NODES);

            rtsyn_spsc_result_message_t snapshot = {};
            snapshot.seq = command.seq;
            snapshot.command_type = command.type;
            snapshot.status = RTSYN_SPSC_RESULT_STATUS_OK;
            snapshot.status_code = 4;
            snapshot.node_id = 11;
            snapshot.node.node_type = RTSYN_ABI_NODE_PLUGIN;
            snprintf(snapshot.node.name, sizeof(snapshot.node.name), "%s", "adder");
            ASSERT_TRUE(rtsyn_spsc_result_try_push(results_.get(), &snapshot));

            rtsyn_spsc_result_message_t done = {};
            done.seq = command.seq;
            done.command_type = command.type;
            done.status = RTSYN_SPSC_RESULT_STATUS_OK;
            done.status_code = UINT32_MAX;
            done.node_id = UINT32_MAX;
            ASSERT_TRUE(rtsyn_spsc_result_try_push(results_.get(), &done));
            break;
        }
    });
    auto nodes = client.Get(RTSYN_API_ENDPOINT_RUNTIME_NODES);
    snapshot_engine.join();
    api.stop();

    ASSERT_TRUE(nodes);
    EXPECT_EQ(nodes->status, 200);
    EXPECT_NE(nodes->body.find("\"node_id\":11"), std::string::npos);
    EXPECT_NE(nodes->body.find("\"name\":\"adder\""), std::string::npos);

    std::remove(runtime_state_path_for_test_port(config.port).c_str());
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
    EXPECT_NE(capabilities->body.find("\"csv_values\":true"), std::string::npos);
    EXPECT_NE(capabilities->body.find("\"csv_measurements\":true"), std::string::npos);
    EXPECT_NE(capabilities->body.find("\"runtime_priority\":true"), std::string::npos);
    EXPECT_NE(capabilities->body.find("\"runtime_deadline_tolerance\":true"), std::string::npos);

    rtsyn_spsc_command_message_t message = {};
    EXPECT_FALSE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
}

TEST_F(ApiTest, HttpRoutePushesRuntimePriorityCommand)
{
    auto config = this->config();
    config.bind_host = "127.0.0.1";
    config.port = 18193;
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

    auto result = client.Post(RTSYN_API_ENDPOINT_COMMAND_RUNTIME_PRIORITY, "{\"priority\":42}",
                              "application/json");
    auto invalid = client.Post(RTSYN_API_ENDPOINT_COMMAND_RUNTIME_PRIORITY, "{\"priority\":100}",
                               "application/json");
    api.stop();

    ASSERT_TRUE(result);
    ASSERT_TRUE(invalid);
    EXPECT_EQ(result->status, RTSYN_API_HTTP_STATUS_ACCEPTED);
    EXPECT_EQ(invalid->status, RTSYN_API_HTTP_STATUS_BAD_REQUEST);

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_PRIORITY);
    EXPECT_EQ(message.data.set_runtime_priority.priority, 42);
    EXPECT_FALSE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
}

TEST_F(ApiTest, HttpRoutePushesRuntimeDeadlineToleranceCommand)
{
    auto config = this->config();
    config.bind_host = "127.0.0.1";
    config.port = 18198;
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

    auto result = client.Post(RTSYN_API_ENDPOINT_COMMAND_RUNTIME_DEADLINE_TOLERANCE,
                              "{\"tolerance_ns\":25000}", "application/json");
    auto invalid = client.Post(RTSYN_API_ENDPOINT_COMMAND_RUNTIME_DEADLINE_TOLERANCE, "{}",
                               "application/json");
    api.stop();

    ASSERT_TRUE(result);
    ASSERT_TRUE(invalid);
    EXPECT_EQ(result->status, RTSYN_API_HTTP_STATUS_ACCEPTED);
    EXPECT_EQ(invalid->status, RTSYN_API_HTTP_STATUS_BAD_REQUEST);

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_DEADLINE_TOLERANCE);
    EXPECT_EQ(message.data.set_runtime_deadline_tolerance.tolerance_ns, 25000U);
    EXPECT_FALSE(rtsyn_spsc_command_try_pop(commands_.get(), &message));
}

TEST_F(ApiTest, ConfiguresCsvValuesFileThroughHttp)
{
    const std::string path = "/tmp/rtsyn-api-test-http-values.csv";
    std::remove(path.c_str());

    auto config = this->config();
    config.bind_host = "127.0.0.1";
    config.port = 18189;
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

    auto result = client.Post(RTSYN_API_ENDPOINT_TELEMETRY_CSV_FILE,
                              "{\"path\":\"" + path
                                  + "\",\"names\":[\"left\",\"right\"],"
                                    "\"values\":[{\"node_id\":3,\"value_id\":1,\"kind\":\"port\"},"
                                    "{\"node_id\":3,\"value_id\":2,\"kind\":\"port\"}]}",
                              "application/json");
    api.stop();

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 202);

    std::ifstream stream(path);
    std::string header;
    ASSERT_TRUE(std::getline(stream, header));
    EXPECT_EQ(header, "cycle_id,timestamp_ns,left,right");

    std::remove(path.c_str());
}

TEST_F(ApiTest, ConfiguresCsvTelemetryFileWithMeasurementsThroughHttp)
{
    const std::string path = "/tmp/rtsyn-api-test-http-measurements.csv";
    std::remove(path.c_str());

    auto config = this->config();
    config.bind_host = "127.0.0.1";
    config.port = 18190;
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

    auto result = client.Post(
        RTSYN_API_ENDPOINT_TELEMETRY_CSV_FILE,
        "{\"path\":\"" + path
            + "\",\"names\":[\"left\",\"latency\"],"
              "\"values\":[{\"node_id\":3,\"value_id\":1,\"kind\":\"port\"}],"
              "\"measurement_fields\":[\"latency_ns\"]}",
        "application/json");
    api.stop();

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 202);

    std::ifstream stream(path);
    std::string header;
    ASSERT_TRUE(std::getline(stream, header));
    EXPECT_EQ(header, "cycle_id,timestamp_ns,left,latency");

    std::remove(path.c_str());
}

TEST_F(ApiTest, ConfiguresCsvTelemetryFileWithOnlyMeasurementsThroughHttp)
{
    const std::string path = "/tmp/rtsyn-api-test-http-measurements-only.csv";
    std::remove(path.c_str());

    auto config = this->config();
    config.bind_host = "127.0.0.1";
    config.port = 18192;
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

    auto result = client.Post(
        RTSYN_API_ENDPOINT_TELEMETRY_CSV_FILE,
        "{\"path\":\"" + path
            + "\",\"names\":[\"actual_period_ns\"],\"values\":[],"
              "\"measurement_fields\":[\"actual_period_ns\"]}",
        "application/json");
    api.stop();

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 202);

    std::ifstream stream(path);
    std::string header;
    ASSERT_TRUE(std::getline(stream, header));
    EXPECT_EQ(header, "cycle_id,timestamp_ns,actual_period_ns");

    std::remove(path.c_str());
}

TEST_F(ApiTest, StopsCsvTelemetryFileThroughHttp)
{
    auto config = this->config();
    config.bind_host = "127.0.0.1";
    config.port = 18191;
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

    auto result = client.Post(RTSYN_API_ENDPOINT_TELEMETRY_CSV_FILE, "{\"enabled\":false}",
                              "application/json");
    api.stop();

    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 202);
    EXPECT_NE(result->body.find("\"enabled\":false"), std::string::npos);
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
    event.data.measurement.wake_lateness_ns = 150;
    event.data.measurement.skipped_cycle_count = 1;
    event.data.measurement.missed_cycle = 1;
    event.data.measurement.deadline_missed = 1;
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
    EXPECT_NE(json.find("\"wake_lateness_ns\":150"), std::string::npos);
    EXPECT_NE(json.find("\"skipped_cycle_count\":1"), std::string::npos);
    EXPECT_NE(json.find("\"deadline_missed\":true"), std::string::npos);
}
