#include <chrono>
#include "kh3ap/features/player/character_ids.h"
#include <utility>
#include "kh3ap/features/world/portals/portal_policy.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/characters/character_trap_state.h"
#include "kh3ap/bridge/rewards/trap_queue.h"
#include "kh3ap/bridge/session/deathlink.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/grants/grant_safety.h"
#include "kh3ap/features/grants/trap_catalog.h"
#include "kh3ap/features/player/characters/character_swap_stats.h"
#include "kh3ap/features/player/combat/character_attack_completion.h"
#include "kh3ap/features/player/combat/character_union_cross.h"
#include "kh3ap/features/player/characters/character_cinematic_binding.h"
#include "kh3ap/features/player/characters/character_travel_policy.h"
#include "kh3ap/features/player/characters/character_ascension_state.h"
#include "kh3ap/features/player/characters/character_unlocks.h"
#include "kh3ap/features/player/characters/character_pawn_pool.h"
#include "kh3ap/features/player/characters/character_swap_assets.h"
#include "kh3ap/features/player/characters/character_swap_effects.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/characters/character_camera.h"
#include "kh3ap/features/player/movement/character_rails.h"
#include "kh3ap/features/player/combat/character_magic.h"
#include "kh3ap/features/player/combat/character_style.h"
#include "kh3ap/features/player/combat/character_shotlock.h"
#include "kh3ap/features/player/combat/character_underwater_combat.h"
#include "kh3ap/features/player/characters/character_party.h"
#include "kh3ap/features/player/characters/character_swap_timing.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/features/player/characters/kairi_idle.h"
#include "kh3ap/features/player/combat/kairi_ground_slam.h"
#include "kh3ap/features/player/combat/kairi_shining_hud.h"
#include "kh3ap/features/player/characters/character_exploration.h"
#include "kh3ap/features/player/characters/character_crowd.h"
#include "kh3ap/features/player/movement/character_floating.h"
#include "kh3ap/features/player/movement/alternate_wall_run.h"
#include "kh3ap/features/player/combat/combat_target_handoff.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/diagnostics/character_swap_trace.h"
#include "kh3ap/features/enemies/enemy_clear_service.h"
#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/utilities.h"
#endif
#include "kh3ap/features/world/garden_return/garden_return_service.h"
#include "kh3ap/features/kh02/ui/kh02_inventory_menu.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflected_property_cache.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/ui/options_menu.h"
#include "kh3ap/features/world/space/space_service.h"
#include <Unreal/UObject.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/GameplayStatics.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <format>
#include <memory>

namespace kh3ap::character_swap
{
    using namespace RC::Unreal;
    static bool finish_pending_swap(std::chrono::steady_clock::time_point now);

    namespace
    {
        constexpr auto possession_property_cache_capacity = 64;
        constexpr auto max_character_swap_parameter_bytes = 256;

        constexpr auto character_swap_prepare_timeout = std::chrono::seconds(30);
        constexpr auto character_swap_handoff_timeout = std::chrono::seconds(10);
        constexpr int pooh_sora_factory_kind = player_id_value(PlayerId::SoraHundredAcreWood);
        constexpr int yozora_sora_factory_kind = player_id_value(PlayerId::SoraYozora);
        constexpr int maximum_cinematic_groups = 512;

        using Clock = std::chrono::steady_clock;
        thread_local ReflectedPropertyCache<possession_property_cache_capacity>
            possession_properties;
        constexpr auto library = L"/Script/TresGame.Default__TresGameBlueprintLibrary";

        struct Choice
        {
            const wchar_t* label;
            int id;
        };

        // ETresPlayerUniqueID: Ventus has no ordinary player pawn implementation.
        constexpr std::array choices{Choice{L"Off", -1},
                                     Choice{L"Aqua", player_id_value(PlayerId::Aqua)},
                                     Choice{L"Kairi", player_id_value(PlayerId::Kairi)},
                                     Choice{L"Roxas", player_id_value(PlayerId::Roxas)},
                                     Choice{L"Riku", player_id_value(PlayerId::Riku)}};
        size_t selected{};
        RuntimeOptionsInputOwnership input;

        struct SwapSession
        {
            FWeakObjectPtr owned_hero, swap_world;
            std::unique_ptr<character_party::Handoff> party_handoff;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            FWeakObjectPtr diagnostic_outgoing;
#endif
            int sora_id{}, target_id{-1}, hp{}, focus{};
            CharacterSwapStats stats;
            int active_character_id{-1};
            int travel_character_id{-1};
            bool pending{}, return_requested{};
            bool cinematic_return_pending{};
            FWeakObjectPtr cinematic_actor;
            FWeakObjectPtr native_cinematic_world;
            bool rebind_pending{};
            Clock::time_point deadline{};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            Clock::time_point swap_started{};
#endif

            auto has_pending_work() const -> bool
            {
                return pending || return_requested || rebind_pending;
            }
        };
        SwapSession session;
        CharacterAscensionState ascension;
        FWeakObjectPtr ascension_boss, ascension_world;
        bool ascension_entering{}, ascension_handoff_pending{}, ascension_recheck_pending{};
        Clock::time_point ascension_handoff_deadline{}, ascension_recheck_deadline{};

        bool ascension_locked()
        {
            return ascension.active && ascension_world.Get() == kh02_current_world() &&
                   ascension_boss.Get();
        }

        void finish_ascension()
        {
            ascension.finish();
            ascension_handoff_pending = false;
            ascension_recheck_pending = false;
            if (ascension.restore_pending)
            {
                session.travel_character_id = ascension.restore_character;
                session.rebind_pending = true;
                session.deadline = Clock::now() + character_swap_prepare_timeout;
            }
        }
        CharacterTrapState trap;
        FWeakObjectPtr trap_world;
        int64_t trap_receipt_index{-1};
        Clock::time_point trap_prepare_deadline{};
        constexpr int king_mickey_id = static_cast<int>(PlayerId::KingMickey);

        void request_asset_preload(int player_id);

        void clear_trap()
        {
            trap.clear();
            trap_receipt_index = -1;
            trap_world = {};
            request_asset_preload(choices[selected].id);
        }

        void activate_trap(Clock::time_point now)
        {
            trap.activated(now);
            // Commit the receipt only after Mickey is actually ready. A crash
            // during preload/factory setup leaves the durable queue pending.
            if (trap_receipt_index >= 0)
            {
                native_traps::applied(trap_receipt_index);
                trap_receipt_index = -1;
            }
        }
        bool cinematic_hooks_installed{};
        bool shared_support_prepared{};
        bool shared_support_pending{};
        Clock::time_point shared_support_deadline{};
        void install_cinematic_return_hook();

        bool swap_preparation_pending(int target)
        {
            return shared_support_pending || character_swap_assets::preparation_pending(target) ||
                   (target == static_cast<int>(player_animation_support::PlayerId::Kairi) &&
                    kairi_shining_hud::preparation_pending());
        }

        bool prepare_shared_support(int player_id)
        {
            if (shared_support_prepared)
            {
                return true;
            }
            const CharacterSwapOperationTimer timer{L"shared_prepare", player_id, true};
            if (!character_exploration::prepare() || !character_traversal::initialize() ||
                !character_shotlock::initialize() || !character_magic::initialize())
            {
                return false;
            }
            Kh02InventoryMenu::install_shared();
            character_union_cross::initialize();
            install_cinematic_return_hook();
            shared_support_prepared = true;
            return true;
        }

        void request_asset_preload(int player_id)
        {
            character_pawn_pool::clear();
            character_swap_assets::select(player_id);
            kairi_shining_hud::select(player_id);
            character_swap_effects::select(player_id >= 0);
            shared_support_pending = player_id >= 0 && !shared_support_prepared;
            shared_support_deadline = Clock::now() + character_swap_prepare_timeout;
        }

