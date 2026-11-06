#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace RC::Unreal
{
    class UObject;
}

namespace kh3ap
{
    class Kh02StoryFlagBindings
    {
      public:
        enum class Operation
        {
            integer,
            boolean,
            set_label,
            set_boolean,
            equal,
            greater_equal,
            less
        };

        struct Request
        {
            Operation operation;
            // Operands are borrowed only during the synchronous native callback.
            const std::wstring& key;
            const std::wstring& normalized_key;
            const std::wstring& label;
            int boolean{};
            std::wstring source_node;
            bool result_requested{};
        };

        struct Callbacks
        {
            std::function<void(RC::Unreal::UObject*)> ensure_world;
            std::function<int(const Request&)> evaluate;
        };

        explicit Kh02StoryFlagBindings(Callbacks callbacks);
        ~Kh02StoryFlagBindings();
        Kh02StoryFlagBindings(const Kh02StoryFlagBindings&) = delete;
        auto operator=(const Kh02StoryFlagBindings&) -> Kh02StoryFlagBindings& = delete;
        void install();
        void note_read();
        void note_write();
        void reset_observations();
        auto snapshot() const -> nlohmann::json;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
} // namespace kh3ap
