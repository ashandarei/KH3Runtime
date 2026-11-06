#include "kh3ap/features/colors/color_apply_state.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <Unreal/UObject.hpp>

#include <algorithm>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        auto contains(const std::vector<FWeakObjectPtr>& objects, UObject* object) -> bool
        {
            return object && std::any_of(objects.begin(), objects.end(),
                    [object](const FWeakObjectPtr& weak) { return weak.Get() == object; });
        }

        auto live_objects(const std::vector<FWeakObjectPtr>& objects) -> std::vector<UObject*>
        {
            std::vector<UObject*> result;
            for (const auto& weak : objects)
            {
                if (UObject* object = weak.Get(); is_uobject_valid_fast(object))
                {
                    result.push_back(object);
                }
            }
            return result;
        }
    }

    auto RuntimeColorApplyState::reset_for_map_jump() -> void
    {
        colored_pawns_.clear();
        colored_weapons_.clear();
        pending_.clear();
    }

    auto RuntimeColorApplyState::prune_expired() -> void
    {
        const auto expired = [](const FWeakObjectPtr& weak) {
            return !is_uobject_valid_fast(weak.Get());
        };
        std::erase_if(colored_pawns_, expired);
        std::erase_if(colored_weapons_, expired);
        std::erase_if(pending_, [&](const RuntimeColorPendingPawn& entry) {
            return expired(entry.pawn);
        });
    }

    auto RuntimeColorApplyState::is_colored(UObject* pawn) const -> bool
    {
        return contains(colored_pawns_, pawn);
    }

    auto RuntimeColorApplyState::colored() const -> std::vector<UObject*>
    {
        return live_objects(colored_pawns_);
    }

    auto RuntimeColorApplyState::mark_colored(UObject* pawn) -> void
    {
        if (is_uobject_valid_fast(pawn) && !is_colored(pawn))
        {
            FWeakObjectPtr weak{};
            weak = pawn;
            colored_pawns_.push_back(weak);
        }
    }

    auto RuntimeColorApplyState::is_weapon_colored(UObject* weapon) const -> bool
    {
        return contains(colored_weapons_, weapon);
    }

    auto RuntimeColorApplyState::colored_weapons() const -> std::vector<UObject*>
    {
        return live_objects(colored_weapons_);
    }

    auto RuntimeColorApplyState::mark_weapon_colored(UObject* weapon) -> void
    {
        if (is_uobject_valid_fast(weapon) && !is_weapon_colored(weapon))
        {
            FWeakObjectPtr weak{};
            weak = weapon;
            colored_weapons_.push_back(weak);
        }
    }

    auto RuntimeColorApplyState::is_pending(UObject* pawn) const -> bool
    {
        return pawn && std::any_of(pending_.begin(), pending_.end(),
                [pawn](const RuntimeColorPendingPawn& entry) { return entry.pawn.Get() == pawn; });
    }

    auto RuntimeColorApplyState::queue_pending(
            UObject* pawn, std::wstring_view character_key,
            std::chrono::steady_clock::time_point now) -> void
    {
        if (!is_uobject_valid_fast(pawn) || is_pending(pawn)) return;
        RuntimeColorPendingPawn entry{};
        entry.pawn = pawn;
        entry.character_key = character_key;
        entry.queued_at = now;
        pending_.push_back(std::move(entry));
    }

    auto RuntimeColorApplyState::pending() -> std::vector<RuntimeColorPendingPawn>&
    {
        return pending_;
    }
}
