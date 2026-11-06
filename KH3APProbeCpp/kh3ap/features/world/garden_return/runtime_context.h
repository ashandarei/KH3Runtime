#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::garden_return_detail
{
    using Clock = std::chrono::steady_clock;

    template <class T> T field(void* object, size_t offset)
    {
        __try
        {
            return *reinterpret_cast<T*>(static_cast<std::byte*>(object) + offset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return {};
        }
    }

    RC::Unreal::UObject* object_field(RC::Unreal::UObject* object, const wchar_t* name);
    RC::Unreal::UObject* library();
    bool loading_closed();
    RC::Unreal::UObject* viewport();
    std::wstring world();
    RC::Unreal::UObject* player();
    int player_id(RC::Unreal::UObject* pawn);
    bool save_open();
} // namespace kh3ap::garden_return_detail
