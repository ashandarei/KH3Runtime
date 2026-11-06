#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace kh3ap::kh02compat
{
    namespace vm
    {
        inline constexpr size_t reference_bytes = sizeof(uint64_t);
        inline constexpr size_t name_literal_bytes = 3 * sizeof(uint32_t);
        inline constexpr size_t reference_operand_bytes = 1 + reference_bytes;
        inline constexpr size_t variable_with_following_token_bytes = reference_operand_bytes + 1;
        inline constexpr size_t name_operand_bytes = 1 + name_literal_bytes;
        inline constexpr size_t byte_operand_bytes = 1 + sizeof(uint8_t);
        inline constexpr size_t scalar_operand_bytes = 1 + sizeof(int32_t);
        inline constexpr size_t latent_prefix_bytes = 2 + reference_bytes + sizeof(int32_t);
        inline constexpr size_t latent_name_and_tail_bytes = name_operand_bytes + 3;
        // Source schema advertises its serialized layout; UE's native ABI packs
        // linkage, UUID, FName and callback into a different size after loading.
        inline constexpr int32_t serialized_latent_bytes = 32;
        inline constexpr size_t native_latent_bytes = 24;
    } // namespace vm
    enum class Domain
    {
        camera,
        audio,
        movies,
        challenges,
        travel,
        presentation
    };
    enum class Adapter : uint8_t
    {
#define KH02_COMPATIBILITY_ADAPTER(name, id, domain) name = id,
#include "kh02_compatibility_adapters.inc"
#undef KH02_COMPATIBILITY_ADAPTER
    };

    struct AdapterDescriptor
    {
        Adapter adapter;
        Domain domain;
        std::string_view name;
    };

    inline constexpr auto adapters = std::to_array<AdapterDescriptor>({
#define KH02_COMPATIBILITY_ADAPTER(name, id, domain) {Adapter::name, Domain::domain, #name},
#include "kh02_compatibility_adapters.inc"
#undef KH02_COMPATIBILITY_ADAPTER
    });

    constexpr auto valid_adapter(int id) -> bool
    {
        return id >= 0 && static_cast<size_t>(id) < adapters.size() && static_cast<int>(adapters[id].adapter) == id;
    }

    constexpr auto domain_of(Adapter adapter) -> Domain
    {
        if (!valid_adapter(static_cast<int>(adapter)))
        {
            throw std::invalid_argument("kh02_compat_adapter");
        }
        return adapters[static_cast<size_t>(adapter)].domain;
    }

    constexpr auto has_latent(Adapter adapter) -> bool
    {
        return adapter == Adapter::WaitVideoReady || adapter == Adapter::WaitVideoEnd ||
               adapter == Adapter::ApplyMapSet || adapter == Adapter::SkipHelp || adapter == Adapter::SkipNotice;
    }

    namespace opcode
    {
        inline constexpr uint8_t local_variable = 0x00;
        inline constexpr uint8_t instance_variable = 0x01;
        inline constexpr uint8_t return_expression = 0x04;
        inline constexpr uint8_t nothing = 0x0b;
        inline constexpr uint8_t end_parameters = 0x16;
        inline constexpr uint8_t self = 0x17;
        inline constexpr uint8_t virtual_function = 0x1b;
        inline constexpr uint8_t integer_constant = 0x1d;
        inline constexpr uint8_t float_constant = 0x1e;
        inline constexpr uint8_t string_constant = 0x1f;
        inline constexpr uint8_t object_constant = 0x20;
        inline constexpr uint8_t name_constant = 0x21;
        inline constexpr uint8_t byte_constant = 0x24;
        inline constexpr uint8_t true_constant = 0x27;
        inline constexpr uint8_t false_constant = 0x28;
        inline constexpr uint8_t no_object = 0x2a;
        inline constexpr uint8_t struct_constant = 0x2f;
        inline constexpr uint8_t end_struct_constant = 0x30;
        inline constexpr uint8_t end_script = 0x53;
        inline constexpr uint8_t skip_offset = 0x5b;
    } // namespace opcode
} // namespace kh3ap::kh02compat
