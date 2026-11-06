#pragma once
#include <Unreal/FWeakObjectPtr.hpp>
#include <array>
#include <functional>

namespace RC::Unreal
{
    class UObject;
}
namespace kh3ap::character_party
{
    using Observer =
        std::function<void(RC::Unreal::UObject *, RC::Unreal::UObject *, RC::Unreal::UObject *)>;
    void set_observer(Observer observer);
    bool transferring();

    // Shares the owning swap's bounded lifetime. Empty slots are significant.
    class Handoff
    {
      public:
        explicit Handoff(RC::Unreal::UObject *outgoing);
        ~Handoff();
        Handoff(const Handoff &) = delete;
        Handoff &operator=(const Handoff &) = delete;
        void finish(RC::Unreal::UObject *incoming);

      private:
        RC::Unreal::FWeakObjectPtr source_manager_, source_world_;
        std::array<RC::Unreal::FWeakObjectPtr, 6> friends_{};
        std::array<RC::Unreal::FWeakObjectPtr, 6> default_classes_{}, classes_{};
        std::array<bool, 6> expected_friends_{};
        bool detached_{};
    };
} // namespace kh3ap::character_party
