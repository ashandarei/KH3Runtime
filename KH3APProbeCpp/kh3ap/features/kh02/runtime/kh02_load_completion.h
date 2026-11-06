#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Owns one copied native load delegate and the audited ABI used to inspect it.
    class Kh02LoadCompletion
    {
      public:
        Kh02LoadCompletion() = default;
        ~Kh02LoadCompletion();
        Kh02LoadCompletion(const Kh02LoadCompletion&) = delete;
        auto operator=(const Kh02LoadCompletion&) -> Kh02LoadCompletion& = delete;
        void clear();
        void copy(uintptr_t image, void* source);
        void fail_without_load(int32_t slot);

        auto native_data() -> void*
        {
            return storage.data();
        }

        static auto resolve_owner(uintptr_t image, void* source, RC::Unreal::UObject* manager) -> RC::Unreal::UObject*;
        static auto is_game_over_owner(RC::Unreal::UObject* object) -> bool;
        static auto supported(uintptr_t image) -> bool;

      private:
        static constexpr std::size_t native_delegate_bytes = 0x40;
        uintptr_t base{};
        alignas(16) std::array<std::byte, native_delegate_bytes> storage{};
    };
} // namespace kh3ap
