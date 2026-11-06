#include <memory>
#include <utility>
#include <algorithm>
#include <limits>
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include "kh3ap/features/world/space/space_rewards.h"
#include "kh3ap/features/world/space/space_battle_markers.h"
#include "kh3ap/features/world/space/space_profile.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/features/world/space/space_sphere_policy.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UEnum.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FEnumProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Windows.h>
#include <array>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <sstream>

using namespace RC;
using namespace RC::Unreal;
namespace kh3ap
{
    using space::native_profile;
    namespace
    {
        constexpr auto max_battle_compensation_entries = 3;
        constexpr auto mission_display_list_offset = 0x3e8;
        constexpr auto mission_display_title_offset = 8;
        constexpr auto mission_display_objective_offset = 24;
        constexpr auto boss_battle_reward_count = 3;
        constexpr auto battle_reward_result_reason = 4;
        constexpr std::array battle_results_with_rewards{2, 4};

        constexpr auto max_space_reward_table_rows = 512;
        constexpr auto max_space_reward_table_capacity = 1024;
        constexpr auto max_space_reward_entries = 512;
        constexpr auto max_space_reward_capacity = 4096;
        constexpr auto max_space_mission_list_capacity = 256;
        constexpr uint8_t completed_sphere_state = 3;
        constexpr uint8_t unknown_sphere_state = 255;
        constexpr uint8_t sphere_world_id_count = 9;

        constexpr auto mission_table =
            L"/Game/GumiShip/DataTable/System/GumiMissionDataTable.GumiMissionDataTable";
        constexpr auto battle_table =
            L"/Game/GumiShip/DataTable/LevelEdit/Mission/"
            L"GumiBattleMissionIndividalDataTable.GumiBattleMissionIndividalDataTable";
        constexpr auto prize_table =
            L"/Game/GumiShip/DataTable/LevelEdit/Mission/"
            L"GumiBattleMissionItemDataTable.GumiBattleMissionItemDataTable";
        constexpr auto sphere_hook_path =
            L"/Script/TresGame.TresGumiShipGimmickTreasure:_ChangeState";
        constexpr std::array<const wchar_t*, space::boss_mission_count> bosses{
            L"gm_01_Battle_10", L"gm_01_Battle_11", L"gm_01_Battle_12", L"gm_02_Battle_14",
            L"gm_02_Battle_15", L"gm_02_Battle_16", L"gm_02_Battle_17", L"gm_03_Battle_01",
            L"gm_03_Battle_04", L"gm_03_Battle_05", L"gm_03_Battle_06"};
        constexpr std::array<const wchar_t*, 7> prize_fields{
            L"m_GumiParts",  L"m_GumiMaterial", L"m_GumiPattern", L"m_GumiSticker",
            L"m_GumiShipBP", L"m_GumiEtc",      L"m_Material"};
        struct Array
        {
            uint8_t* data;
            int32_t num, max;
        };
        struct Row
        {
            FName key;
            uint8_t* data;
            uint64_t padding;
        };
        static_assert(sizeof(Row) == 24 && sizeof(Array) == 16);
        static_assert(sizeof(FString) == 16);
        UObject* loaded(const wchar_t* path)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const RuntimeHotPathProfileScope profile{std::wstring{L"space_rewards:lookup:"} + path};
#endif
            auto* value = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path);
            return is_uobject_valid_fast(value) ? value : nullptr;
        }
        UObject* loaded_asset(const wchar_t* path)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            const RuntimeHotPathProfileScope profile{std::wstring{L"space_rewards:lookup:"} + path};
