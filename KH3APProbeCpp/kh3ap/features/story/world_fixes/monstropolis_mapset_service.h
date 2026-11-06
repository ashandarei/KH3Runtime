#pragma once

#include <memory>
#include <string_view>

namespace kh3ap
{
    class RuntimeState;
    struct RuntimeMonstropolisMapSetConfig
    {
        RuntimeState& runtime_state;
        std::wstring_view get_game_flag_path;
        std::wstring_view blueprint_library_default_object_path;
    };

    class RuntimeMonstropolisMapSetService
    {
    public:
        explicit RuntimeMonstropolisMapSetService(RuntimeMonstropolisMapSetConfig config);
        ~RuntimeMonstropolisMapSetService();
        auto install_once() -> void;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
