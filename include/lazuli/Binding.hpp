#pragma once
#include "lazuli/I18n.hpp"
#include "lazuli/Json.hpp"
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace lazuli {
using Translator = std::function<std::optional<std::string>(std::string_view)>;
class DataContext {
public:
  DataContext(const Json &root, const I18n *i18n, Translator translator = {});
  void push_scope(const Json &scope);
  void pop_scope();
  std::string resolve(std::string_view text) const;
  std::string resolve_path(std::string_view path) const;
  const Json *lookup(std::string_view path) const;
  bool truthy(std::string_view path) const;
  const Json &root() const { return root_; }

private:
  const Json &root_;
  const I18n *i18n_;
  Translator translator_;
  std::vector<const Json *> scopes_;
};
} // namespace lazuli
