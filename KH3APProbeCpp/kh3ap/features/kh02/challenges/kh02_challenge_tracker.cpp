#include "kh02_challenge_player_state_observer.h"
#include "kh02_damage_event_decoder.h"
#include "kh02_combat_event_journal.h"
#include "kh02_freeze_observer.h"
#include "kh02_enemy_weakness.h"
#include "kh02_challenge_actor_state.h"
#include "kh02_challenge_ids.h"
#include "kh02_challenge_hooks.h"
#include "kh3ap/features/kh02/core/kh02_data_table.h"
#include "kh02_challenge_tracker.h"
#include "kh02_challenge_policy.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/core/events/character_level_events.h"
#include "kh3ap/core/objects/actor_discovery.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/World.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <nlohmann/json.hpp>
#define NOMINMAX
#include <Windows.h>
#include <chrono>
#include <vector>
#include <set>
#include <stdexcept>
#include <cstring>
#include <atomic>
#include <utility>

extern "C" bool kh02_table_rows_visit(void*, void (*)(unsigned long long, void*, void*), void*);

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr int excellent_shotlock_grade = 3;

        uint64_t weak_key(const FWeakObjectPtr& weak)
        {
            uint64_t key{};
            static_assert(sizeof(weak) == 8);
            std::memcpy(&key, &weak, sizeof(key));
            return key;
        }

        auto owning_world(UObject* actor) -> UObject*
        {
            if (!is_uobject_valid_fast(actor))
            {
                return nullptr;
            }
            auto* world = try_read_reflected_object_property_value(actor->GetOuterPrivate(), L"OwningWorld");
            return is_uobject_valid_fast(world) ? world : static_cast<AActor*>(actor)->GetWorld();
        }
    } // namespace

    struct Kh02ChallengeTracker::Impl
    {
        using Clock = std::chrono::steady_clock;
        std::map<int, int>& states;
        std::map<int, int>& counts;
        bool fully_equipped{}, pending_change{};
        Kh02ChallengePlayerStateObserver player_state;
        uint64_t shotlock_results{};

        Kh02FreezeObserver freeze_observer;
        std::map<int, kh02_tracking::NoDamageAttempt> no_damage;
        FWeakObjectPtr pawn, world;
        uint64_t revision = UINT64_MAX;
        std::atomic<DWORD> thread{};

        using Journal = Kh02CombatEventJournal<FWeakObjectPtr>;
        Journal journal;
        uint64_t observed_level_revision{UINT64_MAX}, level_reads{};
        uint64_t kill_events{}, damage_events{}, attributed{}, discarded{}, native_damage_calls{}, typed_damage_calls{};
        int last_hp{-1};
        float last_damage{};
        std::string error, last_enemy, last_attack;

        // Destroy hook registrations before any tracker state can be retired.
        Kh02ChallengeHooks hooks{
            {this,
             [](void* context, UObject* victim, float amount, const void* event, UObject* by, UObject* causer,
                Kh02ChallengeHooks::DamageFunction original)
             { return static_cast<Impl*>(context)->take_damage(victim, amount, event, by, causer, original); },
             [](void* context, UObject* enemy, int32_t amount) noexcept
             { static_cast<Impl*>(context)->killed(enemy, amount); },
             [](void* context, void* component, void* state) noexcept
             { static_cast<Impl*>(context)->state_changed(component, state); },
             [](void* context, UObject* state) { return static_cast<Impl*>(context)->observes_shotlock(state); },
             [](void* context, int grade) { static_cast<Impl*>(context)->shotlock_result(grade); }}};

        Impl(std::map<int, int>& s, std::map<int, int>& c) : states(s), counts(c) {}

        ~Impl()
        {
            hooks.stop();
        }

        bool active(UObject* actor)
        {
            return is_uobject_valid_fast(pawn.Get()) && is_uobject_valid_fast(world.Get()) &&
                   owning_world(pawn.Get()) == world.Get() && owning_world(actor) == world.Get();
        }

        void state_changed(void* component, void* next) noexcept
        {
            auto* self = this;
            if (self->thread != GetCurrentThreadId() || !is_uobject_valid_fast(self->pawn.Get()))
            {
                return;
            }
            try
            {
                if (component != try_read_reflected_object_property_value(self->pawn.Get(), L"MyStateComp") ||
                    !self->active(self->pawn.Get()))
                {
                    return;
                }
                auto* state = static_cast<UObject*>(next);
                const auto accepted = self->player_state.accept(state);
                if (!accepted)
                {
                    return;
                }
                const auto& name = *accepted;
                const bool aqua = self->pawn.Get()->GetClassPrivate()->GetName() == L"p_ex002_Pawn_C";
                if (aqua && name == L"TresPlayerState_StyleFinish")
                {
                    bool phantom_battle = false;
                    auto* phantom_class = UObjectGlobals::StaticFindObject<UClass*>(
                        nullptr, nullptr, L"/Script/TresGame.TresEnemyPawn_e_dw405");
                    if (self->states[kh02_challenge_id::finish_magic_wish_enemy_aqua] !=
                            kh02_challenge_state::complete &&
                        is_uobject_valid_fast(phantom_class))
                    {
                        for (auto* enemy : find_live_actors_of_class(self->pawn.Get(), phantom_class, 8))
                        {
                            if (!self->active(enemy))
                            {
                                continue;
                            }
                            Kh02ReflectedCall hp(enemy, L"GetHitPoint", "kh02_challenge", 16);
                            hp.run();
                            auto* value = hp.param(L"ReturnValue");
                            if (!value || value->GetClass().GetName() != L"IntProperty" || value->GetElementSize() != 4)
                            {
                                throw std::runtime_error("The Phantom Aqua health query has an unsupported result.");
                            }
                            phantom_battle |= *value->ContainerPtrToValuePtr<int>(hp.parameters.data()) > 0;
                        }
                    }
                    self->pending_change |= kh02_tracking::spellweaver_activation(self->states, self->counts,
                                                                                  self->fully_equipped, phantom_battle);
                }
                auto* state_id = is_uobject_valid_fast(state) ? state->GetPropertyByNameInChain(L"MyStateID") : nullptr;
                if (state_id && state_id->GetElementSize() == 1 &&
                    *state_id->ContainerPtrToValuePtr<uint8_t>(state) == kh02_tracking::mickey_link_state_id)
                {
                    self->pending_change |=
                        kh02_tracking::add(self->states, self->counts, kh02_challenge_id::coop_action);
                }
            }
            catch (const std::exception& exception)
            {
                self->error = exception.what();
            }
        }

        bool observes_shotlock(UObject* state)
        {
            return thread == GetCurrentThreadId() && is_uobject_valid_fast(state) && active(pawn.Get()) &&
                   kh02_challenge_current_state(pawn.Get()) == state;
        }

        void shotlock_result(int grade)
        {
            ++shotlock_results;
            pending_change |= kh02_tracking::shotlock_result(states, counts, grade == excellent_shotlock_grade);
        }

        void observe_freeze(UObject* enemy, uint64_t identity, int element, bool was_frozen)
        {
            freeze_observer.observe(enemy, identity, element, was_frozen, states, counts, pending_change);
        }

        void killed(UObject* enemy, int32_t amount) noexcept
        {
            auto* self = this;
            if (self->thread != GetCurrentThreadId() || amount != 1)
            {
                return;
            }
            try
            {
                if (!self->active(enemy))
                {
                    return;
                }
                const auto type = enemy->GetClassPrivate()->GetName();
                if (!kh02_tracking::heartless(type) && !type.starts_with(L"e_dw405_Pawn_"))
                {
                    return;
                }
                const auto weak = make_uobject_weak(enemy);
                const auto identity = weak_key(weak);
                if (!is_uobject_valid_fast(weak.Get()))
                {
                    return;
                }
                const auto observation = self->journal.defeat(weak, identity, type, Clock::now());
                if (observation == Journal::Observation::ignored)
                {
                    return;
                }
                if (observation == Journal::Observation::overflow)
                {
                    ++self->discarded;
                    return;
                }
                ++self->kill_events;
            }
            catch (...)
            {
                ++self->discarded;
            }
        }

        float take_damage(UObject* victim, float amount, const void* event, UObject* by, UObject* causer,
                          Kh02ChallengeHooks::DamageFunction original)
        {
            auto* self = this;
            bool observe = self->thread == GetCurrentThreadId();
            FWeakObjectPtr weak{};
            kh02_tracking::Hit hit;
            bool player_victim = false, typed = false;
            bool frozen = false;
            bool weakness = false;
            std::wstring victim_type;
            if (observe)
            {
                try
                {
                    observe = self->active(victim) && !self->journal.saw_defeat(weak_key(make_uobject_weak(victim)));
                    if (observe)
                    {
                        ++self->native_damage_calls;
                        weak = make_uobject_weak(victim);
                        victim_type = victim->GetClassPrivate()->GetName();
                        player_victim = victim == self->pawn.Get();
                        auto* controller = try_read_reflected_object_property_value(self->pawn.Get(), L"Controller");
                        hit.player =
                            (is_uobject_valid_fast(controller) && by == controller) || causer == self->pawn.Get();
                        if (const auto metadata = kh02_decode_damage_event(event))
                        {
                            const bool aqua = self->pawn.Get()->GetClassPrivate()->GetName() == L"p_ex002_Pawn_C";
                            const auto classified = kh02_classify_damage_event(*metadata, aqua);
                            hit.element = metadata->element;
                            hit.magic = classified.magic;
                            hit.counter = classified.counter;
                            hit.style = classified.style;
                            hit.spellweaver = classified.spellweaver;
                            weakness = hit.player && hit.magic && kh02_enemy_has_weakness(victim, hit.element);
                            auto* state = kh02_challenge_current_state(victim);
                            frozen = is_uobject_valid_fast(state) &&
                                     state->GetClassPrivate()->GetName().find(L"BadStatFreeze") != std::wstring::npos;
                            typed = true;
                            ++self->typed_damage_calls;
                            self->last_attack = "attack=" + to_utf8(metadata->attack) +
                                                " command=" + std::to_string(metadata->command) +
                                                " element=" + std::to_string(hit.element) +
                                                " magic=" + std::to_string(hit.magic) +
                                                " player=" + std::to_string(hit.player);
                        }
                        if (hit.player)
                        {
                            auto* fn = self->pawn.Get()->GetFunctionByNameInChain(L"IsOnGround");
                            if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() != 1)
                            {
                                throw std::runtime_error("kh02_challenge_ground_signature");
                            }
                            uint8_t ground = 1;
                            self->pawn.Get()->ProcessEvent(fn, &ground);
                            hit.airborne = !ground;
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    self->error = e.what();
                    observe = false;
                }
            }
            const float result = original(victim, amount, event, by, causer);
            if (observe)
            {
                try
                {
                    if (player_victim)
                    {
                        for (auto& [id, attempt] : self->no_damage)
                        {
                            attempt.damage(result);
                        }
                        return result;
                    }
                    const auto identity = weak_key(weak);
                    // A kill is authoritative only after the original damage call
                    // caused the game's defeat-counter update for this weak identity.
                    hit.lethal = self->journal.saw_defeat(identity);
                    if (typed && hit.player)
                    {
                        if (kh02_tracking::heartless(victim_type) && (result > 0 || hit.lethal || hit.element == 2))
                        {
                            self->observe_freeze(victim, identity, hit.element, frozen);
                        }
                        if (weakness && result > 0)
                        {
                            self->pending_change |=
                                kh02_tracking::add(self->states, self->counts, kh02_challenge_id::weak_point_attack);
                        }
                    }
                    if (!typed)
                    {
                        hit.magic = false;
                        hit.airborne = false;
                    }
                    const auto observation = self->journal.damage(weak, identity, hit, result, Clock::now());
                    if (result > 0 || hit.lethal)
                    {
                        if (observation == Journal::Observation::accepted)
                        {
                            ++self->damage_events;
                        }
                        else if (observation == Journal::Observation::overflow)
                        {
                            ++self->discarded;
                        }
                        self->last_damage = result;
                        self->last_hp = hit.lethal ? 0 : -1;
                    }
                }
                catch (...)
                {
                    ++self->discarded;
                }
            }
            return result;
        }

        void bind(UObject* p, UObject* w, uint64_t r)
        {
            thread = GetCurrentThreadId();
            hooks.install(error);
            if (world.Get() != w || pawn.Get() != p || revision != r)
            {
                journal.clear();
                no_damage.clear();
                player_state.reset(counts[kh02_challenge_id::blizaga_rail_slide]);
                freeze_observer.reset();
                pending_change = false;
                observed_level_revision = UINT64_MAX;
            }
            pawn = make_uobject_weak(p);
            world = make_uobject_weak(w);
            revision = r;
        }

        bool tick()
        {
            if (!active(pawn.Get()))
            {
                return false;
            }
            const auto now = Clock::now();
            bool changed = std::exchange(pending_change, false);
            freeze_observer.tick();
            player_state.resolve_pending_rail();
            if (player_state.sliding() &&
                states[kh02_challenge_id::blizaga_rail_slide] != kh02_challenge_state::complete)
            {
                const int distance = player_state.sample(pawn.Get());
                if (distance > counts[kh02_challenge_id::blizaga_rail_slide])
                {
                    counts[kh02_challenge_id::blizaga_rail_slide] = distance;
                    states[kh02_challenge_id::blizaga_rail_slide] = distance == kh02_tracking::ice_slide_goal_meters
                                                                        ? kh02_challenge_state::complete
                                                                        : kh02_challenge_state::in_progress;
                    changed = true;
                }
            }
            for (const auto& death : journal.deaths())
            {
                const auto hit = journal.match(death);
                if (hit.player && hit.lethal)
                {
                    ++attributed;
                }
                changed |= kh02_tracking::defeat(states, counts, death.type, hit);
                last_enemy = to_utf8(death.type);
            }
            journal.retire_frame(now);
            const auto level_revision = character_level_revision.load(std::memory_order_acquire);
            if (observed_level_revision != level_revision)
            {
                observed_level_revision = level_revision;
                try
                {
                    auto* fn = pawn.Get()->GetFunctionByNameInChain(L"GetChrLevel");
                    if (!is_uobject_valid_fast(fn) || fn->GetParmsSize() != 4)
                    {
                        throw std::runtime_error("kh02_challenge_level_signature");
                    }
                    int value = 0;
                    pawn.Get()->ProcessEvent(fn, &value);
                    ++level_reads;
                    changed |= kh02_tracking::level(states, counts, value);
                }
                catch (const std::exception& e)
                {
                    error = e.what();
                }
            }
            return changed;
        }

        bool event(int id, bool start, std::wstring_view caller)
        {
            if (!kh02_tracking::no_damage_site(id, start, caller) || !active(pawn.Get()))
            {
                return false;
            }
            // Audited original start/end sites define the attempt. A stored
            // victory flag or an actor disappearing cannot establish no damage.
            if (!hooks.damage_installed())
            {
                return false;
            }
            auto& attempt = no_damage[id];
            if (start)
            {
                attempt.begin();
                return false;
            }
            if (attempt.finish() && states[id] != kh02_challenge_state::complete)
            {
                states[id] = kh02_challenge_state::complete;
                return true;
            }
            return false;
        }
    };

    Kh02ChallengeTracker::Kh02ChallengeTracker(std::map<int, int>& s, std::map<int, int>& c)
        : impl(std::make_unique<Impl>(s, c))
    {
    }

    Kh02ChallengeTracker::~Kh02ChallengeTracker() = default;

    void Kh02ChallengeTracker::bind(UObject* p, UObject* w, uint64_t r)
    {
        impl->bind(p, w, r);
    }

    bool Kh02ChallengeTracker::tick()
    {
        return impl->tick();
    }

    void Kh02ChallengeTracker::wardrobe(bool fully_equipped)
    {
        impl->fully_equipped = fully_equipped;
    }

    bool Kh02ChallengeTracker::team_finish(int id)
    {
        if (id != kh02_challenge_id::finish_coop_action_devil_tower || !impl->active(impl->pawn.Get()))
        {
            return false;
        }
        // The original TryClear(49) is a victory-site query of the current
        // Mickey/Aqua link state, independent of who landed its final hit.
        auto* state = kh02_challenge_current_state(impl->pawn.Get());
        auto* field = is_uobject_valid_fast(state) ? state->GetPropertyByNameInChain(L"MyStateID") : nullptr;
        if (!field || field->GetClass().GetName() != L"ByteProperty" || field->GetElementSize() != 1)
        {
            return false;
        }
        const int state_id = *field->ContainerPtrToValuePtr<uint8_t>(state);
        return kh02_tracking::mickey_link_state(state_id) && kh02_tracking::complete(impl->states, id);
    }

    bool Kh02ChallengeTracker::event(int id, bool start, std::wstring_view caller)
    {
        return impl->event(id, start, caller);
    }

    std::string Kh02ChallengeTracker::snapshot() const
    {
        nlohmann::json attempts = nlohmann::json::object();
        for (const auto& [id, attempt] : impl->no_damage)
        {
            attempts[std::to_string(id)] = {{"armed", attempt.armed}, {"hurt", attempt.hurt}};
        }
        return nlohmann::json{{"damage_hook", impl->hooks.damage_installed()},
                              {"shotlock_hook", impl->hooks.shotlock_installed()},
                              {"state_events", impl->player_state.event_count()},
                              {"shotlock_results", impl->shotlock_results},
                              {"last_state", impl->player_state.state_name()},
                              {"last_rail_actor", impl->player_state.rail_name()},
                              {"ice_slide_active", impl->player_state.sliding()},
                              {"ice_slide_cm", impl->player_state.slide_centimeters()},
                              {"freeze_groups", impl->freeze_observer.group_count()},
                              {"level_reads", impl->level_reads},
                              {"level_revision", impl->observed_level_revision},
                              {"native_damage_calls", impl->native_damage_calls},
                              {"typed_damage_calls", impl->typed_damage_calls},
                              {"native_defeat_events", impl->kill_events},
                              {"damage_events", impl->damage_events},
                              {"player_attributed", impl->attributed},
                              {"discarded", impl->discarded},
                              {"last_enemy", impl->last_enemy},
                              {"last_attack", impl->last_attack},
                              {"last_hit_hp", impl->last_hp},
                              {"last_hit_damage", impl->last_damage},
                              {"no_damage_attempts", attempts},
                              {"error", impl->error}}
            .dump();
    }
} // namespace kh3ap
