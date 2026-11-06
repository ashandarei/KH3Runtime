#include "kh3ap/features/grants/delivery/ultima_weapon_shard_policy.h"
#include <cassert>
#include <string>

int main()
{
    using namespace kh3ap::ultima_weapon_shard;
    static_assert(parse(L"KH3AP_ULTIMA_WEAPON_SHARD::7::7").shard.awards_weapon());
    static_assert(!parse(L"KH3AP_ULTIMA_WEAPON_SHARD::8::7").shard.awards_weapon());
    static_assert(parse(L"KH3AP_ULTIMA_WEAPON_SHARD::2147483647::13"));
    static_assert(!parse(L"KH3AP_ULTIMA_WEAPON_SHARD::2147483648::13"));

    for (int required = 0; required <= 15; ++required)
    {
        for (int ordinal = 0; ordinal <= 20; ++ordinal)
        {
            const auto token = std::wstring{token_prefix} + std::to_wstring(ordinal) + L"::" +
                               std::to_wstring(required);
            const auto result = parse(token);
            const bool valid = ordinal > 0 && required >= 2 && required <= 13;
            assert(static_cast<bool>(result) == valid);
            if (valid)
            {
                assert(result.shard.ordinal == ordinal);
                assert(result.shard.required_count == required);
                assert(result.shard.awards_weapon() == (ordinal == required));
            }
            else
            {
                assert(result.failure == ParseFailure::Count);
            }
        }
    }
    assert(parse(L"KH3AP_ULTIMA_WEAPON_SHARD::0007::07").shard.awards_weapon());
    for (const auto suffix : {L"::7", L"1::", L"-1::7", L"+1::7", L"1.0::7", L"1:: 7", L" 1::7",
                              L"1::7 ", L"1::2147483648", L"999999999999999999999999999::7"})
    {
        assert(parse(std::wstring{token_prefix} + suffix).failure == ParseFailure::Count);
    }
    for (const auto suffix : {L"", L"7", L"7:7", L"1::7::8", L"1::::7"})
    {
        assert(parse(std::wstring{token_prefix} + suffix).failure == ParseFailure::Shape);
    }
    assert(parse(L"OTHER_TOKEN::1::7").failure == ParseFailure::Prefix);
    assert(parse(L"").failure == ParseFailure::Prefix);
    assert(!failure_reason(ParseFailure::Prefix).empty());
    assert(!failure_reason(ParseFailure::Shape).empty());
    assert(!failure_reason(ParseFailure::Count).empty());
    assert(failure_reason(ParseFailure::None).empty());
}
