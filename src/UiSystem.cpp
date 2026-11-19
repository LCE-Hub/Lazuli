#include "lazuli/UiSystem.hpp"
#include "lazuli/Binding.hpp"
#include "lazuli/Log.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <utility>
namespace lazuli {
namespace {
using ScrollStates = std::vector<std::pair<std::string, std::pair<float, float>>>;
void capture_scroll(const Element *element, const std::string &path,
                    ScrollStates &states) {
  const auto state = element->scroll_state();
  if (state.first != 0.0F || state.second != 0.0F) {
    states.emplace_back(path, state);
  }
  const auto &children = element->children();
  for (std::size_t i = 0; i < children.size(); ++i) {
    capture_scroll(children[i].get(), path + "/" + std::to_string(i), states);
  }
}

void restore_scroll(Element *element, const std::string &path,
                    const ScrollStates &states) {
  for (const auto &entry : states) {
    if (entry.first == path) {
      element->set_scroll_state(entry.second.first, entry.second.second);
      break;
    }
  }
  const auto &children = element->children();
  for (std::size_t i = 0; i < children.size(); ++i) {
    restore_scroll(children[i].get(), path + "/" + std::to_string(i), states);
  }
}

Element *find_blocking_element(Element *element, const Vec2 &point) {
  if (element == nullptr || !element->style().visible ||
      !element->bounds().contains(point)) {
    return nullptr;
  }
  const ComputedStyle &style = element->visual_style();
  if (style.background.has_value() || style.borderImage.has_value() ||
      style.backgroundImage.has_value()) {
    return element;
  }
  for (const ElementPtr &child : element->children()) {
    if (Element *found = find_blocking_element(child.get(), point)) {
      return found;
    }
  }
  return nullptr;
}
ControllerType controller_type(SDL_GamepadType type) {
  switch (type) {
  case SDL_GAMEPAD_TYPE_XBOX360:
    return ControllerType::Xbox360;
  case SDL_GAMEPAD_TYPE_XBOXONE:
    return ControllerType::Durango;
  case SDL_GAMEPAD_TYPE_PS3:
    return ControllerType::PS3;
  case SDL_GAMEPAD_TYPE_PS4:
  case SDL_GAMEPAD_TYPE_PS5:
    return ControllerType::Orbis;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    return ControllerType::Switch;
  default:
    return ControllerType::Unknown;
  }
}

std::string tooltip_set_for(ControllerType type) {
  switch (type) {
  case ControllerType::Orbis:
    return "Orbis";
  case ControllerType::Xbox360:
    return "Xbox360";
  case ControllerType::PS3:
    return "PS3";
  case ControllerType::Switch:
  case ControllerType::Durango:
  case ControllerType::Unknown:
    return "Durango";
  }
  return "Durango";
}

UiSound default_sound_for(const std::string &handler) {
  if (handler.rfind("open:", 0) == 0 || handler.rfind("overlay.toasts.", 0) == 0) {
    return UiSound::None;
  }
  if (handler == "quit" ||
      (handler.size() > 5 && handler.rfind(".back") == handler.size() - 5)) {
    return UiSound::Back;
  }
  return UiSound::Press;
}
} // namespace

UiSystem::UiSystem(SDL_Renderer *renderer, std::filesystem::path ui_docs_root,
                   std::filesystem::path public_root,
                   std::filesystem::path i18n_root)
    : renderer_(renderer, std::move(public_root)),
      registry_(ElementRegistry::instance()),
      ui_docs_root_(std::move(ui_docs_root)), i18n_root_(std::move(i18n_root)) {
}

UiSystem::~UiSystem() = default;
bool UiSystem::initialize() {
  register_builtin_elements();
  if (!document_.load_directory(ui_docs_root_)) {
    for (const std::string &error : document_.errors()) {
      log::error(std::string{"ui: "} + error);
    }
    return false;
  }

  register_handlers();
  i18n_.load_language("en", i18n_root_);
  detect_controller_type();
  return true;
}

void UiSystem::set_language(const std::string &language) {
  i18n_.load_language(language, i18n_root_);
  renderer_.set_default_font(language_font_ ? language_font_(language)
                                            : i18n_.font_for_language());
  dirty_ = true;
}

void UiSystem::set_controller_type(ControllerType type) {
  if (controller_type_ == type) {
    return;
  }
  controller_type_ = type;
  renderer_.set_tooltip_set(tooltip_set_for(type));
}

void UiSystem::detect_controller_type() {
  int count = 0;
  SDL_JoystickID *ids = SDL_GetGamepads(&count);
  ControllerType detected = ControllerType::Unknown;
  if (ids != nullptr) {
    for (int i = 0; i < count; ++i) {
      const ControllerType type =
          controller_type(SDL_GetGamepadTypeForID(ids[i]));
      if (type != ControllerType::Unknown) {
        detected = type;
        break;
      }
    }
    SDL_free(ids);
  }
  set_controller_type(detected);
}

void UiSystem::register_screen(ScreenPtr screen) {
  if (screen == nullptr) {
    return;
  }
  std::string id(screen->id());
  screens_[std::move(id)] = std::move(screen);
}

void UiSystem::refresh_tooltips(const Json &data) {
  std::vector<TooltipHint> hints;
  const auto label_for = [this](const std::string &label) {
    std::string inner = label;
    if (inner.size() >= 4 && inner.starts_with("{{") && inner.ends_with("}}")) {
      inner = inner.substr(2, inner.size() - 4);
    }
    if (inner.starts_with("t:")) {
      const std::string key = inner.substr(2);
      if (translator_) {
        if (const std::optional<std::string> override_text = translator_(key)) {
          return *override_text;
        }
      }
      return i18n_.translate(key);
    }
    return inner;
  };
  if (data.is_object()) {
    const auto found = data.find("tooltips");
    if (found != data.end() && found->is_array()) {
      for (const Json &entry : *found) {
        if (!entry.is_object()) {
          continue;
        }
        TooltipHint hint;
        hint.icon = entry.value("icon", "");
        hint.label = label_for(entry.value("label", ""));
        hint.action = entry.value("action", "");
        hints.push_back(std::move(hint));
      }
      renderer_.set_tooltips(std::move(hints));
      return;
    }
  }
  if (const ViewDefinition *definition = document_.find_view(current_view_)) {
    hints.reserve(definition->tooltips.size());
    for (const TooltipDefinition &tooltip : definition->tooltips) {
      hints.push_back(
          TooltipHint{tooltip.icon, label_for(tooltip.label), tooltip.action});
    }
  }
  renderer_.set_tooltips(std::move(hints));
}

void UiSystem::focus_element(const std::string &id) {
  pending_focus_id_ = id;
  pending_focus_ = true;
  focus_engaged_ = true;
  dirty_ = true;
}

void UiSystem::set_focus_scope(const std::string &id) {
  focus_scope_id_ = id;
  pending_focus_id_.clear();
  pending_focus_ = true;
  focus_engaged_ = true;
  dirty_ = true;
}

void UiSystem::push_toast(std::string title, std::string message,
                          ToastVariant variant, std::string on_click) {
  Toast toast;
  toast.title = std::move(title);
  toast.message = std::move(message);
  toast.on_click = std::move(on_click);
  toast.variant = variant;
  toast.expires_at =
      SDL_GetTicks() + static_cast<std::uint64_t>(NotificationHub::kToastLifetimeMs);
  notifications_.push(std::move(toast));
  if (sounds_.effect) {
    sounds_.effect(variant == ToastVariant::Error ? "toast.error"
                                                  : "toast.show");
  }
  overlay_dirty_ = true;
}

void UiSystem::update_notifications() {
  if (notifications_.expire(SDL_GetTicks())) {
    if (sounds_.effect) {
      sounds_.effect("toast.hide");
    }
    overlay_dirty_ = true;
  }
}

void UiSystem::register_handlers() {
  handlers_.bind("go", [this](std::string_view argument) {
    navigate(std::string(argument));
  });
  handlers_.bind("open", [](std::string_view argument) {
    const std::string url(argument);
    if (url.empty() || url == "#") {
      return;
    }
    if (!SDL_OpenURL(url.c_str())) {
      log::warn(std::string{"ui: failed to open url "} + url);
    }
  });
}

void UiSystem::navigate(const std::string &view_id) {
  if (!screens_.contains(view_id) || !document_.has_view(view_id)) {
    log::warn(std::string{"ui: cannot navigate to "} + view_id);
    return;
  }
  if (!current_view_.empty() && current_view_ != view_id) {
    back_stack_.push_back(current_view_);
  }
  current_view_ = view_id;
  view_entered_ticks_ = SDL_GetTicks();
  const auto screen = screens_.find(current_view_);
  if (screen != screens_.end()) {
    screen->second->on_enter();
  }
  dirty_ = true;
}

void UiSystem::navigate_back() {
  if (back_stack_.empty()) {
    return;
  }
  current_view_ = back_stack_.back();
  back_stack_.pop_back();
  view_entered_ticks_ = SDL_GetTicks();
  const auto screen = screens_.find(current_view_);
  if (screen != screens_.end()) {
    screen->second->on_enter();
  }
  dirty_ = true;
}

void UiSystem::dispatch(const std::string &handler) {
  if (handler.rfind("overlay.toasts.click:", 0) == 0 ||
      handler.rfind("overlay.toasts.dismiss:", 0) == 0) {
    const std::size_t colon = handler.find(':');
    const std::string argument = handler.substr(colon + 1);
    const std::uint64_t id = std::strtoull(argument.c_str(), nullptr, 10);
    std::string action;
    if (const Toast *toast = notifications_.find(id)) {
      action = toast->on_click;
    }
    if (notifications_.dismiss(id, SDL_GetTicks())) {
      if (sounds_.effect) {
        sounds_.effect("toast.hide");
      }
      overlay_dirty_ = true;
    }
    if (!action.empty()) {
      dispatch(action);
    }
    return;
  }
  const std::size_t colon = handler.find(':');
  if (colon != std::string::npos) {
    const std::string name = handler.substr(0, colon);
    const std::string argument = handler.substr(colon + 1);
    if (handlers_.invoke(name, argument)) {
      dirty_ = true;
      return;
    }
  }
  const auto screen = screens_.find(current_view_);
  if (screen != screens_.end() &&
      screen->second->action(handler, services_)) {
    dirty_ = true;
    return;
  }
  if (handler == "quit" && quit_) {
    quit_();
  }
}

UiSound UiSystem::sound_for_handler(const std::string &handler) {
  return sound_for_ ? sound_for_(handler) : default_sound_for(handler);
}

void UiSystem::play_sound_for(const std::string &handler) {
  switch (sound_for_handler(handler)) {
  case UiSound::None:
    return;
  case UiSound::Back:
    if (sounds_.back) {
      sounds_.back();
    }
    return;
  case UiSound::Press:
    if (sounds_.press) {
      sounds_.press();
    }
    return;
  }
}

void UiSystem::rebuild() {
  hover_chain_.clear();
  active_text_ = nullptr;
  ScrollStates scroll_states;
  if (tree_ != nullptr && built_view_ == current_view_) {
    capture_scroll(tree_.get(), "", scroll_states);
  }
  built_view_ = current_view_;
  const auto screen = screens_.find(current_view_);
  if (screen == screens_.end()) {
    return;
  }
  update_panorama();
  const Json data = screen->second->data(services_);
  refresh_tooltips(data);
  DataContext context(data, &i18n_, translator_);
  ViewBuilder builder(document_, registry_);
  tree_ = builder.build(current_view_, context);
  if (tree_ == nullptr) {
    return;
  }
  ComputedStyle root_style = tree_->style();
  root_style.width = static_cast<float>(logical_width_);
  root_style.height = static_cast<float>(logical_height_);
  tree_->set_style(root_style);
  tree_->measure(renderer_);
  tree_->arrange(Rect{0.0F, 0.0F, static_cast<float>(logical_width_),
                      static_cast<float>(logical_height_)});
  if (!scroll_states.empty()) {
    restore_scroll(tree_.get(), "", scroll_states);
  }
  tree_->seed_clock(view_entered_ticks_);
  focus_order_.clear();
  Element *scope_root = tree_.get();
  if (!focus_scope_id_.empty()) {
    Element *scope = tree_->find_by_id(focus_scope_id_);
    if (scope == nullptr) {
      focus_scope_id_.clear();
    } else {
      scope_root = scope;
    }
  }
  collect_focusables(scope_root);
  if (pending_focus_) {
    pending_focus_ = false;
    int target = 0;
    if (!pending_focus_id_.empty()) {
      for (std::size_t index = 0; index < focus_order_.size(); ++index) {
        if (focus_order_[index]->id() == pending_focus_id_) {
          target = static_cast<int>(index);
          break;
        }
      }
    }
    pending_focus_id_.clear();
    if (!focus_order_.empty()) {
      focus_ = std::clamp(target, 0, static_cast<int>(focus_order_.size()) - 1);
      focus_engaged_ = true;
    }
  }
  if (focus_ >= static_cast<int>(focus_order_.size())) {
    focus_ =
        focus_order_.empty() ? 0 : static_cast<int>(focus_order_.size()) - 1;
  }
  if (focus_engaged_) {
    apply_focus();
  }
  dirty_ = false;
  rebuild_overlay();
}

void UiSystem::rebuild_overlay() {
  overlay_dirty_ = false;
  ScrollStates scroll_states;
  if (overlay_tree_ != nullptr) {
    capture_scroll(overlay_tree_.get(), "", scroll_states);
  }
  overlay_tree_ = nullptr;
  const auto overlay = screens_.find("overlay");
  if (overlay == screens_.end()) {
    return;
  }
  const Json data = overlay->second->data(services_);
  if (!data.value("overlayVisible", false)) {
    return;
  }
  DataContext context(data, &i18n_, translator_);
  ViewBuilder builder(document_, registry_);
  ElementPtr tree = builder.build("overlay", context);
  if (tree == nullptr) {
    return;
  }
  ComputedStyle root_style = tree->style();
  root_style.width = static_cast<float>(logical_width_);
  root_style.height = static_cast<float>(logical_height_);
  tree->set_style(root_style);
  tree->measure(renderer_);
  tree->arrange(Rect{0.0F, 0.0F, static_cast<float>(logical_width_),
                     static_cast<float>(logical_height_)});
  if (!scroll_states.empty()) {
    restore_scroll(tree.get(), "", scroll_states);
  }
  overlay_tree_ = std::move(tree);
}

void UiSystem::collect_focusables(Element *element) {
  if (element == nullptr || !element->style().visible) {
    return;
  }
  if (element->interactive()) {
    focus_order_.push_back(element);
  }
  for (const ElementPtr &child : element->children()) {
    collect_focusables(child.get());
  }
}

bool UiSystem::focus_path(Element *element, const Element *target,
                          std::vector<Element *> &path) const {
  if (element == nullptr || target == nullptr) {
    return false;
  }
  path.push_back(element);
  if (element == target) {
    return true;
  }
  for (const ElementPtr &child : element->children()) {
    if (focus_path(child.get(), target, path)) {
      return true;
    }
  }
  path.pop_back();
  return false;
}

void UiSystem::apply_focus() {
  for (Element *e : hover_chain_) {
    e->set_hovered(false);
  }
  hover_chain_.clear();
  if (focus_order_.empty()) {
    return;
  }
  Element *target = focus_order_[static_cast<std::size_t>(focus_)];
  std::vector<Element *> path;
  focus_path(tree_.get(), target, path);
  if (!path.empty()) {
    path.back()->set_hovered(true);
  }
  hover_chain_ = std::move(path);
  if (target != nullptr) {
    target->reveal();
  }
}

void UiSystem::move_focus(int direction) {
  if (focus_order_.empty()) {
    return;
  }
  focus_engaged_ = true;
  const int count = static_cast<int>(focus_order_.size());
  const int previous = focus_;
  focus_ = (focus_ + direction + count) % count;
  if (focus_ != previous && sounds_.scroll) {
    sounds_.scroll();
  }
  apply_focus();
}

void UiSystem::activate_focus() {
  if (focus_order_.empty()) {
    return;
  }
  focus_engaged_ = true;
  Element *target = focus_order_[static_cast<std::size_t>(focus_)];
  if (auto *input = dynamic_cast<TextInputElement *>(target)) {
    if (input->editing()) {
      end_text_edit(true);
      if (!input->on_submit().empty()) {
        dispatch(input->on_submit());
      }
    } else {
      begin_text_edit(target);
    }
    return;
  }
  if (dynamic_cast<SliderElement *>(target) != nullptr) {
    return;
  }
  const std::string handler = target->click_handler();
  play_sound_for(handler);
  dispatch(handler);
}

bool UiSystem::handle_back() {
  const auto screen = screens_.find(current_view_);
  if (screen != screens_.end() &&
      screen->second->on_escape(services_)) {
    dirty_ = true;
    return true;
  }
  if (sounds_.back) {
    sounds_.back();
  }
  navigate_back();
  return true;
}

TextInputElement *UiSystem::active_text_input() {
  if (active_text_ == nullptr) {
    return nullptr;
  }
  return dynamic_cast<TextInputElement *>(active_text_);
}

void UiSystem::begin_text_edit(Element *element) {
  if (auto *input = dynamic_cast<TextInputElement *>(element)) {
    input->begin_edit();
    active_text_ = element;
    text_result_ = input->text();
    text_edited_ = false;
#if defined(__ANDROID__)
    if (SDL_Window *window = SDL_GetRenderWindow(renderer_.handle())) {
      const Rect &bounds = input->bounds();
      int window_width = 0;
      int window_height = 0;
      SDL_GetWindowSize(window, &window_width, &window_height);
      const float scale_x = logical_width_ > 0
                                ? static_cast<float>(window_width) /
                                      static_cast<float>(logical_width_)
                                : 1.0F;
      const float scale_y = logical_height_ > 0
                                ? static_cast<float>(window_height) /
                                      static_cast<float>(logical_height_)
                                : 1.0F;
      const SDL_Rect rect{
          static_cast<int>(bounds.x * scale_x),
          static_cast<int>(bounds.y * scale_y),
          std::max(1, static_cast<int>(bounds.width * scale_x)),
          std::max(1, static_cast<int>(bounds.height * scale_y)),
      };
      SDL_SetTextInputArea(window, &rect, 0);
      SDL_StartTextInput(window);
    }
#endif
  }
}

void UiSystem::end_text_edit(bool commit) {
  TextInputElement *input = active_text_input();
  if (input == nullptr) {
    return;
  }
  if (commit) {
    text_result_ = input->text();
    text_edited_ = true;
  } else {
    text_edited_ = false;
  }
  input->end_edit(commit);
#if defined(__ANDROID__)
  if (SDL_Window *window = SDL_GetRenderWindow(renderer_.handle())) {
    SDL_StopTextInput(window);
  }
#endif
}

void UiSystem::release_text_edit(bool commit) {
  TextInputElement *input = active_text_input();
  if (input == nullptr) {
    return;
  }
  end_text_edit(commit);
  active_text_ = nullptr;
  if (commit && !input->on_submit().empty()) {
    dispatch(input->on_submit());
  }
}

void UiSystem::adjust_slider(SliderElement *slider, int delta) {
  slider->adjust(delta);
  commit_slider(slider);
}

void UiSystem::commit_slider(SliderElement *slider) {
  if (slider->on_change().empty()) {
    return;
  }
  dispatch(slider->on_change() + ":" + std::to_string(slider->value()));
}

void UiSystem::update_panorama() {
  if (panorama_source_) {
    std::string source = panorama_source_();
    if (!source.empty()) {
      panorama_.set_source(std::move(source));
    }
  }
}

bool UiSystem::hover_path(Element *e, const Vec2 &p,
                          std::vector<Element *> &path) const {
  if (e == nullptr || !e->style().visible || !e->bounds().contains(p))
    return false;
  path.push_back(e);
  for (auto it = e->children().rbegin(); it != e->children().rend(); ++it)
    if (hover_path(it->get(), p, path))
      break;
  return true;
}

void UiSystem::render() {
  update_notifications();
  TextInputElement *typing = active_text_input();
  const bool editing_text = typing != nullptr && typing->editing();
  if (dirty_ && !editing_text) {
    rebuild();
  } else if (overlay_dirty_) {
    rebuild_overlay();
  }
  if (tree_ != nullptr) {
    tree_->tick(SDL_GetTicks());
    tree_->arrange(tree_->bounds());
  }
  renderer_.begin_frame();
  SDL_Renderer *r = renderer_.handle();
  SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  int w = 0, h = 0;
  SDL_GetRenderOutputSize(r, &w, &h);
  panorama_.render(renderer_, Rect{0.0F, 0.0F, float(w), float(h)});
  SDL_SetRenderLogicalPresentation(r, logical_width_, logical_height_,
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);
  if (tree_ != nullptr)
    tree_->render(renderer_);
  if (overlay_tree_ != nullptr)
    overlay_tree_->render(renderer_);
  renderer_.end_frame();
}

Element *UiSystem::find_clickable(Element *element, const Vec2 &point) const {
  if (element == nullptr || !element->style().visible) {
    return nullptr;
  }
  auto &children = element->children();
  for (auto iterator = children.rbegin(); iterator != children.rend();
       ++iterator) {
    if (Element *found = find_clickable(iterator->get(), point)) {
      return found;
    }
  }
  if (element->interactive() && element->bounds().contains(point) &&
      !element->hidden_by_scroll()) {
    return element;
  }
  return nullptr;
}

Element *UiSystem::find_hoverable(Element *element, const Vec2 &point) const {
  if (element == nullptr || !element->style().visible) {
    return nullptr;
  }
  auto &children = element->children();
  for (auto iterator = children.rbegin(); iterator != children.rend();
       ++iterator) {
    if (Element *found = find_hoverable(iterator->get(), point)) {
      return found;
    }
  }
  if (element->bounds().contains(point)) {
    return element;
  }
  return nullptr;
}

void UiSystem::handle_event(const SDL_Event &event) {
  switch (event.type) {
  case SDL_EVENT_FINGER_DOWN: {
    SDL_Event converted = event;
    SDL_ConvertEventToRenderCoordinates(renderer_.handle(), &converted);
    const Vec2 point{converted.tfinger.x, converted.tfinger.y};
    scroll_finger_ = event.tfinger.fingerID;
    scroll_last_y_ = converted.tfinger.y;
    scroll_target_ = nullptr;
    if (overlay_tree_ != nullptr) {
      scroll_target_ = overlay_tree_->scroll_hit_test(point);
    }
    if (scroll_target_ == nullptr && tree_) {
      scroll_target_ = tree_->scroll_hit_test(point);
    }
    break;
  }
  case SDL_EVENT_FINGER_MOTION:
    if (scroll_target_ != nullptr && event.tfinger.fingerID == scroll_finger_) {
      SDL_Event converted = event;
      SDL_ConvertEventToRenderCoordinates(renderer_.handle(), &converted);
      const float drag = scroll_last_y_ - converted.tfinger.y;
      if (drag != 0.0F) {
        scroll_target_->on_wheel(drag / 24.0F);
        scroll_last_y_ = converted.tfinger.y;
      }
    }
    break;
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_CANCELED:
    if (event.tfinger.fingerID == scroll_finger_) {
      scroll_target_ = nullptr;
      scroll_finger_ = 0;
    }
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    if (event.button.button == SDL_BUTTON_LEFT) {
      SDL_Event converted = event;
      SDL_ConvertEventToRenderCoordinates(renderer_.handle(), &converted);
      const Vec2 point{converted.button.x, converted.button.y};
      if (mouse_observer_) {
        mouse_observer_(point.x, point.y);
      }
      if (press_observer_) {
        press_observer_(point.x, point.y);
      }
      TextInputElement *typing = active_text_input();
      if (typing != nullptr && typing->editing()) {
        release_text_edit(true);
      }
      if (const std::string *tooltip_action =
              renderer_.tooltip_action_at(point);
          tooltip_action != nullptr && !tooltip_action->empty()) {
        if (*tooltip_action == "@back") {
          if (sounds_.back) {
            sounds_.back();
          }
          handle_back();
        } else {
          play_sound_for(*tooltip_action);
          dispatch(*tooltip_action);
        }
        break;
      }
      if (Element *target = find_clickable(overlay_tree_.get(), point)) {
        const std::string handler = target->click_handler();
        dispatch(handler);
        break;
      }
      if (overlay_tree_ != nullptr &&
          find_blocking_element(overlay_tree_.get(), point) != nullptr) {
        break;
      }
      if (Element *target = find_clickable(tree_.get(), point)) {
        if (auto *input = dynamic_cast<TextInputElement *>(target)) {
          if (!input->editing()) {
            begin_text_edit(target);
          }
          break;
        }
        if (auto *slider = dynamic_cast<SliderElement *>(target)) {
          slider->click_at(point.x);
          commit_slider(slider);
          break;
        }
        const std::string handler = target->click_handler();
        play_sound_for(handler);
        dispatch(handler);
      }
    }
    break;
  case SDL_EVENT_MOUSE_MOTION:
    focus_engaged_ = false;
    {
      SDL_Event converted = event;
      SDL_ConvertEventToRenderCoordinates(renderer_.handle(), &converted);
      const Vec2 point{converted.motion.x, converted.motion.y};
      if (mouse_observer_) {
        mouse_observer_(point.x, point.y);
      }
      std::vector<Element *> path;
      if (Element *blocking = find_blocking_element(overlay_tree_.get(), point)) {
        if (hover_path(blocking, point, path)) {
          path.insert(path.begin(), overlay_tree_.get());
        }
      }
      if (path.empty()) {
        hover_path(tree_.get(), point, path);
      }
      if (path != hover_chain_) {
        for (Element *e : hover_chain_) {
          e->set_hovered(false);
        }
        Element *hover_target = nullptr;
        for (auto iterator = path.rbegin(); iterator != path.rend();
             ++iterator) {
          if ((*iterator)->interactive()) {
            hover_target = *iterator;
            break;
          }
        }
        if (hover_target != nullptr) {
          hover_target->set_hovered(true);
        }
        hover_chain_ = std::move(path);
      }
    }
    break;
  case SDL_EVENT_MOUSE_WHEEL: {
    float mx = 0.0F;
    float my = 0.0F;
    SDL_GetMouseState(&mx, &my);
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = mx;
    motion.motion.y = my;
    SDL_ConvertEventToRenderCoordinates(renderer_.handle(), &motion);
    const Vec2 point{motion.motion.x, motion.motion.y};
    const float delta = -event.wheel.y;
    Element *target = overlay_tree_ ? overlay_tree_->scroll_hit_test(point)
                                    : nullptr;
    if (target == nullptr && tree_) {
      target = tree_->scroll_hit_test(point);
    }
    if (target) {
      target->on_wheel(delta);
    }
  } break;
  case SDL_EVENT_TEXT_INPUT:
    if (TextInputElement *typing = active_text_input();
        typing != nullptr && typing->editing()) {
      typing->insert(event.text.text);
      text_edited_ = true;
    }
    break;
  case SDL_EVENT_KEY_DOWN:
    gamepad_ = false;
    renderer_.set_input_device(false);
    {
      TextInputElement *typing = active_text_input();
      const bool editing = typing != nullptr && typing->editing();
      Element *focused = nullptr;
      if (focus_engaged_ && focus_ >= 0 &&
          focus_ < static_cast<int>(focus_order_.size())) {
        focused = focus_order_[static_cast<std::size_t>(focus_)];
      }
      switch (event.key.key) {
      case SDLK_BACKSPACE:
        if (typing != nullptr) {
          typing->backspace();
          text_edited_ = true;
        }
        break;
      case SDLK_RETURN:
        if (editing && typing->multiline()) {
          typing->insert("\n");
          text_edited_ = true;
          break;
        }
        if (editing) {
          end_text_edit(true);
          if (!typing->on_submit().empty()) {
            dispatch(typing->on_submit());
          }
          break;
        }
        activate_focus();
        break;
      case SDLK_SPACE:
        if (editing) {
          break;
        }
        activate_focus();
        break;
      case SDLK_ESCAPE:
        if (editing) {
          end_text_edit(false);
          break;
        }
        handle_back();
        break;
      case SDLK_LEFT:
        if (editing) {
          break;
        }
        if (auto *slider = dynamic_cast<SliderElement *>(focused)) {
          adjust_slider(slider, -5);
          break;
        }
        move_focus(-1);
        break;
      case SDLK_RIGHT:
        if (editing) {
          break;
        }
        if (auto *slider = dynamic_cast<SliderElement *>(focused)) {
          adjust_slider(slider, 5);
          break;
        }
        move_focus(1);
        break;
      case SDLK_UP:
        if (editing) {
          break;
        }
        move_focus(-1);
        break;
      case SDLK_DOWN:
        if (editing) {
          break;
        }
        move_focus(1);
        break;
      default:
        break;
      }
    }
    break;
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    gamepad_ = true;
    renderer_.set_input_device(true);
    set_controller_type(
        controller_type(SDL_GetGamepadTypeForID(event.gbutton.which)));
    {
      TextInputElement *typing = active_text_input();
      const bool editing = typing != nullptr && typing->editing();
      Element *focused = nullptr;
      if (focus_engaged_ && focus_ >= 0 &&
          focus_ < static_cast<int>(focus_order_.size())) {
        focused = focus_order_[static_cast<std::size_t>(focus_)];
      }
      switch (event.gbutton.button) {
      case SDL_GAMEPAD_BUTTON_DPAD_UP:
        if (!editing) {
          move_focus(-1);
        }
        break;
      case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        if (!editing) {
          move_focus(1);
        }
        break;
      case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        if (editing) {
          break;
        }
        if (auto *slider = dynamic_cast<SliderElement *>(focused)) {
          adjust_slider(slider, -5);
          break;
        }
        move_focus(-1);
        break;
      case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        if (editing) {
          break;
        }
        if (auto *slider = dynamic_cast<SliderElement *>(focused)) {
          adjust_slider(slider, 5);
          break;
        }
        move_focus(1);
        break;
      case SDL_GAMEPAD_BUTTON_SOUTH:
        if (editing) {
          end_text_edit(true);
          if (!typing->on_submit().empty()) {
            dispatch(typing->on_submit());
          }
          break;
        }
        activate_focus();
        break;
      case SDL_GAMEPAD_BUTTON_EAST:
        if (editing) {
          end_text_edit(false);
          break;
        }
        handle_back();
        break;
      default:
        break;
      }
    }
    break;
  case SDL_EVENT_GAMEPAD_ADDED:
  case SDL_EVENT_GAMEPAD_REMOVED:
    detect_controller_type();
    break;
  default:
    break;
  }
}
} // namespace lazuli
