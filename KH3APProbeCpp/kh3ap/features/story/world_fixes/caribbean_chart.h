#pragma once

#include <memory>

namespace kh3ap
{
    class RuntimeState;

    class RuntimeCaribbeanChart
    {
    public:
        explicit RuntimeCaribbeanChart(RuntimeState& runtime_state);
        ~RuntimeCaribbeanChart();
        auto install_once() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
