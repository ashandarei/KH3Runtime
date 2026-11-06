#include <memory>
#include <utility>
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/core/settings/seed_slot_options.h"
#include "kh3ap/core/events/revision_hooks.h"
#include "kh3ap/core/events/event_work.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/moogle_shop/workshop_service.h"
#include "kh3ap/features/moogle_shop/workshop_events.h"
#include "kh3ap/features/grants/keyblades/progressive_keyblade_level_helpers.h"
#include "kh3ap/bridge/transport/bridge_io_helpers.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/features/grants/abilities/ability_snapshot_helpers.h"
#include "kh3ap/core/types.h"
#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <array>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <sstream>

namespace kh3ap
{
    namespace
    {
        constexpr auto photo_request_count = 20;
        constexpr auto max_photo_request_table_capacity = 1000;
        constexpr auto max_photo_request_field_offset = 256;
        constexpr auto workshop_flag_word_bits = 32;
        constexpr auto workshop_flag_count = 256;
        constexpr auto motif_table_row_map_offset = 0x48;
        constexpr auto motif_table_row_struct_offset = 0x40;
        std::atomic<bool> workshop_object_events_ready{};
        RC::Unreal::FName motif_name, table_name;
        // Change only the request-list gates in the loaded motif table. Photo flags,
        // recipe conditions and Seven Princess/teammate detection remain native.
        struct PhotoRequestUnlocks
        {
            using FName = RC::Unreal::FName;
            struct Entry
            {
                FName name;
                uint64_t row;
                int64_t padding;
            };
            static_assert(sizeof(Entry) == 24);
            struct Gates
            {
                FName story, label;
            };
            RC::Unreal::FWeakObjectPtr table;
            uint64_t row_map_data{};
            bool applied{};
            std::array<Gates, photo_request_count> originals{};

