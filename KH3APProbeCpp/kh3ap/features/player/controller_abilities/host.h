#pragma once
#include "kh3ap/core/types.h"
#include "kh3ap/features/player/controller_abilities/models.h"
#include "kh3ap/features/player/controller_abilities/controller_ability_refresh_state.h"
#include "kh3ap/core/performance_build.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/NameTypes.hpp>
#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace RC::Unreal
{
    struct FFrame;
}
namespace kh3ap
{
    struct RuntimeControllerAbilityService::Impl
    {
        using FName = RC::Unreal::FName;
        using FWeakObjectPtr = RC::Unreal::FWeakObjectPtr;
        using FFrame = RC::Unreal::FFrame;
        using UObject = RC::Unreal::UObject;
        explicit Impl(RuntimeControllerAbilityConfig config);
        ~Impl();
        struct EnumNamePairRaw
        {
            FName name{};
            int64_t value{};
        };

        static_assert(sizeof(EnumNamePairRaw) ==
                          controller_ability_detail::enum_layout::native_name_pair_bytes,
                      "Unexpected raw UEnum name pair size");

        static auto read_enum_names_header(UObject* enum_object) -> std::optional<ArrayHeader>;

        static auto write_enum_names_header(UObject* enum_object, const ArrayHeader& names_header)
            -> bool;

        auto try_apply_ability_enum_augmentation() -> bool;

        auto resolve_controller() const -> UObject*;

        auto call_setter(UObject* controller, size_t setter_index, bool enabled,
                         std::wstring* failure_reason) const -> bool;

        auto observe_controller(UObject* controller) -> bool;

        auto setter_needs_update(size_t setter_index, bool desired) const -> bool;

        auto observe_restriction_pawn(UObject* pawn, uint32_t flags) -> void;

        auto apply_magic_links(UObject* controller, UObject* pawn, std::wstring* failure) -> bool;

        auto apply_magic_links(UObject* controller, UObject* pawn, bool magic_enabled,
                               bool links_enabled, std::wstring* failure) -> bool;

        auto refresh_shared_aqua_unlocks(UObject* aqua, std::wstring* failure) -> bool;

        auto apply_unlock_item(std::wstring_view runtime_token, UObject* sora_pawn,
                               RuntimeControllerAbilityGrantResult* result,
                               std::wstring* failure_reason) -> bool;

        auto try_read_unlock_owned(std::wstring_view key, bool* owned,
                                   std::wstring* failure_reason) const -> bool;

        static auto portal_ownership_thunk(UObject*, FFrame& stack, void* result_storage) -> void;

        auto ensure_portal_ownership_binding() -> bool;

        auto request_refresh(RuntimeControllerAbilityEvent event) -> void;

        auto process_pending_refresh(std::chrono::steady_clock::time_point now) -> void;

        auto install_event_hooks() -> void;

        auto uninstall_event_hooks() -> void;

        inline static std::atomic<Impl*> portal_instance_{};
        FWeakObjectPtr portal_function_{};
        bool portal_signature_warning_{};
        bool portal_binding_warning_{};
        RuntimeControllerAbilityConfig config_;
        std::array<std::wstring_view, controller_ability_detail::setter_index::count>
            setter_paths_{};
        FWeakObjectPtr restriction_pawn_{};
        bool story_restricts_magic_{};
        bool story_restricts_links_{};
        bool map_ready_pending_{};
        bool mount_olympus_shoot_flow_pending_{};
        FWeakObjectPtr controller_{};
        FWeakObjectPtr successful_pawn_{}, successful_controller_{};
        RuntimeControllerAbilityAppliedState applied_states_{};
        std::atomic<uint32_t> pending_events_{};
        RuntimeControllerAbilityRefreshState refresh_state_{};
        std::vector<std::pair<std::wstring, std::pair<int, int>>> event_hooks_{};
        bool ability_enum_augmentation_done_{};
        uint64_t aqua_style_revision_{};
        std::wstring last_query_failure_{};
        std::wstring last_setter_failure_{};
    };
} // namespace kh3ap