        void trace_cinematic(std::wstring_view stage, UObject* actor)
        {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (session.active_character_id < 0 && !session.pending)
            {
                return;
            }
            static const auto directory = try_get_current_module_directory();
            if (!directory)
            {
                return;
            }
            const auto name = [](UObject* value)
            {
                return is_uobject_valid_fast(value) ? value->GetFullName() : L"null";
            };
            append_text_file(
                *directory / L"experimental_cinematic_events.log",
                to_utf8(std::format(
                    L"pid={} stage={} active={} pending={} actor={} actor_world={} "
                    L"viewport_world={} owned={}\n",
                    GetCurrentProcessId(), stage, session.active_character_id, session.pending,
                    name(actor),
                    name(is_uobject_valid_fast(actor) ? kh02_actor_world(actor) : nullptr),
                    name(kh02_current_world()), name(session.owned_hero.Get()))));
#endif
        }

        struct Call : Kh02ReflectedCall
        {
            Call(UObject* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "character_swap",
                                    max_character_swap_parameter_bytes)
            {
            }

            Call(const wchar_t* owner, const wchar_t* method)
                : Kh02ReflectedCall(owner, method, "character_swap",
                                    max_character_swap_parameter_bytes)
            {
            }
        };

        UObject* object(UObject* owner, const wchar_t* field)
        {
            return possession_properties.read_object(owner, field);
        }

        int id(UObject* pawn)
        {
            auto* property = possession_properties.resolve(pawn, L"m_PlayerUniqueID");
            return property && property->GetElementSize() == sizeof(uint8_t)
                       ? *property->ContainerPtrToValuePtr<uint8_t>(pawn)
                       : -1;
        }

        bool sora_variant(UObject* pawn)
        {
            const int kind = id(pawn);
            if (is_standard_sora_player_id(kind))
            {
                return true;
            }
            if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(pawn->GetClassPrivate()))
            {
                return false;
            }
            const auto class_name = pawn->GetClassPrivate()->GetName();
            // Hundred Acre Wood and Yozora use Sora factory IDs outside the
            // ordinary range. Preserve their own IDs for the return swap.
            return (kind == pooh_sora_factory_kind && class_name == L"p_po001_Pawn_C") ||
                   (kind == yozora_sora_factory_kind && class_name == L"p_dl001_Pawn_C");
        }

        UObject* player()
        {
            auto* viewport = kh02_current_viewport();
            auto* manager = object(object(viewport, L"GameInstance"), L"m_UIManager");
            auto* controller = object(manager, L"m_pPlayerController");
            auto* pawn = object(controller, L"Pawn");
            return is_uobject_valid_fast(pawn) && object(pawn, L"Controller") == controller &&
                           kh02_actor_world(pawn) == kh02_current_world()
                       ? pawn
                       : nullptr;
        }

        bool has_doom(UObject* pawn)
        {
            // The native Death Sentence start owns this countdown component;
            // its clear path destroys it and resets the pawn's reference.
            // Read it at swap gates so replacing the pawn cannot dispel Doom.
            return object(pawn, L"m_pDeathSentenceCountEffect") != nullptr;
        }

        bool ready(UObject* pawn)
        {
            if (!pawn || pawn != player() || kh02_current_story_world() ||
                RuntimeSpaceService::is_space_active() || !runtime_sora_control_ready(pawn) ||
                has_doom(pawn) ||
                character_integer_value(pawn, L"GetStateID") == player_state_ids::open_treasure_box)
            {
                return false;
            }
            Call loading(library, L"TresUI_IsClosedLoadingScreen");
            loading.run();
            Call paused(L"/Script/Engine.Default__GameplayStatics", L"IsGamePaused");
            paused.object(L"WorldContextObject", pawn);
            paused.run();
            return loading.boolean_result() && !paused.boolean_result();
        }

        class RikuItemLoadout
        {
        public:
            explicit RikuItemLoadout(int target)
            {
                if (target != static_cast<int>(player_animation_support::PlayerId::Riku))
                {
                    return;
                }
                // EGS InitBaseParam calls the item initializer at 0x1115420
                // for Riku on every new pawn (call site 0x73603c). It erases
                // equipped/consumed slots without returning items to stock.
                // Keep this record's existing loadout across our synchronous
                // factory call, while allowing an uninitialized record to start.
                const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
                if (!character_native::verify_sites(
                        {character_native::Site::character_form_record_lookup}))
                {
                    throw std::runtime_error("character_item_record_lookup_signature");
                }
                lookup_ = reinterpret_cast<Lookup>(base + record_lookup_rva);
                auto* world = kh02_current_world();
                auto* instance = object(world, L"OwningGameInstance");
                if (!is_uobject_valid_fast(instance) ||
                    instance->GetClassPrivate()->GetName() != L"TresGameInstance")
                {
                    throw std::runtime_error("character_item_save_owner_missing");
                }
                auto* record = find_record(instance);
                if (!matches(record))
                {
                    return;
                }
                const auto count = static_cast<uint8_t>(record[item_count_offset]);
                bool initialized = false;
                for (size_t slot = 0; slot < item_slots; ++slot)
                {
                    initialized |=
                        (static_cast<uint8_t>(
                             record[items_offset + slot * item_slot_bytes + item_flags_offset]) &
                         item_slot_available) != 0;
                }
                if (!count || count > item_slots || !initialized)
                {
                    return;
                }
                world_ = make_uobject_weak(world);
                instance_ = make_uobject_weak(instance);
                record_ = record;
                count_ = count;
                std::memcpy(items_.data(), record + items_offset, items_.size());
            }

            ~RikuItemLoadout()
            {
                auto* instance = instance_.Get();
                if (!record_ || !instance || !world_.Get() ||
                    world_.Get() != kh02_current_world() || find_record(instance) != record_ ||
                    !matches(record_))
                {
                    return;
                }
                record_[item_count_offset] = static_cast<std::byte>(count_);
                std::memcpy(record_ + items_offset, items_.data(), items_.size());
            }

            RikuItemLoadout(const RikuItemLoadout&) = delete;
            RikuItemLoadout& operator=(const RikuItemLoadout&) = delete;

        private:
            using Lookup = std::byte* (*)(void*, uint8_t, uint8_t);
            inline static const uintptr_t record_lookup_rva =
                character_native::rva(character_native::Site::character_form_record_lookup);
            static constexpr size_t active_save_data_offset = 0x870;
            static constexpr size_t item_count_offset = 5;
            static constexpr size_t items_offset = 0x118;
            static constexpr size_t item_slots = 8;
            static constexpr size_t item_slot_bytes = 8;
            static constexpr size_t item_flags_offset = 4;
            static constexpr uint8_t item_slot_available = 1;
            static constexpr uint8_t riku_character = character_id_value(CharacterId::Riku);
            static constexpr uint8_t riku_player = player_id_value(PlayerId::Riku);
            Lookup lookup_{};
            FWeakObjectPtr world_, instance_;
            // Borrowed only within one synchronous factory call, never a frame.
            std::byte* record_{};
            std::array<std::byte, item_slots * item_slot_bytes> items_{};
            uint8_t count_{};

            std::byte* find_record(UObject* instance) const
            {
                return lookup_(reinterpret_cast<std::byte*>(instance) + active_save_data_offset,
                               riku_character, riku_player);
            }

            static bool matches(const std::byte* record)
            {
                return record && static_cast<uint8_t>(record[0]) == riku_character &&
                       static_cast<uint8_t>(record[1]) == riku_player;
            }
        };

