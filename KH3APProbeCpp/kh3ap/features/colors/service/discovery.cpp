#include "kh3ap/app/config.h"
#include "kh3ap/features/colors/service/service_impl.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/colors/service/character.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/colors/color_object_registry.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr auto color_pawn_readiness_retry = std::chrono::milliseconds(100);
    }

    using namespace RC;
    using namespace RC::Unreal;
    using namespace color_service_detail;

    namespace
    {
        using ::kh3ap::settings::s_debug_logging;
    }

    [[nodiscard]] auto
    RuntimeColorService::Impl::ready_to_attempt(UObject* pawn,
                                                std::chrono::steady_clock::time_point now) -> bool
    {
        for (auto& [tracked_pawn, attempted_at] : last_attempt_at_)
        {
            if (tracked_pawn.Get() != pawn)
            {
                continue;
            }
            if (now - attempted_at < apply_retry_interval)
            {
                return false;
            }
            attempted_at = now;
            return true;
        }
        FWeakObjectPtr weak{};
        weak = pawn;
        last_attempt_at_.emplace_back(weak, now);
        return true;
    }

    [[nodiscard]] auto RuntimeColorService::Impl::take_mesh_dump_slot(UObject* pawn) -> bool
    {
        if (std::any_of(mesh_dump_logged_.begin(), mesh_dump_logged_.end(),
                        [pawn](const FWeakObjectPtr& weak)
                        {
                            return weak.Get() == pawn;
                        }))
        {
            return false;
        }
        FWeakObjectPtr weak{};
        weak = pawn;
        mesh_dump_logged_.push_back(weak);
        return true;
    }

    [[nodiscard]] auto
    RuntimeColorService::Impl::world_is_allowed(UObject* pawn,
                                                std::chrono::steady_clock::time_point now) -> bool
    {
        std::wstring full_name{};
        if (!pawn || !try_get_uobject_full_name_fast(pawn, full_name))
        {
            return false;
        }
        if (!runtime_color_map_allowed(full_name))
        {
            last_world_name_.clear();
            return false;
        }
        const size_t colon = full_name.find(STR(":PersistentLevel"));
        const size_t dot =
            colon == std::wstring::npos ? std::wstring::npos : full_name.rfind(STR('.'), colon);
        const std::wstring world_name = (colon != std::wstring::npos && dot != std::wstring::npos)
                                            ? full_name.substr(dot + 1, colon - dot - 1)
                                            : full_name;
        if (world_name != last_world_name_)
        {
            last_world_name_ = world_name;
            world_first_seen_at_ = now;
        }
        return true;
    }

    auto RuntimeColorService::Impl::note_unsupported_pawn(UObject* pawn) -> void
    {
        if (!s_debug_logging || !pawn || !is_uobject_valid_fast(pawn))
        {
            return;
        }
        UClass* const klass = pawn->GetClassPrivate();
        if (!klass || !is_uobject_valid_fast(klass))
        {
            return;
        }
        std::wstring class_name = klass->GetName();
        if (std::find(unsupported_logged_.begin(), unsupported_logged_.end(), class_name) !=
            unsupported_logged_.end())
        {
            return;
        }
        unsupported_logged_.push_back(class_name);
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Color pawn not supported class={} pawn={}\n"), class_name,
            format_object_debug_name(pawn));
    }

    auto RuntimeColorService::Impl::maybe_queue(UObject* pawn,
                                                std::chrono::steady_clock::time_point now) -> void
    {
        if (state.is_colored(pawn) || state.is_pending(pawn))
        {
            return;
        }
        const std::optional<std::wstring> character_key = resolve_supported_character_key(pawn);
        if (!character_key)
        {
            note_unsupported_pawn(pawn);
            return;
        }
        std::wstring full_name;
        if (!try_get_uobject_full_name_fast(pawn, full_name) ||
            !runtime_color_map_allowed(full_name))
        {
            return;
        }
        // Record first sightings for timing without delaying application.
        (void)world_is_allowed(pawn, now);
        state.queue_pending(pawn, *character_key, now);
        Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Color pawn queued key={}\n"),
                                       *character_key);
    }

    [[nodiscard]] auto RuntimeColorService::Impl::resolve_persistent_level(UObject* reference_pawn)
        -> UObject*
    {
        if (!reference_pawn || !is_uobject_valid_fast(reference_pawn))
        {
            return nullptr;
        }
        UObject* const outer = reference_pawn->GetOuterPrivate();
        if (!outer || !is_uobject_valid_fast(outer) || outer->GetName() != STR("PersistentLevel"))
        {
            return nullptr;
        }
        return outer;
    }

    auto RuntimeColorService::Impl::discover_pawns(UObject* reference_pawn,
                                                   std::chrono::steady_clock::time_point now)
        -> void
    {
        const auto generation = color_objects::generation();
        if (generation == pawn_generation_ && (!pawn_discovery_pending_ || now < pawn_retry_at_))
        {
            return;
        }
        UObject* const level = resolve_persistent_level(reference_pawn);
        if (!level)
        {
            return;
        }
        const RuntimeHotPathProfileScope profile{L"colors:pawn_discovery"};
        const auto snapshot = color_objects::snapshot();
        pawn_generation_ = snapshot.generation;
        pawn_discovery_pending_ = snapshot.unfinished;
        pawn_retry_at_ = now + color_pawn_readiness_retry;
        for (UObject* const pawn : snapshot.pawns)
        {
            if (resolve_persistent_level(pawn) == level)
            {
                maybe_queue(pawn, now);
            }
        }
        weapons_dirty_ = true;
    }
} // namespace kh3ap
