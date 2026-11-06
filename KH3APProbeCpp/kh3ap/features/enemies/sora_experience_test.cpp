#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/features/enemies/sora_experience_test.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstring>
#include <stdexcept>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_test_sora_level = 99;
        constexpr auto max_test_total_experience = 9999999;
        constexpr auto max_test_experience_grant = 50000;

        template<class T> bool read(uintptr_t address, T& result) noexcept
        {
            __try { std::memcpy(&result, reinterpret_cast<void*>(address), sizeof(T)); return true; }
            __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
        template<class T> T required(uintptr_t address)
        {
            T value{};
            if (!read(address,value)) throw std::runtime_error("experience_unreadable_layout");
            return value;
        }
        struct ExperienceProfile { uintptr_t base, accessor, wrapper, lane; };
        ExperienceProfile validate(RC::Unreal::UObject* pawn, bool aqua = false)
        {
            if (!is_uobject_valid_fast(pawn) || !is_uobject_valid_fast(pawn->GetClassPrivate()) ||
                pawn->GetClassPrivate()->GetName() != (aqua ? L"p_ex002_Pawn_C" : L"p_ex001_Pawn_C") ||
                (aqua && !Kh02Story::aqua_experience(pawn)))
                throw std::runtime_error("experience_requires_sora");
            const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto dos=required<IMAGE_DOS_HEADER>(base);
            if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<=0 || dos.e_lfanew>=0x1000)
                throw std::runtime_error("experience_unsupported_build");
            const auto nt=required<IMAGE_NT_HEADERS64>(base+dos.e_lfanew);
            if(nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
                nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                throw std::runtime_error("experience_unsupported_build");
            const bool steam = kh3ap::native_build::steam.matches(nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage);
            const bool egs = kh3ap::native_build::egs.matches(nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage);
            if (!steam && !egs) throw std::runtime_error("experience_unsupported_build");
            const ExperienceProfile profile{base, steam ? 0x56D000u : 0x6F8A60u,
                    steam ? 0x5D2860u : 0x7648B0u, steam ? 0x5CF120u : 0x765110u};
            // Complete EGS pawn-interface wrapper, including its call to the existing
            // EXP multiplier's guarded award entry at RVA 0x1135CC0. No hook is added.
            static constexpr unsigned char award[] = {
                0x85,0xd2,0x74,0x74,0x48,0x89,0x6c,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x89,
                0x5c,0x24,0x30,0x49,0x8b,0xe8,0x48,0x8d,0x99,0xb8,0xfb,0xff,0xff,0x48,0x89,0x74,
                0x24,0x38,0x48,0x8b,0x03,0x48,0x8b,0xcb,0x8b,0xfa,0xff,0x90,0xc0,0x08,0x00,0x00,
                0x48,0x8b,0xf0,0x48,0x85,0xc0,0x74,0x2c,0x48,0x8b,0x0b,0x48,0x8d,0x05,0x1e,0x08,
                0x00,0x00,0x48,0x8b,0x91,0x90,0x0d,0x00,0x00,0x48,0x3b,0xd0,0x75,0x2b,0x8b,0x83,
                0x14,0x05,0x00,0x00,0x4c,0x8b,0xcd,0x44,0x8b,0xc0,0x8b,0xd7,0x48,0x8b,0xce,0xe8,
                0xac,0x13,0x9d,0x00,0x48,0x8b,0x5c,0x24,0x30,0x48,0x8b,0x74,0x24,0x38,0x48,0x8b,
                0x6c,0x24,0x40,0x48,0x83,0xc4,0x20,0x5f,0xc3,0x48,0x8b,0xcb,0xff,0xd2,0xeb,0xd4};
            static constexpr unsigned char steam_award[] = {
                0x85,0xd2,0x74,0x65,0x48,0x89,0x6c,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x89,
                0x5c,0x24,0x30,0x49,0x8b,0xe8,0x48,0x8d,0x99,0xb8,0xfb,0xff,0xff,0x48,0x89,0x74,
                0x24,0x38,0x48,0x8b,0x03,0x48,0x8b,0xcb,0x8b,0xfa,0xff,0x90,0xc0,0x08,0x00,0x00,
                0x48,0x8b,0xf0,0x48,0x85,0xc0,0x74,0x1d,0x4c,0x8b,0x03,0x48,0x8b,0xcb,0x41,0xff,
                0x90,0x90,0x0d,0x00,0x00,0x4c,0x8b,0xcd,0x8b,0xd7,0x44,0x8b,0xc0,0x48,0x8b,0xce,
                0xe8,0x7b,0xbf,0x8c,0x00,0x48,0x8b,0x5c,0x24,0x30,0x48,0x8b,0x74,0x24,0x38,0x48,
                0x8b,0x6c,0x24,0x40,0x48,0x83,0xc4,0x20,0x5f,0xc3};
            const auto* signature = steam ? steam_award : award;
            const size_t signature_size = steam ? sizeof(steam_award) : sizeof(award);
            for(size_t i=0;i<signature_size;++i)
                if(required<unsigned char>(base+profile.wrapper+i)!=signature[i])
                    throw std::runtime_error("experience_award_signature");
            const auto address=reinterpret_cast<uintptr_t>(pawn);
            const auto vt=required<uintptr_t>(address);
            if(required<uintptr_t>(vt+0x8c0)!=base+profile.accessor ||
                required<uintptr_t>(vt+0xd90)!=base+profile.lane || required<int32_t>(address+0x514)!=0)
                throw std::runtime_error("experience_sora_vtable_or_lane");
            static constexpr unsigned char accessor[]={0x48,0x83,0xec,0x28,0x48,0x8b,0x01,0xff,0x90,0x40,0x01,0x00,0x00,0x48,0x85,0xc0};
            for(size_t i=0;i<sizeof(accessor);++i)
                if(required<unsigned char>(base+profile.accessor+i)!=accessor[i])
                    throw std::runtime_error("experience_accessor_signature");
            return profile;
        }
    }
    auto inspect_sora_experience(RC::Unreal::UObject* pawn) -> SoraExperienceSnapshot
    {
        const auto profile=validate(pawn);
        using Accessor=void*(__fastcall*)(RC::Unreal::UObject*);
        const auto owner=reinterpret_cast<uintptr_t>(reinterpret_cast<Accessor>(profile.base+profile.accessor)(pawn));
        const auto stats=required<uintptr_t>(reinterpret_cast<uintptr_t>(pawn)+0x4d0);
        if(!owner || !stats) throw std::runtime_error("experience_missing_owner");
        // Same character-level field observed by the existing level-write monitor.
        const SoraExperienceSnapshot result{required<int32_t>(stats+0x40),required<int32_t>(owner+0x8670)};
        if(result.level<1 || result.level>max_test_sora_level || result.total_exp<0 || result.total_exp>max_test_total_experience)
            throw std::runtime_error("experience_snapshot_out_of_range");
        return result;
    }
    auto award_kh02_aqua_experience(RC::Unreal::UObject* pawn, int32_t amount) -> void
    {
        if (amount < 1 || amount > max_test_experience_grant)
            throw std::runtime_error("experience_amount_out_of_range");
        const auto profile = validate(pawn, true);
        using Award = void(__fastcall*)(void*, int32_t, void*);
        reinterpret_cast<Award>(profile.base + profile.wrapper)(reinterpret_cast<std::byte*>(pawn) + 0x448, amount, nullptr);
    }

    auto award_sora_experience(RC::Unreal::UObject* pawn, int32_t amount) -> void
    {
        if(amount<1 || amount>max_test_experience_grant) throw std::runtime_error("experience_amount_out_of_range");
        const auto profile=validate(pawn);
        (void)inspect_sora_experience(pawn);
        using Award=void(__fastcall*)(void*,int32_t,void*);
        // Wrapper expects the pawn's +0x448 interface, then resolves its current
        // save owner and progression lane. Null is the optional award-position
        // presentation argument; the normal EXP/level/reward processing still runs.
        reinterpret_cast<Award>(profile.base+profile.wrapper)(reinterpret_cast<std::byte*>(pawn)+0x448,amount,nullptr);
    }
}
