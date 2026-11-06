#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <nlohmann/json.hpp>
#include <vector>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // Tracks only roots acquired here. Release is explicit on the game thread;
    // roots that already belonged to another subsystem are never cleared here.
    class Kh02EntryAssets
    {
      public:
        void retain(RC::Unreal::UObject* asset);
        void release();
        auto paths() const -> nlohmann::json;

      private:
        std::vector<RC::Unreal::FWeakObjectPtr> roots;
    };
} // namespace kh3ap
