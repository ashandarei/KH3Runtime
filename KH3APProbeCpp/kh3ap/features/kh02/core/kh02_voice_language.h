#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace kh3ap
{
    enum class Kh02VoiceLanguage
    {
        english,
        japanese
    };
    auto parse_kh02_voice_language(std::wstring_view language) -> std::optional<Kh02VoiceLanguage>;
    auto kh02_voice_language_code(Kh02VoiceLanguage language) -> const char*;
    auto current_kh02_voice_language() -> Kh02VoiceLanguage;
    // Explicit local title-screen fixture only; never saves game settings.
    auto kh02_voice_language_fixture(std::wstring_view requested) -> nlohmann::json;
} // namespace kh3ap
