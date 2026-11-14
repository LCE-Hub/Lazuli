#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace lazuli {
std::optional<std::vector<std::uint8_t>> base64_decode(const std::string &text);
std::optional<std::string> data_url_base64(const std::string &source);
} // namespace lazuli
