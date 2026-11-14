#include "lazuli/Log.hpp"
#include <cstdio>
#include <string>
namespace lazuli::log {
namespace {
Sink &storage() {
  static Sink sink;
  return sink;
}

void emit(Level level, std::string_view message) {
  if (Sink &sink = storage(); sink) {
    sink(level, message);
    return;
  }
  if (level == Level::Warn || level == Level::Error) {
    std::fprintf(stderr, "[lazuli] %s\n", std::string(message).c_str());
  }
}
} // namespace

void set_sink(Sink sink) { storage() = std::move(sink); }
void debug(std::string_view message) { emit(Level::Debug, message); }
void info(std::string_view message) { emit(Level::Info, message); }
void warn(std::string_view message) { emit(Level::Warn, message); }
void error(std::string_view message) { emit(Level::Error, message); }
} // namespace lazuli::log
