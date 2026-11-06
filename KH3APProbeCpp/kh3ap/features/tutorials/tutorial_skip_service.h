#pragma once

#include "kh3ap/features/tutorials/link_tutorial_bypass.h"
#include "kh3ap/features/tutorials/informational_help_bypass.h"
#include "kh3ap/features/tutorials/tutorial_patch_lifecycle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    inline constexpr std::wstring_view runtime_olympus_early_gameflow_class_name =
        L"he_02_gameflow_C";
    inline constexpr std::wstring_view runtime_olympus_early_execute_function_name =
        L"ExecuteUbergraph_he_02_gameflow";
    inline constexpr std::wstring_view runtime_olympus_early_function_path =
        L"/Game/Levels/he/he_02/umap/"
        L"he_02_gameflow.he_02_gameflow_C:ExecuteUbergraph_he_02_gameflow";

    [[nodiscard]] auto
    try_apply_runtime_olympus_he02_early_bypass(RC::Unreal::UObject* constructed_object) -> bool;

    inline constexpr std::wstring_view runtime_twilight_town_gameflow_class_name =
        L"tt_01_gameflow_C";
    inline constexpr std::wstring_view runtime_twilight_town_execute_function_name =
        L"ExecuteUbergraph_tt_01_gameflow";
    inline constexpr std::wstring_view runtime_twilight_town_function_path =
        L"/Game/Levels/tt/tt_01/umap/"
        L"tt_01_gameflow.tt_01_gameflow_C:ExecuteUbergraph_tt_01_gameflow";

    [[nodiscard]] auto
    try_apply_runtime_twilight_town_early_bypass(RC::Unreal::UObject* constructed_object) -> bool;

    inline constexpr std::wstring_view runtime_olympus_completion_gameflow_class_name =
        L"he_01_gameflow_C";
    inline constexpr std::wstring_view runtime_olympus_completion_execute_function_name =
        L"ExecuteUbergraph_he_01_gameflow";
    inline constexpr std::wstring_view runtime_olympus_completion_function_path =
        L"/Game/Levels/he/he_01/umap/"
        L"he_01_gameflow.he_01_gameflow_C:ExecuteUbergraph_he_01_gameflow";

    inline constexpr std::wstring_view runtime_olympus_save_points_gameflow_class_name =
        L"he_03_gameflow_C";
    inline constexpr std::wstring_view runtime_olympus_save_points_function_path =
        L"/Game/Levels/he/he_03/umap/"
        L"he_03_gameflow.he_03_gameflow_C:ExecuteUbergraph_he_03_gameflow";

    inline constexpr std::wstring_view runtime_caribbean_sea_spectacle_class_name =
        L"ca_02_island01_treasure01_C";
    inline constexpr std::wstring_view runtime_caribbean_sea_spectacle_function_path =
        L"/Game/Levels/ca/ca_02/umap/"
        L"ca_02_island01_treasure01.ca_02_island01_treasure01_C:ExecuteUbergraph_ca_02_island01_"
        L"treasure01";
    inline constexpr std::wstring_view runtime_caribbean_sea_spectacle_execute_function_name =
        L"ExecuteUbergraph_ca_02_island01_treasure01";

    enum class RuntimeFinalWorldGameflowVariant
    {
        Ew01,
        Ew04,
    };

    enum class RuntimeToyBoxTutorialVariant : uint8_t
    {
        AerialRecovery,
        Gigas,
        MagAssemble,
        EightBitBlast,
        Count,
    };

    inline constexpr size_t runtime_toy_box_tutorial_variant_count =
        static_cast<size_t>(RuntimeToyBoxTutorialVariant::Count);
    inline constexpr std::array<std::wstring_view, runtime_toy_box_tutorial_variant_count>
        runtime_toy_box_tutorial_gameflow_class_names{
            L"ts_01_gameflow_C",
            L"ts_02_camera_ms030e_C",
            L"ts_02_Denjigimmickfront_C",
            L"ts_03_gameflow_C",
        };
    inline constexpr std::array<std::wstring_view, runtime_toy_box_tutorial_variant_count>
        runtime_toy_box_tutorial_function_paths{
            L"/Game/Levels/ts/ts_01/umap/"
            L"ts_01_gameflow.ts_01_gameflow_C:ExecuteUbergraph_ts_01_gameflow",
            L"/Game/Levels/ts/ts_02/umap/"
            L"ts_02_camera_ms030e.ts_02_camera_ms030e_C:ExecuteUbergraph_ts_02_camera_ms030e",
            L"/Game/Levels/ts/ts_02/umap/"
            L"ts_02_Denjigimmickfront.ts_02_Denjigimmickfront_C:ExecuteUbergraph_ts_02_"
            L"Denjigimmickfront",
            L"/Game/Levels/ts/ts_03/umap/"
            L"ts_03_gameflow.ts_03_gameflow_C:ExecuteUbergraph_ts_03_gameflow",
        };

    struct RuntimeTutorialSkipConfig
    {
        bool olympus_completion_enabled{false};
        bool olympus_early_tutorials_enabled{false};
        bool olympus_save_points_enabled{false};
        bool caribbean_sea_spectacle_enabled{false};
        bool toy_box_tutorials_enabled{false};
        bool twilight_town_tutorials_enabled{false};
        bool final_world_ui_enabled{false};
        bool link_tutorials_enabled{false};
        bool informational_help_enabled{false};
        std::wstring_view olympus_completion_context_prefix{};
        std::wstring_view olympus_completion_function_path{};
        std::wstring_view olympus_early_context_prefix{};
        std::wstring_view olympus_early_function_path{};
        std::wstring_view olympus_save_points_context_prefix{};
        std::wstring_view olympus_save_points_function_path{};
        std::wstring_view caribbean_sea_spectacle_context_prefix{};
        std::wstring_view caribbean_sea_spectacle_function_path{};
        std::array<std::wstring_view, runtime_toy_box_tutorial_variant_count>
            toy_box_context_prefixes{};
        std::array<std::wstring_view, runtime_toy_box_tutorial_variant_count>
            toy_box_function_paths{};
        std::wstring_view twilight_town_context_prefix{};
        std::wstring_view twilight_town_function_path{};
        std::array<std::wstring_view, 2> final_world_function_paths{};
    };

    // Owns the map-lifecycle orchestration and per-playable-context state for
    // the narrow story tutorial bytecode patches. The mod class only routes
    // lifecycle evidence into this component.
    class RuntimeTutorialSkipService
    {
    public:
        explicit RuntimeTutorialSkipService(RuntimeTutorialSkipConfig config) noexcept;

        auto reset_playable_context() noexcept -> void;
        auto try_apply_for_context(std::wstring_view context_hint, std::wstring_view source)
            -> void;
        auto try_apply_final_world_for_context(std::wstring_view context_hint,
                                               std::wstring_view source) -> void;

        [[nodiscard]] auto final_world_ui_bypasses_applied() const noexcept -> bool;

    private:
        auto try_apply_informational_help_for_context(std::wstring_view context_hint,
                                                      std::wstring_view source) -> void;
        auto try_apply_links_for_context(std::wstring_view context_hint, std::wstring_view source)
            -> void;
        auto try_apply_olympus_completion_for_context(std::wstring_view context_hint,
                                                      std::wstring_view source) -> void;
        auto try_apply_olympus_early_for_context(std::wstring_view context_hint,
                                                 std::wstring_view source) -> void;
        auto try_apply_olympus_save_points_for_context(std::wstring_view context_hint,
                                                       std::wstring_view source) -> void;
        auto try_apply_caribbean_sea_spectacle_for_context(std::wstring_view context_hint,
                                                           std::wstring_view source) -> void;
        auto try_apply_toy_box_for_context(std::wstring_view context_hint, std::wstring_view source)
            -> void;
        auto try_apply_twilight_town_for_context(std::wstring_view context_hint,
                                                 std::wstring_view source) -> void;

        RuntimeTutorialSkipConfig config_{};
        std::array<TutorialPatchLifecycle, runtime_informational_help_class_names.size()>
            informational_help_{};
        std::array<TutorialPatchLifecycle, runtime_link_tutorial_count> links_{};
        TutorialPatchLifecycle olympus_completion_{};
        TutorialPatchLifecycle olympus_early_{};
        TutorialPatchLifecycle olympus_save_points_{};
        TutorialPatchLifecycle caribbean_sea_spectacle_{};
        std::array<TutorialPatchLifecycle, runtime_toy_box_tutorial_variant_count> toy_box_{};
        TutorialPatchLifecycle twilight_town_{};
        TutorialPatchLifecycle final_world_{};
    };

    // UObject construction is the only early-enough signal for the Darkside
    // transition. Keep the synchronous patch implementation alongside the
    // story tutorial subsystem instead of inside the generic object observer.
    [[nodiscard]] auto
    try_apply_runtime_final_world_early_bypass(RuntimeFinalWorldGameflowVariant variant,
                                               RC::Unreal::UObject* object) -> bool;

    // The post-boss return from he_06 constructs he_01_gameflow after
    // HandleStartingNewPlayer, and its reward/tutorial block can begin before
    // a later map-ready callback. Patch each he_01 class load synchronously so
    // both Formchange panels and camp menu are bypassed before ReceiveBeginPlay.
    // world_keyblade_bypass.h separately suppresses the vanilla Hero's Origin
    // grant while retaining the story flags used by the AP completion check.
    [[nodiscard]] auto
    try_apply_runtime_olympus_completion_early_bypass(RC::Unreal::UObject* gameflow_object) -> bool;

    // he_03 can enter its Flowmotion, Team Attacks, Grand Magic, Formchange,
    // Attractions, save-point, and Moogle Shop tutorial calls before the first
    // playable-context callback. Patch synchronously when its gameflow object
    // is constructed, before ReceiveBeginPlay can start any of those sequences
    // or enable Attraction Flow or Style Change.
    [[nodiscard]] auto
    try_apply_runtime_olympus_save_points_early_bypass(RC::Unreal::UObject* object) -> bool;

    // The Sea Spectacle help actor begins its delay as soon as it begins play.
    // Patch only its latent help call synchronously when the class is
    // constructed, retaining cinematic cleanup and using map-lifecycle retry
    // as a fallback.
    [[nodiscard]] auto
    try_apply_runtime_caribbean_sea_spectacle_early_bypass(RC::Unreal::UObject* tutorial_actor)
        -> bool;

    // Toy Box can enter these standalone help calls immediately after the
    // owning gameflow actor begins play. Patch each exact asset synchronously
    // when its class is constructed, with map-lifecycle retry as a fallback.
    [[nodiscard]] auto try_apply_runtime_toy_box_early_bypass(RuntimeToyBoxTutorialVariant variant,
                                                              RC::Unreal::UObject* object) -> bool;

    [[nodiscard]] auto
    try_apply_runtime_informational_help_early_bypass(size_t variant, RC::Unreal::UObject* object)
        -> bool;

    [[nodiscard]] auto
    try_apply_runtime_link_tutorial_early_bypass(RuntimeLinkTutorialVariant variant,
                                                 RC::Unreal::UObject* object) -> bool;
} // namespace kh3ap
