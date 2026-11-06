#pragma once
#include <algorithm>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>

namespace kh3ap
{
    // Owns entry orchestration. Engine bindings are sampled by the coordinator;
    // callbacks are borrowed for one tick and never retained across travel.
    class Kh02EntrySession
    {
      public:
        using Clock = std::chrono::steady_clock;
        enum class Phase
        {
            disabled,
            waiting_for_saved_progress,
            preparing_gameplay,
            preparing_boss_retry,
            waiting_for_control,
            loading,
            playing,
            restoring_view,
            completed,
            error,
            failed
        };

        struct Environment
        {
            bool control_ready{};
            bool arrival_cover_owned{};
            bool arrival_opening{};
            bool arrival_blocks_control{};
        };

        struct SceneObservation
        {
            bool valid{};
            bool playing{};
            bool script_alive{};
            float position{};
        };

        struct Operations
        {
            std::function<void()> prepare_gameplay;
            std::function<bool()> prepare_scene;
            std::function<void()> start_scene;
            std::function<bool()> unload_scene;
            std::function<SceneObservation()> observe_scene;
            std::function<bool()> opening_finished;
            std::function<void()> release_opening_music;
            std::function<Clock::time_point()> current_time;
            std::function<bool()> arrival_opening;
            std::function<bool()> arrival_blocks_control;
        };

        // Configuration and native preparation observations are supplied by the
        // story coordinator. Playback progression is driven only by tick().
        bool enabled{};
        bool gameplay{};
        bool initialized{};
        bool first_castle_intro{};

        auto phase() const -> Phase
        {
            return current_phase;
        }

        auto phase_name() const -> const char*
        {
            switch (current_phase)
            {
            case Phase::disabled:
                return "disabled";
            case Phase::waiting_for_saved_progress:
                return "waiting_for_saved_progress";
            case Phase::preparing_gameplay:
                return "preparing_gameplay";
            case Phase::preparing_boss_retry:
                return "preparing_boss_retry";
            case Phase::waiting_for_control:
                return "waiting_for_control";
            case Phase::loading:
                return "loading";
            case Phase::playing:
                return "playing";
            case Phase::restoring_view:
                return "restoring_view";
            case Phase::completed:
                return "completed";
            case Phase::error:
                return "error";
            case Phase::failed:
                return "failed";
            }
            throw std::logic_error("kh02_entry_phase");
        }

        auto done() const -> bool
        {
            return completed;
        }

        auto observed_playing() const -> bool
        {
            return observed;
        }

        auto maximum_position() const -> float
        {
            return maximum;
        }

        auto failure() const -> const std::string&
        {
            return error;
        }

        void reset_for_world(Clock::time_point now)
        {
            enabled = false;
            completed = false;
            reset_observation();
            error.clear();
            transition(Phase::disabled, now);
        }

        void begin_preparation(Clock::time_point now)
        {
            gameplay = true;
            enabled = true;
            completed = false;
            error.clear();
            transition(Phase::preparing_gameplay, now);
        }

        void await_saved_progress()
        {
            completed = false;
            current_phase = Phase::waiting_for_saved_progress;
        }

        void preparation_finished()
        {
            current_phase = first_castle_intro ? Phase::waiting_for_control : Phase::completed;
            completed = !first_castle_intro;
        }

        void configure(bool requested, bool prepare_gameplay)
        {
            gameplay = prepare_gameplay;
            enabled = requested;
            current_phase = enabled ? Phase::waiting_for_control : Phase::disabled;
        }

        void begin_boss_retry(Clock::time_point now)
        {
            gameplay = true;
            enabled = true;
            completed = false;
            error.clear();
            transition(Phase::preparing_boss_retry, now);
        }

        void reset_observation()
        {
            observed = false;
            maximum = 0;
        }

        void fail(std::string reason)
        {
            error = std::move(reason);
            current_phase = Phase::error;
            completed = true;
        }

        void retire()
        {
            enabled = false;
            gameplay = false;
            completed = true;
            current_phase = Phase::failed;
        }

        void tick(const Environment& environment, const Operations& operations, Clock::time_point now)
        {
            if (!enabled || completed)
            {
                return;
            }
            const auto current_time = [&] { return operations.current_time ? operations.current_time() : now; };
            const auto arrival_opening = [&]
            { return operations.arrival_opening ? operations.arrival_opening() : environment.arrival_opening; };
            const auto arrival_blocks_control = [&]
            {
                return operations.arrival_blocks_control ? operations.arrival_blocks_control()
                                                         : environment.arrival_blocks_control;
            };
            if (gameplay && !initialized)
            {
                if (current_time() - since > preparation_timeout)
                {
                    throw std::runtime_error("kh02_entry_gameplay_timeout");
                }
                operations.prepare_gameplay();
                return;
            }
            if (current_phase == Phase::waiting_for_control)
            {
                if (first_castle_intro && environment.arrival_cover_owned)
                {
                    if (!operations.prepare_scene() || !arrival_opening())
                    {
                        return;
                    }
                }
                if (!environment.control_ready || arrival_blocks_control() ||
                    (!first_castle_intro && current_time() - since < arrival_settle_duration))
                {
                    return;
                }
                transition(Phase::loading, current_time());
            }
            if (current_phase == Phase::loading)
            {
                if (!environment.control_ready)
                {
                    return;
                }
                if (!operations.prepare_scene())
                {
                    if (current_time() - since > preparation_timeout)
                    {
                        throw std::runtime_error("story_entry_load_timeout");
                    }
                    return;
                }
                operations.start_scene();
                transition(Phase::playing, current_time());
                return;
            }
            if (current_phase == Phase::playing)
            {
                const auto scene = operations.observe_scene();
                observed |= scene.playing;
                if (scene.valid)
                {
                    maximum = std::max(maximum, scene.position);
                }
                const bool chain_finished = operations.opening_finished();
                if (observed && !scene.playing && !scene.script_alive && chain_finished)
                {
                    if (first_castle_intro)
                    {
                        operations.release_opening_music();
                    }
                    if (operations.unload_scene())
                    {
                        finish();
                    }
                    else
                    {
                        current_phase = Phase::restoring_view;
                    }
                    return;
                }
                const auto timeout = gameplay && first_castle_intro ? opening_playback_timeout : scene_playback_timeout;
                if (current_time() - since > timeout)
                {
                    throw std::runtime_error("story_entry_playback_timeout");
                }
            }
            if (current_phase == Phase::restoring_view && operations.unload_scene())
            {
                finish();
            }
        }

      private:
        static constexpr auto preparation_timeout = std::chrono::seconds(90);
        static constexpr auto opening_playback_timeout = std::chrono::seconds(240);
        static constexpr auto scene_playback_timeout = std::chrono::seconds(30);
        static constexpr auto arrival_settle_duration = std::chrono::seconds(2);
        Phase current_phase = Phase::disabled;
        Clock::time_point since{};
        bool completed{};
        bool observed{};
        float maximum{};
        std::string error;

        void transition(Phase next, Clock::time_point now)
        {
            current_phase = next;
            since = now;
        }

        void finish()
        {
            current_phase = Phase::completed;
            completed = true;
        }
    };
} // namespace kh3ap
