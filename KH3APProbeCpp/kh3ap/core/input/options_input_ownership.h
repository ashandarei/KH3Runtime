#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace kh3ap {

// Input ownership belongs to a physical key press, not the currently focused
// page. Never hide a release from a native controller that saw its press.
// Likewise, a custom press must not leak its release into a newly focused page.
class RuntimeOptionsInputOwnership {
public:
  template <typename Handler>
  auto route(int32_t controller, const std::wstring &key, int32_t event,
             Handler &&handle_custom) -> bool {
    const auto identity = std::make_pair(controller, key);
    const auto held = held_keys_.find(identity);
    if (held != held_keys_.end()) {
      const bool consumed = held->second;
      if (event == 1) { // IE_Released
        held_keys_.erase(held);
        if (consumed) {
          (void)handle_custom(); // Release the menu's direction/action latch.
        }
      }
      // Repeats and duplicate Pressed events retain the original owner.
      return consumed;
    }
    if (event != 0) { // No observed press: allow native state to clear.
      return false;
    }
    const bool consumed = handle_custom();
    held_keys_.emplace(identity, consumed);
    return consumed;
  }

private:
  std::map<std::pair<int32_t, std::wstring>, bool> held_keys_{};
};

} // namespace kh3ap
