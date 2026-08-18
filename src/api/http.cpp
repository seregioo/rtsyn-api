#include "rtsyn/internal/api/http.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace rtsyn::api::internal {

namespace {

std::string
lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

std::optional<std::string>
parse_string_field(const std::string &body, const std::string &name)
{
    const std::regex field("\"" + name + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(body, match, field))
    {
        return std::nullopt;
    }
    return match[1].str();
}

std::optional<std::uint64_t>
parse_u64_field(const std::string &body, const std::string &name)
{
    const std::regex field("\"" + name + "\"\\s*:\\s*(0x[0-9a-fA-F]+|[0-9]+)");
    std::smatch match;
    if (!std::regex_search(body, match, field))
    {
        return std::nullopt;
    }

    try
    {
        const std::string token = match[1].str();
        return std::stoull(token, nullptr, token.rfind("0x", 0) == 0 ? 16 : 10);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<std::int64_t>
parse_i64_field(const std::string &body, const std::string &name)
{
    const std::regex field("\"" + name + "\"\\s*:\\s*(-?[0-9]+)");
    std::smatch match;
    if (!std::regex_search(body, match, field))
    {
        return std::nullopt;
    }

    try
    {
        return std::stoll(match[1].str());
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<double>
parse_f64_field(const std::string &body, const std::string &name)
{
    const std::regex field("\"" + name
                           + "\"\\s*:\\s*(-?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)");
    std::smatch match;
    if (!std::regex_search(body, match, field))
    {
        return std::nullopt;
    }

    try
    {
        return std::stod(match[1].str());
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<bool>
parse_bool_field(const std::string &body, const std::string &name)
{
    const std::regex field("\"" + name + "\"\\s*:\\s*(true|false|1|0)");
    std::smatch match;
    if (!std::regex_search(body, match, field))
    {
        return std::nullopt;
    }

    const std::string token = lower(match[1].str());
    return token == "true" || token == "1";
}

std::optional<GlobalCommand>
parse_global_command(const std::string &body)
{
    if (auto number = parse_u64_field(body, "command"))
    {
        switch (*number)
        {
            case 0:
                return GlobalCommand::none;
            case 1:
                return GlobalCommand::stop;
            case 2:
                return GlobalCommand::pause;
            case 3:
                return GlobalCommand::resume;
            default:
                return std::nullopt;
        }
    }

    const auto command = parse_string_field(body, "command");
    if (!command)
    {
        return std::nullopt;
    }

    const std::string value = lower(*command);
    if (value == "none")
    {
        return GlobalCommand::none;
    }
    if (value == "stop")
    {
        return GlobalCommand::stop;
    }
    if (value == "pause")
    {
        return GlobalCommand::pause;
    }
    if (value == "resume")
    {
        return GlobalCommand::resume;
    }
    return std::nullopt;
}

std::string
http_error_json(const std::string &message)
{
    std::ostringstream out;
    out << "{\"error\":\"" << message << "\"}";
    return out.str();
}

} // namespace rtsyn::api::internal
