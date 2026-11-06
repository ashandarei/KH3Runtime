#pragma once
#include <cstdint>
#include <exception>
#include <string_view>

namespace kh3ap
{
    enum class Kh02ArrivalInput
    {
        Movement,
        Look
    };

    // Native input ignores are counters. Retire each owned claim independently,
    // and leave only a failed release eligible for another cleanup attempt.
    struct Kh02ArrivalInputClaims
    {
        bool movement{};
        bool look{};

        template <class Release> void release(Release&& operation)
        {
            std::exception_ptr first_failure;
            const auto release_claim = [&](bool& claimed, Kh02ArrivalInput input)
            {
                if (!claimed)
                {
                    return;
                }
                try
                {
                    operation(input);
                    claimed = false;
                }
                catch (...)
                {
                    if (!first_failure)
                    {
                        first_failure = std::current_exception();
                    }
                }
            };
            release_claim(movement, Kh02ArrivalInput::Movement);
            release_claim(look, Kh02ArrivalInput::Look);
            if (first_failure)
            {
                std::rethrow_exception(first_failure);
            }
        }
    };

    enum class Kh02ArrivalPhase
    {
        Idle,
        Loading,
        ClosingLoading,
        Opening,
        Fading,
        Completed,
        Error
    };

    // A generation represents a host and its current source-script instances.
    // Retry invalidates it even when the host UWorld itself survives.
    class Kh02MapSessionState
    {
      public:
        using Generation = std::uint64_t;

        void replace_source()
        {
            ++generation_;
        }

        auto generation() const -> Generation
        {
            return generation_;
        }

        auto accepts(Generation generation) const -> bool
        {
            return generation != 0 && generation == generation_;
        }

        auto accepts_continuation(Generation generation, bool current_world_matches) const -> bool
        {
            // A valid source generation alone cannot authorize a callback while
            // its old World is still alive during a travel handoff.
            return current_world_matches && accepts(generation);
        }

        auto phase() const -> Kh02ArrivalPhase
        {
            return phase_;
        }

        auto active() const -> bool
        {
            return phase_ == Kh02ArrivalPhase::Loading || phase_ == Kh02ArrivalPhase::ClosingLoading ||
                   phase_ == Kh02ArrivalPhase::Opening || phase_ == Kh02ArrivalPhase::Fading;
        }

        void arm()
        {
            if (!active())
            {
                menu_music_released_ = false;
            }
            phase_ = Kh02ArrivalPhase::Loading;
        }

        // Re-arming an active arrival keeps its completed work. A new arrival
        // resets the gate; a failed release remains eligible for another try.
        template <class Release> void release_menu_music_once(Release&& release)
        {
            if (!menu_music_released_)
            {
                release();
                menu_music_released_ = true;
            }
        }

        auto transition(Kh02ArrivalPhase next) -> bool
        {
            const bool allowed = (phase_ == Kh02ArrivalPhase::Loading && next == Kh02ArrivalPhase::ClosingLoading) ||
                                 (phase_ == Kh02ArrivalPhase::ClosingLoading && next == Kh02ArrivalPhase::Opening) ||
                                 ((phase_ == Kh02ArrivalPhase::ClosingLoading || phase_ == Kh02ArrivalPhase::Opening) &&
                                  next == Kh02ArrivalPhase::Fading) ||
                                 (phase_ == Kh02ArrivalPhase::Fading && next == Kh02ArrivalPhase::Completed) ||
                                 (active() && next == Kh02ArrivalPhase::Error) || next == Kh02ArrivalPhase::Idle;
            if (allowed)
            {
                phase_ = next;
            }
            return allowed;
        }

        auto phase_name() const -> std::string_view
        {
            switch (phase_)
            {
            case Kh02ArrivalPhase::Idle:
                return "idle";
            case Kh02ArrivalPhase::Loading:
                return "loading";
            case Kh02ArrivalPhase::ClosingLoading:
                return "closing_loading";
            case Kh02ArrivalPhase::Opening:
                return "opening";
            case Kh02ArrivalPhase::Fading:
                return "fading";
            case Kh02ArrivalPhase::Completed:
                return "completed";
            case Kh02ArrivalPhase::Error:
                return "error";
            }
            return "error";
        }

      private:
        Generation generation_{};
        Kh02ArrivalPhase phase_{Kh02ArrivalPhase::Idle};
        bool menu_music_released_{};
    };
} // namespace kh3ap
