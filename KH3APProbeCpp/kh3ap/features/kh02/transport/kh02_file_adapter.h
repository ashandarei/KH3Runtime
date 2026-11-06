#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace kh3ap
{
    // Owns the audited native file dispatch table. Binding is immutable after installation.
    // Restore dispatch before draining asynchronous reads; keep this owner alive until after the drain.
    class Kh02NativeFileAdapter
    {
      public:
        struct Callbacks
        {
            bool (*exists)(void*, const wchar_t*);
            int64_t (*size)(void*, const wchar_t*);
            void* (*timestamp)(void*, int64_t*, const wchar_t*);
            void* (*stat)(void*, void*, const wchar_t*);
            void* (*open)(void*, const wchar_t*, bool);
            void* (*open_async)(void*, const wchar_t*);
            void (*prepare_binding)(uint32_t);
            void (*rollback_binding)();
        };

        void install(uint32_t source_pid, const Callbacks& callbacks);
        void restore_dispatch();

        auto installed() const -> bool
        {
            return original != nullptr;
        }

        auto native_object() const -> void*
        {
            return pak;
        }

        auto forward_exists(void* self, const wchar_t* path) const -> bool;
        auto forward_size(void* self, const wchar_t* path) const -> int64_t;
        auto forward_timestamp(void* self, int64_t* result, const wchar_t* path) const -> void*;
        auto forward_stat(void* self, void* result, const wchar_t* path) const -> void*;
        auto forward_open(void* self, const wchar_t* path, bool allow_write) const -> void*;
        auto forward_async(void* self, const wchar_t* path) const -> void*;

      private:
        static constexpr size_t native_slot_count = 64;

        struct slot
        {
            static constexpr size_t exists = 13;
            static constexpr size_t size = 14;
            static constexpr size_t timestamp = 19;
            static constexpr size_t open = 23;
            static constexpr size_t open_no_buffering = 24;
            static constexpr size_t stat = 29;
            static constexpr size_t open_async = 32;
        };

        std::array<void*, native_slot_count> replacement{};
        void** original{};
        void* pak{};
        uint32_t bound_provider{};
        std::mutex enable_mutex;
    };
} // namespace kh3ap
