#pragma once
#include "kh02_challenge_navigation.h"
#include "kh3ap/core/input/options_input_ownership.h"
#include <algorithm>
#include <cstdint>
#include <exception>
#include <string_view>
#include <utility>

namespace kh3ap
{
    class Kh02ChallengeInputRouter
    {
      public:
        enum class Action
        {
            InputMode,
            Back,
            ConfirmWardrobe,
            DecideFeedback,
            RotatePreview,
            Horizontal,
            ConfirmSelection,
            Vertical
        };

        struct Context
        {
            bool active{}, visible{}, wardrobe{}, can_confirm{};
        };

        struct RepeatStats
        {
            uint64_t steps{}, last_ms{}, max_gap_ms{};

            void record(int64_t now)
            {
                if (last_ms)
                {
                    max_gap_ms = std::max(max_gap_ms, uint64_t(now) - last_ms);
                }
                last_ms = now;
                ++steps;
            }
        };

        // Ownership survives closing the menu until each physical key is released.
        template <typename Dispatch, typename Failure>
        auto route(int controller, std::wstring_view key, int event, bool gamepad, int64_t now, const Context& context,
                   Dispatch&& dispatch, Failure&& failure) -> bool
        {
            const bool controller_key = gamepad || key.starts_with(L"Gamepad_");
            const auto decoded = decode(key, context.wardrobe);
            const auto handle = [&]
            {
                if (controller_key && decoded.vertical && event == released)
                {
                    vertical.accept(controller, key, decoded.vertical, event, now);
                }
                if (decoded.horizontal && event == released)
                {
                    horizontal.accept(controller, key, decoded.horizontal, event, now);
                }
                if (!context.active || (event != pressed && event != repeated))
                {
                    return false;
                }
                try
                {
                    if (context.visible && context.wardrobe)
                    {
                        dispatch(Action::InputMode, controller_key ? 1 : 0);
                    }
                    if (decoded.back)
                    {
                        if (event == pressed)
                        {
                            reset();
                            dispatch(Action::Back, 0);
                        }
                    }
                    else if (context.visible && context.wardrobe && event == pressed && decoded.confirm)
                    {
                        if (dispatch(Action::ConfirmWardrobe, 0))
                        {
                            reset();
                            dispatch(Action::DecideFeedback, 0);
                        }
                    }
                    else if (context.visible && context.wardrobe && decoded.camera)
                    {
                        dispatch(Action::RotatePreview, decoded.camera);
                    }
                    else if (context.visible && context.wardrobe && decoded.horizontal)
                    {
                        if (horizontal.accept(controller, key, decoded.horizontal, event, now))
                        {
                            horizontal_repeat = {};
                            dispatch(Action::Horizontal, decoded.horizontal);
                        }
                    }
                    else if (context.visible && context.can_confirm && decoded.confirm && event == pressed)
                    {
                        dispatch(Action::ConfirmSelection, 0);
                    }
                    else if (context.visible && decoded.vertical)
                    {
                        if (!controller_key || vertical.accept(controller, key, decoded.vertical, event, now))
                        {
                            dispatch(Action::Vertical, decoded.vertical);
                        }
                    }
                }
                catch (const std::exception& exception)
                {
                    failure(exception.what());
                }
                return true;
            };
            const bool consumed = ownership.route(controller, std::wstring(key), event, handle);
            if (consumed && event == repeated)
            {
                // Only an overlay-owned press may supply a repeat.
                (void)handle();
            }
            return consumed;
        }

        auto poll_vertical(int64_t now) -> int
        {
            const int vertical_delta = vertical.poll(now);
            if (vertical_delta)
            {
                vertical_repeat.record(now);
            }
            return vertical_delta;
        }

        auto poll_horizontal(int64_t now) -> int
        {
            const int horizontal_delta = horizontal.poll(now, horizontal_repeat_interval_ms);
            if (horizontal_delta)
            {
                horizontal_repeat.record(now);
            }
            return horizontal_delta;
        }

        void reset()
        {
            vertical.reset();
            horizontal.reset();
        }

        void reset_vertical_stats()
        {
            vertical_repeat = {};
        }

        auto vertical_stats() const -> const RepeatStats&
        {
            return vertical_repeat;
        }

        auto horizontal_stats() const -> const RepeatStats&
        {
            return horizontal_repeat;
        }

      private:
        static constexpr int pressed = 0, released = 1, repeated = 2;
        static constexpr int page_step = 8;
        static constexpr int64_t horizontal_repeat_interval_ms = 50;

        struct Decoded
        {
            int vertical{}, horizontal{}, camera{};
            bool back{}, confirm{};
        };

        static auto decode(std::wstring_view key, bool wardrobe) -> Decoded
        {
            Decoded value;
            if (key == L"Up" || key == L"W" || key == L"MouseScrollUp" || key == L"Gamepad_DPad_Up" ||
                key == L"Gamepad_LeftStick_Up")
            {
                value.vertical = -1;
            }
            if (key == L"Down" || key == L"S" || key == L"MouseScrollDown" || key == L"Gamepad_DPad_Down" ||
                key == L"Gamepad_LeftStick_Down")
            {
                value.vertical = 1;
            }
            if (!wardrobe && (key == L"PageUp" || key == L"Gamepad_LeftShoulder"))
            {
                value.vertical = -page_step;
            }
            if (!wardrobe && (key == L"PageDown" || key == L"Gamepad_RightShoulder"))
            {
                value.vertical = page_step;
            }
            if (key == L"Left" || key == L"Gamepad_DPad_Left" || key == L"Gamepad_LeftStick_Left")
            {
                value.horizontal = -1;
            }
            if (key == L"Right" || key == L"Gamepad_DPad_Right" || key == L"Gamepad_LeftStick_Right")
            {
                value.horizontal = 1;
            }
            if (key == L"A" || key == L"Gamepad_RightStick_Left")
            {
                value.camera = -1;
            }
            if (key == L"D" || key == L"Gamepad_RightStick_Right")
            {
                value.camera = 1;
            }
            value.back = key == L"Escape" || key == L"BackSpace" || key == L"Gamepad_FaceButton_Right";
            value.confirm = key == L"Enter" || key == L"Gamepad_FaceButton_Bottom";
            return value;
        }

        RuntimeOptionsInputOwnership ownership;
        Kh02ChallengeControllerNavigation vertical, horizontal;
        RepeatStats vertical_repeat, horizontal_repeat;
    };
} // namespace kh3ap
