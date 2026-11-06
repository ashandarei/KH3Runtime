#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <chrono>
#include <functional>
#include <string>
#include <string_view>

namespace kh3ap
{
    // Character replacement creates a new friend manager. Preserve the source
    // encounter's Mickey assignment without discovering actors on idle frames.
    class Kh02CompanionHandoff
    {
      public:
        void prepare(RC::Unreal::UObject* pawn, std::wstring_view world);
        void tick(const std::function<RC::Unreal::UObject*()>& player, std::wstring_view world);

        auto pending() const -> bool
        {
            return slot >= 0;
        }

        auto status() const -> const std::string&
        {
            return phase;
        }

      private:
        RC::Unreal::FWeakObjectPtr previous_manager;
        RC::Unreal::FWeakObjectPtr previous_pawn;
        RC::Unreal::FWeakObjectPtr previous_world;
        std::wstring source_world;
        std::chrono::steady_clock::time_point deadline;
        std::chrono::steady_clock::time_point retry_at;
        int slot{-1};
        bool retired{};
        bool spawn_dispatched{};
        std::string phase{"idle"};
    };
} // namespace kh3ap
