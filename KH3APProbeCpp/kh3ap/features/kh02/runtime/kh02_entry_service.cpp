#include "kh02_entry_service.h"
#include "kh02_bootstrap.h"
#include "kh3ap/features/world/portals/portal_service.h"
#include "kh3ap/features/kh02/world/kh02_portal.h"
#include "kh3ap/features/kh02/transport/kh02_asset_companion.h"
#include <stdexcept>
#include <utility>

namespace kh3ap
{
    struct Kh02EntryService::Impl
    {
        inline static Impl* owner{};
        enum class Source
        {
            none,
            portal,
            bootstrap,
            external
        };
        Source preparation_source{Source::none};
        Prepare prepare;
        std::function<bool()> assets_notified;
        std::function<void()> prepare_asset_access;
        Kh02Bootstrap bootstrap{[this](RC::Unreal::UObject* context)
                                { return run_prepare(context, Source::bootstrap); }};
        Kh02Portal portal{[this](RC::Unreal::UObject* context) { return run_prepare(context, Source::portal); },
                          [this] { return assets_notified && assets_notified(); }};

        bool run_prepare(RC::Unreal::UObject* context, Source source)
        {
            preparation_source = source;
            return prepare(context);
        }

        explicit Impl(Prepare callback, std::function<bool()> notification, std::function<void()> asset_access)
            : prepare(std::move(callback)), assets_notified(std::move(notification)),
              prepare_asset_access(std::move(asset_access))
        {
            owner = this;
        }

        ~Impl()
        {
            if (owner == this)
            {
                owner = nullptr;
            }
        }
    };

    Kh02EntryService::Kh02EntryService(Prepare prepare, std::function<bool()> assets_notified,
                                     std::function<void()> prepare_asset_access)
        : impl(std::make_unique<Impl>(std::move(prepare), std::move(assets_notified),
                                     std::move(prepare_asset_access)))
    {
    }

    Kh02EntryService::~Kh02EntryService() = default;

    auto Kh02EntryService::prepare_journal_assets() -> bool
    {
        if (!RuntimePortalService::kh02_access().unlocked)
        {
            throw std::runtime_error("kh02_journal_locked");
        }
        if (!Impl::owner || !Impl::owner->prepare_asset_access)
        {
            throw std::runtime_error("kh02_journal_asset_service_unavailable");
        }
        if (!prepare_kh02_provider())
        {
            return false;
        }
        // Register source descriptors without arming travel, loading Aqua,
        // changing the story bank or preparing gameplay presentation.
        Impl::owner->prepare_asset_access();
        return true;
    }

    auto Kh02EntryService::prepare_startup(RC::Unreal::UObject* context) -> bool
    {
        if (!Impl::owner)
        {
            throw std::runtime_error("kh02_startup_entry_service_unavailable");
        }
        if (!kh02_provider_enabled())
        {
            return true;
        }
        if (!prepare_kh02_provider())
        {
            return false;
        }
        return Impl::owner->run_prepare(context, Impl::Source::external);
    }

    void Kh02EntryService::initialize()
    {
        impl->bootstrap.install();
    }

    void Kh02EntryService::tick_bootstrap()
    {
        impl->bootstrap.tick();
    }

    void Kh02EntryService::tick_portal(RC::Unreal::UObject* pawn, bool playable)
    {
        impl->portal.tick(pawn, playable);
    }

    void Kh02EntryService::leave_portal()
    {
        impl->portal.cancel();
    }

    auto Kh02EntryService::failed_preparation() const -> bool
    {
        return (impl->preparation_source == Impl::Source::portal && impl->portal.failed_preparation()) ||
               (impl->preparation_source == Impl::Source::bootstrap && impl->bootstrap.failed());
    }

    auto Kh02EntryService::portal_work_pending() const -> bool
    {
        return impl->portal.work_pending();
    }

    void Kh02EntryService::require_restart(const std::string& reason)
    {
        impl->bootstrap.require_restart(reason);
    }

    auto Kh02EntryService::bootstrap_snapshot() const -> std::string
    {
        return impl->bootstrap.inspect();
    }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
    auto Kh02EntryService::portal_command(RC::Unreal::UObject* pawn, std::wstring_view action) -> std::string
    {
        if (action == L"portal_interact")
        {
            impl->portal.interact(pawn);
        }
        else if (action == L"portal_button")
        {
            impl->portal.press(pawn);
        }
        else if (action != L"portal_inspect")
        {
            throw std::runtime_error("kh02_portal_unknown_command");
        }
        return impl->portal.inspect();
    }
#endif
} // namespace kh3ap
