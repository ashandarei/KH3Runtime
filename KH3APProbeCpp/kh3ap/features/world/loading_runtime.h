#pragma once
#include "kh3ap/features/world/startup/autoload_profile.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap::loading_runtime
{
    inline constexpr uintptr_t load_slot_rva = 0x18fd380;
    inline constexpr uintptr_t save_slot_rva = 0x18fcf60;
    inline constexpr uintptr_t make_completion_rva = 0x18cdea0;
    inline constexpr uintptr_t destroy_delegate_rva = 0x59e050;
    inline constexpr uintptr_t free_rva = 0x31aafa0;
    inline constexpr uintptr_t title_vtable_rva = 0x7c381e0;
    inline constexpr uintptr_t catalog_rva = 0x9c460d8;
    inline constexpr uintptr_t catalog_count_rva = 0x9c460e0;
    inline constexpr uintptr_t loading_screen_vtable_rva = 0x7c6ad70;
    inline constexpr uintptr_t close_loading_screen_rva = 0x1addef0;
    inline constexpr uintptr_t loading_operation_rva = 0x9c47a60;
    inline constexpr uintptr_t loading_manager_rva = 0x9c481c0;
    inline constexpr uintptr_t continue_loading_rva = 0x18ec800;
    inline constexpr uintptr_t retire_loading_task_rva = 0x18eee00;
    inline constexpr uintptr_t new_game_confirmed_rva = 0x18c8ce0;

    template <class T> bool read_native(uintptr_t address, T& value) noexcept
    {
        __try
        {
            std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    template <class T> T read_field(const void* object, size_t offset, T fallback = {})
    {
        T value{};
        return object && read_native(reinterpret_cast<uintptr_t>(object) + offset, value) ? value : fallback;
    }

    extern AutoloadStore startup_store;
    uintptr_t native_rva(uintptr_t egs);
    size_t manager_offset(size_t egs);
    bool supported_image(uintptr_t base);
    auto native_load_gate_original_matches(uintptr_t base) -> bool;
    void register_owned_load_detour(uintptr_t base, uintptr_t trampoline, size_t stolen_size, size_t trampoline_size);
    auto native_load_gate_diagnostic() -> std::wstring;
    void clear_owned_load_detour();
    auto native_load_gate_installed() -> bool;

    struct NativeLoadGateFailure
    {
        uint64_t sequence{};
        uint64_t request_generation{};
        std::string reason;
    };

    auto native_load_gate_failure() -> NativeLoadGateFailure;
    void report_native_load_gate_failure(uint64_t request_generation, std::string_view reason);
    void clear_native_load_gate_failure();
    bool matches(uintptr_t base, uintptr_t rva, std::initializer_list<uint8_t> signature);
    bool property_offset(RC::Unreal::UObject* object, const wchar_t* name, int offset);
    RC::Unreal::UObject* owned_save_manager(RC::Unreal::UObject* pawn);

    struct LoadingScreenSnapshot
    {
        // Used only within the current game-thread tick, never retained.
        RC::Unreal::UObject* object{};
        bool known{};
        bool layout_mismatch{};
        bool open{};
        bool closed{};
        int32_t mode{-1};
        float progress{-1.0f};
    };

    struct LoadingTransition
    {
        bool complete{};
        bool prompt_ready{};
        bool dispatched{};
        bool layout_mismatch{};
        bool retire_failed{};
    };

    LoadingScreenSnapshot loading_screen_snapshot(uintptr_t base, RC::Unreal::UObject* manager);
    LoadingTransition loading_transition(uintptr_t base, bool allow_continue);
} // namespace kh3ap::loading_runtime
