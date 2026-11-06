#include "kh3ap/features/enemies/remap_catalog.h"
#include "kh3ap/features/enemies/boss_remap_policy.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>

using namespace kh3ap;
using namespace kh3ap::enemy_remap_detail;
using nlohmann::json;

static json boss_manifest()
{
    return {{"boss_runtime_remap", {
        {"boss_runtime_format_version", 1},
        {"boss_runtime_entry_count", 1},
        {"boss_runtime_classes", {"/Game/Blueprints/Enemy/source.source_C", "/Game/Blueprints/Enemy/target.target_C"}},
        {"boss_runtime_entries", {"BOSS_TEST|0|1"}}
    }}};
}

int main()
{
    auto valid = boss_manifest();
    const auto parsed = parse_runtime_boss_replacements(valid.dump());
    assert(parsed && parsed->size() == 1);
    assert(parsed->at(L"/Game/Blueprints/Enemy/source.source_C") == L"/Game/Blueprints/Enemy/target.target_C");
    assert(!parse_runtime_boss_replacements("{"));
    assert(!parse_runtime_boss_replacements("{}"));
    assert(!parse_runtime_boss_replacements(R"({"nested":{"boss_runtime_remap":{}}})"));

    for (const auto* invalid : {"TEST|0|1", "BOSS_TEST|0|2", "BOSS_TEST|-1|1",
            "BOSS_TEST|0|0", "BOSS_TEST|0|1|2", "BOSS_TEST||1",
            "BOSS_TEST|0|", "BOSS_TEST|99999999999999999999999999999|1"})
    {
        auto manifest = valid;
        manifest["boss_runtime_remap"]["boss_runtime_entries"] = {invalid};
        assert(!parse_runtime_boss_replacements(manifest.dump()));
    }
    for (const auto* field : {"boss_runtime_format_version", "boss_runtime_entry_count"})
    {
        auto manifest = valid;
        manifest["boss_runtime_remap"][field] = 2;
        assert(!parse_runtime_boss_replacements(manifest.dump()));
    }
    auto duplicate = valid;
    duplicate["boss_runtime_remap"]["boss_runtime_entry_count"] = 2;
    duplicate["boss_runtime_remap"]["boss_runtime_entries"] = {"BOSS_TEST|0|1", "BOSS_OTHER|0|1"};
    assert(!parse_runtime_boss_replacements(duplicate.dump()));

    auto empty = valid;
    empty["boss_runtime_remap"]["boss_runtime_entry_count"] = 0;
    empty["boss_runtime_remap"]["boss_runtime_classes"] = json::array();
    empty["boss_runtime_remap"]["boss_runtime_entries"] = json::array();
    const auto empty_result = parse_runtime_boss_replacements(empty.dump());
    assert(empty_result && empty_result->empty());

    // Exclusions remain enforced in both directions, including local experiments.
    for (const auto& pinned : {std::wstring{lich_boss_classes[0]}, std::wstring{final_xehanort_class}})
    {
        for (const auto index : {0, 1})
        {
            auto manifest = valid;
            manifest["boss_runtime_remap"]["boss_runtime_classes"][index] = std::string(pinned.begin(), pinned.end());
            const auto result = parse_runtime_boss_replacements(manifest.dump(), false);
            assert(result && result->empty());
        }
    }
    auto arena = valid;
    arena["boss_runtime_remap"]["boss_runtime_classes"] = {
        "/Game/Blueprints/Enemy/e_he902/e_he902_Pawn.e_he902_Pawn_C",
        "/Game/Blueprints/Enemy/e_ex302/e_ex302_Pawn.e_ex302_Pawn_C"};
    const auto filtered = parse_runtime_boss_replacements(arena.dump());
    const auto experiment = parse_runtime_boss_replacements(arena.dump(), false);
    assert(filtered && filtered->empty());
    assert(experiment && experiment->size() == 1);

    json enemies = {{"enemy_runtime_remap", {
        {"enemy_runtime_format_version", 2}, {"entry_count", 1},
        {"enemy_runtime_packages", {"/Game/Levels/test/test"}},
        {"enemy_runtime_classes", {"/Game/Blueprints/Enemy/target.target_C"}},
        {"enemy_runtime_entries", {"0|PersistentLevel.Enemy_1|0"}}
    }}};
    const auto enemy_result = parse_runtime_replacements(enemies.dump());
    assert(enemy_result && enemy_result->contains(L"/Game/Levels/test/test.PersistentLevel.Enemy_1"));
    enemies["enemy_runtime_remap"]["enemy_runtime_entries"] = {"1|PersistentLevel.Enemy_1|0"};
    assert(!parse_runtime_replacements(enemies.dump()));
    std::cout << "Remap manifest regression tests passed\n";
}
