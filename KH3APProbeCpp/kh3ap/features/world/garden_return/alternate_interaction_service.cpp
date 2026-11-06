#include "alternate_interaction_service.h"
#include "runtime_context.h"
#include "kh3ap/features/player/player_ids.h"
#include "kh3ap/features/player/player_state_ids.h"
#include "kh3ap/features/player/characters/character_profile.h"
#include "kh3ap/features/player/characters/character_layout.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/player/movement/character_traversal.h"
#include "kh3ap/features/player/movement/character_barrel_spin.h"
#include "kh3ap/features/ingredients/ingredient_tracker.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/features/kh02/treasure/kh02_chest_interaction_policy.h"
#include "kh3ap/features/treasure/chest_persistence_service.h"
#include "kh3ap/features/world/garden_return/garden_return_policy.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UClass.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <algorithm>
#include <array>
#include <tuple>
#include <vector>
#include <utility>

namespace kh3ap::garden_return_detail
{
    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        constexpr int maximum_overlap_parameter_bytes = 32;
        constexpr int maximum_reactor_parameter_bytes = 40;
        constexpr size_t maximum_mirror_class_depth = 64;

        constexpr int aqua_player_id = player_id_value(PlayerId::Aqua);
        constexpr auto character_restore_initial_delay = std::chrono::seconds(1);
        constexpr auto aqua_chest_timeout = std::chrono::seconds(30);
        constexpr auto aqua_chest_reconcile_delay = std::chrono::milliseconds(500);
        constexpr auto aqua_save_timeout = std::chrono::seconds(8);
        constexpr auto aqua_interaction_radius = 350.0f;
        constexpr int32_t save_point_action_command = 76;
        constexpr uint8_t idle_player_state = player_state_ids::idle;
        constexpr uint8_t running_player_state = player_state_ids::run;
        constexpr uint8_t check_reactor_command = 4;
        constexpr int32_t ship_action_command = 83;
        constexpr uint8_t ship_reactor_command = 8;
        constexpr int32_t shop_action_command = 80;
        constexpr uint8_t shop_reactor_command = 5;
        constexpr int32_t rail_action_command = 87;
        constexpr uint8_t rail_reactor_command = 12;
    } // namespace

    struct AlternateInteractionService::Impl
    {
        AlternateInteractionConfig config;

        explicit Impl(AlternateInteractionConfig config) : config(std::move(config)) {}

        RuntimeOptionsInputOwnership save_input;
        FWeakObjectPtr aqua_save_point, aqua_save_pawn, aqua_save_controller;
        std::wstring aqua_save_world;
        bool aqua_save_pending{}, aqua_save_released{}, aqua_save_dispatched{};
        uint64_t aqua_saves_opened{};
        Clock::time_point aqua_save_started{};
        FWeakObjectPtr aqua_chest, aqua_chest_pawn, aqua_chest_controller;
        std::wstring aqua_chest_world;
        std::wstring mirror_interaction_rejection;
        bool aqua_chest_observing{}, aqua_chest_pending{};
        Clock::time_point aqua_chest_started{}, aqua_chest_dispatched_at{};
        uint64_t aqua_chests_opened{};
        bool aqua_collectible_pending{};
        uint64_t aqua_collectibles_collected{};

        bool save_point_overlaps(UObject* actor, UObject* pawn)
        {
            auto* recovery = object_field(actor, L"MyRecover");
            auto* reactor = object_field(actor, L"MyReactor");
            if (!recovery || !reactor)
            {
                return false;
            }
            for (auto [object, name] : {std::pair{actor, L"m_bIsActive"}, std::pair{recovery, L"m_bIsGimmickActive"},
                                        std::pair{reactor, L"m_bIsGimmickActive"}})
            {
                auto* property = object->GetPropertyByNameInChain(name);
                if (!property || !property->IsA<FBoolProperty>() ||
                    !static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object))
                {
                    return false;
                }
            }
            auto* function = recovery->GetFunctionByNameInChain(L"IsOverlappingActor");
            if (!is_uobject_valid_fast(function) ||
                function->GetParmsSize() > maximum_overlap_parameter_bytes)
            {
                return false;
            }
            auto* result = find_function_param_by_name(function, L"ReturnValue");
            std::vector<std::byte> params(function->GetParmsSize());
            if (!result || !result->IsA<FBoolProperty>() ||
                !write_object_function_param(function, params.data(), L"Other", pawn))
            {
                return false;
            }
            recovery->ProcessEvent(function, params.data());
            return static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(params.data());
        }

        UObject* current_save_point(UObject* pawn)
        {
            auto* controller = object_field(pawn, L"Controller");
            auto* command_info = object_field(controller, L"m_pUICommandInfo");
            auto* actor = object_field(command_info, L"m_Actor");
            auto* component = object_field(command_info, L"m_Component");
            auto* actor_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresSavePointActor");
            if (!is_uobject_valid_fast(actor_class))
            {
                return nullptr;
            }
            if (actor && actor->IsA(actor_class) && component && object_field(actor, L"MyReactor") == component &&
                save_point_overlaps(actor, pawn))
            {
                return actor;
            }
            const bool alternate = character_swap::owns_swap_pawn(pawn) && character_traversal::ready(pawn);
            if (player_id(pawn) != aqua_player_id && !alternate)
            {
                return nullptr;
            }
            // Alternate controllers may never populate Sora's command-command_info
            // object. Resolve only on an interaction press or pending save,
            // requiring the native recovery overlap and one active point.
            UObject* found = nullptr;
            for (auto* candidate : find_live_actors_of_class(pawn, actor_class, 128))
            {
                if (save_point_overlaps(candidate, pawn))
                {
                    if (found)
                    {
                        return nullptr;
                    }
                    found = candidate;
                }
            }
            return found;
        }

        bool bind_save_command(UObject* pawn, UObject* point)
        {
            if (!save_point_overlaps(point, pawn))
            {
                return false;
            }
            auto* controller = object_field(pawn, L"Controller");
            auto* reactor = object_field(point, L"MyReactor");
            auto* function = controller ? controller->GetFunctionByNameInChain(L"BP_NotifySetActionCommand1") : nullptr;
            if (!is_uobject_valid_fast(function) || !reactor ||
                function->GetParmsSize() > maximum_reactor_parameter_bytes)
            {
                return false;
            }
            std::vector<std::byte> params(function->GetParmsSize());
            if (!write_integral_function_param(find_function_param_by_name(function, L"inCommand"), params.data(),
                                               save_point_action_command) ||
                !write_float_function_param(find_function_param_by_name(function, L"inTimer"), params.data(), 2.0) ||
                !write_object_function_param(function, params.data(), L"inActor", point) ||
                !write_object_function_param(function, params.data(), L"InComponent", reactor))
            {
                return false;
            }
            controller->ProcessEvent(function, params.data());
            // The controller's play-command command_info is assigned when the action
            // starts, rather than when this available command is registered.
            // Opening the native save menu provides the completion evidence.
            return true;
        }

        bool within_aqua_interaction_radius(UObject* actor, UObject* pawn, UObject* component = nullptr)
        {
            std::array<std::array<float, 3>, 2> positions{};
            size_t position_index = 0;
            for (const auto& [target, method] :
                 {std::pair{component ? component : actor,
                            component ? L"K2_GetComponentLocation" : L"K2_GetActorLocation"},
                  std::pair{pawn, L"K2_GetActorLocation"}})
            {
                auto* location = target ? target->GetFunctionByNameInChain(method) : nullptr;
                if (!is_uobject_valid_fast(location) || location->GetParmsSize() != sizeof(positions[0]))
                {
                    return false;
                }
                target->ProcessEvent(location, positions[position_index].data());
                ++position_index;
            }
            float distance_squared = 0;
            for (size_t axis = 0; axis < positions[0].size(); ++axis)
            {
                const float delta = positions[0][axis] - positions[1][axis];
                distance_squared += delta * delta;
            }
            return distance_squared <= aqua_interaction_radius * aqua_interaction_radius;
        }

        bool chest_eligible(UObject* chest, UObject* pawn)
        {
            auto* actor_class =
                UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, L"/Script/TresGame.TresTreasureBox");
            if (!is_uobject_valid_fast(actor_class) || !is_uobject_valid_fast(chest) || !is_uobject_a_fast(chest, actor_class) ||
                (!chest->GetPathName().starts_with(L"/Game/Zevels/dw/") &&
                 (character_swap::current_hero() != pawn || !character_traversal::ready(pawn))) ||
                object_field(chest, L"m_pOpenner") || reconcile_chest_record(chest, false))
            {
                return false;
            }
            auto* level = chest->GetOuterPrivate();
            auto* owning = object_field(level, L"OwningWorld");
            if (!owning || owning->GetFullName() != world())
            {
                return false;
            }
            for (auto [object, name, wanted] :
                 {std::tuple{chest, L"bHidden", false}, std::tuple{chest, L"m_bIsActive", true},
                  std::tuple{object_field(chest, L"MyReactor"), L"m_bIsGimmickActive", true}})
            {
                auto* property = is_uobject_valid_fast(object) ? object->GetPropertyByNameInChain(name) : nullptr;
                if (!property || !property->IsA<FBoolProperty>() ||
                    static_cast<FBoolProperty*>(property)->GetPropertyValueInContainer(object) != wanted)
                {
                    return false;
                }
            }
            // Selection comes from the actual native command command_info below. This
            // is only an additional stale-selection guard, not a proximity scan.
            return within_aqua_interaction_radius(chest, pawn);
        }

        bool experimental_garden_portal(UObject* actor, UObject* pawn)
        {
            if (!pawn || character_swap::current_hero() != pawn ||
                world() != L"World /Game/Maps/GardenOfAssemblage/umap/GoA_01.GoA_01")
            {
                return false;
            }
            auto* portal_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Game/Maps/GardenOfAssemblage/uasset/Textures/Portals/Portal_Base.Portal_Base_C");
            return is_uobject_valid_fast(portal_class) && is_uobject_a_fast(actor, portal_class);
        }

        bool check_target_eligible(UObject* actor, UObject* pawn, bool mirror = false)
        {
            auto reject = [&](const wchar_t* reason)
            {
                if (mirror)
                {
                    mirror_interaction_rejection = reason;
                }
                return false;
            };
            if (!is_uobject_valid_fast(actor) || !is_uobject_valid_fast(pawn))
            {
                return reject(L"invalid_target_or_pawn");
            }
            bool supported_target = false;
            if (mirror)
            {
                // Classify the selected actor's live hierarchy. Source classes
                // stream out and back in with their rooms; a separate global
                // asset lookup must not decide whether this live mirror works.
                constexpr std::array<std::wstring_view, 5> mirror_paths{
                    L"/Game/Zlueprints/Gimmick/dw/g_dw_Mirror/g_dw_MirrorA.g_dw_MirrorA_C",
                    L"/Game/Zlueprints/Gimmick/dw/g_dw_Mirror/g_dw_MirrorB.g_dw_MirrorB_C",
                    L"/Game/Zlueprints/Gimmick/dw/g_dw_Mirror/g_dw_MirrorC.g_dw_MirrorC_C",
                    L"/Game/Zlueprints/Gimmick/dw/g_dw_Mirror/g_dw_MirrorD.g_dw_MirrorD_C",
                    L"/Game/Zlueprints/Gimmick/dw/g_dw_ColumnMirror/g_dw_ColumnMirrorBase.g_dw_ColumnMirrorBase_C"};
                auto* mirror_class = static_cast<UStruct*>(actor->GetClassPrivate());
                for (std::size_t depth = 0;
                     is_uobject_valid_fast(mirror_class) && depth < maximum_mirror_class_depth;
                     ++depth)
                {
                    const auto class_path = mirror_class->GetPathName();
                    if (std::find(mirror_paths.begin(), mirror_paths.end(), class_path) != mirror_paths.end())
                    {
                        supported_target = true;
                        break;
                    }
                    auto* parent_class = mirror_class->GetSuperStruct();
                    if (parent_class == mirror_class)
                    {
                        break;
                    }
                    mirror_class = parent_class;
                }
            }
            else
            {
                auto* collectible_class = UObjectGlobals::StaticFindObject<UClass*>(
                    nullptr, nullptr, L"/Game/Zlueprints/Gimmick/g_SimpleReactorBase.g_SimpleReactorBase_C");
                supported_target =
                    (is_uobject_valid_fast(collectible_class) && is_uobject_a_fast(actor, collectible_class)) ||
                    experimental_garden_portal(actor, pawn);
            }
            if (!supported_target)
            {
                return reject(L"unsupported_target_class");
            }
            auto* owning_world = object_field(actor->GetOuterPrivate(), L"OwningWorld");
            if (!owning_world || owning_world->GetFullName() != world())
            {
                return reject(L"target_world_mismatch");
            }
            auto* reactor = object_field(actor, L"TresReactor");
            auto* command = reactor ? reactor->GetPropertyByNameInChain(L"m_Command") : nullptr;
            auto* active = reactor ? reactor->GetPropertyByNameInChain(L"m_bIsGimmickActive") : nullptr;
            auto* hidden = actor->GetPropertyByNameInChain(L"bHidden");
            if (!command || command->GetElementSize() != 1 ||
                *command->ContainerPtrToValuePtr<uint8_t>(reactor) != check_reactor_command || !active ||
                !active->IsA<FBoolProperty>() ||
                !static_cast<FBoolProperty*>(active)->GetPropertyValueInContainer(reactor) || !hidden ||
                !hidden->IsA<FBoolProperty>() ||
                static_cast<FBoolProperty*>(hidden)->GetPropertyValueInContainer(actor))
            {
                return reject(L"target_reactor_or_visibility");
            }
            if (!within_aqua_interaction_radius(actor, pawn,
                                                experimental_garden_portal(actor, pawn) ? reactor : nullptr))
            {
                return reject(L"target_out_of_range");
            }
            if (mirror)
            {
                mirror_interaction_rejection.clear();
            }
            return true;
        }

        bool alternate_ingredient_target(UObject* actor, UObject* reactor, UObject* pawn)
        {
            if (character_swap::current_hero() != pawn || !character_traversal::ready(pawn) ||
                !is_uobject_valid_fast(actor) || !is_uobject_valid_fast(reactor) ||
                ingredient_spot_key(actor->GetPathName()).empty() || object_field(actor, L"TresReactor") != reactor)
            {
                return false;
            }
            auto* owning_world = object_field(actor->GetOuterPrivate(), L"OwningWorld");
            auto* command = reactor->GetPropertyByNameInChain(L"m_Command");
            auto* active = reactor->GetPropertyByNameInChain(L"m_bIsGimmickActive");
            auto* hidden = actor->GetPropertyByNameInChain(L"bHidden");
            return owning_world && owning_world->GetFullName() == world() && command &&
                   command->GetElementSize() == sizeof(uint8_t) &&
                   *command->ContainerPtrToValuePtr<uint8_t>(reactor) == check_reactor_command && active &&
                   active->IsA<FBoolProperty>() &&
                   static_cast<FBoolProperty*>(active)->GetPropertyValueInContainer(reactor) && hidden &&
                   hidden->IsA<FBoolProperty>() &&
                   !static_cast<FBoolProperty*>(hidden)->GetPropertyValueInContainer(actor) &&
                   within_aqua_interaction_radius(actor, pawn);
        }

        int32_t alternate_ship_command(UObject* actor, UObject* reactor, UObject* pawn, bool require_active = true)
        {
            auto* ship_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresVehiclePawnSpecialShip");
            if (character_swap::current_hero() != pawn || !character_traversal::ready(pawn) ||
                !is_uobject_valid_fast(ship_class) || !is_uobject_valid_fast(actor) ||
                !is_uobject_a_fast(actor, ship_class) || !is_uobject_valid_fast(reactor))
            {
                return 0;
            }
            auto* owning_world = object_field(actor->GetOuterPrivate(), L"OwningWorld");
            auto* command = reactor->GetPropertyByNameInChain(L"m_Command");
            auto* active = reactor->GetPropertyByNameInChain(L"m_bIsGimmickActive");
            if (!owning_world || owning_world->GetFullName() != world() || !command ||
                command->GetElementSize() != sizeof(uint8_t) || !active || !active->IsA<FBoolProperty>() ||
                (require_active && !static_cast<FBoolProperty*>(active)->GetPropertyValueInContainer(reactor)) ||
                !within_aqua_interaction_radius(actor, pawn, reactor))
            {
                return 0;
            }
            const auto selected = *command->ContainerPtrToValuePtr<uint8_t>(reactor);
            if (reactor == object_field(actor, L"m_Reactor") && selected == ship_reactor_command)
            {
                return ship_action_command;
            }
            if (reactor == object_field(actor, L"TresReactorMap") && selected == check_reactor_command)
            {
                return check_action_command;
            }
            return 0;
        }

        bool alternate_shop_target(UObject* actor, UObject* reactor, UObject* pawn)
        {
            const auto current_player = player_id(pawn);
            if ((current_player != player_id_value(PlayerId::Aqua) &&
                 current_player != player_id_value(PlayerId::Kairi) &&
                 current_player != player_id_value(PlayerId::Roxas)) ||
                character_swap::current_hero() != pawn || !character_swap::owns_swap_pawn(pawn) ||
                !character_traversal::ready(pawn) || !is_uobject_valid_fast(actor) ||
                !is_uobject_valid_fast(reactor) || object_field(actor, L"MyReactor") != reactor)
            {
                return false;
            }
            auto* shop_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresShopActorMogli");
            auto* owning_world = object_field(actor->GetOuterPrivate(), L"OwningWorld");
            auto* command = reactor->GetPropertyByNameInChain(L"m_Command");
            if (!is_uobject_valid_fast(shop_class) || !is_uobject_a_fast(actor, shop_class) ||
                !owning_world || owning_world->GetFullName() != world() || !command ||
                command->GetElementSize() != sizeof(uint8_t) ||
                *command->ContainerPtrToValuePtr<uint8_t>(reactor) != shop_reactor_command)
            {
                return false;
            }
            for (const auto& [target, name, wanted] :
                 {std::tuple{actor, L"bHidden", false}, std::tuple{actor, L"m_bIsActive", true},
                  std::tuple{reactor, L"m_bIsGimmickActive", true}})
            {
                auto* field = target->GetPropertyByNameInChain(name);
                if (!field || !field->IsA<FBoolProperty>() ||
                    static_cast<FBoolProperty*>(field)->GetPropertyValueInContainer(target) != wanted)
                {
                    return false;
                }
            }
            // Tres reactors use their own collision shapes and intentionally
            // disable Unreal overlap events. Require the normal press's exact
            // actor/reactor selection and the shared interaction distance;
            // UPrimitiveComponent::IsOverlappingActor is not their overlap API.
            return within_aqua_interaction_radius(actor, pawn, reactor);
        }

        bool alternate_simple_check_target(UObject* actor, UObject* reactor, UObject* pawn)
        {
            const auto current_player = player_id(pawn);
            if ((current_player != player_id_value(PlayerId::Aqua) &&
                 current_player != player_id_value(PlayerId::Kairi) &&
                 current_player != player_id_value(PlayerId::Roxas)) ||
                character_swap::current_hero() != pawn || !character_swap::owns_swap_pawn(pawn) ||
                !character_traversal::ready(pawn) || !is_uobject_valid_fast(actor) ||
                !is_uobject_valid_fast(reactor) || object_field(actor, L"TresReactor") != reactor)
            {
                return false;
            }
            // Classify the selected live hierarchy because these classes stream
            // with their rooms. Static simple reactors share the normal Check
            // command; Monstropolis elevators use a separate skeletal base.
            constexpr std::array<std::wstring_view, 2> check_class_paths{
                L"/Game/Blueprints/Gimmick/g_SimpleReactorStaticBase.g_SimpleReactorStaticBase_C",
                L"/Game/Blueprints/Gimmick/mi/g_mi_Elevator/g_mi_ElevatorBase.g_mi_ElevatorBase_C"};
            bool supported_target = false;
            auto* target_class = static_cast<UStruct*>(actor->GetClassPrivate());
            for (size_t depth = 0;
                 is_uobject_valid_fast(target_class) && depth < maximum_mirror_class_depth;
                 ++depth)
            {
                const auto class_path = target_class->GetPathName();
                if (std::find(check_class_paths.begin(), check_class_paths.end(), class_path) != check_class_paths.end())
                {
                    supported_target = true;
                    break;
                }
                auto* parent = target_class->GetSuperStruct();
                if (parent == target_class)
                {
                    break;
                }
                target_class = parent;
            }
            auto* owning_world = object_field(actor->GetOuterPrivate(), L"OwningWorld");
            auto* command = reactor->GetPropertyByNameInChain(L"m_Command");
            if (!supported_target || !owning_world || owning_world->GetFullName() != world() || !command ||
                command->GetElementSize() != sizeof(uint8_t) ||
                *command->ContainerPtrToValuePtr<uint8_t>(reactor) != check_reactor_command)
            {
                return false;
            }
            for (const auto& [target, name, wanted] :
                 {std::tuple{actor, L"bHidden", false}, std::tuple{actor, L"m_bIsActive", true},
                  std::tuple{reactor, L"m_bIsGimmickActive", true}})
            {
                auto* field = target->GetPropertyByNameInChain(name);
                if (!field || !field->IsA<FBoolProperty>() ||
                    static_cast<FBoolProperty*>(field)->GetPropertyValueInContainer(target) != wanted)
                {
                    return false;
                }
            }
            return within_aqua_interaction_radius(actor, pawn, reactor);
        }

        bool alternate_rail_target(UObject* actor, UObject* reactor, UObject* pawn)
        {
            auto* rail_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, L"/Script/TresGame.TresGimmickRailSlideExStartActor");
            if (!character_traversal::rail_ready(pawn) || !is_uobject_valid_fast(rail_class) ||
                !is_uobject_valid_fast(actor) || !is_uobject_a_fast(actor, rail_class) ||
                !is_uobject_valid_fast(reactor) || object_field(actor, L"MyReactor") != reactor)
            {
                return false;
            }
            auto* owning_world = object_field(actor->GetOuterPrivate(), L"OwningWorld");
            auto* command = reactor->GetPropertyByNameInChain(L"m_Command");
            if (!owning_world || owning_world->GetFullName() != world() || !command ||
                command->GetElementSize() != sizeof(uint8_t) ||
                *command->ContainerPtrToValuePtr<uint8_t>(reactor) != rail_reactor_command)
            {
                return false;
            }
            for (const auto& [target, name, wanted] :
                 {std::tuple{actor, L"bHidden", false}, std::tuple{actor, L"m_bIsActive", true},
                  std::tuple{reactor, L"m_bIsGimmickActive", true}})
            {
                auto* field = target->GetPropertyByNameInChain(name);
                if (!field || !field->IsA<FBoolProperty>() ||
                    static_cast<FBoolProperty*>(field)->GetPropertyValueInContainer(target) != wanted)
                {
                    return false;
                }
            }
            return within_aqua_interaction_radius(actor, pawn, reactor);
        }

        bool dispatch_aqua_interaction(UObject* pawn, int32_t command, UObject* actor,
                                       UObject* selected_reactor = nullptr)
        {
            // The shipping player implements TresStateQueueInterface at +450.
            // Its command dispatcher validates the action, retires the old queue
            // and creates the native SavePoint, OpenTreasureBox or Check state.
            // Use that complete route rather than manufacturing a state or
            // changing the possessed character. Fail closed on other builds.
            const bool alternate = character_swap::current_hero() == pawn && character_traversal::ready(pawn);
            if ((command != save_point_action_command && command != kh02_chest_interaction::open_chest_command &&
                 command != check_action_command && command != ship_action_command && command != shop_action_command &&
                 command != rail_action_command) ||
                (player_id(pawn) != aqua_player_id && !alternate) || !is_uobject_valid_fast(actor))
            {
                return false;
            }
            const auto image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (!character_native::verify_sites(
                    {character_native::Site::action_dispatch, character_native::Site::open_treasure_queue}))
            {
                return false;
            }
            auto* queue_interface =
                reinterpret_cast<std::byte*>(pawn) + character_native::layout::player_state_interface_offset;
            auto* table = field<uintptr_t*>(queue_interface, 0);
            if (!table ||
                field<uintptr_t>(table, 0x628) !=
                    image + character_native::rva(character_native::Site::action_dispatch) ||
                field<uintptr_t>(table, 0x640) !=
                    image + character_native::rva(character_native::Site::open_treasure_queue) ||
                field<UObject*>(queue_interface, 0x150) != object_field(pawn, L"MyStateComp"))
            {
                return false;
            }
            using Dispatch = bool (*)(void*, int32_t, UObject*, UObject*);
            return reinterpret_cast<Dispatch>(image + character_native::rva(character_native::Site::action_dispatch))(
                queue_interface, command, actor,
                selected_reactor
                    ? selected_reactor
                    : object_field(actor, command == check_action_command ? L"TresReactor" : L"MyReactor"));
        }

        void poll_aqua_interaction(Clock::time_point now)
        {
            if (!aqua_chest_observing && !aqua_chest_pending)
            {
                return;
            }
            auto cancel = [&](const wchar_t* reason)
            {
                aqua_chest_observing = false;
                aqua_chest_pending = false;
                aqua_collectible_pending = false;
                config.set_phase(reason);
                config.defer_character_restore(now + character_restore_initial_delay);
            };
            try
            {
                if (world() != aqua_chest_world || now - aqua_chest_started > aqua_chest_timeout)
                {
                    cancel(L"aqua_chest_canceled");
                    return;
                }
                auto* pawn = player();
                if (aqua_chest_observing)
                {
                    auto* controller = object_field(pawn, L"Controller");
                    if (pawn != aqua_chest_pawn.Get() || controller != aqua_chest_controller.Get() ||
                        (player_id(pawn) != aqua_player_id && character_swap::current_hero() != pawn))
                    {
                        cancel(L"aqua_chest_context_changed");
                        return;
                    }
                    auto* command_info = object_field(controller, L"m_pUICommandInfo");
                    auto* chest = object_field(command_info, L"m_Actor");
                    auto* component = object_field(command_info, L"m_Component");
                    const int32_t ship_command = alternate_ship_command(chest, component, pawn);
                    const bool barrel = character_barrel_spin::selected_barrel(chest, component, pawn);
                    const bool ingredient = alternate_ingredient_target(chest, component, pawn);
                    const bool shop = alternate_shop_target(chest, component, pawn);
                    const bool simple_check = alternate_simple_check_target(chest, component, pawn);
                    const bool rail = alternate_rail_target(chest, component, pawn);
                    if (character_swap::current_hero() == pawn && !camp_is_kh02_world(world()) &&
                        !experimental_garden_portal(chest, pawn) && !chest_eligible(chest, pawn) && !ship_command &&
                        !barrel && !ingredient && !shop && !simple_check && !rail)
                    {
                        cancel(L"alternate_portal_not_selected");
                        return;
                    }
                    const bool collectible = check_target_eligible(chest, pawn);
                    const bool mirror = check_target_eligible(chest, pawn, true);
                    const bool check_target = collectible || mirror || ingredient || simple_check;
                    auto* selected_reactor = ship_command || barrel || shop || simple_check || rail
                                                 ? component
                                                 : object_field(chest, check_target ? L"TresReactor" : L"MyReactor");
                    if ((!ship_command && !barrel && !shop && !rail && !check_target && !chest_eligible(chest, pawn)) ||
                        !selected_reactor || object_field(command_info, L"m_Component") != selected_reactor)
                    {
                        cancel(L"aqua_interaction_not_supported");
                        return;
                    }
                    auto* state = pawn->GetFunctionByNameInChain(L"GetStateID");
                    if (!is_uobject_valid_fast(state) || state->GetParmsSize() != 1)
                    {
                        cancel(L"aqua_chest_state_layout");
                        return;
                    }
                    uint8_t state_id{};
                    pawn->ProcessEvent(state, &state_id);
                    auto* instance = object_field(viewport(), L"GameInstance");
                    // GetPlayActionCommand describes an executing state, not
                    // the selected reactor after Aqua's failed transition.
                    if (!kh02_chest_interaction::accepts_player_state(state_id) || !instance ||
                        field<int32_t>(instance, 0x8d0) != 0)
                    {
                        cancel(L"aqua_chest_state_busy");
                        return;
                    }
                    // Aqua's real press selected this reactor but did not open
                    // it. Dispatch her native state using the same selection.
                    if (!loading_closed() || config.camp_ready() || save_open())
                    {
                        return;
                    }
                    aqua_chest = make_uobject_weak(chest);
                    aqua_chest_observing = false;
                    aqua_collectible_pending = collectible;
                    int32_t action_command = kh02_chest_interaction::open_chest_command;
                    if (rail)
                    {
                        action_command = rail_action_command;
                    }
                    else if (shop)
                    {
                        action_command = shop_action_command;
                    }
                    else if (ship_command)
                    {
                        action_command = ship_command;
                    }
                    else if (check_target || barrel)
                    {
                        action_command = check_action_command;
                    }
                    if (!dispatch_aqua_interaction(pawn, action_command, chest, selected_reactor))
                    {
                        cancel(L"aqua_interaction_command_rejected");
                        return;
                    }
                    if (mirror || experimental_garden_portal(chest, pawn) || ship_command || barrel || ingredient || shop || simple_check || rail)
                    {
                        // The selected mirror's native Check state owns its
                        // reaction, puzzle changes and map/cutscene transition.
                        // Unlike a pickup it remains alive after completion.
                        const wchar_t* completed_phase = L"checking_portal_with_alternate";
                        if (rail)
                        {
                            completed_phase = L"boarding_rail_with_alternate";
                        }
                        else if (simple_check)
                        {
                            completed_phase = L"checking_reactor_with_alternate";
                        }
                        else if (shop)
                        {
                            completed_phase = L"opening_shop_with_alternate";
                        }
                        else if (ingredient)
                        {
                            completed_phase = L"gathering_ingredient_with_alternate";
                        }
                        else if (barrel)
                        {
                            completed_phase = L"performing_barrel_with_alternate";
                        }
                        else if (mirror)
                        {
                            completed_phase = L"checking_mirror_with_aqua";
                        }
                        cancel(completed_phase);
                        return;
                    }
                    if (is_fake_chest(chest))
                    {
                        // The native opening state runs the decoy's puzzle
                        // reaction. It will never produce a journal completion.
                        cancel(L"opening_fake_chest_with_aqua");
                        return;
                    }
                    aqua_chest_pending = true;
                    aqua_chest_dispatched_at = now;
                    config.set_phase(collectible ? L"checking_collectible_with_aqua" : L"opening_chest_with_aqua");
                    return;
                }
                auto* chest = aqua_chest.Get();
                if (pawn != aqua_chest_pawn.Get() || object_field(pawn, L"Controller") != aqua_chest_controller.Get() ||
                    (player_id(pawn) != aqua_player_id && character_swap::current_hero() != pawn))
                {
                    cancel(L"aqua_chest_context_changed");
                    return;
                }
                if (aqua_collectible_pending && !is_uobject_valid_fast(chest))
                {
                    ++aqua_collectibles_collected;
                    aqua_collectible_pending = false;
                    cancel(L"aqua_collectible_collected");
                    return;
                }
                if (aqua_collectible_pending)
                {
                    // The original pickup owns its effect and actor retirement.
                    // It has no chest journal entry; wait for that native finish.
                    return;
                }
                if (!is_uobject_valid_fast(chest))
                {
                    cancel(L"aqua_chest_unloaded");
                    return;
                }
                if (!loading_closed() || config.camp_ready() || save_open())
                {
                    return;
                }
                auto* state = pawn->GetFunctionByNameInChain(L"GetStateID");
                auto* instance = object_field(viewport(), L"GameInstance");
                if (!is_uobject_valid_fast(state) || state->GetParmsSize() != 1 || !instance ||
                    field<int32_t>(instance, 0x8d0) != 0)
                {
                    return;
                }
                uint8_t state_id{};
                pawn->ProcessEvent(state, &state_id);
                if (!kh02_chest_interaction::accepts_player_state(state_id))
                {
                    return;
                }
                if (now - aqua_chest_dispatched_at < aqua_chest_reconcile_delay ||
                    !reconcile_chest_record(chest, false))
                {
                    return;
                }
                ++aqua_chests_opened;
                cancel(L"aqua_chest_opened");
            }
            catch (const std::exception& failure)
            {
                const std::string message = failure.what();
                Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] Aqua interaction failed: {}\n"),
                                                std::wstring(message.begin(), message.end()));
                cancel(L"aqua_interaction_failed");
            }
            catch (...)
            {
                cancel(L"aqua_interaction_failed");
            }
        }

        bool aqua_save_key(int controller, std::wstring_view key, int event)
        {
            if (key != L"Gamepad_FaceButton_Top" && key != L"F")
            {
                return false;
            }
            return save_input.route(
                controller, std::wstring(key), event,
                [&]
                {
                    if (event == 1)
                    {
                        aqua_save_released = true;
                        return true;
                    }
                    auto* pawn = player();
                    const bool alternate =
                        pawn && character_swap::current_hero() == pawn && character_traversal::ready(pawn);
                    const bool kh02_aqua = player_id(pawn) == aqua_player_id && camp_is_kh02_world(world()) &&
                                           Kh02Story::challenges_available();
                    if (event != 0 || aqua_save_pending || aqua_chest_pending || config.navigation_pending() ||
                        (!alternate && !kh02_aqua) || !loading_closed() || config.camp_ready() || save_open() ||
                        Kh02Story::challenges_open())
                    {
                        return false;
                    }
                    auto* instance = object_field(viewport(), L"GameInstance");
                    // Same supported-build native pause discriminator used by the
                    // UI bridge: never turn a cutscene/menu Triangle into Save.
                    if (!instance || field<int32_t>(instance, 0x8d0) != 0)
                    {
                        return false;
                    }
                    auto* point = current_save_point(pawn);
                    if (!point)
                    {
                        aqua_chest_pawn = make_uobject_weak(pawn);
                        aqua_chest_controller = make_uobject_weak(object_field(pawn, L"Controller"));
                        aqua_chest_world = world();
                        aqua_chest_started = Clock::now();
                        aqua_chest_observing = true;
                        return false;
                    }
                    // Keep the possessed character and dispatch its native save state
                    // after release, so the press cannot leak into the new menu.
                    aqua_save_point = make_uobject_weak(point);
                    aqua_save_world = world();
                    aqua_save_pawn = make_uobject_weak(pawn);
                    aqua_save_controller = make_uobject_weak(object_field(pawn, L"Controller"));
                    aqua_save_pending = true;
                    aqua_save_released = false;
                    aqua_save_dispatched = false;
                    aqua_save_started = Clock::now();
                    config.select_aqua();
                    config.set_phase(L"waiting_for_aqua_save_release");
                    return true;
                });
        }

        void poll_aqua_save(Clock::time_point now)
        {

            if (!aqua_save_pending)
            {
                return;
            }
            if (world() != aqua_save_world || now - aqua_save_started > aqua_save_timeout ||
                !is_uobject_valid_fast(aqua_save_point.Get()))
            {
                aqua_save_pending = false;
                config.set_phase(L"aqua_save_canceled");
                return;
            }
            if (!aqua_save_released || !loading_closed())
            {
                return;
            }
            auto* pawn = player();
            const bool alternate = character_swap::owns_swap_pawn(pawn) && character_traversal::ready(pawn);
            if ((player_id(pawn) != aqua_player_id && !alternate) || pawn != aqua_save_pawn.Get() ||
                object_field(pawn, L"Controller") != aqua_save_controller.Get())
            {
                aqua_save_pending = false;
                config.set_phase(L"aqua_save_context_changed");
                return;
            }
            if (save_open())
            {
                ++aqua_saves_opened;
                aqua_save_pending = false;
                config.set_phase(L"aqua_save_opened");
                return;
            }
            if (!aqua_save_dispatched)
            {
                auto* state = pawn->GetFunctionByNameInChain(L"GetStateID");
                auto* instance = object_field(viewport(), L"GameInstance");
                if (!is_uobject_valid_fast(state) || state->GetParmsSize() != 1 || !instance ||
                    field<int32_t>(instance, 0x8d0) != 0 || config.camp_ready() || Kh02Story::challenges_open() ||
                    current_save_point(pawn) != aqua_save_point.Get())
                {
                    aqua_save_pending = false;
                    config.set_phase(L"aqua_save_not_idle");
                    return;
                }
                uint8_t state_id{};
                pawn->ProcessEvent(state, &state_id);
                if ((state_id != idle_player_state && state_id != running_player_state) ||
                    !bind_save_command(pawn, aqua_save_point.Get()) ||
                    !dispatch_aqua_interaction(pawn, save_point_action_command, aqua_save_point.Get()))
                {
                    aqua_save_pending = false;
                    config.set_phase(L"aqua_save_command_rejected");
                    return;
                }
                aqua_save_dispatched = true;
                config.set_phase(L"opening_save_with_aqua");
            }
        }
    };

    AlternateInteractionService::AlternateInteractionService(AlternateInteractionConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    AlternateInteractionService::~AlternateInteractionService() = default;

    bool AlternateInteractionService::route_save_key(int controller, std::wstring_view key, int event)
    {
        return impl_->aqua_save_key(controller, key, event);
    }

    void AlternateInteractionService::tick_save(Clock::time_point now)
    {
        impl_->poll_aqua_save(now);
    }

    void AlternateInteractionService::tick_interaction(Clock::time_point now)
    {
        impl_->poll_aqua_interaction(now);
    }

    bool AlternateInteractionService::save_pending() const
    {
        return impl_->aqua_save_pending;
    }

    bool AlternateInteractionService::chest_pending() const
    {
        return impl_->aqua_chest_pending;
    }

    UObject* AlternateInteractionService::current_save_point(UObject* pawn)
    {
        return impl_->current_save_point(pawn);
    }

    int32_t AlternateInteractionService::selected_ship_command(UObject* actor, UObject* reactor, UObject* pawn,
                                                               bool require_active)
    {
        return impl_->alternate_ship_command(actor, reactor, pawn, require_active);
    }

    AlternateInteractionSnapshot AlternateInteractionService::snapshot() const
    {
        return {impl_->aqua_save_pending,           impl_->aqua_chest_pending,          impl_->aqua_chest_observing,
                impl_->aqua_collectible_pending,    impl_->aqua_saves_opened,           impl_->aqua_chests_opened,
                impl_->aqua_collectibles_collected, impl_->mirror_interaction_rejection};
    }
} // namespace kh3ap::garden_return_detail
