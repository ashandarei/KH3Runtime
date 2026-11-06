#pragma once
#include "kh02_subtitle_timeline.h"
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <functional>
#include <vector>

namespace kh3ap
{
    class Kh02MovieBridge;

    class Kh02EndingSubtitles
    {
      public:
        using LocalizedMessage = std::function<nlohmann::json(const wchar_t*, const wchar_t*, std::wstring*)>;
        void select_full_movie(RC::Unreal::UObject* matinee, std::string logical);

        auto is_full_movie(RC::Unreal::UObject* matinee) const -> bool
        {
            return matinee == ending_full_movie.Get();
        }

        auto disabled_tracks() const -> std::size_t
        {
            return ending_movie_disabled_tracks.size();
        }

        auto needs_frame() const -> bool;
        void prepare(RC::Unreal::UObject* matinee, nlohmann::json& proof);
        void tick(RC::Unreal::UObject* world, Kh02MovieBridge& movies, const LocalizedMessage& localized_message,
                  nlohmann::json& proof);
        // Preserve session teardown order: stop presentation, restore scene actors,
        // restore disabled source tracks, then discard weak identities.
        void retire_presentation() noexcept;
        void restore_tracks() noexcept;
        void reset();

      private:
        void close();
        RC::Unreal::FWeakObjectPtr ending_full_movie;
        std::string ending_full_movie_logical;
        std::vector<RC::Unreal::FWeakObjectPtr> ending_movie_disabled_tracks;

        RC::Unreal::FWeakObjectPtr ending_subtitle_matinee;
        std::wstring ending_subtitle_namespace;
        std::vector<Kh02SubtitleCue> ending_subtitle_keys;
        int ending_subtitle_index{-1};
        bool ending_subtitle_started{};
        bool ending_matinee_held{};
    };
} // namespace kh3ap
