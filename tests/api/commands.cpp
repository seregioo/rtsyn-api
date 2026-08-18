#include <gtest/gtest.h>

#include "rtsyn/internal/api/commands.hpp"

TEST(ApiCommandTest, PushesPluginUpdate)
{
    rtsyn_spsc_command_queue_t queue = {};
    rtsyn_spsc_command_init(&queue);

    ASSERT_TRUE(rtsyn::api::internal::push_plugin_update(&queue, 44, 12, 3));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(&queue, &message));
    EXPECT_EQ(message.seq, 44U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_PLUGIN_UPDATE);
    EXPECT_EQ(message.data.plugin_update.plugin_id, 12U);
    EXPECT_EQ(message.data.plugin_update.plugin_state, 3U);
}

TEST(ApiCommandTest, PushesSetParam)
{
    rtsyn_spsc_command_queue_t queue = {};
    rtsyn_spsc_command_init(&queue);

    rtsyn_spsc_command_param_value_t value = {};
    value.f64 = 14.25;

    ASSERT_TRUE(rtsyn::api::internal::push_set_param(&queue, 45, 13, 2,
                                                     RTSYN_ABI_VALUE_F64, value));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(&queue, &message));
    EXPECT_EQ(message.seq, 45U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_PARAM);
    EXPECT_EQ(message.data.set_param.node_id, 13U);
    EXPECT_EQ(message.data.set_param.param_id, 2U);
    EXPECT_EQ(message.data.set_param.value_type, RTSYN_ABI_VALUE_F64);
    EXPECT_DOUBLE_EQ(message.data.set_param.value.f64, value.f64);
}

TEST(ApiCommandTest, PushesRuntimePeriod)
{
    rtsyn_spsc_command_queue_t queue = {};
    rtsyn_spsc_command_init(&queue);

    ASSERT_TRUE(rtsyn::api::internal::push_runtime_period(&queue, 50, 500000));
    EXPECT_FALSE(rtsyn::api::internal::push_runtime_period(&queue, 51, 0));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(&queue, &message));
    EXPECT_EQ(message.seq, 50U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_SET_RUNTIME_PERIOD);
    EXPECT_EQ(message.data.set_runtime_period.period_ns, 500000U);
    EXPECT_FALSE(rtsyn_spsc_command_try_pop(&queue, &message));
}

TEST(ApiCommandTest, PushesLoadAndAddNode)
{
    rtsyn_spsc_command_queue_t queue = {};
    rtsyn_spsc_command_init(&queue);

    ASSERT_TRUE(rtsyn::api::internal::push_load_node(&queue, 46, RTSYN_ABI_NODE_PLUGIN,
                                                     "/tmp/plugin.so"));
    ASSERT_TRUE(rtsyn::api::internal::push_add_node(&queue, 47, RTSYN_ABI_NODE_DEVICE,
                                                    "device-module"));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(&queue, &message));
    EXPECT_EQ(message.seq, 46U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_LOAD_NODE);
    EXPECT_EQ(message.data.load_node.node_type, RTSYN_ABI_NODE_PLUGIN);
    EXPECT_STREQ(message.data.load_node.module_path, "/tmp/plugin.so");

    ASSERT_TRUE(rtsyn_spsc_command_try_pop(&queue, &message));
    EXPECT_EQ(message.seq, 47U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_NODE);
    EXPECT_EQ(message.data.add_node.node_type, RTSYN_ABI_NODE_DEVICE);
    EXPECT_STREQ(message.data.add_node.node_name, "device-module");
}

TEST(ApiCommandTest, PushesAddAndRemoveConnection)
{
    rtsyn_spsc_command_queue_t queue = {};
    rtsyn_spsc_command_init(&queue);

    ASSERT_TRUE(rtsyn::api::internal::push_add_connection(&queue, 48, 10, 1, 2, 3, 4));
    ASSERT_TRUE(rtsyn::api::internal::push_remove_connection(&queue, 49, 10));

    rtsyn_spsc_command_message_t message = {};
    ASSERT_TRUE(rtsyn_spsc_command_try_pop(&queue, &message));
    EXPECT_EQ(message.seq, 48U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_ADD_CONNECTION);
    EXPECT_EQ(message.data.add_connection.connection_id, 10U);
    EXPECT_EQ(message.data.add_connection.source_node_id, 1U);
    EXPECT_EQ(message.data.add_connection.source_port_id, 2U);
    EXPECT_EQ(message.data.add_connection.destination_node_id, 3U);
    EXPECT_EQ(message.data.add_connection.destination_port_id, 4U);

    ASSERT_TRUE(rtsyn_spsc_command_try_pop(&queue, &message));
    EXPECT_EQ(message.seq, 49U);
    EXPECT_EQ(message.type, RTSYN_SPSC_COMMAND_MESSAGE_TYPE_REMOVE_CONNECTION);
    EXPECT_EQ(message.data.remove_connection.connection_id, 10U);
}

TEST(ApiCommandTest, RejectsNullQueue)
{
    EXPECT_FALSE(rtsyn::api::internal::push_global_command(
        nullptr, 1, rtsyn::api::GlobalCommand::stop));
}
