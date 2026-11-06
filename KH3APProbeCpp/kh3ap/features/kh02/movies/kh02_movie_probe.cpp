#include "kh02_movie_probe.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_weak.h"
#include "kh3ap/bridge/utilities.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/FProperty.hpp>
#define NOMINMAX
#include <Windows.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kh3ap
{
    using namespace RC::Unreal;
    using Json = nlohmann::json;

    namespace
    {
        constexpr std::size_t reflected_parameter_limit_bytes = 2'048;
        constexpr std::size_t native_array_header_bytes = 16;

        namespace native_movie_layout
        {
            constexpr std::size_t egs_slots_offset = 0x3a8;
            constexpr std::size_t steam_slots_offset = 0x3f8;
            constexpr int maximum_slots = 32;
            constexpr std::size_t video_object_offset = 0x320;
            constexpr std::size_t player_state_offset = 0x348;
            constexpr std::size_t backend_offset = 0x350;
            constexpr std::size_t cri_player_offset = 0x10;
            constexpr std::size_t cri_inner_offset = 0x60;
            constexpr std::size_t decoder_offset = 8;
            constexpr std::size_t prepared_offset = 0x405;
            constexpr std::size_t handler_offset = 0x3e8;
            constexpr std::size_t handler_ticks_offset = 0x3f8;
            constexpr std::size_t decoder_state_offset = 4;
            constexpr std::size_t decoder_error_offset = 0x8a8;
        } // namespace native_movie_layout

        auto path(UObject* object) -> std::string
        {
            return is_uobject_valid_fast(object) ? to_utf8(object->GetPathName()) : "";
        }
    } // namespace

    auto Kh02NativeMovieProbe::state() -> Json
    {
        auto* manager = movie_manager.Get();
        if (!is_uobject_valid_fast(manager))
        {
            manager = nullptr;
            std::vector<UObject*> objects;
            UObjectGlobals::FindAllOf(L"TresUIManager", objects);
            for (auto* candidate : objects)
            {
                if (is_uobject_valid_fast(candidate) && !candidate->HasAnyFlags(RF_ClassDefaultObject))
                {
                    if (manager)
                    {
                        throw std::runtime_error("kh02_movie_manager_ambiguous");
                    }
                    manager = candidate;
                }
            }
            if (!manager)
            {
                return Json::object();
            }
            movie_manager = make_uobject_weak(manager);
        }
        auto* slots_property = manager->GetPropertyByNameInChain(L"m_fullScreenVideoSlots");

        struct NativeSlots
        {
            UObject** data;
            int32_t count;
            int32_t capacity;
        };

        static_assert(sizeof(NativeSlots) == native_array_header_bytes);

        const auto slots_offset = kh02_native::build() == kh02_native::Build::Steam
                                      ? native_movie_layout::steam_slots_offset
                                      : native_movie_layout::egs_slots_offset;
        if (!slots_property || kh02_native::build() == kh02_native::Build::Unsupported ||
            slots_property->GetOffset_Internal() != slots_offset ||
            slots_property->GetElementSize() != native_array_header_bytes)
        {
            throw std::runtime_error("kh02_movie_slots_layout");
        }
        auto* slot_array = slots_property->ContainerPtrToValuePtr<NativeSlots>(manager);
        if (slot_array->count < 0 || slot_array->count > native_movie_layout::maximum_slots ||
            slot_array->capacity < slot_array->count)
        {
            throw std::runtime_error("kh02_movie_slots_bounds");
        }
        if (!slot_array->count || !slot_array->data)
        {
            return Json::object();
        }
        auto* player = slot_array->data[0];
        if (!is_uobject_valid_fast(player))
        {
            return Json::object();
        }
        auto* video_property = player->GetPropertyByNameInChain(L"m_ObjVideo");
        if (!video_property || video_property->GetOffset_Internal() != native_movie_layout::video_object_offset ||
            try_read_reflected_object_property_value(player, L"m_pUIManager") != manager)
        {
            throw std::runtime_error("kh02_movie_player_layout");
        }
        auto read = [](const void* address, void* destination, size_t size)
        {
            SIZE_T done{};
            return ReadProcessMemory(GetCurrentProcess(), address, destination, size, &done) && done == size;
        };
        auto pointer = [&](const void* base, size_t offset)
        {
            void* value{};
            if (base)
            {
                read(static_cast<const std::byte*>(base) + offset, &value, sizeof(value));
            }
            return value;
        };
        auto integer = [&](const void* base, size_t offset)
        {
            int32_t value = -1;
            if (base)
            {
                read(static_cast<const std::byte*>(base) + offset, &value, sizeof(value));
            }
            return value;
        };
        auto* backend = pointer(player, native_movie_layout::backend_offset);
        if (!backend)
        {
            return Json::object();
        }
        auto* cri = pointer(backend, native_movie_layout::cri_player_offset);
        auto* inner = cri ? pointer(cri, native_movie_layout::cri_inner_offset) : nullptr;
        auto* decoder = inner ? pointer(inner, native_movie_layout::decoder_offset) : nullptr;
        uint8_t prepared{};
        read(static_cast<std::byte*>(backend) + native_movie_layout::prepared_offset, &prepared, 1);
        const auto handler = reinterpret_cast<uintptr_t>(pointer(backend, native_movie_layout::handler_offset));
        const auto image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        return {{"player", path(player)},
                {"ui_state", integer(player, native_movie_layout::player_state_offset)},
                {"handler_ticks", integer(backend, native_movie_layout::handler_ticks_offset)},
                {"handler_rva", handler >= image && handler - image < kh02_native::image_size() ? handler - image : 0},
                {"prepared", prepared == 1},
                {"decoder_state", decoder ? integer(decoder, native_movie_layout::decoder_state_offset) : -1},
                {"decoder_error", inner ? integer(inner, native_movie_layout::decoder_error_offset) : -1},
                {"has_texture", is_uobject_valid_fast(try_read_reflected_object_property_value(player, L"m_Texture"))}};
    }

    void Kh02NativeMovieProbe::begin(bool enabled)
    {
        sampling.begin(enabled);
    }

    void Kh02NativeMovieProbe::observe(UObject* world, bool final_sample)
    {
        if (!sampling.active() && !final_sample)
        {
            return;
        }
        if (!sampling.needs_sample(is_uobject_valid_fast(world), final_sample))
        {
            return;
        }
        Kh02ReflectedCall playing(kh02_require_loaded_object(L"/Script/TresGame.Default__TresGameBlueprintLibrary"),
                                  L"TresUI_IsPlayingVideo", "kh02_story", reflected_parameter_limit_bytes);
        playing.run();
        const bool is_playing = playing.boolean_result();
        auto current_state = state();
        const auto sampled_at = GetTickCount64();
        sampling.record(is_playing, std::move(current_state), sampled_at, final_sample);
    }

    auto Kh02NativeMovieProbe::observing() const -> bool
    {
        return sampling.active();
    }

    auto Kh02NativeMovieProbe::samples() const -> uint64_t
    {
        return sampling.samples();
    }

    auto Kh02NativeMovieProbe::events() const -> const Json&
    {
        return sampling.events();
    }

    void Kh02NativeMovieProbe::clear_world()
    {
        sampling.retire();
        movie_manager = {};
    }
} // namespace kh3ap
