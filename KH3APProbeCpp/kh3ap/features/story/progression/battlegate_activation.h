#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace kh3ap
{
    struct BattlegateActivationRule
    {
        std::wstring_view caller;
        std::wstring_view owner_prefix;
        std::wstring_view story_flag;
        int32_t completed_value;
        bool day_night_gates{};

        [[nodiscard]] constexpr auto matches(std::wstring_view function, std::wstring_view owner) const -> bool
        {
            return function == caller && owner.starts_with(owner_prefix);
        }
    };

    // These stock IsGameCleared results feed only the Battlegate start/stop
    // branches. Corona's second result belongs to another postgame branch.
    inline constexpr std::array<BattlegateActivationRule, 9> battlegate_activation_rules{{
        {L"Function /Game/Levels/he/he_01/umap/he_01_gameflow.he_01_gameflow_C:ExecuteUbergraph_he_01_gameflow",
         L"he_01_gameflow_C /Game/Levels/he/he_01/umap/he_01_gameflow.he_01_gameflow:PersistentLevel.", L"gameflow_HE", 9999},
        {L"Function /Game/Levels/tt/tt_01/umap/tt_01_gameflow.tt_01_gameflow_C:ExecuteUbergraph_tt_01_gameflow",
         L"tt_01_gameflow_C /Game/Levels/tt/tt_01/umap/tt_01_gameflow.tt_01_gameflow:PersistentLevel.", L"gameflow_TT", 9999},
        {L"Function /Game/Levels/ts/ts_02/umap/ts_02_gameflow.ts_02_gameflow_C:ExecuteUbergraph_ts_02_gameflow",
         L"ts_02_gameflow_C /Game/Levels/ts/ts_02/umap/ts_02_gameflow.ts_02_gameflow:PersistentLevel.", L"gameflow_TS", 9999},
        {L"Function /Game/Levels/ra/ra_01/umap/ra_01_gameflow.ra_01_gameflow_C:ExecuteUbergraph_ra_01_gameflow",
         L"ra_01_gameflow_C /Game/Levels/ra/ra_01/umap/ra_01_gameflow.ra_01_gameflow:PersistentLevel.", L"gameflow_RA", 9999},
        {L"Function /Game/Levels/mi/mi_03/umap/mi_03_gameflow.mi_03_gameflow_C:ExecuteUbergraph_mi_03_gameflow",
         L"mi_03_gameflow_C /Game/Levels/mi/mi_03/umap/mi_03_gameflow.mi_03_gameflow:PersistentLevel.", L"gameflow_MI", 9999},
        {L"Function /Game/Levels/fz/fz_02/umap/fz_02_gameflow.fz_02_gameflow_C:ExecuteUbergraph_fz_02_gameflow",
         L"fz_02_gameflow_C /Game/Levels/fz/fz_02/umap/fz_02_gameflow.fz_02_gameflow:PersistentLevel.", L"gameflow_FZ", 8820},
        {L"Function /Game/Levels/ca/ca_02/umap/ca_02_gameflow.ca_02_gameflow_C:ExecuteUbergraph_ca_02_gameflow",
         L"ca_02_gameflow_C /Game/Levels/ca/ca_02/umap/ca_02_gameflow.ca_02_gameflow:PersistentLevel.", L"gameflow_CA", 9999},
        {L"Function /Game/Levels/bx/bx_02/umap/bx_02_gameflow.bx_02_gameflow_C:ExecuteUbergraph_bx_02_gameflow",
         L"bx_02_gameflow_C /Game/Levels/bx/bx_02/umap/bx_02_gameflow.bx_02_gameflow:PersistentLevel.", L"gameflow_BX", 9999, true},
        {L"Function /Game/Levels/kg/kg_01/umap/kg_01_gameflow.kg_01_gameflow_C:ExecuteUbergraph_kg_01_gameflow",
         L"kg_01_gameflow_C /Game/Levels/kg/kg_01/umap/kg_01_gameflow.kg_01_gameflow:PersistentLevel.", L"gameflow_KG", 14700},
    }};

    [[nodiscard]] constexpr auto battlegate_result_local(
            const BattlegateActivationRule& rule, std::wstring_view local) -> bool
    {
        return local == L"CallFunc_IsGameCleared_ReturnValue" ||
               (rule.day_night_gates &&
                (local == L"CallFunc_IsGameCleared_ReturnValue2" ||
                 local == L"CallFunc_IsGameCleared_ReturnValue3"));
    }

    namespace battlegate_bytecode
    {
        inline constexpr uint8_t final_function = 0x1c;
        inline constexpr uint8_t end_function_parms = 0x16;
        inline constexpr uint8_t jump_if_not = 0x07;
        inline constexpr uint8_t pop_execution_flow_if_not = 0x4f;
        inline constexpr uint8_t local_variable = 0x00;
        inline constexpr size_t getter_call_bytes = 10;
        inline constexpr size_t getter_pointer_distance = 9;
        inline constexpr size_t jump_if_not_header_bytes = 5;
        inline constexpr size_t conditional_flow_header_bytes = 1;
        inline constexpr size_t local_variable_bytes = 9;
        inline constexpr size_t local_pointer_offset = 1;
    } // namespace battlegate_bytecode

    // At native post-hook time the caller's Code points past the getter's
    // EndFunctionParms. Match its resolved function pointer and the following
    // branch's exact local property. No script bytes or saved flags are written.
    [[nodiscard]] inline auto is_battlegate_result_branch(
            std::span<const uint8_t> script, size_t offset,
            uintptr_t getter, uintptr_t local_property) -> bool
    {
        if (!getter || !local_property || offset < battlegate_bytecode::getter_call_bytes ||
            offset >= script.size())
        {
            return false;
        }
        const auto pointer = [](const uint8_t* bytes) {
            uintptr_t value{};
            std::memcpy(&value, bytes, sizeof(value));
            return value;
        };
        if (script[offset - battlegate_bytecode::getter_call_bytes] !=
                battlegate_bytecode::final_function ||
            pointer(script.data() + offset - battlegate_bytecode::getter_pointer_distance) !=
                getter ||
            script[offset - 1] != battlegate_bytecode::end_function_parms)
        {
            return false;
        }
        size_t variable{};
        if (script[offset] == battlegate_bytecode::jump_if_not)
        {
            variable = offset + battlegate_bytecode::jump_if_not_header_bytes; // EX_JumpIfNot
        }
        else if (script[offset] == battlegate_bytecode::pop_execution_flow_if_not)
        {
            variable =
                offset +
                battlegate_bytecode::conditional_flow_header_bytes; // EX_PopExecutionFlowIfNot
        }
        else return false;
        return variable < script.size() &&
               script.size() - variable >= battlegate_bytecode::local_variable_bytes &&
               script[variable] == battlegate_bytecode::local_variable && // EX_LocalVariable
               pointer(script.data() + variable + battlegate_bytecode::local_pointer_offset) ==
                   local_property;
    }
}
