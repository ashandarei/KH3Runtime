#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace kh3ap {
// Game-thread mailbox. Hold events through loading; overflow requests one
// catch-up scan, never a recurring scan. Tokens must carry object generations.
template<class Token, size_t Capacity = 1024, class Equal = std::equal_to<Token>>
class ChestDiscoveryQueue {
public:
    struct Batch { bool scan{}; std::vector<Token> actors; };
    void request_scan() { scan_ = true; }
    void notify(Token token) {
        if (scan_ || std::find_if(actors_.begin(), actors_.end(), [&](const auto& other) {return Equal{}(other,token);}) != actors_.end()) return;
        if (actors_.size() == Capacity) { actors_.clear(); request_scan(); return; }
        actors_.push_back(std::move(token));
    }
    bool pending() const { return scan_ || !actors_.empty(); }
    Batch take() { Batch result{std::exchange(scan_, false), {}}; result.actors.swap(actors_); return result; }
private:
    bool scan_{true};
    std::vector<Token> actors_;
};
}
