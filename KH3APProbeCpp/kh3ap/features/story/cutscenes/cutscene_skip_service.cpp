#include <chrono>
#include <memory>
#include <optional>
#include <limits>
#include "kh3ap/features/story/cutscenes/cutscene_skip_service.h"
#include "kh3ap/features/story/cutscenes/cutscene_skip_view.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/player/characters/character_profile.h"

#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <sstream>
#include <utility>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr std::wstring_view blueprint_class_suffix = STR("_c");
        constexpr std::wstring_view blueprint_instance_suffix = STR("_c_");
        constexpr auto cutscene_world_code_chars = 2;
        constexpr auto cutscene_area_code_chars = 2;
        constexpr auto cutscene_world_token_chars =
            cutscene_world_code_chars + cutscene_area_code_chars;
        constexpr auto cutscene_token_log_chars = 180;

        constexpr auto gameflow_call_order = 20;
        constexpr auto gimmick_call_order = 10;
        constexpr auto rejected_cutscene_candidate_score = -1000;
        constexpr auto cutscene_keyword_score = 30;
        constexpr auto cutscene_ui_keyword_score = 12;
        constexpr auto cutscene_native_function_score = 14;
        constexpr auto cutscene_blueprint_function_score = 8;
        constexpr auto cutscene_no_parameter_score = 8;
        constexpr auto initial_cutscene_candidate_capacity = 128;
        constexpr int64_t manual_skip_pause_menu_type = 232;

        constexpr auto cutscene_handle_retention = std::chrono::seconds(30);
        constexpr auto cutscene_auto_skip_cooldown = std::chrono::seconds(2);
        constexpr auto cutscene_self_name_candidate_count = 32;
        constexpr auto cutscene_self_path_candidate_count = 16;
        constexpr auto minimum_cutscene_candidate_score = 40;
        constexpr auto cutscene_candidate_limit = 300;
        constexpr auto cutscene_diagnostic_object_limit = 80;
        constexpr auto cutscene_diagnostic_part_limit = 80;
    } // namespace

    using namespace RC;
    using namespace RC::Unreal;

    struct RuntimeCutsceneSkipService::Impl
    {
        struct CutsceneEndSkipTarget
        {
            UFunction* function_object{};
            UObject* self_object{};
            std::wstring self_name{};
            std::wstring owner_class_path{};
        };

        explicit Impl(RuntimeCutsceneSkipConfig value) : config_(std::move(value))
        {
        }

        auto resolve_bridge_dir() const -> std::optional<std::filesystem::path>
        {
            return config_.bridge_dir_resolver ? config_.bridge_dir_resolver() : std::nullopt;
        }

        auto runtime_context() const -> std::wstring
        {
            return config_.runtime_context_resolver ? config_.runtime_context_resolver()
                                                    : std::wstring{};
        }

        auto skip_enabled() const -> bool
        {
            return config_.skip_enabled_resolver && config_.skip_enabled_resolver();
        }

        static auto lower_ascii_copy(std::wstring_view value) -> std::wstring
        {
            std::wstring lowered{value};
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](wchar_t ch)
                           {
                               return ch >= L'A' && ch <= L'Z'
                                          ? static_cast<wchar_t>(ch + (L'a' - L'A'))
                                          : ch;
                           });
            return lowered;
        }

        RuntimeCutsceneSkipConfig config_;
        CutsceneSkipView view_;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        uint64_t scene_revision_{};
        std::wstring scene_identity_{};
