#include "kh3ap/features/world/space/space_service_impl.h"
#include "kh3ap/features/world/space/space_profile.h"
#include "kh3ap/features/world/space/space_ship_policy.h"
#include "kh3ap/core/gfx/gfx_reflection_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <algorithm>

namespace kh3ap {
namespace {
        constexpr auto embark_denied_message_duration = std::chrono::seconds(4);
        constexpr auto max_embark_help_movie_index = 4096;
        constexpr auto max_embark_help_nodes = 128;

constexpr GfxProbeFunctionPaths gfx{L"/Script/ScaleformUI.GFxObject:GetMemberValue",
    L"/Script/ScaleformUI.GFxObject:Invoke",L"/Script/ScaleformUI.GFxObject:SetMemberValue"};
constexpr std::array line_names{L"apSpaceAccessTitle", L"apSpaceAccessRequirements"};

UObject* help_line(UObject* commands, UObject* component, const wchar_t* name, bool& created) {
    if (auto* existing = try_gfx_get_member_value_object(gfx, component, name)) return existing;
    // KH3 exposes Invoke but no GFx CreateObject. Its authored Button.clone()
    // supplies a fresh TextField. Extract only that field; the clone never
    // enters the display tree or receives focus/input.
    auto* command_body = try_gfx_get_member_value_object(gfx, commands, L"menuCmdSetObj");
    auto* button = try_gfx_get_member_value_object(gfx, command_body, L"cmd1");
    const auto index = try_gfx_get_member_value_number(gfx, button, L"index");
    if (!index || *index < 0 || *index > max_embark_help_movie_index) return nullptr;
    auto* clone = try_gfx_invoke_no_args_object(gfx, button, L"clone");
    // AnchorComponent.clone copies index, replacing the real button in the
    // global focus dictionary even though the clone is never displayed.
    // Unregister it and re-register the original before extracting the label.
    if (clone) try_gfx_set_member_value_int(gfx, clone, L"index", -1);
    try_gfx_set_member_value_int(gfx, button, L"index", -1);
    try_gfx_set_member_value_int(gfx, button, L"index", static_cast<int32_t>(*index));
    auto* label = try_gfx_get_member_value_object(gfx,
        try_gfx_get_member_value_object(gfx, clone, L"menuName"), L"txName");
    auto* line = try_gfx_get_member_value_object(gfx, label, L"textField");
    if (!is_uobject_valid_fast(line)) return nullptr;
    // Prevent the discarded label's FRAME_CONSTRUCTED callback from writing
    // into the extracted field after it joins the help panel.
    if (!try_gfx_set_member_value_null(gfx, label, L"textField")) return nullptr;
    try_gfx_invoke_no_args(gfx, clone, L"stop");
    for (const auto* property : {L"multiline", L"wordWrap", L"selectable", L"mouseEnabled", L"visible"})
        try_gfx_set_member_value_bool(gfx, line, property, false);
    if (!try_gfx_invoke_object_arg(gfx, component, L"addChild", line)) return nullptr;
    // Timeline instance names are immutable. Keep an AS reference on the
    // dynamic help component instead of renaming the extracted TextField.
    try_gfx_set_member_value_object(gfx, component, name, line);
    created = true;
    return line;
}
}

bool RuntimeSpaceService::Impl::embark_selected(UObject* menu) {
    // Other Gummi menus share the help renderer. Require the live WM01
    // owner, main-menu mode, and Embark's native command index.
    if (!embark_help.hook || !is_uobject_valid_fast(menu) ||
        menu->GetClassPrivate()->GetName()!=L"TresUIP_MenuGummiShip" ||
        world_name()!=L"World /Game/Levels/wm/wm_01/wm_01.wm_01") return false;
    auto* ui=object_field(object_field(world(),L"OwningGameInstance"),L"m_UIManager");
    if (object_field(object_field(ui,L"m_ActiveHUD"),L"m_pGummiMenu")!=menu) return false;
    auto* bytes=reinterpret_cast<const uint8_t*>(menu);
    return *reinterpret_cast<const int32_t*>(bytes+0x3d8)==0 &&
        *reinterpret_cast<const int32_t*>(bytes+0x3c8)==0;
}

UObject* RuntimeSpaceService::Impl::embark_help_field(UObject* menu) {
    // The native renderer's exact path is message.messageObj.txHelpText.
    // Resolve from its reflected owner; never retain transient GFx wrappers.
    auto* message=object_field(menu,L"m_pMessage");
    auto* body=message?try_gfx_get_member_value_object(gfx,message,L"messageObj"):nullptr;
    return body?try_gfx_get_member_value_object(gfx,body,L"txHelpText"):nullptr;
}

void RuntimeSpaceService::Impl::embark_help_hook(UObject* menu,FString* message) {
    active->render_embark_help(menu,message,active->embark_help.original);
}

void RuntimeSpaceService::Impl::embark_focus_help_hook(UObject* menu,FString* message) {
    active->render_embark_help(menu,message,active->embark_focus_help.original);
}

bool RuntimeSpaceService::Impl::embark_steam_focus_help_hook(UObject* menu,void* event) {
    active->restore_selector_help(menu);
    return reinterpret_cast<bool(*)(UObject*,void*)>(active->embark_focus_help.original)(menu,event);
}

void RuntimeSpaceService::Impl::render_embark_help(UObject* menu,FString* message,uint64_t original) {
    restore_selector_help(menu);
    // Restore the native field when leaving the selector, then let KH3 render
    // its original localized main-menu description without an AP override.
    reinterpret_cast<void(*)(UObject*,FString*)>(original)(menu,message);
}

std::wstring RuntimeSpaceService::Impl::read_embark_help(UObject* menu) {
    auto* text=embark_help_field(menu);
    return text?try_gfx_get_member_value_string(gfx,text,L"text").value_or(L""):L"";
}

int RuntimeSpaceService::Impl::selected_space_zone(UObject* manager) const {
    if (!is_uobject_valid_fast(manager)) return -1;
    const auto* bytes = reinterpret_cast<const uint8_t*>(manager);
    const int index = *reinterpret_cast<const int32_t*>(bytes + space::native_profile().overview_region);
    if (index < 0 || index >= 3) return -1;
    const int zone = bytes[space::native_profile().overview_region_ids + index];
    return zone < 3 ? zone : -1;
}

bool RuntimeSpaceService::Impl::locked_embark_selection(UObject* manager) const {
    const int zone = selected_space_zone(manager);
    if (!access.include_space || zone < 0 || can_travel(zone)) return false;
    const auto& profile = space::native_profile();
    const auto* bytes = reinterpret_cast<const uint8_t*>(manager);
    const auto* data = *reinterpret_cast<const uint8_t* const*>(bytes + profile.overview_cursor_data);
    const int count = *reinterpret_cast<const int32_t*>(bytes + profile.overview_cursor_count);
    if (!data || count < 1 || count > max_embark_help_nodes) return false;
    for (int i = 0; i < count; ++i) {
        const auto* row = data + i * profile.cursor_stride;
        // Both idle handlers confirm the focused row at 0x3a. Other
        // icon kinds open details or markers, rather than an embark attempt.
        if (row[0x3a]) return (row[profile.cursor_kind] == 3 || row[profile.cursor_kind] == 4) &&
            !row[profile.cursor_selectable];
    }
    return false;
}

bool RuntimeSpaceService::Impl::note_locked_embark(UObject* manager) {
    if (!locked_embark_selection(manager)) return false;
    embark_denied_until = std::chrono::steady_clock::now() + embark_denied_message_duration;
    ++denied_embark_attempts;
    return true;
}

bool RuntimeSpaceService::Impl::embark_input_hook(void* input, int32_t button) {
    auto& self = *active;
    return self.observe_embark_input(
        reinterpret_cast<bool(*)(void*, int32_t)>(self.embark_input.original)(input, button), button);
}

bool RuntimeSpaceService::Impl::embark_steam_input_hook(void* input, int32_t button, bool filtered) {
    auto& self = *active;
    // Preserve Steam's third argument; its filter affects the native result.
    return self.observe_embark_input(
        reinterpret_cast<bool(*)(void*, int32_t, bool)>(self.embark_input.original)(input, button, filtered), button);
}

bool RuntimeSpaceService::Impl::observe_embark_input(bool pressed, int32_t button) {
    auto* manager = embark_input_manager.Get();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    // A single diagnostic selector dispatch can supply a semantic button. The
    // ordinary handler still owns selection, rejection, cancellation and travel.
    if (manager && embark_test_button == button) pressed = true;
#endif
    if (pressed && button == 3 && manager) note_locked_embark(manager);
    return pressed;
}

void RuntimeSpaceService::Impl::embark_selector_hook(UObject* manager) {
    auto& self = *active;
    const bool owned = is_uobject_valid_fast(manager) &&
        self.current_world == L"World /Game/Levels/wm/wm_01/wm_01.wm_01" &&
        static_cast<RC::Unreal::AActor*>(manager)->GetWorld() == self.presentation_world.Get();
    self.embark_input_manager = owned ? manager : nullptr;
    reinterpret_cast<void(*)(UObject*)>(self.embark_selector.original)(manager);
    self.embark_input_manager = nullptr;
    if (owned) self.refresh_selector_help(manager);
}

void RuntimeSpaceService::Impl::restore_selector_help(UObject* menu) {
    if (!selector_help_visible || selector_menu.Get() != menu) return;
    auto* component = embark_help_field(menu);
    for (const auto* name : line_names)
        if (auto* line = try_gfx_get_member_value_object(gfx, component, name))
            try_gfx_set_member_value_bool(gfx, line, L"visible", false);
    if (auto* field = try_gfx_get_member_value_object(gfx, component, L"textField"))
        try_gfx_set_member_value_bool(gfx, field, L"visible", true);
    if (component && try_gfx_get_member_value_string(gfx, component, L"text") == selector_help_text)
        try_gfx_set_member_value_string(gfx, component, L"text", stock_selector_help);
    selector_help_visible = false;
    selector_help_text.clear();
    selector_help_zone = -1;
}

void RuntimeSpaceService::Impl::refresh_selector_help(UObject* manager) {
    if (!embark_selector.hook || !is_uobject_valid_fast(manager)) return;
    const auto* bytes = reinterpret_cast<const uint8_t*>(manager);
    const int state = *reinterpret_cast<const int32_t*>(bytes + space::native_profile().overview_state);
    if (state != space::native_profile().overview_idle_state || !access.include_space) {
        if (auto* menu = selector_menu.Get()) restore_selector_help(menu);
        return;
    }
    const int zone = selected_space_zone(manager);
    if (zone < 0) return;
    if (zone != selector_help_zone) embark_denied_until = {};
    const auto blueprint = std::wstring{space::zone_blueprint(zone)};
    const auto stock = ships.stock.find(blueprint);
    const auto guidance = space::zone_access_guidance(zone, access.access_ready && ships.inventory_verified,
        access.items.contains(blueprint), stock != ships.stock.end() && stock->second > 0,
        ships.completed_worlds, std::chrono::steady_clock::now() < embark_denied_until);
    // The native selector runs every frame; resolve GFx and write only when
    // its displayed state changes. No per-frame UObject scan or UI allocation.
    if (selector_help_visible && selector_menu.Get() && selector_help_text == guidance) return;
    auto* menu = object_field(object_field(manager, L"m_pGummiHUD"), L"m_pGummiMenu");
    if (!is_uobject_valid_fast(menu) || *reinterpret_cast<const int32_t*>(reinterpret_cast<const uint8_t*>(menu) + 0x3d8) != 9) return;
    auto* component = embark_help_field(menu);
    const auto current = component ? try_gfx_get_member_value_string(gfx, component, L"text") : std::nullopt;
    if (!current) return;
    auto* field = try_gfx_get_member_value_object(gfx, component, L"textField");
    if (!selector_help_visible || selector_menu.Get() != menu) {
        stock_selector_help = *current;
        selector_menu = menu;
        selector_help_visible = true;
    }
    // KH3's stock 101-pixel help field already supports multiline text and
    // centers it vertically. Keep the native animation, typography and layout.
    try_gfx_set_member_value_string(gfx, component, L"text", guidance);
    // KH3 retranslates help text when prompts/regions change and discards
    // embedded line breaks. Separate single-line fields make the layout
    // independent of those refreshes, without polling or timed repainting.
    if (field) {
        const auto split = guidance.find(L'\n');
        const std::array lines{guidance.substr(0, split), guidance.substr(split + 1)};
        std::array<UObject*, 2> fields{};
        std::array<double, 2> heights{};
        bool created = false;
        for (size_t i = 0; i < fields.size(); ++i) {
            fields[i] = help_line(object_field(menu, L"m_pMenuCmdSet"), component, line_names[i], created);
            if (!fields[i]) return;
        }
        // Let the authored clones finish FRAME_CONSTRUCTED before applying
        // content. The next selector dispatch completes this once-only setup.
        if (created) return;
        auto* format = try_gfx_invoke_no_args_object(gfx, field, L"getTextFormat");
        if (!format) return;
        const auto width = try_gfx_get_member_value_number(gfx, field, L"width").value_or(0);
        const auto height = try_gfx_get_member_value_number(gfx, field, L"height").value_or(0);
        double text_width = 0;
        for (size_t i = 0; i < fields.size(); ++i) {
            try_gfx_set_member_value_object(gfx, fields[i], L"defaultTextFormat", format);
            for (const auto* property : {L"multiline", L"wordWrap", L"selectable", L"mouseEnabled"})
                try_gfx_set_member_value_bool(gfx, fields[i], property, false);
            try_gfx_set_member_value_number(gfx, fields[i], L"width", width);
            try_gfx_set_member_value_number(gfx, fields[i], L"height", height);
            try_gfx_set_member_value_string(gfx, fields[i], L"text", lines[i]);
            heights[i] = try_gfx_get_member_value_number(gfx, fields[i], L"textHeight").value_or(0);
            text_width = std::max(text_width, try_gfx_get_member_value_number(gfx, fields[i], L"textWidth").value_or(0));
        }
        double y = (height - heights[0] - heights[1] - 11.0) * 0.5;
        for (size_t i = 0; i < fields.size(); ++i) {
            try_gfx_set_member_value_number(gfx, fields[i], L"x", (width - text_width) * 0.5);
            try_gfx_set_member_value_number(gfx, fields[i], L"y", y);
            try_gfx_set_member_value_bool(gfx, fields[i], L"visible", true);
            y += heights[i] + 11.0;
        }
        try_gfx_set_member_value_bool(gfx, field, L"visible", false);
    }
    selector_help_text = guidance;
    selector_help_zone = zone;
}
}
