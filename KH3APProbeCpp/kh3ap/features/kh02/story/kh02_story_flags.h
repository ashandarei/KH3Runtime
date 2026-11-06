#pragma once
#include "kh3ap/features/kh02/progress/kh02_story_checkpoints.h"
#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include "kh3ap/features/kh02/progress/kh02_progress_state.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Local experiment state. Source Blueprint calls never use KH3's flag bank.
    class Kh02StoryFlags
    {
      public:
        Kh02StoryFlags();
        ~Kh02StoryFlags();
        void activate(RC::Unreal::UObject* world, int checkpoint = kh02_checkpoint::gear_scene_dw012,
                      bool restart = false);
        void rebind_boss_retry(RC::Unreal::UObject* world);
        void rebind_mirror(RC::Unreal::UObject* world);
        void reset_mirror_hub_entry();
        void select_mirror_room_entry(int area);
        auto active() const -> bool;
        auto value(std::wstring_view key, int fallback = 0) const -> int;
        auto values() const -> nlohmann::json;
        auto progress_values() const -> kh02_progress::Flags;
        auto snapshot() const -> std::string;
        void restore_collected_gears(const std::string& snapshot);
        void restore_mirror(const std::string& flags);
        void restore_mirror(const nlohmann::json& flags);
        void restore_mirror(const kh02_progress::Flags& flags);
        void on_changed(std::function<void()> callback);
        void record_chest(const std::wstring& id);
        void record_visit(int area);
        void record_aqua_experience(int total);
        void reset();

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
