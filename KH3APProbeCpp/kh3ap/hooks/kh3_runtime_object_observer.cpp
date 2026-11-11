#include <array>
#include "kh3ap/hooks/kh3_runtime_object_observer.h"
#include "kh3ap/features/story/minigames/beat_of_lead_completion.h"
#include "kh3ap/features/story/world_fixes/rapunzel_water_bypass.h"
#include "kh3ap/features/story/world_fixes/rapunzel_swing_frame_rate.h"
#include "kh3ap/features/story/world_fixes/rapunzel_hair_approach.h"
#include "kh3ap/features/story/world_fixes/graveyard_backtracking.h"
#include "kh3ap/features/story/world_fixes/big_hero_rescue_resume.h"
#include "kh3ap/features/music/shuffle/goa_music_patch.h"
#include "kh3ap/features/music/music_object_registry.h"
#include "kh3ap/features/colors/color_object_registry.h"
#include "kh3ap/features/enemies/titan_object_registry.h"
#include "kh3ap/features/enemies/bosses/master_xehanort_center.h"
#include "kh3ap/features/enemies/bosses/master_xehanort_queries.h"
#include "kh3ap/features/moogle_shop/workshop_service.h"
#include "kh3ap/features/story/world_fixes/final_world_guidance_bypass.h"
#include "kh3ap/features/tutorials/tutorial_skip_service.h"
#include "kh3ap/features/story/minigames/union_cross_completion.h"
#include "kh3ap/features/ingredients/ingredient_service.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/story/bootstrap/world_bootstrap.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <UnrealDef.hpp>
#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

namespace kh3ap
{
    namespace
    {
        constexpr auto all_toy_box_gameflow_patch_bits = 0x0f;
        constexpr auto olympus_help_class_index = 2;

        constexpr auto max_observed_runtime_class_name_offset = 0x100;
        // UE4SS exports FName as part of its public ABI. Some UE4SS builds spell
        // that type as a class and others as a struct, which changes decorated
        // operator names even though the UE4.17 in-memory layout is identical.
        // Compare the two uint32 fields locally so this DLL does not import those
        // fragile convenience operators. This is also the cheapest hot-path test.
        [[nodiscard]] auto fname_bits(const RC::Unreal::FName& name) -> uint64_t
        {
            static_assert(sizeof(RC::Unreal::FName) == sizeof(uint64_t));
            uint64_t bits{};
            std::memcpy(&bits, &name, sizeof(bits));
            return bits;
        }

        [[nodiscard]] auto fname_is_none(const RC::Unreal::FName& name) -> bool
        {
            return fname_bits(name) == 0;
        }

        [[nodiscard]] auto fname_equals(const RC::Unreal::FName& left,
                                        const RC::Unreal::FName& right) -> bool
        {
            return fname_bits(left) == fname_bits(right);
        }

        using GetNamePrivateFunction = const RC::Unreal::FName& (*)(const RC::Unreal::UObjectBase*);

        [[nodiscard]] auto resolve_get_name_private() -> GetNamePrivateFunction
        {
            const HMODULE ue4ss_module = GetModuleHandleW(L"UE4SS.dll");
            if (!ue4ss_module)
            {
                return nullptr;
            }

            // RE-UE4SS changed FName from class to struct in its headers without
            // changing the object layout. MSVC records that spelling in the
            // decorated return type, so accept either export at runtime.
            constexpr const char* candidate_exports[]{
                "?GetNamePrivate@UObjectBase@Unreal@RC@@QEBAAEBVFName@23@XZ",
                "?GetNamePrivate@UObjectBase@Unreal@RC@@QEBAAEBUFName@23@XZ",
            };
            for (const char* const export_name : candidate_exports)
            {
                if (const FARPROC address = GetProcAddress(ue4ss_module, export_name))
                {
                    static_assert(sizeof(address) == sizeof(GetNamePrivateFunction));
                    GetNamePrivateFunction function{};
                    std::memcpy(&function, &address, sizeof(function));
                    return function;
                }
            }
            return nullptr;
        }
    } // namespace

    Kh3RuntimeObjectObserver::~Kh3RuntimeObjectObserver()
    {
        uninstall();
    }

