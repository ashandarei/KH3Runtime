#pragma once
#include <string_view>

namespace kh3ap
{
    enum class Kh02WorldKind
    {
        unavailable,
        other,
        portal,
        story
    };

    // Scheduling only. UObject identities and weak serials belong to the adapter.
    class Kh02Lifecycle
    {
      public:
        enum class Phase
        {
            dormant,
            preparing,
            active,
            leaving
        };

        void prepare()
        {
            if (phase_ == Phase::dormant)
            {
                phase_ = Phase::preparing;
            }
        }

        void observe(Kh02WorldKind kind, bool world_changed)
        {
            if (kind == Kh02WorldKind::unavailable)
            {
                // No valid destination is not evidence of departure.
                return;
            }
            if (kind == Kh02WorldKind::story)
            {
                phase_ = Phase::active;
            }
            else if (phase_ == Phase::active || (phase_ == Phase::preparing && world_changed))
            {
                phase_ = Phase::leaving;
            }
        }

        void finish_leaving()
        {
            if (phase_ == Phase::leaving)
            {
                phase_ = Phase::dormant;
            }
        }

        void cancel_preparation()
        {
            if (phase_ == Phase::preparing)
            {
                phase_ = Phase::leaving;
            }
        }

        auto phase() const -> Phase
        {
            return phase_;
        }

        auto phase_name() const -> std::string_view
        {
            switch (phase_)
            {
            case Phase::dormant:
                return "dormant";
            case Phase::preparing:
                return "preparing";
            case Phase::active:
                return "active";
            case Phase::leaving:
                return "leaving";
            }
            return "dormant";
        }

      private:
        Phase phase_{Phase::dormant};
    };
} // namespace kh3ap
