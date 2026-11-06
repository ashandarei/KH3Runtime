#include "kh02_source_script_registry.h"
#include "kh02_arrival_presentation.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh02_map_session.h"
#include "kh02_arrival_notifications.h"
#include "kh02_loading_completion.h"
#include "kh02_mirror_arrival.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/events/world_context_events.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FMulticastDelegateProperty.hpp>
#define NOMINMAX
#include <Windows.h>
#include <stdexcept>
#include <utility>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr uint64_t arrival_timeout_ms = 90'000;
        constexpr uint64_t loading_observation_interval_ms = 100;
        constexpr uint64_t observed_source_settle_ms = 200;
        constexpr std::size_t arrival_evidence_sample_limit = 128;

        std::string path(UObject* o)
        {
            return is_uobject_valid_fast(o) ? to_utf8(o->GetPathName()) : "";
        }

    } // namespace

    struct Kh02MapSession::Impl
    {
        // Host BeginPlay precedes the source entry delay and final fade. Keep a
        // separate PRE_FG cover so the source can finish its FG fade underneath.
        FWeakObjectPtr arrival_world;
        FWeakObjectPtr entry_fade_source;
        Generation entry_fade_generation{};
        Kh02ArrivalPresentation presentation;
        bool arrival_close_requested{};
        bool source_scene_visible{};
        Kh02MapSessionState state;
        Kh02SourceScriptRegistry<FWeakObjectPtr> sources{state};
        Hooks hooks;
        Kh02ArrivalNotifications notifications;
        bool readiness_requested{};
        bool notification_arrival{};
        uint64_t readiness_notifications{};

        void bind_level_notifications(UObject* world)
        {
            struct StreamArray
            {
                UObject** data;
                int32_t count, capacity;
            };

            auto* property = world->GetPropertyByNameInChain(L"StreamingLevels");
            if (!property || !property->IsA<FArrayProperty>() || property->GetElementSize() != sizeof(StreamArray))
            {
                throw std::runtime_error("kh02_arrival_stream_notification_array");
            }
            auto* streams = property->ContainerPtrToValuePtr<StreamArray>(world);
            if (streams->count < 0 || streams->count > 1024 || (streams->count && !streams->data))
            {
                throw std::runtime_error("kh02_arrival_stream_notification_count");
            }
            const FName callback(L"KH02ArrivalLevelChanged", FNAME_Add);
            for (int32_t index = 0; index < streams->count; ++index)
            {
                auto* stream = streams->data[index];
                if (!is_uobject_valid_fast(stream) || stream->GetOuterPrivate() != world)
                {
                    throw std::runtime_error("kh02_arrival_stream_notification_owner");
                }
                for (const auto* event : {L"OnLevelShown", L"OnLevelHidden"})
                {
                    auto* delegate = stream->GetPropertyByNameInChain(event);
                    if (!delegate || !delegate->IsA<FMulticastDelegateProperty>() ||
                        delegate->GetElementSize() != sizeof(FMulticastScriptDelegate))
                    {
                        throw std::runtime_error("kh02_arrival_stream_notification_delegate");
                    }
                    auto& invocations =
                        delegate->ContainerPtrToValuePtr<FMulticastScriptDelegate>(stream)->InvocationList;
                    bool bound = false;
                    for (const auto& invocation : invocations)
                    {
                        bound |= invocation.Object.Get() == stream && invocation.FunctionName == callback;
                    }
                    if (!bound)
                    {
                        invocations.Add(FScriptDelegate{make_uobject_weak(stream), callback});
                    }
                }
            }
        }

        explicit Impl(Hooks callbacks) : hooks(std::move(callbacks)) {}

        std::wstring arrival_target;
        uint64_t arrival_started{}, arrival_close_at{}, arrival_ready_since{}, arrival_next_check{};
        Json arrival_proof = Json::object();
        std::string arrival_error;

        // Controller ignores are counters: release only our paired claims.

        void arm_arrival(UObject* context, const std::wstring& target)
        {
            entry_fade_source = {};
            arrival_error.clear();
            if (!state.active())
            {
                arrival_started = GetTickCount64();
                arrival_proof = {
                    {"target", to_utf8(target)}, {"armed_at_ms", arrival_started}, {"samples", Json::array()}};
            }
            state.arm();
            arrival_target = target;
            presentation.bind_context(context);
            arrival_world = {};
            arrival_close_requested = false;
            source_scene_visible = false;
            arrival_close_at = arrival_ready_since = 0;
            presentation.cover(context, 1);
        }

        void begin_arrival(UObject* w)
        {
            const auto name = w->GetPathName();
            if (!state.active() || arrival_world.Get() != w)
            {
                arm_arrival(w, name);
            }
            arrival_world = make_uobject_weak(w);
            notification_arrival = name == L"/Game/Maps/APKH02CastleTown/APKH02CastleTown.APKH02CastleTown";
            if (notification_arrival)
            {
                notifications.arm(w);
                bind_level_notifications(w);
            }
            else
            {
                notifications.cancel();
            }
            arrival_proof["readiness_mode"] = notification_arrival ? "notifications" : "observed";
            readiness_requested = true;
            arrival_proof["host_begin_at_ms"] = GetTickCount64();
            if (Kh02ArrivalPresentation::loading_closed())
            {
                Kh02ArrivalPresentation::open_loading();
            }
        }

        void reveal_arrival(UObject* context)
        {
            presentation.reveal(context);
            transition(Kh02ArrivalPhase::Fading);
        }

        void source_loading_ready(UObject* script)
        {
            auto* w = kh02_actor_world(script);
            if (state.active() && is_uobject_valid_fast(w) && w->GetPathName() == arrival_target)
            {
                hooks.prepare_music();
                arrival_world = make_uobject_weak(w);
                arrival_close_requested = true;
                arrival_close_at = GetTickCount64();
                arrival_proof["source_close_at_ms"] = arrival_close_at;
                arrival_proof["source"] = path(script);
                if (notification_arrival)
                {
                    bind_level_notifications(w);
                }
                readiness_requested = true;
                return;
            }
            Kh02ArrivalPresentation::close_loading();
        }

        void tick_arrival(UObject* pawn)
        {
            if (!state.active())
            {
                return;
            }
            auto* context = is_uobject_valid_fast(pawn) ? pawn : presentation.context();
            if (!is_uobject_valid_fast(context))
            {
                return;
            }
            auto* w = is_uobject_valid_fast(pawn) ? kh02_actor_world(pawn) : arrival_world.Get();
            const auto now = GetTickCount64();
            try
            {
                if (!notifications.failure().empty())
                {
                    throw std::runtime_error(notifications.failure());
                }
                const auto error = hooks.entry_error(w);
                if (!error.empty())
                {
                    throw std::runtime_error(error);
                }
                if (now - arrival_started > arrival_timeout_ms)
                {
                    throw std::runtime_error("kh02_arrival_timeout");
                }
                if (state.phase() == Kh02ArrivalPhase::Opening)
                {
                    presentation.cover(context, 1);
                    auto opening = hooks.opening_frame(w);
                    if (opening.is_null())
                    {
                        return;
                    }
                    opening["at_ms"] = now;
                    arrival_proof["opening_reveal"] = std::move(opening);
                    reveal_arrival(context);
                    return;
                }
                if (state.phase() == Kh02ArrivalPhase::Fading)
                {
                    if (presentation.changing(context))
                    {
                        return;
                    }
                    presentation.release_input();
                    arrival_proof["damage_protection_restored"] = presentation.damage_protection_restored();
                    transition(Kh02ArrivalPhase::Completed);
                    arrival_proof["released_at_ms"] = now;
                    return;
                }
                presentation.hold_input(pawn);
                presentation.cover(context, source_scene_visible ? 0.f : 1.f);
                if (w != arrival_world.Get() || !is_uobject_valid_fast(w))
                {
                    return;
                }
                if ((!notification_arrival || state.phase() == Kh02ArrivalPhase::ClosingLoading) &&
                    now < arrival_next_check)
                {
                    return;
                }
                if (!notification_arrival)
                {
                    arrival_next_check = now + loading_observation_interval_ms;
                }
                if (state.phase() == Kh02ArrivalPhase::ClosingLoading)
                {
                    arrival_next_check = now + loading_observation_interval_ms;
                    const auto completion = kh02_finish_loading(pawn);
                    arrival_proof["native_loading_complete"] = completion.complete;
                    if (completion.prompt_ready && !completion.complete)
                    {
                        // Reading a loading screen is an unbounded player wait,
                        // not an asset-loading timeout or a failed arrival.
                        arrival_started = now;
                        arrival_proof["native_continue_waiting"] = true;
                    }
                    if (completion.dispatched)
                    {
                        arrival_proof["native_continue_at_ms"] = now;
                    }
                    if (!completion.complete || !Kh02ArrivalPresentation::loading_closed())
                    {
                        return;
                    }
                    // KH3's gameplay script normally releases the title Menu
                    // slot. Do it once after native loading, before exposing
                    // gameplay/camp input. Evidence records the result only.
                    state.release_menu_music_once(
                        [&] { arrival_proof["menu_music_released"] = hooks.release_menu_music(now); });
                    if (hooks.source_scene_playing(w))
                    {
                        // A resumed Matinee already owns this controller. A
                        // character swap would leave its director driving the
                        // retired controller and expose the new field camera.
                        // Show the authored scene while keeping arrival pending;
                        // the source owns cinematic input and its completion.
                        if (!source_scene_visible)
                        {
                            source_scene_visible = true;
                            presentation.cover(context, 0);
                            arrival_proof["source_scene_revealed"] = true;
                        }
                        arrival_started = now;
                        return;
                    }
                    if (source_scene_visible)
                    {
                        source_scene_visible = false;
                        presentation.cover(context, 1);
                        arrival_proof["source_scene_finished_at_ms"] = now;
                    }
                    if (!hooks.finish_loading(pawn))
                    {
                        return;
                    }
                    if (hooks.opening_required(w))
                    {
                        hooks.retire_opening_music();
                        // Release only our input claims so the native cinematic
                        // dispatcher can take ownership while the cover stays up.
                        presentation.release_input();
                        transition(Kh02ArrivalPhase::Opening);
                        arrival_proof["opening_wait_at_ms"] = now;
                        return;
                    }
                    reveal_arrival(context);
                    return;
                }
                if (Kh02ArrivalPresentation::loading_closed())
                {
                    Kh02ArrivalPresentation::open_loading();
                }
                if (!arrival_close_requested || !hooks.source_initialized(w) ||
                    (!notification_arrival && now - arrival_close_at < observed_source_settle_ms))
                {
                    return;
                }
                if (notifications.take_completion())
                {
                    readiness_requested = true;
                }
                if (notifications.pending_fade())
                {
                    arrival_ready_since = 0;
                    return;
                }
                if (notification_arrival && !readiness_requested && !arrival_ready_since)
                {
                    return;
                }
                readiness_requested = false;
                const auto readiness = hooks.source_readiness(context, w);
                const auto& view = readiness.at("view");
                const bool settled = readiness.at("settled").get<bool>();
                const bool ready = readiness.at("ready").get<bool>();
                auto* fade_source = entry_fade_source.Get();
                if (settled && !view.at("changing").get<bool>() &&
                    is_uobject_valid_fast(fade_source) && kh02_actor_world(fade_source) == w &&
                    sources.accepts(entry_fade_generation))
                {
                    // The imported field gameflow closes loading, but its source
                    // persistent level owns the ordinary entry fade-in. The
                    // KH3 host replaces that level and must perform this step.
                    entry_fade_source = {};
                    if (!ready)
                    {
                        arrival_proof["host_entry_fade"] = {{"source", path(fade_source)}, {"before", view}};
                        presentation.reveal_source(context);
                        arrival_next_check = 0;
                        return;
                    }
                }
                auto& samples = arrival_proof["samples"];
                if (samples.size() < arrival_evidence_sample_limit)
                {
                    samples.push_back({{"at_ms", now},
                                       {"source_fade", view},
                                       {"streams_settled", settled},
                                       {"damage_protected", presentation.damage_protection_claimed()},
                                       {"move_blocked", presentation.movement_claimed()}});
                }
                if (!ready)
                {
                    arrival_ready_since = 0;
                    return;
                }
                if (!arrival_ready_since)
                {
                    arrival_ready_since = now;
                    // The source fade, requested levels and entry script have
                    // all settled. Confirm them on the next frame instead of
                    // adding another fixed 200 ms to every successful arrival.
                    arrival_next_check = now;
                    return;
                }
                if (now == arrival_ready_since)
                {
                    return;
                }
                arrival_proof["ready_at_ms"] = now;
                arrival_proof["fade_notifications"] = notifications.snapshot();
                arrival_proof["readiness_notifications"] = readiness_notifications;
                notifications.cancel();
                Kh02ArrivalPresentation::close_loading();
                transition(Kh02ArrivalPhase::ClosingLoading);
            }
            catch (const std::exception& e)
            {
                arrival_error = e.what();
                arrival_proof["error"] = e.what();
                arrival_proof["fade_notifications"] = notifications.snapshot();
                notifications.cancel();
                transition(Kh02ArrivalPhase::Error);
                // Keep the original error visible even if presentation cleanup
                // fails, and always attempt every independent owned release.
                const auto restore = [](auto&& operation)
                {
                    try
                    {
                        operation();
                    }
                    catch (...)
                    {
                    }
                };
                restore([&] { presentation.cover(context, 0); });
                restore([&] { presentation.release_input(); });
                restore([&] { Kh02ArrivalPresentation::close_loading(); });
            }
        }

        void transition(Kh02ArrivalPhase phase)
        {
            if (!state.transition(phase))
            {
                throw std::runtime_error("kh02_arrival_invalid_transition");
            }
        }

        void cancel() noexcept
        {
            try
            {
                if (state.active())
                {
                    if (auto* context = presentation.context(); is_uobject_valid_fast(context))
                    {
                        presentation.cover(context, 0);
                    }
                }
            }
            catch (...)
            {
            }
            try
            {
                presentation.release_input();
            }
            catch (...)
            {
            }
            arrival_world = {};
            entry_fade_source = {};
            notifications.cancel();
            readiness_requested = false;
            notification_arrival = false;
            readiness_notifications = 0;
            presentation.reset_bindings();
            arrival_close_requested = false;
            state.transition(Kh02ArrivalPhase::Idle);
        }
    };

    Kh02MapSession::Kh02MapSession(Hooks hooks) : impl(std::make_unique<Impl>(std::move(hooks))) {}

    Kh02MapSession::~Kh02MapSession()
    {
        cancel();
    }

    void Kh02MapSession::observe_world(UObject* world)
    {
        if (!is_uobject_valid_fast(world) || impl->sources.world().Get() == world)
        {
            return;
        }
        // An already armed destination cover must survive the old-world handoff.
        const bool destination_arrival = impl->state.active() && world->GetPathName() == impl->arrival_target;
        // Retire callback authority before resource release. A failing release
        // must not leave an old generation eligible to resume its source graph.
        impl->sources.replace_world(make_uobject_weak(world));
        // Imported hosts do not pass through the ordinary KH3 map-context
        // producer. Wake character restoration even for cinematic arrivals
        // such as Destiny Islands, which do not use the field loading cover.
        world_context_revision.fetch_add(1, std::memory_order_release);
        if (!destination_arrival)
        {
            cancel();
        }
        else
        {
            impl->presentation.release_input();
        }
        impl->hooks.retire_presentation();
    }

    void Kh02MapSession::bind_source_script(UObject* script)
    {
        if (!is_uobject_valid_fast(script))
        {
            return;
        }
        observe_world(kh02_actor_world(script));
        const auto identity = script->GetClassPrivate()->GetPathName();
        if (impl->sources.replacing(identity,
                                    [script](const FWeakObjectPtr& previous) { return previous.Get() == script; }))
        {
            invalidate_source_script();
        }
        impl->sources.record(identity, make_uobject_weak(script));
    }

    void Kh02MapSession::invalidate_source_script()
    {
        impl->sources.invalidate();
        impl->hooks.retire_presentation();
    }

    auto Kh02MapSession::generation() const -> Generation
    {
        return impl->sources.generation();
    }

    auto Kh02MapSession::accepts(Generation generation) const -> bool
    {
        return impl->sources.accepts(generation);
    }

    auto Kh02MapSession::accepts_continuation(Generation generation, UObject* world) const -> bool
    {
        return impl->sources.accepts_continuation(generation, owns_world(world));
    }

    auto Kh02MapSession::owns_world(UObject* world) const -> bool
    {
        return is_uobject_valid_fast(world) && impl->sources.world().Get() == world;
    }

    void Kh02MapSession::arm(UObject* context, const std::wstring& target)
    {
        impl->arm_arrival(context, target);
    }

    void Kh02MapSession::begin_arrival(UObject* world)
    {
        observe_world(world);
        impl->begin_arrival(world);
    }

    void Kh02MapSession::source_loading_ready(UObject* script)
    {
        if (!is_uobject_valid_fast(script) || !owns_world(kh02_actor_world(script)))
        {
            return;
        }
        if (!impl->sources.accepts_script(script->GetClassPrivate()->GetPathName(),
                                          [script](const FWeakObjectPtr& previous)
                                          { return previous.Get() == script; }))
        {
            return;
        }
        impl->source_loading_ready(script);
        if (kh02_field_host_entry_fade(kh02_actor_world(script)->GetPathName()))
        {
            // Normal mirror/door travel retains the departure's foreground
            // fade. These replacement field hosts must finish the persistent
            // level's entry fade after the source streams have settled.
            request_entry_fade(script);
        }
    }

    void Kh02MapSession::request_entry_fade(UObject* script)
    {
        if (!impl->state.active() || !is_uobject_valid_fast(script) ||
            !owns_world(kh02_actor_world(script)))
        {
            return;
        }
        impl->entry_fade_source = make_uobject_weak(script);
        impl->entry_fade_generation = impl->sources.generation();
    }

    void Kh02MapSession::observe_nonlatent_arrival_fade()
    {
        if (!impl->state.active() || !impl->notification_arrival)
        {
            return;
        }
        // The tower retry ends its fade without a latent continuation. Reuse
        // the bounded 100 ms arrival observer until that animation settles;
        // there is no completion notification to wait for on this route.
        impl->notification_arrival = false;
        impl->notifications.cancel();
        impl->readiness_requested = true;
        impl->arrival_next_check = 0;
        impl->arrival_proof["readiness_mode"] = "observed_nonlatent_fade";
    }

    void Kh02MapSession::tick(UObject* pawn)
    {
        impl->tick_arrival(pawn);
    }

    void Kh02MapSession::notify_readiness()
    {
        if (impl->state.active())
        {
            impl->readiness_requested = true;
            ++impl->readiness_notifications;
        }
    }

    void Kh02MapSession::cancel() noexcept
    {
        impl->cancel();
    }

    auto Kh02MapSession::active() const -> bool
    {
        return impl->state.active();
    }

    auto Kh02MapSession::world() const -> UObject*
    {
        return impl->arrival_world.Get();
    }

    auto Kh02MapSession::phase() const -> Kh02ArrivalPhase
    {
        return impl->state.phase();
    }

    auto Kh02MapSession::failure() const -> const std::string&
    {
        static const std::string no_failure;
        return phase() == Kh02ArrivalPhase::Error ? impl->arrival_error : no_failure;
    }

    auto Kh02MapSession::proof() -> Json&
    {
        return impl->arrival_proof;
    }

    auto Kh02MapSession::snapshot() const -> Json
    {
        return {{"active", active()},
                {"phase", impl->state.phase_name()},
                {"proof", impl->arrival_proof},
                {"generation", generation()},
                {"source_script_count", impl->sources.size()}};
    }

    auto Kh02MapSession::loading_closed() -> bool
    {
        return Kh02ArrivalPresentation::loading_closed();
    }
} // namespace kh3ap
