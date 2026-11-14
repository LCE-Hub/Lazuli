#pragma once
#include "lazuli/Json.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
namespace lazuli {
class I18n {
public:
  void load_language(std::string_view language_code,
                     const std::filesystem::path &root);
  void load_from_json(const Json &translations);
  void set_language(std::string_view language_code);
  const std::string &language() const { return language_; }
  std::string translate(std::string_view key) const;
  std::string translate(std::string_view key, std::string_view defaultval) const;
  std::string font_for_language() const;

private:
  std::string language_ = "en";
  std::map<std::string, std::string> strings_;
};
} // namespace lazuli