#endif
            // These space tables are normally absent in Sora worlds. UE4SS's
            // untyped lookup scans every UObject on a miss; use the engine's
            // nonloading object hash, including when a table arrives after a miss.
            return find_loaded_runtime_asset(path);
        }
        std::vector<Row> rows(UObject* table)
        {
            if (!is_uobject_valid_fast(table))
            {
                return {};
            }
            auto* p = table->GetPropertyByNameInChain(L"RowStruct");
            if (!p || p->GetOffset_Internal() != native_profile().table_row_struct ||
                p->GetElementSize() != sizeof(UObject*))
            {
                return {};
            }
            auto* bytes = reinterpret_cast<uint8_t*>(table);
            auto header = *reinterpret_cast<Array*>(bytes + native_profile().table_rows);
            // Only immutable, dense cooked tables. A sparse or rebuilt table is refused.
            if (!header.data || header.num < 1 || header.num > max_space_reward_table_rows ||
                header.max < header.num || header.max > max_space_reward_table_capacity ||
                *reinterpret_cast<int32_t*>(bytes + native_profile().table_free_count) != 0)
            {
                return {};
            }
            auto* data = reinterpret_cast<Row*>(header.data);
            std::vector<Row> result(data, data + header.num);
            for (const auto& row : result)
            {
                if (!row.data)
                {
                    return {};
                }
            }
            return result;
        }
        UScriptStruct* row_struct(UObject* table)
        {
            auto* value = try_read_reflected_object_property_value(table, L"RowStruct");
            return is_uobject_valid_fast(value) ? static_cast<UScriptStruct*>(value) : nullptr;
        }
        std::wstring numbered(const wchar_t* prefix, int id)
        {
            return std::wstring(prefix) + (id < 10 ? L"0" : L"") + std::to_wstring(id);
        }
    } // namespace

    struct RuntimeSpaceRewards::Impl
    {
        using End = void (*)(UObject*, uint8_t, bool);
        using Thunk = void (*)(UObject*, FFrame&, void*);
        inline static Impl* self{};
        uint64_t original_end{};
        std::unique_ptr<PLH::x64Detour> end_hook;
        uint64_t original_sphere_open{};
        std::unique_ptr<PLH::x64Detour> sphere_open_hook;
        uint64_t original_mission_list{};
        std::unique_ptr<PLH::x64Detour> mission_list_hook;
        Thunk original_sphere{};
        bool enabled{}, installed{}, dirty{};
        bool include_grindy_missions{}, include_superbosses{};
        bool recovery_pending{true}, publication_pending{};
        FWeakObjectPtr patched_mission_table, patched_prize_table;
        uint64_t recovery_scans{}, table_lookups{};
        std::filesystem::path bridge;
        std::wstring seed;
        int64_t slot{};
        std::set<std::wstring> completed, allowed;
        uint64_t completion_revision{};
        RuntimeSpaceBattleMarkers markers{
            [this](std::wstring_view battle)
            {
                if (!enabled)
                {
                    return false;
                }
                for (const auto& key : completed)
                {
                    if (space::battle_first_reward_matches(battle, key))
                    {
                        return true;
                    }
                }
                return false;
            },
            [this]
            {
                return completion_revision;
            }};
        // A restore record owns no UObject pointer. Its address is used only while
        // the owning immutable table/actor and original array allocation still match.
        struct Patch
        {
            FWeakObjectPtr owner;
            uint8_t* address;
            std::vector<uint8_t> bytes;
            Array* array{};
            uint8_t* array_data{};
            int32_t array_num{};
        };
        std::vector<Patch> patches;
        std::vector<FWeakObjectPtr> patched_owners;
        FWeakObjectPtr weak_owner(UObject* owner)
        {
            FWeakObjectPtr weak{};
            if (!installed || !is_uobject_valid_fast(owner))
            {
                return weak;
            }
            // UE4SS assignment only copies an existing serial. Cooked reward
            // tables start with serial zero; use KH3's normal weak assignment to
            // allocate one before retaining restore records across frames.
            using Assign = void (*)(FWeakObjectPtr*, UObject*);
            auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            reinterpret_cast<Assign>(base + native_profile().weak_assign_rva)(&weak, owner);
            return weak;
        }
        ~Impl()
        {
            restore();
            if (mission_list_hook)
            {
                mission_list_hook->unHook();
            }
            if (sphere_open_hook)
            {
                sphere_open_hook->unHook();
            }
            if (end_hook)
            {
                end_hook->unHook();
            }
            auto* fn = static_cast<UFunction*>(loaded(sphere_hook_path));
            if (original_sphere && fn && fn->GetFuncPtr() == sphere_thunk)
            {
                fn->SetFuncPtr(original_sphere);
            }
            if (self == this)
            {
                self = nullptr;
            }
        }
        void restore()
        {
            for (auto& p : patches)
            {
                if (is_uobject_valid_fast(p.owner.Get()) &&
                    (!p.array || (p.array->data == p.array_data && p.array->num == p.array_num)))
                {
                    std::memcpy(p.address, p.bytes.data(), p.bytes.size());
                }
            }
            patches.clear();
            patched_owners.clear();
            patched_mission_table = nullptr;
            patched_prize_table = nullptr;
        }
        bool seen(UObject* owner) const
        {
            for (const auto& p : patched_owners)
            {
                if (p.Get() == owner)
                {
                    return true;
                }
            }
            return false;
        }
        bool remember(UObject* owner, uint8_t* address, size_t size, Array* array = nullptr)
        {
            if (!size)
            {
                return false;
            }
            auto weak = weak_owner(owner);
            if (weak.Get() != owner)
            {
                return false;
            }
            Patch patch{weak,
                        address,
                        std::vector<uint8_t>(address, address + size),
                        array,
                        array ? array->data : nullptr,
                        array ? array->num : 0};
            patches.push_back(std::move(patch));
            return true;
        }
        bool zero(UObject* owner, uint8_t* address, size_t size, Array* array = nullptr)
        {
            if (!remember(owner, address, size, array))
            {
                return false;
            }
            std::memset(address, 0, size);
            return true;
        }
        void clear_array(UObject* owner, uint8_t* data, FProperty* p)
        {
            if (!p || !p->IsA<FArrayProperty>() || p->GetElementSize() != sizeof(Array))
            {
                return;
            }
            auto* inner = static_cast<FArrayProperty*>(p)->GetInner();
            if (!inner || inner->GetElementSize() != 1)
            {
                return;
            }
            auto* array = p->ContainerPtrToValuePtr<Array>(data);
            if (array->num < 0 || array->num > max_space_reward_entries ||
                array->max < array->num || array->max > max_space_reward_capacity ||
                (!array->data && array->num))
            {
                return;
            }
            // Empty the POD reward list without releasing its allocation. Unlike
            // zero-valued entries, a zero count cannot spawn an invalid item prize.
            if (!array->num)
            {
                return;
            }
            if (zero(owner, reinterpret_cast<uint8_t*>(&array->num), sizeof(array->num), array))
            {
                patches.back().array_num = 0;
            }
        }
        void clear_prize(UObject* owner, uint8_t* data, UScriptStruct* structure)
        {
            if (!structure)
            {
                return;
            }
            for (auto* name : prize_fields)
            {
                clear_array(owner, data, find_struct_field_by_name(structure, name));
            }
        }
        void suppress_mission_table()
        {
            if (!enabled)
            {
                return;
            }
            if (is_uobject_valid_fast(patched_mission_table.Get()))
            {
                return;
            }
            ++table_lookups;
            if (auto* table = loaded_asset(mission_table); table && !seen(table))
            {
                auto* structure = row_struct(table);
                auto data = rows(table);
                if (structure && data.size() == space::mission_count)
                {
                    auto* release = find_struct_field_by_name(structure, L"m_nReleaseCondition");
                    auto* use = find_struct_field_by_name(structure, L"m_isUse");
                    auto* hide_count = find_struct_field_by_name(structure, L"m_isHideClearNum");
                    auto* release_enum = static_cast<UEnum*>(
                        loaded(L"/Script/TresGame.ETresGummiMissionReleaseCondition"));
                    std::optional<uint8_t> first_release;
                    if (release_enum)
                    {
                        for (const auto& [name, value] : release_enum->ForEachName())
                        {
                            if (name.ToString() ==
                                    L"ETresGummiMissionReleaseCondition::RELEASE_FIRST" &&
                                value >= 0 && value <= std::numeric_limits<uint8_t>::max())
                            {
                                first_release = static_cast<uint8_t>(value);
                            }
                        }
                    }
                    if (!release || !release->IsA<FEnumProperty>() ||
                        release->GetElementSize() != 1 || !first_release || !use ||
                        !use->IsA<FBoolProperty>() || use->GetElementSize() != 1 ||
                        use->GetArrayDim() != 1 || !hide_count ||
                        !hide_count->IsA<FBoolProperty>() || hide_count->GetElementSize() != 1 ||
                        hide_count->GetArrayDim() != 1)
                    {
                        return;
                    }
                    auto* use_bool = static_cast<FBoolProperty*>(use);
                    auto* hide_count_bool = static_cast<FBoolProperty*>(hide_count);
                    // Validate row identities before changing any shared table data.
                    std::set<unsigned> ids;
                    for (const auto& row : data)
                    {
                        const auto id = space::mission_row_id(row.key.ToString());
                        if (!id || !ids.insert(*id).second)
                        {
                            return;
                        }
                    }
                    size_t revealed{}, hidden{};
                    for (const auto& row : data)
                    {
                        // AP zones unlock independently of vanilla story order.
                        // Reveal included missions; optional groups use the native
                        // list enable flag. Keep thresholds and saved flags intact.
                        const auto id = *space::mission_row_id(row.key.ToString());
                        const bool visible = space::mission_list_enabled(
                            id, include_grindy_missions, include_superbosses);
                        auto* use_byte = use->ContainerPtrToValuePtr<uint8_t>(row.data) +
                                         use_bool->GetByteOffset();
                        if (use_bool->GetPropertyValueInContainer(row.data) != visible &&
                            remember(table, use_byte, 1))
                        {
                            use_bool->SetPropertyValueInContainer(row.data, visible);
                        }
                        if (!visible)
                        {
                            ++hidden;
                        }
                        auto* count_byte = hide_count->ContainerPtrToValuePtr<uint8_t>(row.data) +
                                           hide_count_bool->GetByteOffset();
                        // Blast Off has no numeric threshold (its native row is 0).
                        if (visible && id != 0 &&
                            hide_count_bool->GetPropertyValueInContainer(row.data) &&
                            remember(table, count_byte, 1))
                        {
                            hide_count_bool->SetPropertyValueInContainer(row.data, false);
                        }
                        auto* condition = row.data + release->GetOffset_Internal();
                        if (*condition != *first_release && zero(table, condition, 1))
                        {
                            *condition = *first_release;
                            ++revealed;
                        }
                        for (auto* name : {L"m_nReward1", L"m_nReward1Num"})
                        {
                            auto* p = find_struct_field_by_name(structure, name);
                            const int expected = std::wstring_view(name) == L"m_nReward1" ? 1 : 2;
                            if (p && p->GetElementSize() == expected)
                            {
                                zero(table, row.data + p->GetOffset_Internal(), expected);
                            }
                        }
                        for (auto* name : {L"m_nReward2", L"m_nReward3", L"m_nReward4",
                                           L"m_nReward5", L"m_nReward6"})
                        {
                            clear_array(table, row.data,
                                        find_struct_field_by_name(structure, name));
                        }
                    }
                    patched_owners.push_back(weak_owner(table));
                    patched_mission_table = patched_owners.back();
                    publication_pending = true;
                    Output::send<LogLevel::Warning>(
                        L"[KH3APProbeCpp] Space mission list configured rows={} hidden={} "
                        L"visibility_conditions_changed={}\n",
                        data.size(), hidden, revealed);
                }
            }
        }
        void suppress_prize_table()
        {
            if (!enabled || is_uobject_valid_fast(patched_prize_table.Get()))
            {
                return;
            }
            ++table_lookups;
            if (auto* table = loaded_asset(prize_table); table && !seen(table))
            {
                auto* structure = row_struct(table);
                auto data = rows(table);
                if (structure && !data.empty())
                {
                    for (const auto& row : data)
                    {
                        if (!row.key.ToString().starts_with(L"Item_gm_0"))
                        {
                            continue;
                        }
                        for (auto* name : {L"m_Compensations", L"m_CompensationForFirstTime"})
                        {
                            auto* p = find_struct_field_by_name(structure, name);
                            if (!p || !p->IsA<FStructProperty>() || p->GetArrayDim() < 1 ||
                                p->GetArrayDim() > max_battle_compensation_entries)
                            {
                                continue;
                            }
                            auto* child = static_cast<FStructProperty*>(p)->GetStruct();
                            for (int i = 0; i < p->GetArrayDim(); ++i)
                            {
                                clear_prize(table,
                                            row.data + p->GetOffset_Internal() +
                                                i * p->GetElementSize(),
                                            child);
                            }
                        }
                    }
                    patched_owners.push_back(weak_owner(table));
                    patched_prize_table = patched_owners.back();
                    publication_pending = true;
                }
            }
        }
        void reveal_mission_text(UObject* menu)
        {
            // The normal builder hides titles until completion. Replace only
            // its display strings after construction and before GFx consumes them.
            // The actual saved flags, checkbox, progress and reward logic stay native.
            const auto& profile = native_profile();
            if (!enabled || !is_uobject_valid_fast(menu))
            {
                return;
            }
            auto* cls = static_cast<UClass*>(loaded(L"/Script/TresGame.TresUIP_MenuGummiShip"));
            auto* table = patched_mission_table.Get();
            if (!is_uobject_valid_fast(cls) || !menu->IsA(cls) || !is_uobject_valid_fast(table))
            {
                return;
            }
            auto* structure = row_struct(table);
            if (!structure)
            {
                return;
            }
            auto* title = find_struct_field_by_name(structure, L"m_nTitleTextID");
            auto* objective = find_struct_field_by_name(structure, L"m_nAfterConditionTextID");
            auto* threshold = find_struct_field_by_name(structure, L"m_nMissionClearNum");
            auto* display = find_struct_field_by_name(structure, L"m_nDispIndex");
            if (!title || !objective || !threshold || !display ||
                title->GetClass().GetName() != L"StrProperty" ||
                title->GetElementSize() != sizeof(FString) ||
                objective->GetClass().GetName() != L"StrProperty" ||
                objective->GetElementSize() != sizeof(FString) ||
                threshold->GetClass().GetName() != L"UInt32Property" ||
                threshold->GetElementSize() != sizeof(uint32_t) ||
                display->GetClass().GetName() != L"IntProperty" ||
                display->GetElementSize() != sizeof(int32_t))
            {
                return;
            }
            auto list = *reinterpret_cast<Array*>(reinterpret_cast<uint8_t*>(menu) +
                                                  mission_display_list_offset);
            if (!list.data || list.num < 1 ||
                list.num > static_cast<int32_t>(space::mission_count) || list.max < list.num ||
                list.max > max_space_mission_list_capacity)
            {
                return;
            }
            std::map<int32_t, uint8_t*> by_display;
            for (const auto& row : rows(table))
            {
                if (!by_display
                         .emplace(*display->ContainerPtrToValuePtr<int32_t>(row.data), row.data)
                         .second)
                {
                    return;
                }
            }
            if (by_display.size() != space::mission_count)
            {
                return;
            }
            auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            using Lookup = const FString* (*)(const FString*, const FString*);
            // The formatter's first argument owns a reusable FText, not an FString.
            // Borrow the current UIManager buffer used by the native builder.
            auto* game = try_read_reflected_object_property_value(menu, L"m_pGameInstance");
            auto* game_class = static_cast<UClass*>(loaded(L"/Script/TresGame.TresGameInstance"));
            if (!is_uobject_valid_fast(game) || !is_uobject_valid_fast(game_class) ||
                !game->IsA(game_class))
            {
                return;
            }
            auto* ui = try_read_reflected_object_property_value(game, L"m_UIManager");
            auto* ui_class = static_cast<UClass*>(loaded(L"/Script/TresGame.TresUIManager"));
            if (!is_uobject_valid_fast(ui) || !is_uobject_valid_fast(ui_class) ||
                !ui->IsA(ui_class) ||
                try_read_reflected_object_property_value(menu, L"m_pUIManager") != ui)
            {
                return;
            }
            auto* formatter = reinterpret_cast<uint8_t*>(ui) + profile.text_formatter_offset;
            using Format = const FString* (*)(void*, const FString*, const FString*, uint32_t);
            using SteamFormat =
                const FString* (*)(void*, const FString*, const FString*, uint32_t, void*);
            using Assign = FString* (*)(FString*, const FString*);
            const auto* text_namespace =
                reinterpret_cast<const FString*>(base + profile.mission_text_namespace);
            const auto lookup = reinterpret_cast<Lookup>(base + profile.text_lookup_rva);
            const auto format = reinterpret_cast<Format>(base + profile.text_format_number_rva);
            const auto steam_format =
                reinterpret_cast<SteamFormat>(base + profile.text_format_number_rva);
            const auto assign = reinterpret_cast<Assign>(base + profile.string_assign_rva);
            size_t revealed{};
            for (int i = 0; i < list.num; ++i)
            {
                auto* entry = list.data + i * space::mission_display_record_stride;
                auto found = by_display.find(*reinterpret_cast<int32_t*>(entry));
                if (found == by_display.end())
                {
                    continue;
                }
                auto* row = found->second;
                const auto* name =
                    lookup(text_namespace, title->ContainerPtrToValuePtr<FString>(row));
                if (name)
                {
                    assign(reinterpret_cast<FString*>(entry + mission_display_title_offset), name);
                }
                const auto* objective_id = objective->ContainerPtrToValuePtr<FString>(row);
                const auto clear_num = *threshold->ContainerPtrToValuePtr<uint32_t>(row);
                const auto* body =
                    profile.steam
                        ? steam_format(formatter, text_namespace, objective_id, clear_num, nullptr)
                        : format(formatter, text_namespace, objective_id, clear_num);
                if (body)
                {
                    assign(reinterpret_cast<FString*>(entry + mission_display_objective_offset),
                           body);
                }
                ++revealed;
            }
            Output::send<LogLevel::Verbose>(
                L"[KH3APProbeCpp] Space mission display text revealed rows={}\n", revealed);
        }
        static bool mission_list_thunk(UObject* menu)
        {
            if (self->enabled)
            {
                self->suppress_mission_table();
            }
            const bool result =
                reinterpret_cast<bool (*)(UObject*)>(self->original_mission_list)(menu);
            self->reveal_mission_text(menu);
            return result;
        }
        void suppress_sphere(UObject* actor)
        {
            if (!enabled || !is_uobject_valid_fast(actor) || seen(actor))
            {
                return;
            }
            auto* p = actor->GetPropertyByNameInChain(L"m_DropPrizeSettingData");
            if (!p || !p->IsA<FStructProperty>())
            {
                return;
            }
            clear_prize(actor, reinterpret_cast<uint8_t*>(actor) + p->GetOffset_Internal(),
                        static_cast<FStructProperty*>(p)->GetStruct());
            patched_owners.push_back(weak_owner(actor));
            publication_pending = true;
        }
        std::string snapshot() const
        {
            std::ostringstream out;
            out << "{\"schema\":\"kh3ap.space_checks.v1\",\"source\":\"native_space_completion\","
                   "\"seed_name\":\""
                << json_escape(seed) << "\",\"slot\":" << slot << ",\"completed\":[";
            bool first = true;
            for (const auto& key : completed)
            {
                if (!first)
                {
                    out << ',';
                }
                first = false;
                out << '"' << json_escape(key) << '"';
            }
            out << "],\"suppressed_owners\":" << patched_owners.size()
                << ",\"recovery_scans\":" << recovery_scans
                << ",\"table_lookups\":" << table_lookups
                << ",\"suppressed_reward_fields\":" << patches.size() << "}\n";
            return out.str();
        }
        bool persist()
        {
            const auto text = snapshot();
            // Randomizer-owned disk checkpoint survives both game and client exits.
            if (!write_text_file_atomic_verified(bridge / L"space_progress.json", text))
            {
                return false;
            }
            dirty = false;
            publication_pending = !write_text_file_atomic_verified(
                bridge / channels::runtime_space_checks_file, text);
            return !publication_pending;
        }
        void record(std::wstring key)
        {
            if (enabled && allowed.contains(key) && completed.insert(std::move(key)).second)
            {
                dirty = true;
                ++completion_revision;
            }
        }
        std::wstring canonical_battle(const std::wstring& observed) const
        {
            for (const auto& key : allowed)
            {
                if (space::battle_first_reward_matches(observed, key))
                {
                    return key.substr(0, key.size() - std::wstring_view{L"_1"}.size());
                }
            }
            return {};
        }
        void battle(const std::wstring& observed)
        {
            const auto name = canonical_battle(observed);
            if (name.empty())
            {
                return;
            }
            bool boss = std::find(bosses.begin(), bosses.end(), name) != bosses.end();
            for (int i = 1; i <= (boss ? boss_battle_reward_count : 1); ++i)
            {
                record(name + L"_" + std::to_wstring(i));
            }
        }
        void mission(uint8_t id)
        {
            if (id >= space::mission_count || !enabled)
            {
                return;
            }
            if (id >= space::first_boss_mission_id && id <= space::last_boss_mission_id)
            {
                battle(bosses[id - space::first_boss_mission_id]);
            }
            else
            {
                record(numbered(L"mission_", id));
            }
        }
        static std::wstring starter_battle(UObject* manager)
        {
            auto* fn = is_uobject_valid_fast(manager)
                           ? manager->GetFunctionByNameInChain(L"GetOwner")
                           : nullptr;
            if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() != sizeof(UObject*))
            {
                return {};
            }
            UObject* owner = nullptr;
            manager->ProcessEvent(fn, &owner);
            auto* cls = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresGumiShipGameMode");
            if (!is_uobject_valid_fast(owner) || !is_uobject_valid_fast(cls) || !owner->IsA(cls) ||
                try_read_reflected_object_property_value(owner, L"m_pMissionMgr") != manager)
            {
                return {};
            }
            auto* starter =
                try_read_reflected_object_property_value(owner, L"m_pCurrentEnemyBattleStarter");
            if (!is_uobject_valid_fast(starter))
            {
                return {};
            }
            const auto path = starter->GetPathName();
            return std::wstring(space::battle_from_starter_path(path));
        }
        static void end_thunk(UObject* manager, uint8_t reason, bool show_result)
        {
            auto& s = *self;
            std::wstring name;
            // Native mission data survives the DataTable UObject being unloaded
            // between regions. The live owning game mode still identifies the exact
            // active starter; resolve its cooked battle package against the seed.
            const auto active_battle = starter_battle(manager);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (is_uobject_valid_fast(manager))
            {
                auto* raw = reinterpret_cast<uint8_t*>(manager);
                auto* individual =
                    *reinterpret_cast<uint8_t**>(raw + native_profile().battle_individual);
                const auto candidates = rows(loaded_asset(battle_table));
                std::wstring observed;
                for (const auto& row : candidates)
                {
                    if (row.data == individual)
                    {
                        observed = row.key.ToString();
                        break;
                    }
                }
                std::ostringstream event;
                event << "{\"at\":\"" << now_iso8601_utc()
                      << "\",\"process_id\":" << GetCurrentProcessId()
                      << ",\"reason\":" << static_cast<int>(reason)
                      << ",\"show_result\":" << (show_result ? "true" : "false")
                      << ",\"state\":" << static_cast<int>(raw[native_profile().battle_result])
                      << ",\"enabled\":" << (s.enabled ? "true" : "false") << ",\"row\":\""
                      << json_escape(observed) << "\",\"table_rows\":" << candidates.size()
                      << ",\"allowed_first\":"
                      << (s.allowed.contains(observed + L"_1") ? "true" : "false")
                      << ",\"starter_battle\":\"" << json_escape(active_battle)
                      << "\",\"canonical_battle\":\""
                      << json_escape(
                             s.canonical_battle(observed.empty() ? active_battle : observed))
                      << "\"}\n";
                append_text_file(s.bridge / L"runtime_space_battle_ends.jsonl", event.str());
            }
#endif
            if (s.enabled && reason == battle_reward_result_reason && show_result &&
                is_uobject_valid_fast(manager))
            {
                s.suppress_prize_table();
                auto* raw = reinterpret_cast<uint8_t*>(manager);
                if (std::find(
                        battle_results_with_rewards.begin(), battle_results_with_rewards.end(),
                        raw[native_profile().battle_result]) != battle_results_with_rewards.end())
                {
                    auto* individual =
                        *reinterpret_cast<uint8_t**>(raw + native_profile().battle_individual);
                    for (const auto& row : rows(loaded_asset(battle_table)))
                    {
                        if (row.data == individual)
                        {
                            name = row.key.ToString();
                            break;
                        }
                    }
                    if (name.empty())
                    {
                        name = active_battle;
                    }
                }
            }
            reinterpret_cast<End>(s.original_end)(manager, reason, show_result);
            if (!name.empty())
            {
                s.battle(name);
                if (s.dirty)
                {
                    s.persist();
                }
            }
        }
        static void sphere_open_thunk(uint8_t id)
        {
            auto& s = *self;
            reinterpret_cast<void (*)(uint8_t)>(s.original_sphere_open)(id);
            // The setter can decline the write without a live GameInstance. Read
            // its authoritative bits on the next game-thread tick, not its call ID.
            if (s.enabled && space::sphere_completion_notification(id, true))
            {
                s.recovery_pending = true;
            }
        }
        static void sphere_open_steam_thunk(uint8_t id, bool opened)
        {
            auto& s = *self;
            // Steam also clears flags through this function. Preserve the native
            // argument; a reset must neither reopen a sphere nor emit a check.
            reinterpret_cast<void (*)(uint8_t, bool)>(s.original_sphere_open)(id, opened);
            if (s.enabled && space::sphere_completion_notification(id, opened))
            {
                s.recovery_pending = true;
            }
        }
        static void sphere_thunk(UObject* actor, FFrame& stack, void* result)
        {
            auto& s = *self;
            s.suppress_sphere(actor);
            s.original_sphere(actor, stack, result);
            // _ChangeState also runs on failure/close and success can be entered
            // natively without this reflected thunk. Use the durable setter.
            if (native_profile().sphere_open_rva)
            {
                return;
            }
            if (!s.enabled || !is_uobject_valid_fast(actor))
            {
                return;
            }
            auto* id = actor->GetPropertyByNameInChain(L"m_WorldMapID");
            auto* fn = actor->GetFunctionByNameInChain(L"_GetState");
            if (!id || id->GetElementSize() != 1 || !is_uobject_valid_fast(fn) ||
                fn->GetParmsSize() != 1)
            {
                return;
            }
            uint8_t state = unknown_sphere_state;
            actor->ProcessEvent(fn, &state);
            if (state == completed_sphere_state)
            {
                const auto value = *id->ContainerPtrToValuePtr<uint8_t>(actor);
                if (value < sphere_world_id_count)
                {
                    s.record(numbered(L"sphere_", value));
                    if (s.dirty)
                    {
                        s.persist();
                    }
                }
            }
        }
        bool install()
        {
            if (installed)
            {
                return true;
            }
            auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return false;
            }
            auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (!native_profile().supported ||
                pe->FileHeader.TimeDateStamp != native_profile().image_timestamp ||
                pe->OptionalHeader.SizeOfImage != native_profile().image_size ||
                std::memcmp(reinterpret_cast<void*>(base + native_profile().battle_end_rva),
                            native_profile().battle_end_bytes.data(),
                            native_profile().battle_end_bytes.size()) ||
                std::memcmp(reinterpret_cast<void*>(base + native_profile().weak_assign_rva),
                            native_profile().weak_assign_bytes.data(),
                            native_profile().weak_assign_bytes.size()))
            {
                return false;
            }
            auto* fn = static_cast<UFunction*>(loaded(sphere_hook_path));
            if (!fn || !fn->GetFuncPtr() || fn->GetParmsSize() != 1)
            {
                return false;
            }
            if (native_profile().sphere_open_rva &&
                std::memcmp(reinterpret_cast<void*>(base + native_profile().sphere_open_rva),
                            native_profile().sphere_open_bytes.data(),
                            native_profile().sphere_open_bytes.size()))
            {
                return false;
            }
            self = this;
            end_hook = std::make_unique<PLH::x64Detour>(base + native_profile().battle_end_rva,
                                                        reinterpret_cast<uintptr_t>(&end_thunk),
                                                        &original_end);
            if (!end_hook->hook() || !original_end)
            {
                end_hook.reset();
                return false;
            }
            if (native_profile().sphere_open_rva)
            {
                sphere_open_hook = std::make_unique<PLH::x64Detour>(
                    base + native_profile().sphere_open_rva,
                    native_profile().steam ? reinterpret_cast<uintptr_t>(&sphere_open_steam_thunk)
                                           : reinterpret_cast<uintptr_t>(&sphere_open_thunk),
                    &original_sphere_open);
                if (!sphere_open_hook->hook() || !original_sphere_open)
                {
                    sphere_open_hook.reset();
                    end_hook->unHook();
                    end_hook.reset();
                    return false;
                }
            }
            original_sphere = fn->GetFuncPtr();
            fn->SetFuncPtr(sphere_thunk);
            {
                const auto& profile = native_profile();
                const auto matches = [base](uintptr_t rva, const auto& bytes)
                {
                    return std::memcmp(reinterpret_cast<void*>(base + rva), bytes.data(),
                                       bytes.size()) == 0;
                };
                if (matches(profile.mission_list_rva, profile.mission_list_bytes) &&
                    matches(profile.text_lookup_rva, profile.text_lookup_bytes) &&
                    matches(profile.text_format_number_rva, profile.text_format_number_bytes) &&
                    matches(profile.string_assign_rva, profile.string_assign_bytes))
                {
                    mission_list_hook = std::make_unique<PLH::x64Detour>(
                        base + profile.mission_list_rva,
                        reinterpret_cast<uintptr_t>(&mission_list_thunk), &original_mission_list);
                    if (!mission_list_hook->hook() || !original_mission_list)
                    {
                        mission_list_hook.reset();
                    }
                }
                if (!mission_list_hook)
                {
                    Output::send<LogLevel::Warning>(
                        L"[KH3APProbeCpp] Space mission text hook unavailable\n");
                }
            }
            installed = true;
            return true;
        }
    };
    RuntimeSpaceRewards::RuntimeSpaceRewards() : impl_(std::make_unique<Impl>())
    {
    }
    RuntimeSpaceRewards::~RuntimeSpaceRewards() = default;
    bool RuntimeSpaceRewards::active() const
    {
        return impl_->enabled;
    }
    void RuntimeSpaceRewards::configure(const std::filesystem::path& bridge,
                                        const std::wstring& seed, int64_t slot, bool enabled)
    {
        auto& s = *impl_;
        enabled = enabled && !bridge.empty() && !seed.empty() && slot > 0;
        auto data = enabled ? JsonObject::read_seed(bridge / L"slot_data.json") : JsonObject{};
        const bool grindy =
            seed_bool_option(installed_seed_slot_data(), "include_grindy_space_missions");
        const bool superbosses =
            seed_bool_option(installed_seed_slot_data(), "include_space_superbosses");
        if (s.bridge == bridge && s.seed == seed && s.slot == slot && s.enabled == enabled &&
            s.include_grindy_missions == grindy && s.include_superbosses == superbosses)
        {
            return;
        }
        s.restore();
        s.enabled = false;
        s.completed.clear();
        s.allowed.clear();
        s.dirty = false;
        ++s.completion_revision;
        s.recovery_pending = true;
        s.publication_pending = false;
        s.bridge = bridge;
        s.seed = seed;
        s.slot = slot;
        s.include_grindy_missions = grindy;
        s.include_superbosses = superbosses;
        if (!enabled || !s.install())
        {
            return;
        }
        if (!data.valid())
        {
            return;
        }
        for (auto& row : data.object_field("bridge").objects_field("locations"))
        {
            if (row.string_field("type") == L"space")
            {
                s.allowed.insert(row.string_field("source_id").value_or(L""));
            }
        }
        if (!space::supported_reward_count(s.allowed.size()))
        {
            return;
        }
        s.enabled = true;
        if (auto prior = read_text_file(bridge / L"space_progress.json");
            prior && JsonFieldReader::string_field(*prior, "seed_name") == seed &&
            JsonFieldReader::int64_field(*prior, "slot") == slot &&
            JsonFieldReader::string_field(*prior, "schema") == L"kh3ap.space_checks.v1")
        {
            for (auto& key : JsonFieldReader::string_array_field(*prior, "completed"))
            {
                if (s.allowed.contains(key))
                {
                    s.completed.insert(key);
                }
            }
        }
        // Prove that durable check storage is writable before suppressing loot.
        if (!s.persist())
        {
            s.enabled = false;
            return;
        }
        s.markers.install();
        s.suppress_mission_table();
        s.suppress_prize_table();
        Output::send<LogLevel::Warning>(
            L"[KH3APProbeCpp] Space AP enabled seed={} slot={} checks={}\n", seed, slot,
            s.allowed.size());
    }
    void RuntimeSpaceRewards::mission_completed(uint8_t id)
    {
        impl_->mission(id);
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    void RuntimeSpaceRewards::inspect_battle_markers(std::ostream& out) const
    {
        impl_->markers.inspect(out);
    }
#endif
    void RuntimeSpaceRewards::prepare_mission_rewards()
    {
        impl_->suppress_mission_table();
    }
    void RuntimeSpaceRewards::request_recovery()
    {
        impl_->recovery_pending = true;
    }
    bool RuntimeSpaceRewards::pending() const
    {
        return impl_->enabled &&
               (impl_->dirty || impl_->publication_pending || (impl_->recovery_pending));
    }
    void RuntimeSpaceRewards::tick(UObject* world, bool loading_closed)
    {
        const RuntimeHotPathProfileScope profile{L"space_rewards:tick"};
        auto& s = *impl_;
        if (!s.enabled)
        {
            return;
        }
        // Recover persisted flags once after seed setup, world/load readiness, or
        // a native map-ready event. Normal completions arrive through native hooks.
        if (s.recovery_pending && loading_closed)
        {
            auto* game =
                is_uobject_valid_fast(world)
                    ? try_read_reflected_object_property_value(world, L"OwningGameInstance")
                    : nullptr;
            auto* game_class = static_cast<UClass*>(loaded(L"/Script/TresGame.TresGameInstance"));
            if (is_uobject_valid_fast(game) && game_class && game->IsA(game_class))
            {
                s.recovery_pending = false;
                ++s.recovery_scans;
                const auto* bytes = reinterpret_cast<const uint8_t*>(game);
                for (uint8_t id = 0; id < space::mission_count; ++id)
                {
                    if (bytes[native_profile().mission_flags + id] & 1)
                    {
                        s.mission(id);
                    }
                }
                if (native_profile().sphere_open_flags)
                {
                    uint16_t opened{};
                    std::memcpy(&opened, bytes + native_profile().sphere_open_flags,
                                sizeof(opened));
                    for (unsigned id = 0; id < sphere_world_id_count; ++id)
                    {
                        if (space::sphere_opened(opened, id))
                        {
                            s.record(numbered(L"sphere_", id));
                        }
                    }
                }
                s.suppress_mission_table();
                s.suppress_prize_table();
                s.publication_pending = true;
            }
        }
        if (s.dirty)
        {
            s.persist();
        }
        else if (s.publication_pending)
        {
            s.publication_pending = !write_text_file_atomic_verified(
                s.bridge / channels::runtime_space_checks_file, s.snapshot());
        }
    }
} // namespace kh3ap
