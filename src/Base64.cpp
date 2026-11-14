#include "lazuli/Base64.hpp"
namespace lazuli {
std::optional<std::vector<std::uint8_t>> base64_decode(const std::string &text) {
  static auto value_of = [](char character) -> int {
    if (character >= 'A' && character <= 'Z') {
      return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
      return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
      return character - '0' + 52;
    }
    if (character == '+') {
      return 62;
    }
    if (character == '/') {
      return 63;
    }
    return -1;
  };
  std::vector<std::uint8_t> output;
  output.reserve(text.size() / 4 * 3);
  std::uint32_t accumulator = 0;
  int bits = 0;
  for (const char character : text) {
    if (character == '=' || character == '\n' || character == '\r' ||
        character == ' ' || character == '\t') {
      continue;
    }
    const int value = value_of(character);
    if (value < 0) {
      return std::nullopt;
    }
    accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      output.push_back(
          static_cast<std::uint8_t>((accumulator >> bits) & 0xFFU));
    }
  }
  return output;
}

std::optional<std::string> data_url_base64(const std::string &source) {
  static constexpr std::string_view marker = ";base64,";
  if (source.rfind("data:", 0) != 0) {
    return std::nullopt;
  }
  const std::size_t separator = source.find(marker);
  if (separator == std::string::npos) {
    return std::nullopt;
  }
  return source.substr(separator + marker.size());
}
} // namespace lazuli
