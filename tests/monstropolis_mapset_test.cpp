#include "kh3ap/features/story/world_fixes/monstropolis_mapset.h"
#include <cassert>
#include <limits>

using namespace kh3ap;

int main()
{
    // Cover both return destinations in REAL5. REAL4 and later story progress
    // must keep the stock savepoint preset.
    assert(monstropolis_resume_preset(monstropolis_mapset_caller,
            monstropolis_before_mapset, 3160, L"mi_02_Lv_Save_01") == L"MI_02_AREA_A");
    assert(monstropolis_resume_preset(monstropolis_mapset_caller,
            monstropolis_before_mapset, 3160, L"mi_02_Lv_Save_02") == L"MI_02_AREA_C");
    for (const auto story : {0, 3140, 3150, 3151, 3161, 3170, 9999})
    {
        assert(monstropolis_resume_preset(monstropolis_mapset_caller,
                monstropolis_before_mapset, story, L"mi_02_Lv_Save_01").empty());
        assert(monstropolis_resume_preset(monstropolis_mapset_caller,
                monstropolis_before_mapset, story, L"mi_02_Lv_Save_02").empty());
    }
    for (const auto tag : {L"mi_02_Lv_AutoSave_07", L"mi_02_Lv_Rtry_02", L"mi_02_Lv_Start_01", L""})
        assert(monstropolis_resume_preset(monstropolis_mapset_caller,
                monstropolis_before_mapset, 3160, tag).empty());
    assert(monstropolis_resume_preset(L"Function other_map:ExecuteUbergraph_mi_02",
            monstropolis_before_mapset, 3160, L"mi_02_Lv_Save_02").empty());
    assert(monstropolis_resume_preset(monstropolis_mapset_caller,
            L"/Game/Levels/mi/mi_02/mapset/ms_mi_02_after.ms_mi_02_after",
            3160, L"mi_02_Lv_Save_02").empty());

    constexpr uintptr_t function = 0x100000001ULL;
    constexpr uintptr_t structure = 0x100000002ULL;
    constexpr uintptr_t property = 0x100000003ULL;
    constexpr MonstropolisScriptName callback{10, 10, 0}, preset{20, 20, 0}, other{21, 21, 0};
    constexpr size_t offset = 9;
    std::array<uint8_t, offset + 63> script{};
    const auto put = [&](size_t at, auto value) { std::memcpy(script.data() + at, &value, sizeof(value)); };
    script[0] = 0x1c;
    put(1, function);
    auto* args = script.data() + offset;
    args[0] = 0x17; args[1] = 0x2f;
    put(offset + 2, structure); put(offset + 10, uint32_t{32});
    args[14] = 0x5b; put(offset + 15, uint32_t{16215});
    args[19] = 0x1d; put(offset + 20, uint32_t{1468776243});
    args[24] = 0x21; put(offset + 25, callback);
    args[37] = 0x17; args[38] = 0x30; args[39] = 0x01;
    put(offset + 40, property);
    args[48] = 0x21; put(offset + 49, preset);
    args[61] = 0x27; args[62] = 0x16;
    const auto original = script;
    const auto locate = [&](std::span<uint8_t> bytes, size_t at = 9) {
        return monstropolis_preset_operand(bytes, at, function, structure, property, callback, preset);
    };
    assert(locate(script) == offset + 49);
    assert(script == original); // Inspection never mutates the script.
    for (size_t length = 0; length < script.size(); ++length)
        assert(!locate(std::span{script}.first(length)));
    assert(!locate(script, std::numeric_limits<size_t>::max()));
    assert(!locate(script, 0));

    // Reject altered tokens, pointers, callback names and presets. UUID and
    // linkage are deliberately opaque: the stock continuation must survive.
    for (const auto at : {0U, 1U, 9U, 10U, 11U, 19U, 23U, 28U, 33U, 34U,
                          46U, 47U, 48U, 49U, 57U, 58U, 70U, 71U})
    {
        script = original;
        script[at] ^= 0xff;
        assert(!locate(script));
    }
    script = original;
    args[14] = 0x1d; // Either compiled linkage token has the same width.
    put(offset + 15, uint32_t{11974});
    put(offset + 20, uint32_t{0xabcdef01});
    assert(locate(script) == offset + 49);
    assert(!monstropolis_preset_operand(script, offset, function, structure, property, callback, other));
    assert(!monstropolis_preset_operand(script, offset, function + 1, structure, property, callback, preset));
    assert(!monstropolis_preset_operand(script, offset, function, structure + 1, property, callback, preset));
    assert(!monstropolis_preset_operand(script, offset, function, structure, property + 1, callback, preset));
}
