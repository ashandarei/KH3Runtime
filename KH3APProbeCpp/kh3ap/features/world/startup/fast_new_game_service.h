#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <Unreal/FWeakObjectPtr.hpp>
namespace kh3ap {
class RuntimeFastNewGameService {
public:
    explicit RuntimeFastNewGameService(std::function<std::optional<std::filesystem::path>()> bridge)
        : bridge_(std::move(bridge)) {}
    void tick(bool initialized, bool title_active);
    static int dispatched_difficulty() { return dispatched_difficulty_; }
private:
    inline static int dispatched_difficulty_{-1};
    std::function<std::optional<std::filesystem::path>()> bridge_;
    std::chrono::steady_clock::time_point next_poll_{};
    std::chrono::steady_clock::time_point next_setup_{}, next_title_search_{};
    RC::Unreal::FWeakObjectPtr title_{};
    std::optional<std::string> content_;
    std::optional<std::filesystem::path> last_bridge_;
    std::string failed_config_, last_evidence_;
    bool new_game_dispatched_{}, movie_dismissed_{}, existing_saves_{};
};
}
