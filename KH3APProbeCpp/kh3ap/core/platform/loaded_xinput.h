#pragma once

#include <Windows.h>
#include <Xinput.h>

namespace kh3ap
{
    inline auto loaded_xinput_get_state() -> decltype(&XInputGetState)
    {
        // Use the same provider as KH3, including Steam Input and Proton.
        const HMODULE module = GetModuleHandleW(L"XINPUT1_3.dll");
        return module ? reinterpret_cast<decltype(&XInputGetState)>(GetProcAddress(module, "XInputGetState"))
                      : nullptr;
    }
}
