#include "kh3ap/features/treasure/treasure_class_cache.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>

#include <format>

namespace kh3ap
{
    using namespace RC::Unreal;

    auto TreasureClassCache::refresh(
            std::wstring_view player_state_class_path,
            std::wstring_view char_state_class_path,
            std::wstring_view treasure_logic_actor_class_path)
            -> std::optional<std::wstring>
    {
        if (!open_treasure_player_state_class_)
        {
            open_treasure_player_state_class_ =
                    UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, std::wstring{player_state_class_path});
        }
        if (!open_treasure_char_state_class_)
        {
            open_treasure_char_state_class_ =
                    UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, std::wstring{char_state_class_path});
        }
        if (!actor_base_class_)
        {
            actor_base_class_ = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/Engine.Actor");
        }
        if (!treasure_logic_actor_class_)
        {
            treasure_logic_actor_class_ =
                    UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, std::wstring{treasure_logic_actor_class_path});
        }

        const std::wstring resolution = std::format(
                L"player={} | char={} | treasure_logic={}",
                open_treasure_player_state_class_ ? open_treasure_player_state_class_->GetFullName() : std::wstring{L"<unresolved>"},
                open_treasure_char_state_class_ ? open_treasure_char_state_class_->GetFullName() : std::wstring{L"<unresolved>"},
                treasure_logic_actor_class_ ? treasure_logic_actor_class_->GetFullName() : std::wstring{L"<unresolved>"});
        if (resolution == last_resolution_)
        {
            return std::nullopt;
        }

        last_resolution_ = resolution;
        return resolution;
    }

    auto TreasureClassCache::open_treasure_player_state_class() const -> UClass*
    {
        return open_treasure_player_state_class_;
    }

    auto TreasureClassCache::open_treasure_char_state_class() const -> UClass*
    {
        return open_treasure_char_state_class_;
    }

    auto TreasureClassCache::treasure_logic_actor_class() const -> UClass*
    {
        return treasure_logic_actor_class_;
    }

    auto TreasureClassCache::actor_base_class() const -> UClass*
    {
        return actor_base_class_;
    }

    auto refresh_open_treasure_state_class_cache(
            TreasureClassCache& class_cache,
            std::wstring_view open_treasure_player_state_class_path,
            std::wstring_view open_treasure_char_state_class_path,
            std::wstring_view treasure_logic_actor_class_path) -> void
    {
        const auto resolution = class_cache.refresh(
                open_treasure_player_state_class_path,
                open_treasure_char_state_class_path,
                treasure_logic_actor_class_path);
        if (resolution.has_value())
        {
            RC::Output::send<RC::LogLevel::Verbose>(STR("[KH3APProbeCpp] OpenTreasure class lookup {}\n"), *resolution);
        }
    }

    auto maybe_log_interesting_treasure_construct(
            const RC::Unreal::FStaticConstructObjectParameters&,
            RC::Unreal::UObject*) -> void
    {
        return;
    }
}
