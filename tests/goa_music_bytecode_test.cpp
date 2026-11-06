#include "kh3ap/features/music/shuffle/goa_music_bytecode.h"
#include <cassert>
#include <vector>

int main()
{
    constexpr uintptr_t garden_sound = 0x123450;
    constexpr uintptr_t unrelated_sound = 0x456780;
    std::vector<uint8_t> script;
    const auto add = [&](uintptr_t object) {
        script.push_back(0x20);
        const auto offset = script.size();
        script.resize(offset + sizeof(object));
        std::memcpy(script.data() + offset, &object, sizeof(object));
    };
    add(unrelated_sound);
    add(garden_sound);
    add(garden_sound);
    const auto known = [](uintptr_t object) { return object == garden_sound; };
    assert(kh3ap::silence_goa_music_constants(script, known) == 2);
    uintptr_t remaining{};
    std::memcpy(&remaining, script.data() + 1, sizeof(remaining));
    assert(remaining == unrelated_sound);
    for (size_t offset : {1 + sizeof(uintptr_t), 2 * (1 + sizeof(uintptr_t))})
    {
        std::memcpy(&remaining, script.data() + offset + 1, sizeof(remaining));
        assert(remaining == 0); // Suppressed even with no replacement asset available.
    }
    const auto once = script;
    assert(kh3ap::silence_goa_music_constants(script, known) == 0 && script == once);
    std::vector<uint8_t> truncated{0x20, 0x11};
    assert(kh3ap::silence_goa_music_constants(truncated, known) == 0);
}
