#include <memory>
#include <optional>
#include <utility>
#include "kh3ap/bridge/serialization/json.h"
#include "kh3ap/bridge/catalog/location_catalog_data.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <vector>

namespace kh3ap::json
{
    namespace
    {
        enum CatalogEntryField : size_t
        {
            catalog_location_id,
            catalog_assignment,
            catalog_overrides,
            catalog_removed_fields,
            catalog_entry_field_count,
        };
        constexpr auto first_printable_ascii = 0x20;
        constexpr auto utf8_two_byte_lead_first = 0xc2;
        constexpr auto utf8_two_byte_lead_last = 0xdf;
        constexpr auto utf8_three_byte_lead_first = 0xe0;
        constexpr auto utf8_three_byte_lead_last = 0xef;
        constexpr auto utf8_four_byte_lead_first = 0xf0;
        constexpr auto utf8_four_byte_lead_last = 0xf4;
        constexpr auto utf8_two_byte_lead_payload_mask = 0x1f;
        constexpr auto utf8_three_byte_lead_payload_mask = 0x0f;
        constexpr auto utf8_four_byte_lead_payload_mask = 0x07;
        constexpr auto utf8_two_byte_min_codepoint = 0x80;
        constexpr auto utf8_three_byte_min_codepoint = 0x800;
        constexpr auto utf8_four_byte_min_codepoint = 0x10000;
        constexpr auto utf8_three_byte_continuations = 2;
        constexpr auto utf8_four_byte_continuations = 3;
        constexpr auto utf8_continuation_tag_mask = 0xc0;
        constexpr auto utf8_continuation_tag = 0x80;
        constexpr auto utf8_continuation_payload_bits = 6;
        constexpr auto utf8_continuation_payload_mask = 0x3f;
        constexpr auto unicode_max_codepoint = 0x10ffff;
        constexpr auto utf16_high_surrogate_first = 0xd800;
        constexpr auto utf16_low_surrogate_first = 0xdc00;
        constexpr auto utf16_surrogate_last = 0xdfff;
        constexpr auto utf16_max_single_unit_codepoint = 0xffff;
        constexpr auto utf16_supplementary_plane_start = 0x10000;
        constexpr auto utf16_surrogate_payload_bits = 10;
        constexpr auto utf16_surrogate_payload_mask = 0x3ff;

        constexpr auto max_cached_seed_documents = 4;
        constexpr auto max_seed_catalog_entries = 4096;
        std::atomic<SeedDocumentSource> runtime_seed_source{};
    } // namespace

