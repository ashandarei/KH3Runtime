#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/catalog/slot_data_lookup_parser.h"
#include "kh3ap/bridge/rewards/vbonus_reward_bundle.h"
#include "kh3ap/features/player/context/context_resolver.h"
#include "kh3ap/bridge/serialization/request_parser.h"
#include "kh3ap/bridge/serialization/json.h"
#include "kh3ap/bridge/catalog/location_catalog_data.h"
#include "kh3ap/bridge/checks/check_checkpoint.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <limits>

namespace kh3ap
{
    // Only snapshot construction needs a clock; parsing uses production code.
    auto now_iso8601_utc() -> std::string { return "2026-10-08T00:00:00Z"; }
}

int main()
{
    using namespace kh3ap;
    const JsonObject object{R"({"nested":{"name":"wrong","flag":false},"name":"Sora \"hi\" \\ \n \u00e9 \u65e5 \ud83d\ude00","flag":true,"integer":9223372036854775807,"minimum":-9223372036854775808,"exact":9007199254740993,"decimal":1.25e2,"strings":["A\\B","\u65e5"],"rows":[{"name":"quoted } ] \"","child":{"id":7}}]})"};
    assert(object.valid());
    assert(object.string_field("name") == L"Sora \"hi\" \\ \n \u00e9 \u65e5 \U0001f600");
    assert(object.bool_field("flag") == true);
    assert(object.int64_field("integer") == std::numeric_limits<int64_t>::max());
    assert(object.int64_field("minimum") == std::numeric_limits<int64_t>::min());
    assert(object.int64_field("exact") == 9007199254740993LL);
    assert(!object.int64_field("decimal"));
    assert(object.number_field("decimal") == 125);
    assert(object.string_array_field("strings") == (std::vector<std::wstring>{L"A\\B", L"\u65e5"}));
    assert(object.object_array_field("rows").size() == 1);
    assert(!object.int64_field("id"));
    assert(object.object_field("nested").string_field("name") == L"wrong");
    const auto retained = JsonObject{R"({"rows":[{"child":{"id":7}}]})"}
        .objects_field("rows").front().object_field("child");
    assert(retained.int64_field("id") == 7); // Owns the root after every parent/view is gone.
    assert(JsonObject{"{\"name\":\"\xc3\xa9 \xe6\x97\xa5\"}"}.string_field("name") == L"\u00e9 \u65e5");
    for (const auto text : {"", "[]", "null", R"({"name":"valid"} trailing)",
            R"({"name":"valid","broken":[1,]})", R"({"i":01})", R"({"flag":True})",
            R"({"i":1e})", R"({"i":NaN})", R"({"name":"\q"})", "{\"s\":\"a\x01\"}"})
        assert(!JsonObject{text}.valid());
    const JsonObject wrong{R"({"i":9223372036854775808,"s":12,"b":1,"strings":["ok",3],"rows":[{},false],"n":"1","nested":{"only":"hidden"}})"};
    assert(!wrong.int64_field("i") && !wrong.string_field("s") && !wrong.bool_field("b"));
    assert(!wrong.number_field("n") && !wrong.string_field("only"));
    assert(wrong.string_array_field("strings").empty() && wrong.object_array_field("rows").empty());
    assert(wrong.objects_field("rows").empty()); // Reject the whole mixed array.
    assert(!object.object_field("rows").valid() && object.objects_field("nested").empty());
    const std::string high_surrogate = "{\"s\":\"\\u" "d800\"}";
    const std::string low_surrogate = "{\"s\":\"\\u" "dc00\"}";
    assert(!JsonObject{high_surrogate}.string_field("s"));
    assert(!JsonObject{low_surrogate}.string_field("s"));
    std::string deep = "{\"x\":" + std::string(129, '[') + "0" + std::string(129, ']') + "}";
    assert(!JsonObject{deep}.valid());
    std::string rewritten = R"({"name":"first"})";
    assert(JsonFieldReader::string_field(rewritten, "name") == L"first");
    rewritten = R"({"name":"second"})";
    assert(JsonFieldReader::string_field(rewritten, "name") == L"second");
    rewritten += "broken";
    assert(!JsonFieldReader::string_field(rewritten, "name"));

    const std::string slot = R"({"kh3_seed":{"seed_name":"seed","slot":1},"bridge":{"locations":[
        {"type":"event","vanilla_item":"one","bridge_key":"event1"},
        {"bridge_key":"event2","vanilla_item":"one","type":"event"},
        {"type":"event","vanilla_item":"two","bridge_key":"event3"},
        {"type":"shop","source_id":"potion","bridge_key":"Shop:1","shop_item_token":"ITEM_001","shop_price":500},
        {"type":"vbonus","source_id":"v1","vanilla_items":{"Sora_Ability1":"ETresAbilityKind::DODGE","Sora_Bonus1":"ETresVictoryBonusKind::HP"}},
        {"vanilla_items":{"Sora_Ability2":"ETresAbilityKind::DODGE"},"type":"vbonus","source_id":"v2"},
        {"type":"vbonus","source_id":"v3","vanilla_items":{"Sora_Ability1":"ETresAbilityKind::NONE"}}
    ]},"setting":1,"flag":false,"nested":{"setting":9,"only":1}})";
    const auto event = SlotDataLookupParser::unique_event_bridge_key_lookup(slot);
    assert(event.ambiguous_count == 1 && event.lookup.size() == 1 && event.lookup.at(L"two") == L"event3");
    assert(SlotDataLookupParser::typed_source_bridge_key_lookup(slot, "shop").at(L"potion") == L"Shop:1");
    const auto shop = SlotDataLookupParser::shop_item_locations(slot);
    assert(shop.size() == 1 && shop[0].shop_price == 500);
    const auto bonus = SlotDataLookupParser::unique_vbonus_runtime_reward_lookup(slot);
    assert(bonus.size() == 1 && bonus.at(L"GIVESORA_HP") == L"v1");
    assert(SlotDataLookupParser::int_option(slot, "setting") == 1);
    assert(SlotDataLookupParser::bool_option(slot, "setting") == true);
    assert(SlotDataLookupParser::bool_option(slot, "flag") == false);
    assert(!SlotDataLookupParser::int_option(slot, "only"));
    assert(!SlotDataLookupParser::bool_option(R"({"x":10})", "x"));
    assert(!SlotDataLookupParser::int_option(R"({"x":2147483648})", "x"));
    assert(!SlotDataLookupParser::int_option(R"({"x":1.5})", "x"));
    assert(SlotDataLookupParser::unique_event_bridge_key_lookup(slot + "!").lookup.empty());
    const auto seed_dir = std::filesystem::temp_directory_path() /
        ("kh3-json-views-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(seed_dir);
    const auto seed_path = seed_dir / "slot_data.json";
    assert(!JsonObject::read_seed(seed_path).valid());
    std::ofstream(seed_path) << slot;
    const auto shared = JsonObject::read_seed(seed_path);
    assert(SlotDataLookupParser::unique_event_bridge_key_lookup(shared).lookup == event.lookup);
    assert(SlotDataLookupParser::unique_event_bridge_key_lookup(shared).ambiguous_count == event.ambiguous_count);
    assert(SlotDataLookupParser::unique_vbonus_runtime_reward_lookup(shared) == bonus);
    assert(SlotDataLookupParser::shop_item_locations(shared)[0].shop_price == 500);
    const auto old_identity = shared.object_field("kh3_seed");
    const auto modified = std::filesystem::last_write_time(seed_path);
    auto replacement = slot;
    replacement.replace(replacement.find("\"slot\":1"), 8, "\"slot\":2");
    std::ofstream(seed_path) << replacement;
    std::filesystem::last_write_time(seed_path, modified + std::chrono::seconds(1));
    assert(JsonObject::read_seed(seed_path).object_field("kh3_seed").int64_field("slot") == 2);
    assert(old_identity.int64_field("slot") == 1);
    std::ofstream(seed_path) << "broken";
    assert(!JsonObject::read_seed(seed_path).valid());
    std::filesystem::remove(seed_path);
    assert(!JsonObject::read_seed(seed_path).valid());
    assert(old_identity.int64_field("slot") == 1);
    std::filesystem::remove(seed_dir);
    const auto snapshot = RuntimeContextResolver::parse_snapshot(R"({"world":"world \"x\" \u65e5","sora_level":9,"nested":{"hook":"ignore"}})");
    assert(snapshot && snapshot->hook.empty() && snapshot->world == L"world \"x\" \u65e5" && snapshot->sora_level == 9);
    assert(!RuntimeContextResolver::parse_snapshot(R"({"nested":{"world":"hidden"}})"));
    assert(!RuntimeContextResolver::parse_snapshot(R"({"world":"x"} garbage)"));
    const auto bundles = VBonusRewardBundleParser::parse_lookup(JsonObject{R"({"runtime_reward_bundles":[
        {"kind":"vbonus","source_id":"v1","location_name":"a \"quoted\" \u65e5","randomized_tokens":[{"name":"x]","nested":[1,2]}],"vanilla_tokens":[],"world":"world"},
        {"kind":"event","source_id":"ignored"}]})"});
    assert(bundles.size() == 1 && bundles.at(L"v1").location_name == L"a \"quoted\" \u65e5");
    assert(!bundles.at(L"v1").randomized_tokens_json.empty());
    assert(VBonusRewardBundleParser::parse_lookup(JsonObject{R"({"runtime_reward_bundles":[false]})"}).empty());
    const auto message = RuntimeRequestParser::parse_ap_message(R"({"message":"Hello \"Sora\" \u65e5","duration_seconds":1e1})");
    assert(message && message->message == L"Hello \"Sora\" \u65e5" && message->duration_seconds == 10);
    assert(!RuntimeRequestParser::parse_ap_message(R"({"nested":{"message":"hidden"}})"));
    const auto batch = RuntimeRequestParser::parse_item_grant_batch(R"({"action_kind":"runtime_item_grant_batch","expand_catalog":true,"grants":[{"index":9007199254740993}]})");
    assert(batch && batch->grants.size() == 1 && batch->grants[0].index == 9007199254740993LL);
    assert(!RuntimeRequestParser::parse_item_grant_batch(R"({"action_kind":"runtime_item_grant_batch","expand_catalog":true,"grants":[{"index":1},false]})"));

    // Native workers and field readers share the same complete decoder.
    json::Value native;
    const std::string_view native_text = R"({"name":"\ud83d\ude00","exact":9007199254740993})";
    assert(json::parse(native, native_text));
    assert(native["exact"].get<int64_t>() == 9007199254740993LL);
    assert(json::utf8_to_wide(native["name"].get_ref<const std::string&>()) == L"\U0001f600");
    const auto encoded = json::write(native);
    assert(JsonObject{encoded}.int64_field("exact") == 9007199254740993LL);
    for (const auto text : {R"({"x":1} trailing)", R"({"s":"\ud800"})", R"({"s":"\udc00"})", R"({"x":1e9999})"})
    {
        native = json::Value{{"old", true}};
        assert(!json::parse(native, text));
        assert(native.is_null());
    }
    assert(!json::parse(native, deep));

    // Check histories retain their identity and never accept a corrupt array
    // as an empty history. Exercise non-ASCII seed names through UTF-8 on disk.
    const auto checkpoint = std::filesystem::temp_directory_path() /
        ("kh3-json-checkpoint-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    const auto put = [&](std::string_view body) {
        std::ofstream out(checkpoint, std::ios::binary | std::ios::trunc);
        out << body;
        assert(out.good());
    };
    put(R"({"schema":"checks","seed_name":"\u65e5\ud83d\ude00","slot":1,"completed":["\u00e9"]})");
    assert(load_check_checkpoint(checkpoint, L"checks", L"\u65e5\U0001f600", 1) == std::set<std::wstring>{L"\u00e9"});
    for (const auto text : {R"({"schema":"checks","seed_name":"seed","slot":1.0,"completed":[]})",
            R"({"schema":"checks","seed_name":"seed","slot":1,"completed":["one",false]})",
            R"({"schema":"checks","seed_name":"other","slot":1,"completed":[]})",
            R"({"schema":"checks","seed_name":"seed","slot":9223372036854775808,"completed":[]})",
            R"({"schema":"checks","seed_name":"seed","slot":1,"completed":[]} broken)"})
    {
        put(text);
        bool rejected{};
        try { load_check_checkpoint(checkpoint, L"checks", L"seed", 1); }
        catch (const std::exception&) { rejected = true; }
        assert(rejected);
    }
    std::filesystem::remove(checkpoint);
    assert(load_check_checkpoint(checkpoint, L"checks", L"seed", 1).empty());

    // Shared compact assignments must not amplify a small input into an
    // unbounded expanded document.
    const auto catalog = json::Value::parse(json::location_catalog_data);
    auto entries = json::Value::array();
    constexpr size_t expansion_test_locations = 64;
    for (const auto& [key, base] : catalog["locations"].items())
    {
        entries.push_back(json::Value::array({base["ap_location_id"], 0,
                                             json::Value::object(), json::Value::array()}));
        if (entries.size() == expansion_test_locations)
        {
            break;
        }
    }
    assert(entries.size() == expansion_test_locations);
    json::Value compact{
        {"bridge", json::Value::object()},
        {"location_catalog", {
            {"version", 1}, {"sha256", catalog["sha256"]}, {"entries", entries},
            {"assignments", json::Value::array({json::Value{{"assigned_item_name", "Potion"}}})}}}};
    const auto expanded = json::expand_slot_data(compact);
    assert(expanded["bridge"]["locations"].size() == expansion_test_locations);
    assert(expanded["bridge"]["locations"][0]["assigned_item_name"] == "Potion");
    compact["location_catalog"]["assignments"][0]["assigned_item_name"] =
        std::string(json::limits::seed_bytes / 16, 'x');
    assert(compact.dump().size() < json::limits::seed_bytes);
    bool expansion_rejected{};
    try
    {
        (void)json::expand_slot_data(std::move(compact));
    }
    catch (const std::runtime_error&)
    {
        expansion_rejected = true;
    }
    assert(expansion_rejected);
}
