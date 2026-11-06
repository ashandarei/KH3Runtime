#include "kh3ap/features/world/space/space_service_impl.h"
#include "kh3ap/features/world/space/space_profile.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/features/world/space/space_ship_policy.h"
#include "kh3ap/features/world/portals/portal_service.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/World.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <intrin.h>
#include <algorithm>
#include <cstring>
#include <sstream>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using space::native_profile;

    namespace
    {
        constexpr uintptr_t waypoint_id_offset = 0x4fc;
        constexpr uintptr_t waypoint_active_offset = 0x4f4;
        constexpr uint16_t first_waypoint_discovery_mask = 0x8000;
        constexpr size_t first_embark_symbol_offset = 8;
        constexpr size_t first_embark_map_offset = 0x10;
        constexpr size_t first_embark_tag_offset = 0x18;
        constexpr int32_t native_fade_out = 1;

        // Native overview-cache order differs from the world-symbol enum order.
        constexpr std::array<std::wstring_view, 9> overview_world_codes{
            L"he", L"tt", L"ts", L"ra", L"mi", L"fz", L"bx", L"ca", L"kg"};
    } // namespace

    void RuntimeSpaceService::Impl::travel_begin_hook(UObject* actor)
    {
        auto& self = *active;
        // Correct the preset actor state before native BeginPlay initializes
        // proximity detection and visuals. Never prefill saved discoveries.
        auto* current = self.world();
        if (is_uobject_valid_fast(actor) && is_uobject_valid_fast(current))
        {
            auto* id = actor->GetPropertyByNameInChain(L"m_WorldMapID");
            auto* state = actor->GetPropertyByNameInChain(L"m_ActiveCheck");
            auto* game = object_field(current, L"OwningGameInstance");
            if (id && id->GetOffset_Internal() == waypoint_id_offset &&
                id->GetElementSize() == sizeof(uint8_t) && state && state->IsA<FBoolProperty>() &&
                state->GetOffset_Internal() == waypoint_active_offset &&
                is_uobject_valid_fast(game) &&
                game->GetClassPrivate()->GetName() == L"TresGameInstance")
            {
                const auto point = *id->ContainerPtrToValuePtr<uint8_t>(actor);
                if (point < space::travel_tags.size())
                {
                    uint16_t flags{};
                    std::memcpy(&flags,
                                reinterpret_cast<const uint8_t*>(game) +
                                    native_profile().travel_discovery_flags,
                                sizeof(flags));
                    if (space::rearm_initial_travel_point(
                            self.access.access_ready, point, current->GetFullName(),
                            (flags & (first_waypoint_discovery_mask >> point)) != 0))
                    {
                        // Streamed actors' UE4SS GetWorld can resolve their package
                        // template. Enumerate the current world's actors once at
                        // BeginPlay instead of accepting a path or cached pointer.
                        std::vector<AActor*> actors;
                        UGameplayStatics::GetAllActorsOfClass(game, actor->GetClassPrivate(),
                                                              actors);
                        if (std::find(actors.begin(), actors.end(), actor) != actors.end())
                        {
                            static_cast<FBoolProperty*>(state)->SetPropertyValueInContainer(actor,
                                                                                            false);
                            Output::send<LogLevel::Verbose>(
                                STR("[KH3APProbeCpp] Rearmed undiscovered starting waypoint "
                                    "id={}\n"),
                                point);
                        }
                    }
                }
            }
        }
        reinterpret_cast<void (*)(UObject*)>(self.travel_begin.original)(actor);
    }

    void RuntimeSpaceService::Impl::travel_query_hook(UObject* icon, FFrame& stack, void* result)
    {
        auto& self = *active;
        reinterpret_cast<TravelQuery>(self.travel_query.original)(icon, stack, result);
        // Blueprint presentation uses a separate visited-state query.
        // Let the original thunk consume its bytecode exactly once.
        if (result && is_unlocked_travel_icon(icon))
        {
            *static_cast<uint8_t*>(result) = self.can_travel(travel_icon_zone(icon)) ? 1 : 0;
        }
    }

    void RuntimeSpaceService::Impl::apply_overview(UObject* manager)
    {
        if (!is_uobject_valid_fast(manager))
        {
            return;
        }
        auto* anchor = manager->GetPropertyByNameInChain(L"m_pController");
        if (!anchor || anchor->GetOffset_Internal() != native_profile().overview_anchor ||
            anchor->GetElementSize() != sizeof(UObject*))
        {
            return;
        }
        // Native 1b2e700 maps world codes to this transient menu array.
        // 0=unavailable, 1=first visit, 3=visited, 4=cleared. Preserve
        // the Garden portal's exact first-visit/progressed boundary.
        auto* bytes = reinterpret_cast<uint8_t*>(manager);
        for (size_t i = 0; i < overview_world_codes.size(); ++i)
        {
            auto* state = reinterpret_cast<int32_t*>(
                bytes + native_profile().overview_world_states + sizeof(int32_t) * i);
            const bool allowed = can_land(overview_world_codes[i]);
            *state = space::landing_world_state(
                allowed, allowed ? landing_ui.progressed(overview_world_codes[i]) : std::nullopt,
                *state);
        }
        // These are menu caches, not saved world-clear flags. Native setup
        // builds the space selector from this count/list; keep CS untouched.
        for (size_t i = 0; i < space::travel_maps.size(); ++i)
        {
            *reinterpret_cast<int32_t*>(bytes + native_profile().overview_region_states +
                                        sizeof(int32_t) * i) = 1;
            bytes[native_profile().overview_region_ids + i] = static_cast<uint8_t>(i);
        }
        *reinterpret_cast<int32_t*>(bytes + native_profile().overview_region_count) =
            static_cast<int32_t>(space::travel_maps.size());
    }

    bool RuntimeSpaceService::Impl::overview_hook(UObject* manager)
    {
        auto& self = *active;
        const bool ready = reinterpret_cast<Overview>(self.overview.original)(manager);
        if (ready)
        {
            self.apply_overview(manager);
            // Native setup can reuse the same manager. Its initial selection
            // still needs a fresh cursor cache after progression is applied.
            self.overview_refresh.request();
            if (self.observed_overview.Get() != manager)
            {
                RuntimeSpaceService::notify_context_changed();
                self.observed_overview = manager;
            }
        }
        return ready;
    }

    void RuntimeSpaceService::Impl::overview_default_hook(UObject* manager)
    {
        auto& self = *active;
        reinterpret_cast<void (*)(UObject*)>(self.overview_default.original)(manager);
        if (!is_uobject_valid_fast(manager) ||
            self.world_name() != L"World /Game/Levels/wm/wm_01/wm_01.wm_01")
        {
            return;
        }
        auto* anchor = manager->GetPropertyByNameInChain(L"m_pController");
        if (!anchor || anchor->GetOffset_Internal() != native_profile().overview_anchor ||
            anchor->GetElementSize() != sizeof(UObject*))
        {
            return;
        }
        // Embark restores its remembered cursor separately from manager setup.
        // Apply the existing Garden progress policy before the native caller
        // builds that cursor's route, including when the manager is reused.
        self.apply_overview(manager);
        self.overview_refresh.request();
        auto* bytes = reinterpret_cast<uint8_t*>(manager);
        auto replace_locked = [&](unsigned region, uint8_t& kind, int32_t& id)
        {
            const auto code = space::world_symbol_code(kind, static_cast<unsigned>(id));
            const int replacement = space::overview_default_fallback(
                region, kind, static_cast<unsigned>(id), self.can_land(code));
            if (replacement < 0)
            {
                return;
            }
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Space default selection region={} world={} travel_point={}\n"),
                region, code, replacement);
            kind = space::travel_point_kind;
            id = replacement;
        };
        for (unsigned region = 0; region < space::travel_maps.size(); ++region)
        {
            replace_locked(region, bytes[native_profile().overview_remembered_kinds + region],
                           *reinterpret_cast<int32_t*>(bytes +
                                                       native_profile().overview_remembered_ids +
                                                       sizeof(int32_t) * region));
        }
        const int selected_region =
            *reinterpret_cast<int32_t*>(bytes + native_profile().overview_region);
        if (selected_region >= 0 &&
            selected_region < static_cast<int32_t>(space::travel_maps.size()))
        {
            replace_locked(
                bytes[native_profile().overview_region_ids + selected_region],
                bytes[native_profile().overview_current_kind],
                *reinterpret_cast<int32_t*>(bytes + native_profile().overview_current_id));
        }
        // Native per-region initialization now resolves the travel-point
        // actor, cursor kind and cursor ID together. No saved location edits.
    }

    void RuntimeSpaceService::Impl::save_hook(void* owner, const FString& map, const FString& tag,
                                              int32_t world_code, FName display)
    {
        auto& self = *active;
        const auto original = reinterpret_cast<Save>(self.save.original);
        const auto source = self.world_name();
        if (!self.redirect(source, map.GetCharArray()) ||
            self.save_menu_return.allows(source, map.GetCharArray(), tag.GetCharArray(),
                                         std::chrono::steady_clock::now()))
        {
            original(owner, map, tag, world_code, display);
            return;
        }
        // Match the real GoA NormalPlayerStart: RG world, RG03 display name,
        // NormalSpawn tag. Keep the native save, ordering, and story flags.
        FString destination(space::garden.data());
        FString start(space::garden_start.data());
        original(owner, destination, start, space::garden_world_code,
                 FName(space::garden_display.data(), FNAME_Add));
        destination.Clear();
        start.Clear();
        ++self.redirected_saves;
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] Space checkpoint redirected source={} original_target={} "
                "destination=GoA_01 tag=NormalSpawn world=24 display=rg_03_An_01\n"),
            source, map.GetCharArray());
    }

    bool RuntimeSpaceService::Impl::first_embark_hook(UObject* dataset, int32_t world_code,
                                                      void* result)
    {
        auto& self = *active;
        const bool found = reinterpret_cast<bool (*)(UObject*, int32_t, void*)>(
            self.first_embark.original)(dataset, world_code, result);
        const auto destination = space::first_embark_destination(world_code);
        if (!found || !result || destination.code.empty() || !self.can_land(destination.code) ||
            self.world_name() != L"World /Game/Levels/wm/wm_01/wm_01.wm_01")
        {
            return found;
        }
        auto* row = static_cast<uint8_t*>(result);
        if (row[first_embark_symbol_offset] != destination.symbol_id)
        {
            return found;
        }
        // Change the returned copy, never the shared table or discovery/story
        // flags. The normal embark guard still checks the selected Keyblade.
        *reinterpret_cast<FName*>(row + first_embark_map_offset) =
            FName(destination.map.data(), FNAME_Add);
        *reinterpret_cast<FName*>(row + first_embark_tag_offset) =
            FName(destination.tag.data(), FNAME_Add);
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] Space first embark selected={} target={} tag={}\n"),
            destination.code, destination.map, destination.tag);
        return found;
    }

    bool RuntimeSpaceService::Impl::jump_hook(void* owner, FName map, FName tag, int32_t fade)
    {
        auto& self = *active;
        const auto source = self.world_name();
        const auto target = map.ToString();
        // The overview may be inspected without a ship. Flight and world embark
        // require a delivered AP blueprint assigned to the destination zone.
        // Ordinary-world returns still use the existing Garden redirection.
        if (self.access.include_space)
        {
            if (auto* context = self.ship_inventory_context())
            {
                self.ships.reconcile(context, self.access);
            }
            const auto zone = space::flight_zone(target);
            if (target.starts_with(L"/Game/Levels/gm/") && !self.redirect(source, target) &&
                (zone < 0 || !self.can_travel(zone)))
            {
                ++self.denied_landings;
                ++self.denied_ship_travel;
                self.status_pending = true;
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Space travel denied target={} zone={} "
                        "reason=blueprint_and_world_completion_required\n"),
                    target, zone);
                RuntimePortalService::on_map_jump_canceled();
                return false;
            }
        }
        const bool explicit_return = self.save_menu_return.allows(source, target, tag.ToString(),
                                                                  std::chrono::steady_clock::now());
        if (!explicit_return)
        {
            self.save_menu_return.clear();
        }
        if (target == space::world_map && source.find(space::garden) != source.npos)
        {
            self.space_journey = true;
        }
        if (self.redirect(source, target) && !explicit_return)
        {
            map = FName(space::garden.data(), FNAME_Add);
            tag = FName(space::garden_start.data(), FNAME_Add);
            ++self.redirected_jumps;
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Space hub return redirected source={} original_target={} "
                    "destination=GoA_01\n"),
                source, target);
        }
        else if (space::is_space(source) || (self.space_journey && space::is_entry_scene(source)))
        {
            const auto code = space::destination_code(target);
            const auto embark_code = space::world_embark_code(target, tag.ToString());
            if (!embark_code.empty() && !self.can_land(embark_code))
            {
                ++self.denied_landings;
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Space embark denied target={} "
                                                    "tag={} reason=ap_world_access\n"),
                                                target, tag.ToString());
                RuntimePortalService::on_map_jump_canceled();
                return false;
            }
            // GoA, space navigation and the standalone Bistro are services.
            // All ordinary world landings require the AP receipt gate, even
            // when the native menu or a first-visit route bypasses CheckLanding.
            if (space::destination_requires_keyblade(target) &&
                !space::entry_scene_transition(self.space_journey, source, target) &&
                !self.can_land(code))
            {
                ++self.denied_landings;
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Space landing denied target={} reason=ap_world_access\n"),
                    target);
                RuntimePortalService::on_map_jump_canceled();
                return false;
            }
        }
        // Native travel (camp/save returns, Gummi, landings) bypasses the
        // Blueprint TresMapJump hook. Release map-owned roots before the game
        // starts unloading; waiting for a missing pawn is already too late.
        RuntimePortalService::on_map_jump();
        RuntimeSpaceService::notify_context_changed();
        const bool accepted = reinterpret_cast<Jump>(self.jump.original)(owner, map, tag, fade);
        if (!accepted)
        {
            RuntimePortalService::on_map_jump_canceled();
        }
        return accepted;
    }

    bool RuntimeSpaceService::Impl::landing_hook(UObject* actor)
    {
        auto& self = *active;
        if (!self.can_land(self.actor_world(actor)) || !self.landing_ui.ready())
        {
            return false;
        }
        self.apply_overview(self.landing_manager());
        return reinterpret_cast<Landing>(self.landing.original)(actor);
    }

    void RuntimeSpaceService::Impl::landing_list_hook(UObject* menu, int32_t world_code)
    {
        auto& self = *active;
        reinterpret_cast<void (*)(UObject*, int32_t)>(self.landing_list.original)(menu, world_code);
        if (space::is_space(self.world_name()))
        {
            self.landing_ui.build_list(menu, world_code);
        }
    }

    void RuntimeSpaceService::Impl::lock_hook(UObject* actor, bool locked)
    {
        auto& self = *active;
        // AP ownership replaces ordinary world progression locks. Boss locks
        // use LockWorldByBoss and remain controlled by the current encounter.
        reinterpret_cast<Lock>(self.lock.original)(actor, !self.can_land(self.actor_world(actor)));
        const auto found = std::find_if(self.flight_symbols.begin(), self.flight_symbols.end(),
                                        [actor](const auto& symbol)
                                        {
                                            return symbol.actor.Get() == actor;
                                        });
        self.status_pending = true;
        if (found == self.flight_symbols.end())
        {
            self.flight_refresh.request();
            self.context_refresh.request();
        }
        else if (auto state = self.read_flight_symbol(actor))
        {
            *found = std::move(*state);
        }
    }

    bool RuntimeSpaceService::Impl::mission_hook(UObject* manager, uint8_t id, void* counters)
    {
        auto& self = *active;
        const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        const bool completed =
            reinterpret_cast<Mission>(self.mission.original)(manager, id, counters);
        // TickComponent calls this only for uncleared missions and commits
        // the completion bit immediately after true. Defer IO until our next
        // game-thread tick; never invoke the check, rewards, or delegates.
        if (completed && id < self.pending_missions.size() &&
            caller == reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) +
                          native_profile().mission_completion_caller_rva)
        {
            // The caller awards mission loot after true. Suppress a newly
            // loaded table here, even if it arrived after map readiness.
            self.rewards.prepare_mission_rewards();
            self.pending_missions[id] = true;
            self.context_refresh.request();
        }
        return completed;
    }

    void RuntimeSpaceService::Impl::install_once()
    {
        if (attempted)
        {
            return;
        }
        attempted = true;
        const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
        {
            return;
        }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        {
            return;
        }
        const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto& profile = native_profile();
        const auto signature_matches = [base](uintptr_t offset, const auto& expected)
        {
            return std::memcmp(base + offset, expected.data(), expected.size()) == 0;
        };
        if (!profile.supported || pe->Signature != IMAGE_NT_SIGNATURE ||
            pe->FileHeader.TimeDateStamp != profile.image_timestamp ||
            pe->OptionalHeader.SizeOfImage != profile.image_size ||
            !signature_matches(profile.save_rva, profile.save_bytes) ||
            !signature_matches(profile.jump_rva, profile.jump_bytes) ||
            !signature_matches(profile.landing_rva, profile.landing_bytes) ||
            !signature_matches(profile.lock_rva, profile.lock_bytes) ||
            !signature_matches(profile.mission_rva, profile.mission_bytes) ||
            !signature_matches(profile.overview_rva, profile.overview_bytes) ||
            !signature_matches(profile.icon_state_rva, profile.icon_state_bytes) ||
            !signature_matches(profile.travel_begin_rva, profile.travel_begin_bytes) ||
            !signature_matches(profile.travel_query_rva, profile.travel_query_bytes) ||
            !signature_matches(profile.save_menu_rebuild_rva, profile.save_menu_rebuild_bytes) ||
            !signature_matches(profile.landing_list_rva, profile.landing_list_bytes) ||
            !signature_matches(profile.first_embark_rva, profile.first_embark_bytes) ||
            !signature_matches(profile.overview_default_rva, profile.overview_default_bytes))
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Space routing unavailable: unsupported image/signatures\n"));
            return;
        }
        active = this;
        const auto address = reinterpret_cast<uintptr_t>(base);
        installed =
            save.install(address + profile.save_rva, reinterpret_cast<uintptr_t>(&save_hook)) &&
            jump.install(address + profile.jump_rva, reinterpret_cast<uintptr_t>(&jump_hook)) &&
            landing.install(address + profile.landing_rva,
                            reinterpret_cast<uintptr_t>(&landing_hook)) &&
            lock.install(address + profile.lock_rva, reinterpret_cast<uintptr_t>(&lock_hook)) &&
            mission.install(address + profile.mission_rva,
                            reinterpret_cast<uintptr_t>(&mission_hook)) &&
            overview.install(address + profile.overview_rva,
                             reinterpret_cast<uintptr_t>(&overview_hook)) &&
            icon_state.install(address + profile.icon_state_rva,
                               reinterpret_cast<uintptr_t>(&icon_state_hook)) &&
            travel_begin.install(address + profile.travel_begin_rva,
                                 reinterpret_cast<uintptr_t>(&travel_begin_hook)) &&
            travel_query.install(address + profile.travel_query_rva,
                                 reinterpret_cast<uintptr_t>(&travel_query_hook)) &&
            landing_list.install(address + profile.landing_list_rva,
                                 reinterpret_cast<uintptr_t>(&landing_list_hook)) &&
            first_embark.install(address + profile.first_embark_rva,
                                 reinterpret_cast<uintptr_t>(&first_embark_hook)) &&
            overview_default.install(address + profile.overview_default_rva,
                                     reinterpret_cast<uintptr_t>(&overview_default_hook));
        if (!installed)
        {
            overview_default.reset();
            first_embark.reset();
            landing_list.reset();
            travel_begin.reset();
            travel_query.reset();
            icon_state.reset();
            overview.reset();
            mission.reset();
            lock.reset();
            landing.reset();
            jump.reset();
            save.reset();
            active = nullptr;
        }
        if (installed)
        {
            install_encounter_events();
            // Use each storefront's verified presentation signatures and ABI;
            // native navigation and travel policies remain with their owners.
            const auto& embark_profile = native_profile();
            if (!std::memcmp(base + embark_profile.embark_help_rva,
                             embark_profile.embark_help_bytes.data(),
                             embark_profile.embark_help_bytes.size()) &&
                !std::memcmp(base + embark_profile.embark_focus_help_rva,
                             embark_profile.embark_focus_help_bytes.data(),
                             embark_profile.embark_focus_help_bytes.size()))
            {
                embark_help.install(address + embark_profile.embark_help_rva,
                                    reinterpret_cast<uintptr_t>(&embark_help_hook));
                embark_focus_help.install(
                    address + embark_profile.embark_focus_help_rva,
                    embark_profile.steam
                        ? reinterpret_cast<uintptr_t>(&embark_steam_focus_help_hook)
                        : reinterpret_cast<uintptr_t>(&embark_focus_help_hook));
                if (embark_help.hook && embark_focus_help.hook &&
                    !std::memcmp(base + embark_profile.embark_selector_rva,
                                 embark_profile.embark_selector_bytes.data(),
                                 embark_profile.embark_selector_bytes.size()) &&
                    !std::memcmp(base + embark_profile.embark_input_rva,
                                 embark_profile.embark_input_bytes.data(),
                                 embark_profile.embark_input_bytes.size()))
                {
                    if (!embark_input.install(
                            address + embark_profile.embark_input_rva,
                            embark_profile.steam
                                ? reinterpret_cast<uintptr_t>(&embark_steam_input_hook)
                                : reinterpret_cast<uintptr_t>(&embark_input_hook)) ||
                        !embark_selector.install(
                            address + embark_profile.embark_selector_rva,
                            reinterpret_cast<uintptr_t>(&embark_selector_hook)))
                    {
                        embark_selector.reset();
                        embark_input.reset();
                    }
                }
            }
            RuntimeSpaceService::notify_context_changed();
        }
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] Space routing installed={} storefront={} mode=origin_scoped "
                "hub_checkpoint_metadata=GoA preserve_space_saves=true rewards=vanilla\n"),
            installed, profile.steam ? STR("steam") : STR("egs"));
    }

    void RuntimeSpaceService::Impl::tick(std::chrono::steady_clock::time_point now)
    {
        if (!installed)
        {
            return;
        }
        // Independent of the Sora update coordinator and its title/map
        // gates. No file stats or channel locks when IPC has not changed.
        const auto generation = pipe::input_generation();
        if (generation != consumed_input_generation)
        {
            consumed_input_generation = generation;
            access_refresh_pending = true;
        }
        if (access_refresh_pending)
        {
            access_refresh_pending = false;
            const auto prior_allowed = access.allowed;
            const auto prior_items = access.items;
            const auto prior_seed = access.seed;
            const bool prior_ready = access.access_ready;
            const bool prior_include_space = access.include_space;
            access.refresh(
                [this](const auto& directory, const auto& seed_name, auto slot, bool enabled)
                {
                    rewards.configure(directory, seed_name, slot, enabled);
                });
            if (prior_items != access.items || prior_seed != access.seed ||
                prior_ready != access.access_ready)
            {
                ships_refresh_pending = true;
            }
            if (access.access_ready && !access.include_space)
            {
                ships_refresh_pending = false;
            }
            if (prior_ready != access.access_ready || prior_seed != access.seed ||
                prior_allowed != access.allowed || prior_include_space != access.include_space ||
                ships_refresh_pending)
            {
                flight_refresh.request();
                overview_refresh.request();
                context_refresh.request();
                context_deadline = now + space::context_refresh_timeout;
            }
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        // Kernel directory notifications wake diagnostic requests. No timed
        // request-file stat/read loop, and no readiness work for unrelated IO.
        if (command_changes == INVALID_HANDLE_VALUE && !access.bridge.empty())
        {
            command_changes = FindFirstChangeNotificationW(access.bridge.c_str(), FALSE,
                                                           FILE_NOTIFY_CHANGE_FILE_NAME |
                                                               FILE_NOTIFY_CHANGE_LAST_WRITE);
        }
        if (command_changes != INVALID_HANDLE_VALUE &&
            WaitForSingleObject(command_changes, 0) == WAIT_OBJECT_0)
        {
            FindNextChangeNotification(command_changes);
            if (read_text_file(access.bridge / L"runtime_space_request.json"))
            {
                context_refresh.request();
                process_command();
            }
        }
        if (shadow_test)
        {
            poll_shadow_test(now);
        }
        if (protected_ship.Get() || battle_input_controller.Get() ||
            !enabled_battle_fixtures.empty())
        {
            poll_ship_protection(now);
        }
#endif
        poll_encounter_events(now);
        if (!context_refresh.take(now))
        {
            if (rewards.pending())
            {
                rewards.tick(presentation_world.Get(), loading_closed);
            }
            flush_presentation(now);
            if (!status_pending)
            {
                return;
            }
            publish_status();
            return;
        }
        ++context_samples;
        status_pending = true;
        auto* live_world = world();
        const auto live_world_name = world_name();
        if (presentation_world.Get() != live_world || current_world != live_world_name)
        {
            presentation_world = live_world;
            space_controller = nullptr;
            delivery_controller = nullptr;
            flight_symbols.clear();
            observed_overview = nullptr;
            selector_menu = nullptr;
            selector_help_visible = false;
            selector_help_text.clear();
            selector_help_zone = -1;
            embark_denied_until = {};
            presentation_ready = false;
            rewards.request_recovery();
            flight_refresh.request();
            overview_refresh.request();
        }
        current_world = live_world_name;
        save_menu_return.observe(current_world, now);
        const bool was_loading_closed = loading_closed;
        sample_space();
        if (ships_refresh_pending && access.access_ready && access.include_space)
        {
            if (auto* context = ship_inventory_context())
            {
                const auto previous_zones = ships.unlocked_zones(access);
                if (ships.reconcile(context, access))
                {
                    ships_refresh_pending = false;
                }
                if (previous_zones != ships.unlocked_zones(access))
                {
                    overview_refresh.request();
                    flight_refresh.request();
                }
            }
        }
        if (loading_closed && !was_loading_closed)
        {
            rewards.request_recovery();
        }
        // Reuse the loading query just made by sample_space. External
        // grants and presentation mutations still validate independently.
        auto* controller = delivery_context(false, false);
        // Current possession/loading checks determine readiness immediately;
        // there is no additional timed stability window for space services.
        delivery_controller = controller;
        const bool ready = controller != nullptr;
        if (landing_ui.tick(ready ? controller : nullptr, space::is_space(current_world)))
        {
            // The Garden assets can arrive after native overview setup. Their
            // readiness invalidates the earlier first-visit fallback once.
            // Flight evaluates progress in the actual landing hook instead.
            overview_refresh.request();
        }
        const auto shown_before = notifications.shown();
        notifications.tick(ready ? controller : nullptr, space::is_space(current_world),
                           current_world == L"World /Game/Levels/wm/wm_01/wm_01.wm_01");
        // A large finite receipt batch can outlast the readiness timeout.
        // Actual banner progress extends its work window; idle never arms it.
        if (notifications.shown() != shown_before)
        {
            context_deadline = now + space::context_refresh_timeout;
        }
        if (ready && !presentation_ready)
        {
            flight_refresh.request();
            overview_refresh.request();
        }
        presentation_ready = ready;
        if (space::is_space(current_world))
        {
            space_journey = true;
        }
        else if (space::is_ordinary_world(current_world) && !space::is_entry_scene(current_world))
        {
            space_journey = false;
        }
        flush_presentation(now);
        // Only unfinished event work may retry. Success disarms it completely.
        context_refresh.finish(now,
                               now >= context_deadline ||
                                   (!space::is_space(current_world) && !ships_refresh_pending) ||
                                   (ready && landing_ui.ready() && notifications.pending() == 0));
        if (access.bridge.empty())
        {
            return;
        }
        for (size_t id = 0; id < pending_missions.size(); ++id)
        {
            if (!pending_missions[id])
            {
                continue;
            }
            rewards.mission_completed(static_cast<uint8_t>(id));
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            std::ostringstream event;
            event << "{\"schema\":\"kh3ap.space_completion.v1\",\"event\":\"mission_completed\","
                     "\"mission_id\":"
                  << id << ",\"seed_name\":\"" << json_escape(access.seed)
                  << "\",\"process_id\":" << GetCurrentProcessId()
                  << ",\"source\":\"native_mission_check\",\"ap_check_sent\":false,\"at\":\""
                  << now_iso8601_utc() << "\"}\n";
            append_text_file(access.bridge / L"runtime_space_events.jsonl", event.str());
#endif
            pending_missions[id] = false;
            ++mission_events;
        }
        rewards.tick(live_world, loading_closed);
        publish_status();
    }

    void RuntimeSpaceService::Impl::publish_status()
    {
        const RuntimeHotPathProfileScope profile{L"space:publish_status"};
        if (access.bridge.empty())
        {
            return;
        }
        const bool ready = presentation_ready;
        std::ostringstream json;
        json << "{\"schema\":\"kh3ap.space_runtime.v1\",\"process_id\":" << GetCurrentProcessId()
             << ",\"routing_installed\":true,\"access_ready\":"
             << (access.access_ready ? "true" : "false") << ",\"seed_name\":\""
             << json_escape(access.seed) << "\",\"world\":\"" << json_escape(current_world)
             << "\",\"space_active\":" << (space::is_space(current_world) ? "true" : "false")
             << ",\"loading_screen_closed\":" << (loading_closed ? "true" : "false")
             << ",\"controller_ready\":" << (controller_ready ? "true" : "false")
             << ",\"inventory_delivery_version\":2,\"inventory_delivery_ready\":"
             << (ready ? "true" : "false") << ",\"all_travel_points_available\":"
             << (ships.unlocked_zones(access) == space::all_ship_zones ? "true" : "false")
             << ",\"travel_point_enable_events\":" << travel_points_enabled
             << ",\"world_fast_travel_enable_events\":" << world_fast_travel_enabled
             << ",\"overview_access_refreshes\":" << overview_refreshes
             << ",\"item_banners_shown\":" << notifications.shown()
             << ",\"item_banners_pending\":" << notifications.pending()
             << ",\"flight_visibility_updates\":" << flight_visibility_updates
             << ",\"flight_symbol_discoveries\":" << flight_discoveries
             << ",\"flight_world_symbols\":";
        write_flight_symbols(json, flight_symbols);
        json << ",\"landing_menu\":";
        landing_ui.write_status(json);
        json << ",\"ships\":";
        ships.write_status(json, access);
        json << ",\"redirected_checkpoints\":" << redirected_saves
             << ",\"redirected_jumps\":" << redirected_jumps
             << ",\"denied_landings\":" << denied_landings
             << ",\"denied_ship_travel\":" << denied_ship_travel
             << ",\"mission_completion_events\":" << mission_events << ",\"allowed_worlds\":[";
        bool first = true;
        for (const auto& code : access.allowed)
        {
            if (!first)
            {
                json << ',';
            }
            first = false;
            json << '"' << json_escape(code) << '"';
        }
        json << "],\"rewards\":\"" << (rewards.active() ? "archipelago" : "vanilla") << "\"";
        const auto serialized_state = json.str();
        status_pending = false;
        if (serialized_state == published_status)
        {
            return;
        }
        // Observation counters describe this publication; they are not a
        // gameplay-state change and must not force another disk write.
        json << ",\"context_samples\":" << context_samples
             << ",\"presentation_attempts\":" << presentation_attempts
             << ",\"status_publications\":" << status_publications + 1 << ",\"updated_at_utc\":\""
             << now_iso8601_utc() << "\"}\n";
        // Publish changed event state only; timestamps are not a lease.
        const auto status_path = access.bridge / L"runtime_space.json";
        const auto temporary = access.bridge / L"runtime_space.json.tmp";
        if (write_text_file(temporary, json.str()) &&
            MoveFileExW(temporary.c_str(), status_path.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            published_status = serialized_state;
            ++status_publications;
        }
        else
        {
            status_pending = true;
        }
    }

    void RuntimeSpaceService::Impl::flush_presentation(std::chrono::steady_clock::time_point now)
    {
        // A closed/loading/paused context waits for its next lifecycle event.
        if (!presentation_ready)
        {
            return;
        }
        // Idle frames read two flags only. Unsafe contexts retry only
        // while an event is pending; returning to a ready controller also
        // queues one bootstrap for the newly loaded world.
        if (flight_refresh.take(now))
        {
            ++presentation_attempts;
            const bool symbols_ready = refresh_flight_symbols(now);
            const bool encounters_ready = refresh_story_encounters();
            flight_refresh.finish(now, symbols_ready && encounters_ready);
            status_pending = true;
        }
        if (overview_refresh.take(now))
        {
            ++presentation_attempts;
            status_pending = true;
            const bool in_overview = current_world == L"World /Game/Levels/wm/wm_01/wm_01.wm_01";
            if (in_overview && embark_selector.hook)
            {
                refresh_selector_help(overview_manager());
            }
            overview_refresh.finish(now, !in_overview || refresh_open_overview(overview_manager()));
        }
    }

    RuntimeSpaceService::RuntimeSpaceService(
        std::function<std::optional<std::filesystem::path>()> bridge,
        std::function<UObject*()> sora_pawn)
        : impl_(std::make_unique<Impl>(std::move(bridge), std::move(sora_pawn)))
    {
    }
    RuntimeSpaceService::~RuntimeSpaceService() = default;
    void RuntimeSpaceService::install_once()
    {
        impl_->install_once();
    }
    void RuntimeSpaceService::tick(std::chrono::steady_clock::time_point now)
    {
        impl_->tick(now);
    }
    bool RuntimeSpaceService::is_space_active()
    {
        return Impl::active && space::is_space(Impl::active->world_name());
    }
    bool RuntimeSpaceService::return_from_closed_menu(std::wstring_view expected_world)
    {
        auto* self = Impl::active;
        if (!self || !self->installed || self->world_name() != expected_world ||
            space::is_space(expected_world) || !self->library_bool(L"TresUI_IsClosedLoadingScreen"))
        {
            return false;
        }
        auto* instance = Impl::object_field(self->world(), L"OwningGameInstance");
        if (!instance ||
            *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(instance) +
                                        native_profile().game_instance_pause_state) != 0)
        {
            return false;
        }
        // Keep the exact request authorized through native fade/deferred dispatch.
        // Routing rejects every other destination and clears it on world change.
        const bool previous = self->space_journey;
        self->space_journey = true;
        self->save_menu_return.arm(expected_world, std::chrono::steady_clock::now());
        notify_context_changed();
        const bool result = Impl::jump_hook(
            instance, FName(space::SaveMenuReturn::return_map.data(), FNAME_Add),
            FName(space::SaveMenuReturn::return_tag.data(), FNAME_Add), native_fade_out);
        if (!result)
        {
            self->space_journey = previous;
            self->save_menu_return.clear();
        }
        return result;
    }
    UObject* RuntimeSpaceService::item_delivery_context()
    {
        return Impl::active ? Impl::active->delivery_context(true) : nullptr;
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    UObject* RuntimeSpaceService::diagnostic_flight_pawn(bool flight_only)
    {
        auto* self = Impl::active;
        if (!self || self->current_world != self->world_name() ||
            !space::inventory_delivery_world(self->current_world) ||
            (flight_only && self->current_world.find(L"/Game/Levels/gm/") == std::wstring::npos))
        {
            return nullptr;
        }
        auto* controller = self->space_controller.Get();
        auto* pawn = Impl::object_field(controller, L"Pawn");
        if (!is_uobject_valid_fast(controller) || !is_uobject_valid_fast(pawn) ||
            static_cast<AActor*>(controller)->GetWorld() != self->world() ||
            static_cast<AActor*>(pawn)->GetWorld() != self->world() ||
            Impl::object_field(pawn, L"Controller") != controller)
        {
            return nullptr;
        }
        return pawn;
    }
#endif
    void RuntimeSpaceService::notify_item_received(std::wstring_view token, bool show_notification)
    {
        if (Impl::active && token.starts_with(L"GUMISHIPBP_ITEM"))
        {
            Impl::active->ships_refresh_pending = Impl::active->access.include_space;
            Impl::active->context_refresh.request();
            Impl::active->context_deadline =
                std::chrono::steady_clock::now() + space::context_refresh_timeout;
        }
        if (Impl::active && show_notification && item_delivery_context())
        {
            Impl::active->notifications.enqueue(token);
            Impl::active->context_deadline =
                std::chrono::steady_clock::now() + space::context_refresh_timeout;
            Impl::active->context_refresh.request();
        }
    }
    void RuntimeSpaceService::notify_context_changed()
    {
        if (!Impl::active)
        {
            return;
        }
        auto& self = *Impl::active;
        self.delivery_controller = nullptr;
        self.presentation_ready = false;
        self.status_pending = true;
        self.context_refresh.request();
        self.context_deadline = std::chrono::steady_clock::now() + space::context_refresh_timeout;
        if (self.access.bridge.empty())
        {
            self.access_refresh_pending = true;
        }
    }
    void RuntimeSpaceService::notify_map_ready()
    {
        if (!Impl::active)
        {
            return;
        }
        notify_context_changed();
        // Vanilla-space seeds have no ship inventory work to reconcile.
        // Before access is ready, its eventual input event requests the work.
        Impl::active->ships_refresh_pending = Impl::active->access.include_space;
        Impl::active->ships.inventory_verified = false;
        Impl::active->rewards.request_recovery();
        Impl::active->flight_refresh.request();
        Impl::active->overview_refresh.request();
    }
} // namespace kh3ap
