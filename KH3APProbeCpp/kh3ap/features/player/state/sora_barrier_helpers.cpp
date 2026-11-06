#include "kh3ap/features/player/state/sora_barrier_helpers.h"

#include "kh3ap/features/player/objects/object_resolver.h"
#include "kh3ap/core/reflection/reflection_core.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Property/FMapProperty.hpp>
#include <Unreal/UObject.hpp>

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr int native_form_ability_value_bytes = 32;
        constexpr uint64_t form_key_byte_mask = 0xff;

        constexpr auto max_barrier_map_entries = 32;
        constexpr auto max_barrier_map_capacity = 64;
        constexpr auto max_text_ability_entry_bytes = 8;
    }

    using namespace RC;
    using namespace RC::Unreal;

    namespace
    {
        constexpr uint8_t s_normal_form_key = 0;
        constexpr uint8_t s_guardian_form_key = 3;
        constexpr uint8_t s_barrier_text_ability = 0x5B;
        constexpr uint8_t s_barrier_cracker_text_ability = 0x5C;

        struct ArrayHeader
        {
            uint64_t data_ptr{};
            int32_t num{};
            int32_t max{};
        };

        struct BarrierMapHeader
        {
            uint64_t data_ptr{};
            int32_t num{};
            int32_t max{};
        };

        struct FormAbilityMapElementRaw
        {
            uint64_t key{};
            ArrayHeader ability{};
            ArrayHeader text_ability{};
            int32_t hash_next{};
            int32_t hash_index{};
        };

        struct BarrierInjectionState
        {
            std::mutex mutex{};
            UObject* form_ability_asset{};
            std::wstring original_map_text{};
            int32_t original_map_num{};
            bool injected{};
        };

        BarrierInjectionState s_injection_state{};

        struct FormAbilityMapView
        {
            UObject* asset{};
            FMapProperty* property{};
            void* value_ptr{};
            int text_ability_element_size{1};
        };

        auto set_status(std::wstring* status_details, std::wstring value) -> void
        {
            if (status_details)
            {
                *status_details = std::move(value);
            }
        }

        auto resolve_form_ability_map(UObject* sora_pawn, std::wstring* failure_details)
                -> std::optional<FormAbilityMapView>
        {
            if (!sora_pawn || !kh3ap::is_uobject_valid_fast(sora_pawn) ||
                !RuntimeObjectResolver::is_sora_ability_pawn_candidate(sora_pawn))
            {
                set_status(failure_details, STR("barrier_context_not_sora_pawn"));
                return std::nullopt;
            }

            UObject* const equipment = try_read_reflected_object_property_value(sora_pawn, STR("MyEquipment"));
            if (!equipment)
            {
                set_status(failure_details, STR("sora_equipment_unavailable"));
                return std::nullopt;
            }

            UObject* const asset = try_read_reflected_object_property_value(equipment, STR("m_FormAbilityAsset"));
            if (!asset)
            {
                set_status(failure_details, STR("sora_form_ability_asset_unavailable"));
                return std::nullopt;
            }

            FProperty* const property = asset->GetPropertyByNameInChain(STR("m_Data"));
            void* const value_ptr = asset->GetValuePtrByPropertyNameInChain(STR("m_Data"));
            if (!property || !value_ptr || property->GetClass().GetName() != STR("MapProperty"))
            {
                set_status(failure_details, STR("sora_form_ability_map_unavailable"));
                return std::nullopt;
            }

            auto* const map_property = static_cast<FMapProperty*>(property);
            FProperty* const key_property = const_cast<FProperty*>(map_property->GetKeyProp());
            FProperty* const value_property = const_cast<FProperty*>(map_property->GetValueProp());
            if (!key_property || !value_property || key_property->GetElementSize() != 1 ||
                value_property->GetClass().GetName() != STR("StructProperty") ||
                value_property->GetElementSize() != native_form_ability_value_bytes)
            {
                set_status(
                        failure_details,
                        std::format(
                                STR("unexpected_form_ability_map_layout:key_size={} value_class={} value_size={}"),
                                key_property ? key_property->GetElementSize() : -1,
                                value_property ? value_property->GetClass().GetName() : std::wstring{STR("<none>")},
                                value_property ? value_property->GetElementSize() : -1));
                return std::nullopt;
            }

            return FormAbilityMapView{
                    .asset = asset,
                    .property = map_property,
                    .value_ptr = value_ptr,
                    .text_ability_element_size = 1,
            };
        }

        auto read_map_header(const FormAbilityMapView& view) -> BarrierMapHeader
        {
            BarrierMapHeader header{};
            std::memcpy(&header, view.value_ptr, sizeof(header));
            return header;
        }

        auto array_contains_text_ability(
                const ArrayHeader& header,
                int element_size,
                uint8_t ability) -> bool
        {
            if (!header.data_ptr || header.num <= 0 || header.num > max_barrier_map_entries || header.max < header.num || header.max > max_barrier_map_capacity ||
                element_size <= 0 || element_size > max_text_ability_entry_bytes)
            {
                return false;
            }
            const auto* const bytes = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(header.data_ptr));
            for (int32_t index = 0; index < header.num; ++index)
            {
                if (bytes[static_cast<size_t>(index) * static_cast<size_t>(element_size)] == ability)
                {
                    return true;
                }
            }
            return false;
        }

        auto find_form_entry(
                const FormAbilityMapView& view,
                uint8_t form_key,
                bool require_barrier) -> const FormAbilityMapElementRaw*
        {
            const BarrierMapHeader header = read_map_header(view);
            if (!header.data_ptr || header.num <= 0 || header.num > max_barrier_map_entries || header.max < header.num || header.max > max_barrier_map_capacity)
            {
                return nullptr;
            }

            const auto* const entries =
                    reinterpret_cast<const FormAbilityMapElementRaw*>(static_cast<uintptr_t>(header.data_ptr));
            for (int32_t index = 0; index < header.num; ++index)
            {
                const FormAbilityMapElementRaw& entry = entries[index];
                if (static_cast<uint8_t>(entry.key & form_key_byte_mask) != form_key)
                {
                    continue;
                }
                if (!require_barrier ||
                    (array_contains_text_ability(entry.text_ability, view.text_ability_element_size, s_barrier_text_ability) &&
                     array_contains_text_ability(entry.text_ability, view.text_ability_element_size, s_barrier_cracker_text_ability)))
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        auto export_map_text(const FormAbilityMapView& view) -> std::wstring
        {
            // FStringOut's non-exported destructor pulls duplicate UE4SS/Unreal
            // definitions into C++ mods. A zeroed FString has the same TArray
            // storage shape and ExportTextItem initializes it normally.
            struct FStringStorage
            {
                wchar_t* data{};
                int32_t num{};
                int32_t max{};
            };
            static_assert(sizeof(FStringStorage) == sizeof(FString));
            FStringStorage storage{};
            auto& exported = *reinterpret_cast<FString*>(&storage);
            view.property->ExportTextItem(exported, view.value_ptr, nullptr, view.asset, 0);
            const wchar_t* const text = exported.GetCharArray();
            std::wstring result = text ? std::wstring{text} : std::wstring{};
            exported.Clear();
            return result;
        }

        auto find_matching_close(std::wstring_view text, size_t open_index) -> size_t
        {
            if (open_index >= text.size() || text[open_index] != L'(')
            {
                return std::wstring_view::npos;
            }
            int depth = 0;
            bool in_quotes = false;
            for (size_t index = open_index; index < text.size(); ++index)
            {
                const wchar_t ch = text[index];
                if (ch == L'"' && (index == 0 || text[index - 1] != L'\\'))
                {
                    in_quotes = !in_quotes;
                    continue;
                }
                if (in_quotes)
                {
                    continue;
                }
                if (ch == L'(')
                {
                    ++depth;
                }
                else if (ch == L')' && --depth == 0)
                {
                    return index;
                }
            }
            return std::wstring_view::npos;
        }

        auto immediate_map_entries(std::wstring_view map_text) -> std::vector<std::wstring>
        {
            std::vector<std::wstring> entries{};
            const size_t root_open = map_text.find(L'(');
            if (root_open == std::wstring_view::npos)
            {
                return entries;
            }
            const size_t root_close = find_matching_close(map_text, root_open);
            if (root_close == std::wstring_view::npos)
            {
                return entries;
            }

            for (size_t index = root_open + 1; index < root_close; ++index)
            {
                if (map_text[index] != L'(')
                {
                    continue;
                }
                const size_t entry_close = find_matching_close(map_text, index);
                if (entry_close == std::wstring_view::npos || entry_close > root_close)
                {
                    return {};
                }
                entries.emplace_back(map_text.substr(index, entry_close - index + 1));
                index = entry_close;
            }
            return entries;
        }

        auto find_entry_key_separator(std::wstring_view entry) -> size_t
        {
            int depth = 0;
            bool in_quotes = false;
            for (size_t index = 0; index < entry.size(); ++index)
            {
                const wchar_t ch = entry[index];
                if (ch == L'"' && (index == 0 || entry[index - 1] != L'\\'))
                {
                    in_quotes = !in_quotes;
                    continue;
                }
                if (in_quotes)
                {
                    continue;
                }
                if (ch == L'(')
                {
                    ++depth;
                }
                else if (ch == L')')
                {
                    --depth;
                }
                else if (depth == 1 && (ch == L',' || ch == L'='))
                {
                    return index;
                }
            }
            return std::wstring_view::npos;
        }

        auto make_normal_form_barrier_entry(std::wstring guardian_entry) -> std::optional<std::wstring>
        {
            const size_t separator = find_entry_key_separator(guardian_entry);
            if (separator == std::wstring::npos || guardian_entry.empty() || guardian_entry.front() != L'(')
            {
                return std::nullopt;
            }
            guardian_entry.replace(1, separator - 1, L"0");

            // Copy the complete Guardian row.  The earlier conservative probe
            // erased m_Ability and therefore injected only the Barrier and
            // Barrier Cracker menu-text entries; KH3 never received the seven
            // functional Guardian abilities that back those actions.  Keeping
            // m_Ability lets the normal-form equipment resolver initialize the
            // actions without forcing Guardian's form selectors or timer.
            return guardian_entry;
        }

        auto build_injected_map_text(std::wstring_view original) -> std::optional<std::wstring>
        {
            const std::vector<std::wstring> entries = immediate_map_entries(original);
            const auto guardian_it = std::find_if(entries.begin(), entries.end(), [](const std::wstring& entry) {
                return entry.find(L"TEXT_ACTION_REFLECT") != std::wstring::npos &&
                       entry.find(L"TEXT_ACTION_BARRIER_CRACKER") != std::wstring::npos;
            });
            if (guardian_it == entries.end())
            {
                return std::nullopt;
            }
            auto normal_entry = make_normal_form_barrier_entry(*guardian_it);
            if (!normal_entry.has_value())
            {
                return std::nullopt;
            }

            const size_t root_open = original.find(L'(');
            const size_t root_close = find_matching_close(original, root_open);
            if (root_close == std::wstring_view::npos)
            {
                return std::nullopt;
            }
            std::wstring injected{original};
            injected.insert(root_close, std::format(L",{}", *normal_entry));
            return injected;
        }

        auto import_map_text(const FormAbilityMapView& view, std::wstring_view text) -> bool
        {
            using InstalledImportTextFn = const wchar_t* (*)(
                    const FProperty*,
                    const wchar_t*,
                    void*,
                    int32_t,
                    UObject*,
                    FOutputDevice*);
            static InstalledImportTextFn import_text = []() -> InstalledImportTextFn {
                HMODULE const ue4ss = GetModuleHandleW(L"UE4SS.dll");
                if (!ue4ss)
                {
                    return nullptr;
                }
                // The installed UE4SS exposes the newer const FProperty member
                // ABI, while the probe build headers still declare the older
                // non-const ABI. Resolve it explicitly to remain load-compatible.
                constexpr const char* installed_symbol =
                        "?ImportText@FProperty@Unreal@RC@@QEBAPEB_WPEB_WPEAXHPEAVUObject@23@PEAVFOutputDevice@23@@Z";
                return reinterpret_cast<InstalledImportTextFn>(GetProcAddress(ue4ss, installed_symbol));
            }();
            if (!import_text)
            {
                return false;
            }
            const std::wstring owned_text{text};
            return import_text(view.property, owned_text.c_str(), view.value_ptr, 0, view.asset, nullptr) != nullptr;
        }
    }

    auto try_inject_runtime_sora_normal_form_barrier(
            UObject* sora_pawn,
            std::wstring* status_details) -> bool
    {
        if (status_details)
        {
            status_details->clear();
        }
        auto view = resolve_form_ability_map(sora_pawn, status_details);
        if (!view.has_value())
        {
            return false;
        }

        std::scoped_lock lock{s_injection_state.mutex};
        if (s_injection_state.injected)
        {
            if (s_injection_state.form_ability_asset == view->asset &&
                find_form_entry(*view, s_normal_form_key, true))
            {
                set_status(status_details, STR("sora_normal_form_barrier_already_injected"));
                return true;
            }
            set_status(status_details, STR("stale_barrier_injection_state_restart_required"));
            return false;
        }

        const BarrierMapHeader original_header = read_map_header(*view);
        if (!find_form_entry(*view, s_guardian_form_key, true))
        {
            set_status(status_details, STR("guardian_form_barrier_source_missing"));
            return false;
        }
        if (find_form_entry(*view, s_normal_form_key, false))
        {
            set_status(status_details, STR("normal_form_entry_already_exists_not_overwriting"));
            return false;
        }

        try
        {
            const std::wstring original_text = export_map_text(*view);
            auto injected_text = build_injected_map_text(original_text);
            if (!injected_text.has_value())
            {
                set_status(status_details, STR("unable_to_build_barrier_map_import_text"));
                return false;
            }
            if (!import_map_text(*view, *injected_text) || !find_form_entry(*view, s_normal_form_key, true))
            {
                const bool restored = !original_text.empty() && import_map_text(*view, original_text);
                set_status(
                        status_details,
                        restored ? STR("barrier_map_import_failed_original_restored")
                                 : STR("barrier_map_import_failed_restore_failed_restart_required"));
                return false;
            }

            s_injection_state.form_ability_asset = view->asset;
            s_injection_state.original_map_text = original_text;
            s_injection_state.original_map_num = original_header.num;
            s_injection_state.injected = true;
            set_status(
                    status_details,
                    std::format(
                            STR("sora_normal_form_barrier_injected source_form={} full_guardian_ability_row=true text_abilities=0x{:02X},0x{:02X}"),
                            s_guardian_form_key,
                            s_barrier_text_ability,
                            s_barrier_cracker_text_ability));
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Sora normal-form Barrier injected asset={} original_map_num={} new_map_num={}\n"),
                    view->asset->GetFullName(),
                    original_header.num,
                    read_map_header(*view).num);
            return true;
        }
        catch (const std::exception& error)
        {
            const std::string_view narrow_message{error.what()};
            set_status(
                    status_details,
                    std::format(
                            STR("barrier_map_exception:{}"),
                            std::wstring{narrow_message.begin(), narrow_message.end()}));
            return false;
        }
    }

    auto try_remove_runtime_sora_normal_form_barrier(
            UObject* sora_pawn,
            std::wstring* status_details) -> bool
    {
        if (status_details)
        {
            status_details->clear();
        }
        auto view = resolve_form_ability_map(sora_pawn, status_details);
        if (!view.has_value())
        {
            return false;
        }

        std::scoped_lock lock{s_injection_state.mutex};
        if (!s_injection_state.injected)
        {
            set_status(status_details, STR("sora_normal_form_barrier_not_injected"));
            return true;
        }
        if (s_injection_state.form_ability_asset != view->asset || s_injection_state.original_map_text.empty())
        {
            set_status(status_details, STR("barrier_injection_asset_changed_restart_required"));
            return false;
        }

        try
        {
            if (!import_map_text(*view, s_injection_state.original_map_text))
            {
                set_status(status_details, STR("barrier_original_map_restore_import_failed"));
                return false;
            }
            const BarrierMapHeader restored_header = read_map_header(*view);
            if (restored_header.num != s_injection_state.original_map_num ||
                find_form_entry(*view, s_normal_form_key, true))
            {
                set_status(status_details, STR("barrier_original_map_restore_verification_failed"));
                return false;
            }

            s_injection_state.form_ability_asset = nullptr;
            s_injection_state.original_map_text.clear();
            s_injection_state.original_map_num = 0;
            s_injection_state.injected = false;
            set_status(status_details, STR("sora_normal_form_barrier_removed"));
            Output::send<LogLevel::Verbose>(
                    STR("[KH3APProbeCpp] Sora normal-form Barrier removed; original form map restored\n"));
            return true;
        }
        catch (const std::exception& error)
        {
            const std::string_view narrow_message{error.what()};
            set_status(
                    status_details,
                    std::format(
                            STR("barrier_restore_exception:{}"),
                            std::wstring{narrow_message.begin(), narrow_message.end()}));
            return false;
        }
    }
}
