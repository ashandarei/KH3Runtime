#pragma once

#include <memory>

namespace kh3ap
{
    // Dispatches the native gimmick damage event to encounter rules and diagnostics.
    class GimmickDamageHook
    {
    public:
        GimmickDamageHook();
        ~GimmickDamageHook();
        void install();
        void uninstall();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