    auto expand_slot_data(Value compact) -> Value
    {
        if (!compact.is_object() || !compact.contains("location_catalog"))
        {
            return compact;
        }
        static const auto catalog = Value::parse(location_catalog_data);
        const auto fail = []
        {
            throw std::runtime_error("seed_location_catalog_invalid");
        };
        const auto& metadata = compact["location_catalog"];
        if (!metadata.is_object() || !metadata.contains("version") ||
            !metadata["version"].is_number_integer() || metadata["version"] != 1 ||
            !metadata.contains("sha256") || metadata["sha256"] != catalog["sha256"])
        {
            throw std::runtime_error("seed_location_catalog_mismatch");
        }
        if (!compact.contains("bridge") || !compact["bridge"].is_object() ||
            compact["bridge"].contains("locations") || !metadata.contains("entries") ||
            !metadata["entries"].is_array() || !metadata.contains("assignments") ||
            !metadata["assignments"].is_array() ||
            metadata["entries"].size() > max_seed_catalog_entries ||
            metadata["assignments"].size() > max_seed_catalog_entries)
        {
            fail();
        }
        static const std::set<std::string> assignment_fields{
            "assigned_item_id",     "assigned_item_name",
            "assigned_item_player", "assigned_item_player_name",
            "assigned_item_game",   "assigned_item_classification",
            "is_foreign_item"};
        const auto& assignments = metadata["assignments"];
        std::vector<size_t> assignment_bytes;
        assignment_bytes.reserve(assignments.size());
        for (const auto& assignment : assignments)
        {
            if (!assignment.is_object())
            {
                fail();
            }
            for (const auto& [key, _] : assignment.items())
            {
                if (!assignment_fields.contains(key))
                {
                    fail();
                }
            }
            assignment_bytes.push_back(assignment.dump().size());
        }
        Value rows = Value::array();
        size_t expansion_bytes_remaining = limits::seed_bytes;
        std::set<int64_t> seen;
        for (const auto& entry : metadata["entries"])
        {
            if (!entry.is_array() || entry.size() != catalog_entry_field_count ||
                !entry[catalog_location_id].is_number_integer() ||
                !entry[catalog_assignment].is_number_integer() ||
                !entry[catalog_overrides].is_object() || !entry[catalog_removed_fields].is_array())
            {
                fail();
            }
            const auto id = entry[catalog_location_id].get<int64_t>(),
                       assignment = entry[catalog_assignment].get<int64_t>();
            const auto key = std::to_string(id);
            if (!seen.insert(id).second || !catalog["locations"].contains(key) || assignment < 0 ||
                static_cast<uint64_t>(assignment) >= assignments.size())
            {
                fail();
            }
            const auto& base = catalog["locations"][key];
            // Shared compact assignments are copied into each expanded row.
            // Bound that amplification before allocating the row's values.
            const auto row_bytes = base.dump().size() +
                                   assignment_bytes[static_cast<size_t>(assignment)] +
                                   entry[catalog_overrides].dump().size();
            if (row_bytes > expansion_bytes_remaining)
            {
                fail();
            }
            expansion_bytes_remaining -= row_bytes;
            auto row = base;
            for (const auto& removed : entry[catalog_removed_fields])
            {
                if (!removed.is_string())
                {
                    fail();
                }
                const auto& field = removed.get_ref<const std::string&>();
                if (field == "ap_location_id" || !base.contains(field))
                {
                    fail();
                }
                row.erase(field);
            }
            for (const auto& [field, value] : entry[catalog_overrides].items())
            {
                if (field == "ap_location_id" || assignment_fields.contains(field))
                {
                    fail();
                }
                row[field] = value;
            }
            row.update(assignments[static_cast<size_t>(assignment)]);
            rows.push_back(std::move(row));
        }
        compact.erase("location_catalog");
        compact["bridge"]["locations"] = std::move(rows);
        return compact;
    }
    namespace
    {

        // Bound recursion before the pinned decoder validates the syntax.
        auto bounded_document(std::string_view text) -> bool
        {
            if (text.empty() || text.size() > limits::seed_bytes)
            {
                return false;
            }
            size_t depth{};
            bool quoted{}, escaped{};
            for (const unsigned char ch : text)
            {
                if (quoted)
                {
                    if (ch < first_printable_ascii)
                    {
                        return false;
                    }
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (ch == '\\')
                    {
                        escaped = true;
                    }
                    else if (ch == '"')
                    {
                        quoted = false;
                    }
                }
                else if (ch == '"')
                {
                    quoted = true;
                }
                else if (ch == '{' || ch == '[')
                {
                    if (++depth > limits::document_depth)
                    {
                        return false;
                    }
                }
                else if (ch == '}' || ch == ']')
                {
                    if (!depth)
                    {
                        return false;
                    }
                    --depth;
                }
            }
            return !quoted && !depth;
        }

    } // namespace

    auto is_seed_document(const std::filesystem::path& path) -> bool
    {
        const auto name = path.filename();
        return name == "slot_data.json" || name == "runtime_seed_settings.json" ||
               name == "runtime_seed_hints.json";
    }

    auto bind_runtime_seed_source(SeedDocumentSource source) -> void
    {
        if (!source)
        {
            throw std::invalid_argument("runtime_seed_source_missing");
        }
        SeedDocumentSource expected{};
        if (!runtime_seed_source.compare_exchange_strong(expected, source) && expected != source)
        {
            throw std::logic_error("runtime_seed_source_already_bound");
        }
    }

    auto decode_seed_document(std::string_view name, std::string text)
        -> std::shared_ptr<const SeedDocument>
    {
        const auto limit =
            name == "runtime_seed_hints.json" ? limits::hints_bytes : limits::seed_bytes;
        if (text.size() > limit)
        {
            throw std::runtime_error("seed_metadata_size_limit:" + std::string{name});
        }
        auto document = std::make_shared<SeedDocument>();
        document->text = std::move(text);
        if (!parse(document->value, document->text) || !document->value.is_object())
        {
            throw std::runtime_error("seed_metadata_invalid_json:" + std::string{name});
        }
        if (name == "slot_data.json" && document->value.contains("location_catalog"))
        {
            document->value = expand_slot_data(std::move(document->value));
            document->text = write(document->value);
            if (document->text.size() > limit)
            {
                throw std::runtime_error("seed_metadata_size_limit:" + std::string{name});
            }
        }
        if (name != "slot_data.json")
        {
            const auto schema = name == "runtime_seed_hints.json" ? "kh3ap.seed_hints.v1"
                                                                  : "kh3ap.seed_settings.v1";
            if (!document->value.contains("schema") || document->value["schema"] != schema)
            {
                throw std::runtime_error("seed_metadata_schema_mismatch:" + std::string{name});
            }
        }
        return document;
    }

