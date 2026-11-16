#include "kh3ap/features/party/party_member_service.h"
#include "kh3ap/features/party/party_helpers.h"
#include "kh3ap/features/party/party_world_context.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/memory/checked_process_read.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FClassProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/Property/FObjectProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/FScriptArray.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/TArray.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/UObject.hpp>
#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace kh3ap
{
    using namespace RC::Unreal;
    using namespace party_native;
    namespace
    {
        constexpr uint8_t swimming_state = 143;
        constexpr auto attack_query_path =
            L"/Game/AI/EQS/Queries/Default_FindAttackLocation3D.Default_FindAttackLocation3D";

        auto inherited_action_prefix(std::wstring_view npc) -> std::wstring_view
        {
            // KH3 Mickey's authored tree uses the native Dark World Mickey
            // definitions. They still belong to this character's moveset.
            return npc == L"n_ex003" ? L"/Game/Blueprints/Npc/n_dw003/" : L"";
        }

        auto owns_action_asset(std::wstring_view path, std::wstring_view prefix,
                               std::wstring_view inherited) -> bool
        {
            return (!prefix.empty() && path.starts_with(prefix)) ||
                   (!inherited.empty() && path.starts_with(inherited));
        }

        auto swim_field(UObject* object) -> FBoolProperty*
        {
            auto* field = is_uobject_valid_fast(object)
                              ? object->GetPropertyByNameInChain(L"m_bSwimAttack") : nullptr;
            return field && field->IsA<FBoolProperty>() ? static_cast<FBoolProperty*>(field) : nullptr;
        }

        auto swim_magic_field(UObject* object) -> FBoolProperty*
        {
            auto* field = is_uobject_valid_fast(object)
                              ? object->GetPropertyByNameInChain(L"m_SwimMagic") : nullptr;
            return field && field->IsA<FBoolProperty>() ? static_cast<FBoolProperty*>(field) : nullptr;
        }

        template <typename T> auto array(UObject* object, const wchar_t* name) -> TArray<T>*
        {
            auto* field = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
            if (!field || !field->IsA<FArrayProperty>()) { return nullptr; }
            auto* inner = static_cast<FArrayProperty*>(field)->GetInner();
            if (!inner || inner->GetElementSize() != sizeof(T))
            {
                throw std::runtime_error("party_swim_combat_array_layout");
            }
            auto* values = field->ContainerPtrToValuePtr<TArray<T>>(object);
            if (values->Num() < 0 || values->Num() > 512)
            {
                throw std::runtime_error("party_swim_combat_array_count");
            }
            return values;
        }

        // Walk only the selected pawn's authored tree graph, once per identity.
        // This includes the task and validation decorator references to the same
        // action; neither a global UObject scan nor an idle discovery pass is needed.
        struct ActionGraph
        {
            std::unordered_set<UObject*> visited;
            std::unordered_set<UObject*> actions;
            std::wstring action_prefix;
            std::wstring_view inherited_prefix;

            bool owns(std::wstring_view path) const
            {
                return owns_action_asset(path, action_prefix, inherited_prefix);
            }

            void object(UObject* value)
            {
                if (!is_uobject_valid_fast(value) || !visited.insert(value).second) { return; }
                if (visited.size() > 4096) { throw std::runtime_error("party_swim_combat_graph_bound"); }
                fields(value->GetClassPrivate(), value, 0);
            }

            void fields(UStruct* schema, void* container, unsigned depth)
            {
                if (depth > 8) { throw std::runtime_error("party_swim_combat_struct_depth"); }
                for (auto* field : schema->ForEachPropertyInChain())
                {
                    const auto name = field->GetName();
                    const auto kind = field->GetClass().GetName();
                    const bool action = name == L"m_ActionDefinition" || name == L"ActionDefinitionOverride";
                    const bool link = name == L"RootNode" || name == L"BehaviorAsset" || name == L"DefaultBehaviorAsset" ||
                                      name == L"ChildComposite" || name == L"ChildTask";
                    const bool list = name == L"Children" || name == L"Decorators" ||
                                      name == L"Services" || name == L"RootDecorators";
                    if ((action || link) && (kind == L"ObjectProperty" || kind == L"ClassProperty") &&
                        field->GetElementSize() == sizeof(UObject*))
                    {
                        auto* value = *field->ContainerPtrToValuePtr<UObject*>(container);
                        if (!is_uobject_valid_fast(value)) { continue; }
                        if (link) { object(value); }
                        else if (kind == L"ClassProperty" && owns(value->GetPathName()))
                        {
                            // These reflected fields are native action-class properties.
                            auto* definition = static_cast<UClass*>(value)->GetClassDefaultObject();
                            // Marshmallow's older animation actions expose the
                            // shared attack query/state schema, but no swim flag.
                            if (is_uobject_valid_fast(definition) &&
                                definition->GetPropertyByNameInChain(L"m_ActionEQSQuery"))
                            { actions.insert(definition); }
                        }
                    }
                    if (!list || !field->IsA<FArrayProperty>()) { continue; }
                    auto* inner = static_cast<FArrayProperty*>(field)->GetInner();
                    auto* values = field->ContainerPtrToValuePtr<FScriptArray>(container);
                    if (!inner || values->Num() < 0 || values->Num() > 512)
                    {
                        throw std::runtime_error("party_swim_combat_graph_array");
                    }
                    for (int index = 0; index < values->Num(); ++index)
                    {
                        auto* entry = static_cast<std::byte*>(values->GetData()) +
                                      index * inner->GetElementSize();
                        if (inner->IsA<FStructProperty>())
                        {
                            fields(static_cast<FStructProperty*>(inner)->GetStruct(), entry, depth + 1);
                        }
                        else if (inner->GetClass().GetName() == L"ObjectProperty" && inner->GetElementSize() == sizeof(UObject*))
                        {
                            object(*reinterpret_cast<UObject**>(entry));
                        }
                    }
                }
            }
        };

        template <typename State> bool remember_actions(State& state, const ActionGraph& graph)
        {
            bool added = false;
            for (auto* definition : graph.actions)
            {
                if (std::any_of(state.actions.begin(), state.actions.end(),
                                [definition](const auto& patch) { return patch.definition.Get() == definition; }))
                {
                    continue;
                }
                auto* flag = swim_field(definition);
                auto* states = array<uint8_t>(definition, L"m_ViableStates");
                if (!states || (flag && flag->GetPropertyValueInContainer(definition))) { continue; }
                auto* query = read_object_like_property(definition, L"m_ActionEQSQuery");
                // Null is an authored ground setting (including Lea's attacks).
                if (query) { query->SetRootSet(); }
                const bool add_state = states->Num() > 0 &&
                    std::find(states->begin(), states->end(), swimming_state) == states->end();
                auto* magic = swim_magic_field(definition);
                const auto swimming_magic = magic
                    ? std::optional<bool>{magic->GetPropertyValueInContainer(definition)} : std::nullopt;
                state.actions.push_back({make_uobject_weak(definition), make_uobject_weak(query), false, add_state, swimming_magic});
                added = true;
            }
            return added;
        }

        auto active_roots(UObject* pawn, UObject* manager) -> std::vector<UObject*>
        {
            std::vector<UObject*> roots;
            auto* controller = read_object_like_property(pawn, L"Controller");
            auto* brain = read_object_like_property(controller, L"BrainComponent");
            if (!brain || read_object_like_property(brain, L"AIOwner") != controller) { return roots; }
            auto* nodes = brain->GetPropertyByNameInChain(L"NodeInstances");
            // KH3's shipped BT manager does not retain LoadedTemplates. Read the
            // active stack using its adjacent reflected NodeInstances anchor.
            // The 4.17 stack has two pointers, three array headers and two bytes
            // per entry (72 bytes including alignment). Validate each root's
            // manager, tree reference and composite schema before using it.
            if (!nodes || !nodes->IsA<FArrayProperty>() || nodes->GetOffset_Internal() != 0x280)
            { throw std::runtime_error("party_swim_live_tree_layout"); }
            struct Header { uintptr_t data; int32_t count; int32_t capacity; };
            const auto stack = try_read_current_process_value<Header>(reinterpret_cast<uintptr_t>(brain) + 0x260);
            if (!stack || stack->count < 0 || stack->count > 16 || stack->capacity < stack->count || stack->capacity > 64)
            { throw std::runtime_error("party_swim_live_tree_count"); }
            for (int index = 0; index < stack->count; ++index)
            {
                const auto address = try_read_current_process_value<uintptr_t>(stack->data + index * 72);
                auto* root = address ? reinterpret_cast<UObject*>(*address) : nullptr;
                if (!is_uobject_valid_fast(root) || root->GetOuterPrivate() != manager ||
                    !read_object_like_property(root, L"TreeAsset"))
                { throw std::runtime_error("party_swim_live_tree_identity"); }
                auto* children = root->GetPropertyByNameInChain(L"Children");
                if (!children || !children->IsA<FArrayProperty>())
                { throw std::runtime_error("party_swim_live_tree_schema"); }
                roots.push_back(root);
            }
            return roots;
        }

        bool land_gate(UObject* node)
        {
            if (node->GetClassPrivate()->GetName() != L"TresBTDecorator_TresStateCheck") { return false; }
            auto* inverse = node->GetPropertyByNameInChain(L"bInverseCondition");
            auto* source = node->GetPropertyByNameInChain(L"m_Source");
            if (!inverse || !inverse->IsA<FBoolProperty>() ||
                static_cast<FBoolProperty*>(inverse)->GetPropertyValueInContainer(node) ||
                !source || !source->IsA<FStructProperty>()) { return false; }
            auto* key = find_struct_field_by_name(static_cast<FStructProperty*>(source)->GetStruct(), L"SelectedKeyName");
            if (!key || key->GetClass().GetName() != L"NameProperty" ||
                key->ContainerPtrToValuePtr<FName>(source->ContainerPtrToValuePtr<void>(node))->ToString() != L"SelfActor")
            { return false; }
            auto* states = array<uint8_t>(node, L"m_States");
            return states && std::find(states->begin(), states->end(), uint8_t{98}) != states->end() &&
                   std::find(states->begin(), states->end(), swimming_state) == states->end();
        }

        void adapt_states(UObject* object, const wchar_t* field, bool restore)
        {
            auto* states = array<uint8_t>(object, field);
            if (!states) { throw std::runtime_error("party_swim_combat_state_layout"); }
            int found = -1;
            for (int index = 0; index < states->Num(); ++index)
            {
                if ((*states)[index] == swimming_state) { found = index; break; }
            }
            if (restore && found >= 0) { states->RemoveAt(found, 1, false); }
            else if (!restore && found < 0) { states->Add(swimming_state); }
        }

        auto cached_actions(UObject* pawn) -> std::vector<UObject*>
        {
            std::vector<UObject*> result;
            auto* queue = read_object_like_property(pawn, L"MyStateComp");
            if (!queue) { return result; }
            if (auto* state = read_object_like_property(queue, L"m_pCurrentState")) { result.push_back(state); }
            for (const auto* name : {L"m_StateQueue", L"m_RecycleState"})
            {
                if (auto* states = array<UObject*>(queue, name))
                {
                    for (auto* state : *states)
                    {
                        if (is_uobject_valid_fast(state) &&
                            std::find(result.begin(), result.end(), state) == result.end())
                        {
                            result.push_back(state);
                        }
                    }
                }
            }
            return result;
        }

        void adapt(UObject* action, UObject* query, bool swimming, bool add_state, bool restore,
                   std::optional<bool> swimming_magic = std::nullopt)
        {
            auto* flag = swim_field(action);
            auto* states = array<uint8_t>(action, L"m_ViableStates");
            if (!states || !write_object_like_property(action, L"m_ActionEQSQuery", query, restore))
            {
                throw std::runtime_error("party_swim_combat_action_layout");
            }
            if (flag) { flag->SetPropertyValueInContainer(action, swimming); }
            if (swimming_magic)
            {
                auto* magic = swim_magic_field(action);
                if (!magic) { throw std::runtime_error("party_swim_magic_layout"); }
                magic->SetPropertyValueInContainer(action, *swimming_magic);
            }
            if (!add_state) { return; }
            adapt_states(action, L"m_ViableStates", restore);
        }

        template <typename State> void restore_state(State& state)
        {
            if (!state.underwater) { return; }
            // Restore independent entries even if one reflected layout becomes
            // unavailable during teardown. No exception may interrupt reconciliation.
            const auto attempt = [](auto&& operation)
            {
                try { operation(); }
                catch (const std::exception& error)
                {
                    const std::string reason{error.what()};
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[KH3APProbeCpp] Party swim combat restoration failed reason={}\n"),
                        std::wstring{reason.begin(), reason.end()});
                }
            };
            for (const auto& weak : state.land_gates)
            {
                if (auto* node = weak.Get(); is_uobject_valid_fast(node))
                { attempt([&] { adapt_states(node, L"m_States", true); }); }
            }
            std::vector<UObject*> instances;
            attempt([&] { instances = cached_actions(state.pawn.Get()); });
            for (const auto& patch : state.actions)
            {
                auto* definition = patch.definition.Get();
                if (!is_uobject_valid_fast(definition)) { continue; }
                attempt([&] { adapt(definition, patch.query.Get(), patch.swimming, patch.added_state, true, patch.swimming_magic); });
                for (auto* instance : instances)
                {
                    if (instance->GetClassPrivate() == definition->GetClassPrivate())
                    { attempt([&] { adapt(instance, patch.query.Get(), patch.swimming, patch.added_state, true, patch.swimming_magic); }); }
                }
            }
            state.underwater = false;
        }
    }

    auto RuntimePartyMemberService::observe_friend_state(uintptr_t component) -> void
    {
        if (!installed_ || !active_ || !component) { return; }
        for (const auto& state : swim_combat_)
        {
            if (!state.disabled && reinterpret_cast<uintptr_t>(state.state_component.Get()) == component)
            {
                maintenance_.request();
                return;
            }
        }
    }

    auto RuntimePartyMemberService::restore_party_swim_combat() -> void
    {
        for (auto& state : swim_combat_)
        {
            restore_state(state);
            state = {};
        }
    }

    auto RuntimePartyMemberService::maintain_party_swim_combat() -> void
    {
        if (reconcile_pending_) { return; }
        auto* manager = resolve_friend_manager();
        auto* level = persistent_level_for(manager);
        if (!level || ship_control_active_ ||
            party_world::observed_party_map_policy(level, config_.runtime_state) != RuntimePartyMapPolicy::managed)
        {
            restore_party_swim_combat();
            return;
        }
        std::array<UObject*, runtime_party_member_slot_count> slots{};
        if (!read_friend_slots(manager, slots)) { return; }
        for (size_t slot = 0; slot < slots.size(); ++slot)
        {
            auto* pawn = slots[slot];
            const auto choice = active_choices_[slot];
            auto& state = swim_combat_[slot];
            if (!choice || choice == party_catalog::vanilla_choice || !is_uobject_valid_fast(pawn) ||
                persistent_level_for(pawn) != level || swimming_pawns_[slot].Get() != pawn ||
                pawn->GetClassPrivate()->GetName() != active_npc_ids_[slot] + L"_Pawn_C")
            {
                restore_state(state);
                state = {};
                continue;
            }
            // Preserve a complete authored swim moveset. Surface-only friends
            // receive our generic diving state during swimming initialization;
            // their original non-null CDO state must not suppress combat setup.
            auto* authored_swimming = read_object_like_property(
                pawn->GetClassPrivate()->GetClassDefaultObject(), L"m_Swimming");
            if (authored_swimming && read_object_like_property(pawn, L"m_Swimming") == authored_swimming)
            {
                restore_state(state);
                state = {};
                continue;
            }
            if (state.pawn.Get() == pawn && state.disabled) { continue; }
            try
            {
                if (state.pawn.Get() != pawn)
                {
                    // Replacement/travel restoration precedes the old viable-state restoration.
                    restore_state(state);
                    state = {};
                    state.pawn = make_uobject_weak(pawn);
                    state.state_component = make_uobject_weak(read_object_like_property(pawn, L"MyStateComp"));
                    ActionGraph graph;
                    graph.action_prefix = L"/Game/Blueprints/Npc/" + active_npc_ids_[slot] + L"/";
                    graph.inherited_prefix = inherited_action_prefix(active_npc_ids_[slot]);
                    graph.object(read_object_like_property(pawn, L"MyAIBehavior"));
                    RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] Party swim graph nodes={} attacks={} pawn={}\n"),
                        graph.visited.size(), graph.actions.size(), pawn->GetFullName());
                    remember_actions(state, graph);
                }
                auto* world = level->GetOuterPrivate();
                auto* ai = read_object_like_property(world, L"AISystem");
                auto* trees = read_object_like_property(ai, L"BehaviorTreeManager");
                bool new_gates = false;
                bool new_actions = false;
                const auto prefix = L"/Game/Blueprints/Npc/" + active_npc_ids_[slot] + L"/";
                for (auto* root : active_roots(pawn, trees))
                {
                    auto* asset = read_object_like_property(root, L"TreeAsset");
                    ActionGraph graph;
                    graph.action_prefix = prefix;
                    graph.inherited_prefix = inherited_action_prefix(active_npc_ids_[slot]);
                    if (!graph.owns(asset->GetPathName()) ||
                        std::any_of(state.live_roots.begin(), state.live_roots.end(),
                                    [root](const auto& weak) { return weak.Get() == root; })) { continue; }
                    graph.object(root);
                    // Most world friends inject their battle flow dynamically.
                    // Their top-level authored tree contains no attack references.
                    // Capture newly active definitions before marking this root seen,
                    // including their original land settings for exact restoration.
                    new_actions = remember_actions(state, graph) || new_actions;
                    for (auto* node : graph.visited)
                    {
                        if (node->GetOuterPrivate() == trees && land_gate(node) &&
                            std::none_of(state.land_gates.begin(), state.land_gates.end(),
                                         [node](const auto& weak) { return weak.Get() == node; }))
                        {
                            state.land_gates.push_back(make_uobject_weak(node));
                            new_gates = true;
                        }
                    }
                    state.live_roots.push_back(make_uobject_weak(root));
                }
                const auto underwater = friend_underwater(pawn);
                if (!underwater) { continue; }
                if (*underwater == state.underwater && !((new_gates || new_actions) && *underwater))
                {
                    if (!update_swimming_party_ai(pawn, *underwater))
                    { throw std::runtime_error("party_swim_ai_switch"); }
                    continue;
                }
                auto* query = *underwater ? find_loaded_runtime_asset(attack_query_path) : nullptr;
                if (*underwater && !is_uobject_valid_fast(query)) { throw std::runtime_error("party_swim_combat_query"); }
                auto instances = cached_actions(pawn);
                // Mark first so a partial failure still has an explicit restoration path.
                state.underwater = true;
                for (const auto& weak : state.land_gates)
                {
                    if (auto* node = weak.Get(); is_uobject_valid_fast(node)) { adapt_states(node, L"m_States", !*underwater); }
                }
                for (auto& patch : state.actions)
                {
                    auto* definition = patch.definition.Get();
                    if (!is_uobject_valid_fast(definition)) { continue; }
                    auto* effective_query = *underwater ? query : patch.query.Get();
                    const auto magic = patch.swimming_magic
                        ? std::optional<bool>{*underwater || *patch.swimming_magic} : std::nullopt;
                    adapt(definition, effective_query, *underwater || patch.swimming, patch.added_state, !*underwater, magic);
                    for (auto* instance : instances)
                    {
                        if (instance->GetClassPrivate() == definition->GetClassPrivate())
                        {
                            adapt(instance, effective_query, *underwater || patch.swimming, patch.added_state, !*underwater, magic);
                        }
                    }
                }
                state.underwater = *underwater;
                // Restore attack properties before resuming the original land AI.
                // This runs only on pending party/state work, never an idle timer.
                if (!update_swimming_party_ai(pawn, *underwater))
                { throw std::runtime_error("party_swim_ai_switch"); }
                RC::Output::send<RC::LogLevel::Normal>(
                    STR("[KH3APProbeCpp] Party swim combat underwater={} actions={} gates={} pawn={}\n"),
                    *underwater, state.actions.size(), state.land_gates.size(), pawn->GetFullName());
            }
            catch (const std::exception& error)
            {
                restore_state(state);
                state.pawn = make_uobject_weak(pawn);
                state.disabled = true; // Retry only after a new pawn/reconciliation.
                const std::string reason{error.what()};
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[KH3APProbeCpp] Party swim combat failed pawn={} reason={}\n"),
                    pawn->GetFullName(), std::wstring{reason.begin(), reason.end()});
            }
        }
    }
}
