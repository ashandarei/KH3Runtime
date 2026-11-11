#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include "kh3ap/core/performance_build.h"

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class RuntimeGardenReturnService
    {
      public:
        explicit RuntimeGardenReturnService(std::function<std::optional<std::filesystem::path>()> bridge);
        ~RuntimeGardenReturnService();
        void install_once();
        void tick(std::chrono::steady_clock::time_point now);
        static bool finish_kh02_arrival(RC::Unreal::UObject* pawn);
        static bool route_kh02_character(int controller, std::wstring_view key, int event, bool gamepad);
        static bool request_experimental_swap(RC::Unreal::UObject* pawn);
        static bool request_character_trap_swap(RC::Unreal::UObject* pawn);
        static bool character_trap_swap_pending();
#if KH3AP_ENABLE_PERFORMANCE_DIAGNOSTICS
        void set_gummiphone_diagnostic(std::function<std::string(int)>);
#endif
      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace kh3ap