    auto read_seed_document(const std::filesystem::path& path)
        -> std::shared_ptr<const SeedDocument>
    {
        if (const auto source = runtime_seed_source.load())
        {
            if (!is_seed_document(path))
            {
                throw std::runtime_error("unknown_seed_document");
            }
            auto document = source(path.filename().string());
            if (!document)
            {
                throw std::runtime_error("installed_seed_document_unavailable:" +
                                         path.filename().string());
            }
            return document;
        }
        struct Entry
        {
            std::filesystem::file_time_type modified;
            uintmax_t bytes{};
            uint64_t used{};
            std::shared_ptr<const SeedDocument> document;
        };
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
        if (!is_seed_document(path))
        {
            fail("unknown_seed_document");
        }
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
            name == "runtime_seed_hints.json" ? limits::hints_bytes : limits::seed_bytes;
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
        auto document = std::make_shared<SeedDocument>();
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
        std::shared_ptr<const SeedDocument> decoded;
        try
        {
            decoded = decode_seed_document(name, std::move(document->text));
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

    auto utf8_to_wide(std::string_view text) -> std::optional<std::wstring>
    {
        std::wstring result;
        result.reserve(text.size());
        for (size_t pos = 0; pos < text.size();)
        {
            const auto first = static_cast<unsigned char>(text[pos++]);
            uint32_t codepoint = first;
            unsigned continuation{};
            uint32_t minimum{};
            if (first >= utf8_two_byte_lead_first && first <= utf8_two_byte_lead_last)
            {
                continuation = 1;
                codepoint &= utf8_two_byte_lead_payload_mask;
                minimum = utf8_two_byte_min_codepoint;
            }
            else if (first >= utf8_three_byte_lead_first && first <= utf8_three_byte_lead_last)
            {
                continuation = utf8_three_byte_continuations;
                codepoint &= utf8_three_byte_lead_payload_mask;
                minimum = utf8_three_byte_min_codepoint;
            }
            else if (first >= utf8_four_byte_lead_first && first <= utf8_four_byte_lead_last)
            {
                continuation = utf8_four_byte_continuations;
                codepoint &= utf8_four_byte_lead_payload_mask;
                minimum = utf8_four_byte_min_codepoint;
            }
            else if (first >= utf8_two_byte_min_codepoint)
            {
                return std::nullopt;
            }
            if (continuation > text.size() - pos)
            {
                return std::nullopt;
            }
            while (continuation--)
            {
                const auto next = static_cast<unsigned char>(text[pos++]);
                if ((next & utf8_continuation_tag_mask) != utf8_continuation_tag)
                {
                    return std::nullopt;
                }
                codepoint = (codepoint << utf8_continuation_payload_bits) |
                            (next & utf8_continuation_payload_mask);
            }
            if (codepoint < minimum || codepoint > unicode_max_codepoint ||
                (codepoint >= utf16_high_surrogate_first && codepoint <= utf16_surrogate_last))
            {
                return std::nullopt;
            }
            if constexpr (sizeof(wchar_t) == sizeof(uint16_t))
            {
                if (codepoint > utf16_max_single_unit_codepoint)
                {
                    codepoint -= utf16_supplementary_plane_start;
                    result.push_back(static_cast<wchar_t>(
                        utf16_high_surrogate_first + (codepoint >> utf16_surrogate_payload_bits)));
                    result.push_back(static_cast<wchar_t>(
                        utf16_low_surrogate_first + (codepoint & utf16_surrogate_payload_mask)));
                    continue;
                }
            }
            result.push_back(static_cast<wchar_t>(codepoint));
        }
        return result;
    }

    auto parse(Value& output, std::string_view text) -> bool
    {
        output = nullptr;
        if (!bounded_document(text))
        {
            return false;
        }
        auto parsed = Value::parse(text, nullptr, false);
        if (parsed.is_discarded())
        {
            return false;
        }
        output = std::move(parsed);
        return true;
    }

    auto write(const Value& value) -> std::string
    {
        return value.dump();
    }
} // namespace kh3ap::json
