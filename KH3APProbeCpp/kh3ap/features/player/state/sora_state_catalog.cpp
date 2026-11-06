#include "kh3ap/features/player/state/sora_state_catalog.h"

namespace kh3ap
{
    auto lookup_runtime_sora_state_by_kind(RuntimeSoraStateKind kind) -> const RuntimeSoraStateCatalogEntry*
    {
        for (const RuntimeSoraStateCatalogEntry& entry : k_observed_sora_state_catalog)
        {
            if (entry.kind == kind)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    auto lookup_runtime_sora_state_by_id(uint8_t id) -> const RuntimeSoraStateCatalogEntry*
    {
        for (const RuntimeSoraStateCatalogEntry& entry : k_observed_sora_state_catalog)
        {
            if (entry.id == id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    auto lookup_runtime_sora_state_by_class_name(std::wstring_view class_name) -> const RuntimeSoraStateCatalogEntry*
    {
        for (const RuntimeSoraStateCatalogEntry& entry : k_observed_sora_state_catalog)
        {
            if (entry.class_name == class_name)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    auto runtime_sora_state_matches_kind(
            RuntimeSoraStateKind kind,
            std::optional<uint8_t> id,
            std::wstring_view class_name) -> bool
    {
        const RuntimeSoraStateCatalogEntry* const entry = lookup_runtime_sora_state_by_kind(kind);
        if (!entry)
        {
            return false;
        }
        if (id.has_value() && *id == entry->id)
        {
            return true;
        }
        return !class_name.empty() && class_name == entry->class_name;
    }

    auto runtime_sora_state_class_name(RuntimeSoraStateKind kind) -> std::wstring_view
    {
        const RuntimeSoraStateCatalogEntry* const entry = lookup_runtime_sora_state_by_kind(kind);
        return entry ? entry->class_name : std::wstring_view{};
    }

    auto runtime_sora_state_id(RuntimeSoraStateKind kind) -> std::optional<uint8_t>
    {
        const RuntimeSoraStateCatalogEntry* const entry = lookup_runtime_sora_state_by_kind(kind);
        if (!entry)
        {
            return std::nullopt;
        }
        return entry->id;
    }

    auto runtime_sora_state_safety_label(RuntimeSoraStateSafety safety) -> std::wstring_view
    {
        switch (safety)
        {
        case RuntimeSoraStateSafety::Ok:
            return L"ok";
        case RuntimeSoraStateSafety::NotOk:
            return L"not_ok";
        case RuntimeSoraStateSafety::Unknown:
        default:
            return L"unknown";
        }
    }
}
