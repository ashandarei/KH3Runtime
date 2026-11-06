#include "kh02_battle_level_observer.h"
#include "kh02_battle_level_policy.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/Hooks.hpp>
#include <exception>

namespace kh3ap
{
    using namespace RC::Unreal;

    struct Kh02BattleLevelObserver::Impl
    {
        inline static Impl* instance{};
        inline static bool listener_registered{};
        Callbacks callbacks;
        bool active{};
        bool prepared{};
        const FName battle_level_event_name{L"BP_TresSetBattleLevel", FNAME_Add};

        explicit Impl(Callbacks handlers) : callbacks(handlers)
        {
            if (!listener_registered)
            {
                // UE4SS owns the registration for the process lifetime. Only the
                // currently live observer may receive its before-source callback.
                Hook::RegisterProcessEventPreCallback(
                    [](UObject*, UFunction* function, void* parameters)
                    {
                        if (auto* observer = instance)
                        {
                            observer->observe(function, parameters);
                        }
                    });
                listener_registered = true;
            }
            instance = this;
        }

        void observe(UFunction* function, void* parameters)
        {
            if (!active || !prepared || !is_uobject_valid_fast(function) ||
                function->GetNamePrivate() != battle_level_event_name ||
                function->GetName() != L"BP_TresSetBattleLevel")
            {
                return;
            }
            auto* owner = function->GetOuterPrivate();
            if (!is_uobject_valid_fast(owner) || owner->GetPathName() != L"/Script/TresGame.TresGameBlueprintLibrary")
            {
                return;
            }
            if (kh02_world_kind(kh02_current_world()) != Kh02WorldKind::story)
            {
                return;
            }
            try
            {
                const auto next =
                    kh02_observed_battle_level(read_function_int_param(function, parameters, L"InBattleLevel"));
                if (next)
                {
                    callbacks.apply(callbacks.context, *next);
                }
            }
            catch (const std::exception& error)
            {
                callbacks.failure(callbacks.context, error.what());
            }
        }

        void stop()
        {
            if (instance == this)
            {
                instance = nullptr;
            }
        }
    };

    Kh02BattleLevelObserver::Kh02BattleLevelObserver(Callbacks callbacks) : impl(std::make_unique<Impl>(callbacks)) {}

    Kh02BattleLevelObserver::~Kh02BattleLevelObserver()
    {
        stop();
    }

    void Kh02BattleLevelObserver::set_active(bool active)
    {
        impl->active = active;
    }

    void Kh02BattleLevelObserver::mark_ready()
    {
        impl->prepared = true;
    }

    auto Kh02BattleLevelObserver::ready() const -> bool
    {
        return impl->prepared;
    }

    void Kh02BattleLevelObserver::stop()
    {
        impl->stop();
    }
} // namespace kh3ap
