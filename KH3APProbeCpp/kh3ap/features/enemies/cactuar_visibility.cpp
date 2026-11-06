#include "kh3ap/features/enemies/level_entity_remap.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Hooks.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

namespace kh3ap::enemy_remap_detail
{
    namespace
    {
        constexpr std::wstring_view camera_class =
                L"/Game/Levels/ts/ts_02/umap/ts_02_camera_ms141.ts_02_camera_ms141_C";
        constexpr std::wstring_view enemy_level =
                L"/Game/Levels/ts/ts_02/umap/ts_02_enemy_07.ts_02_enemy_07:PersistentLevel.";
        FName skip_event;

        bool is_cactuar_intro_entity(std::wstring_view path)
        {
            if (!path.starts_with(enemy_level)) return false;
            const auto name = path.substr(enemy_level.size());
            return name == L"e_ex018_Pawn9_44" || name == L"e_ex033_Pawn7_48" ||
                    name == L"e_ex032_Pawn_46";
        }

        std::optional<int32_t> read_integer(UObject* pawn, const wchar_t* name)
        {
            auto* function = pawn->GetFunctionByNameInChain(name);
            auto* result = function ? find_function_param_by_name(function, L"ReturnValue") : nullptr;
            if (!result || result->GetOffset_Internal() != 0 ||
                    !((function->GetParmsSize() == 4 && result->GetClass().GetName() == L"IntProperty") ||
                      (function->GetParmsSize() == 1 && result->GetClass().GetName() == L"ByteProperty")))
                return std::nullopt;
            int32_t value{};
            pawn->ProcessEvent(function, &value);
            return value;
        }

        void reveal_cactuar_replacements(UObject* context, std::wstring_view event)
        {
            auto& state = s_runtime_enemy_remap;
            if (!state.active || !state.enemy_enabled.load()) return;
            if (!is_uobject_valid_fast(context) || !is_uobject_valid_fast(context->GetClassPrivate()) ||
                    context->GetClassPrivate()->GetPathName() != camera_class)
                return;
            auto* level = context->GetOuterPrivate();
            if (!is_uobject_valid_fast(level) ||
                    try_read_reflected_object_property_value(level, L"LevelScriptActor") != context)
                return;

            // The scene's mesh-specific tracks reveal original enemy types only.
            // Once it returns control, reveal the three remapped intro entities.
            // Keep later waves and vanilla actors alone; never poll visibility in combat.
            std::vector<RuntimeLoadedLevelEntityRemap> candidates;
            {
                std::lock_guard lock{state.loaded_level_entity_remaps_mutex};
                for (const auto& remap : state.loaded_level_entity_remaps)
                {
                    if (remap.kind == RuntimeLevelEntityRemapKind::enemy &&
                            remap.source_class_path != remap.replacement_class_path &&
                            is_cactuar_intro_entity(remap.object_path))
                        candidates.push_back(remap);
                }
            }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
            Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Cactuar reveal checkpoint context={} candidates={}\n"),
                    context->GetPathName(), candidates.size());
#endif
            for (const auto& remap : candidates)
            {
                auto* entity = resolve_loaded_level_entity(remap);
                auto* pawn = is_uobject_valid_fast(entity)
                        ? try_read_reflected_object_property_value(entity, L"m_EntityActor") : nullptr;
                if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(pawn->GetClassPrivate()) ||
                        pawn->GetClassPrivate()->GetPathName() != remap.replacement_class_path ||
                        try_read_reflected_object_property_value(pawn, L"m_LevelEntity") != entity)
                    continue;
                auto* hidden = pawn->GetPropertyByNameInChain(L"bHidden");
                if (!hidden || hidden->GetClass().GetName() != L"BoolProperty" ||
                        !static_cast<FBoolProperty*>(hidden)->GetPropertyValueInContainer(pawn))
                    continue;
                const auto hp = read_integer(pawn, L"GetHitPoint");
                const auto current_state = read_integer(pawn, L"GetStateID");
                if (!hp || *hp <= 0 || !current_state || *current_state == tres_state_die ||
                        *current_state == tres_state_vanish_die)
                    continue;
                auto* show = pawn->GetFunctionByNameInChain(L"SetActorHiddenInGame");
                auto* parameter = show ? find_function_param_by_name(show, L"bNewHidden") : nullptr;
                if (!parameter || show->GetParmsSize() != 1 || parameter->GetOffset_Internal() != 0 ||
                        parameter->GetClass().GetName() != L"BoolProperty")
                    continue;
                uint8_t hide = 0;
                pawn->ProcessEvent(show, &hide);
                Output::send<LogLevel::Normal>(STR("[KH3APProbeCpp] Cactuar replacement revealed entity={} pawn={} hidden_after={} event={}\n"),
                        remap.object_path, pawn->GetPathName(),
                        static_cast<FBoolProperty*>(hidden)->GetPropertyValueInContainer(pawn), event);
            }
        }

        void after_cinematic_end(UnrealScriptFunctionCallableContext& callback, void*)
        {
            if (!s_runtime_enemy_remap.enemy_enabled.load()) return;
            // Blueprint-native calls keep the caller's frame. The audited ms141
            // calls pass Self; direct ProcessEvent calls have an actual parameter buffer.
            auto* context = get_frame_object(callback.TheStack);
            if (!is_uobject_valid_fast(context) || !is_uobject_valid_fast(context->GetClassPrivate()) ||
                    context->GetClassPrivate()->GetPathName() != camera_class)
                context = try_read_function_object_param(get_frame_node(callback.TheStack),
                        get_frame_locals(callback.TheStack), L"WorldContextObject");
            reveal_cactuar_replacements(context, L"TresEndCinematicMode");
        }

        void after_scene_skip(UObject* context, UFunction* function, void*)
        {
            if (!s_runtime_enemy_remap.enemy_enabled.load() || !function ||
                    function->GetNamePrivate() != skip_event) return;
            reveal_cactuar_replacements(context, L"OnEventSkip");
        }
    }

    void install_cactuar_replacement_reveal_guard()
    {
        UObjectGlobals::RegisterHook(L"/Script/TresGame.TresGameBlueprintLibrary:TresEndCinematicMode",
                [](UnrealScriptFunctionCallableContext&, void*) {},
                &after_cinematic_end, nullptr);
        skip_event = FName{L"OnEventSkip", FNAME_Add};
        Hook::RegisterProcessEventPostCallback(&after_scene_skip);
    }
}
