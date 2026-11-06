#include "kh3ap/features/player/characters/character_swap.h"
#include "kh3ap/core/platform/game_build.h"
#include "kh02_shared_unlocks.h"
#include "kh3ap/features/kh02/runtime/kh02_world_context.h"
#include "kh3ap/features/kh02/story/kh02_story.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#define NOMINMAX
#include <Windows.h>
#include <cstring>

namespace kh3ap::kh02rewards
{
    namespace
    {
        constexpr size_t active_save_data_offset = 0x870;
        constexpr size_t shared_capabilities_offset = 0x95b0;

        auto supported_layout() -> bool
        {
            static const bool supported = []
            {
                const auto* base = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
                const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
                const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
                return native_build::identify(nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage) !=
                       native_build::Build::Unsupported;
            }();
            return supported;
        }
    } // namespace

    auto read_shared_unlock_flags(RC::Unreal::UObject* hero) -> std::optional<uint16_t>
    {
        if (!supported_layout() || !hero || (Kh02Story::current_hero() != hero && character_swap::current_hero() != hero))
        {
            return std::nullopt;
        }
        auto* world = character_swap::current_hero() == hero ? kh02_current_world() : kh02_current_story_world();
        if (!world || kh02_actor_world(hero) != world)
        {
            return std::nullopt;
        }
        auto* instance = try_read_reflected_object_property_value(world, L"OwningGameInstance");
        if (!is_uobject_valid_fast(instance) || instance->GetClassPrivate()->GetName() != L"TresGameInstance")
        {
            return std::nullopt;
        }
        uint16_t flags{};
        std::memcpy(&flags,
                    reinterpret_cast<const std::byte*>(instance) + active_save_data_offset + shared_capabilities_offset,
                    sizeof(flags));
        return flags;
    }
} // namespace kh3ap::kh02rewards