#endif
        bool m_cutscene_skip_candidate_dumped{false};
        std::chrono::steady_clock::time_point m_last_cutscene_auto_skip_at{};
        bool m_cutscene_auto_skip_call_active{false};
        std::vector<std::pair<std::wstring, std::chrono::steady_clock::time_point>>
            m_recent_cutscene_end_skips{};

        static auto is_ascii_token_char(wchar_t ch) -> bool
        {
            return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
                   (ch >= L'0' && ch <= L'9') || ch == L'_';
        }

        static auto trim_cutscene_instance_suffix(std::wstring token) -> std::wstring
        {
            const std::wstring lowered = lower_ascii_copy(token);
            if (lowered.size() >= blueprint_class_suffix.size() && lowered.ends_with(STR("_c")))
            {
                token.resize(token.size() - blueprint_class_suffix.size());
                return token;
            }
            const auto suffix_pos = lowered.rfind(STR("_c_"));
            if (suffix_pos == std::wstring::npos ||
                suffix_pos + blueprint_instance_suffix.size() >= lowered.size())
            {
                return token;
            }
            for (size_t index = suffix_pos + blueprint_instance_suffix.size();
                 index < lowered.size(); ++index)
            {
                if (lowered[index] < L'0' || lowered[index] > L'9')
                {
                    return token;
                }
            }
            token.resize(suffix_pos);
            return token;
        }

        static auto cutscene_context_base_token(std::wstring_view context_name) -> std::wstring
        {
            const std::wstring lowered = lower_ascii_copy(context_name);
            const size_t start = lowered.find(STR("cutscene_"));
            if (start == std::wstring::npos)
            {
                return {};
            }

            size_t end = start;
            while (end < lowered.size() && is_ascii_token_char(lowered[end]))
            {
                ++end;
            }
            return trim_cutscene_instance_suffix(lowered.substr(start, end - start));
        }

        static auto cutscene_context_tail_token(std::wstring_view base_token) -> std::wstring
        {
            const size_t last_underscore = base_token.rfind(L'_');
            if (last_underscore == std::wstring::npos || last_underscore + 1 >= base_token.size())
            {
                return {};
            }
            return std::wstring{base_token.substr(last_underscore + 1)};
        }

        static auto cutscene_context_blueprint_class_path(std::wstring_view context_name)
            -> std::wstring
        {
            const std::wstring context{context_name};
            size_t path_start = context.find(STR(" /Game/"));
            if (path_start == std::wstring::npos)
            {
                path_start = context.find(STR("/Game/"));
            }
            else
            {
                ++path_start;
            }
            if (path_start == std::wstring::npos)
            {
                return {};
            }

            size_t path_end = context.find(STR(":PersistentLevel"), path_start);
            if (path_end == std::wstring::npos)
            {
                path_end = context.find(L' ', path_start);
            }
            if (path_end == std::wstring::npos)
            {
                path_end = context.size();
            }

            std::wstring class_path = context.substr(path_start, path_end - path_start);
            if (!class_path.ends_with(STR("_C")))
            {
                class_path += STR("_C");
            }
            return class_path;
        }

        static auto cutscene_context_end_skip_function_names(std::wstring_view base_token)
            -> std::vector<std::wstring>
        {
            std::vector<std::wstring> names{};
            const std::wstring base{base_token};
            const std::wstring tail = cutscene_context_tail_token(base_token);
            if (base.empty() || tail.empty())
            {
                return names;
            }

            names.push_back(base + STR("_REvt_") + tail + STR("_End_skip"));
            names.push_back(base + STR("_REvt_") + tail + STR("_End_Skip"));
            if (base == STR("ew_04_camera_ms210"))
            {
                names.push_back(STR("ew_04_gameflow_REvt_ms210_End_skip"));
                names.push_back(STR("ew_04_gameflow_REvt_ms210_End_Skip"));
                names.push_back(STR("ew_04_gameflow_REvt_ew_04_camera_ms210_End_skip"));
                names.push_back(STR("ew_04_gameflow_REvt_ew_04_camera_ms210_End_Skip"));
            }
            return names;
        }

        static auto cutscene_context_map_token(std::wstring_view base_token) -> std::wstring
        {
            const std::wstring base{base_token};
            const size_t cutscene_prefix = base.find(STR("cutscene_"));
            if (cutscene_prefix == std::wstring::npos)
            {
                return {};
            }
            const size_t world_start = cutscene_prefix + std::wstring_view{STR("cutscene_")}.size();
            if (world_start + cutscene_world_token_chars > base.size())
            {
                return {};
            }
            const std::wstring world = base.substr(world_start, cutscene_world_token_chars);
            if (world.size() != cutscene_world_token_chars || world[0] < L'a' || world[0] > L'z' ||
                world[1] < L'a' || world[1] > L'z' || world[2] < L'0' || world[2] > L'9' ||
                world[3] < L'0' || world[3] > L'9')
            {
                return {};
            }
            return world.substr(0, cutscene_world_code_chars) + STR("_") +
                   world.substr(cutscene_world_code_chars, cutscene_area_code_chars);
        }

        static auto cutscene_context_gameflow_self_names(std::wstring_view base_token)
            -> std::vector<std::wstring>
        {
            if (base_token == STR("ew_04_camera_ms210"))
            {
                std::vector<std::wstring> names{};
                for (int index = 0; index < cutscene_self_name_candidate_count; ++index)
                {
                    names.push_back(STR("ew_04_gameflow_C_") + std::to_wstring(index));
                }
                return names;
            }

            const std::wstring map_token = cutscene_context_map_token(base_token);
            if (map_token.empty())
            {
                return {};
            }

            std::vector<std::wstring> names{};
            for (int index = 0; index < cutscene_self_name_candidate_count; ++index)
            {
                names.push_back(map_token + STR("_gameflow_C_") + std::to_wstring(index));
            }
            return names;
        }

        static auto cutscene_context_gameflow_self_paths(std::wstring_view base_token)
            -> std::vector<std::wstring>
        {
            if (base_token == STR("ew_04_camera_ms210"))
            {
                std::vector<std::wstring> paths{};
                const std::wstring prefix =
                    STR("/Game/Levels/ew_DLC/ew_04/umap/"
                        "ew_04_gameflow.ew_04_gameflow:PersistentLevel.ew_04_gameflow_C_");
                for (int index = 0; index < cutscene_self_path_candidate_count; ++index)
                {
                    paths.push_back(prefix + std::to_wstring(index));
                }
                return paths;
            }

            const std::wstring map_token = cutscene_context_map_token(base_token);
            if (map_token.empty())
            {
                return {};
            }
            const std::wstring area = map_token.substr(0, cutscene_world_code_chars);
            std::wstring upper_object = map_token;
            std::transform(upper_object.begin(), upper_object.end(), upper_object.begin(),
                           [](wchar_t ch)
                           {
                               if (ch >= L'a' && ch <= L'z')
                               {
                                   return static_cast<wchar_t>(ch - (L'a' - L'A'));
                               }
                               return ch;
                           });

            const std::wstring lower_prefix =
                STR("/Game/Levels/") + area + STR("/") + map_token + STR("/") + map_token +
                STR(".") + map_token + STR(":PersistentLevel.") + map_token + STR("_gameflow_C_");
            const std::wstring upper_prefix = STR("/Game/Levels/") + area + STR("/") + map_token +
                                              STR("/") + map_token + STR(".") + upper_object +
                                              STR(":PersistentLevel.") + map_token +
                                              STR("_gameflow_C_");

            std::vector<std::wstring> paths{};
            for (int index = 0; index < cutscene_self_path_candidate_count; ++index)
            {
                paths.push_back(lower_prefix + std::to_wstring(index));
                paths.push_back(upper_prefix + std::to_wstring(index));
            }
            return paths;
        }

        static auto cutscene_context_level_owner_class_paths(std::wstring_view base_token)
            -> std::vector<std::wstring>
        {
            if (base_token == STR("ew_04_camera_ms210"))
            {
                return {std::wstring{
                    STR("/Game/Levels/ew_DLC/ew_04/umap/ew_04_gameflow.ew_04_gameflow_C")}};
            }

            const std::wstring map_token = cutscene_context_map_token(base_token);
            if (map_token.empty())
            {
                return {};
            }

            const std::wstring area = map_token.substr(0, cutscene_world_code_chars);
            std::vector<std::wstring> paths{};
            for (std::wstring_view owner_suffix : {STR("gimmick"), STR("gameflow")})
            {
                const std::wstring owner = map_token + STR("_") + std::wstring{owner_suffix};
                paths.push_back(STR("/Game/Levels/") + area + STR("/") + map_token + STR("/umap/") +
                                owner + STR(".") + owner + STR("_C"));
            }
            return paths;
        }

        static auto blueprint_class_short_name(std::wstring_view class_path) -> std::wstring
        {
            if (class_path.empty())
            {
                return {};
            }

            const size_t dot = class_path.find_last_of(L'.');
            const size_t slash = class_path.find_last_of(L'/');
            const size_t start =
                dot == std::wstring::npos ? (slash == std::wstring::npos ? 0 : slash + 1) : dot + 1;
            if (start >= class_path.size())
            {
                return {};
            }
            return std::wstring{class_path.substr(start)};
        }

        static auto find_cutscene_probe_class_self(std::wstring_view class_path) -> UObject*
        {
            std::wstring class_short = blueprint_class_short_name(class_path);
            if (class_short.empty())
            {
                return nullptr;
            }
            if (!class_short.ends_with(STR("_C")))
            {
                class_short += STR("_C");
            }

            std::wstring instance_path_prefix{class_path};
            if (instance_path_prefix.ends_with(STR("_C")))
            {
                instance_path_prefix.resize(instance_path_prefix.size() -
                                            blueprint_class_suffix.size());
            }
            instance_path_prefix += STR(":PersistentLevel.");
            instance_path_prefix += class_short;
            instance_path_prefix += STR("_");

            for (int index = 0; index < cutscene_self_name_candidate_count; ++index)
            {
                const std::wstring self_path = instance_path_prefix + std::to_wstring(index);
                UObject* const candidate =
                    UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, self_path);
                if (candidate && kh3ap::is_uobject_valid_fast(candidate))
                {
                    return candidate;
                }
            }

            for (int index = 0; index < cutscene_self_name_candidate_count; ++index)
            {
                const std::wstring self_name = class_short + STR("_") + std::to_wstring(index);
                UObject* const candidate = UObjectGlobals::FindObject(nullptr, self_name.c_str());
                if (candidate && kh3ap::is_uobject_valid_fast(candidate))
                {
                    return candidate;
                }
            }
            return nullptr;
        }

        static auto cutscene_owner_call_order(std::wstring_view owner_class_path) -> int
        {
            const std::wstring lowered = lower_ascii_copy(owner_class_path);
            if (lowered.find(STR("_gameflow")) != std::wstring::npos)
            {
                return gameflow_call_order;
            }
            if (lowered.find(STR("_gimmick")) != std::wstring::npos)
            {
                return gimmick_call_order;
            }
            return 0;
        }

        static auto cutscene_skip_candidate_score(UFunction* function_object,
                                                  std::wstring_view full_name) -> int
        {
            if (!function_object)
            {
                return rejected_cutscene_candidate_score;
            }

            const std::wstring lowered = lower_ascii_copy(full_name);
            if (contains_any_wide(lowered, {STR("/script/engine."), STR("gameplaystatics"),
                                            STR("kismetsystemlibrary"), STR("debug")}))
            {
                return rejected_cutscene_candidate_score;
            }

            const bool has_cutscene_surface = contains_any_wide(lowered, {
                                                                             STR("cutscene"),
                                                                             STR("cut_scene"),
                                                                             STR("movie"),
                                                                             STR("cinematic"),
                                                                             STR("sequence"),
                                                                             STR("seq"),
                                                                             STR("event"),
                                                                             STR("uip"),
                                                                             STR("hud"),
                                                                             STR("pause"),
                                                                         });
            const bool has_skip_action = contains_any_wide(lowered, {
                                                                        STR("skip"),
                                                                        STR("cancel"),
                                                                        STR("decide"),
                                                                        STR("start"),
                                                                        STR("close"),
                                                                        STR("finish"),
                                                                        STR("end"),
                                                                    });
            if (!has_cutscene_surface || !has_skip_action)
            {
                return rejected_cutscene_candidate_score;
            }

            int score = 0;
            for (std::wstring_view needle :
                 {STR("skip"), STR("cutscene"), STR("movie"), STR("cinematic"), STR("sequence")})
            {
                if (lowered.find(needle) != std::wstring::npos)
                {
                    score += cutscene_keyword_score;
                }
            }
            for (std::wstring_view needle :
                 {STR("uip"), STR("hud"), STR("pause"), STR("menu"), STR("event")})
            {
                if (lowered.find(needle) != std::wstring::npos)
                {
                    score += cutscene_ui_keyword_score;
                }
            }
            if (lowered.find(STR("/script/tresgame.")) != std::wstring::npos)
            {
                score += cutscene_native_function_score;
            }
            if (lowered.find(STR("/game/")) != std::wstring::npos)
            {
                score += cutscene_blueprint_function_score;
            }
            if (function_param_signature(function_object).empty())
            {
                score += cutscene_no_parameter_score;
            }
            return score;
        }

        auto serialize_cutscene_skip_candidate(int32_t object_index, int score,
                                               UFunction* function_object,
                                               std::wstring_view full_name) -> std::string
        {
            std::ostringstream json;
            json << "{\"schema\":\"kh3ap.cutscene_skip_candidate.v1\","
                 << "\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\","
                 << "\"object_index\":" << object_index << ","
                 << "\"score\":" << score << ","
                 << "\"function_path\":\"" << json_escape(full_name) << "\","
                 << "\"function_name\":\""
                 << json_escape(function_object ? function_object->GetName() : STR("")) << "\","
                 << "\"params_signature\":\""
                 << json_escape(function_param_signature(function_object)) << "\"}\n";
            return json.str();
        }

        auto dump_cutscene_skip_candidate_catalog_once() -> void
        {
            if (!config_.candidate_catalog_enabled)
            {
                return;
            }
            if (m_cutscene_skip_candidate_dumped || !skip_enabled())
            {
                return;
            }
            m_cutscene_skip_candidate_dumped = true;

            const auto bridge_dir = resolve_bridge_dir();
            if (!bridge_dir.has_value())
            {
                return;
            }

            struct Candidate
            {
                int32_t object_index{};
                int score{};
                UFunction* function_object{};
                std::wstring full_name{};
            };

            std::vector<Candidate> candidates{};
            candidates.reserve(initial_cutscene_candidate_capacity);
            uint64_t scanned_count = 0;
            UObjectGlobals::ForEachUObject(
                [&](UObject* object, int32_t object_index, int32_t /*chunk_index*/)
                {
                    if (!object || !kh3ap::is_uobject_valid_fast(object))
                    {
                        return RC::LoopAction::Continue;
                    }
                    UClass* const object_class = object->GetClassPrivate();
                    if (!object_class || object_class->GetName() != STR("Function"))
                    {
                        return RC::LoopAction::Continue;
                    }

                    ++scanned_count;
                    auto* const function_object = static_cast<UFunction*>(object);
                    const std::wstring full_name = function_object->GetFullName();
                    const int score = cutscene_skip_candidate_score(function_object, full_name);
                    if (score < minimum_cutscene_candidate_score)
                    {
                        return RC::LoopAction::Continue;
                    }

                    candidates.push_back(Candidate{
                        .object_index = object_index,
                        .score = score,
                        .function_object = function_object,
                        .full_name = full_name,
                    });
                    return RC::LoopAction::Continue;
                });

            std::sort(candidates.begin(), candidates.end(),
                      [](const Candidate& left, const Candidate& right)
                      {
                          return left.score > right.score;
                      });
            if (candidates.size() > cutscene_candidate_limit)
            {
                candidates.resize(cutscene_candidate_limit);
            }

            const auto output_path = *bridge_dir / "cutscene_skip_candidate_functions.jsonl";
            kh3ap::write_text_file(output_path, "");
            for (const Candidate& candidate : candidates)
            {
                append_text_file(output_path, serialize_cutscene_skip_candidate(
                                                  candidate.object_index, candidate.score,
                                                  candidate.function_object, candidate.full_name));
            }

            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] CutsceneSkip candidate catalog "
                                                "wrote entries={} scanned={} file={}\n"),
                                            static_cast<uint64_t>(candidates.size()), scanned_count,
                                            output_path.wstring());
        }

        auto append_cutscene_auto_skip_log(std::wstring_view action, int64_t pause_menu_type,
                                           std::wstring_view context_name, std::wstring_view result)
            -> void
        {
            const auto bridge_dir = resolve_bridge_dir();
            if (!bridge_dir.has_value())
            {
                return;
            }

            std::ostringstream json;
            json << "{\"schema\":\"kh3ap.cutscene_auto_skip.v1\","
                 << "\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\","
                 << "\"action\":\"" << json_escape(action) << "\","
                 << "\"pause_menu_type\":" << pause_menu_type << ","
                 << "\"context\":\"" << json_escape(context_name) << "\","
                 << "\"result\":\"" << json_escape(result) << "\"}\n";
            append_text_file(*bridge_dir / "cutscene_auto_skip_events.jsonl", json.str());
        }

        auto append_cutscene_ui_probe_log(std::wstring_view action, UFunction* function_object,
                                          void* params, std::wstring_view context_name,
                                          std::wstring_view frame_name, HookPhase phase) -> void
        {
            if (!config_.ui_probe_enabled)
            {
                return;
            }

            const auto bridge_dir = resolve_bridge_dir();
            if (!bridge_dir.has_value() || !function_object || !params)
            {
                return;
            }
            if (!skip_enabled())
            {
                return;
            }

            std::ostringstream json;
            json << "{\"schema\":\"kh3ap.cutscene_ui_probe.v1\","
                 << "\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\","
                 << "\"action\":\"" << json_escape(action) << "\","
                 << "\"phase\":\"" << (phase == HookPhase::Pre ? "pre" : "post") << "\","
                 << "\"function\":\"" << json_escape(function_object->GetFullName()) << "\","
                 << "\"context\":\"" << json_escape(context_name) << "\","
                 << "\"frame\":\"" << json_escape(frame_name) << "\","
                 << "\"params\":\""
                 << json_escape(summarize_function_params_compact(function_object, params))
                 << "\"}\n";
            append_text_file(*bridge_dir / "cutscene_ui_probe_events.jsonl", json.str());
        }

        auto append_cutscene_end_skip_candidate_log(std::wstring_view action,
                                                    std::wstring_view context_name,
                                                    std::wstring_view base_token,
                                                    UFunction* function_object,
                                                    std::wstring_view result) -> void
        {
            if (!config_.end_skip_probe_logging_enabled)
            {
                return;
            }

            const auto bridge_dir = resolve_bridge_dir();
            if (!bridge_dir.has_value())
            {
                return;
            }

            std::ostringstream json;
            json << "{\"schema\":\"kh3ap.cutscene_end_skip_probe.v1\","
                 << "\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\","
                 << "\"action\":\"" << json_escape(action) << "\","
                 << "\"context\":\"" << json_escape(context_name) << "\","
                 << "\"base_token\":\"" << json_escape(base_token) << "\","
                 << "\"function\":\""
                 << json_escape(function_object ? function_object->GetFullName() : STR("")) << "\","
                 << "\"function_name\":\""
                 << json_escape(function_object ? function_object->GetName() : STR("")) << "\","
                 << "\"params_signature\":\""
                 << json_escape(function_param_signature(function_object)) << "\","
                 << "\"result\":\"" << json_escape(result) << "\"}\n";
            append_text_file(*bridge_dir / "cutscene_end_skip_probe_events.jsonl", json.str());
        }

        auto scan_and_log_special_cutscene_functions(std::wstring_view context_name,
                                                     std::wstring_view base_token) -> void
        {
            if (base_token != STR("ew_04_camera_ms210"))
            {
                return;
            }

            int logged_count = 0;
            UObjectGlobals::ForEachUObject(
                [&](UObject* object, int32_t /*object_index*/, int32_t /*chunk_index*/)
                {
                    if (logged_count >= cutscene_diagnostic_object_limit || !object ||
                        !kh3ap::is_uobject_valid_fast(object))
                    {
                        return logged_count >= cutscene_diagnostic_object_limit
                                   ? RC::LoopAction::Break
                                   : RC::LoopAction::Continue;
                    }
                    UClass* const object_class = object->GetClassPrivate();
                    if (!object_class || object_class->GetName() != STR("Function"))
                    {
                        return RC::LoopAction::Continue;
                    }

                    auto* const function_object = static_cast<UFunction*>(object);
                    const std::wstring full_name = function_object->GetFullName();
                    const std::wstring lowered = lower_ascii_copy(full_name);
                    const bool relevant_scene =
                        lowered.find(STR("ms210")) != std::wstring::npos ||
                        lowered.find(STR("ew_04")) != std::wstring::npos ||
                        lowered.find(STR("gamehelp")) != std::wstring::npos ||
                        lowered.find(STR("game_help")) != std::wstring::npos;
                    const bool relevant_action = contains_any_wide(
                        lowered, {STR("skip"), STR("end"), STR("finish"), STR("revt"), STR("event"),
                                  STR("movie"), STR("camera"), STR("help")});
                    if (!relevant_scene || !relevant_action)
                    {
                        return RC::LoopAction::Continue;
                    }

                    ++logged_count;
                    append_cutscene_end_skip_candidate_log(
                        STR("special_function_scan"), context_name, base_token, function_object,
                        std::format(STR("candidate_index={}"), logged_count));
                    return RC::LoopAction::Continue;
                });
            append_cutscene_end_skip_candidate_log(
                STR("special_function_scan"), context_name, base_token, nullptr,
                std::format(STR("logged_count={}"), logged_count));
        }

        static auto cutscene_probe_property_interesting(std::wstring_view property_name,
                                                        std::wstring_view property_class) -> bool
        {
            const std::wstring lowered_name = lower_ascii_copy(property_name);
            const std::wstring lowered_class = lower_ascii_copy(property_class);
            return contains_any_wide(lowered_name,
                                     {
                                         STR("cutscene"),
                                         STR("cut_scene"),
                                         STR("skip"),
                                         STR("event"),
                                         STR("flag"),
                                         STR("gameflow"),
                                         STR("game_flow"),
                                         STR("sequence"),
                                         STR("movie"),
                                         STR("cinematic"),
                                         STR("scenario"),
                                         STR("story"),
                                         STR("state"),
                                         STR("phase"),
                                         STR("current"),
                                         STR("complete"),
                                         STR("finish"),
                                         STR("end"),
                                     }) ||
                   contains_any_wide(lowered_class, {STR("array"), STR("map")});
        }

        static auto summarize_cutscene_probe_object_properties(UObject* object) -> std::wstring
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                return STR("<null>");
            }
            UClass* const object_class = object->GetClassPrivate();
            if (!object_class)
            {
                return STR("<no_class>");
            }

            std::vector<std::wstring> parts{};
            int scanned_count = 0;
            int kept_count = 0;
            for (FProperty* property : object_class->ForEachPropertyInChain())
            {
                if (!property)
                {
                    continue;
                }
                ++scanned_count;

                const std::wstring property_name = property->GetName();
                const std::wstring property_class = property->GetClass().GetName();
                const bool scalarish =
                    property_class == STR("NameProperty") || property_class == STR("StrProperty") ||
                    property_class == STR("ObjectProperty") ||
                    property_class == STR("BoolProperty") ||
                    property_class == STR("ByteProperty") ||
                    property_class == STR("EnumProperty") || property_class == STR("IntProperty") ||
                    property_class == STR("UInt32Property") ||
                    property_class == STR("Int64Property") ||
                    property_class == STR("UInt64Property");
                const bool arrayish = property_class == STR("ArrayProperty") ||
                                      property_class == STR("MapProperty") ||
                                      property_class == STR("SetProperty");
                if (!scalarish && !arrayish)
                {
                    continue;
                }
                if (!cutscene_probe_property_interesting(property_name, property_class) &&
                    property_class != STR("BoolProperty"))
                {
                    continue;
                }

                std::wstring value{};
                if (property_class == STR("ObjectProperty"))
                {
                    value = format_object_debug_name(
                        try_read_object_property_object(object, property_name.c_str()));
                }
                else if (scalarish)
                {
                    const auto scalar =
                        try_read_object_scalar_property_as_string(object, property_name.c_str());
                    if (!scalar.has_value())
                    {
                        continue;
                    }
                    value = *scalar;
                }
                else
                {
                    void* const value_ptr =
                        object->GetValuePtrByPropertyNameInChain(property_name.c_str());
                    if (!value_ptr)
                    {
                        continue;
                    }
                    ArrayHeader header{};
                    std::memcpy(&header, value_ptr, sizeof(ArrayHeader));
                    value = std::format(STR("num={} max={} data=0x{:X}"), header.num, header.max,
                                        header.data_ptr);
                }

                parts.push_back(std::format(STR("{}:{}@{}={}"), property_name, property_class,
                                            property->GetOffset_Internal(),
                                            truncate_for_log(value, cutscene_token_log_chars)));
                ++kept_count;
                if (parts.size() >= cutscene_diagnostic_part_limit)
                {
                    break;
                }
            }

            parts.insert(parts.begin(),
                         std::format(STR("scanned={} kept={}"), scanned_count, kept_count));
            return join_wide_parts(parts, STR(" ; "));
        }

        static auto find_cutscene_probe_gameflow_self(std::wstring_view base_token) -> UObject*
        {
            if (base_token.empty())
            {
                return nullptr;
            }
            for (const std::wstring& self_name : cutscene_context_gameflow_self_names(base_token))
            {
                UObject* const candidate = UObjectGlobals::FindObject(nullptr, self_name.c_str());
                if (candidate && kh3ap::is_uobject_valid_fast(candidate))
                {
                    return candidate;
                }
            }
            for (const std::wstring& self_path : cutscene_context_gameflow_self_paths(base_token))
            {
                UObject* const candidate =
                    UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, self_path);
                if (candidate && kh3ap::is_uobject_valid_fast(candidate))
                {
                    return candidate;
                }
            }
            return nullptr;
        }

        auto append_cutscene_state_probe_object_log(
            std::wstring_view action, int64_t pause_menu_type, std::wstring_view context_name,
            std::wstring_view base_token, std::wstring_view target_label, UObject* object) -> void
        {
            if (!config_.state_probe_logging_enabled)
            {
                return;
            }

            const auto bridge_dir = resolve_bridge_dir();
            if (!bridge_dir.has_value())
            {
                return;
            }

            std::ostringstream json;
            json << "{\"schema\":\"kh3ap.cutscene_state_probe.v1\","
                 << "\"build_stamp\":\"" << json_escape(config_.build_stamp) << "\","
                 << "\"action\":\"" << json_escape(action) << "\","
                 << "\"pause_menu_type\":" << pause_menu_type << ","
                 << "\"context\":\"" << json_escape(context_name) << "\","
                 << "\"base_token\":\"" << json_escape(base_token) << "\","
                 << "\"target\":\"" << json_escape(target_label) << "\","
                 << "\"object\":\"" << json_escape(format_object_debug_name(object)) << "\","
                 << "\"class_chain\":\"" << json_escape(summarize_object_class_chain(object))
                 << "\","
                 << "\"properties\":\""
                 << json_escape(summarize_cutscene_probe_object_properties(object)) << "\"}\n";
            append_text_file(*bridge_dir / "cutscene_state_probe_events.jsonl", json.str());
        }

        auto append_cutscene_state_probe_log(std::wstring_view action, int64_t pause_menu_type,
                                             UObject* context, std::wstring_view context_name)
            -> void
        {
            if (!skip_enabled())
            {
                return;
            }

            const std::wstring base_token = cutscene_context_base_token(context_name);
            append_cutscene_state_probe_object_log(action, pause_menu_type, context_name,
                                                   base_token, STR("context"), context);

            UObject* const gameflow_self = find_cutscene_probe_gameflow_self(base_token);
            if (gameflow_self && gameflow_self != context)
            {
                append_cutscene_state_probe_object_log(action, pause_menu_type, context_name,
                                                       base_token, STR("gameflow_self"),
                                                       gameflow_self);
            }
        }

        auto find_context_cutscene_end_skip_targets(UObject* context,
                                                    std::wstring_view context_name)
            -> std::vector<CutsceneEndSkipTarget>
        {
            std::vector<CutsceneEndSkipTarget> targets{};
            const std::wstring base_token = cutscene_context_base_token(context_name);
            if (base_token.empty())
            {
                append_cutscene_end_skip_candidate_log(STR("find_end_skip"), context_name,
                                                       base_token, nullptr,
                                                       STR("missing_base_token"));
                return targets;
            }

            const std::wstring class_path = cutscene_context_blueprint_class_path(context_name);
            if (class_path.empty())
            {
                append_cutscene_end_skip_candidate_log(STR("find_end_skip"), context_name,
                                                       base_token, nullptr,
                                                       STR("missing_class_path"));
                return targets;
            }

            const std::vector<std::wstring> function_names =
                cutscene_context_end_skip_function_names(base_token);
            for (const std::wstring& function_name : function_names)
            {
                const std::wstring function_path = class_path + STR(":") + function_name;
                auto* const function_object =
                    UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, function_path);
                if (function_object)
                {
                    append_cutscene_end_skip_candidate_log(
                        STR("direct_find_end_skip"), context_name, base_token, function_object,
                        std::wstring{STR("found:")} + function_path);
                    targets.push_back(CutsceneEndSkipTarget{
                        function_object, context,
                        context_name.empty() ? std::wstring{} : std::wstring{context_name},
                        class_path});
                    continue;
                }

                append_cutscene_end_skip_candidate_log(
                    STR("direct_find_end_skip"), context_name, base_token, nullptr,
                    std::wstring{STR("missing:")} + function_path);
            }

            for (const std::wstring& owner_class_path :
                 cutscene_context_level_owner_class_paths(base_token))
            {
                for (const std::wstring& function_name : function_names)
                {
                    const std::wstring function_path = owner_class_path + STR(":") + function_name;
                    auto* const function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                        nullptr, nullptr, function_path);
                    if (!function_object)
                    {
                        append_cutscene_end_skip_candidate_log(
                            STR("direct_owner_end_skip"), context_name, base_token, nullptr,
                            std::wstring{STR("missing:")} + function_path);
                        continue;
                    }

                    const bool duplicate =
                        std::any_of(targets.begin(), targets.end(),
                                    [&](const CutsceneEndSkipTarget& target)
                                    {
                                        return target.function_object == function_object;
                                    });
                    if (duplicate)
                    {
                        continue;
                    }

                    UObject* const owner_self = find_cutscene_probe_class_self(owner_class_path);
                    if (!owner_self)
                    {
                        append_cutscene_end_skip_candidate_log(
                            STR("direct_owner_end_skip"), context_name, base_token, function_object,
                            std::wstring{STR("found_but_missing_owner_self:")} + function_path);
                        continue;
                    }

                    const std::wstring self_name = owner_self->GetFullName();
                    append_cutscene_end_skip_candidate_log(
                        STR("direct_owner_end_skip"), context_name, base_token, function_object,
                        std::wstring{STR("found:")} + function_path + STR(" self=") + self_name);
                    targets.push_back(CutsceneEndSkipTarget{function_object, owner_self, self_name,
                                                            owner_class_path});
                }
            }

            std::sort(targets.begin(), targets.end(),
                      [](const CutsceneEndSkipTarget& left, const CutsceneEndSkipTarget& right)
                      {
                          const int left_order = cutscene_owner_call_order(left.owner_class_path);
                          const int right_order = cutscene_owner_call_order(right.owner_class_path);
                          if (left_order != right_order)
                          {
                              return left_order < right_order;
                          }
                          return left.owner_class_path < right.owner_class_path;
                      });

            if (targets.empty())
            {
                scan_and_log_special_cutscene_functions(context_name, base_token);
            }

            append_cutscene_end_skip_candidate_log(
                STR("find_end_skip"), context_name, base_token, nullptr,
                targets.empty() ? STR("missing_cutscene_end_skip_targets")
                                : std::format(STR("target_count={}"), targets.size()));
            return targets;
        }

        auto try_call_context_cutscene_end_skip(UObject* context, std::wstring_view context_name)
            -> bool
        {
            if (!context || !kh3ap::is_uobject_valid_fast(context))
            {
                append_cutscene_end_skip_candidate_log(STR("call_end_skip"), context_name,
                                                       cutscene_context_base_token(context_name),
                                                       nullptr, STR("missing_context"));
                return false;
            }

            const std::vector<CutsceneEndSkipTarget> end_skip_targets =
                find_context_cutscene_end_skip_targets(context, context_name);
            if (end_skip_targets.empty())
            {
                return false;
            }

            bool called_any = false;
            for (const CutsceneEndSkipTarget& end_skip_target : end_skip_targets)
            {
                if (!end_skip_target.function_object)
                {
                    continue;
                }
                UObject* const self_object =
                    end_skip_target.self_object ? end_skip_target.self_object : context;
                std::vector<std::byte> params(
                    static_cast<size_t>(end_skip_target.function_object->GetParmsSize()));
                std::fill(params.begin(), params.end(), std::byte{0});
                m_cutscene_auto_skip_call_active = true;
                try
                {
                    self_object->ProcessEvent(end_skip_target.function_object,
                                              params.empty() ? nullptr : params.data());
                }
                catch (...)
                {
                    m_cutscene_auto_skip_call_active = false;
                    append_cutscene_end_skip_candidate_log(
                        STR("call_end_skip"), context_name,
                        cutscene_context_base_token(context_name), end_skip_target.function_object,
                        std::wstring{STR("exception self=")} + end_skip_target.self_name);
                    continue;
                }
                m_cutscene_auto_skip_call_active = false;
                append_cutscene_end_skip_candidate_log(
                    STR("call_end_skip"), context_name, cutscene_context_base_token(context_name),
                    end_skip_target.function_object,
                    std::wstring{STR("called self=")} + end_skip_target.self_name);
                called_any = true;
            }
            m_cutscene_auto_skip_call_active = false;
            return called_any;
        }

        auto recently_skipped_cutscene_base_token(std::wstring_view base_token,
                                                  std::chrono::steady_clock::time_point now) -> bool
        {
            m_recent_cutscene_end_skips.erase(std::remove_if(m_recent_cutscene_end_skips.begin(),
                                                             m_recent_cutscene_end_skips.end(),
                                                             [&](const auto& entry)
                                                             {
                                                                 return (now - entry.second) >
                                                                        cutscene_handle_retention;
                                                             }),
                                              m_recent_cutscene_end_skips.end());

            return std::any_of(m_recent_cutscene_end_skips.begin(),
                               m_recent_cutscene_end_skips.end(),
                               [&](const auto& entry)
                               {
                                   return entry.first == base_token;
                               });
        }

        auto remember_skipped_cutscene_base_token(std::wstring_view base_token,
                                                  std::chrono::steady_clock::time_point now) -> void
        {
            if (base_token.empty())
            {
                return;
            }
            m_recent_cutscene_end_skips.emplace_back(std::wstring{base_token}, now);
        }

        static auto cutscene_auto_skip_blocked_base_token(std::wstring_view base_token) -> bool
        {
            // Toy Box entry cutscenes are worse than manual skipping with the
            // current End_skip route: ts251 skips too late, and ts252 reaches a
            // mixed combat/cutscene state. Twilight Town is disabled while its
            // normal cutscene path is still being validated. Camera/gameflow
            // sequences are parked until we identify a reliable skip path. The
            // later Olympus he103-he108 and he111-he115 handlers were found, but
            // are disabled for now while testing validates their surrounding
            // sequence.
            return base_token.starts_with(STR("cutscene_tt")) ||
                   base_token == STR("cutscene_ts01_ts251") ||
                   base_token == STR("cutscene_ts01_ts252") ||
                   base_token == STR("cutscene_he02_he103") ||
                   base_token == STR("cutscene_he03_he104") ||
                   base_token == STR("cutscene_he03_he105") ||
                   base_token == STR("cutscene_he03_he106") ||
                   base_token == STR("cutscene_he03_he107") ||
                   base_token == STR("cutscene_he03_he108") ||
                   base_token == STR("cutscene_he03_he111") ||
                   base_token == STR("cutscene_he03_he112") ||
                   base_token == STR("cutscene_he02_he113") ||
                   base_token == STR("cutscene_he02_he114") ||
                   base_token == STR("cutscene_he01_he115") ||
                   base_token == STR("he_03_camera_ms020") ||
                   base_token == STR("ew_04_camera_ms210") ||
                   base_token == STR("ew_01_camera_ms070");
        }

        static auto cutscene_auto_skip_experimental_scope_base_token(std::wstring_view base_token)
            -> bool
        {
            // Keep the experimental normal-cutscene skip route constrained to
            // Olympus plus the first Frozen cutscene that validated the path.
            return base_token.starts_with(STR("cutscene_he")) ||
                   base_token == STR("cutscene_fz05_fz451");
        }

        auto maybe_auto_skip_cutscene_from_pause_menu_type(UFunction* target_function, void* params,
                                                           UObject* context,
                                                           std::wstring_view context_name,
                                                           HookPhase phase, int64_t pause_menu_type)
            -> void
        {
            if (phase != HookPhase::Post || !target_function || !params ||
                m_cutscene_auto_skip_call_active)
            {
                return;
            }

            const std::wstring lowered_context = lower_ascii_copy(context_name);
            const std::wstring base_token = cutscene_context_base_token(context_name);
            const bool cutscene_context =
                lowered_context.find(STR("cutscene_")) != std::wstring::npos ||
                lowered_context.find(STR("/cutscene")) != std::wstring::npos;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            // Toy Box sequences need not appear in m_MatineeActorsForMovie. Observe
            // the owning scene's real lifecycle, including its manual Skip marker,
            // even when the seed's automatic cutscene skipping is disabled.
            if (cutscene_context)
            {
                scene_identity_ =
                    std::wstring{context_name} + L"#" + std::to_wstring(++scene_revision_);
            }
#endif
            if (!cutscene_context && pause_menu_type != manual_skip_pause_menu_type)
            {
                return;
            }

            const bool skippable_cutscene_pause_menu_type =
                cutscene_context && pause_menu_type > 0 &&
                pause_menu_type != manual_skip_pause_menu_type;
            if (!skip_enabled())
            {
                return;
            }
            append_cutscene_auto_skip_log(STR("pause_menu_type"), pause_menu_type, context_name,
                                          STR("observed"));
            if (pause_menu_type == manual_skip_pause_menu_type)
            {
                append_cutscene_state_probe_log(STR("manual_skip_marker"), pause_menu_type, context,
                                                context_name);
            }
            if (!skippable_cutscene_pause_menu_type)
            {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            append_cutscene_state_probe_log(STR("cutscene_pause_marker"), pause_menu_type, context,
                                            context_name);
            if (cutscene_auto_skip_blocked_base_token(base_token))
            {
                append_cutscene_auto_skip_log(STR("skip_request"), pause_menu_type, context_name,
                                              STR("blocked_problematic_cutscene"));
                return;
            }
            if (!cutscene_auto_skip_experimental_scope_base_token(base_token))
            {
                append_cutscene_auto_skip_log(STR("skip_request"), pause_menu_type, context_name,
                                              STR("outside_experimental_scope"));
                return;
            }
            if (m_last_cutscene_auto_skip_at.time_since_epoch().count() != 0 &&
                (now - m_last_cutscene_auto_skip_at) < cutscene_auto_skip_cooldown)
            {
                append_cutscene_auto_skip_log(STR("pause_menu_type"), pause_menu_type, context_name,
                                              STR("debounced"));
                return;
            }
            m_last_cutscene_auto_skip_at = now;

            if (recently_skipped_cutscene_base_token(base_token, now))
            {
                append_cutscene_auto_skip_log(STR("skip_request"), pause_menu_type, context_name,
                                              STR("duplicate_base_token_debounced"));
                return;
            }
            const bool called_end_skip = try_call_context_cutscene_end_skip(context, context_name);
            if (called_end_skip)
            {
                remember_skipped_cutscene_base_token(base_token, now);
            }
            append_cutscene_auto_skip_log(STR("skip_request"), pause_menu_type, context_name,
                                          called_end_skip ? STR("called_short_end_skip")
                                                          : STR("short_end_skip_not_found"));
        }
    };

    RuntimeCutsceneSkipService::RuntimeCutsceneSkipService(RuntimeCutsceneSkipConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    RuntimeCutsceneSkipService::~RuntimeCutsceneSkipService() = default;
    RuntimeCutsceneSkipService::RuntimeCutsceneSkipService(RuntimeCutsceneSkipService&&) noexcept =
        default;
    auto RuntimeCutsceneSkipService::operator=(RuntimeCutsceneSkipService&&) noexcept
        -> RuntimeCutsceneSkipService& = default;

    auto RuntimeCutsceneSkipService::dump_candidate_catalog_once() -> void
    {
        impl_->dump_cutscene_skip_candidate_catalog_once();
    }

    auto RuntimeCutsceneSkipService::scene_identity() const -> std::wstring
    {
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        return impl_->scene_identity_;
#else
        return {};
#endif
    }

    auto RuntimeCutsceneSkipService::on_event_skip_enter(std::wstring_view context_name) -> void
    {
        impl_->append_cutscene_auto_skip_log(impl_->m_cutscene_auto_skip_call_active
                                                 ? STR("on_event_skip_internal")
                                                 : STR("on_event_skip_external"),
                                             1, context_name, STR("entered"));
    }

    auto RuntimeCutsceneSkipService::on_pause_menu_type(UFunction* function_object, void* params,
                                                        UObject* context,
                                                        std::wstring_view context_name,
                                                        HookPhase phase) -> std::optional<int64_t>
    {
        if (phase != HookPhase::Post || !is_uobject_valid_fast(context) ||
            character_native::build() == character_native::Build::Unsupported)
        {
            return std::nullopt;
        }
        auto* world = kh02_actor_world(context);
        auto* instance =
            is_uobject_valid_fast(world)
                ? try_read_reflected_object_property_value(world, L"OwningGameInstance")
                : nullptr;
        if (world != kh02_current_world() || !is_uobject_valid_fast(instance) ||
            instance->GetClassPrivate()->GetName() != L"TresGameInstance")
        {
            return std::nullopt;
        }
        // Native exec hooks expose the caller's Blueprint locals, not the
        // evaluated Type argument. Read the state just committed by the setter.
        // Both supported retail layouts use this unreflected field; the native
        // UI inspector reads the same authoritative pause-menu value.
        constexpr size_t pause_menu_type_offset = 0x8d0;
        int32_t pause_menu_type = -1;
        std::memcpy(&pause_menu_type,
                    reinterpret_cast<const std::byte*>(instance) + pause_menu_type_offset,
                    sizeof(pause_menu_type));
        if (pause_menu_type < 0 || pause_menu_type > std::numeric_limits<uint8_t>::max())
        {
            return std::nullopt;
        }
        if (pause_menu_type == 0 || pause_menu_type == manual_skip_pause_menu_type)
        {
            impl_->view_.arm(context);
        }
        impl_->maybe_auto_skip_cutscene_from_pause_menu_type(function_object, params, context,
                                                             context_name, phase, pause_menu_type);
        return pause_menu_type;
    }

    void RuntimeCutsceneSkipService::tick()
    {
        impl_->view_.tick();
    }

    auto RuntimeCutsceneSkipService::on_ui_probe(UFunction* function_object, void* params,
                                                 std::wstring_view context_name,
                                                 std::wstring_view frame_name, HookPhase phase)
        -> void
    {
        impl_->append_cutscene_ui_probe_log(STR("uiparts_callback"), function_object, params,
                                            context_name, frame_name, phase);
    }
} // namespace kh3ap