            bool update(bool enabled)
            {
                if (!enabled && !applied)
                {
                    return true;
                }
                using namespace RC::Unreal;
                auto* object = UObjectGlobals::StaticFindObject<UObject*>(
                    nullptr, nullptr,
                    L"/Game/Load/Tres/ItemSynthesis/TresItemMotifData.TresItemMotifData");
                if (!object || !is_uobject_valid_fast(object) ||
                    object->GetClassPrivate()->GetName() != L"DataTable")
                {
                    return false;
                }
                auto header_bytes = try_read_process_memory_bytes(
                    reinterpret_cast<uint64_t>(object) + motif_table_row_map_offset,
                    sizeof(MapHeader));
                auto struct_bytes = try_read_process_memory_bytes(
                    reinterpret_cast<uint64_t>(object) + motif_table_row_struct_offset,
                    sizeof(uint64_t));
                if (!header_bytes || !struct_bytes)
                {
                    return false;
                }
                MapHeader header{};
                uint64_t struct_address{};
                std::memcpy(&header, header_bytes->data(), sizeof(header));
                std::memcpy(&struct_address, struct_bytes->data(), sizeof(struct_address));
                if (header.num != photo_request_count || header.max < header.num ||
                    header.max > max_photo_request_table_capacity)
                {
                    return false;
                }
                auto* structure = reinterpret_cast<UScriptStruct*>(struct_address);
                if (!is_uobject_valid_fast(structure) ||
                    structure->GetFullName() !=
                        L"ScriptStruct /Script/TresGame.TresItemMotifDataTable")
                {
                    return false;
                }
                const auto offset = [&](const wchar_t* name, const wchar_t* kind, int size) -> int
                {
                    auto* prop = structure->GetPropertyByNameInChain(name);
                    if (!prop || prop->GetClass().GetName() != kind ||
                        prop->GetElementSize() != size)
                    {
                        return -1;
                    }
                    const int value = prop->GetOffset_Internal();
                    return value >= 0 && value <= max_photo_request_field_offset ? value : -1;
                };
                const int story = offset(L"m_GameFlagName", L"NameProperty", 8);
                const int label = offset(L"m_GameFlagLabel", L"NameProperty", 8);
                const int flag = offset(L"m_FlagIndex", L"IntProperty", 4);
                if (story < 0 || label < 0 || flag < 0)
                {
                    return false;
                }
                if (table.Get() != object || row_map_data != header.data_ptr)
                {
                    applied = false;
                }
                if (enabled && applied)
                {
                    return true;
                }
                auto entries = try_read_process_memory_bytes(header.data_ptr,
                                                             photo_request_count * sizeof(Entry));
                if (!entries)
                {
                    return false;
                }
                std::array<uint64_t, photo_request_count> rows{};
                std::array<Gates, photo_request_count> observed{};
                for (int n = 0; n < photo_request_count; ++n)
                {
                    Entry entry{};
                    std::memcpy(&entry, entries->data() + n * sizeof(Entry), sizeof(entry));
                    auto flag_bytes =
                        try_read_process_memory_bytes(entry.row + flag, sizeof(int32_t));
                    auto story_bytes =
                        try_read_process_memory_bytes(entry.row + story, sizeof(FName));
                    auto label_bytes =
                        try_read_process_memory_bytes(entry.row + label, sizeof(FName));
                    if (!flag_bytes || !story_bytes || !label_bytes)
                    {
                        return false;
                    }
                    int index{};
                    std::memcpy(&index, flag_bytes->data(), sizeof(index));
                    if (index < 0 || index >= photo_request_count || rows[index] ||
                        entry.name.ToString() != L"IM_" + std::to_wstring(index))
                    {
                        return false;
                    }
                    rows[index] = entry.row;
                    std::memcpy(&observed[index].story, story_bytes->data(),
                                sizeof(observed[index].story));
                    std::memcpy(&observed[index].label, label_bytes->data(),
                                sizeof(observed[index].label));
                }
                if (!applied)
                {
                    if (!enabled)
                    {
                        return true;
                    }
                    originals = observed;
                }
                const auto live_table = make_uobject_weak(object);
                if (live_table.Get() != object)
                {
                    return false;
                }
                const Gates unrestricted{};
                // Validate the complete table before touching a row; no completion bits are
                // written.
                for (int n = 0; n < photo_request_count; ++n)
                {
                    const auto& gates = enabled ? unrestricted : originals[n];
                    std::memcpy(reinterpret_cast<void*>(rows[n] + story), &gates.story,
                                sizeof(gates.story));
                    std::memcpy(reinterpret_cast<void*>(rows[n] + label), &gates.label,
                                sizeof(gates.label));
                }
                table = live_table;
                row_map_data = header.data_ptr;
                applied = enabled;
                RC::Output::send<RC::LogLevel::Normal>(
                    L"[KH3APProbeCpp] Photo requests unlocked={} rows=20 "
                    L"completion_flags=preserved\n",
                    enabled);
                return true;
            }
        };

