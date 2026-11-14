#pragma once
#include "lazuli/Json.hpp"
#include <memory>
#include <string>
#include <string_view>
namespace lazuli {
class Screen {
public:
  virtual ~Screen() = default;
  virtual std::string_view id() const = 0;
  virtual Json data(const void *services) const = 0;
  virtual bool action(std::string_view name, void *services) {
    (void)name;
    (void)services;
    return false;
  }
  virtual void on_enter() {}
  virtual bool on_escape(void *services) {
    (void)services;
    return false;
  }
};
using ScreenPtr = std::unique_ptr<Screen>;
} // namespace lazuli
