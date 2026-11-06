#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    // On-demand chapter commands remain available before first entry. No
    // presentation runtime, imported assets or provider is needed to edit a bank.
    class Kh02ProgressService
    {
      public:
        using Invalidate = std::function<void()>;
        static auto command(RC::Unreal::UObject* pawn, std::wstring_view request_id, std::wstring_view action,
                            std::wstring_view flag_name, std::optional<int> value, const Invalidate& invalidate)
            -> std::string;
        static auto reset(RC::Unreal::UObject* pawn, const Invalidate& invalidate) -> std::string;
    };
} // namespace kh3ap
