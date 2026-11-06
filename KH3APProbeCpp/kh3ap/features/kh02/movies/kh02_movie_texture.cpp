#include "kh02_movie_texture.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh3ap/features/kh02/core/kh02_reflected_call.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include "kh3ap/core/objects/uobject_validity.h"
#include "shared/kh02/kh02_movie_shared.h"
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/Property/FBoolProperty.hpp>
#include <cstring>
#include <stdexcept>

namespace kh3ap
{
    namespace
    {
        using namespace RC::Unreal;

        auto image() -> unsigned char*
        {
            auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
            using kh02_native::Site;
            if (!kh02_native::verify_sites({Site::texture_task_create, Site::texture_task_copy,
                                            Site::texture_task_owner, Site::texture_task_dispatch,
                                            Site::texture_update_resource, Site::texture_thread_guards,
                                            Site::texture_render_thread_guard}))
            {
                throw std::runtime_error("movie_texture_native_signature");
            }
            if (GetCurrentThreadId() != *reinterpret_cast<DWORD*>(base + kh02_native::rva(Site::texture_game_thread)) ||
                !base[kh02_native::rva(Site::texture_threaded_rendering)])
            {
                throw std::runtime_error("movie_texture_thread_profile");
            }
            return base;
        }
    } // namespace

    auto create_kh02_movie_texture(RC::Unreal::UObject* owner) -> RC::Unreal::UObject*
    {
        using namespace RC::Unreal;
        auto* base = image();
        auto* type = static_cast<UClass*>(kh02_require_loaded_object(L"/Script/Engine.Texture2DDynamic"));
        if (!is_uobject_valid_fast(owner))
        {
            throw std::runtime_error("movie_texture_owner");
        }
        FStaticConstructObjectParameters parameters(type, owner);
        parameters.SetFlags = RF_Transient;
        auto* texture = UObjectGlobals::StaticConstructObject(parameters);
        auto* format = is_uobject_valid_fast(texture) ? texture->GetPropertyByNameInChain(L"Format") : nullptr;
        if (!format || format->GetOffset_Internal() != 0x100)
        {
            throw std::runtime_error("movie_texture_dynamic_layout");
        }
        auto* update = (*reinterpret_cast<void***>(texture))[0x238 / sizeof(void*)];
        if (update != base + kh02_native::rva(kh02_native::Site::texture_update_resource))
        {
            throw std::runtime_error("movie_texture_update_resource_profile");
        }
        auto* bytes = reinterpret_cast<unsigned char*>(texture);
        const int dimensions[]{1920, 1080};
        std::memcpy(bytes + 0xf8, dimensions, sizeof(dimensions));
        bytes[0x100] = 2; // PF_B8G8R8A8, matching the native image-download path.
        const int mips = 1;
        std::memcpy(bytes + 0x104, &mips, sizeof(mips));
        bytes[0x108] &= ~1;
        for (const auto* name : {L"SRGB", L"bNoTiling", L"NeverStream"})
        {
            auto* property = texture->GetPropertyByNameInChain(name);
            if (!property || !property->IsA<FBoolProperty>())
            {
                throw std::runtime_error("movie_texture_boolean_layout");
            }
            static_cast<FBoolProperty*>(property)->SetPropertyValueInContainer(texture, true);
        }
        texture->SetRootSet();
        reinterpret_cast<void (*)(UObject*)>(update)(texture);
        return texture;
    }

    void upload_kh02_movie_texture(RC::Unreal::UObject* texture, std::span<const uint8_t> pixels)
    {
        using namespace RC::Unreal;
        auto* base = image();
        if (!is_uobject_valid_fast(texture) ||
            texture->GetClassPrivate()->GetPathName() != L"/Script/Engine.Texture2DDynamic" ||
            pixels.size() != kh02movie::frame_bytes)
        {
            throw std::runtime_error("movie_texture_upload_input");
        }
        auto* resource = *reinterpret_cast<void**>(reinterpret_cast<unsigned char*>(texture) + 0x90);
        if (!resource)
        {
            throw std::runtime_error("movie_texture_resource_missing");
        }
        const auto render_thread =
            *reinterpret_cast<int*>(base + kh02_native::rva(kh02_native::Site::texture_render_thread));
        if (render_thread < 0 || render_thread > 7)
        {
            throw std::runtime_error("movie_texture_render_thread_profile");
        }

        struct Constructor
        {
            unsigned char* task;
            void* prerequisites;
            int current_thread;
        } constructor{};

        struct Bytes
        {
            const uint8_t* data;
            int count, capacity;
        } source{pixels.data(), static_cast<int>(pixels.size()), static_cast<int>(pixels.size())};

        // Use the engine's own image-upload task, allocator, data-copy constructor
        // and dispatch routine. Its render-thread body locks/unlocks the native RHI
        // texture, and its destructor releases the owned pixel copy. No RHI or D3D
        // call runs on the game thread and no UObject crosses a custom worker.
        // Steam retains the prerequisites/thread arguments that EGS specializes
        // away. Supply the complete original constructor ABI on both builds.
        reinterpret_cast<void* (*)(Constructor*, void*, int)>(
            base + kh02_native::rva(kh02_native::Site::texture_task_create))(&constructor, nullptr, 0xff);
        if (!constructor.task || constructor.prerequisites || constructor.current_thread != 0xff ||
            *reinterpret_cast<void**>(constructor.task + 0x30))
        {
            throw std::runtime_error("movie_texture_render_task_profile");
        }
        reinterpret_cast<void* (*)(void*, void*, const Bytes*)>(
            base + kh02_native::rva(kh02_native::Site::texture_task_copy))(constructor.task + 0x10, resource, &source);
        constructor.task[0x28] = 1;
        std::memcpy(constructor.task + 8, &render_thread, sizeof(render_thread));
        const auto dispatch = base + kh02_native::rva(kh02_native::Site::texture_task_dispatch);
        if (kh02_native::build() == kh02_native::Build::Steam)
        {
            // The owning Steam callback supplies prerequisites and task mode 2.
            reinterpret_cast<void (*)(void*, void*, int, bool, int)>(dispatch)(
                constructor.task, constructor.prerequisites, constructor.current_thread, true, 2);
        }
        else
        {
            reinterpret_cast<void (*)(void*, int, int, bool)>(dispatch)(constructor.task, constructor.current_thread, 0,
                                                                        true);
        }
    }
} // namespace kh3ap
