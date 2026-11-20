#include "kh3ap/features/party/party_member_service.h"
#include "kh3ap/features/party/party_helpers.h"
#include "kh3ap/features/party/party_world_context.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflected_function_call.h"
#include "kh3ap/features/player/player_animation_support.h"
#include "kh3ap/features/player/combat/character_underwater_combat.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FMulticastDelegateProperty.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <stdexcept>
#include <algorithm>
#include <limits>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace party_native;

    namespace
    {
        auto object_array(UObject* object, const wchar_t* name) -> TArray<UObject*>*
        {
            auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
            if (!property || !property->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(property)->GetInner()->GetElementSize() != sizeof(UObject*))
            {
                throw std::runtime_error("party_swim_array_layout");
            }
            return property->ContainerPtrToValuePtr<TArray<UObject*>>(object);
        }

        auto add_navigation(UObject* pawn, UClass* donor) -> UObject*
        {
            auto* template_class = donor;
            UObject* component_template = nullptr;
            while (template_class && !component_template)
            {
                auto* script = read_object_like_property(template_class, L"SimpleConstructionScript");
                if (script)
                {
                    for (auto* node : *object_array(script, L"AllNodes"))
                    {
                        auto* candidate = read_object_like_property(node, L"ComponentTemplate");
                        if (candidate && candidate->GetClassPrivate()->GetName() == L"MercunaNavigationComponent")
                        {
                            component_template = candidate;
                            break;
                        }
                    }
                }
                if (!component_template) { template_class = template_class->GetSuperClass(); }
            }
            if (!component_template || !template_class)
            {
                throw std::runtime_error("party_swim_navigation_template");
            }
            // Expose the existing SCS template only for this synchronous native
            // AddComponent call. It owns registration, actor lifetime and teardown.
            // Restore the donor's template catalog even if reflection rejects us.
            auto* templates = object_array(template_class, L"ComponentTemplates");
            const auto index = templates->Num();
            templates->Add(component_template);
            struct Restore
            {
                TArray<UObject*>* values;
                int index;
                ~Restore() { values->RemoveAt(index, 1, false); }
            } restore{templates, index};
            ReflectedFunctionCall call(pawn, L"AddComponent", "party swimming navigation", 128);
            call.name(L"TemplateName", component_template->GetName().c_str());
            call.boolean(L"bManualAttachment", true);
            call.object(L"ComponentTemplateContext", donor->GetClassDefaultObject());
            call.numbers(L"RelativeTransform", {0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0});
            call.run();
            auto* navigation = call.result();
            if (!is_uobject_valid_fast(navigation)) { return nullptr; }
            // The SCS template retains Mercuna's defaults. Goofy's constructed
            // component disables both options: NPC steering supplies the route,
            // including underwater arenas without a Mercuna pathfinding volume.
            for (const auto* name : {L"Pathfinding", L"DynamicAvoidance"})
            {
                auto* property = navigation->GetPropertyByNameInChain(name);
                if (!property || !property->IsA<FBoolProperty>())
                {
                    throw std::runtime_error("party_swim_navigation_options");
                }
                static_cast<FBoolProperty*>(property)->SetPropertyValueInContainer(navigation, false);
            }
            return navigation;
        }

        auto animations(UObject* set) -> TArray<player_animation_support::AnimationEntry>*
        {
            auto* property = is_uobject_valid_fast(set) ? set->GetPropertyByNameInChain(L"Anims") : nullptr;
            if (!property || !property->IsA<FArrayProperty>() ||
                static_cast<FArrayProperty*>(property)->GetInner()->GetElementSize() !=
                    sizeof(player_animation_support::AnimationEntry))
            {
                throw std::runtime_error("party_swim_animation_layout");
            }
            auto* result = property->ContainerPtrToValuePtr<TArray<player_animation_support::AnimationEntry>>(set);
            if (result->Num() < 0 || result->Num() > 4096) { throw std::runtime_error("party_swim_animation_count"); }
            return result;
        }

        auto prepare_animations(UObject* instance) -> size_t
        {
            using player_animation_support::AnimationEntry;
            constexpr std::array names{L"SwimIdle", L"SwimNormal", L"SwimTurn",
                                       L"SwimToDive", L"DiveIdle", L"DiveMoveHigh"};
            auto* sets = object_array(instance, L"MyAnimSets");
            auto* source = find_loaded_runtime_asset(player_animation_support::sora_animation_set_path);
            auto* underwater = find_loaded_runtime_asset(character_underwater_combat::animation_set_path);
            std::vector<AnimationEntry> missing;
            for (const auto* name : names)
            {
                const FName key{name, FNAME_Add};
                bool authored = false;
                for (auto* set : *sets)
                {
                    for (const auto& entry : *animations(set))
                    {
                        authored = authored || (entry.name == key && is_uobject_valid_fast(entry.asset));
                    }
                }
                if (authored) { continue; }
                UObject* borrowed = nullptr;
                for (auto* set : {source, underwater})
                {
                    for (const auto& entry : *animations(set))
                    {
                        if (entry.name == key && is_uobject_valid_fast(entry.asset)) { borrowed = entry.asset; break; }
                    }
                    if (borrowed) { break; }
                }
                if (!borrowed) { throw std::runtime_error("party_swim_animation_missing"); }
                missing.push_back({key, borrowed});
            }
            if (missing.empty()) { return 0; }
            if (!player_animation_support::supported_skeleton_layout())
            {
                throw std::runtime_error("party_swim_skeleton_build");
            }
            // Existing native bone-name remapping avoids sharing Sora bone indices.
            player_animation_support::allow_animation_skeleton(instance, missing.front().asset);
            FStaticConstructObjectParameters parameters(source->GetClassPrivate(), instance);
            parameters.SetFlags = RF_Transient;
            auto* private_set = UObjectGlobals::StaticConstructObject(parameters);
            if (!is_uobject_valid_fast(private_set)) { throw std::runtime_error("party_swim_animset_create"); }
            auto* entries = animations(private_set);
            for (const auto& entry : missing) { entries->Add(entry); }
            sets->Add(private_set);
            return missing.size();
        }

        void bind_navigation_completion(UObject* navigation, UObject* pawn)
        {
            auto* property = navigation->GetPropertyByNameInChain(L"OnMoveCompleted");
            if (!property || !property->IsA<FMulticastDelegateProperty>() ||
                property->GetElementSize() != sizeof(FMulticastScriptDelegate) ||
                !pawn->GetFunctionByNameInChain(L"OnMove3DCompleted"))
            {
                throw std::runtime_error("party_swim_completion_delegate");
            }
            auto& callbacks = property->ContainerPtrToValuePtr<FMulticastScriptDelegate>(navigation)->InvocationList;
            const FName function{L"OnMove3DCompleted", FNAME_Add};
            for (const auto& callback : callbacks)
            {
                if (callback.Object.Get() == pawn && callback.FunctionName == function) { return; }
            }
            callbacks.Add(FScriptDelegate{make_uobject_weak(pawn), function});
        }
    }

    auto RuntimePartyMemberService::initialize_party_swimming() -> bool
    {
        if (reconcile_pending_) { return false; }
        auto* manager = resolve_friend_manager();
        auto* level = persistent_level_for(manager);
        if (!level || ship_control_active_ ||
            party_world::observed_party_map_policy(level, config_.runtime_state) != RuntimePartyMapPolicy::managed)
        {
            return true;
        }
        std::array<UObject*, runtime_party_member_slot_count> slots{};
        if (!read_friend_slots(manager, slots)) { return true; }
        bool complete = true;
        for (size_t slot = 0; slot < slots.size(); ++slot)
        {
            auto* pawn = slots[slot];
            const auto choice = active_choices_[slot];
            if (!choice || choice == party_catalog::vanilla_choice ||
                choice >= party_catalog::party_choices.size() || !is_uobject_valid_fast(pawn) ||
                persistent_level_for(pawn) != level ||
                pawn->GetClassPrivate()->GetName() != active_npc_ids_[slot] + L"_Pawn_C" ||
                swimming_pawns_[slot].Get() == pawn)
            {
                continue;
            }
            // Flynn and Jack have authored surface-only states without 3D
            // navigation. They need the diving adapter too. Preserve complete
            // native swim implementations, including Donald and Goofy.
            if (read_object_like_property(pawn, L"m_Swimming") &&
                read_object_like_property(pawn, L"m_Navigation3DComponent"))
            {
                swimming_pawns_[slot] = pawn;
                continue;
            }
            auto* controller = read_object_like_property(pawn, L"Controller");
            auto* board = read_object_like_property(controller, L"Blackboard");
            auto* instance = read_object_like_property(read_object_like_property(pawn, L"MyMesh"), L"AnimScriptInstance");
            if ((!board || !instance) && std::chrono::steady_clock::now() < swimming_deadline_)
            {
                complete = false;
                continue;
            }
            swimming_pawns_[slot] = pawn;
            try
            {
                auto* donor = find_loaded_runtime_asset_class(
                    L"/Game/Blueprints/Npc/n_ex002/n_ex002_Pawn.n_ex002_Pawn_C");
                auto* state = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, L"/Script/TresGame.TresNpcLocomotionDefinitionSwim");
                auto* query = find_loaded_runtime_asset(
                    L"/Game/Blueprints/Npc/Common/BT/Follow/n_Common_FollowPlayerWithSwimQuery.n_Common_FollowPlayerWithSwimQuery");
                if (!is_uobject_valid_fast(donor) || !is_uobject_valid_fast(state) || !query)
                {
                    throw std::runtime_error("party_swim_assets");
                }
                if (!board || !instance) { throw std::runtime_error("party_swim_pawn_readiness_deadline"); }
                if (pawn->GetClassPrivate()->GetName() == L"n_ra201_Pawn_C")
                {
                    // Her native drowning rescue teleports to an authored Corona
                    // location whenever immersion exceeds this threshold. This
                    // selected, swim-enabled pawn can dive instead. Never change
                    // the CDO or the world's native/story Rapunzel.
                    auto* depth = pawn->GetPropertyByNameInChain(L"m_fInWaterWarpDepth");
                    if (!depth || depth->GetClass().GetName() != L"FloatProperty" ||
                        depth->GetElementSize() != sizeof(float))
                    {
                        throw std::runtime_error("party_swim_rescue_depth_layout");
                    }
                    *depth->ContainerPtrToValuePtr<float>(pawn) = std::numeric_limits<float>::max();
                }
                const auto borrowed = prepare_animations(instance);
                auto* navigation = read_object_like_property(pawn, L"m_Navigation3DComponent");
                if (!navigation) { navigation = add_navigation(pawn, donor); }
                if (!navigation) { throw std::runtime_error("party_swim_navigation_create"); }
                bind_navigation_completion(navigation, pawn);
                if (!navigation || !write_object_like_property(pawn, L"m_Navigation3DComponent", navigation) ||
                    !write_object_like_property(pawn, L"m_Swimming", state) ||
                    !write_object_like_property(pawn, L"m_FollowPlayerEQSQuery", query))
                {
                    throw std::runtime_error("party_swim_assignment");
                }
                auto* donor_controller = read_object_like_property(donor->GetClassDefaultObject(), L"AIControllerClass");
                auto* filter = donor_controller ? read_object_like_property(
                    static_cast<UClass*>(donor_controller)->GetClassDefaultObject(), L"DefaultNavigationFilterClass") : nullptr;
                if (!filter || !write_object_like_property(controller, L"DefaultNavigationFilterClass", filter))
                {
                    throw std::runtime_error("party_swim_navigation_filter");
                }
                ReflectedFunctionCall set(board, L"SetValueAsObject", "party swim follow query", 64);
                set.name(L"KeyName", L"FollowPlayerEQSQuery");
                set.object(L"ObjectValue", query);
                set.run();
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Party swimming initialized pawn={} navigation={} borrowed_animations={}\n"),
                    pawn->GetFullName(), navigation->GetFullName(), borrowed);
            }
            catch (const std::exception& error)
            {
                const std::string reason{error.what()};
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Party swimming initialization failed pawn={} reason={}\n"),
                    pawn->GetFullName(), std::wstring{reason.begin(), reason.end()});
            }
        }
        return complete;
    }
}
