#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kh3ap
{
    class IngredientCheckpointWriter
    {
    public:
        struct Checkpoint
        {
            uint64_t scope{}, revision{};
            std::filesystem::path path;
            std::string payload;
        };
        struct Completion
        {
            std::shared_ptr<const Checkpoint> checkpoint;
            bool committed{};
            uint64_t commit_us{};
        };
        using Write = std::function<bool(const std::filesystem::path&, std::string_view)>;
        explicit IngredientCheckpointWriter(Write write);
        ~IngredientCheckpointWriter();
        auto submit(Checkpoint checkpoint) -> bool;
        auto take_completed() -> std::vector<Completion>;
        // Drain accepted writes before the bridge transport shuts down.
        void stop();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace kh3ap
