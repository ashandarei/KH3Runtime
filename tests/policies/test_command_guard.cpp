#include "kh3ap/bridge/session/command_guard.h"

#include <cassert>

int main()
{
    using kh3ap::runtime_command_request_failure;
    assert(runtime_command_request_failure(R"({"map_name":"/Game/Test"})", 42, 1000) ==
           L"unsupported_request_schema");
    assert(runtime_command_request_failure(R"({"request_schema":"kh3ap.runtime_control.v1","process_id":42,"expires_unix_ms":2000})", 42, 1000).empty());
    assert(
        runtime_command_request_failure(
            R"({"request_schema":"kh3ap.runtime_control.v1","process_id":41,"expires_unix_ms":2000})",
            42, 1000) == L"process_id_mismatch");
    assert(
        runtime_command_request_failure(
            R"({"request_schema":"kh3ap.runtime_control.v1","process_id":42,"expires_unix_ms":1000})",
            42, 1000) == L"invalid_or_expired_request");
    assert(
        runtime_command_request_failure(
            R"({"request_schema":"kh3ap.runtime_control.v1","process_id":42,"expires_unix_ms":601001})",
            42, 1000) == L"invalid_or_expired_request");
    assert(runtime_command_request_failure(
               R"({"request_schema":"kh3ap.runtime_control.v1","process_id":42})", 42, 1000) ==
           L"invalid_or_expired_request");
    assert(runtime_command_request_failure(R"({"request_schema":"kh3ap.runtime_control.v1","process_id":"42","expires_unix_ms":2000})", 42, 1000) == L"process_id_mismatch");
    assert(runtime_command_request_failure(R"({"request_schema":"unknown","process_id":42,"expires_unix_ms":2000})", 42, 1000) == L"unsupported_request_schema");

    // Cached expressions must re-read changed values, stay type-specific, and
    // remain correct after the bounded per-thread cache evicts old entries.
    using kh3ap::JsonFieldReader;
    assert(JsonFieldReader::string_field(R"({"value":"first"})", "value") == L"first");
    assert(JsonFieldReader::string_field(R"({"value":"second"})", "value") == L"second");
    assert(JsonFieldReader::bool_field(R"({"value":true})", "value") == true);
    assert(JsonFieldReader::bool_field(R"({"value":false})", "value") == false);
    assert(JsonFieldReader::number_field(R"({"value":-12.5})", "value") == -12.5);
    assert(JsonFieldReader::number_field(R"({"value":42})", "value") == 42.0);
    assert(!JsonFieldReader::string_field(R"({"other":"first"})", "value"));
    for (int index = 0; index < 256; ++index)
    {
        const std::string key = "field_" + std::to_string(index);
        const std::string json = "{\"" + key + "\":\"observed\"}";
        assert(JsonFieldReader::string_field(json, key) == L"observed");
    }
    assert(JsonFieldReader::string_field(R"({"value":"after eviction"})", "value") == L"after eviction");
    assert(runtime_command_request_failure(R"({"process_id":42,"expires_unix_ms":2000})", 42,
                                           1000) == L"unsupported_request_schema");
    assert(runtime_command_request_failure(
               R"({"request_schema":null,"process_id":42,"expires_unix_ms":2000})", 42, 1000) ==
           L"unsupported_request_schema");
    assert(
        runtime_command_request_failure(
            R"({"request_schema":"kh3ap.runtime_control.v1","process_id":42,"expires_unix_ms":9223372036854775807})",
            42, 9223372036854775806LL)
            .empty());
}
