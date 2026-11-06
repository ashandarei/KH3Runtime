#include "kh3ap/features/tutorials/toy_box_cactuar_bypass.h"
#include <cassert>
#include <vector>
#include <cstring>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

// Opcode/operand anchors transcribed from the EGS asset disassembly. Pointer
// operands deliberately vary, exercising relocation from the loaded function.
static auto fixture(bool collect) -> std::vector<uint8_t>
{
    std::vector<uint8_t> b(collect ? 0x10CEF : 0xABEA, 0xCC);
    const auto put = [&](size_t p, std::initializer_list<uint8_t> v) {
        std::copy(v.begin(), v.end(), b.begin() + p);
    };
    if (!collect) {
        put(0xA7C2, {6,0x40,0xA7,0,0});
        put(0xA7DF, {6,0xAA,0xA7,0,0});
        put(0xA7E4, {6,0xC7,0xA7,0,0});
        put(0x8784, {0x14,0}); b[0x87A4]=0x1C;
        b[0x87AE]=0x21; b[0x87BC]=0x4D;
    } else {
        put(0x1075E, {6,0x5F,6,1,0});
        put(0x10763, {6,0xB4,6,1,0});
        put(0x10768, {6,9,7,1,0});
        for (size_t p : {0x10694,0x106E9,0x1073E}) {
            b[p]=0x0F; b[p+9]=1; b[p+0x12]=0x21;
            b[p+0x13]=uint8_t(p); // Different completion FNames.
        }
        for (size_t p : {0x109E5,0x10A7C,0x10B13,0x10BAA,0x10C41,0x8758})
            for (size_t j=0;j<3;++j) {
                put(p+j*32,{0x19,1}); b[p+j*32+22]=0x1C;
                b[p+j*32+31]=0x16; b[p+j*32+2]=uint8_t(p+j);
            }
        put(0xE990,{0x19,0x20}); b[0xE9A6]=0x1C;
        put(0xE9B0,{0x1E,0,0,0,0x3F,0x2F});
        put(0xE9C2,{0x5B,0xBB,0x8C,0,0});
        b[0xE9C7]=0x1D; b[0xE9CC]=0x21; b[0xE9DC]=0x4D;
        put(0xDEF3,{0x14,0}); b[0xDF13]=0x1C; b[0xDF1D]=1;
        put(0x8CBB,{0x14,0}); b[0x8DB8]=0x5F;
        b[0x8FD4]=0x19; b[0x9027]=0x19;
        put(0x105A2,{0x5F,0}); b[0x105CD]=0x5F; b[0x10600]=0x0F;
        b[0x10641]=0x4F; put(0x1064B,{0x5F,1}); b[0x1065E]=0x4D;
        put(0xE8D6,{0x14,0}); b[0xE8F6]=0x1C; b[0xE8FF]=0x16;
        for (size_t p : {0xE925,0xE835,0xE6B0,0xE6E8,0xE720}) {
            put(p,{0x14,0}); b[p+0x20]=0x1C; b[p+0x2A]=0x21;
        }
        put(0xE900,{0x19,1}); b[0xE923]=0x27;
        put(0x8C7F,{0x19,1}); put(0xE31A,{0x19,1});
        put(0x108DA,{0x19,0x20}); put(0x10901,{0x19,1}); put(0x1076D,{0x19,1});
        put(0xD29B,{0x5F,0}); b[0xD2F6]=0x0F;
        for (size_t p : {0x1068C,0x106E1,0x10736}) b[p-1]=1;
        for (size_t p : {0x6C7A,0x661E,0x696B,0x1084B,0x10948,0x107B4}) {
            put(p,{0x19,1}); b[p+0x16]=0x1B; b[p+0x23]=0x16;
        }
        b[0xE61D]=6; b[0xE586]=6; b[0xE4AF]=0x19;
        b[0xE3C7]=0x0F; b[0xE539]=0x14; b[0xE5B7]=0x14;
        for (size_t p : {0xE89D,0xE7AD,0xE873,0xE628,0xE4E6}) b[p]=0x14;
        for (size_t p : {0xE33F,0xE512,0xE590}) b[p]=0x19;
        for (size_t p : {0xE58B,0xE622,0xE763}) b[p]=6;
    }
    return b;
}

static auto target(const std::vector<uint8_t>& b, size_t p) -> size_t
{
    uint32_t result{}; std::memcpy(&result,b.data()+p+1,4); return result;
}
static void test_reactor_consumption()
{
    using R = kh3ap::RuntimeTutorialPopupBypassResult;
    const std::array<size_t, 3> stops{0x1B96, 0x3E76, 0x4141};
    const std::array<uint32_t, 3> entries{0x1AA3, 0x4773, 0x471F};
    for (size_t part = 0; part < 3; ++part)
    {
        std::vector<uint8_t> owner(part == 0 ? 0x2418 : 0x502B, 0xCD);
        auto* stop = owner.data() + stops[part];
        stop[0]=0x19; stop[1]=1; stop[0x16]=0x1C; stop[0x1F]=0x27; stop[0x20]=0x16;
        stop[2]=uint8_t(part + 1); // each block's distinct resolved property
        std::vector<uint8_t> script(0x4C, 0xCC);
        for (size_t at : {0u, 0x12u, 0x24u}) { script[at]=0x64; script[at+9]=0; }
        script[0x36]=0x1B; script[0x43]=0x1D;
        std::memcpy(script.data()+0x44, &entries[part], 4);
        script[0x48]=0x16; script[0x49]=4; script[0x4A]=0x0B; script[0x4B]=0x53;
        const auto original = script;
        const auto patch = [&](auto& value) { return kh3ap::try_patch_cactuar_reactor_delegate(
                value.data(), value.size(), owner.data(), owner.size(), part); };
        assert(patch(script)==R::Patched);
        assert(std::equal(stop, stop+0x21, script.begin()));
        assert(std::all_of(script.begin()+0x21, script.begin()+0x36, [](auto b){return b==0x0B;}));
        assert(std::equal(original.begin()+0x36, original.end(), script.begin()+0x36));
        assert(patch(script)==R::AlreadyPatched);
        for (size_t offset : {0u,0x12u,0x24u,0x36u,0x43u,0x44u,0x49u,0x4Bu}) {
            auto invalid=original; invalid[offset]^=0x80; const auto before=invalid;
            assert(patch(invalid)==R::Rejected && invalid==before);
        }
        auto partial=script; partial[0x21]=0xCC; const auto before=partial;
        assert(patch(partial)==R::Rejected && partial==before);
        script=original; stop[0x1F]=0x28;
        assert(patch(script)==R::Rejected && script==original);
        assert(kh3ap::try_patch_cactuar_reactor_delegate(nullptr,0,owner.data(),owner.size(),part)==R::Rejected);
    }
}

