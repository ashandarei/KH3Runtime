#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <utility>

namespace kh3ap {

// Track physical presses even while unfocused. A button held across an
// alt-tab must be released before it can become a new game action.
class RuntimeControllerFocus {
public:
  using Key = std::pair<int32_t, std::wstring>;
  enum class Route { forward, release, consume };

  auto button(const Key &key, int32_t event, bool focused, bool enabled = true) -> Route {
    if (!enabled) { focused = true; blocked_.erase(key); }
    if (event == 1) {
      blocked_.erase(key);
      return held_.erase(key) ? Route::release : Route::consume;
    }
    if (!focused || blocked_.contains(key)) {
      blocked_.insert(key);
      return held_.erase(key) ? Route::release : Route::consume;
    }
    if (event == 0) held_.insert(key);
    return Route::forward;
  }

  auto axis(const Key &key, float value, bool focused, bool enabled = true) -> float {
    if (enabled && !focused) value = 0.0F;
    if (value == 0.0F) axes_.erase(key);
    else axes_.insert(key);
    return value;
  }

  template <typename Release, typename Neutral>
  auto lose_focus(Release &&release, Neutral &&neutral, bool enabled = true) -> void {
    if (!enabled) return;
    const auto held = std::exchange(held_, {});
    const auto axes = std::exchange(axes_, {});
    blocked_.insert(held.begin(), held.end());
    for (const auto &key : held) release(key);
    for (const auto &key : axes) neutral(key);
  }

private:
  std::set<Key> held_{};
  std::set<Key> blocked_{};
  std::set<Key> axes_{};
};

} // namespace kh3ap
