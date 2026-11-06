#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace RC::Unreal { class UObject; }

namespace kh3ap {
class RuntimeGameSettingsShortcut {
public:
  RuntimeGameSettingsShortcut();
  ~RuntimeGameSettingsShortcut();
  auto bind_viewport(RC::Unreal::UObject *viewport) -> void;
  // Translate Square/X to Escape only in the title or top-level camp menu.
  auto route(int32_t controller, const std::wstring &name, int32_t event) -> bool;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace kh3ap
