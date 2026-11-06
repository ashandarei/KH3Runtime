#include <optional>
#include "kh3ap/diagnostics/ability_probe/manual_equipment_probe_helpers.h"

#include "kh3ap/diagnostics/output/diagnostic_probe_serializer.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/grants/abilities/ability_diagnostic_state.h"
#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/diagnostics/diagnostic_state.h"
#include "kh3ap/features/grants/grant_pawn_state.h"
#include "kh3ap/features/grants/grant_safety.h"
#include "kh3ap/features/grants/grant_state.h"
#include "kh3ap/features/player/objects/object_cache.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/app/state.h"
#include "kh3ap/core/types.h"
#include "kh3ap/bridge/utilities.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <string>
#include <utility>

namespace kh3ap
{
    using namespace RC;
    using namespace RC::Unreal;

    auto make_manual_equipment_probe_deps(RuntimeState& runtime_state,
                                          std::optional<std::filesystem::path> bridge_dir,
                                          std::wstring_view request_filename,
                                          std::wstring_view result_filename,
                                          std::wstring_view protocol_name, int64_t protocol_version,
                                          std::wstring_view runtime_context_summary,
                                          std::wstring_view build_stamp) -> ManualEquipmentProbeDeps
    {
        return {runtime_state, std::move(bridge_dir), request_filename,        result_filename,
                protocol_name, protocol_version,      runtime_context_summary, build_stamp};
    }

    namespace
    {
        constexpr auto max_equipment_probe_pawns = 3;
        constexpr auto max_equipment_probe_array_entries = 16;
        auto try_read_manual_equipment_object_property_value(UObject* object,
                                                             std::wstring_view property_name)
            -> UObject*
        {
            if (!object || !kh3ap::is_uobject_valid_fast(object))
            {
                return nullptr;
            }

            FProperty* property =
                object->GetPropertyByNameInChain(std::wstring{property_name}.c_str());
            void* value_ptr =
                object->GetValuePtrByPropertyNameInChain(std::wstring{property_name}.c_str());
            if (!property || !value_ptr ||
                property->GetClass().GetName() != STR("ObjectProperty") ||
                property->GetElementSize() < static_cast<int>(sizeof(UObject*)))
            {
                return nullptr;
            }

            UObject* object_value = nullptr;
            std::memcpy(&object_value, value_ptr, sizeof(UObject*));
            if (!object_value || !kh3ap::is_uobject_valid_fast(object_value))
            {
                return nullptr;
            }
            return object_value;
        }

        auto append_manual_equipment_probe_jsonl(const ManualEquipmentProbeDeps& deps,
                                                 std::wstring_view kind,
                                                 std::wstring_view request_id,
                                                 std::wstring_view label, std::wstring_view stage,
                                                 std::wstring_view object_name,
                                                 std::wstring_view class_name,
                                                 std::wstring_view detail = {}) -> void
        {
            if (!deps.bridge_dir.has_value())
            {
                return;
            }

            const std::string json =
                DiagnosticProbeSerializer::serialize_manual_equipment_probe_event(
                    deps.build_stamp, kind, request_id, label, stage, deps.runtime_context_summary,
                    object_name, class_name, detail);

            auto lock = deps.runtime_state.ability_diagnostic_state()
                            .probe_diagnostic_state()
                            .acquire_log_lock();
            append_text_file(*deps.bridge_dir / "manual_equipment_probe.jsonl", json);
        }

        auto write_manual_equipment_probe_result(const ManualEquipmentProbeDeps& deps,
                                                 std::wstring_view request_id,
                                                 std::wstring_view status, std::wstring_view stage,
                                                 std::wstring_view detail = {}) -> bool
        {
            if (!deps.bridge_dir.has_value())
            {
                return false;
            }

            const std::string json =
                DiagnosticProbeSerializer::serialize_manual_equipment_probe_result(
                    deps.protocol_name, deps.protocol_version, request_id, status, stage, detail,
                    deps.runtime_context_summary, deps.build_stamp);
            return write_text_file(*deps.bridge_dir / std::filesystem::path{deps.result_filename},
                                   json);
        }

        auto format_equipment_header_probe(uint64_t equipment_address, size_t offset)
            -> std::wstring
        {
            const auto bytes =
                try_read_process_memory_bytes(equipment_address + offset, sizeof(ArrayHeader));
            if (!bytes.has_value())
            {
                return STR("<unreadable_header>");
            }

            ArrayHeader header{};
            std::memcpy(&header, bytes->data(), sizeof(ArrayHeader));
            return std::format(STR("offset=0x{:X} data=0x{:X} num={} max={}"),
                               static_cast<uint64_t>(offset), header.data_ptr, header.num,
                               header.max);
        }
    } // namespace

