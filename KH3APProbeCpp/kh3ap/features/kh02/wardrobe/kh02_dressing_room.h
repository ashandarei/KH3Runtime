#pragma once
#include "kh3ap/features/kh02/challenges/kh02_challenge_log.h"
#include "kh3ap/features/kh02/progress/kh02_progress_state.h"
#include <functional>
#include <memory>
#include <string>

namespace kh3ap
{
    class Kh02DressingRoom
    {
      public:
        Kh02DressingRoom();
        ~Kh02DressingRoom();
        void restore(const std::string& progress);
        void restore(const std::optional<kh02_progress::CosmeticSelection>& selection);
        void release(bool restore_materials);
        auto selection() const -> std::string;
        auto progress_selection() const -> kh02_progress::CosmeticSelection;
        auto snapshot() const -> std::string;
        auto list(int category) const -> Kh02CostumeList;
        void choose(int category, int index);
        void set_selection(const std::string& selection);
        void adjust_color(int index, int delta);
        void preview(bool visible, float angle);
        void on_change(std::function<void()> changed);
        void observe(RC::Unreal::UObject* hero, bool available);
        void tick(RC::Unreal::UObject* hero, bool available);
        auto needs_tick() const -> bool;
        auto ready() const -> bool;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