    bool Kh3RuntimeObjectObserver::initialize_class_names(
        const Kh3RuntimeObjectObservationOptions& options)
    {
        using RC::Unreal::FName;
        struct Target
        {
            FName* destination;
            const wchar_t* name;
            bool enabled;
        };
        // Resolve each enabled name once on the game thread. Object creation
        // callbacks retain their existing integer-only classification path.
        const auto targets = std::to_array<Target>({
            {&open_treasure_player_state_name_, L"TresPlayerState_OpenTreasureBox",
             options.observe_open_treasure_player_state},
            {&open_treasure_char_state_name_, L"TresCharState_OpenTreasureBox",
             options.observe_open_treasure_char_state},
            {&treasure_logic_actor_name_, L"TreasureLogicActor_C", true},
            {&sora_pawn_class_name_, L"p_ex001_Pawn_C", options.observe_sora_pawn},
            {&lucky_mark_component_class_name_, L"TresHiddenMickeyComponent",
             options.observe_lucky_mark_components},
            {&bx_portal_class_object_name_, L"BX_Portal_C", options.observe_runtime_portal_classes},
            {&death_mirror_class_object_name_, L"A_GoA_DeathMirror_C",
             options.observe_runtime_portal_classes},
            {&rapunzel_water_class_name_, runtime_rapunzel_water_class, true},
            {&big_hero_gameflow_class_name_, L"bx_02_gameflow_C", true},
            {&rapunzel_swing_camera_class_name_, runtime_rapunzel_swing_camera_class, true},
            {&rapunzel_hair_approach_class_name_, runtime_rapunzel_hair_approach_class, true},
            {&goa_level_script_class_name_, runtime_goa_level_script_class, true},
            {&festival_dance_story_class_name_, L"ra_02_mission_Dance_C",
             options.observe_festival_dance_story},
            {&festival_dance_story_manager_class_name_, L"lv_ra_FestivalDance_C",
             options.observe_festival_dance_story},
            {&beat_of_lead_manager_class_name_, runtime_beat_of_lead_manager_class_name.data(),
             options.observe_beat_of_lead_story},
            {&union_cross_command_class_name_, runtime_union_cross_command_class_name.data(),
             options.observe_union_cross_commands},
            {&final_world_guidance_ew01_gameflow_class_name_,
             runtime_final_world_guidance_ew01_gameflow_class_name.data(),
             options.observe_final_world_gameflow_patches},
            {&final_world_guidance_ew04_gameflow_class_name_,
             runtime_final_world_guidance_ew04_gameflow_class_name.data(),
             options.observe_final_world_gameflow_patches},
            {&olympus_completion_gameflow_class_name_,
             runtime_olympus_completion_gameflow_class_name.data(),
             options.observe_olympus_completion_gameflow_patch},
            {&olympus_early_gameflow_class_name_, runtime_olympus_early_gameflow_class_name.data(),
             options.observe_olympus_early_gameflow_patch},
            {&twilight_town_gameflow_class_name_, runtime_twilight_town_gameflow_class_name.data(),
             options.observe_twilight_town_gameflow_patch},
            {&olympus_save_points_gameflow_class_name_,
             runtime_olympus_save_points_gameflow_class_name.data(),
             options.observe_olympus_save_points_gameflow_patch},
            {&caribbean_sea_spectacle_class_name_,
             runtime_caribbean_sea_spectacle_class_name.data(),
             options.observe_caribbean_sea_spectacle_patch},
        });
        for (const auto& target : targets)
        {
            *target.destination =
                target.enabled ? FName{target.name, RC::Unreal::FNAME_Add} : FName{};
            if (target.enabled && fname_is_none(*target.destination))
            {
                return false;
            }
        }
        const auto register_group =
            []<size_t Size>(std::array<FName, Size>& destination, const auto& names, bool enabled)
        {
            for (size_t index = 0; index < destination.size(); ++index)
            {
                destination[index] =
                    enabled ? FName{names[index].data(), RC::Unreal::FNAME_Add} : FName{};
                if (enabled && fname_is_none(destination[index]))
                {
                    return false;
                }
            }
            return true;
        };
        constexpr std::array graveyard_names{std::wstring_view{L"kg_02_gameflow_Main_C"},
                                             std::wstring_view{L"kg_02_Mission_Area_A_C"},
                                             std::wstring_view{L"kg_02_Mission_Area_B_C"}};
        if (!register_group(graveyard_backtracking_class_names_, graveyard_names, true) ||
            !register_group(runtime_world_bootstrap_class_object_names_,
                            runtime_world_bootstrap_class_object_names,
                            options.observe_runtime_world_bootstrap_classes) ||
            !register_group(toy_box_gameflow_class_names_,
                            runtime_toy_box_tutorial_gameflow_class_names,
                            options.observe_toy_box_gameflow_patches) ||
            !register_group(link_tutorial_class_names_, runtime_link_tutorial_class_names,
                            options.observe_link_tutorial_patches) ||
            !register_group(informational_help_class_names_, runtime_informational_help_class_names,
                            options.observe_informational_help_patches))
        {
            return false;
        }
        for (size_t index = 0; index < world_keyblade_class_names_.size(); ++index)
        {
            const std::wstring name =
                std::wstring{runtime_world_keyblade_profiles[index].gameflow} + L"_C";
            world_keyblade_class_names_[index] = FName{name.c_str(), RC::Unreal::FNAME_Add};
            if (fname_is_none(world_keyblade_class_names_[index]))
            {
                return false;
            }
        }
        return true;
    }

