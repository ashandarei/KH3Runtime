#pragma once

// Engine boundary for the service regressions. The tested service methods and
// their state declarations are taken directly from the production sources.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#define STR(value) L##value
#define KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS 0
#include "kh3ap/features/music/events/sead_music_events.h"
#include "kh3ap/features/music/events/music_update_events.h"

namespace kh3ap
{
    inline SeadMusicMailbox test_music_mailbox;
    inline auto install_sead_music_events() -> bool { return true; }
    inline auto uninstall_sead_music_events() -> void { (void)test_music_mailbox.take(); }
    inline auto take_sead_music_events() noexcept -> uint32_t { return test_music_mailbox.take(); }
}

namespace RC
{
    enum class LogLevel { Normal, Warning };
    struct Output
    {
        template <LogLevel, class... Args>
        static void send(Args&&...) {}
    };

    namespace Unreal
    {
        struct UClass {};
        class UObject
        {
        public:
            // Same serial-reuse model as the existing color lifetime tests.
            int serial{1};
            bool alive{true};
            std::wstring name{}, path{}, level{L"he_01"}, class_name{};
            std::unordered_map<std::wstring, std::wstring> assets{};
            std::wstring track{}, pending_track{};
            uint8_t state{3}, behaviour{}, default_behaviour{};
            float volume{1.0f};
            bool defer_path{}, reject_path{};
            int path_writes{}, stops{};
            uint8_t character_state{31};
            std::wstring keyblade{L"WEP_KEYBLADE02"}, weapon{L"w_so021_C"};
            uint64_t fingerprint{222};
            float palette_use{1.0f}, palette_index{10.0f};
            std::array<float, 4> custom_color{0.1f, 0.2f, 0.3f, 1.0f};
            int material_writes{};

            auto GetName() const -> std::wstring { return name; }
            auto GetPathName() const -> std::wstring { return path; }
            void SetRootSet() {}
        };
        class UFunction : public UObject {};
        struct FWeakObjectPtr
        {
            UObject* object{};
            int serial{};
            void operator=(UObject* value)
            {
                object = value;
                serial = value ? value->serial : 0;
            }
            auto Get() const -> UObject*
            {
                return serial != 0 && object && object->alive && object->serial == serial ? object : nullptr;
            }
        };
        namespace UObjectGlobals
        {
            inline void UnregisterHook(UFunction*, std::pair<int, int>) {}
        }
    }
}

#include "kh3ap/features/music/shuffle/bgm_shuffle_service.h"
#include "kh3ap/features/music/shuffle/bgm_shuffle_table.h"
#include "kh3ap/features/music/audio/custom_music_library.h"
#include "kh3ap/features/music/audio/custom_music_player.h"
#include "kh3ap/features/music/shuffle/form_theme.h"
#include "kh3ap/features/music/shuffle/goa_music_patch.h"
#include "kh3ap/features/music/events/sead_bgm.h"
#include "kh3ap/features/music/music_object_registry.h"
#include "kh3ap/features/colors/color_palette.h"
#include "kh3ap/features/colors/color_object_registry.h"

namespace kh3ap
{
    using RC::Unreal::UObject;
    using RC::Unreal::UClass;
    using RC::Unreal::FWeakObjectPtr;
    inline auto is_uobject_valid_fast(UObject* object) -> bool { return object && object->alive; }
    inline auto is_uobject_valid_fast(RC::Unreal::UFunction*) -> bool { return true; }
    inline auto make_uobject_weak(UObject* object) -> FWeakObjectPtr
    {
        FWeakObjectPtr result{};
        if (!is_uobject_valid_fast(object)) return result;
        // KH3's native assignment allocates a serial; UE4SS assignment doesn't.
        if (!object->serial) object->serial = 1;
        result = object;
        return result;
    }

    namespace fake_runtime
    {
        inline UObject manager{}, game_instance{}, settings{};
        inline std::vector<sead_bgm::SlotController> slots{};
        inline std::vector<UObject*> world_settings{}, load_assets{};
        inline std::unordered_map<std::wstring, bool> playable_files{};
        inline std::filesystem::path music_folder{};
        inline int scans{};
        inline int slot_mapping_reads{}, music_directory_reads{};
        inline UObject gummi_world;
        inline bool gummi_world_available{true};
        inline UObject* editing{};
        inline color_objects::Snapshot gummi_objects;
        inline uint64_t gummi_revision{};
        inline int gummi_snapshots{}, world_reads{}, table_reads{};
    }

    namespace color_objects {
        inline uint64_t gummi_generation() { return fake_runtime::gummi_revision; }
        inline Snapshot snapshot(bool) { ++fake_runtime::gummi_snapshots;return fake_runtime::gummi_objects; }
    }
    inline UObject* current_world(FWeakObjectPtr&) {
        ++fake_runtime::world_reads;
        if(fake_runtime::gummi_world.name.empty())fake_runtime::gummi_world.name=L"wm_01";
        return fake_runtime::gummi_world_available ? &fake_runtime::gummi_world : nullptr;
    }
    inline void read_plan_table(UObject* table,std::unordered_map<uint64_t,std::wstring>& plans) {
        ++fake_runtime::table_reads;plans[table->fingerprint]=table->name;
    }

