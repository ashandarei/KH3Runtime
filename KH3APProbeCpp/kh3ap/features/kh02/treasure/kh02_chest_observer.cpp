#include "kh02_chest_observer.h"
#include "kh02_chest_persistence.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/features/kh02/story/kh02_story_flags.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/NameTypes.hpp>
#include <stdexcept>
#include <utility>
#include <algorithm>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t pending_chest_limit = 128;
        constexpr std::size_t native_name_bytes = sizeof(FName);
        static_assert(native_name_bytes == sizeof(uint64_t));

    } // namespace

    void Kh02ChestObserver::enqueue(UObject* chest)
    {
        if (!is_uobject_valid_fast(chest) || !chest->GetPathName().starts_with(L"/Game/Zevels/dw/"))
        {
            return;
        }
        auto* type = find_loaded_runtime_asset_class(L"/Script/TresGame.TresTreasureBox");
        if (!is_uobject_a_fast(chest, type))
        {
            return;
        }
        if (is_fake_chest(chest))
        {
            return;
        }
        std::erase_if(watched_chests, [](const auto& weak) { return !is_uobject_valid_fast(weak.Get()); });
        const bool watched = std::any_of(watched_chests.begin(), watched_chests.end(),
                                         [chest](const auto& weak) { return weak.Get() == chest; });
        if (!watched)
        {
            if (watched_chests.size() >= pending_chest_limit)
            {
                throw std::runtime_error("kh02_challenge_chest_watch_limit");
            }
            watched_chests.push_back(make_uobject_weak(chest));
        }
        ++notifications;
        std::erase_if(pending_chests, [](const auto& weak) { return !is_uobject_valid_fast(weak.Get()); });
        for (const auto& weak : pending_chests)
        {
            if (weak.Get() == chest)
            {
                return;
            }
        }
        if (pending_chests.size() >= pending_chest_limit)
        {
            throw std::runtime_error("kh02_challenge_chest_event_limit");
        }
        pending_chests.push_back(make_uobject_weak(chest));
    }

    void Kh02ChestObserver::reconcile(UObject* world, Kh02StoryFlags& source_flags)
    {
        // BeginPlay and the native journal setter enqueue weak identities.
        // Drain after their VM/animation call stacks have returned, including
        // chests that streamed in before challenge tracking became ready.
        auto pending = std::move(pending_chests);
        pending_chests.clear();
        for (const auto& weak : pending)
        {
            auto* chest = weak.Get();
            if (!is_uobject_valid_fast(chest) || kh02_actor_world(chest) != world ||
                is_fake_chest(chest))
            {
                continue;
            }
            try
            {
                ++reconciliations;
                auto* id = chest->GetPropertyByNameInChain(L"m_TreasureID");
                if (!id || id->GetClass().GetName() != L"NameProperty" || id->GetElementSize() != native_name_bytes)
                {
                    throw std::runtime_error("kh02_challenge_chest_field");
                }
                const auto treasure = id->ContainerPtrToValuePtr<FName>(chest)->ToString();
                const auto binding = reconciliation::view();
                if (kh02_chest_collected(treasure,
                                        source_flags.value(L"gameflow_DW_CHALLENGE_CHEST_" + treasure),
                                        binding->completed_checks))
                {
                    // Restore completion before the getter chooses presentation.
                    // This never invokes the reward or the chest's Check action.
                    record_native_chest(treasure);
                }
                if (!reconcile_chest_record(chest, false))
                {
                    continue;
                }
                source_flags.record_chest(treasure);
                if (restore_recorded_chest_appearance(chest))
                {
                    ++restored_appearances;
                }
            }
            catch (const std::exception& error)
            {
                last_error = error.what();
            }
        }
    }

    auto Kh02ChestObserver::snapshot() const -> Json
    {
        return {{"notifications", notifications},
                {"reconciliations", reconciliations},
                {"restored_appearances", restored_appearances},
                {"pending", pending_chests.size()}};
    }

    void Kh02ChestObserver::notify(UObject* chest) noexcept
    {
        try
        {
            enqueue(chest);
        }
        catch (const std::exception& error)
        {
            last_error = error.what();
        }
    }

    auto Kh02ChestObserver::pending() const -> bool
    {
        return !pending_chests.empty();
    }

    void Kh02ChestObserver::mapset_finished(UObject* world) noexcept
    {
        // A flip reuses existing treasure actors. Revisit only identities
        // captured by their creation/show events; no actor discovery or timer.
        try
        {
            const auto watched = watched_chests;
            for (const auto& weak : watched)
            {
                auto* chest = weak.Get();
                if (is_uobject_valid_fast(chest) && kh02_actor_world(chest) == world)
                {
                    notify(chest);
                }
            }
        }
        catch (const std::exception& error)
        {
            last_error = error.what();
        }
    }

    auto Kh02ChestObserver::error() const -> const std::string&
    {
        return last_error;
    }

    void Kh02ChestObserver::clear_world()
    {
        pending_chests.clear();
        watched_chests.clear();
    }
} // namespace kh3ap
