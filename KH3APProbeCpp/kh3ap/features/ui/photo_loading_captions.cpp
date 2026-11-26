#include "photo_loading_captions.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh3ap/hooks/patches/checked_code_patch.h"
#include <polyhook2/Misc.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <UnrealDef.hpp>
#include <array>
#include <atomic>

namespace kh3ap
{
    namespace
    {
        // The photo renderer copies the returned FString immediately. It only borrows
        // these static characters and owns the copy it passes through the usual UI formatter.
        struct Caption
        {
            const wchar_t* text;
            int32_t length;
            int32_t capacity;

            template <size_t N>
            constexpr Caption(const wchar_t (&value)[N]) : text(value), length(N), capacity(N) {}
        };

        constexpr std::array captions{
            Caption{L"You can toggle boss or enemy randomization, and many more options in the in-game settings menu!<br><color(hashtag)>\uFFE0randomizer \uFFE0settingsmenu<color>"},
            Caption{L"You can complete San Fran without wall run<br><color(hashtag)>\uFFE0sanfransokyo \uFFE0nowallrun<color>"},
            Caption{L"Try jumping and hitting a flower if stuck in the sewers in Twilight Town<br><color(hashtag)>\uFFE0twilighttown \uFFE0sewertip<color>"},
            Caption{L"100 Acre minigames reward 99 Panacea's that you can sell for Munny<br><color(hashtag)>\uFFE0100acrewood \uFFE0munny<color>"},
            Caption{L"Both Links and Magic abilities unlock the MP bar, but you still require each respective unlock to use them<br><color(hashtag)>\uFFE0links \uFFE0magic<color>"},
            Caption{L"L1 + R1 + Select + Start takes you back to the title screen in case of a softlock<br><color(hashtag)>\uFFE0softlock \uFFE0titlescreen<color>"},
            Caption{L"Hold L3 to toggle the overlay map when in a world with a minimap<br><color(hashtag)>\uFFE0overlaymap \uFFE0minimap<color>"},
            Caption{L"Unlocked an Ally? Set your R2 binding in the AP Options setting<br><color(hashtag)>\uFFE0allies \uFFE0apoptions<color>"},
            Caption{L"Unlock playable Kairi by finishing Remind, Riku via Data battles, Aqua by finishing 0.2, and Roxas by finishing Keyblade graveyard and obtaining Oblivion and Oathkeeper<br><color(hashtag)>\uFFE0playablecharacters \uFFE0unlocks<color>"},
            Caption{L"Wtih stylesanity on, each keyblade's form change is an ability unlock<br><color(hashtag)>\uFFE0stylesanity \uFFE0formchange<color>"},
            Caption{L"Melody of Memory chests give +1 check<br><color(hashtag)>\uFFE0melodyofmemory \uFFE0chests<color>"},
            Caption{L"You can alter the XP and battle level from the in-game settings menu!<br><color(hashtag)>\uFFE0battlelevel \uFFE0settingsmenu<color>"},
            Caption{L"The doom counter on the goal portal signifies how many worlds are needed to be cleared to open the door<br><color(hashtag)>\uFFE0goalportal \uFFE0doomcounter<color>"},
        };
        static_assert(sizeof(Caption) == 16);
        std::atomic_uint32_t next_caption{};

        const Caption* choose_caption() noexcept
        {
            const auto index = next_caption.fetch_add(1, std::memory_order_relaxed) % captions.size();
            return &captions[index];
        }

        // EGS photo open: resolve Definition.CommentTextID, followed by a native
        // FString copy. The character-name lookup and the ordinary spinner are separate.
        constexpr uintptr_t caption_lookup_rva = 0x1ade493;
        constexpr std::array<uint8_t, 9> lookup{0x49, 0x8d, 0x4e, 0x40, 0xe8, 0x24, 0x97, 0xdf, 0xff};
        constexpr std::array<uint8_t, 7> comment_field{0x48, 0x8b, 0x8f, 0x48, 0x02, 0x00, 0x00};
        std::array<uint8_t, 9> redirect{0xff, 0x15, 0, 0, 0, 0, 0x90, 0x90, 0x90};
        uint8_t* installed_site{};
        uintptr_t callback_slot{};
    }

    void install_photo_loading_captions()
    {
        if (installed_site) return;
        auto* image = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
        if (!native_build::egs.matches(pe->FileHeader.TimeDateStamp, pe->OptionalHeader.SizeOfImage)) return;
        auto* site = image + caption_lookup_rva;
        if (std::memcmp(site, lookup.data(), lookup.size()) ||
            std::memcmp(image + 0x1ade4e6, comment_field.data(), comment_field.size())) return;

        // RIP-relative indirect CALL. The nearby page contains a pointer, never executable code.
        auto address = reinterpret_cast<uintptr_t>(site);
        auto low = PLH::calc_2gb_below(address) & ~uintptr_t{0xffff};
        auto high = PLH::calc_2gb_above(address) & ~uintptr_t{0xffff};
        if (!PLH::boundedAllocSupported()) return;
        callback_slot = PLH::boundAlloc(low, high, sizeof(void*));
        if (!callback_slot) return;
        const auto callback = &choose_caption;
        std::memcpy(reinterpret_cast<void*>(callback_slot), &callback, sizeof(callback));
        const auto displacement = static_cast<int32_t>(callback_slot - (address + 6));
        std::memcpy(redirect.data() + 2, &displacement, sizeof(displacement));
        if (!replace_checked_code_bytes(site, lookup, redirect))
        {
            PLH::boundAllocFree(callback_slot, sizeof(void*));
            callback_slot = 0;
            return;
        }
        installed_site = site;
        RC::Output::send<RC::LogLevel::Verbose>(STR("[KH3APProbeCpp] Photo loading captions installed count={}\n"), captions.size());
    }

    void uninstall_photo_loading_captions()
    {
        if (!installed_site || !replace_checked_code_bytes(installed_site, redirect, lookup)) return;
        installed_site = nullptr;
        PLH::boundAllocFree(callback_slot, sizeof(void*));
        callback_slot = 0;
    }
}
