#pragma once
#include "kh3ap/core/input/runtime_player_control.h"
#include "kh3ap/core/objects/uobject_weak.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace RC::Unreal { class UObject; }
namespace kh3ap
{
    class RuntimeEnemyRemapService;
    auto runtime_damage_sora_for_checkpoint(RC::Unreal::UObject* pawn) -> bool;
    // Diagnostic callers must resolve the target from the current friend slots.
    auto runtime_damage_friend_for_test(RC::Unreal::UObject* sora, RC::Unreal::UObject* target) -> bool;
    struct RuntimeEnemyClearConfig
    {
        std::function<std::optional<std::filesystem::path>()> bridge_dir;
        std::function<RC::Unreal::UObject*()> playable_sora;
        std::function<bool()> gameplay_ready;
        RuntimeEnemyRemapService* enemy_remaps{};
        std::function<bool()> special_battle_mode_active;
    };

    // On-demand actor enumeration and native defeat/position operations only.
    class RuntimeEnemyClearService
    {
    public:
        explicit RuntimeEnemyClearService(RuntimeEnemyClearConfig config);
        ~RuntimeEnemyClearService();
        void tick();
        // Diagnostic New Game lease: protect each possessed opening pawn before
        // control handoff, restoring on Garden arrival or the bounded deadline.
        static bool arm_opening_protection(int seconds);
        // Preserve an existing diagnostic lease across an explicit local swap.
        static void prepare_protection_handoff(RC::Unreal::UObject* outgoing);
        static void handoff_protection(const RC::Unreal::FWeakObjectPtr& outgoing,
                                       RC::Unreal::UObject* incoming);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
