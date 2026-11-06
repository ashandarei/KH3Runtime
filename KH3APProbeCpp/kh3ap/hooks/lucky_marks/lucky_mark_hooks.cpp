#include "kh3ap/app/config.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/hooks/memory/memory_hook_platform.h"
#include "kh3ap/hooks/memory/kh3_memory_hooks.h"
#include "kh3ap/hooks/memory/memory_hook_detail.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/utilities.h"
#include <UnrealDef.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "kh3ap/bridge/checks/lucky_marks.h"
#include "kh3ap/hooks/inventory/item_catalog.h"
#include "kh3ap/hooks/lucky_marks/lucky_mark_retirement.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <format>
#include <mutex>
#include <sstream>
#include <utility>

namespace kh3ap
{
    using ::kh3ap::settings::s_enable_lucky_mark_native_ap_datatable_row_dump_diagnostic;
    using ::kh3ap::settings::s_enable_lucky_mark_native_ap_datatable_text;
    using ::kh3ap::settings::s_enable_lucky_mark_native_ap_fstring_assignment;
    using ::kh3ap::settings::s_enable_lucky_mark_native_ap_resolver;
    using ::kh3ap::settings::s_enable_lucky_mark_native_ap_resolver_event_diagnostics;
    using ::kh3ap::settings::s_enable_lucky_mark_native_ap_shell_item_text_initializer;
    using ::kh3ap::settings::s_lucky_mark_item_id_to_reward_text_egs_offset;
    using ::kh3ap::settings::s_lucky_mark_item_id_to_reward_text_steam_offset;
    using ::kh3ap::settings::s_lucky_mark_native_text_shell_item_id;
    using ::kh3ap::settings::s_lucky_mark_reward_token_to_item_id_egs_offset;
    using ::kh3ap::settings::s_lucky_mark_reward_token_to_item_id_steam_offset;

    using namespace memory_hook_detail;
    auto Kh3MemoryHooks::retire_lucky_mark_native_ap_milestone_for_reward_token(
        uint64_t reward_token_name) -> void
    {
        std::lock_guard<std::mutex> lock{lucky_mark_native_retirement_mutex_};
        const auto rows = collect_lucky_mark_native_milestone_rows();
        if (!rows.has_value())
        {
            return;
        }

        const auto row =
            std::find_if(rows->begin(), rows->end(),
                         [reward_token_name](const LuckyMarkNativeMilestoneRow& candidate)
                         {
                             return candidate.reward_token_name == reward_token_name;
                         });
        if (row == rows->end() || !lucky_mark_reward_index_needs_retirement(row->reward_index))
        {
            return;
        }

        if (std::find(lucky_mark_native_retired_milestones_.begin(),
                      lucky_mark_native_retired_milestones_.end(),
                      row->milestone) == lucky_mark_native_retired_milestones_.end())
        {
            lucky_mark_native_retired_milestones_.push_back(row->milestone);
            std::sort(lucky_mark_native_retired_milestones_.begin(),
                      lucky_mark_native_retired_milestones_.end());
        }
        write_lucky_mark_native_retired_alias(row->row_ptr);
    }

