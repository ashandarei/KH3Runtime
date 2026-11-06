#pragma once
namespace RC
{
    enum class LogLevel { Verbose, Warning };
    namespace Output
    {
        template <LogLevel Level, typename... Args>
        auto send(const wchar_t*, Args&&...) -> void {}
    }
}
