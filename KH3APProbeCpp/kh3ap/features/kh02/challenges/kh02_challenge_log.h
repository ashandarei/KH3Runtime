#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    struct Kh02ChallengeRow
    {
        std::wstring text, progress, description;
        int state{};
    };

    struct Kh02CostumeList
    {
        std::wstring title;
        std::vector<Kh02ChallengeRow> rows;
        int selected{};
    };

    // Presentation only. Original objectives and save ownership remain in Kh02Story.
    class Kh02ChallengeLog
    {
      public:
        Kh02ChallengeLog();
        ~Kh02ChallengeLog();
        void tick(RC::Unreal::UObject* pawn, bool available);
        auto open(RC::Unreal::UObject* camp, std::vector<Kh02ChallengeRow> rows) -> bool;
        auto open_selection(RC::Unreal::UObject* host, std::wstring title, std::vector<Kh02ChallengeRow> rows,
                            std::function<void(int)> confirm) -> bool;
        auto open_costumes(RC::Unreal::UObject* host, std::function<Kh02CostumeList(int)> list,
                           std::function<void(int, int)> equip, std::function<std::string()> read,
                           std::function<void(const std::string&)> write, std::function<void(bool, float)> preview)
            -> bool;
        void close();
        auto active() const -> bool;
        auto needs_tick() const -> bool;
        auto route(int controller, std::wstring_view key, int event, bool gamepad) -> bool;
        auto snapshot() -> std::string;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
