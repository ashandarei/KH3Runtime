#pragma once

#include "kh3ap/features/player/emblems/lucky_emblem_record.h"
#include <filesystem>
#include <optional>
#include <memory>
#include <string>

namespace RC::Unreal { class UObject; }
namespace PLH { class x64Detour; }

namespace kh3ap
{
    class LuckyEmblemPersistence
    {
    public:
        LuckyEmblemPersistence();
        ~LuckyEmblemPersistence();
        auto install_observer() -> void;
        static auto photo_revision() -> uint64_t;
        // Called only for photo completion, load/restore, or map readiness.
        // No UObject or native save pointer survives this call.
        auto reconcile(const std::filesystem::path& bridge, std::wstring_view seed,
                       int64_t slot, RC::Unreal::UObject* pawn) -> bool;
    private:
        std::optional<std::filesystem::path> bridge_;
        std::wstring seed_;
        int64_t slot_{};
        emblem_record::Words remembered_{};
        std::unique_ptr<PLH::x64Detour> hook_;
        uint64_t original_{};
        static auto photographed(RC::Unreal::UObject* component, bool recorded) -> void;
    };
}
