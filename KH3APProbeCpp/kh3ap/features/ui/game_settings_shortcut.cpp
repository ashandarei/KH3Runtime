#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/ui/title_state_ids.h"
#include "kh3ap/features/ui/game_settings_shortcut.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstddef>
#include <set>

using namespace RC;
using namespace RC::Unreal;
namespace kh3ap {
namespace {
// Shared title fields are paired in the EGS/Steam title state machines.
// Camp vtables and its trailing submenu/input fields differ by storefront.
struct EgsLayout {
  static constexpr uintptr_t image_timestamp = kh3ap::native_build::egs.timestamp;
  static constexpr uintptr_t image_size = kh3ap::native_build::egs.image_size;
  static constexpr uintptr_t title_hud_vtable = 0x7c381e0;
  static constexpr uintptr_t camp_controller_vtable = 0x7c2a488;
  static constexpr uintptr_t camp_menu_vtable = 0x7c45698;
  static constexpr uintptr_t ui_manager_vtable = 0x7c427f8;
  static constexpr uintptr_t title_state = 0x570;
  static constexpr uintptr_t title_transition_gate = 0x5a5;
  static constexpr uintptr_t title_mode = 0x574;
  static constexpr uintptr_t title_pending_gate = 0x5d0;
  static constexpr uintptr_t camp_flags = 0x60;
  static constexpr uintptr_t camp_menu = 0x50;
  static constexpr uintptr_t camp_menu_open = 0xa1;
  static constexpr uintptr_t camp_submenu = 0x468;
  static constexpr uintptr_t camp_input_gate = 0x470;
};
struct NativeLayout {
  uintptr_t title_vtable{EgsLayout::title_hud_vtable};
  uintptr_t camp_controller_vtable{EgsLayout::camp_controller_vtable};
  uintptr_t camp_menu_vtable{EgsLayout::camp_menu_vtable};
  uintptr_t ui_manager_vtable{EgsLayout::ui_manager_vtable};
  size_t camp_submenu{EgsLayout::camp_submenu};
  size_t camp_input_gate{EgsLayout::camp_input_gate};
};
constexpr int32_t input_pressed = 0;
constexpr int32_t input_released = 1;

template<class T> auto field(void *object, size_t offset) -> T {
  __try { return *reinterpret_cast<T *>(static_cast<std::byte *>(object) + offset); }
  __except(EXCEPTION_EXECUTE_HANDLER) { return {}; }
}
auto title_ready(UObject *title, uintptr_t base, const NativeLayout& layout) -> bool {
  if (!is_uobject_valid_fast(title)) return false;
  const auto state = field<int32_t>(title, EgsLayout::title_state);
  return field<uintptr_t>(title, 0) == base + layout.title_vtable &&
         (state == title_state_ids::intro_movie || state == title_state_ids::menu_ready) &&
         field<uint8_t>(title, EgsLayout::title_transition_gate) == 0 &&
         field<int32_t>(title, EgsLayout::title_mode) != 1 &&
         field<int32_t>(title, EgsLayout::title_pending_gate) == 0;
}
auto camp_ready(UObject *camp, uintptr_t base, const NativeLayout& layout) -> bool {
  if (!is_uobject_valid_fast(camp) || field<uintptr_t>(camp, 0) != base + layout.camp_controller_vtable ||
      (field<uint32_t>(camp, EgsLayout::camp_flags) & 1) == 0) return false;
  auto *ui = field<UObject *>(camp, EgsLayout::camp_menu);
  // CampMenu::Open/Close own a1; the layout selects its active submenu. The three
  // transition/selection flags are also checked by its normal input handler.
  return is_uobject_valid_fast(ui) && field<uintptr_t>(ui, 0) == base + layout.camp_menu_vtable &&
      field<uint8_t>(ui, EgsLayout::camp_menu_open) != 0 && field<uintptr_t>(ui, layout.camp_submenu) == 0 &&
      field<uint8_t>(ui, layout.camp_input_gate) == 0 && field<uint8_t>(ui, layout.camp_input_gate + 1) == 0 &&
      field<uint8_t>(ui, layout.camp_input_gate + 2) == 0;
}
}
struct RuntimeGameSettingsShortcut::Impl {
  Impl() {
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(GetModuleHandleW(nullptr));
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(reinterpret_cast<const std::byte *>(dos) + dos->e_lfanew);
    supported = nt->Signature == IMAGE_NT_SIGNATURE &&
      nt->FileHeader.TimeDateStamp == EgsLayout::image_timestamp && nt->OptionalHeader.SizeOfImage == EgsLayout::image_size;
    if (nt->Signature == IMAGE_NT_SIGNATURE && kh3ap::native_build::steam.matches(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage)) {
      // Same camp owners and offsets as the audited Steam camp-return adapter.
      layout = {0x6a56078, 0x6a43610, 0x6a7d2e0, 0x6a6cec8, 0x470, 0x478};
      supported = true;
    }
  }
  auto route(int32_t controller, const std::wstring &name, int32_t event) -> bool {
    if (!supported || name != L"Gamepad_FaceButton_Left") return false;
    // Keep the entire translated key cycle, including release after the menu
    // changes or focus is lost. Never translate a release for a gameplay press.
    if (held.contains(controller)) {
      if (event == input_released) held.erase(controller);
      return true;
    }
    if (event != input_pressed) return false;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    // Follow the current viewport's ownership links. Do not search the
    // global object array when a menu is closed (Square is also gameplay input).
    auto *viewport_object = viewport.Get();
    if (!is_uobject_valid_fast(viewport_object)) return false;
    auto *instance = properties.read_object(viewport_object, L"GameInstance");
    if (!is_uobject_valid_fast(instance)) return false;
    auto *manager = properties.read_object(instance, L"m_UIManager");
    if (!is_uobject_valid_fast(manager) || field<uintptr_t>(manager, 0) != base + layout.ui_manager_vtable)
      return false;
    const bool from_camp = camp_ready(properties.read_object(manager, L"m_CampController"), base, layout);
    if (!from_camp) {
      auto *player = properties.read_object(manager, L"m_pPlayerController");
      if (!is_uobject_valid_fast(player) || !title_ready(properties.read_object(player, L"MyHUD"), base, layout))
        return false; // MyHUD is a normal gameplay HUD outside the title map.
    }
    held.insert(controller);
    return true;
  }
  NativeLayout layout;
  ReflectedPropertyCache<8> properties;
  bool supported{};
  FWeakObjectPtr viewport{};
  std::set<int32_t> held{};
};
RuntimeGameSettingsShortcut::RuntimeGameSettingsShortcut() : impl_(std::make_unique<Impl>()) {}
RuntimeGameSettingsShortcut::~RuntimeGameSettingsShortcut() = default;

auto RuntimeGameSettingsShortcut::bind_viewport(UObject *viewport) -> void { impl_->viewport = viewport; }

auto RuntimeGameSettingsShortcut::route(int32_t controller, const std::wstring &name, int32_t event) -> bool { return impl_->route(controller, name, event); }

} // namespace kh3ap
