#include <memory>
#include <utility>
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include "kh3ap/features/story/progression/game_flag_events.h"
#include "kh3ap/features/story/progression/event_tracking_service.h"
#include "kh3ap/features/story/progression/vbonus_tracking_policy.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/grant_internal_call_depth.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/checks/check_checkpoint.h"
#include "kh3ap/bridge/catalog/catalog.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/utilities.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/NameTypes.hpp>
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace kh3ap
{

    using namespace RC::Unreal;

    namespace
    {
        constexpr auto flag_query_result_offset = sizeof(RC::Unreal::FName);
        constexpr auto flag_boolean_query_parameter_bytes =
            sizeof(RC::Unreal::FName) + sizeof(bool);
        constexpr auto flag_integer_query_parameter_bytes =
            sizeof(RC::Unreal::FName) + sizeof(int32_t);
        constexpr auto flag_query_storage_bytes = 16;

        constexpr auto flag_comparison_result_offset =
            sizeof(RC::Unreal::FName) + sizeof(RC::Unreal::FName);
        constexpr auto flag_comparison_parameter_bytes =
            flag_comparison_result_offset + sizeof(bool);
        constexpr auto flag_comparison_storage_bytes = 24;

        constexpr auto event_flag_update_budget = 16;
        constexpr auto max_tracked_event_flags = 4096;
        constexpr auto schema = L"kh3ap.event_progress.v1";
        constexpr auto config_file = L"slot_data.json";
        constexpr auto progress_file = L"event_progress.json";
        constexpr auto state_file = channels::runtime_event_checks_file;
        uint64_t raw_name(FName name)
        {
            uint64_t raw;
            static_assert(sizeof(name) == sizeof(raw));
            std::memcpy(&raw, &name, sizeof(raw));
            return raw;
        }
    } // namespace
    struct RuntimeEventTrackingService::Impl
    {
        BridgeResolver resolver;
        std::filesystem::path bridge;
        std::wstring seed;
        int64_t slot{};
        uint64_t generation{}, recovery_scans{}, flag_reads{};
        VBonusFlagEvents inbox, integer_inbox;
        struct Condition
        {
            FName name;
            int32_t threshold;
            bool tt_clear;
        };
        std::map<uint64_t, std::vector<std::wstring>> watched;
        std::map<std::wstring, Condition> flags;
        std::set<std::wstring> completed;
        bool initialized{}, ready{}, recover{}, failed{}, dirty{};
        bool recovering{};
        std::wstring recovery_after;
        std::string config_text;
        FWeakObjectPtr flag_library;
        explicit Impl(BridgeResolver value) : resolver(std::move(value))
        {
        }
        ~Impl()
        {
            unsubscribe_game_flags(inbox);
            unsubscribe_game_flags(integer_inbox);
        }
        void install()
        {
            subscribe_game_flags(inbox);
            subscribe_game_flags(integer_inbox, true);
        }
        bool flag_value(const Condition& condition)
        {
            ++flag_reads;
            auto* owner = flag_library.Get();
            if (!is_uobject_valid_fast(owner))
            {
                owner = find_loaded_runtime_asset(
                    L"/Script/TresGame.Default__TresGameBlueprintLibrary");
                flag_library = make_uobject_weak(owner);
            }
            if (!is_uobject_valid_fast(owner))
            {
                throw std::runtime_error("event_flag_owner");
            }
            const bool boolean = condition.threshold == -1;
            auto* fn =
                owner->GetFunctionByNameInChain(boolean ? L"GetGameFlagBoolean" : L"GetGameFlag");
            if (!is_uobject_valid_fast(fn) ||
                fn->GetParmsSize() != (boolean ? flag_boolean_query_parameter_bytes
                                               : flag_integer_query_parameter_bytes))
            {
                throw std::runtime_error("event_flag_function");
            }
            auto* key = find_function_param_by_name(fn, L"FlagName");
            auto* result = find_function_param_by_name(fn, L"ReturnValue");
            if (!key || key->GetClass().GetName() != L"NameProperty" ||
                key->GetOffset_Internal() != 0 ||
                key->GetElementSize() != sizeof(RC::Unreal::FName) || !result ||
                result->GetOffset_Internal() != flag_query_result_offset)
            {
                throw std::runtime_error("event_flag_parameters");
            }
            alignas(uint64_t) std::array<std::byte, flag_query_storage_bytes> params{};
            std::memcpy(params.data(), &condition.name, sizeof(condition.name));
            const RuntimeGrantInternalCallDepth::Scope scope{};
            owner->ProcessEvent(fn, params.data());
            bool complete{};
            if (boolean)
            {
                if (!result->IsA<FBoolProperty>())
                {
                    throw std::runtime_error("event_bool_result");
                }
                complete =
                    static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(params.data());
            }
            else
            {
                if (result->GetClass().GetName() != L"IntProperty" ||
                    result->GetElementSize() != sizeof(int32_t))
                {
                    throw std::runtime_error("event_int_result");
                }
                complete =
                    *result->ContainerPtrToValuePtr<int32_t>(params.data()) >= condition.threshold;
            }
            if (complete && condition.tt_clear)
            {
                auto* clear = owner->GetFunctionByNameInChain(L"GreaterEqual_FlagLabel");
                if (!is_uobject_valid_fast(clear) ||
                    clear->GetParmsSize() != flag_comparison_parameter_bytes)
                {
                    throw std::runtime_error("event_tt_function");
                }
                alignas(uint64_t) std::array<std::byte, flag_comparison_storage_bytes> args{};
                const FName flag(L"gameflow_WM_WORLD_TT", FNAME_Add),
                    label(L"WM_WORLD_TT_CLEARED", FNAME_Add);
                std::memcpy(args.data(), &flag, sizeof(flag));
                std::memcpy(args.data() + sizeof(flag), &label, sizeof(label));
                auto* out = find_function_param_by_name(clear, L"ReturnValue");
                if (!out || !out->IsA<FBoolProperty>() ||
                    out->GetOffset_Internal() != flag_comparison_result_offset)
                {
                    throw std::runtime_error("event_tt_result");
                }
                owner->ProcessEvent(clear, args.data());
                complete =
                    static_cast<FBoolProperty*>(out)->GetPropertyValueInContainer(args.data());
            }
            return complete;
        }
        void configure(const std::filesystem::path& directory, const std::string& text)
        {
            if (initialized && bridge == directory && text == config_text)
            {
                return;
            }
            // Never apply a new scope until the old scope's pending state is saved.
            if (dirty)
            {
                persist();
            }
            auto setup = native_catalog::check_setup(text, native_catalog::CheckKind::Event);
            auto new_seed = std::move(setup.seed_name);
            auto new_slot = setup.slot;
            std::map<std::wstring, Condition> next_flags;
            std::set<uint64_t> bool_names, int_names;
            std::map<uint64_t, std::vector<std::wstring>> next_watched;
            for (const auto& row : setup.rows)
            {
                const auto& id = row.source_id;
                const auto& flag = row.flag_name;
                const auto threshold = row.threshold;
                FName name(flag, FNAME_Add);
                const bool tt = row.requires_tt_clear;
                next_flags.emplace(id, Condition{name, static_cast<int32_t>(threshold), tt});
                next_watched[raw_name(name)].push_back(id);
                (threshold == -1 ? bool_names : int_names).insert(raw_name(name));
                if (tt)
                {
                    const auto dependency = raw_name(FName(L"gameflow_WM_WORLD_TT", FNAME_Add));
                    int_names.insert(dependency);
                    next_watched[dependency].push_back(id);
                }
            }
            if (next_flags.size() > max_tracked_event_flags)
            {
                throw std::runtime_error("event_config_count");
            }
            install();
            auto next_completed =
                load_check_checkpoint(directory / progress_file, schema, new_seed, new_slot);
            bridge = directory;
            seed = std::move(new_seed);
            slot = new_slot;
            flags = std::move(next_flags);
            completed = std::move(next_completed);
            watched = std::move(next_watched);
            inbox.configure(std::move(bool_names));
            integer_inbox.configure(std::move(int_names));
            config_text = text;
            initialized = true;
            failed = false;
            dirty = true;
            recover = ready;
            recovering = false;
            recovery_after.clear();
            persist();
            RC::Output::send<RC::LogLevel::Warning>(
                L"[KH3APProbeCpp] NativeEventTracking configured checks={} completed={} "
                L"mode=events\n",
                flags.size(), completed.size());
        }
        void persist()
        {
            const auto events = inbox.events() + integer_inbox.events();
            std::ostringstream out;
            out << "{\"schema\":\"kh3ap.event_progress.v1\",\"seed_name\":\"" << json_escape(seed)
                << "\",\"slot\":" << slot
                << ",\"source\":\"native_game_flag_write\",\"completed\":[";
            bool first = true;
            for (const auto& id : completed)
            {
                if (!first)
                {
                    out << ',';
                }
                first = false;
                out << '"' << json_escape(id) << '"';
            }
            out << "],\"flag_events\":" << events << ",\"recovery_scans\":" << recovery_scans
                << ",\"flag_reads\":" << flag_reads << "}\n";
            const auto text = out.str();
            if (!write_text_file_atomic_verified(bridge / progress_file, text))
            {
                throw std::runtime_error("event_checkpoint_write_failed");
            }
            dirty = false;
            if (!write_text_file_atomic_verified(bridge / state_file, text))
            {
                throw std::runtime_error("event_publish_failed");
            }
        }
        void record(const std::wstring& id, const wchar_t* source)
        {
            if (completed.contains(id) || !flag_value(flags.at(id)))
            {
                return;
            }
            completed.insert(id);
            dirty = true;
            RC::Output::send<RC::LogLevel::Warning>(
                L"[KH3APProbeCpp] NativeEventTracking completed={} source={}\n", id, source);
        }
        void process()
        {
            const auto current = pipe::input_generation();
            const bool input = current != generation;
            generation = current;
            const bool notification = inbox.pending() || integer_inbox.pending();
            if (!input && !notification && !recover && !recovering)
            {
                return;
            }
            try
            {
                if (input || !initialized)
                {
                    if (const auto directory = resolver())
                    {
                        if (!initialized || *directory != bridge)
                        {
                            if (auto text = read_text_file(*directory / config_file))
                            {
                                configure(*directory, *text);
                            }
                        }
                    }
                }
                if (!initialized || failed)
                {
                    recover = false;
                    recovering = false;
                    inbox.take();
                    integer_inbox.take();
                    return;
                }
                if (recover)
                {
                    recover = false;
                    ++recovery_scans;
                    recovering = true;
                    recovery_after.clear();
                }
                if (recovering)
                {
                    // Finite recovery after actual ownership/load events, not an
                    // idle scan. Bound entry work while native flag-write events
                    // continue to report newly completed checks immediately.
                    auto it =
                        recovery_after.empty() ? flags.begin() : flags.upper_bound(recovery_after);
                    for (size_t budget = event_flag_update_budget; it != flags.end() && budget > 0;
                         --budget, ++it)
                    {
                        record(it->first, L"world_ready");
                        recovery_after = it->first;
                    }
                    if (it == flags.end())
                    {
                        recovering = false;
                        recovery_after.clear();
                        dirty = true;
                    }
                }
                auto events = inbox.take();
                const auto integers = integer_inbox.take();
                events.insert(integers.begin(), integers.end());
                for (const auto name : events)
                {
                    for (const auto& id : watched.at(name))
                    {
                        record(id, L"flag_write");
                    }
                }
                if (dirty || !events.empty())
                {
                    persist();
                }
            }
            catch (const std::exception& error)
            {
                failed = true;
                recover = false;
                recovering = false;
                // No polling fallback, sleep, backoff, or retry timer. A new config
                // or explicit world-ready event is required to resume this service.
                RC::Output::send<RC::LogLevel::Error>(
                    L"[KH3APProbeCpp] NativeEventTracking stopped reason={}\n",
                    std::wstring(error.what(), error.what() + std::strlen(error.what())));
            }
        }
    };
    RuntimeEventTrackingService::RuntimeEventTrackingService(BridgeResolver resolver)
        : impl_(std::make_unique<Impl>(std::move(resolver)))
    {
    }
    RuntimeEventTrackingService::~RuntimeEventTrackingService() = default;
    void RuntimeEventTrackingService::process_events()
    {
        impl_->process();
    }
    void RuntimeEventTrackingService::world_ready()
    {
        impl_->ready = true;
        impl_->failed = false;
        impl_->recover = true;
    }
    void RuntimeEventTrackingService::reset()
    {
        impl_->ready = false;
        impl_->recover = false;
        impl_->recovering = false;
        impl_->recovery_after.clear();
    }
} // namespace kh3ap
