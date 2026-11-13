#include <chrono>
#include "kh3ap/features/colors/service/service_impl.h"
#include "kh3ap/features/colors/service/character.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/features/colors/color_material_ops.h"
#include "kh3ap/features/colors/color_weapons.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;
    using namespace color_service_detail;

    namespace
    {
        constexpr auto max_component_material_slots = 24;
        constexpr std::wstring_view default_seed_identity = STR("kh3ap-default-colors");
    } // namespace

    [[nodiscard]] auto RuntimeColorService::Impl::enabled() const -> bool
    {
        return !config.enabled_resolver || config.enabled_resolver();
    }

    [[nodiscard]] auto RuntimeColorService::Impl::harmony_mode() const -> RuntimeColorHarmonyMode
    {
        return config.harmony_mode_resolver ? config.harmony_mode_resolver()
                                            : RuntimeColorHarmonyMode::Coordinated;
    }

    auto RuntimeColorService::Impl::apply_reroll(std::chrono::steady_clock::time_point now) -> void
    {
        // Holding the key down repeats the keydown event - a single press
        // produced 224 re-rolls live. Anything faster than this is a key
        // repeat rather than a person asking for another draw.
        static constexpr std::chrono::milliseconds reroll_cooldown{400};
        if (reroll_counter_ > 0 && now - last_reroll_at_ < reroll_cooldown)
        {
            return;
        }
        last_reroll_at_ = now;
        ++reroll_counter_;
        reapply_in_place(now);
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Color reroll requested counter={} seed={}\n"), reroll_counter_,
            seed_identity());
    }

    [[nodiscard]] auto RuntimeColorService::Impl::seed_identity() const -> std::wstring
    {
        std::wstring seed =
            config.seed_identity_resolver ? config.seed_identity_resolver() : std::wstring{};
        if (seed.empty())
        {
            seed = std::wstring{default_seed_identity};
        }
        if (reroll_counter_ > 0)
        {
            seed += STR("#reroll") + std::to_wstring(reroll_counter_);
        }
        return seed;
    }

    auto RuntimeColorService::Impl::restore_all_colored(std::chrono::steady_clock::time_point now)
        -> void
    {
        UClass* const mesh_component_class = resolve_mesh_component_class();
        if (!mesh_component_class)
        {
            return;
        }
        int restored_pawns = 0;
        int restored_slots = 0;
        for (UObject* const pawn : state.colored())
        {
            if (!pawn || !is_uobject_valid_fast(pawn))
            {
                continue;
            }
            bool touched = false;
            for (UObject* const component : collect_mesh_components(pawn, mesh_component_class))
            {
                const std::optional<int> count = read_num_materials(component);
                const int limit = std::min(count.value_or(0), max_component_material_slots);
                for (int index = 0; index < limit; ++index)
                {
                    if (restore_shipped_material(component, index))
                    {
                        ++restored_slots;
                        touched = true;
                    }
                }
            }
            if (touched)
            {
                ++restored_pawns;
            }
        }
        // Weapons are separate actors, so the walk above never reaches
        // them - runtime_color_weapons owns that half.
        const auto [restored_weapons, restored_weapon_slots] = runtime_color_restore_weapons(state);
        restored_slots += restored_weapon_slots;
        restored_slots += gummi_.restore_all();
        reapply_in_place(now);
        Output::send<LogLevel::Normal>(
            STR("[KH3APProbeCpp] Color restored to vanilla pawns={} weapons={} slots={}\n"),
            restored_pawns, restored_weapons, restored_slots);
    }

    auto RuntimeColorService::Impl::reapply_in_place(std::chrono::steady_clock::time_point now)
        -> void
    {
        auto known_pawns = state.colored();
        for (const auto& entry : state.pending())
        {
            if (UObject* pawn = entry.pawn.Get(); is_uobject_valid_fast(pawn))
            {
                known_pawns.push_back(pawn);
            }
        }
        state.reset_for_map_jump();
        mesh_dump_logged_.clear();
        last_attempt_at_.clear();
        for (UObject* pawn : known_pawns)
        {
            maybe_queue(pawn, now);
        }
        // Known cast does not need another global object scan on F9.
        weapons_dirty_ = true;
        pawn_generation_ = ~uint64_t{0};
        gummi_.repaint_all();
    }

    [[nodiscard]] auto RuntimeColorService::Impl::current_mode() const -> ColorRandomizationMode
    {
        if (!enabled())
        {
            return ColorRandomizationMode::Off;
        }
        return harmony_mode() == RuntimeColorHarmonyMode::Wild
                   ? ColorRandomizationMode::Wild
                   : ColorRandomizationMode::Coordinated;
    }

    auto RuntimeColorService::Impl::try_apply(const RuntimeColorPendingPawn& entry,
                                              std::chrono::steady_clock::time_point now) -> bool
    {
        if (character_is_link(entry.character_key) && now - entry.queued_at < link_settle)
        {
            return false;
        }
        const bool require_anim_instance = (now - entry.queued_at) < anim_instance_grace;
        // The full candidate dump is logged once per pawn: this runs
        // from the pending-retry loop until it succeeds, and a companion
        // that never becomes render-ready would otherwise fill the log
        // with identical scans.
        const bool log_full_dump = take_mesh_dump_slot(entry.pawn.Get());
        UObject* const mesh = resolve_skeletal_mesh_component(entry.pawn.Get(), entry.character_key,
                                                              log_full_dump, require_anim_instance);
        if (!mesh)
        {
            // Stay pending and retry - resolve_skeletal_mesh_component
            // has already logged whether nothing was found or nothing
            // was ready yet.
            return false;
        }
        // The chosen skeletal mesh is the body, but it is not
        // everything a character is wearing - Rapunzel's hair is a
        // second component (TresCharHairMesh1) and would be missed
        // entirely by colouring only the best one.
        // SetVectorParameterValueOnMaterials lives on MeshComponent,
        // so static meshes take it just as well; colour every mesh
        // component the pawn owns.
        std::vector<UObject*> targets{mesh};
        if (UClass* const mesh_component_class = resolve_mesh_component_class())
        {
            for (UObject* const component :
                 collect_mesh_components(entry.pawn.Get(), mesh_component_class))
            {
                if (std::find(targets.begin(), targets.end(), component) == targets.end())
                {
                    targets.push_back(component);
                }
            }
        }

        for (UObject* const target : targets)
        {
            if (!inspect_mesh_candidate(target, false).usable())
            {
                continue;
            }
            apply_character_colors(target, entry.character_key, seed_identity(), harmony_mode());
        }
        return true;
    }
} // namespace kh3ap
