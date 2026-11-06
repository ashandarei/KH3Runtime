#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/Hooks.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <UnrealDef.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto save_parameter_probe_bytes = 512;
        constexpr auto save_parameter_log_bytes = 96;
        constexpr auto slot_string_num_offset = sizeof(uint64_t);
        constexpr auto slot_string_capacity_offset = slot_string_num_offset + sizeof(int32_t);
        constexpr auto slot_user_index_offset = slot_string_capacity_offset + sizeof(int32_t);

        constexpr auto minimum_save_slot_parameter_bytes = 20;
        constexpr auto max_save_slot_name_chars = 128;
        constexpr auto max_save_slot_name_capacity = 256;
        constexpr auto max_observed_save_user_index = 1000;
    } // namespace

    using namespace autosave_detail;
    auto Kh3AutosaveProbe::get_or_find_function_object(HookTargetInfo& hook_info) -> UFunction*
    {
        if (!hook_info.function_object)
        {
            hook_info.function_object = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, hook_info.function_path);
        }
        return hook_info.function_object;
    }

    auto
    Kh3AutosaveProbe::count_save_game_to_slot(UnrealScriptFunctionCallableContext& callback_context,
                                              HookPhase phase) -> void
    {
        if (phase == HookPhase::Pre)
        {
            save_game_to_slot_pre_hits_.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            save_game_to_slot_post_hits_.fetch_add(1, std::memory_order_relaxed);
        }

        bool is_randomizer_caller = false;
        std::wstring frame_node_name{};
        if (UFunction* const frame_node = get_frame_node(callback_context.TheStack))
        {
            frame_node_name = frame_node->GetFullName();
            is_randomizer_caller =
                frame_node_name.find(STR("/Game/Mods/KH3Randomizer/")) != std::wstring::npos;
        }

        if (is_randomizer_caller)
        {
            if (phase == HookPhase::Pre)
            {
                save_game_to_slot_randomizer_pre_hits_.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                save_game_to_slot_randomizer_post_hits_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        else
        {
            if (phase == HookPhase::Pre)
            {
                save_game_to_slot_non_randomizer_pre_hits_.fetch_add(1, std::memory_order_relaxed);
                const std::wstring_view caller_name =
                    frame_node_name.empty() ? STR("<null>") : std::wstring_view{frame_node_name};
                record_non_randomizer_caller(caller_name);
                record_non_randomizer_slot(
                    caller_name, try_decode_save_slot(get_frame_locals(callback_context.TheStack)));
            }
            else
            {
                save_game_to_slot_non_randomizer_post_hits_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    auto Kh3AutosaveProbe::try_decode_save_slot(void* params) -> DecodedSaveSlot
    {
        if (!params)
        {
            return DecodedSaveSlot{
                .slot_name = STR("<no_params>"),
                .slot_offset = -1,
                .user_index = std::nullopt,
                .raw_param_prefix = {},
            };
        }

        std::array<std::byte, save_parameter_probe_bytes> param_bytes{};
        SIZE_T bytes_read = 0;
        const bool copied_params =
            looks_like_process_pointer(reinterpret_cast<uint64_t>(params)) &&
            ReadProcessMemory(GetCurrentProcess(), params, param_bytes.data(), param_bytes.size(),
                              &bytes_read) &&
            bytes_read >= minimum_save_slot_parameter_bytes;
        if (!copied_params)
        {
            return DecodedSaveSlot{
                .slot_name = STR("<unreadable_params>"),
                .slot_offset = -1,
                .user_index = std::nullopt,
                .raw_param_prefix = try_probe_pointer_bytes(reinterpret_cast<uint64_t>(params),
                                                            save_parameter_log_bytes)
                                        .value_or(STR("<unreadable>")),
            };
        }

        const auto* const bytes = param_bytes.data();
        const int max_offset = static_cast<int>(std::min<SIZE_T>(bytes_read, param_bytes.size()) -
                                                minimum_save_slot_parameter_bytes);
        for (int offset = 0; offset <= max_offset; offset += sizeof(int32_t))
        {
            uint64_t data_ptr = 0;
            int32_t array_num = 0;
            int32_t array_max = 0;
            std::memcpy(&data_ptr, bytes + offset, sizeof(data_ptr));
            std::memcpy(&array_num, bytes + offset + slot_string_num_offset, sizeof(array_num));
            std::memcpy(&array_max, bytes + offset + slot_string_capacity_offset,
                        sizeof(array_max));

            if (!looks_like_process_pointer(data_ptr) || array_num <= 0 ||
                array_num > max_save_slot_name_chars || array_max < array_num ||
                array_max > max_save_slot_name_capacity)
            {
                continue;
            }

            std::optional<std::wstring> value =
                try_read_utf16_string(data_ptr, static_cast<size_t>(array_num));
            if (!value.has_value() || value->empty())
            {
                continue;
            }

            std::optional<int32_t> user_index{};
            int32_t raw_user_index = 0;
            std::memcpy(&raw_user_index, bytes + offset + slot_user_index_offset,
                        sizeof(raw_user_index));
            if (raw_user_index >= -1 && raw_user_index <= max_observed_save_user_index)
            {
                user_index = raw_user_index;
            }

            return DecodedSaveSlot{
                .slot_name = *value,
                .slot_offset = offset,
                .user_index = user_index,
                .raw_param_prefix = {},
            };
        }

        return DecodedSaveSlot{
            .slot_name = STR("<undecoded>"),
            .slot_offset = -1,
            .user_index = std::nullopt,
            .raw_param_prefix = try_probe_pointer_bytes(reinterpret_cast<uint64_t>(params),
                                                        save_parameter_log_bytes)
                                    .value_or(STR("<unreadable>")),
        };
    }

    auto Kh3AutosaveProbe::record_non_randomizer_caller(std::wstring_view caller_name) -> void
    {
        for (size_t index = 0; index < non_randomizer_caller_count_; ++index)
        {
            if (non_randomizer_caller_names_[index] == caller_name)
            {
                ++non_randomizer_caller_pre_hits_[index];
                return;
            }
        }

        if (non_randomizer_caller_count_ < non_randomizer_caller_names_.size())
        {
            non_randomizer_caller_names_[non_randomizer_caller_count_] = std::wstring{caller_name};
            non_randomizer_caller_pre_hits_[non_randomizer_caller_count_] = 1;
            ++non_randomizer_caller_count_;
            return;
        }

        ++non_randomizer_caller_pre_hits_[non_randomizer_caller_names_.size() - 1];
    }

    auto Kh3AutosaveProbe::record_non_randomizer_slot(std::wstring_view caller_name,
                                                      const DecodedSaveSlot& slot) -> void
    {
        for (size_t index = 0; index < non_randomizer_slot_bucket_count_; ++index)
        {
            NonRandomizerSlotBucket& bucket = non_randomizer_slot_buckets_[index];
            if (bucket.caller_name == caller_name && bucket.slot_name == slot.slot_name &&
                bucket.slot_offset == slot.slot_offset && bucket.user_index == slot.user_index)
            {
                ++bucket.pre_hits;
                return;
            }
        }

        if (non_randomizer_slot_bucket_count_ < non_randomizer_slot_buckets_.size())
        {
            non_randomizer_slot_buckets_[non_randomizer_slot_bucket_count_] =
                NonRandomizerSlotBucket{
                    .caller_name = std::wstring{caller_name},
                    .slot_name = slot.slot_name,
                    .slot_offset = slot.slot_offset,
                    .user_index = slot.user_index,
                    .raw_param_prefix = slot.raw_param_prefix,
                    .pre_hits = 1,
                };
            ++non_randomizer_slot_bucket_count_;
            return;
        }

        ++non_randomizer_slot_buckets_[non_randomizer_slot_buckets_.size() - 1].pre_hits;
    }

    auto Kh3AutosaveProbe::should_write_event(const HookTargetInfo& hook_info, HookPhase phase)
        -> bool
    {
        if (hook_info.function_name == save_game_to_slot_name)
        {
            if (phase != HookPhase::Pre)
            {
                return false;
            }
            ++save_game_to_slot_seen_;

            const auto now = std::chrono::steady_clock::now();
            const bool initial_sample =
                save_game_to_slot_seen_ <= save_game_to_slot_initial_sample_limit;
            const bool interval_sample =
                save_game_to_slot_seen_ % save_game_to_slot_sample_interval == 0;
            const bool time_sample =
                last_save_game_to_slot_log_ == std::chrono::steady_clock::time_point{} ||
                now - last_save_game_to_slot_log_ >= std::chrono::seconds{5};
            if (initial_sample || interval_sample || time_sample)
            {
                last_save_game_to_slot_log_ = now;
                return true;
            }
            return false;
        }

        if (hook_info.function_name == autosave_basic_name)
        {
            ++autosave_basic_seen_;
            return true;
        }

        if (hook_info.function_name == autosave_ex_name)
        {
            ++autosave_ex_seen_;
            return true;
        }

        return true;
    }

    auto Kh3AutosaveProbe::seen_count_for(const HookTargetInfo& hook_info) const -> uint64_t
    {
        if (hook_info.function_name == save_game_to_slot_name)
        {
            return save_game_to_slot_seen_;
        }
        if (hook_info.function_name == autosave_basic_name)
        {
            return autosave_basic_seen_;
        }
        if (hook_info.function_name == autosave_ex_name)
        {
            return autosave_ex_seen_;
        }
        return 0;
    }

    auto Kh3AutosaveProbe::on_hook(UnrealScriptFunctionCallableContext& callback_context,
                                   HookTargetInfo* hook_info, HookPhase phase) -> void
    {
        if (!hook_info)
        {
            return;
        }

        write_breadcrumb(*hook_info, phase, callback_context, STR("enter"));
        if (!should_write_event(*hook_info, phase))
        {
            return;
        }

        UFunction* const target_function = get_or_find_function_object(*hook_info);
        write_breadcrumb(*hook_info, phase, callback_context, STR("function_resolved"),
                         target_function);
        if (!target_function)
        {
            Output::send<LogLevel::Warning>(
                STR("[KH3APProbeCpp] AutosaveProbe missing function_object hook={} phase={}\n"),
                hook_info->function_path, phase_name(phase));
            return;
        }

        void* const params = get_frame_locals(callback_context.TheStack);
        write_breadcrumb(*hook_info, phase, callback_context, STR("locals_resolved"),
                         target_function, params);
        write_event(*hook_info, phase, callback_context, target_function, params);
        write_breadcrumb(*hook_info, phase, callback_context, STR("detail_written"),
                         target_function, params);
    }
} // namespace kh3ap
