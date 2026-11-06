#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/bridge/transport/bridge_channels.h"
#include "kh3ap/features/world/space/space_access.h"
#include "kh3ap/features/world/space/space_policy.h"
#include "kh3ap/features/world/portals/portal_policy.h"
#include "kh3ap/bridge/utilities.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include <fstream>
#include <sstream>

namespace kh3ap
{
    void RuntimeSpaceAccess::refresh(const ConfigureRewards& configure_rewards)
    {
        const auto directory = bridge_resolver();
        if (!directory) { access_ready = false; allowed.clear(); return; }
        if (bridge != *directory) configure_rewards(*directory, L"", 0, false);
        constexpr std::array names{channels::ap_status_file, channels::ap_items_file};
        std::array<std::filesystem::file_time_type, 2> times{};
        std::array<uint64_t, 2> revisions{};
        for (size_t i = 0; i < names.size(); ++i)
        {
            revisions[i] = pipe::input_revision(*directory / names[i]);
            if (revisions[i]) continue;
            std::error_code error;
            times[i] = std::filesystem::last_write_time(*directory / names[i], error);
            if (error && i == 0) { access_ready = false; allowed.clear(); return; }
        }
        if (access_ready && bridge == *directory && times == input_times && revisions == input_revisions) return;
        access_ready = false;
        allowed.clear(); items.clear();
        party_portals = false;
        kh02_enabled = false;
        bridge = *directory;
        auto read_snapshot = [&](size_t i) -> std::optional<std::string> {
            const auto path = bridge / names[i];
            if (revisions[i]) return read_text_file(path);
            // Before a client publishes this process's IPC state, preserve
            // offline access using its durable randomizer receipt snapshot.
            // Input revisions retain deletion tombstones, even when SET
            // and DELETE both arrived before this service's first read.
            std::ifstream input(path, std::ios::binary);
            if (!input) return std::nullopt;
            std::ostringstream text; text << input.rdbuf(); return text.str();
        };
        const auto status = read_snapshot(0);
        const auto receipts = read_snapshot(1);
        if (!status) return;
        if (!slot_settings) {
            const auto& settings = installed_seed_slot_data();
            if (!settings.valid()) return;
            if(settings.string_field("protocol_name") != L"kh3_ap_file_bridge" ||
                settings.int64_field("protocol_version") != 1) return;
            const auto slot_seed = settings.object_field("kh3_seed");
            slot_settings = SlotSettings{
                slot_seed.string_field("seed_name").value_or(L""),
                slot_seed.int64_field("slot").value_or(0),
                static_cast<int>(settings.int64_field("heart_piece_count").value_or(0)),
                static_cast<int>(settings.int64_field("worlds_required").value_or(-1)),
                settings.int64_field("include_space").value_or(0) == 1,
                settings.int64_field("portal_unlocks").value_or(0) == 1,
                settings.int64_field("kh02_enabled").value_or(0) == 1};
        }
        if (JsonFieldReader::string_field(*status, "protocol_name") != L"kh3_ap_file_bridge" ||
            JsonFieldReader::int64_field(*status, "protocol_version") != 1) return;
        seed = JsonFieldReader::string_field(*status, "seed_name").value_or(L"");
        if (seed.empty() || seed != slot_settings->seed) {
            configure_rewards(bridge, L"", 0, false);
            return;
        }
        ap_slot = slot_settings->slot;
        heart_piece_count = slot_settings->heart_piece_count;
        worlds_required = slot_settings->worlds_required;
        include_space = slot_settings->include_space;
        party_portals = slot_settings->party_portals;
        kh02_enabled = slot_settings->kh02_enabled;
        // Reward ownership follows the verified seed option. Temporary
        // receipt resynchronization must not turn native loot back on.
        configure_rewards(bridge, seed, ap_slot, include_space);
        if (!receipts || JsonFieldReader::string_field(*receipts, "protocol_name") != L"kh3_ap_file_bridge" ||
            JsonFieldReader::int64_field(*receipts, "protocol_version") != 1) return;
        int64_t expected_index = 0;
        for (const auto& row : JsonFieldReader::object_array_field(*receipts, "items"))
        {
            if (JsonFieldReader::int64_field(row, "index") != expected_index++) return;
            if (const auto name = JsonFieldReader::string_field(row, "item_name")) items.insert(*name);
            else return;
        }
        if (JsonFieldReader::int64_field(*status, "received_item_count") != expected_index) return;
        for (const auto& entry : space::landings)
            if (auto has = [this](std::wstring_view name) { return items.contains(std::wstring{name}); };
                party_portals ? portals::allowed(entry.code, has) : space::landing_allowed(entry.code, has))
                allowed.insert(std::wstring{entry.code});
        if (party_portals && portals::allowed(L"ss", [this](std::wstring_view name) { return items.contains(std::wstring{name}); }))
            allowed.insert(L"ss");
        access_ready = true;
        input_times = times;
        input_revisions = revisions;
    }
}
