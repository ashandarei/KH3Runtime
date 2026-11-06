#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>

namespace RC::Unreal { class UObject; }
namespace kh3ap
{
    class RuntimePartyMemberService;
    struct RuntimePartyTestConfig
    {
        RuntimePartyMemberService& party;
        std::function<std::optional<std::filesystem::path>()> bridge_dir;
        std::function<RC::Unreal::UObject*()> playable_sora;
        std::function<bool()> gameplay_ready;
        std::function<bool()> control_ready;
    };

    // Explicit bridge requests only; no UObject inspection while idle.
    class RuntimePartyTestService
    {
    public:
        explicit RuntimePartyTestService(RuntimePartyTestConfig config);
        ~RuntimePartyTestService();
        void tick();
    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