    namespace music_objects
    {
        inline void initialize() {}
        inline Snapshot snapshot()
        {
            Snapshot result;
            result.settings=fake_runtime::world_settings;
            result.load_assets=fake_runtime::load_assets;
            result.managers={&fake_runtime::manager};
            result.bootstrap_scans=1;
            return result;
        }
    }
    namespace sead_bgm
    {
        inline auto find_live_objects(const wchar_t* name) -> std::vector<UObject*>
        {
            if (std::wstring_view{name} == L"SQEXSEADBGMManager") return {&fake_runtime::manager};
            if (std::wstring_view{name} == L"TresGameInstance") return {&fake_runtime::game_instance};
            if (std::wstring_view{name} == L"TresWorldSettings") return fake_runtime::world_settings;
            return {};
        }
        inline auto still_live(UObject* object, std::wstring_view) -> bool { return is_uobject_valid_fast(object); }
        inline auto current_level_name(UObject* object) -> std::optional<std::wstring>
        {
            return object ? std::optional{object->level} : std::nullopt;
        }
        inline auto slot_controllers(UObject*) -> std::vector<SlotController>
        { ++fake_runtime::slot_mapping_reads; return fake_runtime::slots; }
        inline auto controller_for_slot(UObject*, std::wstring_view name) -> UObject*
        {
            ++fake_runtime::slot_mapping_reads;
            for (const auto& slot : fake_runtime::slots) if (slot.name == name) return slot.controller;
            return nullptr;
        }
        inline auto slot_state(UObject* object) -> std::optional<uint8_t>
        {
            return object ? std::optional{object->state} : std::nullopt;
        }
        inline auto current_track(UObject* object) -> std::wstring { return object ? object->track : std::wstring{}; }
        inline auto set_volume(UObject* object, float volume, float) -> bool { object->volume = volume; return true; }
        inline auto set_path(UObject* object, std::wstring_view path) -> bool
        {
            if (!object || object->reject_path) return false;
            ++object->path_writes;
            if (object->defer_path) object->pending_track = path;
            else object->track = path;
            return true;
        }
        inline auto play(UObject* object) -> bool { object->state = slot_state_playing; object->volume = 1.0f; return true; }
        inline auto stop(UObject* object) -> bool { object->state = slot_state_stop; ++object->stops; return true; }
        inline auto set_seek_time(UObject*, float) -> bool { return true; }
        inline auto default_options(UObject* object) -> std::optional<BgmOptions>
        {
            return BgmOptions{object->default_behaviour, 0.0f};
        }
        inline auto set_store_behaviour(UObject* object, uint8_t value) -> bool { object->behaviour = value; return true; }
        inline auto read_volume_settings(UObject*) -> std::optional<VolumeSettings> { return VolumeSettings{10, 10}; }
        inline auto read_asset_path(UObject* object, const wchar_t* property) -> std::optional<std::wstring>
        {
            return object->assets[property];
        }
        inline auto write_asset_path(UObject* object, const wchar_t* property, std::wstring_view path) -> bool
        {
            object->assets[property] = path;
            return true;
        }
    }

    inline auto read_equipped_keyblade_name(UObject* pawn) -> std::optional<std::wstring> { return pawn->keyblade; }
    inline auto find_loaded_runtime_asset(std::wstring_view) -> UObject* { return nullptr; }
    inline auto request_runtime_asset_load(UObject*, std::wstring_view) -> bool { return true; }

    inline auto ship_fingerprint(UObject* ship) -> std::optional<uint64_t> { return ship->fingerprint; }
    inline auto resolve_mesh_component_class() -> UClass* { static UClass type; return &type; }
    inline auto collect_mesh_components(UObject* actor, UClass*) -> std::vector<UObject*> { return {actor}; }
    inline auto read_num_materials(UObject*) -> std::optional<int> { return 1; }
    inline auto create_dynamic_material_instance(UObject* object, int) -> UObject* { return object; }
    inline auto read_scalar_parameter_from_mid(UObject* object, std::wstring_view name) -> std::optional<float>
    {
        return name == L"CT_PaletteUse" ? object->palette_use : object->palette_index;
    }
    inline auto read_vector_parameter_from_mid(UObject* object, std::wstring_view) -> std::optional<std::array<float, 4>>
    {
        return object->custom_color;
    }
    inline void set_vector_parameter_on_mid(UObject* object, std::wstring_view, DerivedColor value, std::wstring_view, float alpha)
    {
        object->custom_color = {value.r, value.g, value.b, alpha};
        ++object->material_writes;
    }
    inline void set_scalar_parameter_on_mid(UObject* object, std::wstring_view, float value, std::wstring_view)
    {
        object->palette_use = value;
        ++object->material_writes;
    }
}
