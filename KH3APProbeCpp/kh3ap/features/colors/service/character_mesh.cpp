#include "kh3ap/features/colors/service/character.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/colors/color_material_ops.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UClass.hpp>
#include <UnrealDef.hpp>

#include <algorithm>
#include <optional>
#include <vector>

namespace kh3ap::color_service_detail
{
    using namespace RC;
    using namespace RC::Unreal;

    [[nodiscard]] auto resolve_skeletal_mesh_component(UObject* pawn,
                                                       std::wstring_view character_key,
                                                       bool log_full_dump,
                                                       bool require_anim_instance) -> UObject*
    {
        if (!pawn || !is_uobject_valid_fast(pawn))
        {
            return nullptr;
        }
        UClass* const mesh_class = resolve_skeletal_mesh_component_class();
        if (!mesh_class || !is_uobject_valid_fast(mesh_class))
        {
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Color mesh resolution failed: "
                                                "SkeletalMeshComponent class unresolved\n"));
            return nullptr;
        }

        std::vector<UObject*> components = collect_mesh_components(pawn, mesh_class);
        const size_t enumerated_count = components.size();
        const auto add_candidate = [&components](UObject* candidate)
        {
            if (candidate &&
                std::find(components.begin(), components.end(), candidate) == components.end())
            {
                components.push_back(candidate);
            }
        };
        UObject* const mesh_property_component = try_get_character_mesh(pawn);
        // Always folded in, never merely logged: this is the one
        // component lookup confirmed to work on these pawns, so it is
        // the floor this function must not fall below.
        UObject* const single_lookup_component = first_component_by_class(pawn, mesh_class);
        add_candidate(mesh_property_component);
        add_candidate(single_lookup_component);

        if (log_full_dump)
        {
            Output::send<LogLevel::Normal>(
                STR("[KH3APProbeCpp] Color mesh scan key={} pawn={} candidates={} enumerated={} ")
                    STR("mesh_property={} single_lookup={}\n"),
                character_key, format_object_debug_name(pawn),
                static_cast<uint64_t>(components.size()), static_cast<uint64_t>(enumerated_count),
                format_object_debug_name(mesh_property_component),
                format_object_debug_name(single_lookup_component));
        }

        if (components.empty())
        {
            if (log_full_dump)
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Color mesh resolution found "
                                                    "no skeletal mesh components key={}\n"),
                                                character_key);
            }
            return nullptr;
        }

        std::optional<MeshCandidate> best{};
        for (UObject* const component : components)
        {
            const MeshCandidate candidate =
                inspect_mesh_candidate(component, component == mesh_property_component);
            if (log_full_dump)
            {
                log_mesh_candidate(character_key, candidate, STR("scanned"));
            }
            if (!candidate.usable())
            {
                continue;
            }
            // Within the grace window, hold out for the mesh that is
            // actually animating rather than colouring whatever is
            // available first.
            if (require_anim_instance && !candidate.anim_instance)
            {
                continue;
            }
            if (!best || candidate.score > best->score)
            {
                best = candidate;
            }
        }

        if (!best)
        {
            // Not an error: a companion pawn detected the instant it
            // spawns can legitimately have no render-ready mesh yet.
            // The caller keeps the pawn pending and retries.
            if (log_full_dump)
            {
                Output::send<LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Color mesh not ready yet key={} scanned={}\n"),
                    character_key, static_cast<uint64_t>(components.size()));
            }
            return nullptr;
        }

        log_mesh_candidate(character_key, *best, STR("chosen"));
        return best->component;
    }
} // namespace kh3ap::color_service_detail