    auto Kh3MemoryHooks::reapply_lucky_mark_native_ap_retirements(std::wstring_view reason) -> void
    {
        (void)reason;
        std::lock_guard<std::mutex> lock{lucky_mark_native_retirement_mutex_};

        // Membership is recovered and committed by the native worker. Repeated
        // calls also observe completion of asynchronous startup recovery.
        const auto progress = lucky_marks::view();
        if (progress->ready)
        {
            lucky_mark_native_retired_milestones_.insert(
                lucky_mark_native_retired_milestones_.end(), progress->milestones.begin(),
                progress->milestones.end());
            std::sort(lucky_mark_native_retired_milestones_.begin(),
                      lucky_mark_native_retired_milestones_.end());
            lucky_mark_native_retired_milestones_.erase(
                std::unique(lucky_mark_native_retired_milestones_.begin(),
                            lucky_mark_native_retired_milestones_.end()),
                lucky_mark_native_retired_milestones_.end());
        }

        if (lucky_mark_native_retired_milestones_.empty())
        {
            return;
        }

        const auto rows = collect_lucky_mark_native_milestone_rows();
        if (!rows.has_value())
        {
            return;
        }
        for (const auto& row : *rows)
        {
            if (!lucky_mark_reward_index_needs_retirement(row.reward_index) ||
                !std::binary_search(lucky_mark_native_retired_milestones_.begin(),
                                    lucky_mark_native_retired_milestones_.end(), row.milestone))
            {
                continue;
            }
            write_lucky_mark_native_retired_alias(row.row_ptr);
        }
    }

    auto original_item_catalog_lookup(uintptr_t expected_target) -> uintptr_t
    {
        return Kh3MemoryHooks::original_item_catalog_lookup(expected_target);
    }

    auto Kh3MemoryHooks::original_item_catalog_lookup(uintptr_t expected_target) -> uintptr_t
    {
        auto* owner = s_lucky_mark_native_ap_resolver_owner;
        if (!owner || !owner->state_.lucky_mark_reward_token_to_item_id_installed ||
            owner->state_.lucky_mark_reward_token_to_item_id_target != expected_target)
        {
            return 0;
        }
        return owner->state_.lucky_mark_reward_token_to_item_id_trampoline;
    }

    auto Kh3MemoryHooks::lucky_mark_reward_token_to_item_id_hook(void* resolver,
                                                                 uint64_t reward_token_name)
        -> int32_t
    {
        Kh3MemoryHooks* const owner = s_lucky_mark_native_ap_resolver_owner;
        if (!owner || owner->state_.lucky_mark_reward_token_to_item_id_trampoline == 0)
        {
            return 0;
        }

        using OriginalFn = int32_t(__fastcall*)(void*, uint64_t);
        auto* const original = reinterpret_cast<OriginalFn>(
            owner->state_.lucky_mark_reward_token_to_item_id_trampoline);
        const int32_t native_result = original(resolver, reward_token_name);
        const auto decoded = decode_name_value(reward_token_name);
        const auto resolution = resolve_lucky_mark_native_ap_reward(
            native_result, try_parse_lucky_mark_native_ap_item_id(best_decoded_fname(decoded)));
        const int32_t result = resolution.item_id;
        // Required bridge transport, intentionally independent of the optional
        // high-volume resolver diagnostics below.
        if (resolution.process_ap_check)
        {
            owner->retire_lucky_mark_native_ap_milestone_for_reward_token(reward_token_name);
            owner->record_lucky_mark_native_ap_check(reward_token_name, result);
        }
        if constexpr (s_enable_lucky_mark_native_ap_resolver_event_diagnostics)
        {
            owner->append_lucky_mark_reward_token_to_item_id_diagnostic_event(
                resolver, reward_token_name, result);
        }
        return result;
    }

