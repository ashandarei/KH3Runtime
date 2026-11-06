#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kh02asset
{
    using Bytes = std::vector<uint8_t>;
    struct AdaptedPackage
    {
        Bytes header;
        Bytes exports;
    };

    // All inputs are original bytes read from the owned source process. The
    // result has identical file sizes and never leaves session memory.
    auto adapt_package(std::string_view package, std::span<const uint8_t> header,
                       std::span<const uint8_t> exports) -> AdaptedPackage;

    class PackageAdapter
    {
    public:
        struct Stats
        {
            uint64_t hits{}, preparations{}, evictions{}, entries{}, bytes{};
        };
        using RawRead = std::function<bool(uint32_t, uint64_t, uint32_t, void*)>;
        PackageAdapter();
        ~PackageAdapter();
        auto read(uint32_t file, uint64_t offset, uint32_t length, void* output, const RawRead& raw)
            -> bool;
        auto last_error() const -> std::string;
        void clear();
        auto snapshot() const -> Stats;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh02asset
