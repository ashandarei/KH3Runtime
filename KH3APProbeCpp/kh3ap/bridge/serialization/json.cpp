#include <memory>
#include <optional>
#include <utility>
#include "kh3ap/bridge/serialization/json.h"
#include <atomic>
#include <stdexcept>

namespace kh3ap::json
{
    namespace
    {
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

        std::atomic<SeedDocumentSource> runtime_seed_source{};
    } // namespace

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
            throw std::runtime_error("seed_location_records_required");
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
        if (!is_seed_document(path))
        {
            throw std::runtime_error("unknown_seed_document");
        }
        const auto source = runtime_seed_source.load();
        if (!source)
        {
            throw std::logic_error("runtime_seed_source_missing");
        }
        auto document = source(path);
        if (!document)
        {
            throw std::runtime_error("installed_seed_document_unavailable:" + path.filename().string());
        }
        return document;
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
