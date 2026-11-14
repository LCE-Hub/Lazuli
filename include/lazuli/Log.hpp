#pragma once
#include <functional>
#include <string_view>
namespace lazuli::log {
enum class Level {
  Debug,
  Info,
  Warn,
  Error,
};

using Sink = std::function<void(Level, std::string_view)>;
void set_sink(Sink sink);
void debug(std::string_view message);
void info(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);
} // namespace lazuli::log
