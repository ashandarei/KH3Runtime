#include "kh02_runtime.h"
#include "kh02_entry_service.h"
#include "kh02_recovery_service.h"
#include "kh02_world_context.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/kh02/transport/kh02_asset_companion.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/core/reflection/reflection_core.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include <Unreal/UClass.hpp>
#include "kh3ap/features/kh02/progress/kh02_progress_policy.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#endif
#include <Unreal/UObject.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <utility>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr auto runtime_update_interval = std::chrono::milliseconds(100);
    }

    struct Kh02Runtime::Impl
    {
        explicit Impl(Kh02RuntimeConfig value) : config(std::move(value)) {}

        Kh02RuntimeConfig config;
        // Retain the instance once imported native aliases reference its thunks.
        // Dormancy suspends work; destroying/rebinding live engine imports is unsafe.
        std::unique_ptr<Kh02Story> story;
        Kh02Lifecycle lifecycle;
        Kh02EntryService entry{[this](UObject* context)
                               {
                                   if (recovery.active())
                                   {
                                       throw std::runtime_error("kh02_entry_requires_restart");
                                   }
                                   lifecycle.prepare();
                                   return ensure_story().prepare_entry(context);
                               },
                               [this] { return story && story->entry_assets_notified(); },
                               [this] { ensure_story().prepare_asset_access(); }};
        Kh02RecoveryService recovery{{[this] { return config.gameplay_ready() && !config.game_over(); },
                                      [this](const std::string& message) { report(message); }}};
        FWeakObjectPtr observed_world;
        FWeakObjectPtr ready_reward_hero;
        Kh02WorldKind observed_kind{Kh02WorldKind::unavailable};
        kh02control::Identity active_provider;
        std::chrono::steady_clock::time_point next_update{}, next_recovery{};
        std::string last_error, cleanup_error;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        std::string failure_story;
#endif
        uint64_t constructions{}, story_polls{}, frame_updates{}, preparation_ticks{}, exit_cleanups{}, portal_polls{},
            recovery_ticks{};

        auto ensure_story() -> Kh02Story&
        {
            if (!story)
            {
                story = std::make_unique<Kh02Story>(config.battle_level_settings);
                ++constructions;
            }
            return *story;
        }

        void report(const std::string& message)
        {
            if (last_error != message)
            {
                last_error = message;
                if (config.report_error)
                {
                    config.report_error(message);
                }
            }
        }

        void fail(const std::string& reason)
        {
            report(reason);
            if (recovery.active() || !story || !story->active_session())
            {
                return;
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            try
            {
                failure_story = story->command(config.player(), L"flow_inspect").value_or("");
            }
            catch (...)
            {
            }
#endif
            entry.require_restart(reason);
            recovery.begin(*story, reason, active_provider);
            entry.leave_portal();
        }

        auto snapshot() const -> std::string
        {
            const auto provider = kh02_companion_health();
            nlohmann::json result{{"bootstrap", nlohmann::json::parse(entry.bootstrap_snapshot())},
                                  {"recovery", recovery.snapshot()},
                                  {"provider",
                                   {{"owner_pid", provider.identity.owner_pid},
                                    {"pid", provider.identity.source_pid},
                                    {"session_generation", provider.identity.session_generation},
                                    {"running", provider.running},
                                    {"owned", provider.owned},
                                    {"ready", provider.ready}}},
                                  {"lifecycle",
                                   {{"phase", lifecycle.phase_name()},
                                    {"story_constructed", bool(story)},
                                    {"constructions", constructions},
                                    {"story_polls", story_polls},
                                    {"frame_updates", frame_updates},
                                    {"preparation_ticks", preparation_ticks},
                                    {"exit_cleanups", exit_cleanups},
                                    {"portal_polls", portal_polls},
                                    {"recovery_ticks", recovery_ticks},
                                    {"cleanup_error", cleanup_error}}}};
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            if (!failure_story.empty())
            {
                result["failure_story"] = nlohmann::json::parse(failure_story);
            }
            result["movie_bridge"] = story ? nlohmann::json::parse(story->movie_snapshot())
                                           : nlohmann::json{{"phase", "idle"}, {"playing", false}};
#endif
            return result.dump();
        }

        void observe_destination(UObject* world, Kh02WorldKind kind)
        {
            if (kind == Kh02WorldKind::unavailable)
            {
                return;
            }
            const bool changed = observed_world.Get() != world;
            if (changed)
            {
                if (observed_kind == Kh02WorldKind::portal && kind != Kh02WorldKind::portal)
                {
                    entry.leave_portal();
                }
                observed_world = make_uobject_weak(world);
                observed_kind = kind;
                next_update = {};
            }
            lifecycle.observe(kind, changed);
            if (lifecycle.phase() == Kh02Lifecycle::Phase::leaving)
            {
                // Retire once, including after failure recovery. Do not keep
                // invoking a normal story tick merely to observe departure.
                lifecycle.finish_leaving();
                active_provider = {};
                ready_reward_hero = {};
                if (story)
                {
                    ++exit_cleanups;
                    try
                    {
                        story->leave_world(world);
                    }
                    catch (const std::exception& error)
                    {
                        cleanup_error = error.what();
                        report("kh02_departure_cleanup:" + cleanup_error);
                    }
                }
            }
        }

        void tick()
        {
            auto* world = kh02_current_world();
            const auto kind = kh02_world_kind(world);
            observe_destination(world, kind);
            entry.tick_bootstrap();
            if (lifecycle.phase() == Kh02Lifecycle::Phase::preparing && entry.failed_preparation())
            {
                lifecycle.cancel_preparation();
                observe_destination(world, kind);
            }
            const auto now = std::chrono::steady_clock::now();
            if (recovery.active())
            {
                if (recovery.needs_tick() && now >= next_recovery)
                {
                    next_recovery = now + runtime_update_interval;
                    ++recovery_ticks;
                    recovery.tick(config.player());
                }
                return;
            }
            if (kind == Kh02WorldKind::unavailable)
            {
                return;
            }
            if (kind != Kh02WorldKind::story)
            {
                if (kind == Kh02WorldKind::portal && (entry.portal_work_pending() || now >= next_update))
                {
                    next_update = now + runtime_update_interval;
                    ++portal_polls;
                    auto* portal_pawn = config.player();
                    auto* alternate = character_swap::current_hero();
                    if (character_swap::owns_swap_pawn(alternate))
                    {
                        portal_pawn = alternate;
                    }
                    entry.tick_portal(portal_pawn, config.gameplay_ready() && runtime_sora_control_ready(portal_pawn) &&
                                                       !runtime_sora_game_over(portal_pawn));
                    if (entry.failed_preparation())
                    {
                        lifecycle.cancel_preparation();
                        observe_destination(world, kind);
                    }
                }
                if (lifecycle.phase() == Kh02Lifecycle::Phase::preparing && story && story->preparation_pending())
                {
                    ++preparation_ticks;
                    story->tick_preparation(config.player());
                }
                return;
            }
            auto& active_story = ensure_story();
            // Provider identity stays live before any movie/EOF callback.
            const auto provider = kh02_companion_health();
            if (!provider.ready || (active_provider.source_pid && provider.identity != active_provider))
            {
                fail("kh02_provider_session_lost");
                return;
            }
            active_provider = provider.identity;
            if (const auto reason = active_story.failure(); !reason.empty())
            {
                fail(reason);
                return;
            }
            active_story.tick_enemy_level(config.player());
            const bool frame_pending = active_story.needs_frame();
            if (!frame_pending && now < next_update)
            {
                return;
            }
            auto* pawn = config.player();
            if (frame_pending)
            {
                ++frame_updates;
                active_story.tick_frame(pawn);
                if (const auto reason = active_story.failure(); !reason.empty())
                {
                    fail(reason);
                    return;
                }
            }
            const auto after_frame = std::chrono::steady_clock::now();
            if (after_frame < next_update)
            {
                return;
            }
            next_update = after_frame + runtime_update_interval;
            ++story_polls;
            active_story.tick(pawn, config.gameplay_ready() && !config.game_over());
            if (const auto reason = active_story.failure(); !reason.empty())
            {
                fail(reason);
                return;
            }
            auto* ready_hero = Kh02Story::persistent_reward_context();
            if (ready_hero && ready_reward_hero.Get() != ready_hero && config.shared_unlocks_ready)
            {
                config.shared_unlocks_ready();
            }
            ready_reward_hero = make_uobject_weak(ready_hero);
        }
    };

    Kh02Runtime::Kh02Runtime(Kh02RuntimeConfig config) : impl(std::make_unique<Impl>(std::move(config))) {}

    Kh02Runtime::~Kh02Runtime() = default;

    void Kh02Runtime::initialize()
    {
        impl->entry.initialize();
    }

    void Kh02Runtime::tick(bool unreal_initialized)
    {
        if (!unreal_initialized)
        {
            return;
        }
        try
        {
            impl->tick();
        }
        catch (const std::exception& error)
        {
            impl->fail(error.what());
        }
    }

    auto Kh02Runtime::error() const -> const std::string&
    {
        return impl->last_error;
    }

#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto Kh02Runtime::story_command(RC::Unreal::UObject* pawn, std::wstring_view action) -> std::optional<std::string>
    {
        // Permit the existing half-health test fixture after a failed chapter
        // returns a zero-HP player to the Garden. This does not alter save data
        // directly or widen the living-pawn gates on gameplay commands.
        const bool garden_health_recovery = action == L"flow_recover_garden_health";
        if ((action == L"flow_inventory_health_fixture" || garden_health_recovery) &&
            kh02_world_kind(kh02_current_world()) == Kh02WorldKind::portal)
        {
            auto* controller = try_read_reflected_object_property_value(pawn, L"Controller");
            if (!impl->config.gameplay_ready() || !is_uobject_valid_fast(pawn) ||
                pawn->GetClassPrivate()->GetName() != L"p_ex001_Pawn_C" ||
                kh02_actor_world(pawn) != kh02_current_world() || !is_uobject_valid_fast(controller) ||
                try_read_reflected_object_property_value(controller, L"Pawn") != pawn)
            {
                throw std::runtime_error("kh02_health_fixture_requires_garden_sora");
            }
            Kh02ReflectedCall state(pawn, L"GetStateID", "kh02_health_fixture", 4096);
            Kh02ReflectedCall maximum(pawn, L"GetMaxHitPoint", "kh02_health_fixture", 4096);
            Kh02ReflectedCall before(pawn, L"GetHitPoint", "kh02_health_fixture", 4096);
            state.run();
            maximum.run();
            before.run();
            const auto state_id = read_function_int_param(state.function, state.parameters.data(), L"ReturnValue");
            const auto max_hp = read_function_int_param(maximum.function, maximum.parameters.data(), L"ReturnValue");
            const auto old_hp = read_function_int_param(before.function, before.parameters.data(), L"ReturnValue");
            if (state_id != 1 || !max_hp || *max_hp < 4 || !old_hp)
            {
                throw std::runtime_error("kh02_health_fixture_requires_idle_sora");
            }
            const auto target_hp = garden_health_recovery ? *max_hp : *max_hp / 2;
            Kh02ReflectedCall health(pawn, L"SetHitPoint", "kh02_health_fixture", 4096);
            health.integer(L"InHP", target_hp);
            health.run();
            before.run();
            const auto hp = read_function_int_param(before.function, before.parameters.data(), L"ReturnValue");
            if (hp != target_hp)
            {
                throw std::runtime_error("kh02_health_fixture_readback_mismatch");
            }
            return nlohmann::json{{"kind", "idle_garden_health_fixture"}, {"hp_before", *old_hp},
                                  {"max_hp", *max_hp}, {"hp", *hp}}.dump();
        }
        if (action == L"runtime_inspect")
        {
            return impl->snapshot();
        }
        if (action == L"runtime_recover")
        {
            if (!impl->recovery.active())
            {
                throw std::runtime_error("kh02_recovery_no_failed_session");
            }
            impl->recovery.request_return();
            return impl->snapshot();
        }
        if (action == L"runtime_provider_loss")
        {
            if (!impl->story || !impl->story->active_session())
            {
                throw std::runtime_error("kh02_provider_loss_requires_owned_active_movie");
            }
            const auto provider = kh02_companion_health();
            const auto flow = impl->ensure_story().command(pawn, L"flow_inspect");
            const auto movie = flow ? nlohmann::json::parse(*flow).at("movie_bridge") : nlohmann::json::object();
            if ((!impl->story || !impl->story->active_session()) || !provider.owned || !provider.ready ||
                !movie.value("playing", false) || movie.value("source_pid", 0u) != provider.identity.source_pid)
            {
                throw std::runtime_error("kh02_provider_loss_requires_owned_active_movie");
            }
            impl->active_provider = provider.identity;
            const auto saved = reconciliation::view();
            const nlohmann::json progress_before{{"progress", kh02_progress::to_json(saved->kh02_progress)},
                                                 {"checks", saved->completed_checks},
                                                 {"revision", saved->kh02_progress_revision}};
            if (!stop_kh02_companion_session(provider.identity))
            {
                throw std::runtime_error("kh02_provider_loss_identity_changed");
            }
            impl->fail("kh02_provider_session_lost");
            return nlohmann::json{{"operation", "owned_provider_loss"},
                                  {"owner_pid", provider.identity.owner_pid},
                                  {"stopped_pid", provider.identity.source_pid},
                                  {"stopped_session_generation", provider.identity.session_generation},
                                  {"progress_before", progress_before},
                                  {"movie_before", movie},
                                  {"runtime", nlohmann::json::parse(impl->snapshot())}}
                .dump();
        }
        return impl->ensure_story().command(pawn, action);
    }

    auto Kh02Runtime::portal_command(RC::Unreal::UObject* pawn, std::wstring_view action) -> std::string
    {
        return impl->entry.portal_command(pawn, action);
    }
#endif
} // namespace kh3ap
