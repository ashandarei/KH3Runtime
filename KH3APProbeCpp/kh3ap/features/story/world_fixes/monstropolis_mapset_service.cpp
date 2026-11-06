#include "kh3ap/features/story/world_fixes/monstropolis_mapset_service.h"

#include "kh3ap/features/grants/delivery/item_call_helpers.h"
#include "kh3ap/features/story/world_fixes/monstropolis_mapset.h"
#include "kh3ap/features/story/world_fixes/graveyard_backtracking.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <UnrealDef.hpp>

#include <utility>

using namespace RC;
using namespace RC::Unreal;

namespace kh3ap
{
    namespace
    {
        constexpr auto hook_path = L"/Script/TresGame.TresGameBlueprintLibrary:TresApplyMapSet";

        auto script_name(const wchar_t* text) -> MonstropolisScriptName
        {
            const FName name(text, FNAME_Add);
            return {name.GetComparisonIndex(), name.GetDisplayIndex(), name.GetNumber()};
        }

        // This reference lives only during the synchronous native thunk. Restore
        // the operand before returning so a later normal arrival uses retail data.
        struct ScopedPreset
        {
            uint8_t* operand{};
            MonstropolisScriptName original{};
            ~ScopedPreset()
            {
                if (operand) std::memcpy(operand, original.data(), sizeof(original));
            }
        };
    }

    struct RuntimeMonstropolisMapSetService::Impl
    {
        using Thunk = void(*)(UObject*, FFrame&, void*);
        inline static Impl* instance{};
        RuntimeMonstropolisMapSetConfig config;
        Thunk original{};
        bool attempted{};

        explicit Impl(RuntimeMonstropolisMapSetConfig value) : config(std::move(value)) {}
        ~Impl()
        {
            if (original)
            {
                auto* function = UObjectGlobals::StaticFindObject<UFunction*>(
                        UFunction::StaticClass(), nullptr, hook_path);
                if (is_uobject_valid_fast(function) && function->GetFuncPtr() == thunk)
                    function->SetFuncPtr(original);
            }
            if (instance == this) instance = nullptr;
        }

        static auto last_start_tag(FFrame_50_AndBelow* frame) -> std::optional<std::wstring>
        {
            if (!frame->Locals) return {};
            // This local is the result used by the immediately preceding
            // PlayerStart switch. Borrow it; never change or destroy its FString.
            for (auto* property : frame->Node->ForEachProperty())
            {
                if (property->GetName() != L"CallFunc_GetLastPlayerStartTag_ReturnValue") continue;
                const auto offset = property->GetOffset_Internal();
                if (property->GetClass().GetName() != L"StrProperty" ||
                    property->GetElementSize() != sizeof(FString) || offset < 0 ||
                    offset + sizeof(FString) > frame->Node->GetPropertiesSize()) return {};
                const auto* value = property->ContainerPtrToValuePtr<FString>(frame->Locals);
                const auto* chars = value->GetCharArray();
                return chars ? std::optional<std::wstring>{chars} : std::nullopt;
            }
            return {};
        }

