#pragma once
#include "lazuli/Binding.hpp"
#include "lazuli/Element.hpp"
#include "lazuli/Handlers.hpp"
#include "lazuli/I18n.hpp"
#include "lazuli/Notifications.hpp"
#include "lazuli/Render/Panorama.hpp"
#include "lazuli/Render/SdlRenderer.hpp"
#include "lazuli/Screen.hpp"
#include "lazuli/Xml.hpp"
#include <SDL3/SDL_events.h>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
struct SDL_Renderer;
namespace lazuli {
enum class ControllerType { Unknown, Xbox360, Durango, PS3, Orbis, Switch };
enum class UiSound { None, Press, Back };
struct UiSounds {
  std::function<void()> press;
  std::function<void()> back;
  std::function<void()> scroll;
  std::function<void(std::string_view)> effect;
};

using SoundFor = std::function<UiSound(std::string_view)>;
class UiSystem {
public:
  UiSystem(SDL_Renderer *renderer, std::filesystem::path ui_docs_root,
           std::filesystem::path public_root, std::filesystem::path i18n_root);
  ~UiSystem();
  UiSystem(const UiSystem &) = delete;
  UiSystem &operator=(const UiSystem &) = delete;
  bool initialize();
  void set_language(const std::string &language);
  void set_language_font(
      std::function<std::string(std::string_view)> resolver) {
    language_font_ = std::move(resolver);
  }
  void register_screen(ScreenPtr screen);
  void set_services(void *services) { services_ = services; }
  void set_translator(Translator translator) {
    translator_ = std::move(translator);
  }
  void set_sounds(UiSounds sounds) { sounds_ = std::move(sounds); }
  void set_sound_for(SoundFor sound_for) { sound_for_ = std::move(sound_for); }
  void set_panorama_source(std::function<std::string()> source) {
    panorama_source_ = std::move(source);
  }
  void set_mouse_observer(std::function<void(float, float)> observer) {
    mouse_observer_ = std::move(observer);
  }
  void set_quit(std::function<void()> quit) { quit_ = std::move(quit); }
  void set_status_text(std::string text) {
    renderer_.set_status_text(std::move(text));
  }
  const I18n &i18n() const { return i18n_; }
  NotificationHub &notifications() { return notifications_; }
  std::string tooltip_set() const { return renderer_.tooltip_set(); }
  void reset_focus() {
    focus_engaged_ = true;
    focus_ = 0;
    dirty_ = true;
  }
  const std::string &text_result() const { return text_result_; }
  bool text_edited() const { return text_edited_; }
  void navigate(const std::string &view_id);
  void navigate_back();
  std::string translate(const std::string &key) const {
    return i18n_.translate(key);
  }
  std::string translate(const std::string &key,
                        const std::string &defaultval) const {
    return i18n_.translate(key, defaultval);
  }
  void invalidate() { dirty_ = true; }
  void push_toast(std::string title, std::string message,
                  ToastVariant variant, std::string on_click = "");
  const std::string &current_view() const { return current_view_; }
  void set_controller_type(ControllerType type);
  void focus_element(const std::string &id);
  void set_focus_scope(const std::string &id);
  std::vector<Element *> hover_chain_;
  void handle_event(const SDL_Event &event);
  void render();

private:
  void register_handlers();
  void rebuild();
  void rebuild_overlay();
  void update_notifications();
  void refresh_tooltips(const Json &data);
  void detect_controller_type();
  void update_panorama();
  void dispatch(const std::string &handler);
  UiSound sound_for_handler(const std::string &handler);
  void play_sound_for(const std::string &handler);
  void collect_focusables(Element *element);
  bool focus_path(Element *element, const Element *target,
                  std::vector<Element *> &path) const;
  void apply_focus();
  void move_focus(int direction);
  void activate_focus();
  bool handle_back();
  TextInputElement *active_text_input();
  void begin_text_edit(Element *element);
  void end_text_edit(bool commit);
  void release_text_edit(bool commit);
  void adjust_slider(SliderElement *slider, int delta);
  void commit_slider(SliderElement *slider);
  Element *find_clickable(Element *element, const Vec2 &point) const;
  Element *find_hoverable(Element *element, const Vec2 &point) const;
  bool hover_path(Element *e, const Vec2 &p,
                  std::vector<Element *> &path) const;
  SDL_FingerID scroll_finger_ = 0;
  Element *scroll_target_ = nullptr;
  float scroll_last_y_ = 0.0F;
  Panorama panorama_;
  ElementRegistry &registry_;
  SdlRenderer renderer_;
  I18n i18n_;
  UiDocument document_;
  HandlerRegistry handlers_;
  void *services_ = nullptr;
  Translator translator_;
  UiSounds sounds_;
  SoundFor sound_for_;
  std::function<std::string(std::string_view)> language_font_;
  std::function<std::string()> panorama_source_;
  std::function<void(float, float)> mouse_observer_;
  std::function<void()> quit_;
  std::filesystem::path ui_docs_root_;
  std::filesystem::path i18n_root_;
  std::map<std::string, ScreenPtr> screens_;
  std::string current_view_;
  std::vector<std::string> back_stack_;
  ElementPtr tree_;
  ElementPtr overlay_tree_;
  NotificationHub notifications_;
  bool overlay_dirty_ = true;
  Element *active_text_ = nullptr;
  std::string text_result_;
  bool text_edited_ = false;
  std::vector<Element *> focus_order_;
  int focus_ = 0;
  bool focus_engaged_ = false;
  std::string focus_scope_id_;
  std::string pending_focus_id_;
  bool pending_focus_ = false;
  bool gamepad_ = true;
  ControllerType controller_type_ = ControllerType::Unknown;
  bool dirty_ = true;
  uint64_t view_entered_ticks_ = 0U;
  int logical_width_ = 1280;
  int logical_height_ = 720;
};
} // namespace lazuli
