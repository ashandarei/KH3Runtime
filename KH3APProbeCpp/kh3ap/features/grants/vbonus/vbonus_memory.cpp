#include "kh3ap/features/grants/vbonus/vbonus_grant_internal.h"
#include "kh3ap/features/grants/keyblades/keyblade_enhancement_profile.h"
#include "kh3ap/core/reflection/reflection_core.h"
#include <UnrealDef.hpp>
#include <Unreal/UObject.hpp>
#include <cstddef>
#include <cstring>
#include <optional>
#include <vector>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_vbonus_memory_transfer_bytes = 1024;
    }

    using namespace RC;
    using namespace RC::Unreal;
    using namespace vbonus_detail;

    namespace vbonus_detail
    {
        auto try_read_process_bytes(uint64_t address, size_t size)
            -> std::optional<std::vector<std::byte>>
        {
            if (!looks_like_process_pointer(address) || size == 0 || size > max_vbonus_memory_transfer_bytes)
            {
                return std::nullopt;
            }
            std::vector<std::byte> bytes(size);
            SIZE_T bytes_read = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                                   bytes.data(), size, &bytes_read) ||
                bytes_read != size)
            {
                return std::nullopt;
            }
            return bytes;
        }

        auto try_write_process_bytes(uint64_t address, const void* bytes, size_t size) -> bool
        {
            if (!looks_like_process_pointer(address) || !bytes || size == 0 || size > max_vbonus_memory_transfer_bytes)
            {
                return false;
            }
            SIZE_T bytes_written = 0;
            if (!WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes,
                                    size, &bytes_written) ||
                bytes_written != size)
            {
                return false;
            }
            const auto verify = try_read_process_bytes(address, size);
            return verify.has_value() && std::memcmp(verify->data(), bytes, size) == 0;
        }

        auto native_signature_matches(uint64_t address,
                                      const NativeKeybladeEnhancementSignature& expected) -> bool
        {
            if (!expected.bytes || expected.size == 0)
            {
                return false;
            }
            const auto bytes = try_read_process_bytes(address, expected.size);
            if (!bytes.has_value() || bytes->size() != expected.size)
            {
                return false;
            }
            for (size_t index = 0; index < expected.size; ++index)
            {
                if (std::to_integer<uint8_t>((*bytes)[index]) != expected.bytes[index])
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace vbonus_detail

} // namespace kh3ap
