#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/world/space/space_access.h"
#include "kh3ap/features/world/portals/portal_policy.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include "kh3ap/bridge/utilities.h"
#include <cassert>
#include <fstream>
#include <map>

namespace
{
    // In-memory client publications; nonzero revisions include tombstones.
    std::map<std::filesystem::path, uint64_t> revisions;
    std::map<std::filesystem::path, std::string> snapshots;
    kh3ap::JsonObject pak_options;
    size_t reads{};
    size_t slot_reads{};
}
namespace kh3ap::pipe
{
    uint64_t input_revision(const std::filesystem::path& path) { return revisions[path]; }
}
namespace kh3ap
{
    const JsonObject& installed_seed_slot_data() { return pak_options; }
    std::optional<std::string> read_text_file(const std::filesystem::path& path)
    {
        ++reads;
        if(path.filename()=="slot_data.json")++slot_reads;
        const auto found = snapshots.find(path);
        return found == snapshots.end() ? std::nullopt : std::optional{found->second};
    }
}

int main()
{
    using kh3ap::RuntimeSpaceAccess;
    const auto root = std::filesystem::current_path() / "space-access-fixture";
    std::filesystem::create_directories(root);
    std::optional<std::filesystem::path> directory = root;
    const std::string protocol = R"("protocol_name":"kh3_ap_file_bridge","protocol_version":1,)";
    const auto status = "{" + protocol + R"("seed_name":"seed","received_item_count":1})";
    const auto slot = "{" + protocol + R"("kh3_seed":{"seed_name":"seed","slot":7},"include_space":1})";
    const auto items = "{" + protocol + R"("items":[{"index":0,"item_name":"Nano Gear"}]})";
    for (const auto& [name, text] : std::map<std::string, std::string>{
        {"ap_status.json", status}, {"slot_data.json", slot}, {"ap_items.json", items}})
        std::ofstream(root / name) << text;
    // Pak options work before the client publishes any bridge slot data.
    pak_options = kh3ap::JsonObject{slot};

    RuntimeSpaceAccess access([&] { return directory; });
    std::wstring reward_seed;
    bool rewards_enabled{};
    int configurations{};
    const auto configure = [&](const auto& path, const auto& seed, int64_t player, bool enabled) {
        assert(path == root);
        // A verified slot remains selected when space checks are disabled.
        assert(player == (seed.empty() ? 0 : 7));
        reward_seed = seed;
        rewards_enabled = enabled;
        ++configurations;
    };
    access.refresh(configure);
    assert(access.access_ready && access.allowed == std::set<std::wstring>{L"bx"});
    assert(rewards_enabled && reward_seed == L"seed" && reads == 0);
    const auto configured = configurations;
    access.refresh(configure);
    assert(configurations == configured); // unchanged offline bootstrap is cached

    for (const auto& name : {"ap_status.json", "slot_data.json", "ap_items.json"}) {
        revisions[root / name] = 1;
    }
    snapshots[root / "ap_status.json"] = status;
    snapshots[root / "slot_data.json"] = slot;
    // DELETE before the first live read must not resurrect Nano Gear from disk.
    access.refresh(configure);
    assert(!access.access_ready && access.allowed.empty());
    assert(rewards_enabled); // receipt resync must not restore vanilla rewards

    snapshots[root / "ap_items.json"] = items;
    revisions[root / "ap_items.json"]++;
    access.refresh(configure);
    assert(access.access_ready && access.allowed.contains(L"bx"));
    const auto read_count = reads;
    access.refresh(configure);
    assert(reads == read_count);
    // Normal check/status and receipt updates must not reread the much larger
    // static seed catalog. The changing status/receipts stay authoritative.
    const auto slot_read_count = slot_reads;
    snapshots[root / "ap_status.json"] = "{" + protocol + R"("seed_name":"seed","received_item_count":2})";
    revisions[root / "ap_status.json"]++;
    access.refresh(configure);
    assert(!access.access_ready && slot_reads == slot_read_count);
    snapshots[root / "ap_status.json"] = status;
    revisions[root / "ap_status.json"]++;
    access.refresh(configure);
    assert(access.access_ready && slot_reads == slot_read_count);
    revisions[root / "ap_items.json"]++;
    access.refresh(configure);
    assert(access.access_ready && slot_reads == slot_read_count);

    // Garden gating/portraits and Gummi landings use the same single-character
    // policy. Companion receipts and Keyblades cannot substitute in party mode.
    pak_options = kh3ap::JsonObject{ "{" + protocol + R"("kh3_seed":{"seed_name":"seed","slot":7},"include_space":1,"portal_unlocks":1})"};
    access = RuntimeSpaceAccess([&] { return directory; });
    revisions[root / "slot_data.json"]++;
    struct PortalCase {
        int id;
        std::wstring_view code, required, portrait;
        std::string_view companion, keyblade, receipt;
    };
    for (const auto& test : {
        PortalCase{4, L"he", L"Party Member: Hercules", L"MenuFaceHeHercules",
                   "Party Member: Donald", "Hero's Origin", "Party Member: Hercules"},
        PortalCase{13, L"tt", L"Party Member: Roxas", L"MenuFaceExRoxas",
                   "Party Member: Lea", "Shooting Star", "Party Member: Roxas"},
        PortalCase{7, L"ra", L"Party Member: Rapunzel", L"MenuFaceRaRapunzel",
                   "Party Member: Flynn Rider", "Ever After", "Party Member: Rapunzel"},
        PortalCase{11, L"mi", L"Party Member: Sulley", L"MenuFaceMiSulley",
                   "Party Member: Mike", "Happy Gear", "Party Member: Sulley"},
        PortalCase{12, L"ts", L"Party Member: Woody", L"MenuFaceTsWoody",
                   "Party Member: Buzz Lightyear", "Favorite Deputy", "Party Member: Woody"},
        PortalCase{6, L"fz", L"Party Member: Marshmallow", L"MenuFaceFzMarshmallow",
                   "Party Member: Xion", "Crystal Snow", "Party Member: Marshmallow"},
        PortalCase{5, L"ca", L"Party Member: Captain Jack Sparrow", L"MenuFaceCaJackSparrow",
                   "Party Member: Goofy", "Wheel of Fate", "Party Member: Captain Jack Sparrow"},
        PortalCase{10, L"bx", L"Party Member: Baymax", L"MenuFaceBxBaymax",
                   "Party Member: Hiro", "Nano Gear", "Party Member: Baymax"},
        PortalCase{3, L"kg", L"Party Member: Ventus", L"MenuFaceExVentus",
                   "Party Member: Terra", "Starlight", "Party Member: Ventus"},
    }) {
        const auto* definition = kh3ap::portals::by_id(test.id);
        assert(definition && definition->code == test.code && definition->first == test.required);
        assert(definition->second.empty() && definition->portrait_b.empty());
        assert(definition->portrait_a == test.portrait);
        for (const auto receipt : {test.companion, test.keyblade, test.receipt}) {
            snapshots[root / "ap_items.json"] = "{" + protocol +
                R"("items":[{"index":0,"item_name":")" + std::string{receipt} + R"("}]})";
            revisions[root / "ap_items.json"]++;
            access.refresh(configure);
            assert(access.access_ready && access.party_portals);
            const auto has = [&](std::wstring_view name) { return access.items.contains(std::wstring{name}); };
            const bool expected = receipt == test.receipt;
            assert(kh3ap::portals::allowed(*definition, has) == expected);
            const auto expected_worlds = expected
                ? std::set<std::wstring>{L"ss", std::wstring{test.code}}
                : std::set<std::wstring>{L"ss"};
            assert(access.allowed == expected_worlds);
        }

        // Turning off space checks does not turn off character world gates.
        pak_options = kh3ap::JsonObject{ "{" + protocol + R"("kh3_seed":{"seed_name":"seed","slot":7},"include_space":0,"portal_unlocks":1})"};
        access = RuntimeSpaceAccess([&] { return directory; });
        revisions[root / "slot_data.json"]++;
        access.refresh(configure);
        assert(access.access_ready && access.party_portals && !access.include_space);
        assert((access.allowed == std::set<std::wstring>{L"ss", std::wstring{test.code}}));

        // Switch the setting with the same receipts: the character alone must
        // no longer open this world; its Keyblade now owns access.
        pak_options = kh3ap::JsonObject{ "{" + protocol + R"("kh3_seed":{"seed_name":"seed","slot":7},"include_space":1,"portal_unlocks":0})"};
        access = RuntimeSpaceAccess([&] { return directory; });
        revisions[root / "slot_data.json"]++;
        access.refresh(configure);
        assert(access.access_ready && !access.party_portals && access.allowed.empty());
        snapshots[root / "ap_items.json"] = "{" + protocol +
            R"("items":[{"index":0,"item_name":")" + std::string{test.keyblade} + R"("}]})";
        revisions[root / "ap_items.json"]++;
        access.refresh(configure);
        assert((access.allowed == std::set<std::wstring>{std::wstring{test.code}}));
        pak_options = kh3ap::JsonObject{ "{" + protocol + R"("kh3_seed":{"seed_name":"seed","slot":7},"include_space":1,"portal_unlocks":1})"};
        access = RuntimeSpaceAccess([&] { return directory; });
        revisions[root / "slot_data.json"]++;
        access.refresh(configure);
        assert(access.access_ready && access.party_portals && access.allowed == std::set<std::wstring>{L"ss"});
    }

    // Only the chapter's master switch controls 0.2 portal routing. Leaving
    // optional challenges enabled must not reactivate a disabled chapter.
    for (const int enabled : {0, 1}) {
        for (const int challenges : {0, 1}) {
            pak_options = kh3ap::JsonObject{"{" + protocol +
                R"("kh3_seed":{"seed_name":"seed","slot":7},"include_space":1,"kh02_enabled":)" +
                std::to_string(enabled) + R"(,"kh02_challenges":)" + std::to_string(challenges) + "}"};
            access = RuntimeSpaceAccess([&] { return directory; });
            access.refresh(configure);
            assert(access.access_ready);
            assert(access.kh02_enabled == (enabled == 1));
        }
    }

    snapshots[root / "ap_items.json"] = "{" + protocol + R"("items":[{"index":1,"item_name":"Nano Gear"}]})";
    revisions[root / "ap_items.json"]++;
    access.refresh(configure);
    assert(!access.access_ready && access.allowed.empty() && rewards_enabled);
    snapshots[root / "ap_items.json"] = items;
    // Conflicting and deleted loose options cannot change the installed seed.
    snapshots[root / "slot_data.json"] = "{" + protocol + R"("kh3_seed":{"seed_name":"other","slot":7},"include_space":0})";
    revisions[root / "slot_data.json"]++;
    access.refresh(configure);
    assert(access.access_ready && rewards_enabled && reward_seed == L"seed");
    snapshots.erase(root / "slot_data.json");
    revisions[root / "slot_data.json"]++;
    access.refresh(configure);
    assert(access.access_ready && rewards_enabled);
    assert(slot_reads == 0);
    // Live AP status still must match the pak, even with valid receipts.
    snapshots[root / "ap_status.json"] = "{" + protocol + R"("seed_name":"other","received_item_count":1})";
    revisions[root / "ap_status.json"]++;
    access.refresh(configure);
    assert(!access.access_ready && !rewards_enabled && reward_seed.empty());
    directory.reset();
    access.refresh(configure);
    assert(!access.access_ready && access.allowed.empty());

    // Remove only the three randomizer-owned fixture files created above.
    for (const auto& name : {"ap_status.json", "slot_data.json", "ap_items.json"})
        std::filesystem::remove(root / name);
    std::filesystem::remove(root);
}
