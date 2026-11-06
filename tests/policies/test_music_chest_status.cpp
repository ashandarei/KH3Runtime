#include "kh3ap/features/treasure/music_chest/music_chest_status.h"
#include <cassert>
#include <map>

int main() {
    using namespace std::chrono;
    kh3ap::RuntimeMusicChestStatus status;
    const auto start = kh3ap::RuntimeMusicChestStatus::Clock::now();
    const std::filesystem::path first{"seed-one/status.json"}, second{"seed-two/status.json"};
    std::map<std::filesystem::path, std::string> disk;
    int writes{}, existence_checks{};
    bool fail_write{};
    auto exists = [&](const auto& path) {++existence_checks; return disk.contains(path);};
    auto write = [&](const auto& path, std::string_view text) {
        ++writes;
        if (fail_write) return false;
        disk[path] = text;
        return true;
    };
    auto publish = [&](const auto& path, std::string_view text, int seconds_after) {
        return status.publish(path, text, start + seconds(seconds_after), exists, write);
    };

    assert(publish(first, "closed", 0));
    for (int i = 1; i <= 60; ++i) assert(publish(first, "closed", i));
    assert(writes == 1 && existence_checks == 12); // No repeated status writes.

    assert(publish(first, "opening", 61));
    assert(publish(first, "opened", 62));
    assert(writes == 3 && disk[first] == "opened"); // Changes bypass the timer.

    disk.erase(first);
    assert(publish(first, "opened", 63));
    assert(writes == 3);
    assert(publish(first, "opened", 67));
    assert(writes == 4 && disk[first] == "opened"); // Deleted sidecar recovers.

    fail_write = true;
    assert(!publish(first, "save failed", 68));
    assert(!publish(first, "save failed", 69));
    assert(writes == 6 && disk[first] == "opened");
    fail_write = false;
    assert(publish(first, "save failed", 70));
    assert(writes == 7 && disk[first] == "save failed");

    assert(publish(second, "save failed", 71));
    assert(writes == 8 && disk[second] == "save failed"); // Same text, new seed.
    status.reset();
    assert(publish(second, "save failed", 72));
    assert(writes == 9); // Map/session reset must republish.

    disk.erase(second);
    fail_write = true;
    assert(!publish(second, "save failed", 77));
    fail_write = false;
    assert(publish(second, "save failed", 78));
    assert(writes == 11 && disk[second] == "save failed");
}
