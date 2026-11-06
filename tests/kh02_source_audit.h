#pragma once
#include "kh3ap/features/kh02/transport/kh02_package_format.h"
#include <kh02/asset_compatibility.h>
#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include <map>
#include <set>

namespace kh02test
{
    inline auto sha256(std::span<const uint8_t> bytes) -> std::string
    {
        std::array<uint8_t, 32> hash{};
        kh02asset::package::require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                                               const_cast<PUCHAR>(bytes.data()),
                                               static_cast<ULONG>(bytes.size()), hash.data(),
                                               static_cast<ULONG>(hash.size())) >= 0,
                                    "source audit hash");
        constexpr char digits[] = "0123456789abcdef";
        std::string text;
        for (const auto byte : hash)
        {
            text += digits[byte >> 4];
            text += digits[byte & 15];
        }
        return text;
    }

    inline auto audit(kh3ap::Kh02RemotePipeTransport& transport) -> nlohmann::json
    {
        using namespace kh02asset;
        using namespace package;
        const auto rules = nlohmann::json::parse(compatibility_rules);
        std::set<std::string> selected;
        for (const auto group :
             {"guards", "mirror", "navigation_levels", "navigation_data", "environments",
              "cutscene_lighting", "effects_lighting", "navigation"})
        {
            for (const auto& [name, value] : rules.at(group).items())
            {
                selected.insert(name);
            }
        }
        const auto& index = asset_index();
        std::map<std::string, uint32_t> ids;
        for (uint32_t i = 0; i < index.files.size(); ++i)
        {
            ids.emplace(ascii_lower(source_ascii(index.files[i].source)), i);
        }
        const auto read = [&](uint32_t id, std::size_t maximum)
        {
            const auto size = index.files.at(id).size;
            require(size <= maximum, "source audit buffer limit");
            Bytes bytes(static_cast<std::size_t>(size));
            for (std::size_t offset = 0; offset < bytes.size();)
            {
                const auto count = static_cast<uint32_t>(
                    std::min<std::size_t>(1024 * 1024, bytes.size() - offset));
                require(transport.read_original(id, offset, count, bytes.data() + offset),
                        "source audit read");
                offset += count;
            }
            return bytes;
        };
        nlohmann::json failures = nlohmann::json::array();
        std::size_t checked{};
        for (const auto& name : selected)
        {
            const auto base = ascii_lower(std::string(source_content_prefix) + name);
            auto header_id = ids.find(base + ".uasset");
            if (header_id == ids.end())
            {
                header_id = ids.find(base + ".umap");
            }
            require(header_id != ids.end(), "source audit package missing");
            const auto payload = ids.find(base + ".uexp");
            const auto header = read(header_id->second, maximum_header_bytes);
            const auto exports =
                payload == ids.end() ? Bytes{} : read(payload->second, maximum_export_bytes);
            ++checked;
            try
            {
                adapt_package(name, header, exports);
            }
            catch (const std::exception& error)
            {
                nlohmann::json failure{{"package", name},
                                       {"error", error.what()},
                                       {"header_sha256", sha256(header)},
                                       {"exports_sha256", sha256(exports)}};
                if (rules.at("cutscene_lighting").contains(name))
                {
                    const auto& rule = rules.at("cutscene_lighting").at(name);
                    const auto at = rule.at("export_offset").get<std::size_t>();
                    const auto count = rule.at("export_size").get<std::size_t>();
                    range(exports, at, count);
                    failure["lighting_track_sha256"] =
                        sha256(std::span(exports).subspan(at, count));
                    failure["lighting_track_verified"] =
                        failure["lighting_track_sha256"] == rule.at("export_sha256");
                }
                failures.push_back(std::move(failure));
            }
        }
        return {{"status", failures.empty() ? "passed" : "failed"},
                {"checked", checked},
                {"source_build", index.source_build},
                {"failures", std::move(failures)}};
    }
} // namespace kh02test
