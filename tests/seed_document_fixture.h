#pragma once

#include "kh3ap/bridge/serialization/json.h"
#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace kh3ap::seed_document_fixture
{
    // Host tests explicitly bind their fixture files as a seed source.
    inline auto read(const std::filesystem::path& path) -> std::shared_ptr<const json::SeedDocument>
    {
        struct Entry
        {
            std::filesystem::file_time_type modified;
            uintmax_t bytes{};
            uint64_t used{};
            std::shared_ptr<const json::SeedDocument> document;
        };
        constexpr auto max_cached_seed_documents = 4;
        static std::mutex mutex;
        static std::map<std::filesystem::path, Entry> cache;
        static uint64_t serial{};
        std::lock_guard lock{mutex};
        const auto key = std::filesystem::absolute(path).lexically_normal();
        const auto name = path.filename().string();
        const auto fail = [&](const char* reason) -> void
        {
            cache.erase(key);
            throw std::runtime_error(std::string(reason) + ":" + name);
        };
        std::error_code ec;
        const auto modified = std::filesystem::last_write_time(path, ec);
        if (ec)
        {
            fail("seed_metadata_read_failed");
        }
        const auto bytes = std::filesystem::file_size(path, ec);
        if (ec)
        {
            fail("seed_metadata_read_failed");
        }
        const auto limit =
            name == "runtime_seed_hints.json" ? json::limits::hints_bytes : json::limits::seed_bytes;
        if (bytes > limit)
        {
            fail("seed_metadata_size_limit");
        }
        if (const auto prior = cache.find(key); prior != cache.end() &&
                                                prior->second.modified == modified &&
                                                prior->second.bytes == bytes)
        {
            prior->second.used = ++serial;
            return prior->second.document;
        }
        auto document = std::make_shared<json::SeedDocument>();
        std::ifstream file(path, std::ios::binary);
        document->text.resize(static_cast<size_t>(bytes));
        if (!file.read(document->text.data(), static_cast<std::streamsize>(bytes)) ||
            file.peek() != std::char_traits<char>::eof())
        {
            fail("seed_metadata_read_failed");
        }
        if (std::filesystem::last_write_time(path, ec) != modified || ec)
        {
            fail("seed_metadata_changed_during_read");
        }
        std::shared_ptr<const json::SeedDocument> decoded;
        try
        {
            decoded = json::decode_seed_document(name, std::move(document->text));
        }
        catch (...)
        {
            cache.erase(key);
            throw;
        }
        // Only a few documents are live per run; do not retain past runs forever.
        if (cache.size() >= max_cached_seed_documents && !cache.contains(key))
        {
            cache.erase(std::min_element(cache.begin(), cache.end(),
                                         [](const auto& a, const auto& b)
                                         {
                                             return a.second.used < b.second.used;
                                         }));
        }
        cache[key] = Entry{modified, bytes, ++serial, decoded};
        return decoded;
    }
} // namespace kh3ap::seed_document_fixture
