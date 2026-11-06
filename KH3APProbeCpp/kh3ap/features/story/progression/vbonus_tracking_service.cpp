#include <memory>
#include <utility>
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include "kh3ap/features/story/progression/game_flag_events.h"
#include "kh3ap/features/story/progression/vbonus_tracking_service.h"
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

        constexpr auto vbonus_flag_update_budget = 16;
        constexpr auto schema = L"kh3ap.vbonus_progress.v1";
        constexpr auto config_file = L"slot_data.json";
        constexpr auto progress_file = L"vbonus_progress.json";
        constexpr auto state_file = channels::runtime_vbonus_checks_file;
        uint64_t raw_name(FName name)
        {
            uint64_t raw;
            static_assert(sizeof(name) == sizeof(raw));
            std::memcpy(&raw, &name, sizeof(raw));
            return raw;
        }
    } // namespace
    struct RuntimeVBonusTrackingService::Impl
    {
        BridgeResolver resolver;
        std::filesystem::path bridge;
        std::wstring seed;
        int64_t slot{};
        uint64_t generation{}, recovery_scans{}, flag_reads{};
        VBonusFlagEvents inbox;
        std::map<uint64_t, std::vector<std::wstring>> watched;
        std::map<std::wstring, FName> flags;
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
        }
        void install()
        {
            subscribe_game_flags(inbox);
        }
        bool flag_value(FName name)
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
                throw std::runtime_error("vbonus_flag_owner");
            }
            auto* fn = owner->GetFunctionByNameInChain(L"GetGameFlagBoolean");
            if (!is_uobject_valid_fast(fn) ||
                fn->GetParmsSize() != flag_boolean_query_parameter_bytes)
            {
                throw std::runtime_error("vbonus_flag_function");
            }
            auto* key = find_function_param_by_name(fn, L"FlagName");
            auto* out = find_function_param_by_name(fn, L"ReturnValue");
            if (!key || key->GetClass().GetName() != L"NameProperty" ||
                key->GetOffset_Internal() != 0 ||
                key->GetElementSize() != sizeof(RC::Unreal::FName) || !out ||
                !out->IsA<FBoolProperty>() || out->GetOffset_Internal() != flag_query_result_offset)
            {
                throw std::runtime_error("vbonus_flag_parameters");
            }
            alignas(uint64_t) std::array<std::byte, flag_query_storage_bytes> params{};
            std::memcpy(params.data(), &name, sizeof(name));
            const RuntimeGrantInternalCallDepth::Scope scope{};
            owner->ProcessEvent(fn, params.data());
            return static_cast<FBoolProperty*>(out)->GetPropertyValueInContainer(params.data());
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
            auto setup = native_catalog::check_setup(text, native_catalog::CheckKind::VBonus);
            auto new_seed = std::move(setup.seed_name);
            auto new_slot = setup.slot;
            std::map<std::wstring, FName> next_flags;
            std::map<uint64_t, std::vector<std::wstring>> next_watched;
            for (const auto& row : setup.rows)
            {
                const auto& id = row.source_id;
                const auto& flag = row.flag_name;
                FName name(flag, FNAME_Add);
                next_flags.emplace(id, name);
                next_watched[raw_name(name)].push_back(id);
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
            std::set<uint64_t> names;
            for (const auto& [name, ids] : watched)
            {
                names.insert(name);
            }
            inbox.configure(std::move(names));
            config_text = text;
            initialized = true;
            failed = false;
            dirty = true;
            recover = ready;
            recovering = false;
            recovery_after.clear();
            persist();
            RC::Output::send<RC::LogLevel::Warning>(
                L"[KH3APProbeCpp] NativeVBonusTracking configured checks={} completed={} "
                L"mode=events\n",
                flags.size(), completed.size());
        }
        void persist()
        {
            const auto events = inbox.events();
            std::ostringstream out;
            out << "{\"schema\":\"kh3ap.vbonus_progress.v1\",\"seed_name\":\"" << json_escape(seed)
                << "\",\"slot\":" << slot
                << ",\"source\":\"native_boolean_flag_write\",\"completed\":[";
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
                throw std::runtime_error("vbonus_checkpoint_write_failed");
            }
            dirty = false;
            if (!write_text_file_atomic_verified(bridge / state_file, text))
            {
                throw std::runtime_error("vbonus_publish_failed");
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
                L"[KH3APProbeCpp] NativeVBonusTracking completed={} source={}\n", id, source);
        }
        void process()
        {
            const auto current = pipe::input_generation();
            const bool input = current != generation;
            generation = current;
            const bool notification = inbox.pending();
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
                    for (size_t budget = vbonus_flag_update_budget; it != flags.end() && budget > 0;
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
                const auto events = inbox.take();
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
                    L"[KH3APProbeCpp] NativeVBonusTracking stopped reason={}\n",
                    std::wstring(error.what(), error.what() + std::strlen(error.what())));
            }
        }
    };
    RuntimeVBonusTrackingService::RuntimeVBonusTrackingService(BridgeResolver resolver)
        : impl_(std::make_unique<Impl>(std::move(resolver)))
    {
    }
    RuntimeVBonusTrackingService::~RuntimeVBonusTrackingService() = default;
    void RuntimeVBonusTrackingService::process_events()
    {
        impl_->process();
    }
    void RuntimeVBonusTrackingService::world_ready()
    {
        impl_->ready = true;
        impl_->failed = false;
        impl_->recover = true;
    }
    void RuntimeVBonusTrackingService::reset()
    {
        impl_->ready = false;
        impl_->recover = false;
        impl_->recovering = false;
        impl_->recovery_after.clear();
    }
} // namespace kh3ap
