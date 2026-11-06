#include <array>
#include "kh3ap/features/treasure/chest_commands.h"
#include "kh3ap/features/enemies/enemy_clear/service_impl.h"
#include "kh3ap/features/enemies/enemy_clear/support.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/core/objects/asset_load_helpers.h"
#include "kh3ap/bridge/utilities.h"

#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <cstring>
#include <sstream>
#include <utility>

namespace kh3ap
{
    namespace
    {
        constexpr auto chest_notice_parameter_count = 3;
        constexpr auto chest_notice_parameter_bytes = 24;
        constexpr auto chest_notice_reactor_offset = 16;
        constexpr std::array default_chest_test_color{0.1f, 1.f, 1.f, 0.f};
        constexpr std::array maximum_chest_test_channels{2, 8, 2};

        constexpr auto chest_gate_notice_duration_seconds = 3.f;
        constexpr auto max_test_chest_gates = 64;
        constexpr auto chest_notice_load_uuid = 0x4b484702;
        constexpr auto chest_material_load_uuid = 0x4b484701;
    } // namespace

    using namespace RC::Unreal;
    using namespace enemy_clear_detail;

    void RuntimeEnemyClearService::Impl::ensure_cache_context(UObject* pawn)
    {
        auto* current_world = actor_world(pawn);
        if (cache_pawn.Get() == pawn && cache_world.Get() == current_world)
        {
            return;
        }
        keyblade_cache.clear();
        cache_pawn = pawn;
        cache_world = current_world;
        for (auto& [id, gate] : chest_gates)
        {
            gate.gate_inventory_dirty = true;
            if (gate.gate_enabled && gate.gate_world.Get() != current_world)
            {
                remove_chest_notice(gate);
                gate.gate_enabled = false;
            }
        }
    }

    void RuntimeEnemyClearService::Impl::refresh_keyblade_group(UObject* pawn,
                                                                const std::wstring& token)
    {
        keyblade_cache.invalidate(token);
        for (auto& [id, gate] : chest_gates)
        {
            if (gate.gate_enabled && gate.keyblade == token)
            {
                update_chest_gate(pawn, gate);
            }
        }
    }

