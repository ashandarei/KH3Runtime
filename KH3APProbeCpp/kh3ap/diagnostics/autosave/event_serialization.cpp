#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include <Unreal/Hooks.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/FProperty.hpp>
#include "kh3ap/bridge/utilities.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <UnrealDef.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace autosave_detail;
    namespace autosave_detail
    {
        auto phase_name(HookPhase phase) -> std::wstring_view
        {
            return phase == HookPhase::Pre ? STR("pre") : STR("post");
        }

        auto json_field(std::ostringstream& stream, std::string_view name, std::wstring_view value)
            -> void
        {
            stream << ",\"" << name << "\":\"" << json_escape(value) << "\"";
        }

        auto json_bool_field(std::ostringstream& stream, std::string_view name, bool value) -> void
        {
            stream << ",\"" << name << "\":" << (value ? "true" : "false");
        }
    } // namespace autosave_detail
    auto Kh3AutosaveProbe::write_counter_snapshot(std::wstring_view reason) -> void
    {
        const uint64_t pre_hits = save_game_to_slot_pre_hits_.load(std::memory_order_relaxed);
        const uint64_t post_hits = save_game_to_slot_post_hits_.load(std::memory_order_relaxed);
        const uint64_t randomizer_pre_hits =
            save_game_to_slot_randomizer_pre_hits_.load(std::memory_order_relaxed);
        const uint64_t randomizer_post_hits =
            save_game_to_slot_randomizer_post_hits_.load(std::memory_order_relaxed);
        const uint64_t non_randomizer_pre_hits =
            save_game_to_slot_non_randomizer_pre_hits_.load(std::memory_order_relaxed);
        const uint64_t non_randomizer_post_hits =
            save_game_to_slot_non_randomizer_post_hits_.load(std::memory_order_relaxed);
        save_game_to_slot_pre_hits_flushed_ = pre_hits;
        save_game_to_slot_post_hits_flushed_ = post_hits;
        save_game_to_slot_randomizer_pre_hits_flushed_ = randomizer_pre_hits;
        save_game_to_slot_randomizer_post_hits_flushed_ = randomizer_post_hits;
        save_game_to_slot_non_randomizer_pre_hits_flushed_ = non_randomizer_pre_hits;
        save_game_to_slot_non_randomizer_post_hits_flushed_ = non_randomizer_post_hits;

        std::ostringstream stream;
        stream << "{\"type\":\"autosave_probe_counter\"";
        stream << ",\"observed_at\":\"" << now_iso8601_utc() << "\"";
        json_field(stream, "build_stamp", config_.build_stamp);
        json_field(stream, "function_name", save_game_to_slot_name);
        json_field(stream, "reason", reason);
        stream << ",\"pre_hits\":" << pre_hits;
        stream << ",\"post_hits\":" << post_hits;
        stream << ",\"randomizer_pre_hits\":" << randomizer_pre_hits;
        stream << ",\"randomizer_post_hits\":" << randomizer_post_hits;
        stream << ",\"non_randomizer_pre_hits\":" << non_randomizer_pre_hits;
        stream << ",\"non_randomizer_post_hits\":" << non_randomizer_post_hits;
        stream << ",\"non_randomizer_callers\":[";
        for (size_t index = 0; index < non_randomizer_caller_count_; ++index)
        {
            if (index > 0)
            {
                stream << ",";
            }
            stream << "{\"caller\":\"" << json_escape(non_randomizer_caller_names_[index]) << "\"";
            stream << ",\"pre_hits\":" << non_randomizer_caller_pre_hits_[index] << "}";
        }
        stream << "]";
        stream << ",\"non_randomizer_slots\":[";
        for (size_t index = 0; index < non_randomizer_slot_bucket_count_; ++index)
        {
            const NonRandomizerSlotBucket& bucket = non_randomizer_slot_buckets_[index];
            if (index > 0)
            {
                stream << ",";
            }
            stream << "{\"caller\":\"" << json_escape(bucket.caller_name) << "\"";
            stream << ",\"slot_name\":\"" << json_escape(bucket.slot_name) << "\"";
            stream << ",\"slot_offset\":" << bucket.slot_offset;
            if (bucket.user_index.has_value())
            {
                stream << ",\"user_index\":" << *bucket.user_index;
            }
            else
            {
                stream << ",\"user_index\":null";
            }
            if (!bucket.raw_param_prefix.empty())
            {
                stream << ",\"raw_param_prefix\":\"" << json_escape(bucket.raw_param_prefix)
                       << "\"";
            }
            stream << ",\"pre_hits\":" << bucket.pre_hits << "}";
        }
        stream << "]";
        if (config_.runtime_context_resolver)
        {
            json_field(stream, "runtime_context", config_.runtime_context_resolver());
        }
        stream << "}";

        const std::optional<std::filesystem::path> bridge_dir =
            config_.bridge_dir_resolver ? config_.bridge_dir_resolver() : std::nullopt;
        if (!bridge_dir.has_value())
        {
            return;
        }

        const std::string json = stream.str();
        write_text_file(*bridge_dir / std::filesystem::path{latest_filename}, json);
        append_text_file(*bridge_dir / std::filesystem::path{jsonl_filename}, json + "\n");
    }

    auto Kh3AutosaveProbe::write_breadcrumb(const HookTargetInfo& hook_info, HookPhase phase,
                                            UnrealScriptFunctionCallableContext& callback_context,
                                            std::wstring_view stage, UFunction* target_function,
                                            void* params) -> void
    {
        (void)hook_info;
        (void)phase;
        (void)callback_context;
        (void)stage;
        (void)target_function;
        (void)params;
    }

    auto Kh3AutosaveProbe::write_event(const HookTargetInfo& hook_info, HookPhase phase,
                                       UnrealScriptFunctionCallableContext& callback_context,
                                       UFunction* target_function, void* params) -> void
    {
        const bool include_returns = phase == HookPhase::Post;
        UFunction* const frame_node = get_frame_node(callback_context.TheStack);
        UObject* const frame_object = get_frame_object(callback_context.TheStack);
        const std::wstring frame_node_name = frame_node ? frame_node->GetFullName() : STR("<null>");
        const std::wstring frame_object_name =
            frame_object ? frame_object->GetFullName() : STR("<null>");
        std::vector<std::wstring> known_param_parts{};
        const auto inspect_known_param = [&](std::wstring_view param_name,
                                             std::wstring_view stage_name) -> std::wstring
        {
            (void)stage_name;
            FProperty* const property = find_function_param_by_name(target_function, param_name);
            std::wstring summary = std::wstring{param_name} + STR("=<missing>");
            if (property)
            {
                const int offset = property->GetOffset_Internal();
                const int element_size = property->GetElementSize();
                const int parms_size = target_function ? target_function->GetParmsSize() : 0;
                const std::wstring property_class = property->GetClass().GetName();
                const std::wstring raw_bytes =
                    params && offset >= 0 && element_size > 0
                        ? try_probe_pointer_bytes(reinterpret_cast<uint64_t>(params) +
                                                      static_cast<uint64_t>(offset),
                                                  static_cast<size_t>(std::min(element_size, 32)))
                              .value_or(STR("<unreadable>"))
                        : std::wstring{STR("<none>")};
                summary = std::format(
                    STR("{}={{property={}, class={}, offset={}, size={}, parms_size={}, raw={}}}"),
                    param_name, property->GetName(), property_class, offset, element_size,
                    parms_size, raw_bytes);
            }
            known_param_parts.push_back(summary);
            return summary;
        };
        std::wstring slot_name{};
        std::wstring user_index{};
        std::wstring save_game_object{};
        std::wstring return_value{};
        if (hook_info.function_name == save_game_to_slot_name)
        {
            slot_name = inspect_known_param(STR("SlotName"), STR("slot_name"));
            user_index = inspect_known_param(STR("UserIndex"), STR("user_index"));
            save_game_object = inspect_known_param(STR("SaveGameObject"), STR("save_game_object"));
            if (include_returns)
            {
                return_value = inspect_known_param(STR("ReturnValue"), STR("return_value"));
            }
        }
        else
        {
            inspect_known_param(STR("PlayerStartPosition"), STR("player_start_position"));
            inspect_known_param(STR("Map"), STR("map"));
            inspect_known_param(STR("Tag"), STR("tag"));
            inspect_known_param(STR("WorldCode"), STR("world_code"));
            inspect_known_param(STR("MapName"), STR("map_name"));
            inspect_known_param(STR("RetryPoint"), STR("retry_point"));
            inspect_known_param(STR("InMap"), STR("in_map"));
            inspect_known_param(STR("InTag"), STR("in_tag"));
            inspect_known_param(STR("InWorldCode"), STR("in_world_code"));
            inspect_known_param(STR("InMapName"), STR("in_map_name"));
        }
        std::wostringstream known_param_stream;
        for (size_t index = 0; index < known_param_parts.size(); ++index)
        {
            if (index > 0)
            {
                known_param_stream << STR("; ");
            }
            known_param_stream << known_param_parts[index];
        }
        const std::wstring known_params = known_param_stream.str();

        std::ostringstream stream;
        stream << "{\"type\":\"autosave_probe_event\"";
        stream << ",\"observed_at\":\"" << now_iso8601_utc() << "\"";
        json_field(stream, "build_stamp", config_.build_stamp);
        json_field(stream, "phase", phase_name(phase));
        json_field(stream, "function_name", hook_info.function_name);
        stream << ",\"seen_count\":" << seen_count_for(hook_info);
        json_field(stream, "frame_node", frame_node_name);
        json_field(stream, "frame_object", frame_object_name);
        json_field(stream, "params", known_params);
        json_bool_field(stream, "internal_bridge_call", RuntimeGrantInternalCallDepth::is_active());
        stream << "}";
        const std::string json = stream.str();

        const std::optional<std::filesystem::path> bridge_dir =
            config_.bridge_dir_resolver ? config_.bridge_dir_resolver() : std::nullopt;
        if (!bridge_dir.has_value())
        {
            return;
        }

        write_text_file(*bridge_dir / std::filesystem::path{latest_filename}, json);
        append_text_file(*bridge_dir / std::filesystem::path{jsonl_filename}, json + "\n");
    }
} // namespace kh3ap