        bool begin(UObject* pawn, int target, bool cinematic = false)
        {
            if (ascension_locked() && !ascension.allows_target(target, session.sora_id))
            {
                return false;
            }
            const CharacterSwapOperationTimer begin_timer{L"begin", target, true};
            const bool cinematic_handoff = cinematic;
            const bool cinematic_owner = cinematic && is_uobject_valid_fast(pawn) &&
                                         pawn == player() && !kh02_current_story_world() &&
                                         !RuntimeSpaceService::is_space_active() &&
                                         !has_doom(pawn) &&
                                         character_integer_value(pawn, L"GetStateID") !=
                                             player_state_ids::open_treasure_box &&
                                         character_integer_value(pawn, L"GetHitPoint") > 0;
            if (character_style::blocks_swap(pawn) ||
                (!cinematic_owner && (!ready(pawn) || character_shotlock::blocks_swap(pawn))) ||
                !prepare_shared_support(target))
            {
                return false;
            }
            const bool returning = pawn == session.owned_hero.Get();
            if (!returning && !sora_variant(pawn))
            {
                return false;
            }
            CharacterSwapStats::validate(pawn);
            if (!returning)
            {
                session.sora_id = id(pawn);
                if (!session.stats.capture(pawn))
                {
                    return false;
                }
                alternate_wall_run::capture_sora(pawn);
                character_traversal::capture_sora(pawn);
                character_camera::capture_sora(
                    pawn, target == static_cast<int>(player_animation_support::PlayerId::Roxas));
                character_swap_assets::retain_return_class(pawn);
                character_underwater_combat::capture_sora(pawn);
            }
            session.hp = character_integer_value(pawn, L"GetHitPoint");
            character_magic::capture(pawn);
            session.focus = character_integer_value(pawn, L"GetFocusPoint");
            auto* outgoing_controller = object(pawn, L"Controller");
            const auto local_player = make_uobject_weak(object(outgoing_controller, L"Player"));
            Call get_control_rotation(outgoing_controller, L"GetControlRotation");
            get_control_rotation.run();
            const auto control_rotation = get_control_rotation.result_vector();
            session.target_id = target;
            session.swap_world = make_uobject_weak(kh02_actor_world(pawn));
            session.deadline = Clock::now() + character_swap_handoff_timeout;
            Call change(library, L"BP_TresGameChangePlayer");
            change.object(L"WorldContextObject", pawn);
            change.integer(L"InKind", target);
            if (!combat_target_handoff::prepare(pawn))
            {
                return false;
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            session.diagnostic_outgoing = make_uobject_weak(pawn);
            RuntimeEnemyClearService::prepare_protection_handoff(pawn);
#endif
            session.pending = true;
            session.cinematic_return_pending = cinematic_handoff;
            session.travel_character_id =
                target == king_mickey_id
                    ? -1
                    : travel_target_after_swap(session.travel_character_id, target, session.sora_id,
                                               cinematic);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            session.swap_started = begin_timer.started;
#endif
            {
                const CharacterSwapOperationTimer factory_timer{L"native_factory", target, true};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                character_swap_trace::begin(target);
#endif
                session.party_handoff = std::make_unique<character_party::Handoff>(pawn);
                if (!cinematic_handoff)
                {
                    character_swap_effects::departing(pawn);
                }
                character_swap_assets::notify_player_swap_started(pawn);
                kairi_shining_hud::before_swap(pawn, target);
                // Close item/target submenus while the outgoing HUD still has
                // their state. The factory resets that state without closing
                // the visible menus, including when a parked pawn is reused.
                Call reset_commands(library, L"TresUI_ResetHudCommandState");
                reset_commands.run();
                const RikuItemLoadout riku_items(target);
                if (cinematic_handoff)
                {
                    character_pawn_pool::clear();
                    change.run();
                }
                else
                {
                    character_style::capture(pawn, target);
                    character_pawn_pool::Transaction pooled(pawn, target);
                    change.run();
                    pooled.finish();
                }
            }
            auto* incoming = player();
            auto* possessed = object(object(local_player.Get(), L"PlayerController"), L"Pawn");
            if (owns_swap_pawn(possessed))
            {
                character_shotlock::prepare_incoming_input(possessed);
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            character_shotlock::diagnostic_after_factory(possessed);
#endif
            if (id(incoming) == session.target_id)
            {
                if (!cinematic_handoff && owns_swap_pawn(incoming))
                {
                    Call restore_rotation(object(incoming, L"Controller"), L"SetControlRotation");
                    restore_rotation.vector(L"NewRotation", control_rotation[0],
                                            control_rotation[1], control_rotation[2]);
                    restore_rotation.run();
                }
                if (session.party_handoff && owns_swap_pawn(incoming))
                {
                    session.party_handoff->finish(incoming);
                    session.party_handoff.reset();
                }
                character_traversal::apply_movement(incoming);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                RuntimeEnemyClearService::handoff_protection(session.diagnostic_outgoing, incoming);
#endif
                combat_target_handoff::apply(incoming);
            }
            return true;
        }

        void enter_ascension(UObject* boss)
        {
            auto* world = kh02_current_world();
            if (!is_uobject_valid_fast(boss) || !is_uobject_valid_fast(world) ||
                kh02_actor_world(boss) != world ||
                world->GetPathName().find(L"/Game/Levels/bt/bt_04/") == std::wstring::npos)
            {
                return;
            }
            if (ascension_world.Get() != world || ascension_boss.Get() != boss)
            {
                ascension.clear();
            }
            ascension_boss = make_uobject_weak(boss);
            ascension_world = make_uobject_weak(world);
            ascension.start(session.travel_character_id);
            ascension_entering = true;
            ascension_handoff_pending = true;
            ascension_handoff_deadline = Clock::now() + character_swap_handoff_timeout;
            session.rebind_pending = false;
            session.return_requested = false;
            if (trap.locked())
            {
                // A scripted Sora requirement takes priority over the
                // temporary Mickey pawn; pending receipts stay queued.
                clear_trap();
            }
            try
            {
                auto* pawn = player();
                if (!sora_variant(pawn) && !session.pending && owns_swap_pawn(pawn))
                {
                    begin(pawn, session.sora_id, true);
                }
                if (sora_variant(player()))
                {
                    ascension_handoff_pending = false;
                    if (session.pending && session.target_id == session.sora_id)
                    {
                        // Commit resources before the boss's scripted
                        // transformation can change them on Sora.
                        finish_pending_swap(Clock::now());
                    }
                }
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Character Ascension entered boss={} restore={} sora={}\n"),
                    boss->GetName(), ascension.restore_character, sora_variant(player()));
            }
            catch (const std::exception&)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Character Ascension Sora handoff pending\n"));
            }
        }

        bool cinematic_uses_sora(UObject* matinee)
        {
            if (!is_uobject_valid_fast(matinee) ||
                kh02_actor_world(matinee) != kh02_current_world())
            {
                return false;
            }
            auto* data = object(matinee, L"MatineeData");
            auto* field = is_uobject_valid_fast(data)
                              ? data->GetPropertyByNameInChain(L"InterpGroups")
                              : nullptr;
            if (!field || field->GetClass().GetName() != L"ArrayProperty")
            {
                return false;
            }
            auto* inner = static_cast<FArrayProperty*>(field)->GetInner();
            if (!inner || inner->GetClass().GetName() != L"ObjectProperty" ||
                inner->GetElementSize() != sizeof(UObject*))
            {
                return false;
            }
            const auto* groups = field->ContainerPtrToValuePtr<TArray<UObject*>>(data);
            if (groups->Num() < 0 || groups->Num() > maximum_cinematic_groups)
            {
                return false;
            }
            for (auto* group : *groups)
            {
                if (!is_uobject_valid_fast(group))
                {
                    continue;
                }
                auto* mesh_field = group->GetPropertyByNameInChain(L"m_CharacterMesh");
                if (!mesh_field || mesh_field->GetClass().GetName() != L"StrProperty")
                {
                    continue;
                }
                // The city's p_bx001 movie model can use a p_ex001 pawn.
                // Match the authored Sora roles, not the pawn class's spelling.
                constexpr std::array<std::wstring_view, 9> sora_meshes{
                    L"p_ex001", L"p_ca001", L"p_mi001", L"p_ts001", L"p_po001",
                    L"p_bx001", L"p_ex011", L"p_ew001", L"p_dl001"};
                const auto mesh = try_read_object_string_property(group, L"m_CharacterMesh");
                if (mesh &&
                    std::find(sora_meshes.begin(), sora_meshes.end(), *mesh) != sora_meshes.end())
                {
                    return true;
                }
            }
            return false;
        }

