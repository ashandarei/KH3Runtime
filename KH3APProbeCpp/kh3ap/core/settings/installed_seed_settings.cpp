#include <memory>
#include <optional>
#include <utility>
#include "kh3ap/core/settings/pak_seed_settings.h"
#include "kh3ap/bridge/serialization/json.h"
#include "kh3ap/bridge/serialization/json_fields.h"
#include "kh3ap/features/enemies/randomization.h"
#include <Windows.h>
#include <DynamicOutput/DynamicOutput.hpp>
#include <array>

namespace kh3ap
{
    namespace
    {
        constexpr auto max_windows_module_path_chars = 32768;
        struct InstalledSeedDocuments
        {
            std::optional<std::string> settings;
            std::shared_ptr<const json::SeedDocument> slot;
            std::shared_ptr<const json::SeedDocument> hints;
            std::shared_ptr<const json::SeedDocument> options;
        };

        auto installed_documents() -> const InstalledSeedDocuments&
        {
            // The selected pak and all its immutable documents have process lifetime.
            static const auto documents = []() -> InstalledSeedDocuments
            {
                try
                {
                    std::array<wchar_t, max_windows_module_path_chars> path{};
                    const auto length =
                        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
                    if (!length || length == path.size())
                    {
                        throw std::runtime_error("Could not resolve KH3 executable path");
                    }
                    const auto root = std::filesystem::path{path.data()}
                                          .parent_path()
                                          .parent_path()
                                          .parent_path();
                    const auto pak = find_seed_pak(root / L"Content/Paks/~mods");
                    auto records = read_pak_seed_records(pak);
                    InstalledSeedDocuments result;
                    result.options = json::decode_seed_document(
                        "runtime_seed_settings.json", records.at("runtime_seed_settings.json"));
                    randomization::validate_settings(result.options->value);
                    result.slot = json::decode_seed_document(
                        "slot_data.json", std::move(records.at("slot_data.json")));
                    const auto& slot = result.slot->value;
                    const auto& identity = slot.at("kh3_seed");
                    if (slot.at("protocol_name") != "kh3_ap_file_bridge" ||
                        slot.at("protocol_version") != 1 ||
                        identity.at("seed_name") != result.options->value.at("seed_name") ||
                        identity.at("slot") != result.options->value.at("slot") ||
                        !slot.at("bridge").at("locations").is_array())
                    {
                        throw std::runtime_error("Installed seed catalog identity mismatch");
                    }
                    result.settings = std::move(records.at("runtime_seed_settings.json"));
                    try
                    {
                        result.hints = json::decode_seed_document(
                            "runtime_seed_hints.json", records.at("runtime_seed_hints.json"));
                        if (result.hints->value.at("seed_name") != identity.at("seed_name") ||
                            result.hints->value.at("slot") != identity.at("slot"))
                        {
                            throw std::runtime_error("Installed hint identity mismatch");
                        }
                    }
                    catch (const std::exception& error)
                    {
                        result.hints.reset();
                        const std::string reason = error.what();
                        RC::Output::send<RC::LogLevel::Warning>(
                            L"[KH3APProbeCpp] Optional pak hint targets unavailable: {}\n",
                            std::wstring(reason.begin(), reason.end()));
                    }
                    RC::Output::send<RC::LogLevel::Normal>(
                        L"[KH3APProbeCpp] Native seed settings/catalog loaded from pak={} "
                        L"locations={} loose_files=false\n",
                        pak.wstring(), slot.at("bridge").at("locations").size());
                    return result;
                }
                catch (const std::exception& error)
                {
                    const std::string reason = error.what();
                    RC::Output::send<RC::LogLevel::Error>(
                        L"[KH3APProbeCpp] Native seed settings unavailable: {}\n",
                        std::wstring(reason.begin(), reason.end()));
                    return {};
                }
            }();
            return documents;
        }

        auto document_from_pak(std::string_view name) -> std::shared_ptr<const json::SeedDocument>
        {
            const auto& documents = installed_documents();
            if (name == "slot_data.json")
            {
                return documents.slot;
            }
            if (name == "runtime_seed_hints.json")
            {
                return documents.hints;
            }
            if (name == "runtime_seed_settings.json")
            {
                return documents.options;
            }
            return {};
        }
    } // namespace

    auto bind_installed_pak_seed_source() -> void
    {
        json::bind_runtime_seed_source(&document_from_pak);
    }

    auto installed_pak_seed_settings() -> const std::optional<std::string>&
    {
        return installed_documents().settings;
    }

    auto installed_seed_slot_data() -> const JsonObject&
    {
        static const auto slot = []()
        {
            const auto document = installed_documents().slot;
            return document
                       ? JsonObject{std::shared_ptr<const json::Value>(document, &document->value)}
                       : JsonObject{};
        }();
        return slot;
    }
} // namespace kh3ap
