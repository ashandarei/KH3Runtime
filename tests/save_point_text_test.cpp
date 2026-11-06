#include "kh3ap/features/story/save_points/text.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/bridge/transport/pipe_transport.h"
#include <cassert>
#include <sstream>

// The production utility object also contains IPC file helpers. Text-only
// serialization must never reach those entry points in this standalone test.
namespace kh3ap::pipe
{
    auto manages(const std::filesystem::path&) -> bool
    {
        assert(false && "text test must not access bridge transport");
        return false;
    }
    auto read(const std::filesystem::path&) -> std::optional<std::string>
    {
        assert(false && "text test must not access bridge transport");
        return std::nullopt;
    }
    auto write(const std::filesystem::path&, std::string_view) -> bool
    {
        assert(false && "text test must not access bridge transport");
        return false;
    }
} // namespace kh3ap::pipe

int main()
{
    using namespace kh3ap::save_point_text;
    assert(parent_path_text(L"/Game/Levels/he/he_01/he_01") == L"/Game/Levels/he/he_01");
    assert(parent_path_text(L"plain") == L"plain");
    assert(parent_path_text(L"/") == L"/");
    assert(runtime_context_matches_map(L"level=/Game/Levels/he/he_01/he_01",
                                       L"/Game/Levels/he/he_01/he_01"));
    assert(runtime_context_matches_map(L"level=/Game/Levels/he/he_01/sublevel",
                                       L"/Game/Levels/he/he_01/he_01"));
    assert(!runtime_context_matches_map(L"level=/Game/Levels/he/he_010/map",
                                        L"/Game/Levels/he/he_01/he_01"));
    assert(!runtime_context_matches_map(L"level", L""));
    assert(auto_spawn_context_key(L"/Game/Levels/he/he_01/he_01") ==
           auto_spawn_context_key(L"/Game/Levels/he/he_01/sublevel"));
    assert(sanitize_name_token(L"a b!?_", 32) == L"a_b");
    assert(sanitize_name_token(L"abcdef", 3) == L"abc");
    assert(sanitize_name_token(L"abcdef", 0).empty());
    assert(sanitize_name_token(L"???", 32).empty());
    const auto paths = object_lookup_candidates(L"Blueprint'/Game/Test.Test_C'");
    assert(paths.size() == 2 && paths[0] == L"Blueprint'/Game/Test.Test_C'" &&
           paths[1] == L"/Game/Test.Test_C");
    assert(object_lookup_candidates(L"/Game/Test.Test_C").size() == 1);
    assert(object_lookup_candidates(L"").empty());
    assert(string_field_or("{\"primary\":\"first\",\"fallback\":\"second\"}", "primary", "fallback",
                           L"default") == L"first");
    assert(string_field_or("{\"fallback\":\"second\"}", "primary", "fallback", L"default") ==
           L"second");
    assert(string_field_or("{}", "primary", "fallback", L"default") == L"default");
    assert(bool_text(true) == L"true" && bool_text(false) == L"false");
    std::ostringstream json;
    json << "{\"base\":0";
    append_json_string_field(json, "label", L"line\n\"quoted\"");
    append_json_number_field(json, "coordinate", -1.25);
    append_json_optional_bool_field(json, "off", false);
    append_json_optional_bool_field(json, "missing", std::nullopt);
    json << '}';
    const auto text = json.str();
    assert(text.find("\"coordinate\":-1.250") != std::string::npos);
    assert(kh3ap::JsonFieldReader::string_field(text, "label") == L"line\n\"quoted\"");
    assert(kh3ap::JsonFieldReader::bool_field(text, "off") == false);
    assert(!kh3ap::JsonFieldReader::bool_field(text, "missing"));
}
