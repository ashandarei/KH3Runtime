#include "kh02_mesh_buffer_lease.h"
#include "kh3ap/features/kh02/core/kh02_profile.h"
#include "kh02_reflected_call.h"
#include <Unreal/UObject.hpp>
#include <Windows.h>
#include <cstring>
#include <cwchar>
#include <stdexcept>

namespace kh3ap
{
    using namespace RC::Unreal;

    namespace
    {
        constexpr size_t parameter_limit_bytes = 2'048;
        constexpr int preserve_buffers = 0;
        constexpr int release_buffers = 1;

        int mesh_buffer_setting(UObject* pawn, std::optional<int> set = {})
        {
            auto* image = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
            using kh02_native::Site;
            // The registration guard binds the cvar's game-thread data pointer.
            // Writes continue to use its owning engine console command.
            if (!kh02_native::verify_sites({Site::mesh_registration, Site::mesh_cvar_name}) ||
                std::wcscmp(reinterpret_cast<wchar_t*>(image + kh02_native::rva(Site::mesh_cvar_name)),
                            L"r.FreeSkeletalMeshBuffers"))
            {
                throw std::runtime_error("kh02_mesh_cpu_buffer_profile");
            }
            auto* library = kh02_require_loaded_object(L"/Script/Engine.Default__KismetSystemLibrary");
            if (set)
            {
                Kh02ReflectedCall write(library, L"ExecuteConsoleCommand", "kh02_story", parameter_limit_bytes);
                write.object(L"WorldContextObject", pawn);
                write.string(L"Command", L"r.FreeSkeletalMeshBuffers " + std::to_wstring(*set));
                write.run();
            }
            void* data{};
            int value = -1;
            SIZE_T read{};
            if (!ReadProcessMemory(GetCurrentProcess(), image + kh02_native::rva(Site::mesh_cvar_data), &data,
                                   sizeof(data), &read) ||
                read != sizeof(data) || !ReadProcessMemory(GetCurrentProcess(), data, &value, sizeof(value), &read) ||
                read != sizeof(value) || (value != preserve_buffers && value != release_buffers) ||
                (set && value != *set))
            {
                throw std::runtime_error("kh02_mesh_cpu_buffer_cvar");
            }
            return value;
        }

    } // namespace

    void Kh02MeshBufferLease::retain(UObject* context)
    {
        const int current = mesh_buffer_setting(context);
        if (!previous_value)
        {
            previous_value = current;
        }
        if (current != preserve_buffers)
        {
            mesh_buffer_setting(context, preserve_buffers);
        }
    }

    void Kh02MeshBufferLease::restore(UObject* context)
    {
        if (previous_value)
        {
            mesh_buffer_setting(context, *previous_value);
            previous_value.reset();
        }
    }

    auto Kh02MeshBufferLease::previous() const -> std::optional<int>
    {
        return previous_value;
    }

    auto Kh02MeshBufferLease::current(UObject* context) -> int
    {
        return mesh_buffer_setting(context);
    }
} // namespace kh3ap
