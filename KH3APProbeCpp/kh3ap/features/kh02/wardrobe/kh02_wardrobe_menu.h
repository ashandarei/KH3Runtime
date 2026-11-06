#pragma once
#include "kh3ap/features/kh02/challenges/kh02_challenge_log.h"
#include <nlohmann/json.hpp>
#include <memory>

namespace kh3ap
{
    class Kh02WardrobeMenu
    {
      public:
        Kh02WardrobeMenu();
        ~Kh02WardrobeMenu();
        void configure(std::function<Kh02CostumeList(int)> list, std::function<void(int, int)> equip,
                       std::function<std::string()> read, std::function<void(const std::string&)> write);
        auto prepare(RC::Unreal::UObject* pawn) -> bool;
        void open(RC::Unreal::UObject* root);
        void tick(RC::Unreal::UObject* root);
        void input_mode(RC::Unreal::UObject* root, bool gamepad);
        auto move(RC::Unreal::UObject* root, int delta) -> bool;
        auto horizontal(RC::Unreal::UObject* root, int delta) -> bool;
        auto confirm(RC::Unreal::UObject* root) -> bool;
        auto back(RC::Unreal::UObject* root) -> bool;
        void cancel();
        auto snapshot() const -> nlohmann::json;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