        bool adopt_travel_pawn()
        {
            if (session.travel_character_id < 0 || session.pending || kh02_current_story_world() ||
                RuntimeSpaceService::is_space_active() ||
                !character_unlocks::available(session.travel_character_id))
            {
                return false;
            }
            auto* pawn = player();
            auto* controller = object(pawn, L"Controller");
            auto* local = object(controller, L"Player");
            if (id(pawn) != session.travel_character_id ||
                object(local, L"PlayerController") != controller ||
                !is_uobject_valid_fast(controller))
            {
                return false;
            }
            // Travel can recreate the alternate before cinematic input unlocks.
            // Retain the departure's stat snapshot; only adopt verified local possession.
            session.swap_world = make_uobject_weak(kh02_actor_world(pawn));
            session.owned_hero = make_uobject_weak(pawn);
            session.active_character_id = session.travel_character_id;
            session.rebind_pending = true;
            session.deadline = Clock::now() + character_swap_prepare_timeout;
            return true;
        }

        void return_for_cinematic(UObject* matinee)
        {
            trace_cinematic(L"consider_return", matinee);
            adopt_travel_pawn();
            auto* world = kh02_current_world();
            if (is_uobject_valid_fast(matinee) && is_uobject_valid_fast(world) &&
                kh02_actor_world(matinee) == world &&
                is_ascension_cinematic(world->GetPathName(), matinee->GetPathName()))
            {
                // This authored movie starts before BP_AscensionAttackStart.
                // Resolve its boss once at this event, before alternate binding.
                auto* type = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, L"/Script/TresGame.TresEnemyPawn_e_ex301");
                if (type)
                {
                    std::vector<AActor*> actors;
                    UGameplayStatics::GetAllActorsOfClass(world, type, actors);
                    for (auto* actor : actors)
                    {
                        if (is_uobject_valid_fast(actor) && kh02_actor_world(actor) == world &&
                            actor->GetClassPrivate()->GetName() == L"e_ex301_Pawn_C")
                        {
                            enter_ascension(actor);
                            ascension_entering = false;
                            break;
                        }
                    }
                }
            }
            const bool native_sora = is_uobject_valid_fast(world) &&
                                     cinematic_requires_native_sora(world->GetPathName());
            if (native_sora && session.travel_character_id >= 0 && cinematic_uses_sora(matinee))
            {
                session.native_cinematic_world = make_uobject_weak(world);
            }
            if (!native_sora && !ascension_locked() && session.travel_character_id >= 0 &&
                cinematic_uses_sora(matinee) &&
                character_cinematic_binding::can_bind(matinee))
            {
                auto* incoming = player();
                if (!session.pending && sora_variant(incoming))
                {
                    begin(incoming, session.travel_character_id, true);
                    incoming = player();
                }
                if (id(incoming) == session.travel_character_id && owns_swap_pawn(incoming) &&
                    character_cinematic_binding::bind(matinee, incoming))
                {
                    return;
                }
            }
            auto* outgoing = current_hero();
            if (!outgoing || session.pending || !owns_swap_pawn(outgoing))
            {
                return;
            }
            if (!cinematic_uses_sora(matinee))
            {
                return;
            }
            // The movie binds players by their authored mesh before playback.
            // An alternate leaves Sora's group empty, including its end-position
            // track. Return before playback or a video's skipped end-position pass
            // initializes those bindings.
            // Cinematic input is already locked; ordinary input readiness is not
            // an appropriate gate for this owned-pawn lifecycle transition.
            session.hp = character_integer_value(outgoing, L"GetHitPoint");
            character_magic::capture(outgoing);
            session.focus = character_integer_value(outgoing, L"GetFocusPoint");
            if (!combat_target_handoff::prepare(outgoing))
            {
                return;
            }
            session.pending = true;
            session.cinematic_return_pending = true;
            session.native_cinematic_world = make_uobject_weak(kh02_current_world());
            session.cinematic_actor = make_uobject_weak(matinee);
            session.deadline = Clock::now() + character_swap_handoff_timeout;
            session.rebind_pending = false;
            session.return_requested = false;
            session.target_id = session.sora_id;
            try
            {
                character_pawn_pool::clear();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                session.diagnostic_outgoing = make_uobject_weak(outgoing);
                RuntimeEnemyClearService::prepare_protection_handoff(outgoing);
#endif
                Call change(library, L"BP_TresGameChangePlayer");
                change.object(L"WorldContextObject", outgoing);
                change.integer(L"InKind", session.sora_id);
                session.party_handoff = std::make_unique<character_party::Handoff>(outgoing);
                change.run();
                auto* incoming = player();
                if (is_uobject_valid_fast(incoming) && id(incoming) == session.target_id &&
                    owns_swap_pawn(incoming))
                {
                    session.party_handoff->finish(incoming);
                    session.party_handoff.reset();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                    RuntimeEnemyClearService::handoff_protection(session.diagnostic_outgoing,
                                                                 incoming);
#endif
                    combat_target_handoff::apply(incoming);
                }
                // Possession/UI publication can finish on the next frame.
                // The existing bounded swap transaction commits health and
                // targeting as soon as local ownership is established, even
                // while the movie intentionally keeps input locked.
            }
            catch (...)
            {
                session.party_handoff.reset();
                combat_target_handoff::cancel();
                session.pending = false;
                session.cinematic_return_pending = false;
                session.cinematic_actor = {};
                throw;
            }
        }

        void install_ascension_hooks()
        {
            static bool installed{};
            if (installed)
            {
                return;
            }
            installed = true;
            // These are the shipping boss's gameplay entry/exit events. Return
            // before entry runs, while its player references can still resolve
            // the newly possessed Sora; never wait for the movie to finish.
            UObjectGlobals::RegisterHook(
                L"/Script/TresGame.TresEnemyPawn_e_ex301:BP_AscensionAttackStart",
                [](UnrealScriptFunctionCallableContext& context, void*)
                {
                    enter_ascension(context.Context);
                },
                [](UnrealScriptFunctionCallableContext&, void*)
                {
                    ascension_entering = false;
                }, nullptr);
            for (const auto* path :
                 {L"/Script/TresGame.TresEnemyPawn_e_ex301:BP_AscensionEnd",
                  L"/Script/TresGame.TresEnemyPawn_e_ex301:BattleEndAscensionMode"})
            {
                UObjectGlobals::RegisterHook(
                    path, [](UnrealScriptFunctionCallableContext&, void*) {},
                    [](UnrealScriptFunctionCallableContext& context, void*)
                    {
                        if (!ascension_locked() || context.Context != ascension_boss.Get())
                        {
                            return;
                        }
                        try
                        {
                            Call mode(context.Context, L"IsAscensionMode");
                            mode.run();
                            if (!mode.boolean_result())
                            {
                                finish_ascension();
                                RC::Output::send<RC::LogLevel::Normal>(
                                    STR("[KH3APProbeCpp] Character Ascension ended restore={}\n"),
                                    ascension.restore_character);
                            }
                        }
                        catch (...)
                        {
                            // Unknown state must retain the lock.
                        }
                    }, nullptr);
            }
        }

        void install_cinematic_return_hook()
        {
            if (cinematic_hooks_installed)
            {
                return;
            }
            // Some movies show a video before starting their Matinee. Native
            // Skip can go straight to the end-position pass without calling Play.
            for (const auto* path :
                 {L"/Script/Engine.MatineeActor:Play",
                  L"/Script/Engine.MatineeActor:SQEX_SetActorLocationAtEndFrame"})
            {
                UObjectGlobals::RegisterHook(
                    path,
                    [path](UnrealScriptFunctionCallableContext& context, void*)
                    {
                        trace_cinematic(path, context.Context);
                        try
                        {
                            return_for_cinematic(context.Context);
                        }
                        catch (const std::exception& failure)
                        {
                            RC::Output::send<RC::LogLevel::Warning>(
                                STR("[KH3APProbeCpp] Experimental cinematic return failed: {}\n"),
                                std::wstring(failure.what(),
                                             failure.what() + std::strlen(failure.what())));
                        }
                    },
                    [](UnrealScriptFunctionCallableContext&, void*)
                    {
                    },
                    nullptr);
            }
            cinematic_hooks_installed = true;
            UObjectGlobals::RegisterHook(
                L"/Script/Engine.MatineeActor:Stop",
                [](UnrealScriptFunctionCallableContext&, void*)
                {
                },
                [](UnrealScriptFunctionCallableContext& context, void*)
                {
                    try
                    {
                        character_cinematic_binding::release(context.Context);
                    }
                    catch (...)
                    {
                    }
                },
                nullptr);
        }
        auto rebind_after_travel(Clock::time_point now) -> bool
        {
            if (ascension_locked())
            {
                session.rebind_pending = false;
                return false;
            }
            if (now > session.deadline)
            {
                session.rebind_pending = false;
                ascension.restore_pending = false;
                return false;
            }
            auto* pawn = player();
            // Player-start and movie-finished notifications can arrive under
            // the loading cover, before the ship has placed/attached Sora.
            // Retain the bounded request until normal control is available.
            if (!ready(pawn))
            {
                return false;
            }
            if (session.travel_character_id >= 0 && sora_variant(pawn) &&
                session.native_cinematic_world.Get() != kh02_current_world() &&
                !kh02_current_story_world() && !RuntimeSpaceService::is_space_active() &&
                character_unlocks::available(session.travel_character_id) &&
                begin(pawn, session.travel_character_id, true))
            {
                ascension.clear();
                session.rebind_pending = false;
                return false;
            }
            if (id(pawn) == session.travel_character_id &&
                (session.owned_hero.Get() != pawn ||
                 session.swap_world.Get() != kh02_current_world()))
            {
                adopt_travel_pawn();
            }
            if (!ready(pawn))
            {
                return false;
            }
            session.rebind_pending = false;
            if (id(pawn) != session.active_character_id)
            {
                session.owned_hero = {};
                session.active_character_id = -1;
                session.return_requested = false;
                character_swap_assets::release_return_class();
                return false;
            }
            // A native map transition can reconstruct the selected pawn.
            // Rebind only after the existing player-start event, then stop.
            session.swap_world = make_uobject_weak(kh02_actor_world(pawn));
            session.hp = character_integer_value(pawn, L"GetHitPoint");
            character_magic::capture(pawn);
            session.focus = character_integer_value(pawn, L"GetFocusPoint");
            session.target_id = id(pawn);
            session.pending = true;
            return true;
        }

    } // namespace

    void append_menu(RuntimeOptionsPageBuilder& page)
    {
        std::vector<RuntimeOptionsChoice> entries;
        for (const auto& choice : choices)
        {
            entries.push_back({std::to_wstring(choice.id), choice.label, [player_id = choice.id]
                               {
                                   return character_unlocks::available(player_id);
                               }});
        }
        page.choice(
            L"character_swap", L"Combat Ally", std::move(entries),
            []
            {
                return selected;
            },
            [](size_t choice)
            {
                if (manual_swaps_blocked() || choice >= choices.size() || session.pending ||
                    has_doom(player()) ||
                    !character_unlocks::available(choices[choice].id))
                {
                    return;
                }
                if (choice != selected && session.owned_hero.Get())
                {
                    session.return_requested = true;
                }
                selected = choice;
                ascension.clear();
                session.travel_character_id = -1;
                request_asset_preload(choices[selected].id);
            },
            {},
            []
            {
                return std::wstring{L"R2 swaps Sora with one selected character, carrying movement "
                                    L"and normal combos. "
                                    L"Map zoom takes priority."};
            },
            []
            {
                return std::wstring{
                    L"AP unlocks: Aqua after 0.2, Roxas after Keyblade Graveyard with Oblivion and "
                    L"Oathkeeper owned (any order), Kairi after Re "
                    L"Mind, Riku after all 13 Data battles. "
                    L"Changing selection returns to Sora first."};
            });
    }

    bool route(int controller, std::wstring_view key, int event, bool gamepad)
    {
        if (character_rails::route(controller, key, event, gamepad) ||
            character_traversal::route(controller, key, event, gamepad) ||
            character_camera::route(controller, key, event, gamepad))
        {
            return true;
        }
        if (!gamepad || key != L"Gamepad_RightTrigger")
        {
            return false;
        }
        if (manual_swaps_blocked())
        {
            return true;
        }
        return input.route(controller, std::wstring(key), event,
                           [&]
                           {
                               if (event != 0)
                               {
                                   return true;
                               }
                               if (session.pending)
                               {
                                   return true;
                               }
                               if (!selected && !session.owned_hero.Get())
                               {
                                   return false;
                               }
                               try
                               {
                                   auto* pawn = player();
                                   return RuntimeGardenReturnService::request_experimental_swap(
                                       pawn);
                               }
                               catch (...)
                               {
                                   session.party_handoff.reset();
                                   combat_target_handoff::cancel();
                                   session.pending = false;
                                   return false;
                               }
                           });
    }

    static void tick_shared_support(Clock::time_point now)
    {
        if (!shared_support_pending)
        {
            return;
        }

        try
        {
            if (now >= shared_support_deadline)
            {
                shared_support_pending = false;
            }
            else if (player() && !kh02_current_story_world() &&
                     !RuntimeSpaceService::is_space_active())
            {
                shared_support_pending = !prepare_shared_support(choices[selected].id);
            }
        }
        catch (const std::exception& failure)
        {
            shared_support_pending = false;
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Character shared preparation failed: {}\n"),
                std::wstring(failure.what(), failure.what() + std::strlen(failure.what())));
        }
    }
    static void tick_character_trap(Clock::time_point now)
    {
        if (!trap.locked())
        {
            return;
        }

        auto* pawn = player();
        if (trap_world.Get() != kh02_current_world())
        {
            // Save loads/retries and non-Blueprint travel can bypass the
            // ordinary map-jump hook. Never carry Mickey into a new world.
            trap.phase = CharacterTrapState::Phase::Returning;
            trap_world = make_uobject_weak(kh02_current_world());
            session.travel_character_id = -1;
            session.rebind_pending = false;
            if (sora_variant(pawn))
            {
                session.pending = false;
                session.owned_hero = {};
                session.active_character_id = -1;
                clear_trap();
            }
            else if (id(pawn) == king_mickey_id)
            {
                session.swap_world = trap_world;
                session.owned_hero = make_uobject_weak(pawn);
                session.active_character_id = king_mickey_id;
                session.pending = false;
            }
        }
        if (trap.phase == CharacterTrapState::Phase::Preparing)
        {
            if (now > trap_prepare_deadline)
            {
                clear_trap();
            }
            else if (!session.pending && !swap_preparation_pending(king_mickey_id) &&
                     sora_variant(pawn) && ready(pawn) &&
                     runtime_trap_application_state_status(pawn).ready)
            {
                trap.phase = CharacterTrapState::Phase::Starting;
                if (!RuntimeGardenReturnService::request_character_trap_swap(pawn))
                {
                    trap.phase = CharacterTrapState::Phase::Preparing;
                }
            }
        }
        else if (trap.phase == CharacterTrapState::Phase::Starting)
        {
            if (!session.pending && id(current_hero()) == king_mickey_id)
            {
                activate_trap(now);
            }
            else if (now > trap_prepare_deadline && !session.pending)
            {
                clear_trap();
            }
            else if (!session.pending && !RuntimeGardenReturnService::character_trap_swap_pending())
            {
                // The queued swap was rejected at dispatch. Keep the receipt pending
                // and retry within the same preparation deadline.
                trap.phase = CharacterTrapState::Phase::Preparing;
            }
        }
        if (trap.due(now))
        {
            trap.phase = CharacterTrapState::Phase::Returning;
        }
        if (trap.phase == CharacterTrapState::Phase::Returning && !session.pending)
        {
            if (sora_variant(pawn))
            {
                clear_trap();
            }
            else if (id(current_hero()) == king_mickey_id)
            {
                RuntimeGardenReturnService::request_character_trap_swap(pawn);
            }
        }
    }
    static bool finish_pending_swap(Clock::time_point now)
    {
        const CharacterSwapOperationTimer finish_timer{L"finish", session.target_id};
        try
        {
            if (session.rebind_pending && !rebind_after_travel(now))
            {
                return false;
            }
            if (session.return_requested && !session.pending)
            {
                auto* pawn = current_hero();
                if (!pawn)
                {
                    session.return_requested = false;
                    return false;
                }
                if (!(trap.locked() ? RuntimeGardenReturnService::request_character_trap_swap(pawn)
                                    : RuntimeGardenReturnService::request_experimental_swap(pawn)))
                {
                    return false;
                }
                session.return_requested = false;
                return false;
            }
            if (now > session.deadline || session.swap_world.Get() != kh02_current_world())
            {
                session.party_handoff.reset();
                combat_target_handoff::cancel();
                session.pending = false;
                if (session.cinematic_return_pending)
                {
                    session.cinematic_return_pending = false;
                    session.cinematic_actor = {};
                    session.active_character_id = -1;
                }
                auto* incoming = player();
                if (trap.locked() && id(incoming) == king_mickey_id &&
                    kh02_actor_world(incoming) == kh02_current_world())
                {
                    session.owned_hero = make_uobject_weak(incoming);
                    session.swap_world = make_uobject_weak(kh02_current_world());
                    session.active_character_id = king_mickey_id;
                    trap.phase = CharacterTrapState::Phase::Returning;
                }
                else
                {
                    session.owned_hero = {};
                }
                return false;
            }
            auto* pawn = player();
            if (id(pawn) == session.target_id)
            {
                if (session.party_handoff && owns_swap_pawn(pawn))
                {
                    session.party_handoff->finish(pawn);
                    session.party_handoff.reset();
                }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                RuntimeEnemyClearService::handoff_protection(session.diagnostic_outgoing, pawn);
#endif
                combat_target_handoff::apply(pawn);
            }
            if (id(pawn) != session.target_id ||
                (session.cinematic_return_pending ? !owns_swap_pawn(pawn) : !ready(pawn)))
            {
                return false;
            }
            const auto prepare = [&](std::wstring_view stage, auto action)
            {
                const CharacterSwapOperationTimer timer{stage, session.target_id};
                return action(pawn);
            };
            if (!session.cinematic_return_pending && !prepare(L"kairi_idle", kairi_idle::prepare))
            {
                return false;
            }
            if (!session.cinematic_return_pending &&
                !prepare(L"ground_slam", kairi_ground_slam::prepare))
            {
                return false;
            }
            if (!session.cinematic_return_pending &&
                !prepare(L"wall_run", alternate_wall_run::prepare))
            {
                return false;
            }
            if (!session.cinematic_return_pending && session.target_id != session.sora_id &&
                session.target_id != king_mickey_id &&
                (!prepare(L"exploration", character_exploration::prepare_pawn) ||
                 !prepare(L"traversal", character_traversal::prepare) ||
                 !prepare(L"underwater_combat", character_underwater_combat::prepare) ||
                 !prepare(L"camera", character_camera::prepare) ||
                 !prepare(L"shotlock", character_shotlock::prepare) ||
                 !prepare(L"magic", character_magic::refresh_spells) ||
                 !prepare(L"attack_completion", character_attack_completion::prepare)))
            {
                return false;
            }
            if (session.cinematic_return_pending)
            {
                if (character_integer_value(pawn, L"GetMaxHitPoint") < 1 ||
                    character_integer_value(pawn, L"GetMaxMagicPoint") < 0)
                {
                    return false;
                }
                trace_cinematic(L"cinematic_ownership_ready", session.cinematic_actor.Get());
            }
            if (session.target_id == session.sora_id)
            {
                Kh02Story::refresh_sora_experience(pawn);
            }
            if (session.target_id != session.sora_id)
            {
                session.stats.apply_base(pawn);
                session.owned_hero = make_uobject_weak(pawn);
                session.active_character_id = session.target_id;
                session.stats.synchronize_totals(pawn);
            }
            else
            {
                session.owned_hero = {};
                session.active_character_id = -1;
                if (selected == 0)
                {
                    character_pawn_pool::clear();
                }
                character_swap_assets::release_return_class();
            }
            // Ownership is committed now. Resource restoration can fail after
            // possession, but crowds must still follow the actual local pawn.
            character_crowd::rebind(pawn);
            Call health(pawn, L"SetHitPoint");
            health.integer(L"InHP", std::clamp(session.hp, 1,
                                               character_integer_value(pawn, L"GetMaxHitPoint")));
            health.run();
            character_magic::restore(pawn);
            Call focus_gauge(pawn, L"SetFocusPoint");
            focus_gauge.integer(
                L"InFP",
                std::clamp(session.focus, 0, character_integer_value(pawn, L"GetMaxFocusPoint")));
            focus_gauge.run();
            if (!session.cinematic_return_pending)
            {
                Call show_commands(library, L"TresUI_ShowHudCommand");
                show_commands.run();
            }
            combat_target_handoff::finish(pawn);
            const bool restore_after_cinematic =
                session.cinematic_return_pending && session.travel_character_id >= 0 &&
                !ascension_locked();
            session.pending = false;
            session.cinematic_return_pending = false;
            session.cinematic_actor = {};
            if (trap.locked() && session.target_id == king_mickey_id)
            {
                activate_trap(now);
            }
            if (restore_after_cinematic)
            {
                session.rebind_pending = true;
                session.deadline = now + character_swap_prepare_timeout;
            }
            character_exploration::refresh_portrait(pawn);
            observe_game_over_state_write(reinterpret_cast<uintptr_t>(object(pawn, L"MyStateComp")),
                                          reinterpret_cast<uintptr_t>(object(
                                              object(pawn, L"MyStateComp"), L"m_pCurrentState")));
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (session.swap_started != Clock::time_point{})
            {
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] CharacterSwapTiming operation=ready id={} "
                        "elapsed_us={}\n"),
                    session.target_id,
                    std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                                          session.swap_started)
                        .count());
                session.swap_started = {};
            }
