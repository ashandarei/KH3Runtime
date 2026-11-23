#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <map>
#include <optional>
#include <string_view>

namespace kh3ap
{
    class TitleShortcut
    {
      public:
        template <class ReadChord>
        bool take_xinput_chord(ReadChord read_chord)
        {
            for (auto& [controller, state] : controllers_)
            {
                if (!state.held.test(1) || state.attempted || state.xinput_unavailable)
                {
                    continue;
                }
                const auto chord = read_chord(controller);
                if (!chord.has_value())
                {
                    state.xinput_unavailable = true;
                }
                else if (*chord)
                {
                    state.attempted = true;
                    return true;
                }
            }
            return false;
        }

        // KH3's native DualSense mapping omits Create from viewport events.
        // Options can open a menu that takes ownership of later shoulder events.
        // Use its hold to arm a read of all four buttons on one physical device.
        auto awaiting_create() const -> std::optional<int32_t>
        {
            for (const auto& [controller, state] : controllers_)
            {
                if (!state.attempted && state.held.test(1))
                {
                    return controller;
                }
            }
            return {};
        }

        void mark_physical_attempt(int32_t controller)
        {
            controllers_.at(controller).attempted = true;
        }

        template <class ReturnToTitle>
        bool route(int32_t controller, std::wstring_view key, int32_t event, ReturnToTitle return_to_title)
        {
            const auto button = std::find(buttons.begin(), buttons.end(), key);
            if (button == buttons.end())
            {
                return false;
            }
            const auto index = static_cast<size_t>(button - buttons.begin());
            auto found = controllers_.find(controller);
            if (event != 0 && found == controllers_.end())
            {
                return false;
            }
            auto& state = event == 0 ? controllers_[controller] : found->second;
            const bool owned = state.consumed == index;
            if (event == 1)
            {
                state.held.reset(index);
                if (index == 1)
                {
                    state.xinput_unavailable = false;
                }
                if (owned)
                {
                    state.consumed = buttons.size();
                }
                if (state.held.none())
                {
                    controllers_.erase(controller);
                }
                return owned;
            }
            if (event != 0 || state.held.test(index))
            {
                return owned;
            }
            state.held.set(index);
            if (!state.held.all() || state.attempted)
            {
                return false;
            }
            // Re-arm after all four releases, including when travel was unavailable.
            state.attempted = true;
            state.consumed = index;
            if (return_to_title())
            {
                return true;
            }
            state.consumed = buttons.size();
            return false;
        }

      private:
        static constexpr std::array<std::wstring_view, 4> buttons{
            L"Gamepad_Special_Left", L"Gamepad_Special_Right", L"Gamepad_LeftShoulder", L"Gamepad_RightShoulder"};
        struct Controller
        {
            std::bitset<buttons.size()> held;
            size_t consumed{buttons.size()};
            bool attempted{};
            bool xinput_unavailable{};
        };
        std::map<int32_t, Controller> controllers_;
    };
}
