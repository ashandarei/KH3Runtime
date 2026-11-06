#include "kh3ap/features/player/characters/character_party.h"
#include "kh3ap/features/player/combat/combat_blackboard_handoff.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/features/party/party_helpers.h"
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
#include <DynamicOutput/DynamicOutput.hpp>
#endif
#include <stdexcept>
#include <utility>

namespace kh3ap::character_party
{
    using namespace RC::Unreal;
    namespace
    {
        constexpr size_t friend_count = 6;
        bool active{};
        FWeakObjectPtr active_world;
        Observer observer;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        void trace(const wchar_t* stage, const std::array<FWeakObjectPtr, friend_count>& friends)
        {
            size_t alive{};
            for (const auto& pawn : friends) { alive += pawn.Get() != nullptr; }
            RC::Output::send<RC::LogLevel::Normal>(STR("[KH3APProbeCpp] CharacterParty stage={} alive={}\n"), stage, alive);
        }
#endif

        template <typename T> T *slots(UObject *manager, const wchar_t *name, const wchar_t *kind)
        {
            auto *field = manager->GetPropertyByNameInChain(name);
            if (!field || field->GetClass().GetName() != kind ||
                field->GetElementSize() != sizeof(T) || field->GetArrayDim() != friend_count)
            {
                throw std::runtime_error("character_party_slot_layout");
            }
            return field->ContainerPtrToValuePtr<T>(manager);
        }
    } // namespace

    void set_observer(Observer value)
    {
        observer = std::move(value);
    }
    bool transferring()
    {
        return active && active_world.Get() == kh02_current_world() && !kh02_current_story_world();
    }

    Handoff::Handoff(UObject *outgoing)
    {
        // KH0.2 retains its original solo/scripted-companion ownership.
        if (kh02_current_story_world())
        {
            return;
        }
        auto *manager = party_native::read_object_like_property(outgoing, L"MyFrdMgr");
        if (!party_native::is_friend_component(manager) || active)
        {
            throw std::runtime_error("character_party_source_manager");
        }
        auto *pawns = slots<FWeakObjectPtr>(manager, L"m_pFriendPawn", L"WeakObjectProperty");
        auto **defaults = slots<UObject *>(manager, L"m_FriendPawnBPDefault", L"ClassProperty");
        auto **classes = slots<UObject *>(manager, L"m_FriendPawnBP", L"ClassProperty");
        source_manager_ = make_uobject_weak(manager);
        source_world_ = make_uobject_weak(kh02_actor_world(outgoing));
        if (!source_manager_.Get() || !source_world_.Get())
        {
            throw std::runtime_error("character_party_source_lifetime");
        }
        for (size_t index = 0; index < friend_count; ++index)
        {
            auto *pawn = pawns[index].Get();
            if (pawn && (kh02_actor_world(pawn) != source_world_.Get() ||
                         party_native::read_object_like_property(pawn, L"m_pFriendMan") != manager))
            {
                throw std::runtime_error("character_party_friend_owner");
            }
            friends_[index] = pawns[index];
            expected_friends_[index] = pawn != nullptr;
            default_classes_[index] = make_uobject_weak(defaults[index]);
            classes_[index] = make_uobject_weak(classes[index]);
            if (pawn)
            {
                combat_blackboard_handoff::capture(pawn, outgoing);
            }
        }
        combat_blackboard_handoff::suspend();
        // EndPlay retires the native manager's slots. Move these references out
        // until the swap publishes its incoming local possession. The existing
        // transaction deadline and map cancellation bound this pending work.
        active = true;
        active_world = source_world_;
        detached_ = true;
        for (size_t index = 0; index < friend_count; ++index)
        {
            pawns[index] = {};
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        trace(L"detached", friends_);
#endif
    }

    Handoff::~Handoff()
    {
        if (!detached_)
        {
            return;
        }
        try
        {
            if (auto *manager = source_manager_.Get())
            {
                auto *pawns =
                    slots<FWeakObjectPtr>(manager, L"m_pFriendPawn", L"WeakObjectProperty");
                for (size_t index = 0; index < friend_count; ++index)
                {
                    pawns[index] = friends_[index];
                }
            }
        }
        catch (...)
        { /* Never throw while unwinding a failed native factory. */
        }
        active = false;
    }

    void Handoff::finish(UObject *incoming)
    {
        if (!detached_)
        {
            return;
        }
        auto *manager = party_native::read_object_like_property(incoming, L"MyFrdMgr");
        if (!party_native::is_friend_component(manager) ||
            kh02_actor_world(incoming) != source_world_.Get() || kh02_current_story_world())
        {
            throw std::runtime_error("character_party_destination_manager");
        }
        auto *pawns = slots<FWeakObjectPtr>(manager, L"m_pFriendPawn", L"WeakObjectProperty");
        auto **defaults = slots<UObject *>(manager, L"m_FriendPawnBPDefault", L"ClassProperty");
        auto **classes = slots<UObject *>(manager, L"m_FriendPawnBP", L"ClassProperty");
        for (size_t index = 0; index < friend_count; ++index)
        {
            if (expected_friends_[index] && !friends_[index].Get())
            {
                throw std::runtime_error("character_party_retained_friend_lost");
            }
        }
        auto *controller = party_native::read_object_like_property(incoming, L"Controller");
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        trace(L"factory_returned", friends_);
#endif
        if (!party_native::write_object_like_property(manager, L"m_pController", controller))
        {
            throw std::runtime_error("character_party_controller_rebind");
        }
        // Sora's constructor can create a new default roster on the return.
        // Retire only that replacement roster before adopting the original NPCs.
        if (manager != source_manager_.Get())
        {
            for (size_t index = 0; index < friend_count; ++index)
            {
                auto *pawn = pawns[index].Get();
                for (const auto &retained : friends_)
                {
                    if (pawn && retained.Get() == pawn)
                    {
                        pawns[index] = {};
                        break;
                    }
                }
            }
            bool has_replacements = false;
            for (size_t index = 0; index < friend_count; ++index)
            {
                has_replacements = has_replacements || pawns[index].Get() != nullptr;
            }
            if (has_replacements)
            {
                Kh02ReflectedCall retire(manager, L"DeleteFriendAll", "character party", 64);
                retire.run();
            }
        }
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        trace(L"replacement_retired", friends_);
#endif
        for (size_t index = 0; index < friend_count; ++index)
        {
            auto *pawn = friends_[index].Get();
            if (pawn && !party_native::write_object_like_property(pawn, L"m_pFriendMan", manager))
            {
                throw std::runtime_error("character_party_friend_rebind");
            }
            pawns[index] = friends_[index];
            defaults[index] = default_classes_[index].Get();
            classes[index] = classes_[index].Get();
        }
        if (observer)
        {
            observer(source_manager_.Get(), manager, incoming);
        }
        active = false;
        detached_ = false;
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        trace(L"adopted", friends_);
#endif
    }
} // namespace kh3ap::character_party
