#include "kh3ap/features/story/world_fixes/graveyard_backtracking.h"
#include <cassert>
#include <algorithm>

using namespace kh3ap;

int main()
{
    assert(graveyard_resume_preset(13500,9999,9999,0,0,L"Init") == L"KG_02_Mission_CD");
    assert(graveyard_resume_preset(13500,9999,9999,9999,0,L"KG_02_Mission_AB") == L"KG_02_Mission_CD");
    assert(graveyard_resume_preset(13000,9999,0,0,0,L"Init").empty());
    assert(graveyard_resume_preset(14000,9999,9999,9999,9999,L"Init").empty());
    assert(graveyard_resume_preset(13500,9999,9999,1100,0,L"Init").empty());
    assert(graveyard_resume_preset(13500,9999,9999,0,1100,L"Init").empty());
    assert(graveyard_resume_preset(13500,9999,9999,9999,9999,L"Init").empty());
    assert(graveyard_resume_preset(13500,9999,9999,0,0,L"KG_02_Area_D").empty());
    assert(graveyard_resume_preset(13500,9999,9999,0,0,L"KG_02_Mission_CD").empty());
    const GraveyardScriptName uber{12, 12, 0};
    const GraveyardScriptName preset{15,15,0};
    std::vector<uint8_t> call(72);
    auto put = [&call](size_t at, const auto& value) { std::memcpy(call.data()+at,&value,sizeof(value)); };
    call[0]=0x1c; put(1,uintptr_t{100});
    call[9]=0x17;call[10]=0x2f;put(11,uintptr_t{200});put(19,uint32_t{32});
    call[23]=0x5b;call[28]=0x1d;call[33]=0x21;put(34,uber);
    call[46]=0x17;call[47]=0x30;call[48]=0x20;put(49,uintptr_t{300});
    call[57]=0x21;put(58,preset);call[70]=0x27;call[71]=0x16;
    assert(graveyard_preset_operand(call,9,100,200,300,uber,preset)==58);
    assert(!graveyard_preset_operand(call,9,100,200,301,uber,preset));
    call[48]=0x01; // reject a different argument form instead of altering a random operand
    assert(!graveyard_preset_operand(call,9,100,200,300,uber,preset));
    const std::array<GraveyardEntryCondition, 2> conditions{{{{1,1,0},{2,2,0}}, {{3,3,0},{4,4,0}}}};
    std::vector<uint8_t> stub(58);
    stub[0] = stub[18] = 0x64;
    stub[36] = 0x1b;
    std::memcpy(stub.data() + 37, uber.data(), 12);
    stub[49] = 0x1d;
    const int32_t entry = 458202;
    std::memcpy(stub.data() + 50, &entry, 4);
    stub[54] = 0x16; stub[55] = 0x04; stub[56] = 0x0b; stub[57] = 0x53;
    const auto guarded = guard_graveyard_entry(stub, uber, 0x12345678, 0xabcdef, 0x456789, conditions);
    assert(guarded.size() == 188);
    assert(std::equal(stub.begin(), stub.end(), guarded.begin() + 130));
    // Execute the generated control-flow prefix for unfinished, one completed,
    // and both completed conditions. The original stub must only run when both
    // completion checks are false, before it can dispatch any gameflow event.
    for (int mask = 0; mask < 4; ++mask)
    {
        size_t pc{}; bool returned{};
        while (pc < 130)
        {
            assert(guarded[pc] == 0x07 && guarded[pc + 5] == 0x19 && guarded[pc + 6] == 0x20);
            uintptr_t library{}; std::memcpy(&library, guarded.data() + pc + 7, 8);
            assert(library == 0xabcdef);
            uint32_t skip{}; std::memcpy(&skip, guarded.data() + pc + 15, 4);
            assert(skip == 36 && guarded[pc + 27] == 0x1c);
            uintptr_t function{}; std::memcpy(&function, guarded.data() + pc + 28, 8);
            assert(function == 0x12345678);
            assert(guarded[pc + 36] == 0x21 && guarded[pc + 49] == 0x21 && guarded[pc + 62] == 0x16);
            if (mask & (1 << (pc / 65)))
            {
                assert(guarded[pc + 63] == 0x04 && guarded[pc + 64] == 0x0b);
                returned = true; break;
            }
            uint32_t next{}; std::memcpy(&next, guarded.data() + pc + 1, 4);
            assert(next == pc + 65); pc = next;
        }
        assert(returned == (mask != 0));
        if (!returned) assert(pc == 130);
    }
    assert(guard_graveyard_entry(guarded, uber, 0x12345678, 0xabcdef, 0x456789, conditions).empty());
    // Wall-run callbacks have no parameters; rising-wall overlaps copy one.
    // Both must retain their complete original forwarding just like the
    // two-parameter mission and arena overlaps above.
    for (const size_t copies : {0u, 1u})
    {
        const auto short_stub = std::span<const uint8_t>(stub).subspan((2 - copies) * 18);
        const auto short_guard = guard_graveyard_entry(short_stub, uber, 0x12345678, 0xabcdef, 0x456789, conditions);
        assert(short_guard.size() == short_stub.size() + 130);
        assert(std::equal(short_stub.begin(), short_stub.end(), short_guard.begin() + 130));
    }
    stub[54] = 0x06; // reject a wrapper containing unexpected branching
    assert(guard_graveyard_entry(stub, uber, 0x12345678, 0xabcdef, 0x456789, conditions).empty());
    assert(guard_graveyard_entry({}, uber, 0x12345678, 0xabcdef, 0x456789, conditions).empty());
}
