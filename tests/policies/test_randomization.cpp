#include "kh3ap/features/enemies/randomization.h"
#include "kh3ap/core/settings/pak_seed_settings.h"
#include <iostream>
#include "kh3ap/bridge/serialization/json.h"
#include <string>

// Python drives the real native generator/reader against archive and policy fixtures.
int main()
{
    std::string line;
    while (std::getline(std::cin, line))
    {
        try
        {
            const auto input = nlohmann::json::parse(line);
            auto settings = input;
            if (input.contains("catalog_pak"))
            {
                const auto records = kh3ap::read_pak_seed_records(std::filesystem::u8path(input.at("catalog_pak").get<std::string>()));
                const auto slot = kh3ap::json::decode_seed_document("slot_data.json", records.at("slot_data.json"));
                std::cout << nlohmann::json{{"slot_data", slot->value}, {"hints", records.contains("runtime_seed_hints.json") ? nlohmann::json::parse(records.at("runtime_seed_hints.json")) : nlohmann::json{}}}.dump() << '\n';
                continue;
            }
            if (input.contains("pak"))
                settings = nlohmann::json::parse(kh3ap::read_pak_seed_settings(
                    std::filesystem::u8path(input.at("pak").get<std::string>())));
            if (input.contains("mods"))
            {
                const auto path = kh3ap::find_seed_pak(std::filesystem::u8path(input.at("mods").get<std::string>()));
                std::cout << nlohmann::json{{"pak", path.generic_string()}}.dump() << '\n';
            }
            else std::cout << kh3ap::randomization::generate(settings).dump() << '\n';
        }
        catch (const std::exception& error)
        {
            std::cout << nlohmann::json{{"error", error.what()}}.dump() << '\n';
        }
    }
}
