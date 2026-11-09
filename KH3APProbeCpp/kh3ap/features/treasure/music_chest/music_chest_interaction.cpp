#include <array>
#include <memory>
#include <utility>
#include "kh3ap/features/treasure/chest_commands.h"
#include "kh3ap/features/treasure/chest_character_policy.h"
#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/features/treasure/music_chest/music_chest_internal.h"
#include "kh3ap/features/treasure/music_chest/music_chest_pause.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/rewards/reconciliation.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/FFrame.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <algorithm>
#include <cstring>
#include <format>
#include <sstream>
#include <stdexcept>

namespace kh3ap
{
    namespace
    {
        constexpr auto chest_command_reactor_offset = 16;
        constexpr auto chest_command_parameter_count = 3;
        constexpr auto chest_command_parameter_bytes = 24;

        constexpr auto music_chest_notice_duration_seconds = 3.f;
        constexpr auto music_chest_request_lifetime_ms = 15000;
    } // namespace

    using namespace RC::Unreal;
    using namespace music_chest;

    bool RuntimeMusicChestService::Impl::keyblade_owned(UObject* pawn, const Assignment& a)
    {
        // This is chest access only. Do not cache Roxas's exception as inventory
        // ownership or let it survive a return to Sora/another character.
        if (last_snapshot && last_snapshot->roxas_chestsanity &&
            character_swap::owns_swap_pawn(pawn))
        {
            auto* id = pawn->GetPropertyByNameInChain(L"m_PlayerUniqueID");
            if (id && id->GetElementSize() == sizeof(uint8_t) &&
                roxas_chest_key_allowed(true, *id->ContainerPtrToValuePtr<uint8_t>(pawn),
                                       a.keyblade_name))
            {
                return true;
            }
        }
        const auto epoch = reconciliation::epoch();
        if (inventory_pawn.Get() != pawn || inventory_epoch != epoch)
        {
            keyblade_cache.clear();
            inventory_pawn = pawn;
            inventory_epoch = epoch;
        }
        return keyblade_cache
            .get(a.keyblade,
                 [&]
                 {
                     Call have(loaded(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                               L"BP_TresHaveItem");
                     if (!write_object_function_param(have.fn, have.data.data(),
                                                      L"WorldContextObject", pawn) ||
                         !write_name_or_string_function_param(have.field(L"InItemKeyName"),
                                                              have.data.data(), a.keyblade))
                     {
                         throw std::runtime_error("chest_keyblade_inventory_layout");
                     }
                     auto* result = have.field(L"ReturnValue");
                     if (!result->IsA<FBoolProperty>())
                     {
                         throw std::runtime_error("chest_keyblade_inventory_result");
                     }
                     have.run();
                     return static_cast<FBoolProperty*>(result)->GetPropertyValueInContainer(
                         have.data.data());
                 })
            .owned;
    }

    uint8_t RuntimeMusicChestService::Impl::desired_command(UObject* pawn, Gate& gate,
                                                            const Assignment& a)
    {
        if (a.keyblade.empty())
        {
            return a.cleared ? chest_command::open : chest_command::examine;
        }
        try
        {
            gate.keyblade_owned = keyblade_owned(pawn, a);
        }
        catch (...)
        {
            gate.keyblade_owned = false;
            const uint8_t locked = chest_command::examine;
            reactor_command(gate.reactor.Get(), &locked);
            throw;
        }
        return gate.keyblade_owned ? chest_command::open : chest_command::examine;
    }

    void RuntimeMusicChestService::Impl::refresh_keyblade_gates(UObject* pawn)
    {
        if (!is_uobject_valid_fast(pawn) || !config.gameplay_ready() ||
            world(pawn) != current_world.Get())
        {
            return;
        }
        for (auto& [key, gate] : gates)
        {
            const auto& a = assignments.at(gate.treasure);
            if (a.keyblade.empty() || !active_chest(gate.chest.Get()) ||
                !is_uobject_valid_fast(gate.reactor.Get()))
            {
                continue;
            }
            try
            {
                const auto command = desired_command(pawn, gate, a);
                if (reactor_command(gate.reactor.Get()) != command)
                {
                    reactor_command(gate.reactor.Get(), &command);
                }
            }
            catch (const std::exception& e)
            {
                error = e.what();
            }
        }
    }

    void RuntimeMusicChestService::Impl::checked(UObject* context, FFrame& stack, void*)
    {
        auto* self = instance;
        auto* f = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
        if (!self || f->PreviousFrame || !f->Locals)
        {
            return;
        }
        try
        {
            auto it = std::find_if(self->gates.begin(), self->gates.end(),
                                   [&](auto& e)
                                   {
                                       return e.second.notice.Get() == context;
                                   });
            if (it == self->gates.end())
            {
                return;
            }
            auto& gate = it->second;
            auto& a = self->assignments.at(gate.treasure);
            UObject* causer{};
            UObject* reactor{};
            std::memcpy(&causer, f->Locals, sizeof(causer));
            std::memcpy(&reactor,
                        reinterpret_cast<std::byte*>(f->Locals) + chest_command_reactor_offset,
                        sizeof(reactor));
            auto* pawn = self->config.playable_pawn();
            if (a.cleared || !gate.bound || causer != pawn || reactor != gate.reactor.Get() ||
                !self->config.gameplay_ready() ||
                !(gate.spawned ? is_uobject_valid_fast(gate.chest.Get()) && !gate.opened
                               : active_chest(gate.chest.Get())) ||
                world(pawn) != self->current_world.Get() ||
                reactor_command(reactor) != chest_command::examine)
            {
                return;
            }
            auto p = position(pawn), q = gate.spawned ? a.position : position(gate.chest.Get());
            float distance{};
            for (int i = 0; i < static_cast<int>(p.size()); ++i)
            {
                distance += (p[i] - q[i]) * (p[i] - q[i]);
            }
            if (distance > music_gate_interaction_radius * music_gate_interaction_radius ||
                !self->directory)
            {
                return;
            }
            if (!a.keyblade.empty())
            {
                self->keyblade_cache.invalidate(a.keyblade);
                self->refresh_keyblade_gates(pawn);
                if (gate.keyblade_owned || Clock::now() < gate.notice_next)
                {
                    return;
                }
                if (!gate.notice_storage)
                {
                    gate.notice_storage = std::make_unique<RuntimeApMessageParamStorage>();
                }
                Call notice(loaded(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                            L"TresUI_OpenInformation");
                if (!write_tres_loc_text_function_param(
                        notice.field(L"InformationText"), notice.data.data(),
                        L"Requires " + a.keyblade_name + L" to open.", *gate.notice_storage) ||
                    !write_bool_function_param(notice.field(L"AutoClose"), notice.data.data(),
                                               true) ||
                    !write_float_function_param(notice.field(L"OptionalDisplayTime"),
                                                notice.data.data(),
                                                music_chest_notice_duration_seconds))
                {
                    throw std::runtime_error("keyblade_chest_notice_layout");
                }
                Call clear(loaded(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                           L"TresUI_ClearInformationTextCache");
                clear.run();
                notice.run();
                retain_runtime_ap_message_params(*gate.notice_storage, std::move(notice.data));
                gate.notice_next = Clock::now() + music_gate_notice_interval;
                return;
            }
            if (!begin_music_chest_pause(pawn))
            {
                return;
            }
            ++self->sequence;
            const auto id = std::format(L"{:08x}-{:04x}-4000-8000-{:012x}", GetCurrentProcessId(),
                                        unsigned(self->sequence & 0xffff),
                                        uint64_t(unix_ms()) & 0xffffffffffff);
            std::ostringstream identity;
            identity << "\"schema\":\"kh3ap.mom_barrier.v1\",\"barrier_id\":\"" << json_escape(id)
                     << "\",\"process_id\":" << GetCurrentProcessId() << ",\"seed_name\":\""
                     << json_escape(self->seed) << "\",\"slot\":" << self->slot
                     << ",\"ap_location_id\":" << a.location;
            write_text_file_atomic_verified(*self->directory /
                                                channels::runtime_mom_barrier_state_file,
                                            "{" + identity.str() + ",\"active\":true}");
            write_text_file_atomic_verified(
                *self->directory / channels::runtime_mom_barrier_interaction_file,
                "{" + identity.str() + ",\"sequence\":" + std::to_string(self->sequence) +
                    ",\"expires_unix_ms\":" +
                    std::to_string(unix_ms() + music_chest_request_lifetime_ms) + "}");
        }
        catch (const std::exception& e)
        {
            self->error = e.what();
            self->publish_status();
        }
    }

    void RuntimeMusicChestService::Impl::bind(Gate& gate)
    {
        auto* klass = static_cast<UClass*>(retained_notice);
        if (!is_uobject_valid_fast(klass))
        {
            throw std::runtime_error("music_callback_class_unavailable");
        }
        UFunction* fn{};
        for (auto* candidate : klass->ForEachFunctionInChain())
        {
            if (candidate->GetNamePrivate() == FName(L"OnChestChecked"))
            {
                fn = candidate;
                break;
            }
        }
        if (!is_uobject_valid_fast(fn) || fn->GetNumParms() != chest_command_parameter_count ||
            fn->GetParmsSize() != chest_command_parameter_bytes)
        {
            throw std::runtime_error("music_callback_signature");
        }
        constexpr std::array names{L"CommandCauser", L"CommandID", L"ReactorComponent"};
        constexpr std::array offsets{0, 8, 16};
        constexpr std::array sizes{8, 1, 8};
        for (size_t i = 0; i < names.size(); ++i)
        {
            auto* p = find_function_param_by_name(fn, names[i]);
            if (!p || p->GetOffset_Internal() != offsets[i] || p->GetElementSize() != sizes[i])
            {
                throw std::runtime_error("music_callback_layout");
            }
        }
        auto* r = gate.reactor.Get();
        auto* delegate = chest_delegate(r);
        // Stock chest Blueprints can already listen to this multicast delegate
        // (including MI chests). BindToReactor uses AddDelegate, so append our
        // listener without requiring an empty list or removing stock callbacks.
        const auto previous_count = delegate->InvocationList.Num();
        FStaticConstructObjectParameters params(klass, klass->GetOuterPrivate());
        params.SetFlags = RF_Transient;
        auto* object = UObjectGlobals::StaticConstructObject(params);
        Call c(object, L"BindToReactor");
        write_object_function_param(c.fn, c.data.data(), L"ReactorComponent", r);
        c.run();
        object->SetRootSet();
        gate.notice = object;
        fn->SetFuncPtr(&Impl::checked);
        fn->GetFunctionFlags() |= FUNC_Native;
        int own_count{};
        for (const auto& listener : delegate->InvocationList)
        {
            if (listener.Object.Get() == object &&
                listener.FunctionName.ToString() == L"OnChestChecked")
            {
                ++own_count;
            }
        }
        if (gate.notice.Get() != object || delegate->InvocationList.Num() != previous_count + 1 ||
            own_count != 1)
        {
            // A failed readback must not leave a rooted callback behind that
            // would accumulate another binding on each retry.
            auto& list = delegate->InvocationList;
            for (int i = list.Num() - 1; i >= 0; --i)
            {
                if (list[i].Object.Get() == object &&
                    list[i].FunctionName.ToString() == L"OnChestChecked")
                {
                    list.RemoveAt(i);
                }
            }
            object->ClearRootSet();
            gate.notice = FWeakObjectPtr{};
            throw std::runtime_error("music_callback_readback");
        }
        gate.bound = true;
    }
} // namespace kh3ap
