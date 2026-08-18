#include <gtest/gtest.h>

#include "rtsyn/internal/api/http.hpp"

TEST(ApiHttpTest, ParsesGlobalCommandStringsAndNumbers)
{
    auto stop = rtsyn::api::internal::parse_global_command("{\"command\":\"stop\"}");
    auto resume = rtsyn::api::internal::parse_global_command("{\"command\":3}");

    ASSERT_TRUE(stop.has_value());
    ASSERT_TRUE(resume.has_value());
    EXPECT_EQ(*stop, rtsyn::api::GlobalCommand::stop);
    EXPECT_EQ(*resume, rtsyn::api::GlobalCommand::resume);
    EXPECT_FALSE(rtsyn::api::internal::parse_global_command("{\"command\":\"bad\"}"));
}

TEST(ApiHttpTest, ParsesFields)
{
    const std::string body = "{\"plugin_id\":7,\"send\":true,\"portsyn_mask\":0x40}";

    EXPECT_EQ(rtsyn::api::internal::parse_u64_field(body, "plugin_id"), 7U);
    EXPECT_EQ(rtsyn::api::internal::parse_bool_field(body, "send"), true);
    EXPECT_EQ(rtsyn::api::internal::parse_u64_field(body, "portsyn_mask"), 0x40U);
}

TEST(ApiHttpTest, ParsesParamValueFields)
{
    const std::string body = "{\"i64\":-12,\"f64\":1.25e2,\"string\":\"value\"}";

    EXPECT_EQ(rtsyn::api::internal::parse_i64_field(body, "i64"), -12);
    EXPECT_EQ(rtsyn::api::internal::parse_f64_field(body, "f64"), 125.0);
    EXPECT_EQ(rtsyn::api::internal::parse_string_field(body, "string"), "value");
    EXPECT_FALSE(rtsyn::api::internal::parse_i64_field(body, "missing"));
    EXPECT_FALSE(rtsyn::api::internal::parse_f64_field(body, "missing"));
    EXPECT_FALSE(rtsyn::api::internal::parse_string_field(body, "missing"));
}

TEST(ApiHttpTest, ParsesArrayFields)
{
    const std::string body = "{\"names\":[\"left\",\"right\"],\"value_ids\":[4,0x10]}";

    const auto names = rtsyn::api::internal::parse_string_array_field(body, "names");
    const auto value_ids = rtsyn::api::internal::parse_u64_array_field(body, "value_ids");

    ASSERT_TRUE(names);
    ASSERT_TRUE(value_ids);
    ASSERT_EQ(names->size(), 2U);
    ASSERT_EQ(value_ids->size(), 2U);
    EXPECT_EQ((*names)[0], "left");
    EXPECT_EQ((*names)[1], "right");
    EXPECT_EQ((*value_ids)[0], 4U);
    EXPECT_EQ((*value_ids)[1], 16U);
    EXPECT_FALSE(rtsyn::api::internal::parse_string_array_field(body, "missing"));
    EXPECT_FALSE(rtsyn::api::internal::parse_u64_array_field(body, "missing"));
    EXPECT_FALSE(rtsyn::api::internal::parse_string_array_field("{\"names\":[\"left\",bad]}",
                                                               "names"));
    EXPECT_FALSE(rtsyn::api::internal::parse_u64_array_field("{\"value_ids\":[4,bad]}",
                                                            "value_ids"));
}
