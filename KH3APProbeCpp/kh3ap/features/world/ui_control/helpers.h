#pragma once
#include "layout.h"
#include <Unreal/NameTypes.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace RC::Unreal
{
    class UObject;
    class UFunction;
} // namespace RC::Unreal
namespace kh3ap::ui_control_detail
{
    using RC::Unreal::FName;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;
    struct UiProbeCall
    {
        RC::Unreal::UObject* owner;
        RC::Unreal::UFunction* function;
        std::vector<std::byte> params;
        UiProbeCall(RC::Unreal::UObject* object, const wchar_t* name);
        void run();
    };

    void cutscene_engine_pause(UObject* pawn, bool paused);
    void remote_texture_screenshot(UObject* pawn);
    int flight_hp(UObject* pawn);
    void damage_flight(UObject* pawn, UObject* controller);
    int64_t unix_ms();
    UObject* unique(const wchar_t* name);
    std::wstring command_pawn_identity(UObject* pawn);
    std::string command_snapshot(UObject* manager, UObject* pawn);
    std::optional<int64_t> selection_game_flag(const wchar_t* name);
    std::string viewport_fixture(UObject* pawn, const std::optional<std::array<int, 3>>& requested,
                                 bool apply);
    UObject* frozen_garden_portal(const wchar_t* story_flag = L"gameflow_FZ");
    UObject* frozen_menu_widget();
    std::vector<FName> frozen_menu_rows(UObject* widget);
    void select_frozen_menu_row(UObject* widget, const wchar_t* row_name = L"FZ_SAVE08",
                                const wchar_t* player_start = L"fz_02_Lv_AP_Save_01");
    int sora_selection_index(std::wstring_view pawn_name);
    std::string workshop_probe(uintptr_t base, bool craft, bool deposit = false);
} // namespace kh3ap::ui_control_detail
