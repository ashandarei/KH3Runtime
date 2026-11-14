#pragma once
#include <cstdint>

namespace kh3ap
{
    // One request per scene occurrence, including a re-entrant scene change
    // during dispatch. Timeouts never cause a second skip or forced teleport.
    class CutsceneSkipTransaction
    {
    public:
        enum class Action
        {
            None,
            Request,
            Transition,
            Cancel
        };
        bool begin(uint64_t scene, int64_t now)
        {
            if (scene == scene_)
            {
                return false;
            }
            scene_ = scene;
            active_ = true;
            requested_ = false;
            deadline_ = now + 15000;
            return true;
        }
        auto advance(int64_t now, bool enabled, bool same_world, bool source_alive, bool skippable,
                     bool busy) -> Action
        {
            if (!active_)
            {
                return Action::None;
            }
            if (requested_ && (!same_world || !source_alive || !skippable))
            {
                active_ = false;
                return Action::Transition;
            }
            if (!enabled || !same_world || !source_alive || !skippable || now >= deadline_)
            {
                active_ = false;
                return Action::Cancel;
            }
            return requested_ || busy ? Action::None : Action::Request;
        }
        void mark_requested()
        {
            requested_ = true;
        }
        // Busy means the native function was never entered. Preserve the
        // original deadline while waiting for its UI/fade owner to finish.
        void defer_unissued_request()
        {
            requested_ = false;
        }
        void cancel()
        {
            active_ = false;
        }
        bool active() const
        {
            return active_;
        }
        bool requested() const
        {
            return requested_;
        }
        uint64_t scene() const
        {
            return scene_;
        }

    private:
        uint64_t scene_{};
        int64_t deadline_{};
        bool active_{}, requested_{};
    };
} // namespace kh3ap