    auto process_pending_manual_equipment_probe(const ManualEquipmentProbeDeps& deps) -> void
    {
        if (!deps.bridge_dir.has_value())
        {
            return;
        }

        const auto request_path = *deps.bridge_dir / std::filesystem::path{deps.request_filename};
        if (!std::filesystem::exists(request_path))
        {
            return;
        }

        const auto request_text = read_text_file(request_path);
        const std::wstring request_id =
            request_text.has_value() ? JsonFieldReader::string_field(*request_text, "request_id")
                                           .value_or(std::wstring{})
                                     : std::wstring{};
        const std::wstring label =
            request_text.has_value()
                ? JsonFieldReader::string_field(*request_text, "label").value_or(std::wstring{})
                : std::wstring{};
        const std::wstring stage = request_text.has_value()
                                       ? JsonFieldReader::string_field(*request_text, "stage")
                                             .value_or(std::wstring{STR("equipment_link")})
                                       : std::wstring{STR("equipment_link")};
        const std::wstring effective_request_id =
            request_id.empty() ? std::wstring{STR("manual-equipment-probe")} : request_id;

        try_remove_file(request_path);
        write_manual_equipment_probe_result(deps, effective_request_id, STR("running"), stage);
        Output::send<LogLevel::Warning>(STR("[KH3APProbeCpp] ManualEquipmentProbe begin "
                                            "request_id={} stage={} label={} context={}\n"),
                                        effective_request_id, stage, label,
                                        deps.runtime_context_summary);

        UObject* const stable_pawn = deps.runtime_state.grant_state().pawn_state().object();
        UObject* const last_world_context_object =
            deps.runtime_state.object_cache().last_world_context_object();
        UObject* const preferred_object =
            stable_pawn && kh3ap::is_uobject_valid_fast(stable_pawn)
                ? stable_pawn
                : (last_world_context_object &&
                           kh3ap::is_uobject_valid_fast(last_world_context_object)
                       ? last_world_context_object
                       : nullptr);
        const auto pawn_candidates =
            collect_sora_ability_pawn_candidates(deps.runtime_state, preferred_object);
        append_manual_equipment_probe_jsonl(
            deps, STR("summary"), effective_request_id, label, stage,
            preferred_object ? format_object_debug_name(preferred_object)
                             : std::wstring{STR("<null>")},
            STR("preferred"),
            std::format(STR("sora_candidates={}"), static_cast<uint64_t>(pawn_candidates.size())));

        int pawn_count = 0;
        for (UObject* pawn_object : pawn_candidates)
        {
            if (!pawn_object || !kh3ap::is_uobject_valid_fast(pawn_object))
            {
                continue;
            }

            UClass* const pawn_class = pawn_object->GetClassPrivate();
            append_manual_equipment_probe_jsonl(deps, STR("pawn"), effective_request_id, label,
                                                stage, format_object_debug_name(pawn_object),
                                                pawn_class ? pawn_class->GetName()
                                                           : std::wstring{STR("<null>")});

            if (stage == STR("pawn"))
            {
                ++pawn_count;
                if (pawn_count >= max_equipment_probe_pawns)
                {
                    break;
                }
                continue;
            }

            UObject* const equipment_object =
                try_read_manual_equipment_object_property_value(pawn_object, STR("MyEquipment"));
            if (!equipment_object || !kh3ap::is_uobject_valid_fast(equipment_object))
            {
                append_manual_equipment_probe_jsonl(
                    deps, STR("equipment_missing"), effective_request_id, label, stage,
                    format_object_debug_name(pawn_object),
                    pawn_class ? pawn_class->GetName() : std::wstring{STR("<null>")},
                    STR("MyEquipment=<null>"));
            }
            else
            {
                UClass* const equipment_class = equipment_object->GetClassPrivate();
                append_manual_equipment_probe_jsonl(
                    deps, STR("equipment_link"), effective_request_id, label, stage,
                    format_object_debug_name(equipment_object),
                    equipment_class ? equipment_class->GetName() : std::wstring{STR("<null>")},
                    std::format(STR("from_pawn={}"), format_object_debug_name(pawn_object)));

                if (stage == STR("headers"))
                {
                    const uint64_t equipment_address = reinterpret_cast<uint64_t>(equipment_object);
                    for (size_t offset : {0x210ull, 0x218ull, 0x240ull, 0x250ull, 0x260ull,
                                          0x278ull, 0x2E8ull, 0x380ull, 0x390ull})
                    {
                        append_manual_equipment_probe_jsonl(
                            deps, STR("equipment_header"), effective_request_id, label, stage,
                            format_object_debug_name(equipment_object),
                            equipment_class ? equipment_class->GetName()
                                            : std::wstring{STR("<null>")},
                            format_equipment_header_probe(equipment_address, offset));
                    }
                }
                else if (stage == STR("weapon_equips"))
                {
                    const uint64_t equipment_address = reinterpret_cast<uint64_t>(equipment_object);
                    const size_t offset = 0x240;
                    const auto header_bytes = try_read_process_memory_bytes(
                        equipment_address + offset, sizeof(ArrayHeader));
                    if (!header_bytes.has_value())
                    {
                        append_manual_equipment_probe_jsonl(
                            deps, STR("weapon_equips_header_unreadable"), effective_request_id,
                            label, stage, format_object_debug_name(equipment_object),
                            equipment_class ? equipment_class->GetName()
                                            : std::wstring{STR("<null>")},
                            STR("offset=0x240"));
                    }
                    else
                    {
                        ArrayHeader header{};
                        std::memcpy(&header, header_bytes->data(), sizeof(ArrayHeader));
                        append_manual_equipment_probe_jsonl(
                            deps, STR("weapon_equips_header"), effective_request_id, label, stage,
                            format_object_debug_name(equipment_object),
                            equipment_class ? equipment_class->GetName()
                                            : std::wstring{STR("<null>")},
                            std::format(STR("offset=0x240 data=0x{:X} num={} max={}"),
                                        header.data_ptr, header.num, header.max));

                        if (header.data_ptr && header.num > 0 &&
                            header.num <= max_equipment_probe_array_entries &&
                            header.max >= header.num)
                        {
                            const auto entry_bytes = try_read_process_memory_bytes(
                                header.data_ptr,
                                static_cast<size_t>(header.num) * sizeof(UObject*));
                            if (!entry_bytes.has_value())
                            {
                                append_manual_equipment_probe_jsonl(
                                    deps, STR("weapon_equips_entries_unreadable"),
                                    effective_request_id, label, stage,
                                    format_object_debug_name(equipment_object),
                                    equipment_class ? equipment_class->GetName()
                                                    : std::wstring{STR("<null>")},
                                    STR("entry_bytes_unreadable"));
                            }
                            else
                            {
                                for (int index = 0; index < header.num; ++index)
                                {
                                    UObject* entry_object = nullptr;
                                    std::memcpy(&entry_object,
                                                entry_bytes->data() +
                                                    (static_cast<size_t>(index) * sizeof(UObject*)),
                                                sizeof(UObject*));
                                    if (entry_object && kh3ap::is_uobject_valid_fast(entry_object))
                                    {
                                        UClass* const entry_class = entry_object->GetClassPrivate();
                                        append_manual_equipment_probe_jsonl(
                                            deps, STR("weapon_equips_entry"), effective_request_id,
                                            label, stage, format_object_debug_name(entry_object),
                                            entry_class ? entry_class->GetName()
                                                        : std::wstring{STR("<null>")},
                                            std::format(STR("index={} ptr=0x{:X}"), index,
                                                        reinterpret_cast<uintptr_t>(entry_object)));
                                    }
                                    else
                                    {
                                        append_manual_equipment_probe_jsonl(
                                            deps, STR("weapon_equips_entry"), effective_request_id,
                                            label, stage, STR("<null_or_unreal>"), STR("<null>"),
                                            std::format(STR("index={} ptr=0x{:X} real=false"),
                                                        index,
                                                        reinterpret_cast<uintptr_t>(entry_object)));
                                    }
                                }
                            }
                        }
                    }
                }
            }

            ++pawn_count;
            if (pawn_count >= max_equipment_probe_pawns)
            {
                break;
            }
        }

        write_manual_equipment_probe_result(
            deps, effective_request_id, STR("completed"), stage,
            std::format(STR("sora_candidates={} logged_pawns={}"),
                        static_cast<uint64_t>(pawn_candidates.size()), pawn_count));
        Output::send<LogLevel::Warning>(
            STR("[KH3APProbeCpp] ManualEquipmentProbe completed request_id={} stage={} pawns={}\n"),
            effective_request_id, stage, pawn_count);
    }
} // namespace kh3ap
