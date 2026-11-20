#include "kh3ap/features/story/world_fixes/final_world_qol.h"

#include "kh3ap/app/config.h"
#include "kh3ap/core/events/world_context_events.h"
#include "kh3ap/core/objects/actor_owning_world.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/core/reflection/reflected_function_call.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"

#include "kh3ap/features/player/context/current_player.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/Hooks.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/Property/FNumericProperty.hpp>
#include <array>
#include <atomic>
#include <mutex>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace std::chrono_literals;

    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr int star_area_goal = 12;
        constexpr int main_area_goal = 333;
        constexpr size_t fragment_actor_limit = 16384;
        constexpr auto fragment_class =
            L"/Game/Blueprints/Gimmick/ew/g_ew_heartsora/g_ew_heartsora_01.g_ew_heartsora_01_C";
        constexpr auto star_portal_reaction =
            L"BndEvt__g_ew_portal_01_137_K2Node_ActorBoundEvent_3_DoReaction__DelegateSignature";
        constexpr auto retry_interval = 200ms;
        constexpr auto lich_departure_delay = 200ms;
        constexpr auto streaming_timeout = 30s;
        constexpr auto gameplay = L"/Script/Engine.Default__GameplayStatics";
        constexpr auto lich_missions = std::to_array({
            L"gameflow_EW_MISSION_RA", L"gameflow_EW_MISSION_MI", L"gameflow_EW_MISSION_TS",
            L"gameflow_EW_MISSION_CA", L"gameflow_EW_MISSION_FZ"});
        constexpr int completed_mission = 9999;
        constexpr int final_lich_stage = 310;
        constexpr size_t parameter_limit = 2048;
        std::once_flag pickup_hook;

        enum class Task { idle, enter_main_area, pull_fragment, complete_missions, depart };
        struct Visit
        {
            FWeakObjectPtr world;
            FWeakObjectPtr stream;
            Task task{};
            Clock::time_point next{}, deadline{};
            RuntimeState* collection_runtime{};
            FWeakObjectPtr mission, fragment;
            bool star_pickup{};
            size_t next_mission{};
        } visit;
        uint64_t last_context = UINT64_MAX;

        bool in_collection_story()
        {
            int32_t story{};
            return visit.collection_runtime && try_call_get_game_flag(L"gameflow_EW", story, *visit.collection_runtime,
                settings::s_get_game_flag_path, settings::s_tres_game_blueprint_library_default_object_path, false)
                && story >= 0 && story < completed_mission;
        }

        void schedule_collection(Task task)
        {
            visit.task = task;
            visit.next = Clock::now() + retry_interval;
            visit.deadline = visit.next + streaming_timeout;
        }

        void before_collection_event(UObject* script, UFunction* event, void*)
        {
            if (!event || !visit.collection_runtime) return;
            static const FName pickup(L"HMCount", FNAME_Add);
            // The retail mission's "aaaa" event opens its counter and creates the phantom task.
            static const FName mission_start(L"aaaa", FNAME_Add);
            static const FName star_portal_visible(L"ew_02_camera_ms101s_REvt_app_portal", FNAME_Add);
            const auto action = event->GetNamePrivate();
            if (action != pickup && action != mission_start && action != star_portal_visible) return;
            if (!is_uobject_valid_fast(script)) return;
            auto* type = script->GetClassPrivate();
            if (!is_uobject_valid_fast(type) || event->GetOuterPrivate() != type) return;
            auto* world = visit.world.Get();
            if (!is_uobject_valid_fast(world) || actor_owning_world(script) != world) return;
            static const FName star_area(L"ew_02_HeartStart_C", FNAME_Add);
            static const FName main_area(L"ew_02_HeartMission_C", FNAME_Add);
            static const FName gameflow(L"ew_02_gameflow_C", FNAME_Add);
            const auto name = type->GetNamePrivate();
            if (action == star_portal_visible && name == gameflow && visit.star_pickup)
            {
                visit.star_pickup = false;
                schedule_collection(Task::enter_main_area);
                return;
            }
            if (name != star_area && name != main_area) return;
            if (action == mission_start && name == main_area && in_collection_story())
            {
                visit.star_pickup = false;
                visit.fragment = {};
                visit.mission = make_uobject_weak(script);
                schedule_collection(Task::pull_fragment);
                return;
            }
            if (action != pickup || !in_collection_story()) return;

            const bool main_pickup = name == main_area;
            if (main_pickup && (visit.task != Task::pull_fragment || script != visit.mission.Get())) return;
            auto* field = script->GetPropertyByNameInChain(main_pickup ? L"Heart" : L"HeartCount");
            if (!field || field->GetClass().GetName() != L"IntProperty" ||
                field->GetElementSize() != sizeof(int32_t)) return;
            auto& score = *field->ContainerPtrToValuePtr<int32_t>(script);
            if (score < 0) return;
            if (main_pickup)
            {
                // HMCount adds one, awards the 222 and 333 victory bonuses in order,
                // then clears the mission after its reward handling finishes.
                visit.task = Task::idle;
            }
            const int goal = main_pickup ? main_area_goal : star_area_goal;
            if (score >= goal) return;
            if (!main_pickup) visit.star_pickup = true;
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] Faster Sora Collection {} pickup: {} -> {}\n"),
                main_pickup ? STR("main") : STR("star"), score, goal);
            score = goal - 1;
        }

        void enter_world(UObject* world, RuntimeState& runtime)
        {
            visit = {};
            if (!is_uobject_valid_fast(world)) return;
            visit.world = make_uobject_weak(world);
            const auto path = world->GetPathName();
            if (path == L"/Game/Levels/ew/ew_02/ew_02.ew_02" &&
                seed_bool_option(installed_seed_slot_data(), "faster_sora_collection"))
            {
                std::call_once(pickup_hook, [] { Hook::RegisterProcessEventPreCallback(&before_collection_event); });
                visit.collection_runtime = &runtime;
            }
            if (path == L"/Game/Levels/ew/ew_28/ew_28.ew_28" &&
                seed_bool_option(installed_seed_slot_data(), "faster_lich_sequence"))
            {
                visit.task = Task::complete_missions;
            }
        }

        bool environment_visible(UObject* world)
        {
            auto* stream = visit.stream.Get();
            if (!is_uobject_valid_fast(stream))
            {
                ReflectedFunctionCall find(gameplay, L"GetStreamingLevel", "final_world_qol", parameter_limit);
                find.object(L"WorldContextObject", world);
                find.name(L"PackageName", L"ew_28_ENV");
                find.run();
                stream = find.result();
                if (!is_uobject_valid_fast(stream) || stream->GetOuterPrivate() != world) return false;
                visit.stream = make_uobject_weak(stream);
            }
            ReflectedFunctionCall visible(stream, L"IsLevelVisible", "final_world_qol", parameter_limit);
            visible.run();
            return visible.boolean_result();
        }

        void advance_collection(UObject* world)
        {
            auto* pawn = runtime_ready_player_pawn();
            if (!pawn || actor_owning_world(pawn) != world) return;
            if (!in_collection_story())
            {
                visit.task = Task::idle;
                return;
            }
            if (visit.task == Task::pull_fragment)
            {
                auto* mission = visit.mission.Get();
                if (!is_uobject_valid_fast(mission) || actor_owning_world(mission) != world) return;
                auto* fragment = visit.fragment.Get();
                if (!is_uobject_valid_fast(fragment))
                {
                    auto* type = find_loaded_runtime_asset_class(fragment_class);
                    const auto actors = find_live_actors_of_class(pawn, type, fragment_actor_limit);
                    for (auto* actor : actors)
                    {
                        if (actor->GetOuterPrivate() != mission->GetOuterPrivate()) continue;
                        visit.fragment = make_uobject_weak(actor);
                        fragment = actor;
                        RC::Output::send<RC::LogLevel::Normal>(
                            STR("[KH3APProbeCpp] Faster Sora Collection pulling {}\n"), actor->GetName());
                        break;
                    }
                }
                if (!is_uobject_valid_fast(fragment) || actor_owning_world(fragment) != world) return;
                ReflectedFunctionCall location(pawn, L"K2_GetActorLocation", "final_world_qol", parameter_limit);
                location.run();
                const auto [x, y, z] = location.result_vector();
                ReflectedFunctionCall move(fragment, L"K2_SetActorLocation", "final_world_qol", parameter_limit);
                move.vector(L"NewLocation", x, y, z);
                move.boolean(L"bSweep", false);
                move.boolean(L"bTeleport", true);
                move.run();
                return;
            }
            ReflectedFunctionCall send(settings::s_tres_game_blueprint_library_default_object_path.data(),
                L"RemoteEvent", "final_world_qol", parameter_limit);
            send.object(L"WorldContextObject", pawn);
            send.name(L"EventName", star_portal_reaction);
            visit.task = Task::idle;
            send.run();
            RC::Output::send<RC::LogLevel::Normal>(
                STR("[KH3APProbeCpp] Faster Sora Collection sent {} handled={}\n"), star_portal_reaction, send.boolean_result());
        }

        void advance_lich(UObject* world, RuntimeState& runtime, Clock::time_point now)
        {
            if (visit.task == Task::complete_missions)
            {
                const auto set_flag = [&](std::wstring_view name, int value) {
                    return try_call_set_game_flag(name, value, runtime, settings::s_set_game_flag_path,
                        settings::s_tres_game_blueprint_library_default_object_path, false);
                };
                while (visit.next_mission < lich_missions.size())
                {
                    if (!set_flag(lich_missions[visit.next_mission], completed_mission)) return;
                    ++visit.next_mission;
                }
                if (!set_flag(L"gameflow_EW_MISSION", final_lich_stage)) return;
                visit.task = Task::depart;
                visit.next = now + lich_departure_delay;
                return;
            }
            ReflectedFunctionCall open(gameplay, L"OpenLevel", "final_world_qol", parameter_limit);
            open.object(L"WorldContextObject", world);
            open.name(L"LevelName", L"/Game/Levels/ew/ew_27/ew_27");
            open.boolean(L"bAbsolute", true);
            visit.task = Task::idle;
            open.run();
            RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Faster Lich Sequence opened EW27\n"));
        }
    }

    void tick_final_world_qol(Clock::time_point now, RuntimeState& runtime)
    {
        const auto revision = world_context_revision.load(std::memory_order_acquire);
        if (last_context != revision)
        {
            last_context = revision;
            auto* world = kh02_current_world();
            if (!world || world != visit.world.Get())
            {
                enter_world(world, runtime);
                visit.deadline = now + streaming_timeout;
            }
        }
        if (visit.task == Task::idle) return;
        if (now < visit.next) return;
        if (now > visit.deadline)
        {
            visit.task = Task::idle;
            return;
        }

        visit.next = now + retry_interval;
        auto* world = visit.world.Get();
        if (!is_uobject_valid_fast(world) || world != kh02_current_world())
        {
            visit = {};
            return;
        }
        try
        {
            if (visit.task == Task::complete_missions || visit.task == Task::depart)
            {
                if (environment_visible(world)) advance_lich(world, runtime, now);
            }
            else advance_collection(world);
        }
        catch (const std::exception& error)
        {
            visit.task = Task::idle;
            const std::string message(error.what());
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] Final World QoL failed: {}\n"),
                std::wstring(message.begin(), message.end()));
        }
    }
}
