#pragma once
#include "kh3ap/core/objects/uobject_weak.h"
#include <Unreal/NameTypes.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
    class UClass;
    class FProperty;
} // namespace RC::Unreal
namespace kh3ap::bad_status_detail
{
    using RC::Unreal::UObject;
    inline constexpr uint8_t sneeze_bad_status_kind = 13;

    inline constexpr bool s_enable_direct_player_bad_status_diagnostics = false;
    struct DirectPlayerBadStatusSpec
    {
        std::wstring_view name{};
        std::wstring_view runtime_token{};
        std::wstring_view short_token{};
        uint8_t bad_status_kind{};
        uint8_t damage_attribute{};
        uint8_t state_id{};
        std::wstring_view state_class_name{};
        std::wstring_view state_class_path{};
        std::wstring_view particle_template_path_fragment{};
        std::wstring_view particle_template_name{};
    };

    inline constexpr std::array<DirectPlayerBadStatusSpec, 4> s_direct_player_bad_status_specs{{
        {
            L"Stun",
            L"ETresBadStatusType::TRES_BADSTAT_STUN",
            L"TRES_BADSTAT_STUN",
            12,
            3,
            122,
            L"TresPlayerState_BadStatStun",
            L"/Script/TresGame.TresPlayerState_BadStatStun",
            L"/Game/Effects/system/abnormal/",
            {},
        },
        {
            L"Freeze",
            L"ETresBadStatusType::TRES_BADSTAT_FREEZE",
            L"TRES_BADSTAT_FREEZE",
            7,
            2,
            123,
            L"TresPlayerState_BadStatFreeze",
            L"/Script/TresGame.TresPlayerState_BadStatFreeze",
            {},
            {},
        },
        {
            L"Burn",
            L"ETresBadStatusType::TRES_BADSTAT_BURN",
            L"TRES_BADSTAT_BURN",
            17,
            1,
            124,
            L"TresPlayerState_BadStatBurn",
            L"/Script/TresGame.TresPlayerState_BadStatBurn",
            L"/Game/Effects/system/abnormal/burn/",
            L"vs_abn_brn0",
        },
        {
            L"Sneeze",
            L"ETresBadStatusType::TRES_BADSTAT_SNEEZE",
            L"TRES_BADSTAT_SNEEZE",
            sneeze_bad_status_kind,
            0,
            151,
            {},
            {},
            {},
            {},
        },
    }};

    struct PlayerBadStatusStateAsset
    {
        RC::Unreal::UClass* state_class{};
        RC::Unreal::FName state_name{};
    };

    struct LiveBadStatusContext
    {
        UObject* state_component{};
        UObject* current_state{};
    };

    auto find_direct_player_bad_status_spec(std::wstring_view runtime_token)
        -> const DirectPlayerBadStatusSpec*;
    auto try_read_object_pointer_in_chain(UObject* owner, const wchar_t* property_name) -> UObject*;

    class BadStatusStateDiscovery
    {
    public:
        BadStatusStateDiscovery();
        ~BadStatusStateDiscovery();
        auto prewarm_context(UObject* pawn, UObject* state_component, UObject* current_state)
            -> LiveBadStatusContext;
        auto find_asset(UObject* state_component, const DirectPlayerBadStatusSpec& spec,
                        PlayerBadStatusStateAsset* asset, std::wstring* failure) -> bool;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    class BadStatusStateQueue
    {
    public:
        BadStatusStateQueue();
        ~BadStatusStateQueue();
        auto property(UObject* state_component) -> RC::Unreal::FProperty*;
        auto prewarm_fields(UObject* current_state) -> void;
        auto write_fields(UObject* state, UObject* pawn, const DirectPlayerBadStatusSpec& spec,
                          const PlayerBadStatusStateAsset& asset, std::wstring* failure) -> bool;
        auto prepend(UObject* state_component, UObject* state, int* count_before, int* count_after,
                     std::wstring* failure) -> bool;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    class BadStatusParticles
    {
    public:
        BadStatusParticles();
        ~BadStatusParticles();
        auto prewarm() -> void;
        auto clean(UObject* pawn, const DirectPlayerBadStatusSpec& spec, UObject* effect_state)
            -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    struct PendingDirectPlayerBadStatus
    {
        RC::Unreal::FWeakObjectPtr sora_pawn;
        RC::Unreal::FWeakObjectPtr state_component;
        RC::Unreal::FWeakObjectPtr bad_status_state;
        const DirectPlayerBadStatusSpec* spec{};
        bool observed_active{};

        // Acquire serial-backed identities before mutating the live state queue.
        auto arm(UObject* pawn, UObject* component, UObject* state,
                 const DirectPlayerBadStatusSpec& effect) -> bool;
    };
} // namespace kh3ap::bad_status_detail