        auto select_resume_preset(FFrame& stack, ScopedPreset& replacement) -> void
        {
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (!frame->Code || !is_uobject_valid_fast(frame->Node) ||
                !is_uobject_valid_fast(frame->Object)) return;
            if (frame->Node->GetFullName() != monstropolis_mapset_caller) return;
            const auto owner = frame->Object->GetFullName();
            if (!owner.starts_with(L"mi_02_C /Game/Levels/mi/mi_02/mi_02.mi_02:PersistentLevel.")) return;

            // The map owns the MapSet object. Neither cached Sora nor engine
            // streaming flags describe the visibility command being selected.
            auto* property = frame->Object->GetPropertyByNameInChain(L"mi_02_mapset");
            if (!property || property->GetClass().GetName() != L"ObjectProperty" ||
                property->GetElementSize() != sizeof(UObject*)) return;
            auto* mapset = *property->ContainerPtrToValuePtr<UObject*>(frame->Object);
            if (!is_uobject_valid_fast(mapset)) return;
            const auto mapset_path = mapset->GetPathName();
            if (mapset_path != monstropolis_before_mapset) return;
            int32_t story{};
            std::wstring failure;
            if (!try_call_get_game_flag(L"gameflow_MI", story, config.runtime_state,
                        config.get_game_flag_path, config.blueprint_library_default_object_path,
                        false, &failure) || story != 3160) return;
            const auto tag = last_start_tag(frame);
            if (!tag) return;
            const auto preset = monstropolis_resume_preset(
                    frame->Node->GetFullName(), mapset_path, story, *tag);
            if (preset.empty()) return;

            auto* function = UObjectGlobals::StaticFindObject<UFunction*>(
                    UFunction::StaticClass(), nullptr, hook_path);
            if (!is_uobject_valid_fast(function)) return;
            auto* latent = find_function_param_by_name(function, L"LatentInfo");
            if (!latent || !latent->IsA<FStructProperty>()) return;
            auto* structure = static_cast<FStructProperty*>(latent)->GetStruct();
            if (!is_uobject_valid_fast(structure) || structure->GetName() != L"LatentActionInfo") return;
            auto& script = frame->Node->GetScript();
            const auto begin = reinterpret_cast<uintptr_t>(script.GetData());
            const auto code = reinterpret_cast<uintptr_t>(frame->Code);
            if (!begin || script.Num() <= 0 || code < begin ||
                code - begin >= static_cast<size_t>(script.Num())) return;
            const auto operand = monstropolis_preset_operand(
                    {script.GetData(), static_cast<size_t>(script.Num())}, code - begin,
                    reinterpret_cast<uintptr_t>(function), reinterpret_cast<uintptr_t>(structure),
                    reinterpret_cast<uintptr_t>(property), script_name(L"ExecuteUbergraph_mi_02"),
                    script_name(preset.data()));
            if (!operand)
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] MonstropolisMapSet startup layout rejected tag={} story={}\n"), *tag, story);
                return;
            }
            replacement.operand = script.GetData() + *operand;
            std::memcpy(replacement.original.data(), replacement.operand, sizeof(replacement.original));
            const auto mission = script_name(L"MI_02_MISSION");
            std::memcpy(replacement.operand, mission.data(), sizeof(mission));
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] MonstropolisMapSet laser resume tag={} gameflow_MI={} preset={}->MI_02_MISSION\n"),
                    *tag, story, preset);
        }

        auto select_graveyard_resume_preset(FFrame& stack, ScopedPreset& replacement) -> void
        {
            auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
            if (!frame->Code || !is_uobject_valid_fast(frame->Node) || !is_uobject_valid_fast(frame->Object) ||
                    frame->Node->GetFullName() != L"Function /Game/Levels/kg/kg_02/kg_02.kg_02_C:ExecuteUbergraph_kg_02" ||
                    !frame->Object->GetFullName().starts_with(L"kg_02_C /Game/Levels/kg/kg_02/kg_02.kg_02:PersistentLevel.")) return;
            auto* function = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, hook_path);
            if (!is_uobject_valid_fast(function)) return;
            auto* property = find_function_param_by_name(function, L"LatentInfo");
            if (!property || !property->IsA<FStructProperty>()) return;
            auto* latent = static_cast<FStructProperty*>(property)->GetStruct();
            if (!is_uobject_valid_fast(latent) || latent->GetName() != L"LatentActionInfo") return;
            auto& script = frame->Node->GetScript();
            const auto start = reinterpret_cast<uintptr_t>(script.GetData());
            const auto code = reinterpret_cast<uintptr_t>(frame->Code);
            if (!start || script.Num() <= 0 || code < start || code - start >= static_cast<size_t>(script.Num())) return;
            const size_t argument_offset = code - start;
            if (static_cast<size_t>(script.Num()) - argument_offset < 63) return;
            const auto* args = script.GetData() + argument_offset;
            if (args[39] != 0x20) return;
            // Use the object actually supplied by this loaded graph. An async
            // return from Garden can run before a global name lookup sees it.
            UObject* mapset{}; std::memcpy(&mapset, args + 40, sizeof(mapset));
            if (!is_uobject_valid_fast(mapset) || mapset->GetPathName() !=
                    L"/Game/Levels/kg/kg_02/mapset/ms_kg_02.ms_kg_02") return;
            std::optional<size_t> operand;
            std::wstring_view old;
            for (const auto* candidate : {L"Init", L"KG_02_Mission_AB_Start", L"KG_02_Mission_AB_Camera",
                    L"KG_02_Mission_AB", L"KG_02_AB_CLEAR", L"KG_02_Area_A_End", L"KG_02_Area_B_End"})
            {
                operand = graveyard_preset_operand({script.GetData(), static_cast<size_t>(script.Num())}, code - start,
                        reinterpret_cast<uintptr_t>(function), reinterpret_cast<uintptr_t>(latent),
                        reinterpret_cast<uintptr_t>(mapset), script_name(L"ExecuteUbergraph_kg_02"), script_name(candidate));
                if (operand) { old = candidate; break; }
            }
            if (!operand) return;
            std::array<int32_t, 5> flags{};
            constexpr std::wstring_view names[]{L"gameflow_KG", L"gameflow_KG_13_A", L"gameflow_KG_13_B",
                    L"gameflow_KG_13_C", L"gameflow_KG_13_D"};
            for (size_t i = 0; i < flags.size(); ++i)
                if (!try_call_get_game_flag(names[i], flags[i], config.runtime_state, config.get_game_flag_path,
                            config.blueprint_library_default_object_path, false)) return;
            const auto target = graveyard_resume_preset(flags[0], flags[1], flags[2], flags[3], flags[4], old);
            if (target.empty())
            {
                Output::send<LogLevel::Warning>(
                        STR("[KH3APProbeCpp] GraveyardMapSet retained story={} A={} B={} C={} D={} preset={}\n"),
                        flags[0], flags[1], flags[2], flags[3], flags[4], old);
                return;
            }
            replacement.operand = script.GetData() + *operand;
            std::memcpy(replacement.original.data(), replacement.operand, sizeof(replacement.original));
            const auto next = script_name(target.data());
            std::memcpy(replacement.operand, next.data(), sizeof(next));
            Output::send<LogLevel::Warning>(
                    STR("[KH3APProbeCpp] GraveyardMapSet story={} A={} B={} C={} D={} preset={}->{} latent=preserved\n"),
                    flags[0], flags[1], flags[2], flags[3], flags[4], old, target);
        }

        static auto thunk(UObject* context, FFrame& stack, void* result) -> void
        {
            auto& self = *instance;
            ScopedPreset replacement;
            try { self.select_resume_preset(stack, replacement); }
            catch (...) {}
            if (!replacement.operand)
            {
                try { self.select_graveyard_resume_preset(stack, replacement); }
                catch (...) {}
            }
            // Preserve the owning map, latent continuation, loading behavior and
            // original native implementation. Only this call's preset differs.
            self.original(context, stack, result);
        }

        auto install_once() -> void
        {
            if (attempted) return;
            attempted = true;
            auto* function = UObjectGlobals::StaticFindObject<UFunction*>(
                    UFunction::StaticClass(), nullptr, hook_path);
            if (!is_uobject_valid_fast(function) || !(function->GetFunctionFlags() & FUNC_Native) ||
                !function->GetFuncPtr() || function->GetNumParms() != 5)
            {
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] MonstropolisMapSet hook rejected reflected layout\n"));
                return;
            }
            original = function->GetFuncPtr();
            instance = this;
            function->SetFuncPtr(thunk);
            Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] MonstropolisMapSet hook installed mode=mission_resume\n"));
        }
    };

    RuntimeMonstropolisMapSetService::RuntimeMonstropolisMapSetService(RuntimeMonstropolisMapSetConfig config)
        : impl_(std::make_unique<Impl>(std::move(config))) {}
    RuntimeMonstropolisMapSetService::~RuntimeMonstropolisMapSetService() = default;
    auto RuntimeMonstropolisMapSetService::install_once() -> void { impl_->install_once(); }
}
