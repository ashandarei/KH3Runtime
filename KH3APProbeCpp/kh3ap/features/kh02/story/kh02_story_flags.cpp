#include "kh02_story_flag_bindings.h"
#include "kh3ap/features/kh02/progress/kh02_story_checkpoints.h"
#include "kh02_story_flags.h"
#include "kh3ap/features/kh02/challenges/kh02_challenge_policy.h"
#include "kh02/mirror_host_metadata.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/features/kh02/progress/kh02_progress_policy.h"
#include "kh3ap/features/kh02/progress/kh02_check_policy.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/FProperty.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <cwctype>
#include <DynamicOutput/DynamicOutput.hpp>

namespace kh3ap
{
    using namespace RC::Unreal;

    struct Kh02StoryFlags::Impl
    {
        struct NameLess
        {
            using is_transparent = void;

            bool operator()(std::wstring_view a, std::wstring_view b) const
            {
                return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](wchar_t x, wchar_t y)
                                                    { return std::towlower(x) < std::towlower(y); });
            }
        };

        FWeakObjectPtr world;
        std::map<std::wstring, int, NameLess> flags;
        std::function<void()> changed;
        const std::map<std::wstring, int, NameLess> labels{
            {L"DW_INIT", 0},
            {L"DW_FOREST_EV_dw021", kh02_checkpoint::forest_scene_dw021},
            {L"DW_FOREST_EV_dw022", kh02_checkpoint::forest_scene_dw022},
            {L"DW_FOREST_BT_DARKSIDE01_START", kh02_checkpoint::first_darkside_start},
            {L"DW_FOREST_BT_DARKSIDE01_END", kh02_checkpoint::first_darkside_end},
            {L"DW_FOREST_BT_AREA_F_BATTLE_START", kh02_checkpoint::forest_area_f_start},
            {L"DW_FOREST_BT_AREA_F_BATTLE_END", kh02_checkpoint::forest_area_f_end},
            {L"DW_FOREST_EV_dw023", kh02_checkpoint::forest_scene_dw023},
            {L"DW_FOREST_BT_DARKSIDE02_START", kh02_checkpoint::second_darkside_start},
            {L"DW_FOREST_BT_DARKSIDE02_END", kh02_checkpoint::second_darkside_end},
            {L"DW_FOREST_EV_dw024", kh02_checkpoint::forest_scene_dw024},
            {L"DW_FOREST_EV_dw025_A", kh02_checkpoint::forest_scene_dw025_a},
            // Two shipped graphs still use pre-rename labels. Resolve their
            // read-side aliases to the released flag-table checkpoints.
            {L"DW_FOREST_BT_DARKSIDE01", kh02_checkpoint::first_darkside_start},
            {L"DW_FOREST_EV_dw027", kh02_checkpoint::island_scene_dw027},
            {L"DW_FOREST_EV_dw025_B", kh02_checkpoint::forest_scene_dw025_b},
            {L"DW_DARK_BT_DEVILTOWER_START", kh02_checkpoint::depths_tower_start},
            {L"DW_DARK_BT_DEVILTOWER_END", kh02_checkpoint::depths_tower_end},
            {L"DW_FOREST_EV_dw026", kh02_checkpoint::forest_scene_dw026},
            {L"DW_ISLAND_EV_dw027", kh02_checkpoint::island_scene_dw027},
            {L"DW_ISLAND_EV_dw028", kh02_checkpoint::island_scene_dw028},
            {L"DW_ISLAND_BT_DEVILWAVE_START", kh02_checkpoint::demon_tide_start},
            {L"DW_ISLAND_BT_DEVILWAVE_END", kh02_checkpoint::demon_tide_end},
            {L"DW_ISLAND_EV_dw029", kh02_checkpoint::island_scene_dw029},
            {L"DW_ISLAND_EV_dw030", kh02_checkpoint::island_scene_dw030},
            {L"DW_ISLAND_EV_dw031", kh02_checkpoint::island_scene_dw031},
            {L"DW_ISLAND_EV_dw032", kh02_checkpoint::island_scene_dw032},
            {L"DW_ISLAND_EV_dw033", kh02_checkpoint::island_scene_dw033},
            {L"DW_ISLAND_EV_dw034", kh02_checkpoint::island_scene_dw034},
            {L"DW_ISLAND_EV_dw035", kh02_checkpoint::island_scene_dw035},
            {L"DW_MAGICROOM_EV_yt036", kh02_checkpoint::magic_room_scene_yt036},
            {L"DW_COCKPIT_EV_gm037", kh02_checkpoint::cockpit_scene_gm037},
            {L"DW_END", kh02_checkpoint::story_end},
            {L"CHALLENGE_42_START", 0},
            {L"CHALLENGE_42_BATTLE_01", 10},
            {L"CHALLENGE_42_BATTLE_02", 20},
            {L"CHALLENGE_42_BATTLE_03", 30},
            {L"CHALLENGE_42_BATTLE_04", 40},
            {L"CHALLENGE_42_BATTLE_05", 50},
            {L"DW_ROAD_BT_BATTLE02", 70},
            {L"DW_GEAR_EV_dw009", kh02_checkpoint::gear_scene_dw009},
            {L"DW_GEAR_EV_dw010", kh02_checkpoint::gear_scene_dw010},
            {L"DW_GEAR_EV_dw011", kh02_checkpoint::gear_scene_dw011},
            {L"DW_GEAR_EV_dw012", kh02_checkpoint::gear_scene_dw012},
            {L"DW_GEAR_EV_GET_GEAR1", kh02_checkpoint::first_gear},
            {L"DW_GEAR_EV_GET_GEAR2", kh02_checkpoint::second_gear},
            {L"DW_GEAR_EV_GET_GEAR3", kh02_checkpoint::third_gear},
            {L"DW_GEAR_EV_GET_GEAR4", kh02_checkpoint::fourth_gear},
            {L"DW_GEAR_BT_BATTLE03_START", kh02_checkpoint::castle_tower_start},
            {L"DW_GEAR_BT_BATTLE03_END", kh02_checkpoint::castle_tower_end},
            {L"DW_GEAR_EV_dw013", kh02_checkpoint::gear_scene_dw013},
            {L"DW_GEAR_EV_dw014", kh02_checkpoint::gear_scene_dw014},
            {L"DW_MIRROR_EV_dw015", kh02_checkpoint::mirror_scene_dw015},
            {L"DW_MIRROR_EV_dw016", kh02_checkpoint::mirror_scene_dw016},
            {L"DW_MIRROR_EV_dw017", kh02_checkpoint::mirror_scene_dw017},
            {L"DW_MIRROR_PHANTOM_BATTLE01", kh02_checkpoint::phantom_battle_one},
            {L"DW_MIRROR_EV_dw018", kh02_checkpoint::mirror_scene_dw018},
            {L"DW_MIRROR_PHANTOM_BATTLE02", kh02_checkpoint::phantom_battle_two},
            {L"DW_MIRROR_PHANTOM_BATTLE03", kh02_checkpoint::phantom_battle_three},
            {L"DW_MIRROR_EV_dw019", kh02_checkpoint::mirror_scene_dw019},
            {L"DW_MIRROR_EV_dw020", kh02_checkpoint::mirror_scene_dw020},
            {L"None", -1},
            {L"DW_GEAR_GETCOUNT_00", 0},
            {L"DW_GEAR_GETCOUNT_01", kh02_gear_counter::one_collected},
            {L"DW_GEAR_GETCOUNT_02", kh02_gear_counter::two_collected},
            {L"DW_GEAR_GETCOUNT_03", kh02_gear_counter::three_collected},
            {L"DW_GEAR_GETCOUNT_04", kh02_gear_counter::all_collected}};

        using Operation = Kh02StoryFlagBindings::Operation;
        Kh02StoryFlagBindings bindings{{[this](UObject* caller) { ensure_source_world(caller); },
                                        [this](const Kh02StoryFlagBindings::Request& request)
                                        { return evaluate(request); }}};

        void ensure_source_world(UObject* caller)
        {
            if (!is_uobject_valid_fast(world.Get()))
            {
                // Native Retry creates a replacement host before its source
                // level BeginPlay calls. Only this captured boss checkpoint
                // may follow an actor into the exact Castle Town host.
                auto* actor =
                    is_uobject_valid_fast(caller) && caller->IsA<AActor>() ? static_cast<AActor*>(caller) : nullptr;
                if (flags[L"gameflow_DW"] >= kh02_checkpoint::mirror_scene_dw015)
                {
                    rebind_mirror(actor ? actor->GetWorld() : nullptr);
                }
                else
                {
                    rebind_boss_retry(actor ? actor->GetWorld() : nullptr);
                }
            }
        }

        auto evaluate(const Kh02StoryFlagBindings::Request& request) -> int
        {
            const auto& key = request.key;
            if (request.operation == Operation::integer || request.operation == Operation::boolean)
            {
                bindings.note_read();
                if (!request.result_requested)
                {
                    return 0;
                }
                const int value = flags[key];
                return request.operation == Operation::boolean ? value != 0 : value;
            }
            if (request.operation == Operation::set_boolean)
            {
                flags[key] = request.boolean;
                bindings.note_write();
                if (changed)
                {
                    changed();
                }
                return 0;
            }
            const auto label = labels.find(request.label);
            if (label == labels.end())
            {
                throw std::runtime_error("source_flag_unknown_label:" + to_utf8(request.label));
            }
            if (request.operation == Operation::set_label)
            {
                if (kh02_checks::second_darkside_victory(request.source_node, request.normalized_key, flags[key],
                                                         label->second))
                {
                    flags[L"gameflow_DW_AP_DARKSIDE_02_CLEAR"] = 1;
                }
                flags[key] = label->second;
                bindings.note_write();
                if (changed)
                {
                    changed();
                }
                return 0;
            }
            bindings.note_read();
            const int value = flags[key];
            return request.operation == Operation::equal           ? value == label->second
                   : request.operation == Operation::greater_equal ? value >= label->second
                                                                   : value < label->second;
        }

        void rebind_boss_retry(UObject* w)
        {
            if (!is_uobject_valid_fast(w) ||
                w->GetPathName() != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" ||
                flags[L"gameflow_DW"] != kh02_checkpoint::castle_tower_start ||
                flags[L"gameflow_DW_GEAR_GETCOUNT"] != kh02_gear_counter::all_collected)
            {
                throw std::runtime_error("source_retry_context");
            }
            world = make_uobject_weak(w);
        }

        void rebind_mirror(UObject* w)
        {
            if (!is_uobject_valid_fast(w) || flags[L"gameflow_DW"] < kh02_checkpoint::mirror_scene_dw015)
            {
                throw std::runtime_error("source_mirror_rebind_state");
            }
            const auto name = w->GetPathName();
            bool owned = name == L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin";
            for (const auto& r : kh02mirror_metadata::rooms)
            {
                owned |= name == std::wstring(r.package) + L"." + r.name;
            }
            if (!owned)
            {
                throw std::runtime_error("source_mirror_rebind_world");
            }
            world = make_uobject_weak(w);
        }

        void activate(UObject* w, int checkpoint, bool restart)
        {
            if (checkpoint != kh02_checkpoint::gear_scene_dw011 && checkpoint != kh02_checkpoint::gear_scene_dw012 &&
                checkpoint != kh02_checkpoint::mirror_scene_dw015 && checkpoint != kh02_checkpoint::mirror_scene_dw016)
            {
                throw std::runtime_error("source_flag_checkpoint");
            }
            bool owned = is_uobject_valid_fast(w) &&
                         (w->GetPathName() == L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown" ||
                          w->GetPathName() == L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin");
            if (is_uobject_valid_fast(w) && (checkpoint == kh02_checkpoint::mirror_scene_dw015 ||
                                             checkpoint == kh02_checkpoint::mirror_scene_dw016))
            {
                for (const auto& r : kh02mirror_metadata::rooms)
                {
                    owned |= w->GetPathName() == std::wstring(r.package) + L"." + r.name;
                }
            }
            if (!owned)
            {
                throw std::runtime_error("source_flag_world");
            }
            bindings.install();
            if (world.Get() != w || restart)
            {
                world = make_uobject_weak(w);
                flags.clear();
                flags[L"gameflow_DW"] = checkpoint;
                flags[L"gameflow_DW_GEAR_GETCOUNT"] = 0;
                bindings.reset_observations();
            }
        }

        auto values() const -> nlohmann::json
        {
            nlohmann::json values = nlohmann::json::object();
            for (const auto& [key, v] : flags)
            {
                values[to_utf8(key)] = v;
            }
            return values;
        }

        auto snapshot() const -> std::string
        {
            auto result = bindings.snapshot();
            result["active"] = is_uobject_valid_fast(world.Get());
            result["values"] = values();
            return result.dump();
        }
    };

    Kh02StoryFlags::Kh02StoryFlags() : impl(std::make_unique<Impl>()) {}

    Kh02StoryFlags::~Kh02StoryFlags() = default;

    void Kh02StoryFlags::activate(UObject* w, int checkpoint, bool restart)
    {
        impl->activate(w, checkpoint, restart);
    }

    void Kh02StoryFlags::rebind_boss_retry(UObject* w)
    {
        impl->rebind_boss_retry(w);
    }

    void Kh02StoryFlags::rebind_mirror(UObject* w)
    {
        impl->rebind_mirror(w);
    }

    void Kh02StoryFlags::reset_mirror_hub_entry()
    {
        auto* world = impl->world.Get();
        if (!is_uobject_valid_fast(world) ||
            world->GetPathName() != L"/Game/Maps/APKH02WorldWithin/APKH02WorldWithin.APKH02WorldWithin")
        {
            throw std::runtime_error("source_mirror_hub_entry_world");
        }
        const auto before = progress_values();
        const auto after = kh02_progress::hub_entry_flags(before);
        if (after == before)
        {
            return;
        }
        restore_mirror(after);
        if (impl->changed)
        {
            impl->changed();
        }
    }

    void Kh02StoryFlags::select_mirror_room_entry(int area)
    {
        const wchar_t* name = area == 4   ? L"APKH02Stairs"
                              : area == 5 ? L"APKH02Corridor"
                              : area == 6 ? L"APKH02Tunnel"
                                          : nullptr;
        auto* world = impl->world.Get();
        if (!name || !is_uobject_valid_fast(world) ||
            world->GetPathName() != L"/Game/Maps/APKH02Mirrors/" + std::wstring(name) + L"." + name)
        {
            throw std::runtime_error("source_mirror_room_entry_world");
        }
        const auto before = progress_values();
        const auto after = kh02_progress::mirror_room_entry_flags(before, area);
        if (after == before)
        {
            return;
        }
        restore_mirror(after);
        if (impl->changed)
        {
            impl->changed();
        }
    }

    auto Kh02StoryFlags::active() const -> bool
    {
        return is_uobject_valid_fast(impl->world.Get());
    }

    auto Kh02StoryFlags::value(std::wstring_view key, int fallback) const -> int
    {
        const auto found = impl->flags.find(key);
        return found == impl->flags.end() ? fallback : found->second;
    }

    auto Kh02StoryFlags::values() const -> nlohmann::json
    {
        return impl->values();
    }

    auto Kh02StoryFlags::progress_values() const -> kh02_progress::Flags
    {
        kh02_progress::Flags result;
        for (const auto& [key, value] : impl->flags)
        {
            result.emplace(to_utf8(key), value);
        }
        return result;
    }

    auto Kh02StoryFlags::snapshot() const -> std::string
    {
        return impl->snapshot();
    }

    void Kh02StoryFlags::on_changed(std::function<void()> callback)
    {
        impl->changed = std::move(callback);
    }

    void Kh02StoryFlags::reset()
    {
        impl->world = {};
        impl->flags.clear();
        impl->bindings.reset_observations();
    }

    void Kh02StoryFlags::record_chest(const std::wstring& id)
    {
        static const std::set<std::wstring> ids{L"DW_LBOX01", L"DW_BOX003", L"DW_BOX004", L"DW_BOX006", L"DW_BOX007",
                                                L"DW_BOX008", L"DW_LBOX02", L"DW_BOX009", L"DW_BOX011", L"DW_BOX013",
                                                L"DW_BOX033", L"DW_BOX015", L"DW_BOX016"};
        if (!is_uobject_valid_fast(impl->world.Get()) ||
            (!ids.contains(id) &&
             std::find(kh02_tracking::forest_chests.begin(), kh02_tracking::forest_chests.end(), id) ==
                 kh02_tracking::forest_chests.end() &&
             std::find(kh02_tracking::additional_chests.begin(), kh02_tracking::additional_chests.end(), id) ==
                 kh02_tracking::additional_chests.end()))
        {
            throw std::runtime_error("kh02_challenge_chest_identity");
        }
        auto& value = impl->flags[L"gameflow_DW_CHALLENGE_CHEST_" + id];
        if (!value)
        {
            value = 1;
            if (impl->changed)
            {
                impl->changed();
            }
        }
    }

    void Kh02StoryFlags::record_aqua_experience(int total)
    {
        if (!active())
        {
            throw std::runtime_error("kh02_aqua_experience_inactive");
        }
        (void)kh02_level::add_experience(total, 0);
        auto& value = impl->flags[kh02_level::exp_flag];
        if (value != total)
        {
            value = total;
            if (impl->changed)
            {
                impl->changed();
            }
        }
    }

    void Kh02StoryFlags::record_visit(int area)
    {
        if (!is_uobject_valid_fast(impl->world.Get()) || area < 2 || area > 10)
        {
            throw std::runtime_error("source_visit_context");
        }
        auto& value = impl->flags[L"gameflow_DW_VISITED_" + std::to_wstring(area)];
        if (!value)
        {
            value = 1;
            if (impl->changed)
            {
                impl->changed();
            }
        }
    }

    void Kh02StoryFlags::restore_mirror(const std::string& text)
    {
        restore_mirror(nlohmann::json::parse(text));
    }

    void Kh02StoryFlags::restore_mirror(const nlohmann::json& values)
    {
        kh02_progress::validate(nlohmann::json{{"schema", "kh02.mirror_progress.v1"},
                                               {"flags", values},
                                               {"challenges", nlohmann::json::object()},
                                               {"counts", nlohmann::json::object()},
                                               {"events", nlohmann::json::object()}});
        kh02_progress::Flags restored;
        for (auto entry = values.begin(); entry != values.end(); ++entry)
        {
            restored.emplace(entry.key(), entry.value().get<int>());
        }
        restore_mirror(restored);
    }

    void Kh02StoryFlags::restore_mirror(const kh02_progress::Flags& values)
    {
        kh02_progress::Snapshot progress;
        progress.flags = values;
        kh02_progress::validate(progress);
        auto* world = impl->world.Get();
        if (!is_uobject_valid_fast(world))
        {
            throw std::runtime_error("source_progress_world");
        }
        if (world->GetPathName() != L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown")
        {
            impl->rebind_mirror(world);
        }
        decltype(impl->flags) restored;
        for (const auto& [name, value] : values)
        {
            restored[std::wstring(name.begin(), name.end())] = value;
        }
        impl->flags = std::move(restored);
    }

    void Kh02StoryFlags::restore_collected_gears(const std::string& text)
    {
        const auto saved = nlohmann::json::parse(text);
        if (!is_uobject_valid_fast(impl->world.Get()) || saved.size() > 32 || !saved.is_object())
        {
            throw std::runtime_error("source_resume_context");
        }
        decltype(impl->flags) values;
        for (auto it = saved.begin(); it != saved.end(); ++it)
        {
            const auto text_key = it.key();
            const std::wstring key(text_key.begin(), text_key.end());
            const auto v = it.value().get<int>();
            if (!key.starts_with(L"gameflow_DW") || key.size() > 96 || v < 0 || v > 1000)
            {
                throw std::runtime_error("source_resume_key");
            }
            values[key] = v;
        }
        if (values[L"gameflow_DW"] != kh02_checkpoint::fourth_gear ||
            values[L"gameflow_DW_GEAR_GETCOUNT"] != kh02_gear_counter::all_collected)
        {
            throw std::runtime_error("source_resume_not_collected");
        }
        impl->flags = std::move(values);
    }
} // namespace kh3ap
