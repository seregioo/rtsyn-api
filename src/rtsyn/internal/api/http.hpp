/**
 * @file rtsyn/internal/api/http.hpp
 * @brief RTSyn API HTTP request helpers.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef RTSYN_INTERNAL_API_HTTP_HPP
#define RTSYN_INTERNAL_API_HTTP_HPP

#include <cstdint>
#include <optional>
#include <string>

#include <rtsyn/api.hpp>

namespace rtsyn::api::internal {

std::optional<GlobalCommand> parse_global_command(const std::string &body);
std::optional<std::uint64_t> parse_u64_field(const std::string &body, const std::string &name);
std::optional<std::int64_t> parse_i64_field(const std::string &body, const std::string &name);
std::optional<double> parse_f64_field(const std::string &body, const std::string &name);
std::optional<std::string> parse_string_field(const std::string &body, const std::string &name);
std::optional<bool> parse_bool_field(const std::string &body, const std::string &name);
std::string http_error_json(const std::string &message);

} // namespace rtsyn::api::internal

#endif // RTSYN_INTERNAL_API_HTTP_HPP
