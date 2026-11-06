#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/session/command_sources.h"
#include "kh3ap/features/world/portals/map_jump_command_helpers.h"
#include "kh3ap/features/world/portals/map_jump_player_start_policy.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/hooks/hook_phase.h"
#include "kh3ap/core/types.h"
#include "kh02/runtime_metadata.h"
#include "kh02/mirror_host_metadata.h"

#include "kh3ap/bridge/transport/bridge_paths.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/session/command_guard.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/kh02/runtime/kh02_entry_service.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/world/save_destination.h"
#include "kh3ap/app/state.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <chrono>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr uint8_t map_jump_fade_none = 0;
        constexpr uint8_t map_jump_fade_out = 1;
        constexpr uint8_t map_jump_white_out = 2;
        constexpr uint8_t map_jump_wipe_out = 3;

        constexpr auto max_map_jump_player_starts = 1024;
        constexpr auto max_map_jump_argument_characters = 512;
    } // namespace

    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        MapJumpPlayerStartRequest automatic_start;

        auto preferred_map_start(std::wstring_view package) -> std::wstring_view
        {
            for (const auto& area : kh02metadata::visited_areas)
            {
                if (package == area.package)
                {
                    return area.tag;
                }
            }
            for (const auto& room : kh02mirror_metadata::rooms)
            {
                if (package == room.package && room.start_count > 0)
                {
                    return room.starts[0].tag;
                }
            }
            if (package == L"/Game/Maps/GardenOfAssemblage/umap/GoA_01" ||
                package == L"/Game/Maps/GardenOfAssemblage/umap/GoA_02")
            {
                return L"NormalSpawn";
            }
            return {};
        }

        struct MapJumpRequest
        {
            std::wstring request_id{};
            std::wstring map_name{};
            std::wstring target_tag{};
            std::wstring fade_kind{STR("MAPJUMP_FADE_KIND_FADEOUT")};
            uint8_t fade_kind_value{1};
        };

        auto lower_copy(std::wstring_view value) -> std::wstring
        {
            std::wstring lowered{value};
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](wchar_t ch)
                           {
                               return static_cast<wchar_t>(std::towlower(ch));
                           });
            return lowered;
        }

        auto parse_fade_kind(std::wstring_view value, uint8_t& out_value) -> bool
        {
            const std::wstring lowered = lower_copy(value);
            if (lowered == STR("mapjump_fade_kind_none") || lowered == STR("none"))
            {
                out_value = map_jump_fade_none;
                return true;
            }
            if (lowered == STR("mapjump_fade_kind_fadeout") || lowered == STR("fadeout"))
            {
                out_value = map_jump_fade_out;
                return true;
            }
            if (lowered == STR("mapjump_fade_kind_whiteout") || lowered == STR("whiteout"))
            {
                out_value = map_jump_white_out;
                return true;
            }
            if (lowered == STR("mapjump_fade_kind_wipeout") || lowered == STR("wipeout"))
            {
                out_value = map_jump_wipe_out;
                return true;
            }
            return false;
        }

        auto is_valid_name_value(std::wstring_view value, bool allow_slash) -> bool
        {
            if (value.empty() || value.size() > max_map_jump_argument_characters)
            {
                return false;
            }
            return std::all_of(value.begin(), value.end(),
                               [allow_slash](wchar_t ch)
                               {
                                   return std::iswalnum(ch) != 0 || ch == L'_' || ch == L'-' ||
                                          ch == L'.' || (allow_slash && ch == L'/');
                               });
        }

        auto parse_request(const std::string& text, MapJumpRequest& request,
                           std::wstring& failure_reason) -> bool
        {
            request.request_id =
                JsonFieldReader::string_field(text, "request_id").value_or(std::wstring{});
            request.map_name =
                JsonFieldReader::string_field(text, "map_name").value_or(std::wstring{});
            request.target_tag =
                JsonFieldReader::string_field(text, "target_tag").value_or(std::wstring{});
            request.fade_kind = JsonFieldReader::string_field(text, "fade_kind")
                                    .value_or(STR("MAPJUMP_FADE_KIND_FADEOUT"));

            if (request.request_id.empty())
            {
                const std::string observed_at = now_iso8601_utc();
                request.request_id =
                    STR("map-jump-") + std::wstring{observed_at.begin(), observed_at.end()};
            }
            const auto guard_failure = runtime_command_request_failure(
                text, GetCurrentProcessId(),
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
            if (!guard_failure.empty())
            {
                failure_reason = guard_failure;
                return false;
            }
            if (!is_valid_name_value(request.map_name, true) ||
                !request.map_name.starts_with(STR("/Game/")))
            {
                failure_reason = STR("invalid_map_name");
                return false;
            }
            if (!is_valid_name_value(request.target_tag, false))
            {
                failure_reason = STR("invalid_target_tag");
                return false;
            }
            if (!parse_fade_kind(request.fade_kind, request.fade_kind_value))
            {
                failure_reason = STR("invalid_fade_kind");
                return false;
            }
            return true;
        }

        auto serialize_result(const RuntimeMapJumpCommandDeps& deps, const MapJumpRequest& request,
                              std::wstring_view status, std::wstring_view reason) -> std::string
        {
            std::ostringstream json;
            json << "{";
            json << "\"schema\":\"kh3ap.runtime_map_jump_result.v1\"";
            json << ",\"protocol_name\":\"" << json_escape(deps.protocol_name) << "\"";
            json << ",\"protocol_version\":" << deps.protocol_version;
            json << ",\"event\":\"runtime_map_jump_result\"";
            json << ",\"status\":\"" << json_escape(status) << "\"";
            json << ",\"reason\":\"" << json_escape(reason) << "\"";
            json << ",\"request_id\":\"" << json_escape(request.request_id) << "\"";
            json << ",\"process_id\":" << GetCurrentProcessId();
            json << ",\"map_name\":\"" << json_escape(request.map_name) << "\"";
            json << ",\"target_tag\":\"" << json_escape(request.target_tag) << "\"";
            json << ",\"fade_kind\":\"" << json_escape(request.fade_kind) << "\"";
            json << ",\"fade_kind_value\":" << static_cast<unsigned>(request.fade_kind_value);
            json << ",\"build_stamp\":\"" << json_escape(deps.build_stamp) << "\"";
            json << ",\"observed_at\":\"" << now_iso8601_utc() << "\"";
            json << "}\n";
            return json.str();
        }

        auto write_result(const RuntimeMapJumpCommandDeps& deps, const MapJumpRequest& request,
                          std::wstring_view status, std::wstring_view reason, bool append_event)
            -> void
        {
            if (!deps.bridge_dir.has_value())
            {
                return;
            }
            const std::string payload = serialize_result(deps, request, status, reason);
            write_text_file(*deps.bridge_dir /
                                std::filesystem::path{std::wstring{deps.result_filename}},
                            payload);
            if (append_event)
            {
                append_text_file(*deps.bridge_dir /
                                     std::filesystem::path{std::wstring{deps.events_filename}},
                                 payload);
            }
        }

        auto invoke_map_jump(const RuntimeMapJumpCommandDeps& deps, const MapJumpRequest& request,
                             std::wstring& failure_reason) -> bool
        {
            UFunction* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, deps.map_jump_function_path);
            UObject* self_object =
                RuntimeObjectResolver::try_get_tres_game_blueprint_library_default_object(
                    deps.runtime_state.object_cache(), deps.blueprint_library_default_object_path);
            if (!self_object)
            {
                self_object = try_get_function_default_object(function_object);
            }
            if (!function_object || !self_object || !kh3ap::is_uobject_valid_fast(self_object))
            {
                failure_reason = STR("map_jump_function_or_default_object_unavailable");
                return false;
            }

            std::vector<std::byte> params(static_cast<size_t>(function_object->GetParmsSize()));
            if (params.empty())
            {
                failure_reason = STR("map_jump_params_unavailable");
                return false;
            }
            std::fill(params.begin(), params.end(), std::byte{0});

            const bool wrote_map = write_name_or_string_function_param(
                find_function_param_by_name(function_object, STR("MapName")), params.data(),
                request.map_name);
            const bool wrote_tag = write_name_or_string_function_param(
                find_function_param_by_name(function_object, STR("TargetTag")), params.data(),
                request.target_tag);
            const bool wrote_fade = write_integral_function_param(
                find_function_param_by_name(function_object, STR("InFadeKind")), params.data(),
                request.fade_kind_value);
            if (!wrote_map || !wrote_tag || !wrote_fade)
            {
                failure_reason = STR("map_jump_parameter_layout_mismatch");
                return false;
            }

            const RuntimeGrantInternalCallDepth::Scope internal_call_scope{};
            automatic_start.clear();
            if (request.target_tag == map_jump_auto_start_tag)
            {
                automatic_start.arm(request.map_name, MapJumpPlayerStartRequest::Clock::now());
            }
            self_object->ProcessEvent(function_object, params.data());
            return true;
        }
    } // namespace

    auto handle_map_jump_player_start(UnrealScriptFunctionCallableContext& callback,
                                      HookTargetInfo& hook, HookPhase phase) -> void
    {
        const auto now = MapJumpPlayerStartRequest::Clock::now();
        if (phase != HookPhase::Post || !automatic_start.pending(now) || !callback.RESULT_DECL ||
            !is_uobject_valid_fast(callback.Context))
        {
            return;
        }
        try
        {
            auto* world = kh02_actor_world(callback.Context);
            if (!is_uobject_valid_fast(world) ||
                !automatic_start.matches(world->GetPathName(), now))
            {
                return;
            }
            auto* function = hook.function_object;
            if (!is_uobject_valid_fast(function))
            {
                function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
                                                                        hook.function_path);
            }
            auto* parameters = get_frame_locals(callback.TheStack);
            if (!is_uobject_valid_fast(function) || !parameters ||
                read_function_param_value_as_string(function, parameters, L"IncomingName") !=
                    map_jump_auto_start_tag)
            {
                return;
            }
            auto* controller = try_read_function_object_param(function, parameters, L"Player");
            auto* player_controller_class =
                find_loaded_runtime_asset_class(L"/Script/Engine.PlayerController");
            if (!is_uobject_a_fast(controller, player_controller_class) ||
                kh02_actor_world(controller) != world)
            {
                return;
            }
            const auto world_name = world->GetPathName();
            const auto package = std::wstring_view(world_name).substr(0, world_name.rfind(L'.'));
            const auto preferred = preferred_map_start(package);
            auto* start_class = find_loaded_runtime_asset_class(L"/Script/Engine.PlayerStart");
            UObject* selected = nullptr;
            int best_rank = -1;
            std::wstring selected_tag, selected_path;
            for (auto* start : find_live_actors_of_class(callback.Context, start_class,
                                                         max_map_jump_player_starts))
            {
                if (!is_uobject_valid_fast(start) || kh02_actor_world(start) != world ||
                    start->HasAnyFlags(static_cast<EObjectFlags>(RF_NeedLoad | RF_NeedPostLoad |
                                                                 RF_NeedPostLoadSubobjects)))
                {
                    continue;
                }
                auto* tag_property = start->GetPropertyByNameInChain(L"PlayerStartTag");
                if (!tag_property || tag_property->GetClass().GetName() != L"NameProperty" ||
                    tag_property->GetElementSize() != sizeof(FName))
                {
                    continue;
                }
                const auto tag = tag_property->ContainerPtrToValuePtr<FName>(start)->ToString();
                const auto rank = map_jump_start_rank(tag, preferred);
                const auto path = start->GetPathName();
                if (rank >= 0 &&
                    (rank > best_rank ||
                     (rank == best_rank &&
                      (tag < selected_tag || (tag == selected_tag && path < selected_path)))))
                {
                    selected = start;
                    best_rank = rank;
                    selected_tag = tag;
                    selected_path = path;
                }
            }
            automatic_start.clear();
            if (selected)
            {
                *static_cast<UObject**>(callback.RESULT_DECL) = selected;
                Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Map jump automatic PlayerStart "
                                                   "world={} tag={} actor={} rank={}\n"),
                                               world_name, selected_tag, selected_path, best_rank);
            }
            else
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Map jump found no suitable PlayerStart world={}; "
                        "retaining native selection\n"),
                    world_name);
            }
        }
        catch (const std::exception& error)
        {
            automatic_start.clear();
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] Map jump PlayerStart selection failed: {}\n"),
                std::wstring(error.what(), error.what() + std::strlen(error.what())));
        }
    }

    auto process_pending_runtime_map_jump_command(const RuntimeMapJumpCommandDeps& deps) -> void
    {
        if (!runtime_command_source_allowed(deps.request_filename ==
                                            channels::runtime_warp_command_file))
        {
            return;
        }

        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        const auto request_path =
            *deps.bridge_dir / std::filesystem::path{std::wstring{deps.request_filename}};
        if (!path_points_to_regular_file(request_path))
        {
            return;
        }
        const auto request_text = read_text_file(request_path);
        if (!request_text.has_value())
        {
            return;
        }

        MapJumpRequest request{};
        std::wstring failure_reason{};
        if (!parse_request(*request_text, request, failure_reason))
        {
            write_result(deps, request, STR("failed"), failure_reason, true);
            try_remove_file(request_path);
            return;
        }

        UObject* pawn = character_swap::controlled_travel_pawn();
        auto* hero = kh02_story_player(nullptr);
        auto* alternate = character_swap::current_hero();
        if (character_swap::owns_swap_pawn(alternate))
        {
            pawn = runtime_sora_control_ready(alternate) ? alternate : nullptr;
        }
        else if (is_uobject_valid_fast(hero) &&
                 hero->GetClassPrivate()->GetName() == L"p_ex002_Pawn_C")
        {
            // Travel follows the possessed KH0.2 hero independently of the AP
            // grant cache, which must continue to require playable Sora.
            auto* world = kh02_current_story_world();
            if (is_uobject_valid_fast(world) && kh02_actor_world(hero) == world &&
                runtime_sora_control_ready(hero))
            {
                pawn = hero;
            }
            else
            {
                pawn = nullptr;
            }
        }
        else if (!is_uobject_valid_fast(pawn) ||
                 !RuntimeObjectResolver::is_sora_ability_pawn_candidate(pawn))
        {
            pawn = nullptr;
        }
        if (!is_uobject_valid_fast(pawn))
        {
            write_result(deps, request, STR("waiting"), STR("missing_safe_player_pawn"), false);
            return;
        }

        const bool kh02_destination = kh02_map_package(std::wstring_view(request.map_name));
        const bool dark_world_destination =
            std::wstring_view(request.map_name).starts_with(L"/Game/Levels/dw/");
        if ((kh02_destination || dark_world_destination) && !kh02_current_story_world() &&
            !character_swap::prepare_native_story_travel())
        {
            write_result(deps, request, STR("waiting"), STR("returning_to_sora_for_story_travel"),
                         false);
            return;
        }

        if (kh02_destination)
        {
            // Match the portal's pre-travel preparation. Returning to GoA
            // releases source asset roots, so host BeginPlay is too late to
            // preload them or arm the Castle Town source entry.
            try
            {
                if (!Kh02EntryService::prepare_startup(pawn))
                {
                    write_result(deps, request, STR("waiting"), STR("preparing_kh02_castle_entry"),
                                 false);
                    return;
                }
            }
            catch (const std::exception&)
            {
                write_result(deps, request, STR("failed"),
                             STR("kh02_castle_entry_preparation_failed"), true);
                try_remove_file(request_path);
                return;
            }
        }

        const bool invoked = invoke_map_jump(deps, request, failure_reason);
        write_result(deps, request, invoked ? STR("completed") : STR("failed"), failure_reason,
                     true);
        try_remove_file(request_path);

        if (deps.debug_logging)
        {
            Output::send<LogLevel::Verbose>(STR("[KH3APProbeCpp] RuntimeMapJumpCommand status={} "
                                                "request_id={} map={} tag={} fade={}\n"),
                                            invoked ? STR("completed") : STR("failed"),
                                            request.request_id, request.map_name,
                                            request.target_tag, request.fade_kind);
        }
    }
} // namespace kh3ap