int main()
{
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    test_reactor_consumption();
    using R=kh3ap::RuntimeTutorialPopupBypassResult;
    const auto patch=[](auto& b,bool assemble) {
        return kh3ap::try_patch_runtime_toy_box_cactuar_bytecode(b.data(),b.size(),assemble);
    };
    for (bool assemble : {false,true}) {
        auto original=fixture(assemble), b=original;
        assert(patch(b,assemble)==R::Patched);
        const auto patched=b;
        assert(patch(b,assemble)==R::AlreadyPatched && b==patched);
        b=original; b.pop_back(); const auto truncated=b;
        assert(patch(b,assemble)==R::Rejected && b==truncated);
        assert(kh3ap::try_patch_runtime_toy_box_cactuar_bytecode(nullptr,original.size(),assemble)==R::Rejected);
        const std::vector<size_t> entries=assemble
            ? std::vector<size_t>{0x1075E,0x10763,0x10768,0x108DA,0x10901,0x1076D,
                0xE61D,0xE586,0xE4AF,0xE3C7,0xE539,0xE5B7,0xE89D,0xE7AD,0xE873,0xE628}
            : std::vector<size_t>{0xA7C2,0xA7DF,0xA7E4};
        const std::vector<size_t> noops=assemble
            ? std::vector<size_t>{0xE33F,0xE512,0xE590,0xE4E6,0xE58B,0xE622,0xE763}
            : std::vector<size_t>{};
        for(size_t p=0;p<original.size();++p) {
            const bool writable=std::any_of(entries.begin(),entries.end(),[&](size_t e){return p>=e&&p<e+5;}) ||
                std::find(noops.begin(),noops.end(),p)!=noops.end() || (assemble&&p>=0xD29B&&p<0xD29B+0x76D);
            if(!writable) assert(patched[p]==original[p]);
        }
        for(size_t e:entries) {
            b=original; b[e]^=0x80; const auto bad=b;
            assert(patch(b,assemble)==R::Rejected&&b==bad);
            b=original; std::copy_n(patched.begin()+e,5,b.begin()+e);
            const auto mixed=b; assert(patch(b,assemble)==R::Rejected&&b==mixed);
        }
        if(!assemble) {
            for(size_t e:entries) assert(target(patched,e)==0x8784);
            continue;
        }
        // Every actual interaction reacquires the current Gigas, waits one
        // tick, and ends Electro state before starting visual-only animation.
        for(size_t i=0;i<6;++i) {
            size_t p=target(patched,entries[i]);
            assert(patched[p]==0x4C&&target(patched,p)==p+10);
            assert(patched[p+5]==6&&target(patched,p+5)==0x105A2);
            assert(target(patched,p+10+0x33-1)==p+0x57);
            for(size_t j=0;j<4;++j) assert(patched[p+10+0x21+j]==0);
            assert(std::equal(original.begin()+0xE8D6,original.begin()+0xE900,patched.begin()+p+0x57));
            if(i<3) {
                assert(patched[p+0x81]==0x19&&patched[p+0x81+0x16]==0x1B);
                assert(patched[p+0xA5]==0x4D);
            } else {
                assert(patched[p+0x81+0x23]==0x28); // event actor visible
                assert(patched[p+0xA6]==0x19&&patched[p+0xA6+0x16]==0x1B);
                assert(patched[p+0xCA]==0x4D);
            }
        }
        const std::array<size_t,3> plays{0x6C7A,0x661E,0x696B};
        const std::array<size_t,3> names{0x10694,0x106E9,0x1073E};
        for(size_t i=0;i<3;++i) {
            size_t p=target(patched,entries[6+i]);
            assert(std::equal(original.begin()+plays[i],original.begin()+plays[i]+0x24,patched.begin()+p));
            assert(patched[p+0x24]==0x4D);
            p=target(patched,entries[9+i]);
            assert(std::equal(original.begin()+names[i],original.begin()+names[i]+0x1F,patched.begin()+p));
            assert(std::equal(original.begin()+0xDEF3,original.begin()+0xDF27,patched.begin()+p+0x1F));
            assert(patched[p+0x53]==0x4D);
        }
        for(size_t p:noops) assert(patched[p]==0x4D);
        for(size_t anchor:{0x105A2,0x10641,0x1064B,0xE8D6,0xE8F6,0xE923,0xE6B0,0x6C90}) {
            b=original; b[anchor]^=0x80; const auto bad=b;
            assert(patch(b,true)==R::Rejected&&b==bad);
        }
        b=patched; b[0xD29B+150]^=1; const auto corrupt=b;
        assert(patch(b,true)==R::Rejected&&b==corrupt);
    }
}
