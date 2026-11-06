#include "kh3ap/diagnostics/autosave/detour.h"
#include "kh3ap/diagnostics/kh3_autosave_probe.h"
#include "kh3ap/diagnostics/autosave/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/Hooks.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include "kh3ap/bridge/utilities.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <UnrealDef.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    using namespace autosave_detail;
    Kh3AutosaveProbe::~Kh3AutosaveProbe()
    {
        uninstall(STR("destructor"));
    }

    auto Kh3AutosaveProbe::X64DetourDeleter::operator()(PLH::x64Detour* detour) const noexcept
        -> void
    {
        delete detour;
    }

    auto Kh3AutosaveProbe::install(Kh3AutosaveProbeConfig config) -> void
    {
        if (!config.enabled || installed_)
        {
            return;
        }

        if (config.steam_save_startup_guard_enabled)
        {
            install_early_steam_save_startup_guard();
        }

        config_ = std::move(config);
        save_game_to_slot_seen_ = 0;
        save_game_to_slot_pre_hits_.store(0);
        save_game_to_slot_post_hits_.store(0);
        save_game_to_slot_randomizer_pre_hits_.store(0);
        save_game_to_slot_randomizer_post_hits_.store(0);
        save_game_to_slot_non_randomizer_pre_hits_.store(0);
        save_game_to_slot_non_randomizer_post_hits_.store(0);
        save_game_to_slot_pre_hits_flushed_ = 0;
        save_game_to_slot_post_hits_flushed_ = 0;
        save_game_to_slot_randomizer_pre_hits_flushed_ = 0;
        save_game_to_slot_randomizer_post_hits_flushed_ = 0;
        save_game_to_slot_non_randomizer_pre_hits_flushed_ = 0;
        save_game_to_slot_non_randomizer_post_hits_flushed_ = 0;
        non_randomizer_caller_names_ = {};
        non_randomizer_caller_pre_hits_ = {};
        non_randomizer_caller_count_ = 0;
        non_randomizer_slot_buckets_ = {};
        non_randomizer_slot_bucket_count_ = 0;
        if (!save_file_open_probe_installed_)
        {
            save_file_open_probe_attempted_ = false;
            save_file_close_probe_installed_ = false;
            save_file_open_event_sequence_ = 0;
            save_file_open_events_flushed_ = 0;
            save_file_open_events_ = {};
            save_file_open_event_count_ = 0;
        }
        native_save_request_probe_attempted_ = false;
        native_save_request_hook_installed_ = false;
        native_save_probe_event_sequence_ = 0;
        native_save_probe_events_ = {};
        native_save_probe_event_count_ = 0;
        manual_save_handles_ = {};
        manual_save_handle_count_ = 0;
        manual_save_completed_events_ = {};
        manual_save_completed_event_count_ = 0;
        manual_save_completed_events_pending_.store(false, std::memory_order_relaxed);
        manual_save_completed_event_sequence_ = 0;
        autosave_basic_seen_ = 0;
        autosave_ex_seen_ = 0;
        last_save_game_to_slot_log_ = std::chrono::steady_clock::time_point{};
        next_counter_flush_ = std::chrono::steady_clock::time_point{};
        hook_infos_.clear();
        if (config_.save_game_to_slot_counter_enabled)
        {
            hook_infos_.push_back(HookTargetInfo{std::wstring{save_game_to_slot_path},
                                                 std::wstring{save_game_to_slot_name},
                                                 STR("SlotName"), STR("ReturnValue"), nullptr});
        }
        hook_ids_.clear();
        hook_ids_.reserve(hook_infos_.size());
        installed_ = true;

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] AutosaveProbe install begin hooks={}\n"), hook_infos_.size());
        const std::optional<std::filesystem::path> bridge_dir =
            config_.bridge_dir_resolver ? config_.bridge_dir_resolver() : std::nullopt;

        for (HookTargetInfo& hook_info : hook_infos_)
        {
            try
            {
                if (hook_info.function_name == save_game_to_slot_name)
                {
                    auto ids = UObjectGlobals::RegisterHook(
                        hook_info.function_path,
                        [this](UnrealScriptFunctionCallableContext& context, void* custom_data)
                        {
                            (void)custom_data;
                            count_save_game_to_slot(context, HookPhase::Pre);
                        },
                        [this](UnrealScriptFunctionCallableContext& context, void* custom_data)
                        {
                            (void)custom_data;
                            count_save_game_to_slot(context, HookPhase::Post);
                        },
                        &hook_info);
                    hook_ids_.push_back(ids);
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] AutosaveProbe counter hook installed function={} "
                            "ids=({}, {})\n"),
                        hook_info.function_path, ids.first, ids.second);

                    if (bridge_dir.has_value())
                    {
                        std::ostringstream stream;
                        stream << "{\"type\":\"autosave_probe_hook_installed\"";
                        stream << ",\"observed_at\":\"" << now_iso8601_utc() << "\"";
                        json_field(stream, "build_stamp", config_.build_stamp);
                        json_field(stream, "function_path", hook_info.function_path);
                        json_field(stream, "function_name", hook_info.function_name);
                        json_field(stream, "mode", STR("counter_callback_no_io"));
                        stream << ",\"pre_hook_id\":" << ids.first;
                        stream << ",\"post_hook_id\":" << ids.second;
                        stream << "}\n";
                        append_text_file(*bridge_dir /
                                             std::filesystem::path{installed_hooks_filename},
                                         stream.str());
                    }
                    continue;
                }

                if (hook_info.function_name == autosave_basic_name)
                {
                    auto ids = UObjectGlobals::RegisterHook(
                        hook_info.function_path,
                        [this](UnrealScriptFunctionCallableContext& context, void* custom_data)
                        {
                            (void)context;
                            (void)this;
                            (void)custom_data;
                        },
                        [](UnrealScriptFunctionCallableContext& context, void* custom_data)
                        {
                            (void)context;
                            (void)custom_data;
                        },
                        &hook_info);
                    hook_ids_.push_back(ids);
                    Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] AutosaveProbe minimal hook installed function={} "
                            "ids=({}, {})\n"),
                        hook_info.function_path, ids.first, ids.second);

                    if (bridge_dir.has_value())
                    {
                        std::ostringstream stream;
                        stream << "{\"type\":\"autosave_probe_hook_installed\"";
                        stream << ",\"observed_at\":\"" << now_iso8601_utc() << "\"";
                        json_field(stream, "build_stamp", config_.build_stamp);
                        json_field(stream, "function_path", hook_info.function_path);
                        json_field(stream, "function_name", hook_info.function_name);
                        json_field(stream, "mode", STR("noop_callbacks"));
                        stream << ",\"pre_hook_id\":" << ids.first;
                        stream << ",\"post_hook_id\":" << ids.second;
                        stream << "}\n";
                        append_text_file(*bridge_dir /
                                             std::filesystem::path{installed_hooks_filename},
                                         stream.str());
                    }
                    continue;
                }

                auto ids = UObjectGlobals::RegisterHook(
                    hook_info.function_path,
                    [this](UnrealScriptFunctionCallableContext& context, void* custom_data)
                    {
                        try
                        {
                            on_hook(context, static_cast<HookTargetInfo*>(custom_data),
                                    HookPhase::Pre);
                        }
                        catch (...)
                        {
                        }
                    },
                    [this](UnrealScriptFunctionCallableContext& context, void* custom_data)
                    {
                        try
                        {
                            on_hook(context, static_cast<HookTargetInfo*>(custom_data),
                                    HookPhase::Post);
                        }
                        catch (...)
                        {
                        }
                    },
                    &hook_info);
                hook_ids_.push_back(ids);
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] AutosaveProbe hook installed function={} ids=({}, {})\n"),
                    hook_info.function_path, ids.first, ids.second);

                if (bridge_dir.has_value())
                {
                    std::ostringstream stream;
                    stream << "{\"type\":\"autosave_probe_hook_installed\"";
                    stream << ",\"observed_at\":\"" << now_iso8601_utc() << "\"";
                    json_field(stream, "build_stamp", config_.build_stamp);
                    json_field(stream, "function_path", hook_info.function_path);
                    json_field(stream, "function_name", hook_info.function_name);
                    stream << ",\"pre_hook_id\":" << ids.first;
                    stream << ",\"post_hook_id\":" << ids.second;
                    stream << "}\n";
                    append_text_file(*bridge_dir / std::filesystem::path{installed_hooks_filename},
                                     stream.str());
                }
            }
            catch (...)
            {
                Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] AutosaveProbe hook registration failed function={}\n"),
                    hook_info.function_path);
            }
        }

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] AutosaveProbe install end installed_hooks={}\n"),
            hook_ids_.size());
        install_native_save_request_probe(STR("autosave_probe_install"));
        install_save_file_open_probe(STR("autosave_probe_install"));
    }

    auto Kh3AutosaveProbe::uninstall(std::wstring_view reason) -> void
    {
        uninstall_steam_save_startup_guard();
        uninstall_save_file_open_probe();
        if (!installed_)
        {
            return;
        }

        const size_t count = std::min(hook_infos_.size(), hook_ids_.size());
        for (size_t index = 0; index < count; ++index)
        {
            try
            {
                UObjectGlobals::UnregisterHook(hook_infos_[index].function_path, hook_ids_[index]);
            }
            catch (...)
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] AutosaveProbe hook unregister "
                                                    "failed function={} reason={}\n"),
                                                hook_infos_[index].function_path, reason);
            }
        }

        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] AutosaveProbe uninstalled hooks={} reason={}\n"), count, reason);
        uninstall_native_save_request_probe();
        hook_ids_.clear();
        hook_infos_.clear();
        installed_ = false;
    }

    auto Kh3AutosaveProbe::update(std::chrono::steady_clock::time_point now) -> void
    {
        if (!installed_)
        {
            return;
        }

        if (manual_save_completed_events_pending_.load(std::memory_order_acquire))
        {
            flush_manual_save_completed_events();
        }

        if (now < next_counter_flush_)
        {
            return;
        }
        next_counter_flush_ = now + std::chrono::seconds{1};

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
        flush_native_save_probe_events();
        flush_save_file_open_events();
        if (pre_hits == save_game_to_slot_pre_hits_flushed_ &&
            post_hits == save_game_to_slot_post_hits_flushed_ &&
            randomizer_pre_hits == save_game_to_slot_randomizer_pre_hits_flushed_ &&
            randomizer_post_hits == save_game_to_slot_randomizer_post_hits_flushed_ &&
            non_randomizer_pre_hits == save_game_to_slot_non_randomizer_pre_hits_flushed_ &&
            non_randomizer_post_hits == save_game_to_slot_non_randomizer_post_hits_flushed_)
        {
            return;
        }

        write_counter_snapshot(STR("update_flush"));
    }
} // namespace kh3ap