    auto Kh3MemoryHooks::lucky_mark_item_id_to_reward_text_hook(void* formatter, void* out_text,
                                                                int32_t item_id,
                                                                bool use_display_name) -> void*
    {
        Kh3MemoryHooks* const owner = s_lucky_mark_native_ap_resolver_owner;
        if (!owner || owner->state_.lucky_mark_item_id_to_reward_text_trampoline == 0)
        {
            return nullptr;
        }

        std::wstring out_text_before{};
        if constexpr (s_enable_lucky_mark_native_ap_resolver_event_diagnostics)
        {
            out_text_before =
                out_text ? try_probe_pointer_bytes(reinterpret_cast<uint64_t>(out_text), 80)
                               .value_or(STR("<unreadable>"))
                         : std::wstring{STR("<null>")};
        }

        bool datatable_text_applied = false;
        bool fstring_text_applied = false;
        bool shell_text_applied = false;
        std::wstring datatable_display_name{};
        std::wstring formatter_display_text{};
        if constexpr (s_enable_lucky_mark_native_ap_datatable_text)
        {
            if (const auto display_name = try_resolve_lucky_mark_native_ap_display_name(item_id);
                display_name.has_value())
            {
                datatable_display_name = *display_name;
            }
        }

        int32_t formatter_item_id_used = item_id;
        if constexpr (s_enable_lucky_mark_native_ap_shell_item_text_initializer)
        {
            if (!datatable_display_name.empty())
            {
                formatter_item_id_used =
                    static_cast<int32_t>(s_lucky_mark_native_text_shell_item_id);
                shell_text_applied = true;
            }
        }

        using OriginalFn = void*(__fastcall*)(void*, void*, int32_t, bool);
        auto* const original = reinterpret_cast<OriginalFn>(
            owner->state_.lucky_mark_item_id_to_reward_text_trampoline);
        void* const return_value =
            original(formatter, out_text, formatter_item_id_used, use_display_name);

        if constexpr (s_enable_lucky_mark_native_ap_fstring_assignment)
        {
            if (!datatable_display_name.empty())
            {
                fstring_text_applied = apply_lucky_mark_native_fstring_assignment_display_text(
                    out_text, datatable_display_name, formatter_display_text);
                datatable_text_applied = fstring_text_applied;
            }
        }
        if constexpr (s_enable_lucky_mark_native_ap_resolver_event_diagnostics)
        {
            owner->append_lucky_mark_item_id_to_reward_text_diagnostic_event(
                formatter, out_text, item_id, use_display_name, return_value, out_text_before,
                datatable_text_applied, datatable_display_name, fstring_text_applied,
                formatter_display_text, shell_text_applied, formatter_item_id_used);
        }
        if constexpr (s_enable_lucky_mark_native_ap_datatable_row_dump_diagnostic)
        {
            if (owner->should_dump_lucky_mark_native_ap_datatable_diagnostic(item_id))
            {
                owner->append_lucky_mark_native_ap_datatable_diagnostic(
                    formatter, item_id, STR("synthetic_item_id_formatter_call"));
            }
        }
        return return_value;
    }

    auto Kh3MemoryHooks::record_lucky_mark_native_ap_check(uint64_t reward_token_name,
                                                           int32_t result) -> void
    {
        if (!config_.bridge_dir_resolver)
        {
            return;
        }
        const auto bridge_dir = config_.bridge_dir_resolver();
        if (!bridge_dir)
        {
            return;
        }
        const auto decoded_name = decode_name_value(reward_token_name);
        lucky_marks::record(*bridge_dir, best_decoded_fname(decoded_name), result,
                            decoded_name.high32);
    }