        bool bit(const std::array<uint32_t, workshop_flag_count / workshop_flag_word_bits>& words,
                 int index)
        {
            return index >= 0 && index < workshop_flag_count &&
                   (words[index / workshop_flag_word_bits] &
                    (uint32_t{1} << (index % workshop_flag_word_bits)));
        }

    } // namespace
    void notify_workshop_object_created(RC::Unreal::UObject* object)
    {
        if (!workshop_object_events_ready.load(std::memory_order_acquire) || !object)
        {
            return;
        }
        if (object->GetNamePrivate().Equals(motif_name) &&
            object->GetClassPrivate()->GetNamePrivate().Equals(table_name))
        {
            notify_workshop_changed();
        }
    }
    struct RuntimeWorkshopService::Impl
    {
        RuntimeWorkshopConfig config;
        PhotoRequestUnlocks photo_requests;
        struct Check
        {
            std::wstring type, key, keyblade_token;
            int flag;
        };
        std::vector<Check> checks;
        std::set<std::wstring> completed;
        std::filesystem::path bridge;
        std::wstring seed;
        int64_t slot{};
        bool max_levels{}, dirty{};
        RuntimeEventWork work;
        RuntimeNativeRevisionHooks mutations;
        uint64_t observed_revision{}, input_generation{}, object_revision{};
        bool hook_attempted{}, hooks_ready{};
        void install_hooks()
        {
            if (hook_attempted)
            {
                return;
            }
            hook_attempted = true;
            motif_name = RC::Unreal::FName{L"TresItemMotifData", RC::Unreal::FNAME_Add};
            table_name = RC::Unreal::FName{L"DataTable", RC::Unreal::FNAME_Add};
            workshop_object_events_ready.store(true, std::memory_order_release);
            const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            struct Site
            {
                uintptr_t rva;
                std::vector<uint8_t> bytes;
            };
            std::vector<Site> sites;
            if (kh3ap::native_build::egs.matches(nt->FileHeader.TimeDateStamp,
                                                 nt->OptionalHeader.SizeOfImage))
            {
                sites = {{0x110a660, {0x4c, 0x8b, 0xc9, 0x81, 0xfa, 0xff, 0, 0, 0}},
                         {0x169e80b, {0x09, 0x84, 0x96, 0xf8, 0x38, 0x01, 0}},
                         {0x1ac4fe9, {0x09, 0x84, 0x2a, 0xf8, 0x38, 0x01, 0}},
                         {0x13eef00,
                          {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57}}};
            }
            else if (kh3ap::native_build::steam.matches(nt->FileHeader.TimeDateStamp,
                                                        nt->OptionalHeader.SizeOfImage))
            {
                sites = {
                    {0xe8f9d7, {0x09, 0x82, 0x18, 0x06, 0, 0}},
                    // Observe the leaf setter at entry. Its final OR is directly
                    // followed by RET and cannot provide a reliable detour site.
                    {0xe96c70, {0x4c, 0x8b, 0xc9, 0x81, 0xfa, 0xff, 0, 0, 0}},
                    {0xe972c0, {0x4c, 0x8b, 0xc9, 0x81, 0xfa, 0xff, 0, 0, 0}},
                    {0x112a3c0,
                     {0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57}}};
            }
            // Verify the whole profile before installing any notifications.
            if (sites.empty() ||
                std::any_of(sites.begin(), sites.end(),
                            [&](const Site& site)
                            {
                                return std::memcmp(reinterpret_cast<void*>(base + site.rva),
                                                   site.bytes.data(), site.bytes.size()) != 0;
                            }))
            {
                RC::Output::send<RC::LogLevel::Error>(
                    L"[KH3APProbeCpp] Workshop mutation hooks unsupported; no polling fallback.\n");
                return;
            }
            hooks_ready = true;
            for (const auto& site : sites)
            {
                const bool installed = mutations.install(base + site.rva, site.bytes);
                hooks_ready = installed && hooks_ready;
                RC::Output::send<RC::LogLevel::Normal>(
                    L"[KH3APProbeCpp] Workshop mutation hook rva=0x{:X} installed={}\n", site.rva,
                    installed);
            }
            RC::Output::send<RC::LogLevel::Normal>(
                L"[KH3APProbeCpp] Workshop mutation hooks installed={} sites={}\n", hooks_ready,
                sites.size());
        }
        explicit Impl(RuntimeWorkshopConfig c) : config(std::move(c))
        {
        }
        bool scope()
        {
            auto dir = config.bridge_dir();
            if (!dir)
            {
                return false;
            }
            if (*dir == bridge)
            {
                return !checks.empty();
            }
            auto document = JsonObject::read_seed(*dir / L"slot_data.json");
            if (!document.valid())
            {
                return false;
            }
            const auto identity = document.object_field("kh3_seed");
            auto name = identity.string_field("seed_name").value_or(L"");
            auto player = identity.int64_field("slot").value_or(0);
            if (name.empty() || player < 1)
            {
                return false;
            }
            bridge = *dir;
            seed = name;
            slot = player;
            checks.clear();
            completed.clear();
            dirty = false;
            max_levels = seed_keyblade_levels_start_at_max(installed_seed_slot_data());
            for (auto& row : document.object_field("bridge").objects_field("locations"))
            {
                auto type = row.string_field("type").value_or(L"");
                if (type != L"synthesis" && type != L"photo_mission" && type != L"keyblade_upgrade")
                {
                    continue;
                }
                auto source = row.string_field("source_id").value_or(L"");
                auto key = row.string_field("bridge_key").value_or(L"");
                auto flag = row.int64_field("workshop_flag").value_or(-1);
                auto location = row.int64_field("ap_location_id").value_or(0);
                if (flag < 0 || flag >= workshop_flag_count || location <= 0)
                {
                    continue;
                }
                const auto prefix = type == L"synthesis"       ? L"SynthesisItem:"
                                    : type == L"photo_mission" ? L"PhotoMission:"
                                                               : L"WeaponEnhance:";
                if (key != prefix + source)
                {
                    continue;
                }
                checks.push_back({type, key, row.string_field("keyblade_token").value_or(L""),
                                  static_cast<int>(flag)});
            }
            auto saved = read_text_file(bridge / channels::runtime_workshop_file);
            if (saved &&
                JsonFieldReader::string_field(*saved, "schema").value_or(L"") ==
                    L"kh3ap.workshop.v1" &&
                JsonFieldReader::string_field(*saved, "source").value_or(L"") ==
                    L"native_workshop_flags" &&
                JsonFieldReader::string_field(*saved, "seed_name").value_or(L"") == seed &&
                JsonFieldReader::int64_field(*saved, "slot").value_or(0) == slot)
            {
                const auto earned = JsonFieldReader::string_array_field(*saved, "completed_checks");
                for (auto& check : checks)
                {
                    for (auto& key : earned)
                    {
                        if (check.key == key)
                        {
                            completed.insert(key);
                        }
                    }
                }
            }
            return !checks.empty();
        }
        bool persist()
        {
            std::ostringstream json;
            json << "{\"schema\":\"kh3ap.workshop.v1\",\"source\":\"native_workshop_flags\",\"seed_"
                    "name\":\""
                 << json_escape(seed) << "\",\"slot\":" << slot << ",\"completed_checks\":[";
            bool first = true;
            for (auto& key : completed)
            {
                if (!first)
                {
                    json << ',';
                }
                first = false;
                json << '"' << json_escape(key) << '"';
            }
            json << "]}\n";
            return write_text_file_atomic_verified(bridge / channels::runtime_workshop_file,
                                                   json.str());
        }
        void tick()
        {
            const auto now = std::chrono::steady_clock::now();
            install_hooks();
            const auto revision = mutations.revision();
            const auto inputs = pipe::input_generation();
            const auto objects = workshop_update_revision.load(std::memory_order_acquire);
            if (revision != observed_revision || inputs != input_generation ||
                objects != object_revision)
            {
                observed_revision = revision;
                input_generation = inputs;
                object_revision = objects;
                work.request();
            }
            if (!work.take(now))
            {
                return;
            }
            const auto retry = [&]
            {
                work.finish(now, false);
            };
            if (!config.gameplay_ready())
            {
                retry();
                return;
            }
            const bool active = scope();
            const bool photos = std::any_of(checks.begin(), checks.end(),
                                            [](const Check& check)
                                            {
                                                return check.type == L"photo_mission";
                                            });
            if (!photo_requests.update(photos))
            {
                retry();
                return;
            }
            if (!active)
            {
                return;
            }
            const RuntimeHotPathProfileScope profile{L"workshop:completion_read"};
            NativeWorkshopSnapshot snapshot;
            if (!try_query_native_workshop(snapshot, nullptr))
            {
                retry();
                return;
            }
            std::map<std::wstring, bool> owned;
            for (auto& check : checks)
            {
                if (completed.contains(check.key))
                {
                    continue;
                }
                bool earned = false;
                if (check.type == L"synthesis")
                {
                    earned = bit(snapshot.synthesis, check.flag);
                }
                else if (check.type == L"photo_mission")
                {
                    earned = bit(snapshot.photos, check.flag);
                }
                else if (max_levels || bit(snapshot.enhancements, check.flag))
                {
                    if (!check.keyblade_token.empty())
                    {
                        auto [it, inserted] = owned.try_emplace(check.keyblade_token, false);
                        if (inserted)
                        {
                            it->second = config.owns_item(check.keyblade_token);
                        }
                        earned = it->second;
                    }
                }
                if (earned)
                {
                    completed.insert(check.key);
                    dirty = true;
                }
            }
            if (dirty)
            {
                if (!persist())
                {
                    retry();
                    return;
                }
                dirty = false;
            }
        }
    };
    RuntimeWorkshopService::RuntimeWorkshopService(RuntimeWorkshopConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }
    RuntimeWorkshopService::~RuntimeWorkshopService() = default;
    void RuntimeWorkshopService::tick()
    {
        impl_->tick();
    }
    void RuntimeWorkshopService::request_refresh()
    {
        impl_->work.request();
    }
} // namespace kh3ap
