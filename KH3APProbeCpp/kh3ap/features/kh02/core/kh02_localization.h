#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace kh3ap
{
    // Entry-time resource preparation and synchronous message lookup. Owns no
    // world objects; the existing display-language and voice policies are separate.
    class Kh02Localization
    {
      public:
        void prepare();
        auto lookup(const wchar_t* text_namespace = L"kh02_message.dw", const wchar_t* text_key = L"information_005",
                    std::wstring* localized_text = nullptr) -> nlohmann::json;
        auto snapshot() const -> const nlohmann::json&;

      private:
        bool ready{};
        nlohmann::json proof = nlohmann::json::object();
    };
} // namespace kh3ap
