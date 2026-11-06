#pragma once
namespace RC
{
    enum class LogLevel { Warning, Error };
    namespace Output
    {
        inline int warnings{};
        template <LogLevel Level, typename... Args> auto send(const wchar_t*, Args&&...) -> void
        {
            ++warnings;
        }
    }
}