    void Kh3RuntimeObjectObserver::configure_observations(
        const Kh3RuntimeObjectObservationOptions& options)
    {
        observe_festival_dance_story_.store(options.observe_festival_dance_story,
                                            std::memory_order_release);
        observe_beat_of_lead_story_.store(options.observe_beat_of_lead_story,
                                          std::memory_order_release);
        observe_union_cross_commands_.store(options.observe_union_cross_commands,
                                            std::memory_order_release);
        observe_final_world_gameflow_patches_.store(options.observe_final_world_gameflow_patches,
                                                    std::memory_order_release);
        observe_olympus_completion_gameflow_patch_.store(
            options.observe_olympus_completion_gameflow_patch, std::memory_order_release);
        observe_olympus_early_gameflow_patch_.store(options.observe_olympus_early_gameflow_patch,
                                                    std::memory_order_release);
        observe_twilight_town_gameflow_patch_.store(options.observe_twilight_town_gameflow_patch,
                                                    std::memory_order_release);
        observe_olympus_save_points_gameflow_patch_.store(
            options.observe_olympus_save_points_gameflow_patch, std::memory_order_release);
        observe_caribbean_sea_spectacle_patch_.store(options.observe_caribbean_sea_spectacle_patch,
                                                     std::memory_order_release);
        observe_link_tutorial_patches_.store(options.observe_link_tutorial_patches,
                                             std::memory_order_release);
        observe_informational_help_patches_.store(options.observe_informational_help_patches,
                                                  std::memory_order_release);
        toy_box_enabled_patch_mask_.store(
            options.observe_toy_box_gameflow_patches ? all_toy_box_gameflow_patch_bits : 0,
            std::memory_order_release);

        observe_open_treasure_player_state_ = options.observe_open_treasure_player_state;
        observe_open_treasure_char_state_ = options.observe_open_treasure_char_state;
        observe_any_open_treasure_state_ =
            observe_open_treasure_player_state_ || observe_open_treasure_char_state_;
        observe_sora_pawn_ = options.observe_sora_pawn;
        observe_lucky_mark_components_ = options.observe_lucky_mark_components;
        observe_runtime_portal_classes_ = options.observe_runtime_portal_classes;
        observe_runtime_world_bootstrap_classes_ = options.observe_runtime_world_bootstrap_classes;
    }

