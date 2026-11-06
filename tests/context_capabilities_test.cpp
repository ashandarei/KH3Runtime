#include "kh3ap/bridge/serialization/payload_serializer.h"
#include <cassert>
#include <string>

namespace kh3ap
{
    // Serialization dependencies; this fixture uses empty textual context fields.
    auto json_escape(std::wstring_view text) -> std::string { return {text.begin(), text.end()}; }
    auto now_iso8601_utc() -> std::string { return "2026-11-07T00:00:00Z"; }
}

int main()
{
    const auto text = kh3ap::RuntimePayloadSerializer::serialize_context({}, 123);
    assert(text.find("\"process_id\": 123") != std::string::npos);
    assert(text.find("\"recovery_protocol\": 1") != std::string::npos);
    assert(text.find("\"kh02_gameflow_protocol\": 1") != std::string::npos);
    const auto unbound = kh3ap::RuntimePayloadSerializer::serialize_context({}, 0);
    assert(unbound.find("kh02_gameflow_protocol") == std::string::npos);
}
