#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <vector>

namespace kh3ap
{
    class Kh02EndingVoice
    {
      public:
        void prepare(RC::Unreal::UObject* matinee, nlohmann::json& ending_presentation);
        void restore() noexcept;
        // Called explicitly when the containing session is destroyed, after restore.
        void release_root();

      private:
        RC::Unreal::FWeakObjectPtr ending_voice_attenuation;

        struct VoiceOverride
        {
            RC::Unreal::FWeakObjectPtr sound;
            RC::Unreal::FWeakObjectPtr attenuation;
            bool ignored_audio_volume{};
        };

        std::vector<VoiceOverride> voice_overrides;
    };
} // namespace kh3ap