    auto Kh3MemoryHooks::install_lucky_mark_native_ap_resolver_once(std::wstring_view reason)
        -> void
    {
        if constexpr (!s_enable_lucky_mark_native_ap_resolver)
        {
            return;
        }
        if (state_.lucky_mark_native_ap_resolver_attempted)
        {
            return;
        }
        state_.lucky_mark_native_ap_resolver_attempted = true;

        HMODULE const module = GetModuleHandleW(nullptr);
        const uintptr_t module_base = reinterpret_cast<uintptr_t>(module);
        const size_t module_size = module_size_from_pe_header(module);
        const bool egs_1_0_0_cl_1065258 =
            module_size == static_cast<size_t>(native_build::egs.image_size);
        const uint64_t token_offset = egs_1_0_0_cl_1065258
                                          ? s_lucky_mark_reward_token_to_item_id_egs_offset
                                          : s_lucky_mark_reward_token_to_item_id_steam_offset;
        const uint64_t text_offset = egs_1_0_0_cl_1065258
                                         ? s_lucky_mark_item_id_to_reward_text_egs_offset
                                         : s_lucky_mark_item_id_to_reward_text_steam_offset;
        const auto token_target = find_lucky_mark_native_ap_resolver_target(
            module_base, module_size, token_offset, STR("reward_token_to_item_id"));
        const auto text_target = find_lucky_mark_native_ap_resolver_target(
            module_base, module_size, text_offset, STR("item_id_to_reward_text"));

        if (!token_target.has_value())
        {
            append_lucky_mark_native_ap_resolver_install_diagnostic_log(
                STR("target_not_found"), STR("reward_token_to_item_id"), token_offset, module_base,
                module_size, module_base + static_cast<uintptr_t>(token_offset), false, reason);
        }
        if (!text_target.has_value())
        {
            append_lucky_mark_native_ap_resolver_install_diagnostic_log(
                STR("target_not_found"), STR("item_id_to_reward_text"), text_offset, module_base,
                module_size, module_base + static_cast<uintptr_t>(text_offset), false, reason);
        }
        if (!token_target.has_value() && !text_target.has_value())
        {
            return;
        }

        s_lucky_mark_native_ap_resolver_owner = this;

        if (token_target.has_value())
        {
            state_.lucky_mark_reward_token_to_item_id_target = *token_target;
            state_.lucky_mark_reward_token_to_item_id_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(*token_target),
                reinterpret_cast<uint64_t>(
                    &Kh3MemoryHooks::lucky_mark_reward_token_to_item_id_hook),
                &state_.lucky_mark_reward_token_to_item_id_trampoline);
            const bool hook_result = state_.lucky_mark_reward_token_to_item_id_detour->hook();
            state_.lucky_mark_reward_token_to_item_id_installed = hook_result;
            append_lucky_mark_native_ap_resolver_install_diagnostic_log(
                hook_result ? STR("installed") : STR("hook_failed"), STR("reward_token_to_item_id"),
                token_offset, module_base, module_size, *token_target, hook_result, reason);
        }

        if (text_target.has_value())
        {
            state_.lucky_mark_item_id_to_reward_text_target = *text_target;
            state_.lucky_mark_item_id_to_reward_text_detour = std::make_unique<PLH::x64Detour>(
                static_cast<uint64_t>(*text_target),
                reinterpret_cast<uint64_t>(&Kh3MemoryHooks::lucky_mark_item_id_to_reward_text_hook),
                &state_.lucky_mark_item_id_to_reward_text_trampoline);
            const bool hook_result = state_.lucky_mark_item_id_to_reward_text_detour->hook();
            state_.lucky_mark_item_id_to_reward_text_installed = hook_result;
            append_lucky_mark_native_ap_resolver_install_diagnostic_log(
                hook_result ? STR("installed") : STR("hook_failed"), STR("item_id_to_reward_text"),
                text_offset, module_base, module_size, *text_target, hook_result, reason);
        }
    }

    auto Kh3MemoryHooks::uninstall_lucky_mark_native_ap_resolver() -> void
    {
        if (state_.lucky_mark_item_id_to_reward_text_detour)
        {
            if (state_.lucky_mark_item_id_to_reward_text_installed)
            {
                state_.lucky_mark_item_id_to_reward_text_detour->unHook();
            }
            state_.lucky_mark_item_id_to_reward_text_detour.reset();
        }
        if (state_.lucky_mark_reward_token_to_item_id_detour)
        {
            if (state_.lucky_mark_reward_token_to_item_id_installed)
            {
                state_.lucky_mark_reward_token_to_item_id_detour->unHook();
            }
            state_.lucky_mark_reward_token_to_item_id_detour.reset();
        }
        if (s_lucky_mark_native_ap_resolver_owner == this)
        {
            s_lucky_mark_native_ap_resolver_owner = nullptr;
        }
        state_.lucky_mark_item_id_to_reward_text_installed = false;
        state_.lucky_mark_reward_token_to_item_id_installed = false;
    }
} // namespace kh3ap