#endif
            const auto actual = CharacterSwapStats::totals(pawn);
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] Experimental character ready id={} level={} "
                    "HP={} MP={} strength={} magic={} defense={}\n"),
                session.target_id, session.stats.level(), actual[0], actual[1], actual[2],
                actual[3], actual[4]);
            return true;
        }
        catch (const std::exception& failure)
        {
            session.party_handoff.reset();
            combat_target_handoff::cancel();
            // The native factory may already have possessed the incoming pawn.
            // Keep that verified ownership so a preparation failure can return
            // to Sora instead of stranding an unregistered alternate.
            auto* incoming = player();
            if (session.pending && session.target_id != session.sora_id && owns_swap_pawn(incoming))
            {
                session.owned_hero = make_uobject_weak(incoming);
                session.active_character_id = session.target_id;
            }
            session.pending = false;
            session.cinematic_return_pending = false;
            session.cinematic_actor = {};
            session.return_requested = session.owned_hero.Get() != nullptr;
            if (trap.locked())
            {
                trap.phase = CharacterTrapState::Phase::Returning;
            }
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] Experimental character failed: {}\n"),
                std::wstring(failure.what(), failure.what() + std::strlen(failure.what())));
            return false;
        }
    }

    bool tick(Clock::time_point now)
    {
        if (ascension.active)
        {
            if (ascension_world.Get() != kh02_current_world())
            {
                ascension.clear();
                ascension_handoff_pending = false;
                ascension_entering = false;
                ascension_recheck_pending = false;
            }
            else if (!ascension_boss.Get())
            {
                finish_ascension();
            }
            else if (ascension_recheck_pending)
            {
                if (now >= ascension_recheck_deadline)
                {
                    ascension_recheck_pending = false;
                }
                else if (!session.pending && ready(player()))
                {
                    ascension_recheck_pending = false;
                    try
                    {
                        Call mode(ascension_boss.Get(), L"IsAscensionMode");
                        mode.run();
                        if (!mode.boolean_result())
                        {
                            finish_ascension();
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }
            else if (ascension_handoff_pending && !session.pending)
            {
                try
                {
                    auto* pawn = player();
                    if (!sora_variant(pawn) && owns_swap_pawn(pawn))
                    {
                        begin(pawn, session.sora_id, true);
                    }
                    if (sora_variant(player()))
                    {
                        ascension_handoff_pending = false;
                    }
                }
                catch (...)
                {
                }
                if (ascension_handoff_pending && now >= ascension_handoff_deadline)
                {
                    ascension_handoff_pending = false;
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Character Ascension Sora handoff timed out\n"));
                }
            }
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        character_shotlock::diagnostic_race_tick(now);
#endif
        tick_shared_support(now);
        character_swap_assets::tick(now);
        kairi_shining_hud::tick(now);
        character_swap_effects::tick(now);
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        alternate_wall_run::diagnostic_tick();
#endif
        tick_character_trap(now);
        // No UObject discovery while idle. Retries exist only for a requested change.
        if (!session.has_pending_work())
        {
            return false;
        }
        return finish_pending_swap(now);
    }

    UObject* current_hero()
    {
        auto* pawn = session.owned_hero.Get();
        return pawn && pawn == player() && session.swap_world.Get() == kh02_current_world() &&
                       !kh02_current_story_world()
                   ? pawn
                   : nullptr;
    }

    UObject* party_owner()
    {
        auto* pawn = player();
        return owns_swap_pawn(pawn) ? pawn : nullptr;
    }

    void notify_player_started()
    {
        install_ascension_hooks();
        character_swap_assets::notify_player_started();
        kairi_shining_hud::notify_player_started();
        character_swap_effects::notify_player_started();
        // Retry can recreate the player in the same UWorld. Recheck once its
        // loading/control transition finishes, with a bounded readiness wait.
        if (ascension_locked() && !ascension_entering && !session.pending)
        {
            ascension_recheck_pending = true;
            ascension_recheck_deadline = Clock::now() + character_swap_prepare_timeout;
        }
        if (trap.locked())
        {
            if (sora_variant(player()) && trap.phase != CharacterTrapState::Phase::Preparing &&
                trap.phase != CharacterTrapState::Phase::Starting)
            {
                // Retry/Continue can recreate Sora in the same UWorld. A dead
                // Mickey must not retain the manual-swap lock on that new pawn.
                session.owned_hero = {};
                session.active_character_id = -1;
                session.travel_character_id = -1;
                session.pending = false;
                clear_trap();
            }
            return;
        }
        adopt_travel_pawn();
        if (!session.pending &&
            (session.active_character_id >= 0 || session.travel_character_id >= 0))
        {
            session.rebind_pending = true;
            session.deadline = Clock::now() + character_swap_prepare_timeout;
        }
    }

    UObject* controlled_travel_pawn()
    {
        auto* pawn = player();
        auto* controller = object(pawn, L"Controller");
        auto* local = object(controller, L"Player");
        return is_uobject_valid_fast(controller) &&
                       object(local, L"PlayerController") == controller &&
                       (sora_variant(pawn) || owns_swap_pawn(pawn)) &&
                       runtime_sora_control_ready(pawn)
                   ? pawn
                   : nullptr;
    }

    bool prepare_native_story_travel()
    {
        if (trap.locked())
        {
            trap.phase = CharacterTrapState::Phase::Returning;
        }
        session.travel_character_id = -1;
        session.rebind_pending = false;
        session.return_requested = false;
        if (session.pending)
        {
            return false;
        }
        auto* pawn = player();
        if (sora_variant(pawn))
        {
            return true;
        }
        if (pawn && pawn == current_hero() && owns_swap_pawn(pawn))
        {
            begin(pawn, session.sora_id, true);
        }
        return false;
    }

    UObject* return_to_sora_for_encounter(UObject* pawn)
    {
        if (sora_variant(pawn) && pawn == player())
        {
            return pawn;
        }
        if (session.pending || !owns_swap_pawn(pawn) || pawn != current_hero())
        {
            return nullptr;
        }
        const auto read_vector = [](UObject* owner, const wchar_t* method)
        {
            Call call(owner, method);
            call.run();
            return call.result_vector();
        };
        const auto location = read_vector(pawn, L"K2_GetActorLocation");
        const auto rotation = read_vector(pawn, L"K2_GetActorRotation");
        const auto velocity = read_vector(pawn, L"GetVelocity");
        const auto control_rotation =
            read_vector(object(pawn, L"Controller"), L"GetControlRotation");
        if (!begin(pawn, session.sora_id, true))
        {
            return nullptr;
        }
        // Keep the selected alternate available for R2 after this sequence,
        // without automatically restoring it during the freefall/ground slam.
        session.travel_character_id = -1;
        auto* incoming = player();
        if (!sora_variant(incoming) || !owns_swap_pawn(incoming))
        {
            return nullptr;
        }
        Call position(incoming, L"K2_SetActorLocationAndRotation");
        position.vector(L"NewLocation", location[0], location[1], location[2]);
        position.vector(L"NewRotation", rotation[0], rotation[1], rotation[2]);
        position.boolean(L"bSweep", false);
        position.boolean(L"bTeleport", true);
        position.run();
        auto* movement = object(incoming, L"MyMovement");
        auto* speed = possession_properties.resolve(movement, L"Velocity");
        if (speed && speed->GetElementSize() == sizeof(velocity))
        {
            std::memcpy(speed->ContainerPtrToValuePtr<void>(movement), velocity.data(),
                        sizeof(velocity));
        }
        Call camera(object(incoming, L"Controller"), L"SetControlRotation");
        camera.vector(L"NewRotation", control_rotation[0], control_rotation[1],
                      control_rotation[2]);
        camera.run();
        return incoming;
    }

    void notify_map_jump(UObject* source, std::wstring_view caller)
    {
        if (trap.locked())
        {
            prepare_native_story_travel();
        }
        // The Dark World portal also owns the KH0.2 entry. Its stock Dark Aqua
        // branch must not restore an experimental alternate in the destination.
        if (caller.find(L"/Game/Maps/GardenOfAssemblage/uasset/Textures/Portals/Portal_Base") ==
            caller.npos)
        {
            return;
        }
        auto portal_id = [](UObject* actor)
        {
            auto* property = is_uobject_valid_fast(actor)
                                 ? actor->GetPropertyByNameInChain(L"PortalID")
                                 : nullptr;
            return property && property->GetClass().GetName() == L"IntProperty" &&
                           property->GetElementSize() == sizeof(int32_t)
                       ? *property->ContainerPtrToValuePtr<int32_t>(actor)
                       : -1;
        };
        auto* selected_portal =
            object(object(object(player(), L"Controller"), L"m_pUICommandInfo"), L"m_Actor");
        if (portal_id(source) == portals::dark_world_portal_id ||
            portal_id(selected_portal) == portals::dark_world_portal_id)
        {
            prepare_native_story_travel();
        }
    }

    void notify_cinematic_finished(UObject* source)
    {
        if (!is_uobject_valid_fast(source) ||
            !source->GetClassPrivate()->GetPathName().starts_with(L"/Game/Cutscene/") ||
            kh02_actor_world(source) != kh02_current_world())
        {
            return;
        }
        session.native_cinematic_world = {};
        if (ascension_locked())
        {
            return;
        }
        if (session.travel_character_id < 0 || session.pending)
        {
            return;
        }
        session.rebind_pending = true;
        session.deadline = Clock::now() + character_swap_prepare_timeout;
    }

    void notify_cinematic_started(UObject* source)
    {
        if (is_uobject_valid_fast(source) &&
            source->GetClassPrivate()->GetPathName().starts_with(L"/Game/Cutscene/") &&
            kh02_actor_world(source) == kh02_current_world())
        {
            if (session.pending && session.target_id == session.travel_character_id)
            {
                session.cinematic_return_pending = true;
            }
            adopt_travel_pawn();
        }
    }

    bool start_king_mickey_trap(UObject* pawn, int64_t receipt_index)
    {
        if (manual_swaps_blocked() || session.pending || !sora_variant(pawn) || !ready(pawn) ||
            !runtime_trap_application_state_status(pawn).ready)
        {
            return false;
        }
        request_asset_preload(king_mickey_id);
        trap.prepare(id(pawn));
        trap_receipt_index = receipt_index;
        trap_world = make_uobject_weak(kh02_current_world());
        trap_prepare_deadline = Clock::now() + character_swap_prepare_timeout;
        return true;
    }

    bool manual_swaps_blocked()
    {
        return trap.locked() || ascension_locked();
    }

    bool enabled()
    {
        return trap.locked() ||
               (selected != 0 && character_unlocks::available(choices[selected].id)) ||
               session.active_character_id >= 0 || session.pending;
    }

    bool owns_swap_pawn(UObject* pawn)
    {
        // Input must be seeded before the incoming controller is control-ready.
        // Ownership, possession and world lifetime still have to match this swap.
        auto* controller = object(pawn, L"Controller");
        auto* local = object(controller, L"Player");
        return is_uobject_valid_fast(pawn) && !kh02_current_story_world() &&
               session.swap_world.Get() == kh02_current_world() &&
               kh02_actor_world(pawn) == session.swap_world.Get() &&
               object(controller, L"Pawn") == pawn &&
               object(local, L"PlayerController") == controller &&
               (pawn == session.owned_hero.Get() ||
                (session.pending && id(pawn) == session.target_id));
    }

    bool swap_available(UObject* pawn)
    {
        return !ascension_locked() && enabled() && character_floating::allows_swap(pawn) &&
               ready(pawn) &&
               (sora_variant(pawn) || pawn == current_hero() ||
                (session.pending && id(pawn) == session.target_id &&
                 kh02_actor_world(pawn) == session.swap_world.Get())) &&
               !character_shotlock::blocks_swap(pawn) && !character_style::blocks_swap(pawn);
    }

    bool handoff_pending()
    {
        return session.pending;
    }

    int swap_target(UObject* pawn)
    {
        if (trap.phase == CharacterTrapState::Phase::Starting)
        {
            return king_mickey_id;
        }
        if (trap.phase == CharacterTrapState::Phase::Returning)
        {
            return trap.return_player;
        }
        return pawn && pawn == current_hero() ? session.sora_id : choices[selected].id;
    }

    bool dispatch_swap(UObject* pawn, int target)
    {
        const bool starting_trap = trap.phase == CharacterTrapState::Phase::Starting &&
                                   target == king_mickey_id;
        const bool trap_target = starting_trap ||
            (trap.phase == CharacterTrapState::Phase::Returning && target == trap.return_player);
        if (ascension_locked() || session.pending ||
            (!trap_target && pawn != current_hero() && !character_unlocks::available(target)) ||
            target != swap_target(pawn) ||
            (starting_trap && !runtime_trap_application_state_status(pawn).ready))
        {
            return false;
        }
        return begin(pawn, target);
    }

    bool preparation_pending(int target)
    {
        return target != session.sora_id && swap_preparation_pending(target);
    }

    void apply_stat_bonus(int hp_amount, int mp_amount)
    {
        auto* pawn = current_hero();
        if (!pawn || session.pending || (hp_amount == 0 && mp_amount == 0))
        {
            return;
        }
        try
        {
            session.stats.add_bonus(pawn, hp_amount, mp_amount);
        }
        catch (...)
        {
            // The persistent reward is already committed. Return to Sora;
            // never retry the grant merely because its live mirror failed.
            session.return_requested = true;
        }
    }

    UObject* reward_context()
    {
        try
        {
            auto* pawn = current_hero();
            return !session.pending && ready(pawn) ? pawn : nullptr;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    UObject* bad_status_context()
    {
        auto* pawn = current_hero();
        return !trap.locked() && alternate_bad_status_player_allowed(id(pawn)) ? reward_context()
                                                                               : nullptr;
    }

    UObject* game_over_context()
    {
        auto* pawn = current_hero();
        return owns_swap_pawn(pawn) ? reward_context() : nullptr;
    }

    void observe_game_over_state_write(uintptr_t component, uintptr_t state)
    {
        if (!deathlink::view()->manual_active)
        {
            return;
        }
        auto* pawn = current_hero();
        auto* current = reinterpret_cast<UObject*>(state);
        if (!owns_swap_pawn(pawn) ||
            object(pawn, L"MyStateComp") != reinterpret_cast<UObject*>(component) ||
            !is_uobject_valid_fast(current) || object(current, L"MyCharPawn") != pawn)
        {
            return;
        }
        auto* field = current->GetPropertyByNameInChain(L"MyStateID");
        if (!field || field->GetElementSize() != sizeof(uint8_t))
        {
            return;
        }
        const bool dead =
            *field->ContainerPtrToValuePtr<uint8_t>(current) == player_state_ids::game_over;
        const auto weak = make_uobject_weak(pawn);
        if (weak.Get() != pawn)
        {
            return;
        }
        static_assert(sizeof(weak) == sizeof(uint64_t));
        uint64_t key{};
        std::memcpy(&key, &weak, sizeof(key));
        deathlink::observe_manual_player(std::to_string(key), dead,
                                         !dead && !session.pending && ready(pawn) &&
                                             current_runtime_trap_live_state_status(pawn).ready);
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    bool diagnostic_select(int player_id, bool preload)
    {
        if (manual_swaps_blocked() || session.pending || session.owned_hero.Get() ||
            !character_unlocks::available(player_id))
        {
            return false;
        }
        for (size_t index = 0; index < choices.size(); ++index)
        {
            if (choices[index].id == player_id)
            {
                selected = index;
                // Diagnostic-only cold-load control for comparable A/B captures.
                request_asset_preload(preload ? player_id : -1);
                return true;
            }
        }
        return false;
    }
#endif
} // namespace kh3ap::character_swap

namespace kh3ap
{
    RC::Unreal::UObject* persistent_player_reward_context()
    {
        if (auto* hero = character_swap::reward_context())
        {
            return hero;
        }
        return Kh02Story::persistent_reward_context();
    }
} // namespace kh3ap
