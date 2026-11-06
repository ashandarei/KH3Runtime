#include "kh02_load_completion.h"
#include "kh02_bootstrap.h"
#include "kh02_bootstrap_state.h"
#include "kh3ap/features/world/loading_runtime.h"
#include "kh3ap/features/world/save_destination.h"
#include "kh02_world_context.h"
#include "kh3ap/features/kh02/transport/kh02_asset_companion.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <polyhook2/Detour/x64Detour.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <nlohmann/json.hpp>
#include <array>
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <stdexcept>
#include <utility>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr size_t manager_load_state_offset = 0x69d0;

        class NativeLoadDetour : public PLH::x64Detour
        {
          public:
            using PLH::x64Detour::x64Detour;

            auto stolen_size() const -> size_t
            {
                return m_hookSize;
            }

            auto trampoline_size() const -> size_t
            {
                return m_trampolineSz;
            }
        };

    } // namespace

    struct Kh02Bootstrap::Impl
    {
        std::function<bool(UObject*)> prepare;
        using Load = void(__fastcall*)(UObject*, int32_t, void*, bool, bool, bool);
        inline static Impl* owner{};
        std::unique_ptr<NativeLoadDetour> detour;
        uint64_t trampoline{};
        uintptr_t base{};
        bool enabled{};
        bool provider_ready{};
        bool aliases_ready{};
        bool requires_restart{};
        Kh02BootstrapState policy;
        FWeakObjectPtr manager;
        FWeakObjectPtr context;
        FWeakObjectPtr world;
        Kh02LoadCompletion completion;
        kh02control::Identity prepared_identity;
        int32_t slot{};
        bool first{}, second{}, third{};
        DWORD request_thread{};
        std::string last_error;
        std::string progress_wait_reason;
        uint64_t intercepted{}, dispatched{}, cancelled{};

        ~Impl()
        {
            completion.clear();
            if (detour)
            {
                detour->unHook();
                loading_runtime::clear_owned_load_detour();
            }
            if (owner == this)
            {
                owner = nullptr;
            }
        }

        void fail(const std::string& reason)
        {
            requires_restart = requires_restart || reason == "kh02_provider_restart_required";
            last_error = reason;
            loading_runtime::report_native_load_gate_failure(policy.generation(), reason);
            policy.fail();
            provider_ready = false;
            aliases_ready = false;
            if (is_uobject_valid_fast(manager.Get()) && is_uobject_valid_fast(context.Get()) &&
                is_uobject_valid_fast(world.Get()) && world.Get() == kh02_current_world() &&
                request_thread == GetCurrentThreadId() &&
                Kh02LoadCompletion::resolve_owner(base, completion.native_data(), manager.Get()) == context.Get())
            {
                completion.fail_without_load(slot);
                ++cancelled;
            }
            completion.clear();
            manager = {};
            context = {};
            world = {};
            RC::Output::send<RC::LogLevel::Warning>(STR("[KH3APProbeCpp] KH02 native load gate cancelled reason={}\n"),
                                                    std::wstring(reason.begin(), reason.end()));
        }

        static void on_load(UObject* request_manager, int32_t requested_slot, void* callback, bool option_one,
                            bool option_two, bool option_three)
        {
            auto* self = owner;
            if (!self || !self->trampoline)
            {
                return;
            }
            if (self->policy.phase() == Kh02BootstrapState::Phase::waiting)
            {
                // The native menu has one outstanding operation. A duplicate
                // selection must not replace its retained completion delegate.
                return;
            }
            uintptr_t entries{};
            int32_t count{};
            const bool catalog_ready =
                loading_runtime::read_native(self->base + loading_runtime::native_rva(loading_runtime::catalog_rva),
                                             entries) &&
                loading_runtime::read_native(
                    self->base + loading_runtime::native_rva(loading_runtime::catalog_count_rva), count);
            const auto destination =
                catalog_ready
                    ? native_save_destination(entries, count, requested_slot, [](uintptr_t address, auto& value)
                                              { return loading_runtime::read_native(address, value); })
                    : std::nullopt;
            if (destination && !kh02_map_package(std::string_view(*destination)))
            {
                // Ordinary saves do not depend on the imported provider, even
                // after a failed 0.2 session has latched its restart requirement.
                reinterpret_cast<Load>(self->trampoline)(request_manager, requested_slot, callback, option_one,
                                                         option_two, option_three);
                return;
            }
            if (!destination || self->requires_restart)
            {
                try
                {
                    if (is_uobject_valid_fast(Kh02LoadCompletion::resolve_owner(self->base, callback, request_manager)))
                    {
                        Kh02LoadCompletion rejected;
                        rejected.copy(self->base, callback);
                        rejected.fail_without_load(requested_slot);
                        ++self->cancelled;
                    }
                    self->fail(destination ? "kh02_bootstrap_restart_required"
                                           : "kh02_bootstrap_save_destination_unavailable");
                }
                catch (const std::exception& error)
                {
                    self->fail(error.what());
                }
                return;
            }
            // Every imported load re-arms its entry session. A previous load's
            // aliases may remain registered after departure while its rooted
            // entry assets and presentation ownership have been released.
            try
            {
                if (!is_uobject_valid_fast(request_manager) ||
                    loading_runtime::read_field<int32_t>(
                        request_manager, loading_runtime::manager_offset(manager_load_state_offset), -1) != 0)
                {
                    throw std::runtime_error("kh02_bootstrap_load_manager_busy");
                }
                auto* request_context = Kh02LoadCompletion::resolve_owner(self->base, callback, request_manager);
                if (!is_uobject_valid_fast(request_context))
                {
                    throw std::runtime_error("kh02_bootstrap_unrecognized_load_completion");
                }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
                std::wstring context_name;
                static_cast<void>(try_get_uobject_full_name_fast(request_context, context_name));
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] KH02 native load completion accepted owner={} manager=0x{:x}\n"), context_name,
                    reinterpret_cast<uintptr_t>(request_manager));
#endif
                if (!self->last_error.empty())
                {
                    reset_kh02_provider_preparation();
                }
                self->manager = make_uobject_weak(request_manager);
                self->context = make_uobject_weak(request_context);
                self->world = make_uobject_weak(kh02_current_world());
                self->request_thread = GetCurrentThreadId();
                self->slot = requested_slot;
                self->first = option_one;
                self->second = option_two;
                self->third = option_three;
                self->completion.copy(self->base, callback);
                self->provider_ready = false;
                self->aliases_ready = false;
                self->last_error.clear();
                self->progress_wait_reason.clear();
                loading_runtime::clear_native_load_gate_failure();
                self->policy.begin(Kh02BootstrapState::Clock::now());
                ++self->intercepted;
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] KH02 native load gate retained slot={} request={} thread={}\n"),
                    requested_slot, self->policy.generation(), self->request_thread);
            }
            catch (const std::exception& error)
            {
                self->fail(error.what());
            }
        }

        void install()
        {
            enabled = kh02_provider_enabled();
            if (!enabled || detour)
            {
                return;
            }
            base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!loading_runtime::supported_image(base))
            {
                throw std::runtime_error("kh02_bootstrap_unsupported_native_load_profile");
            }
            if (!loading_runtime::native_load_gate_original_matches(base))
            {
                throw std::runtime_error("kh02_bootstrap_unsupported_native_load_prologue");
            }
            if (!Kh02LoadCompletion::supported(base))
            {
                throw std::runtime_error("kh02_bootstrap_unsupported_completion_profile");
            }
            owner = this;
            detour =
                std::make_unique<NativeLoadDetour>(base + loading_runtime::native_rva(loading_runtime::load_slot_rva),
                                                   reinterpret_cast<uint64_t>(&on_load), &trampoline);
            const bool hooked = detour->hook();
            if (!hooked || !trampoline)
            {
                if (hooked)
                {
                    detour->unHook();
                }
                detour.reset();
                owner = nullptr;
                trampoline = 0;
                throw std::runtime_error("kh02_bootstrap_load_gate_install_failed");
            }
            loading_runtime::register_owned_load_detour(base, trampoline, detour->stolen_size(),
                                                        detour->trampoline_size());
            if (!loading_runtime::supported_image(base))
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] KH02 native load gate verification failed {}\n"),
                    loading_runtime::native_load_gate_diagnostic());
                detour->unHook();
                detour.reset();
                owner = nullptr;
                trampoline = 0;
                loading_runtime::clear_owned_load_detour();
                throw std::runtime_error("kh02_bootstrap_load_gate_verification_failed");
            }
            RC::Output::send<RC::LogLevel::Warning>(
                STR("[KH3APProbeCpp] KH02 normal Continue/Load gate installed {}\n"),
                loading_runtime::native_load_gate_diagnostic());
        }

        void tick()
        {
            if (requires_restart)
            {
                return;
            }
            if (policy.phase() != Kh02BootstrapState::Phase::waiting)
            {
                return;
            }
            const bool valid =
                is_uobject_valid_fast(manager.Get()) && is_uobject_valid_fast(context.Get()) &&
                is_uobject_valid_fast(world.Get()) && world.Get() == kh02_current_world() &&
                Kh02LoadCompletion::resolve_owner(base, completion.native_data(), manager.Get()) == context.Get() &&
                loading_runtime::read_field<int32_t>(
                    manager.Get(), loading_runtime::manager_offset(manager_load_state_offset), -1) == 0;
            if (GetCurrentThreadId() != request_thread)
            {
                fail("kh02_bootstrap_replay_thread_mismatch");
                return;
            }
            try
            {
                if (valid)
                {
                    provider_ready = prepare_kh02_provider();
                    if (provider_ready)
                    {
                        try
                        {
                            // UI ownership is retained separately. UWorld is an
                            // explicit Kismet asset-loading context for the
                            // GameOver UI, whose UObject has no actor World.
                            auto* preparation_context =
                                Kh02LoadCompletion::is_game_over_owner(context.Get()) ? world.Get() : context.Get();
                            aliases_ready = prepare(preparation_context);
                        }
                        catch (const std::exception& error)
                        {
                            if (!std::string_view(error.what()).starts_with("kh02_entry_progress_not_ready"))
                            {
                                throw;
                            }
                            aliases_ready = false;
                            if (progress_wait_reason != error.what())
                            {
                                progress_wait_reason = error.what();
                                RC::Output::send<RC::LogLevel::Warning>(
                                    STR("[KH3APProbeCpp] KH02 native load waiting for progress: {}\n"),
                                    std::wstring(progress_wait_reason.begin(), progress_wait_reason.end()));
                            }
                        }
                    }
                }
                const auto decision =
                    policy.poll(Kh02BootstrapState::Clock::now(), valid, provider_ready, aliases_ready);
                if (decision == Kh02BootstrapState::Decision::cancel)
                {
                    fail(valid ? "kh02_bootstrap_readiness_timeout" : "kh02_bootstrap_owner_retired");
                }
                else if (decision == Kh02BootstrapState::Decision::dispatch)
                {
                    prepared_identity = kh02_companion_health().identity;
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] KH02 native load gate ready slot={} request={} source_pid={} "
                            "aliases_ready=true; dispatching native load\n"),
                        slot, policy.generation(), prepared_identity.source_pid);
                    reinterpret_cast<Load>(trampoline)(manager.Get(), slot, completion.native_data(), first, second,
                                                       third);
                    ++dispatched;
                    completion.clear();
                    manager = {};
                    context = {};
                    world = {};
                }
            }
            catch (const std::exception& error)
            {
                fail(error.what());
            }
        }
    };

    Kh02Bootstrap::Kh02Bootstrap(std::function<bool(UObject*)> prepare) : impl(std::make_unique<Impl>())
    {
        impl->prepare = std::move(prepare);
    }

    Kh02Bootstrap::~Kh02Bootstrap() = default;

    void Kh02Bootstrap::install()
    {
        impl->install();
    }

    void Kh02Bootstrap::tick()
    {
        impl->tick();
    }

    void Kh02Bootstrap::require_restart(const std::string& reason)
    {
        impl->requires_restart = true;
        if (impl->policy.phase() == Kh02BootstrapState::Phase::waiting)
        {
            impl->fail(reason);
        }
    }

    auto Kh02Bootstrap::failed() const -> bool
    {
        return impl->policy.phase() == Kh02BootstrapState::Phase::failed;
    }

    auto Kh02Bootstrap::error() const -> const std::string&
    {
        return impl->last_error;
    }

    auto Kh02Bootstrap::inspect() const -> std::string
    {
        return nlohmann::json{{"enabled", impl->enabled},
                              {"installed", impl->trampoline != 0},
                              {"phase", static_cast<int>(impl->policy.phase())},
                              {"requires_restart", impl->requires_restart},
                              {"error", impl->last_error},
                              {"provider_ready", impl->provider_ready},
                              {"aliases_ready", impl->aliases_ready},
                              {"request_generation", impl->policy.generation()},
                              {"intercepted", impl->intercepted},
                              {"dispatched", impl->dispatched},
                              {"cancelled", impl->cancelled}}
            .dump();
    }
} // namespace kh3ap
