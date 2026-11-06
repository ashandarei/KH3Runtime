#pragma once
#include <memory>
#include <string>
#include <optional>
#include <nlohmann/json.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02MovieBridge
    {
        struct Impl;
        std::unique_ptr<Impl> impl;

      public:
        Kh02MovieBridge();
        ~Kh02MovieBridge();
        void prepare(RC::Unreal::UObject* pawn, RC::Unreal::UObject* script, const std::wstring& movie,
                     bool finish_event);
        void prewarm(RC::Unreal::UObject* pawn, const std::wstring& movie);
        void wait(RC::Unreal::UObject* script, int linkage, const std::wstring& function);
        void wait_finish(RC::Unreal::UObject* script, int linkage, const std::wstring& function);
        void resume();
        void stop();
        // Retire a map/Retry generation without dispatching any old completion.
        void retire_session();
        void tick(RC::Unreal::UObject* pawn);
        auto needs_tick() const -> bool;
        auto playing() const -> bool;
        auto presentation_seconds() const -> std::optional<float>;
        auto completed_movie(std::string_view logical) const -> bool;
        auto error() const -> const std::string&;
        auto snapshot() const -> nlohmann::json;
    };
} // namespace kh3ap
