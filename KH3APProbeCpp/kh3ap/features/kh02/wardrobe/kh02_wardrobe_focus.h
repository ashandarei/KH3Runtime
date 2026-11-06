#pragma once
#include "kh02_wardrobe_presentation.h"
#include <array>
#include <string>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02WardrobeFocus
    {
      public:
        struct Selection
        {
            Kh02WardrobePage page;
            int category{}, selected{}, count{}, gradient_history_count{};
            bool gamepad{};
        };

        static auto path(const Selection& selection) -> std::wstring;
        auto focus(RC::Unreal::UObject* root, RC::Unreal::UObject* cursor_root, const Selection& selection) -> bool;
        void transition(RC::Unreal::UObject* root, RC::Unreal::UObject* cursor_root, const Selection& selection);
        void tick(RC::Unreal::UObject* root, RC::Unreal::UObject* cursor_root, const Selection& selection);

        auto pending() const -> bool
        {
            return focus_pending;
        }

        auto immediate() const -> bool
        {
            return focus_immediate;
        }

        auto target() const -> const std::wstring&
        {
            return cursor_target;
        }

        auto position() const -> const std::array<double, 3>&
        {
            return cursor_position;
        }

      private:
        auto cursor(RC::Unreal::UObject* target, RC::Unreal::UObject* root, const std::wstring& target_path) -> bool;
        bool focus_pending{}, focus_immediate{};
        std::wstring cursor_target;
        std::array<double, 3> cursor_position{};
    };
} // namespace kh3ap