    auto Kh3RuntimeObjectObserver::install(const Kh3RuntimeObjectObservationOptions& options)
        -> bool
    {
        if (installed_.load(std::memory_order_acquire))
        {
            return true;
        }

        const GetNamePrivateFunction get_name_private = resolve_get_name_private();
        const RC::Unreal::UClass* const class_class = RC::Unreal::UClass::StaticClass();
        if (!get_name_private || !class_class)
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[KH3APProbeCpp] Runtime object observer install failed "
                    "reason=get_name_private_unresolved\n"));
            return false;
        }

        const uintptr_t class_address = reinterpret_cast<uintptr_t>(class_class);
        const uintptr_t name_address =
            reinterpret_cast<uintptr_t>(std::addressof(get_name_private(class_class)));
        if (name_address <= class_address ||
            name_address - class_address > max_observed_runtime_class_name_offset)
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[KH3APProbeCpp] Runtime object observer install failed "
                    "reason=get_name_private_offset_invalid offset={}\n"),
                name_address - class_address);
            return false;
        }
        object_name_offset_ = static_cast<size_t>(name_address - class_address);

        if (!initialize_class_names(options))
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[KH3APProbeCpp] Runtime object observer install failed "
                    "reason=target_fname_unresolved\n"));
            return false;
        }
        configure_observations(options);

        try
        {
            RC::Unreal::UObjectArray::AddUObjectCreateListener(this);
        }
        catch (const std::exception& exception)
        {
            const std::string message =
                exception.what() ? exception.what() : "<empty std::exception::what()>";
            RC::Output::send<RC::LogLevel::Error>(
                STR("[KH3APProbeCpp] Runtime object observer install failed exception={}\n"),
                std::wstring{message.begin(), message.end()});
            return false;
        }
        catch (...)
        {
            RC::Output::send<RC::LogLevel::Error>(STR(
                "[KH3APProbeCpp] Runtime object observer install failed exception=<unknown>\n"));
            return false;
        }

        installed_.store(true, std::memory_order_release);
        RC::Output::send<RC::LogLevel::Warning>(
            STR("[KH3APProbeCpp] Runtime UObject-create observer installed queue_capacity={} "
                "player_state_fallback={} char_state_fallback={} sora_pawn={} "
                "festival_dance_story={} "
                "beat_of_lead_story={} "
                "union_cross_commands={} "
                "final_world_gameflow_patches={} olympus_completion_gameflow_patch={} "
                "olympus_early_gameflow_patch={} twilight_town_gameflow_patch={} "
                "olympus_save_points_gameflow_patch={} "
                "caribbean_sea_spectacle_patch={} "
                "toy_box_gameflow_patches={} lucky_mark_components={} runtime_portal_classes={} "
                "runtime_world_bootstrap_classes={}\n"),
            static_cast<uint64_t>(max_pending_observations), observe_open_treasure_player_state_,
            observe_open_treasure_char_state_, observe_sora_pawn_,
            options.observe_festival_dance_story, options.observe_beat_of_lead_story,
            options.observe_union_cross_commands, options.observe_final_world_gameflow_patches,
            options.observe_olympus_completion_gameflow_patch,
            options.observe_olympus_early_gameflow_patch,
            options.observe_twilight_town_gameflow_patch,
            options.observe_olympus_save_points_gameflow_patch,
            options.observe_caribbean_sea_spectacle_patch, options.observe_toy_box_gameflow_patches,
            observe_lucky_mark_components_, observe_runtime_portal_classes_,
            observe_runtime_world_bootstrap_classes_);
        return true;
    }

    auto Kh3RuntimeObjectObserver::uninstall() -> void
    {
        if (!installed_.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }

        try
        {
            RC::Unreal::UObjectArray::RemoveUObjectCreateListener(this);
        }
        catch (...)
        {
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Runtime object observer uninstall failed\n"));
        }

        std::lock_guard lock{pending_mutex_};
        pending_count_ = 0;
        published_pending_count_.store(0, std::memory_order_release);
        observe_festival_dance_story_.store(false, std::memory_order_release);
        observe_beat_of_lead_story_.store(false, std::memory_order_release);
        observe_union_cross_commands_.store(false, std::memory_order_release);
        observe_final_world_gameflow_patches_.store(false, std::memory_order_release);
        observe_olympus_completion_gameflow_patch_.store(false, std::memory_order_release);
        observe_olympus_early_gameflow_patch_.store(false, std::memory_order_release);
        observe_twilight_town_gameflow_patch_.store(false, std::memory_order_release);
        observe_olympus_save_points_gameflow_patch_.store(false, std::memory_order_release);
        observe_caribbean_sea_spectacle_patch_.store(false, std::memory_order_release);
        observe_runtime_portal_classes_ = false;
        observe_runtime_world_bootstrap_classes_ = false;
        observe_lucky_mark_components_ = false;
        toy_box_enabled_patch_mask_.store(0, std::memory_order_release);
        observe_link_tutorial_patches_.store(false, std::memory_order_release);
        observe_informational_help_patches_.store(false, std::memory_order_release);
    }

    auto Kh3RuntimeObjectObserver::installed() const -> bool
    {
        return installed_.load(std::memory_order_acquire);
    }

    auto Kh3RuntimeObjectObserver::disable_festival_dance_story_observation() -> void
    {
        observe_festival_dance_story_.store(false, std::memory_order_release);
    }

    auto Kh3RuntimeObjectObserver::is_treasure_logic_actor(const RC::Unreal::UObject* object) const
        -> bool
    {
        if (!object || fname_is_none(treasure_logic_actor_name_))
        {
            return false;
        }
        const RC::Unreal::UClass* const object_class = object->GetClassPrivate();
        const RC::Unreal::FName* const class_name = object_name(object_class);
        return class_name && fname_equals(*class_name, treasure_logic_actor_name_);
    }

    auto
    Kh3RuntimeObjectObserver::enqueue_native_item_grant(RC::Unreal::UObject* treasure_logic_actor,
                                                        const RC::Unreal::FName& item_key) -> void
    {
        enqueue({Kh3RuntimeObjectObservationKind::NativeItemGrant, treasure_logic_actor, item_key});
    }

    auto Kh3RuntimeObjectObserver::enqueue_ready_treasure_box(RC::Unreal::UObject* treasure_box)
        -> void
    {
        enqueue({Kh3RuntimeObjectObservationKind::TreasureBoxReady, treasure_box, {}});
    }

    auto Kh3RuntimeObjectObserver::drain(
        std::array<Kh3RuntimeObjectObservation, max_pending_observations>& output) -> size_t
    {
        if (published_pending_count_.load(std::memory_order_acquire) == 0)
        {
            return 0;
        }

        std::lock_guard lock{pending_mutex_};
        const size_t count = pending_count_;
        std::copy_n(pending_.begin(), count, output.begin());
        pending_count_ = 0;
        published_pending_count_.store(0, std::memory_order_release);
        return count;
    }

    auto Kh3RuntimeObjectObserver::resolve_observation_object(
        const Kh3RuntimeObjectObservation& observation) const -> RC::Unreal::UObject*
    {
        RC::Unreal::FUObjectItem* const object_item = observation.object_item;
        if (!observation.object || !object_item || object_item->GetUObject() != observation.object)
        {
            return nullptr;
        }
        return observation.object;
    }

    auto Kh3RuntimeObjectObserver::take_dropped_count() -> uint64_t
    {
        return dropped_count_.exchange(0, std::memory_order_acq_rel);
    }

    auto Kh3RuntimeObjectObserver::NotifyUObjectCreated(const RC::Unreal::UObjectBase* object,
                                                        RC::Unreal::int32 /*index*/) -> void
    {
        notify_workshop_object_created(
            static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        titan_objects::constructed(
            static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        notify_master_ascension_created(
            static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        notify_master_query_created(
            static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        color_objects::constructed(
            static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        music_objects::constructed(
            static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        notify_ingredient_marker_created(
            static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        observe_constructed_object(object);
    }

    auto Kh3RuntimeObjectObserver::observe_created_class(const RC::Unreal::UObjectBase* object)
        -> std::optional<bool>
    {
        if (observe_runtime_portal_classes_)
        {
            const RC::Unreal::FName* const created_object_name = object_name(object);
            Kh3RuntimeObjectObservationKind portal_kind{};
            bool portal_class_name_matches = false;
            if (created_object_name &&
                fname_equals(*created_object_name, bx_portal_class_object_name_))
            {
                portal_kind = Kh3RuntimeObjectObservationKind::BxPortalClassConstructed;
                portal_class_name_matches = true;
            }
            else if (created_object_name &&
                     fname_equals(*created_object_name, death_mirror_class_object_name_))
            {
                portal_kind = Kh3RuntimeObjectObservationKind::DeathMirrorClassConstructed;
                portal_class_name_matches = true;
            }

            if (portal_class_name_matches)
            {
                auto* const created_object =
                    static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object));
                if (!kh3ap::is_uobject_a_fast(created_object, RC::Unreal::UClass::StaticClass()))
                {
                    return false;
                }
                enqueue({portal_kind, created_object, {}});
                return true;
            }
        }

        if (observe_runtime_world_bootstrap_classes_)
        {
            const RC::Unreal::FName* const created_object_name = object_name(object);
            const bool class_name_matches =
                created_object_name &&
                std::any_of(runtime_world_bootstrap_class_object_names_.begin(),
                            runtime_world_bootstrap_class_object_names_.end(),
                            [created_object_name](const RC::Unreal::FName& name)
                            {
                                return fname_equals(*created_object_name, name);
                            });
            if (class_name_matches)
            {
                auto* const created_object =
                    static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object));
                if (!kh3ap::is_uobject_a_fast(created_object, RC::Unreal::UClass::StaticClass()))
                {
                    return false;
                }
                enqueue({
                    Kh3RuntimeObjectObservationKind::WorldBootstrapClassConstructed,
                    created_object,
                    {},
                });
                return true;
            }
        }

        return std::nullopt;
    }

    auto Kh3RuntimeObjectObserver::apply_story_patches(const RC::Unreal::UObjectBase* object,
                                                       const RC::Unreal::FName* class_name)
        -> std::optional<bool>
    {
        // Apply alongside existing help/ending patches on every map reload.
        // Only fixed FName comparisons run for unrelated objects.
        if (fname_equals(*class_name, big_hero_gameflow_class_name_))
        {
            (void)try_apply_big_hero_rescue_resume(
                    static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        }
        for (size_t index = 0; index < graveyard_backtracking_class_names_.size(); ++index)
        {
            if (fname_equals(*class_name, graveyard_backtracking_class_names_[index]))
            {
                return try_apply_graveyard_backtracking(
                    static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)),
                    index);
            }
        }
        for (size_t index = 0; index < world_keyblade_class_names_.size(); ++index)
        {
            if (fname_equals(*class_name, world_keyblade_class_names_[index]))
            {
                (void)try_apply_runtime_world_keyblade_early_bypass(
                    index, static_cast<RC::Unreal::UObject*>(
                               const_cast<RC::Unreal::UObjectBase*>(object)));
                break;
            }
        }
        if (fname_equals(*class_name, goa_level_script_class_name_))
        {
            // Before BeginPlay, which plays the Garden track: see
            // goa_music_patch.h.
            return try_apply_runtime_goa_music_patch(
                static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        }
        if (fname_equals(*class_name, rapunzel_water_class_name_))
        {
            return try_apply_rapunzel_water_bypass(
                static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        }
        if (fname_equals(*class_name, rapunzel_swing_camera_class_name_))
        {
            return try_apply_rapunzel_swing_frame_rate(
                static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        }
        if (fname_equals(*class_name, rapunzel_hair_approach_class_name_))
        {
            return try_apply_rapunzel_hair_approach(
                static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        }
        if (observe_caribbean_sea_spectacle_patch_.load(std::memory_order_relaxed) &&
            fname_equals(*class_name, caribbean_sea_spectacle_class_name_))
        {
            // This actor begins its delay/tutorial sequence from BeginPlay.
            // Keep the observer armed for every class reload and patch the
            // exact bytecode before that entry can execute.
            return try_apply_runtime_caribbean_sea_spectacle_early_bypass(
                static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        }
        return std::nullopt;
    }

    auto Kh3RuntimeObjectObserver::apply_tutorial_patches(const RC::Unreal::UObjectBase* object,
                                                          const RC::Unreal::FName* class_name)
        -> std::optional<bool>
    {
        if (observe_link_tutorial_patches_.load(std::memory_order_relaxed))
        {
            // Keep armed across Retry/map reloads. Patch before BeginPlay can
            // reach the local heartbinder grant; no raw UObject is retained.
            for (size_t index = 0; index < link_tutorial_class_names_.size(); ++index)
            {
                if (fname_equals(*class_name, link_tutorial_class_names_[index]))
                {
                    return try_apply_runtime_link_tutorial_early_bypass(
                        static_cast<RuntimeLinkTutorialVariant>(index),
                        static_cast<RC::Unreal::UObject*>(
                            const_cast<RC::Unreal::UObjectBase*>(object)));
                }
            }
        }
        if (observe_informational_help_patches_.load(std::memory_order_relaxed))
        {
            // Keep armed across Retry/map reloads. Patch before BeginPlay can
            // reach the help panel; no raw UObject is retained.
            for (size_t index = 0; index < informational_help_class_names_.size(); ++index)
            {
                if (fname_equals(*class_name, informational_help_class_names_[index]))
                {
                    const bool patched = try_apply_runtime_informational_help_early_bypass(
                        index, static_cast<RC::Unreal::UObject*>(
                                   const_cast<RC::Unreal::UObjectBase*>(object)));
                    // he_02 also owns the existing magic/free-running bypasses
                    // below. Apply both sets before BeginPlay.
                    if (index != olympus_help_class_index)
                    {
                        return patched;
                    }
                }
            }
        }
        const uint8_t toy_box_enabled_mask =
            toy_box_enabled_patch_mask_.load(std::memory_order_relaxed);
        if (toy_box_enabled_mask != 0)
        {
            for (size_t index = 0; index < toy_box_gameflow_class_names_.size(); ++index)
            {
                const uint8_t variant_bit = static_cast<uint8_t>(1u << index);
                if ((toy_box_enabled_mask & variant_bit) == 0 ||
                    !fname_equals(*class_name, toy_box_gameflow_class_names_[index]))
                {
                    continue;
                }

                return try_apply_runtime_toy_box_early_bypass(
                    static_cast<RuntimeToyBoxTutorialVariant>(index),
                    static_cast<RC::Unreal::UObject*>(
                        const_cast<RC::Unreal::UObjectBase*>(object)));
            }
        }
        if (observe_olympus_completion_gameflow_patch_.load(std::memory_order_relaxed) &&
            fname_equals(*class_name, olympus_completion_gameflow_class_name_))
        {
            // he_01 is unloaded during the boss arena and rebuilt on the
            // return from he_06. Keep this observer armed so every new class
            // load is patched synchronously before ReceiveBeginPlay can enter
            // the Hero's Origin/tutorial/camp-menu block.
            return try_apply_runtime_olympus_completion_early_bypass(
                static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        }
        if (observe_olympus_early_gameflow_patch_.load(std::memory_order_relaxed) &&
            fname_equals(*class_name, olympus_early_gameflow_class_name_))
        {
            // Continue can reload HE02 after the player-start fallback ran.
            // Stay armed for every newly constructed script, before BeginPlay.
            return try_apply_runtime_olympus_he02_early_bypass(
                static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        }
        if (observe_twilight_town_gameflow_patch_.load(std::memory_order_relaxed) &&
            fname_equals(*class_name, twilight_town_gameflow_class_name_))
        {
            // Continue can reload TT01 after the player-start fallback ran.
            // Stay armed for every newly constructed script, before BeginPlay.
            return try_apply_runtime_twilight_town_early_bypass(
                static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        }
        if (observe_olympus_save_points_gameflow_patch_.load(std::memory_order_relaxed) &&
            fname_equals(*class_name, olympus_save_points_gameflow_class_name_))
        {
            return try_apply_runtime_olympus_save_points_early_bypass(
                static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        }
        if (observe_final_world_gameflow_patches_.load(std::memory_order_relaxed) &&
            (fname_equals(*class_name, final_world_guidance_ew01_gameflow_class_name_) ||
             fname_equals(*class_name, final_world_guidance_ew04_gameflow_class_name_)))
        {
            // A gameflow CDO/live actor can only be constructed after its
            // BlueprintGeneratedClass and ExecuteUbergraph UFunction exist.
            // Patch synchronously here, before ReceiveBeginPlay can cross the
            // guidance gate. Deferring this notification until the next engine
            // tick is too late for the Darkside transition.
            const RuntimeFinalWorldGameflowVariant variant =
                fname_equals(*class_name, final_world_guidance_ew04_gameflow_class_name_)
                    ? RuntimeFinalWorldGameflowVariant::Ew04
                    : RuntimeFinalWorldGameflowVariant::Ew01;
            return try_apply_runtime_final_world_early_bypass(
                variant,
                static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)));
        }
        return std::nullopt;
    }

    auto Kh3RuntimeObjectObserver::observe_constructed_object(const RC::Unreal::UObjectBase* object)
        -> bool
    {
        if (!installed_.load(std::memory_order_relaxed) || !object)
        {
            return false;
        }

        const RC::Unreal::UClass* const object_class = object->GetClassPrivate();
        if (!object_class)
        {
            return false;
        }

        if (const auto observed = observe_created_class(object))
        {
            return *observed;
        }
        const RC::Unreal::FName* const class_name = object_name(object_class);
        if (!class_name)
        {
            return false;
        }
        if (const auto patched = apply_story_patches(object, class_name))
        {
            return *patched;
        }
        if (const auto patched = apply_tutorial_patches(object, class_name))
        {
            return *patched;
        }
        Kh3RuntimeObjectObservationKind kind{};
        if (fname_equals(*class_name, treasure_logic_actor_name_))
        {
            return false; // Native treasure-record setter owns completion; no helper retries.
        }
        else if (observe_festival_dance_story_.load(std::memory_order_relaxed) &&
                 (fname_equals(*class_name, festival_dance_story_class_name_) ||
                  fname_equals(*class_name, festival_dance_story_manager_class_name_)))
        {
            kind = Kh3RuntimeObjectObservationKind::FestivalDanceStoryObjectConstructed;
        }
        else if (observe_beat_of_lead_story_.load(std::memory_order_relaxed) &&
                 fname_equals(*class_name, beat_of_lead_manager_class_name_))
        {
            kind = Kh3RuntimeObjectObservationKind::BeatOfLeadManagerConstructed;
        }
        else if (observe_union_cross_commands_.load(std::memory_order_relaxed) &&
                 fname_equals(*class_name, union_cross_command_class_name_))
        {
            kind = Kh3RuntimeObjectObservationKind::UnionCrossCommandObjectConstructed;
        }
        else if (observe_sora_pawn_ && fname_equals(*class_name, sora_pawn_class_name_))
        {
            kind = Kh3RuntimeObjectObservationKind::SoraPawnConstructed;
        }
        else if (observe_lucky_mark_components_ &&
                 fname_equals(*class_name, lucky_mark_component_class_name_))
        {
            kind = Kh3RuntimeObjectObservationKind::LuckyMarkComponentConstructed;
        }
        else if (!observe_any_open_treasure_state_)
        {
            return false;
        }
        else if (observe_open_treasure_player_state_ &&
                 fname_equals(*class_name, open_treasure_player_state_name_))
        {
            kind = Kh3RuntimeObjectObservationKind::OpenTreasurePlayerState;
        }
        else if (observe_open_treasure_char_state_ &&
                 fname_equals(*class_name, open_treasure_char_state_name_))
        {
            kind = Kh3RuntimeObjectObservationKind::OpenTreasureCharState;
        }
        else
        {
            return false;
        }

        enqueue({kind,
                 static_cast<RC::Unreal::UObject*>(const_cast<RC::Unreal::UObjectBase*>(object)),
                 {}});
        return true;
    }

    auto Kh3RuntimeObjectObserver::OnUObjectArrayShutdown() -> void
    {
        uninstall();
    }

    auto Kh3RuntimeObjectObserver::object_name(const RC::Unreal::UObjectBase* object) const
        -> const RC::Unreal::FName*
    {
        if (!object || object_name_offset_ == 0)
        {
            return nullptr;
        }
        const auto* const object_bytes = reinterpret_cast<const std::byte*>(object);
        return reinterpret_cast<const RC::Unreal::FName*>(object_bytes + object_name_offset_);
    }

    auto Kh3RuntimeObjectObserver::enqueue(Kh3RuntimeObjectObservation observation) -> void
    {
        if (!observation.object)
        {
            return;
        }
        observation.object_item = observation.object->GetObjectItem();
        if (!observation.object_item)
        {
            return;
        }

        std::lock_guard lock{pending_mutex_};
        for (size_t index = 0; index < pending_count_; ++index)
        {
            const auto& existing = pending_[index];
            if (existing.kind == observation.kind && existing.object == observation.object &&
                (observation.kind != Kh3RuntimeObjectObservationKind::NativeItemGrant ||
                 fname_equals(existing.item_key, observation.item_key)))
            {
                return;
            }
        }
        if (pending_count_ >= pending_.size())
        {
            dropped_count_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        pending_[pending_count_++] = observation;
        published_pending_count_.store(pending_count_, std::memory_order_release);
    }
} // namespace kh3ap
