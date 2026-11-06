#pragma once
#include "kh3ap/features/ui/options_menu/detour.h"
#include "kh3ap/features/ui/options_menu/models.h"
#include "kh3ap/features/ui/options_menu/callback_state.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/core/input/controller_focus.h"
#include "kh3ap/features/ui/game_settings_shortcut.h"
#include "kh3ap/features/ui/ui_sound_service.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kh3ap
{
    struct RuntimeOptionsMenuService::Impl
    {
        using UObject = RC::Unreal::UObject;
        using UFunction = RC::Unreal::UFunction;
        using FName = RC::Unreal::FName;
        using FWeakObjectPtr = RC::Unreal::FWeakObjectPtr;
        using UnrealScriptFunctionCallableContext = RC::Unreal::UnrealScriptFunctionCallableContext;
        using RawFKey = options_menu_detail::RawFKey;
        using DisplayTransform = options_menu_detail::DisplayTransform;
        using RuntimeOptionsScreenMode = options_menu_detail::RuntimeOptionsScreenMode;
        using RuntimeOptionsPageFrame = options_menu_detail::RuntimeOptionsPageFrame;
        using ViewportInputKeyFn = bool (*)(void* viewport_client, void* viewport,
                                            int32_t controller_id, RawFKey* key, int32_t event_type,
                                            float amount_depressed, bool gamepad);
        using ViewportInputAxisFn = bool (*)(void*, void*, int32_t, RawFKey*, float, float, int32_t,
                                             bool);
        using ViewportLostFocusFn = void (*)(void*, void*);

        explicit Impl(RuntimeOptionsMenuConfig config);

        auto parent_anchor_index(size_t index) const -> int32_t;

        auto page_anchor_index(size_t index) const -> int32_t;

        auto choice_anchor_index(size_t index) const -> int32_t;

        ~Impl();

        auto block_background_controller_input() const -> bool;

        auto dispatch_key(void* client, void* viewport, int32_t controller, RawFKey* key,
                          int32_t event, float amount, bool gamepad) -> bool;

        static auto viewport_input_axis_hook(void* client, void* viewport, int32_t controller,
                                             RawFKey* key, float delta, float delta_time,
                                             int32_t samples, bool gamepad) -> bool;

        auto release_background_controller_input(void* client, void* viewport) -> void;

        static auto viewport_lost_focus_hook(void* client, void* viewport) -> void;

        static auto viewport_input_key_hook(void* viewport_client, void* viewport,
                                            int32_t controller_id, RawFKey* key, int32_t event_type,
                                            float amount_depressed, bool gamepad) -> bool;

        auto find_active_viewport_client() -> UObject*;

        auto viewport_console_property_offset(UObject* viewport_client) -> std::optional<int32_t>;

        auto try_install_viewport_input_hook() -> bool;

        auto install_once() -> void;

        auto member_path(UObject* object, std::initializer_list<std::wstring_view> members)
            -> UObject*;

        auto display_child_named(UObject* container, std::wstring_view name) -> UObject*;

        auto object_member(UObject* root, std::wstring_view marker, std::wstring_view instance)
            -> UObject*;

        auto page_row(UObject* root, size_t index) -> UObject*;

        auto choice_row(UObject* root, size_t index) -> UObject*;

        auto page_scroll_host(UObject* root) -> UObject*;

        auto live_menu_root() -> UObject*;

        auto live_parent_button(UObject* root, size_t index) -> UObject*;

        auto active_parent_button(UObject* root) -> UObject*;

        auto list_item(UObject* list, int32_t index) -> UObject*;

        auto write_callback_index(UFunction* function, void* params, int32_t value) -> bool;

        auto set_button_selected(UObject* button, bool selected) -> void;

        auto repaint_button_selected(UObject* button, bool selected) -> void;

        auto set_button_visible(UObject* button, bool visible) -> void;

        auto prepare_detached_item(UObject* item) -> void;

        auto normalize_parent_button(UObject* button) -> void;

        auto set_button_label(UObject* button, std::wstring_view label) -> bool;

        auto set_adjusted_text(UObject* object, std::wstring_view text) -> bool;

        auto set_adjusted_text_color(UObject* object, int32_t color) -> bool;

        auto show_adjustable_text_only(UObject* timeline, UObject* adjustable_text) -> void;

        auto set_setting_item(UObject* item, std::wstring_view name, std::wstring_view value,
                              bool enabled) -> void;

        auto set_option_item(UObject* item, std::wstring_view label, bool checked, bool enabled)
            -> void;

        auto configure_anchor(UObject* object, int32_t up, int32_t down) -> void;

        auto clone_parent_button(UObject* body, UObject* source, size_t menu_index) -> UObject*;

        auto clone_detached_item(UObject* root, UObject* source, std::wstring_view marker,
                                 std::wstring_view instance_name, int32_t private_anchor_index)
            -> UObject*;

        auto clone_detached_scroll_host(UObject* root, UObject* source) -> UObject*;

        auto display_transform(std::initializer_list<UObject*> chain) -> DisplayTransform;

        auto apply_display_transform(UObject* item, const DisplayTransform& transform) -> void;

        static auto extrapolated_transform(const DisplayTransform& first,
                                           const DisplayTransform& second, size_t index)
            -> DisplayTransform;

        auto ensure_detached_items(UObject* root) -> bool;

        auto active_menu() const -> const RuntimeOptionsMenuDefinition*;

        auto active_page_frame() -> RuntimeOptionsPageFrame*;

        auto active_page_frame() const -> const RuntimeOptionsPageFrame*;

        auto selected_item() const -> const RuntimeOptionsItemDefinition*;

        auto reset_navigation(size_t menu_index) -> void;

        auto item_available(const RuntimeOptionsItemDefinition& item) const -> bool;

        auto choice_available(const RuntimeOptionsChoice& choice) const -> bool;

        auto item_help_text(const RuntimeOptionsItemDefinition& item, bool choosing) const
            -> std::wstring;

        auto current_choice_index(const RuntimeOptionsItemDefinition& item) const -> std::optional<size_t>;

        auto item_value_text(const RuntimeOptionsItemDefinition& item) const -> std::wstring;

        auto refresh_parent_labels(UObject* root) -> void;

        auto refresh_help_text(UObject* root, std::wstring_view text) -> void;

        auto hide_detached_items(UObject* root) -> void;

        auto set_active_parent_selected(UObject* root) -> void;

        auto update_page_scrollbar(UObject* root, size_t item_count, size_t visible_count,
                                   size_t scroll_position) -> void;

        auto paint_page_rows(UObject* root) -> void;

        auto paint_choice_rows(UObject* root) -> void;

        auto show_parent(UObject* root) -> void;

        auto show_page(UObject* root) -> void;

        auto show_choice(UObject* root) -> void;

        auto parent_button_index(UObject* root, UObject* object) -> std::optional<size_t>;

        auto parent_menu_index_for_anchor(int32_t index) const -> std::optional<size_t>;

        auto is_parent_anchor_index(int32_t index) const -> bool;

        auto is_page_anchor_index(int32_t index) const -> bool;

        auto is_choice_anchor_index(int32_t index) const -> bool;

        auto is_custom_anchor_index(int32_t index) const -> bool;

        auto resolve_custom_anchor_index(UObject* root, UObject* anchor, int32_t callback_index)
            -> int32_t;

        auto render_custom_anchor(UObject* root, int32_t anchor_index) -> void;

        auto focus_custom_anchor(UObject* root, int32_t anchor_index) -> bool;

        auto selected_custom_button(UObject* root) -> UObject*;

        auto trigger_selected_button_decide(UObject* root) -> void;

        auto ensure_injected(UObject* root) -> bool;

        auto handle_focus_in(UnrealScriptFunctionCallableContext& context) -> void;

        auto handle_focus_out(UnrealScriptFunctionCallableContext& context) -> void;

        auto handle_anchor_in_pre(UnrealScriptFunctionCallableContext& context) -> void;

        auto handle_anchor_in_post(UnrealScriptFunctionCallableContext& context) -> void;

        auto handle_anchor_out_pre(UnrealScriptFunctionCallableContext& context) -> void;

        auto selected_item_available() const -> bool;

        auto apply_selected_choice() -> void;

        static auto is_confirm_key(std::wstring_view key_name) -> bool;

        static auto is_back_key(std::wstring_view key_name) -> bool;

        static auto is_previous_navigation_key(std::wstring_view key_name) -> bool;

        static auto is_next_navigation_key(std::wstring_view key_name) -> bool;

        auto gfx_bool_member_is_true(UObject* object, std::wstring_view member_name) const -> bool;

        auto recover_capture_from_selected_row(UObject* root) -> bool;

        auto trace_input_focus(std::wstring_view event, int32_t anchor) -> void;

        auto route_viewport_key(const FName& key_name, int32_t controller_id, int32_t event_type,
                                bool gamepad) -> bool;

        auto handle_viewport_key(const FName& key_name, int32_t event_type) -> bool;

        auto tick() -> void;

        // Declaration order preserves the host, weak-object pools and detour ownership.

        RuntimeOptionsMenuConfig config_;
        mutable options_menu_detail::CallbackState callback_state_;
        RuntimeUiSoundService ui_sound_service_{};
        FWeakObjectPtr menu_context_{};
        FWeakObjectPtr menu_root_{};
        std::vector<FWeakObjectPtr> parent_buttons_{};
        std::vector<int32_t> parent_anchor_indices_{};
        std::vector<std::wstring> parent_members_{};
        std::vector<std::wstring> parent_instances_{};
        std::wstring page_scroll_host_member_{};
        std::wstring page_scroll_host_instance_{};
        std::vector<std::wstring> page_members_{};
        std::vector<std::wstring> page_instances_{};
        std::vector<std::wstring> choice_members_{};
        std::vector<std::wstring> choice_instances_{};
        std::vector<RuntimeOptionsPageFrame> page_stack_{};
        RuntimeOptionsScreenMode screen_mode_{RuntimeOptionsScreenMode::inactive};
        bool installed_{};
        bool injection_logged_{};
        bool injection_ready_{};
        bool injection_pending_{};
        bool capture_active_{};
        bool viewport_input_key_hooked_{};
        bool viewport_input_hook_failed_{};
        int32_t active_custom_anchor_index_{-1};
        int32_t pending_custom_anchor_index_{-1};
        int32_t page_anchor_base_{};
        int32_t choice_anchor_base_{};
        size_t page_capacity_{};
        size_t choice_capacity_{};
        DisplayTransform page_scroll_host_transform_{};
        DisplayTransform page_first_transform_{};
        DisplayTransform page_second_transform_{};
        DisplayTransform choice_first_transform_{};
        DisplayTransform choice_second_transform_{};
        size_t active_menu_index_{};
        size_t selected_choice_index_{};
        uint32_t injection_delay_ticks_{};
        uint32_t injection_attempts_{};
        uint32_t parent_label_refresh_ticks_{};
        int32_t navigation_direction_latched_{};
        std::wstring consumed_key_name_{};
        RuntimeOptionsInputOwnership input_ownership_{};
        RuntimeControllerFocus controller_focus_{};
        RuntimeGameSettingsShortcut settings_shortcut_{};
        bool controller_focus_installed_{};
        std::unique_ptr<PLH::x64Detour> viewport_input_axis_detour_{};
        std::unique_ptr<PLH::x64Detour> viewport_lost_focus_detour_{};
        static inline uint64_t s_original_input_axis_{};
        static inline uint64_t s_original_lost_focus_{};
        uint32_t input_diagnostic_count_{};
        uint64_t viewport_input_key_trampoline_{};
        std::unique_ptr<PLH::x64Detour> viewport_input_key_detour_{};
        static inline std::atomic<Impl*> s_input_key_owner_{nullptr};
        static inline std::atomic<uintptr_t> s_original_input_key_{0};
        std::pair<int, int> focus_in_hook_ids_{};
        std::pair<int, int> focus_out_hook_ids_{};
        std::pair<int, int> anchor_in_hook_ids_{};
        std::pair<int, int> anchor_out_hook_ids_{};
    };
} // namespace kh3ap
