#include "kh02_mirror_transition_diagnostics.h"
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include "kh02_runtime_inspection.h"
#include "kh3ap/features/kh02/cutscenes/kh02_intro_presentation.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/story/kh02_arrival_presentation.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <chrono>
#include <algorithm>
#include <string>

namespace kh3ap::kh02_mirror_transition_diagnostics
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;

    namespace
    {
        constexpr auto sample_delay = std::chrono::seconds(10);
        constexpr auto maximum_capture_time = std::chrono::seconds(30);
        constexpr uint32_t maximum_events = 64;
        FWeakObjectPtr observed_world, observed_script;
        Clock::time_point sample_at{};
        Clock::time_point capture_until{};
        uint64_t sequence{};
        uint32_t event_count{};
        int32_t last_linkage{};
        std::string observed_mirror;
        bool sample_pending{};

        void emit(std::string_view stage, bool include_streams)
        {
            auto* world = observed_world.Get();
            auto* script = observed_script.Get();
            if (!is_uobject_valid_fast(world) || !is_uobject_valid_fast(script) ||
                world != kh02_current_world() || kh02_actor_world(script) != world)
            {
                return;
            }
            Json record = {{"sequence", sequence},
                           {"event", event_count},
                           {"stage", stage},
                           {"mirror_event", observed_mirror},
                           {"linkage", last_linkage},
                           {"world", to_utf8(world->GetPathName())},
                           {"script", to_utf8(script->GetPathName())}};
            try
            {
                Kh02IntroPresentation presentation;
                record["fade"] = presentation.fade(script);
                record["loading_closed"] = Kh02ArrivalPresentation::loading_closed();
                auto* hero = kh02_story_player(nullptr);
                if (is_uobject_valid_fast(hero) && kh02_actor_world(hero) == world)
                {
                    record["pawn"] = to_utf8(hero->GetPathName());
                    auto* id = hero->GetPropertyByNameInChain(L"m_PlayerUniqueID");
                    if (id && id->GetElementSize() == sizeof(uint8_t))
                    {
                        record["player_id"] = *id->ContainerPtrToValuePtr<uint8_t>(hero);
                    }
                }
                Kh02ReflectedCall paused(L"/Script/Engine.Default__GameplayStatics", L"IsGamePaused",
                                         "kh02_mirror_trace", 64);
                paused.object(L"WorldContextObject", script);
                paused.run();
                record["paused"] = paused.boolean_result();
                if (include_streams)
                {
                    record["streams"] = kh02_stream_states(world, L"/Game/");
                }
            }
            catch (const std::exception& failure)
            {
                record["inspection_error"] = failure.what();
            }
            const auto serialized = record.dump(-1, ' ', true);
            RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] KH02MirrorTrace {}\n"),
                                                   std::wstring(serialized.begin(), serialized.end()));
        }
    } // namespace

    void observe(UObject* world, UObject* script, int32_t linkage, std::string_view stage,
                 std::string_view mirror_event)
    {
        const auto now = Clock::now();
        if (stage == "mirror_touch")
        {
            observed_world = make_uobject_weak(world);
            observed_script = make_uobject_weak(script);
            ++sequence;
            event_count = 0;
            observed_mirror = mirror_event;
            capture_until = now + maximum_capture_time;
        }
        else if (!sample_pending || observed_world.Get() != world || observed_script.Get() != script)
        {
            return;
        }
        else if (last_linkage == linkage)
        {
            return;
        }
        last_linkage = linkage;
        if (event_count < maximum_events)
        {
            ++event_count;
            emit(stage, false);
        }
        // One deferred snapshot captures a missing continuation too. Only a
        // deadline check runs meanwhile; no idle discovery or repeated sampling.
        sample_at = std::min(now + sample_delay, capture_until);
        sample_pending = true;
    }

    auto pending() -> bool
    {
        return sample_pending;
    }

    void tick()
    {
        if (!sample_pending)
        {
            return;
        }
        auto* world = observed_world.Get();
        if (!is_uobject_valid_fast(world) || world != kh02_current_world() ||
            !is_uobject_valid_fast(observed_script.Get()))
        {
            sample_pending = false;
            return;
        }
        if (Clock::now() < sample_at)
        {
            return;
        }
        sample_pending = false;
        emit("settled_sample", true);
    }
} // namespace kh3ap::kh02_mirror_transition_diagnostics
#endif