    void RuntimeEnemyClearService::Impl::chest_notice_thunk(UObject* context, FFrame& stack, void*)
    {
        // This owned callback is dispatched exclusively by a dynamic
        // delegate's ProcessEvent; parameters live in the reflected slab.
        auto* frame = reinterpret_cast<FFrame_50_AndBelow*>(&stack);
        // ProcessEvent's frame still has this cooked Blueprint's bytecode
        // pointer after native binding. Locals, not Code, holds its inputs.
        if (frame->PreviousFrame || !frame->Locals)
        {
            return;
        }
        auto* self = notice_instance;
        if (!self)
        {
            return;
        }
        auto found = std::find_if(self->chest_gates.begin(), self->chest_gates.end(),
                                  [context](auto& entry)
                                  {
                                      return entry.second.notice_object.Get() == context;
                                  });
        if (found == self->chest_gates.end())
        {
            return;
        }
        auto& gate = found->second;
        try
        {
            UObject* causer{};
            UObject* reactor{};
            std::memcpy(&causer, frame->Locals, sizeof(causer));
            std::memcpy(&reactor,
                        reinterpret_cast<std::byte*>(frame->Locals) + chest_notice_reactor_offset,
                        sizeof(reactor));
            auto* pawn = self->config.playable_sora();
            if (!gate.gate_enabled || !gate.gate_notification || causer != pawn ||
                !self->gameplay(pawn) || actor_world(pawn) != gate.gate_world.Get() ||
                reactor != gate.notice_reactor.Get() ||
                reactor_command(reactor) != chest_command::examine ||
                Clock::now() < gate.notice_next)
            {
                return;
            }
            // Revalidate at interaction, including grants made outside the
            // AP runtime. A stale Examine prompt becomes Open when owned.
            self->refresh_keyblade_group(pawn, gate.keyblade);
            if (gate.gate_owned)
            {
                return;
            }
            auto* chest = gate.gate_chest.Get();
            if (!is_uobject_valid_fast(chest) || !property_bool(chest, L"m_bIsActive", false) ||
                is_uobject_valid_fast(
                    try_read_reflected_object_property_value(chest, L"m_pOpenner")))
            {
                return;
            }
            RuntimeHotPathProfileScope profile{L"chest_gate:notification"};
            auto* library = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, L"/Script/TresGame.Default__TresGameBlueprintLibrary");
            Call notice(library, L"TresUI_OpenInformation");
            if (!write_tres_loc_text_function_param(
                    notice.field(L"InformationText"), notice.params.data(),
                    L"Requires " + gate.keyblade_name + L" to open.", gate.notice_storage) ||
                !write_bool_function_param(notice.field(L"AutoClose"), notice.params.data(),
                                           true) ||
                !write_float_function_param(notice.field(L"OptionalDisplayTime"),
                                            notice.params.data(),
                                            chest_gate_notice_duration_seconds))
            {
                throw std::runtime_error("chest_notice_text_layout");
            }
            Call clear(library, L"TresUI_ClearInformationTextCache");
            clear.invoke();
            notice.invoke();
            retain_runtime_ap_message_params(gate.notice_storage, std::move(notice.params));
            ++gate.notices_shown;
            gate.notice_next = Clock::now() + battlegate_notice_interval;
            gate.notice_error.clear();
        }
        catch (const std::exception& e)
        {
            gate.notice_error = e.what();
        }
        catch (...)
        {
            gate.notice_error = "chest_notice_callback_failed";
        }
    }

    void RuntimeEnemyClearService::Impl::remove_chest_notice(ChestGate& gate)
    {
        auto* object = gate.notice_object.Get();
        auto* reactor = gate.notice_reactor.Get();
        if (is_uobject_valid_fast(reactor))
        {
            auto& list = chest_delegate(reactor)->InvocationList;
            for (int32_t i = list.Num() - 1; i >= 0; --i)
            {
                if (list[i].Object.Get() == object &&
                    list[i].FunctionName.ToString() == L"OnChestChecked")
                {
                    list.RemoveAt(i);
                }
            }
            reactor_command(reactor, &gate.gate_original_command);
        }
        if (is_uobject_valid_fast(object))
        {
            object->ClearRootSet();
        }
        gate.notice_object = nullptr;
        gate.notice_reactor = nullptr;
        gate.gate_notification = false;
    }

    void RuntimeEnemyClearService::Impl::bind_chest_notice(UObject* pawn, ChestGate& gate)
    {
        if (gate.gate_notification)
        {
            return;
        }
        auto* chest = gate.gate_chest.Get();
        if (!gate.gate_enabled || !is_uobject_valid_fast(chest) ||
            gate.gate_world.Get() != actor_world(pawn))
        {
            throw std::runtime_error("notice_requires_armed_chest");
        }
        auto* reactor = try_read_reflected_object_property_value(chest, L"MyReactor");
        auto* klass = chest_notice_class();
        if (!klass)
        {
            throw std::runtime_error("prepare_chest_notice_first");
        }
        auto* function = UObjectGlobals::StaticFindObject<UFunction*>(
            UFunction::StaticClass(), nullptr,
            L"/Game/Mods/KH3APDiagnostics/ChestNotice.ChestNotice_C:OnChestChecked");
        if (!is_uobject_valid_fast(function) ||
            function->GetNumParms() != chest_notice_parameter_count ||
            function->GetParmsSize() != chest_notice_parameter_bytes)
        {
            throw std::runtime_error("chest_notice_signature_layout");
        }
        constexpr std::array names{L"CommandCauser", L"CommandID", L"ReactorComponent"};
        constexpr std::array offsets{0, 8, 16};
        constexpr std::array sizes{8, 1, 8};
        for (size_t i = 0; i < names.size(); ++i)
        {
            auto* p = find_function_param_by_name(function, names[i]);
            if (!p || p->GetOffset_Internal() != offsets[i] || p->GetElementSize() != sizes[i])
            {
                throw std::runtime_error("chest_notice_parameter_layout");
            }
        }
        gate.gate_original_command = reactor_command(reactor);
        if (gate.gate_original_command != chest_command::open)
        {
            throw std::runtime_error("notice_requires_original_open_command");
        }
        auto* delegate = chest_delegate(reactor);
        if (delegate->InvocationList.Num() != 0)
        {
            throw std::runtime_error("chest_has_existing_check_listeners");
        }
        // Outer is our asset package, avoiding a rooted reference to a map.
        FStaticConstructObjectParameters params(klass, klass->GetOuterPrivate());
        params.SetFlags = RF_Transient;
        auto* object = UObjectGlobals::StaticConstructObject(params);
        if (!is_uobject_valid_fast(object))
        {
            throw std::runtime_error("chest_notice_construct_failed");
        }
        Call bind(object, L"BindToReactor");
        if (!write_object_function_param(bind.function, bind.params.data(), L"ReactorComponent",
                                         reactor))
        {
            throw std::runtime_error("chest_notice_bind_layout");
        }
        bind.invoke();
        gate.notice_object = object;
        if (gate.notice_object.Get() != object)
        {
            throw std::runtime_error("chest_notice_weak_identity_failed");
        }
        object->SetRootSet();
        gate.notice_reactor = reactor;
        notice_instance = this;
        function->SetFuncPtr(&Impl::chest_notice_thunk);
        function->GetFunctionFlags() |= FUNC_Native;
        if (delegate->InvocationList.Num() != 1 ||
            delegate->InvocationList[0].Object.Get() != object ||
            delegate->InvocationList[0].FunctionName.ToString() != L"OnChestChecked")
        {
            remove_chest_notice(gate);
            throw std::runtime_error("chest_notice_binding_readback_failed");
        }
        gate.gate_inventory_dirty = true;
        gate.gate_notification = true;
        gate.notices_shown = 0;
        gate.notice_error.clear();
        update_chest_gate(pawn, gate);
    }

    void RuntimeEnemyClearService::Impl::update_chest_gate(UObject* pawn, ChestGate& gate)
    {
        if (!gate.gate_enabled)
        {
            return;
        }
        auto* chest = gate.gate_chest.Get();
        if (!is_uobject_valid_fast(chest))
        {
            remove_chest_notice(gate);
            gate.gate_enabled = false;
            return;
        }
        if (!is_uobject_valid_fast(pawn) || actor_world(pawn) != gate.gate_world.Get() ||
            !gameplay(pawn))
        {
            return;
        }
        if (!property_bool(chest, L"m_bIsActive", false) ||
            is_uobject_valid_fast(try_read_reflected_object_property_value(chest, L"m_pOpenner")))
        {
            remove_chest_notice(gate);
            gate.gate_enabled = false;
            return;
        }
        auto* reactor = try_read_reflected_object_property_value(chest, L"MyReactor");
        const auto revision = read_runtime_keyblade_inventory_revision();
        const auto& cached = keyblade_cache.get(gate.keyblade,
                                                [&]
                                                {
                                                    RuntimeHotPathProfileScope profile{
                                                        L"chest_gate:inventory_refresh"};
                                                    return keyblade_inventory(pawn, gate.keyblade);
                                                });
        // Another keyblade event leaves this reactor untouched.
        if (gate.gate_inventory_checks == cached.queries && !gate.gate_inventory_dirty)
        {
            gate.gate_inventory_revision = revision;
            return;
        }
        const bool owned = cached.owned;
        gate.gate_inventory_checks = cached.queries;
        gate.gate_owned = owned;
        const bool enable = gate.gate_notification || owned;
        if (gate.gate_notification)
        {
            const uint8_t command = owned ? chest_command::open : chest_command::examine;
            reactor_command(reactor, &command);
        }
        if (boolean(reactor, L"BP_IsGimmickComponentActive") != enable)
        {
            Call toggle(reactor,
                        enable ? L"BP_ActivateGimmickComponent" : L"BP_DeactivateGimmickComponent");
            toggle.invoke();
        }
        if (!owned &&
            ((gate.gate_notification && reactor_command(reactor) == chest_command::examine) ||
             !boolean(reactor, L"BP_IsGimmickComponentActive")))
        {
            gate.gate_locked_observed = true;
        }
        gate.gate_inventory_revision = revision;
        gate.gate_inventory_dirty = false;
    }

    void RuntimeEnemyClearService::Impl::process_chest_gate_events(ChestGate& gate)
    {
        RuntimeHotPathProfileScope profile{L"chest_gate:event_check"};
        auto* chest = gate.gate_chest.Get();
        if (!is_uobject_valid_fast(chest) || !property_bool(chest, L"m_bIsActive", false))
        {
            remove_chest_notice(gate);
            gate.gate_enabled = false;
            gate.gate_inventory_dirty = false;
            return;
        }
        if (gate.gate_inventory_dirty ||
            gate.gate_inventory_revision != read_runtime_keyblade_inventory_revision())
        {
            update_chest_gate(config.playable_sora(), gate);
        }
    }

    std::string RuntimeEnemyClearService::Impl::inspect_chest_gate(UObject* pawn, ChestGate& gate)
    {
        auto* chest = gate.gate_chest.Get();
        auto* mesh = is_uobject_valid_fast(chest)
                         ? try_read_reflected_object_property_value(chest, L"MyMesh")
                         : nullptr;
        auto* material = is_uobject_valid_fast(mesh) ? material_zero(mesh) : nullptr;
        auto* reactor = is_uobject_valid_fast(chest)
                            ? try_read_reflected_object_property_value(chest, L"MyReactor")
                            : nullptr;
        std::ostringstream out;
        out << std::boolalpha << "{\"enabled\":" << gate.gate_enabled << ",\"required_keyblade\":\""
            << json_escape(gate.keyblade) << "\",\"required_keyblade_name\":\""
            << json_escape(gate.keyblade_name)
            << "\",\"has_required_keyblade\":" << keyblade_inventory(pawn, gate.keyblade)
            << ",\"colored\":" << gate.gate_colored
            << ",\"notification_mode\":" << gate.gate_notification
            << ",\"event_driven\":true,\"inventory_refreshes\":" << gate.gate_inventory_checks
            << ",\"inventory_revision\":" << gate.gate_inventory_revision
            << ",\"inventory_dirty\":" << gate.gate_inventory_dirty
            << ",\"notice_ready\":" << (chest_notice_class() != nullptr)
            << ",\"notices_shown\":" << gate.notices_shown << ",\"notice_error\":\""
            << json_escape(std::wstring(gate.notice_error.begin(), gate.notice_error.end())) << "\""
            << ",\"reactor_command\":"
            << (is_uobject_valid_fast(reactor) ? static_cast<int>(reactor_command(reactor)) : -1)
            << ",\"material_ready\":" << is_uobject_valid_fast(test_material())
            << ",\"has_shooting_star\":" << keyblade_inventory(pawn, L"WEP_KEYBLADE_SO_02")
            << ",\"locked_observed\":" << gate.gate_locked_observed
            << ",\"grant_dispatched\":" << gate.gate_grant_dispatched << ",\"actor\":\""
            << json_escape(is_uobject_valid_fast(chest) ? chest->GetPathName() : L"")
            << "\",\"treasure_id\":\""
            << json_escape(is_uobject_valid_fast(chest) ? property_name(chest, L"m_TreasureID")
                                                        : L"")
            << "\",\"material\":\""
            << json_escape(is_uobject_valid_fast(material) ? material->GetPathName() : L"")
            << "\",\"reactor_active\":"
            << (is_uobject_valid_fast(reactor) && boolean(reactor, L"BP_IsGimmickComponentActive"))
            << "}";
        auto snapshot = out.str();
        if (gate.gate_colored && is_uobject_valid_fast(material))
        {
            auto* diffuse = chest_diffuse(material);
            const auto hsb = chest_hsb(material);
            snapshot.pop_back();
            std::ostringstream extra;
            extra << ",\"diffuse\":\""
                  << json_escape(is_uobject_valid_fast(diffuse) ? diffuse->GetPathName() : L"")
                  << "\",\"hsb\":[" << hsb[0] << "," << hsb[1] << "," << hsb[2] << "," << hsb[3]
                  << "]}";
            snapshot += extra.str();
        }
        return snapshot;
    }

    void RuntimeEnemyClearService::Impl::accept_chest_gate(UObject* pawn, const std::string& text)
    {
        ensure_cache_context(pawn);
        const auto treasure =
            JsonFieldReader::string_field(text, "treasure_id").value_or(L"RA_LBOX_001");
        if (!chest_gates.contains(treasure) && chest_gates.size() >= max_test_chest_gates)
        {
            throw std::runtime_error("chest_gate_limit");
        }
        auto& gate = chest_gates[treasure];
        const auto action = JsonFieldReader::string_field(text, "action").value_or(L"");
        if (action == L"prepare_notice")
        {
            if (!chest_notice_class())
            {
                std::wstring reason;
                if (!request_runtime_asset_class_load(pawn, chest_notice_class_path, &reason,
                                                      chest_notice_load_uuid))
                {
                    throw std::runtime_error("notice_class_load_failed:" + utf8(reason.c_str()));
                }
            }
        }
        else if (action == L"notify")
        {
            bind_chest_notice(pawn, gate);
        }
        else if (action == L"prepare")
        {
            if (!is_uobject_valid_fast(test_material()))
            {
                std::wstring reason;
                if (!request_runtime_asset_load(pawn, test_material_path, &reason,
                                                chest_material_load_uuid))
                {
                    throw std::runtime_error("material_load_request_failed:" +
                                             utf8(reason.c_str()));
                }
            }
        }
        else if (action == L"arm")
        {
            if (gate.gate_enabled)
            {
                throw std::runtime_error("remove_existing_chest_gate_first");
            }
            auto* chest = resolve_chest(
                pawn, JsonFieldReader::string_field(text, "actor").value_or(L""), treasure);
            if (!property_bool(chest, L"m_bIsActive", false) ||
                property_bool(chest, L"bHidden", true) ||
                !boolean(chest, L"GetActorEnableCollision") ||
                is_uobject_valid_fast(
                    try_read_reflected_object_property_value(chest, L"m_pOpenner")))
            {
                throw std::runtime_error("gate_requires_unopened_visible_chest");
            }
            const auto token =
                JsonFieldReader::string_field(text, "keyblade").value_or(L"WEP_KEYBLADE_SO_02");
            // Verified game item tokens; display text is never supplied by bridge input.
            const std::map<std::wstring, std::wstring> names{
                {L"WEP_KEYBLADE_SO_02", L"Shooting Star"},
                {L"WEP_KEYBLADE_SO_01", L"Hero's Origin"},
                {L"WEP_KEYBLADE_SO_03", L"Favorite Deputy"}};
            const auto name = names.find(token);
            if (name == names.end())
            {
                throw std::runtime_error("unsupported_test_keyblade");
            }
            gate.keyblade = token;
            gate.keyblade_name = name->second;
            gate.gate_colored = false;
            gate.gate_inventory_checks = 0;
            gate.gate_chest = chest;
            gate.gate_world = actor_world(pawn);
            gate.gate_original_material =
                material_zero(try_read_reflected_object_property_value(chest, L"MyMesh"));
            gate.gate_locked_observed = gate.gate_grant_dispatched = false;
            gate.gate_enabled = true;
            gate.gate_inventory_dirty = true;
            try
            {
                bind_chest_notice(pawn, gate);
            }
            catch (...)
            {
                remove_chest_notice(gate);
                gate.gate_enabled = false;
                throw;
            }
        }
        else if (action == L"recolor")
        {
            if (treasure != L"RA_LBOX_001")
            {
                throw std::runtime_error("recolor_requires_original_test_chest");
            }
            auto* chest = resolve_chest(
                pawn, JsonFieldReader::string_field(text, "actor").value_or(L""), L"RA_LBOX_001");
            if (!property_bool(chest, L"m_bIsActive", false) ||
                is_uobject_valid_fast(
                    try_read_reflected_object_property_value(chest, L"m_pOpenner")))
            {
                throw std::runtime_error("recolor_requires_unopened_chest");
            }
            auto* material =
                material_zero(try_read_reflected_object_property_value(chest, L"MyMesh"));
            auto* diffuse = chest_diffuse(material);
            if (!is_uobject_valid_fast(diffuse) ||
                diffuse->GetPathName() != L"/Game/Maps/ra/gimmick/g_ra_TreasureBox_l/mdl/"
                                          L"T_ra_TreasureBox_l_D.T_ra_TreasureBox_l_D")
            {
                throw std::runtime_error("recolor_requires_original_corona_texture");
            }
            auto color = default_chest_test_color;
            constexpr std::array names{"hue", "saturation", "brightness"};
            for (size_t i = 0; i < names.size(); ++i)
            {
                const auto value = JsonFieldReader::number_field(text, names[i]);
                if (!value || !std::isfinite(*value) || *value < 0 ||
                    *value > maximum_chest_test_channels[i])
                {
                    throw std::runtime_error("invalid_chest_color");
                }
                color[i] = static_cast<float>(*value);
            }
            if (!gate.gate_colored || gate.gate_chest.Get() != chest)
            {
                gate.gate_original_hsb = chest_hsb(material);
                gate.gate_original_material = material;
                gate.gate_locked_observed = gate.gate_grant_dispatched = false;
            }
            gate.gate_chest = chest;
            gate.gate_world = actor_world(pawn);
            report(L"accepted", L"chest_recolor_checkpoint");
            chest_hsb(material, &color);
            gate.gate_colored = true;
            gate.gate_enabled = true;
            gate.gate_inventory_dirty = true;
            update_chest_gate(pawn, gate);
        }
        else if (action == L"apply")
        {
            if (treasure != L"RA_LBOX_001")
            {
                throw std::runtime_error("texture_swap_requires_original_test_chest");
            }
            if (gate.gate_enabled)
            {
                throw std::runtime_error("chest_gate_already_armed");
            }
            auto* chest = resolve_chest(
                pawn, JsonFieldReader::string_field(text, "actor").value_or(L""), L"RA_LBOX_001");
            auto* reactor = try_read_reflected_object_property_value(chest, L"MyReactor");
            if (!property_bool(chest, L"m_bIsActive", false) ||
                property_bool(chest, L"bHidden", true) ||
                !boolean(chest, L"GetActorEnableCollision") ||
                !boolean(reactor, L"BP_IsGimmickComponentActive"))
            {
                throw std::runtime_error("test_requires_untouched_interactable_chest");
            }
            if (keyblade_inventory(pawn, gate.keyblade))
            {
                throw std::runtime_error("shooting_star_already_owned");
            }
            auto* material = validated_test_material();
            auto* mesh = try_read_reflected_object_property_value(chest, L"MyMesh");
            if (!is_uobject_valid_fast(mesh))
            {
                throw std::runtime_error("missing_chest_mesh");
            }
            gate.gate_original_material = material_zero(mesh);
            gate.gate_chest = chest;
            gate.gate_world = actor_world(pawn);
            gate.gate_colored = false;
            gate.gate_locked_observed = gate.gate_grant_dispatched = false;
            report(L"accepted", L"chest_gate_checkpoint");
            set_material_zero(mesh, material);
            gate.gate_enabled = true;
            gate.gate_inventory_dirty = true;
            update_chest_gate(pawn, gate);
        }
        else if (action == L"grant")
        {
            if (!gate.gate_enabled || !gate.gate_locked_observed ||
                gate.gate_world.Get() != actor_world(pawn) ||
                keyblade_inventory(pawn, gate.keyblade))
            {
                throw std::runtime_error("grant_requires_locked_chest_and_missing_shooting_star");
            }
            report(L"accepted", L"shooting_star_grant_checkpoint");
            if (!keyblade_inventory(pawn, gate.keyblade, true, &gate.gate_grant_dispatched))
            {
                throw std::runtime_error("shooting_star_not_owned_after_grant");
            }
            // The common grant event wakes the gate; this branch never
            // changes the reactor or assumes ownership from dispatch.
        }
        else if (action == L"remove")
        {
            remove_chest_notice(gate);
            auto* chest = gate.gate_chest.Get();
            if (is_uobject_valid_fast(chest) && gate.gate_world.Get() == actor_world(pawn))
            {
                auto* material = gate.gate_original_material.Get();
                if (!is_uobject_valid_fast(material))
                {
                    throw std::runtime_error("original_material_unavailable");
                }
                if (gate.gate_colored)
                {
                    chest_hsb(material, &gate.gate_original_hsb);
                }
                set_material_zero(try_read_reflected_object_property_value(chest, L"MyMesh"),
                                  material);
                if (gate.gate_enabled && property_bool(chest, L"m_bIsActive", false) &&
                    !is_uobject_valid_fast(
                        try_read_reflected_object_property_value(chest, L"m_pOpenner")))
                {
                    Call activate(try_read_reflected_object_property_value(chest, L"MyReactor"),
                                  L"BP_ActivateGimmickComponent");
                    activate.invoke();
                }
            }
            gate.gate_enabled = false;
        }
        else if (action != L"inspect")
        {
            throw std::runtime_error("invalid_chest_gate_action");
        }
        gate_snapshot = inspect_chest_gate(pawn, gate);
        gates_snapshot = "[";
        for (auto& [id, entry] : chest_gates)
        {
            if (gates_snapshot.size() > 1)
            {
                gates_snapshot += ",";
            }
            gates_snapshot += inspect_chest_gate(pawn, entry);
        }
        gates_snapshot += "]";
        inspect_scene(pawn);
        finish(L"completed", L"chest_gate_snapshot");
        return;
    }

} // namespace kh3ap
