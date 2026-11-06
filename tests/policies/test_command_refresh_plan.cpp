#include "kh3ap/features/grants/command_refresh_plan.h"
#include <cassert>
#include <vector>

int main()
{
    using namespace kh3ap;
    int reads = 0;
    const auto counts =
        read_command_bonus_counts<6>(24, 31,
                                     [&](uint8_t first, std::span<uint8_t> output)
                                     {
                                         ++reads;
                                         assert(first == 24 && output.size() == 6);
                                         const std::array<uint8_t, 6> actual{0, 1, 2, 255, 0, 3};
                                         std::copy(actual.begin(), actual.end(), output.begin());
                                         return true;
                                     });
    assert(reads == 1 && counts && (*counts)[3] == 255 && (*counts)[5] == 3);
    assert(!read_command_bonus_counts<6>(24, 31,
                                         [](uint8_t, std::span<uint8_t> output)
                                         {
                                             output[0] = 2;
                                             return false;
                                         }));
    const auto invalid = [](uint8_t, std::span<uint8_t>)
    {
        assert(false);
        return true;
    };
    assert(!read_command_bonus_counts<6>(0, 31, invalid));
    assert(!read_command_bonus_counts<6>(28, 31, invalid));
    assert(!read_command_bonus_counts<6>(32, 31, invalid));

    int queries = 0, preparations = 0;
    std::array<bool, 5> owned{};
    std::vector<size_t> applied;
    auto query = [&](size_t index, bool& value)
    {
        ++queries;
        value = owned[index];
        return true;
    };
    auto prepare = [&]()
    {
        ++preparations;
        return true;
    };
    auto apply = [&](size_t index)
    {
        applied.push_back(index);
        return true;
    };
    assert(refresh_owned_commands<5>(query, prepare, apply));
    assert(queries == 5 && preparations == 0 && applied.empty());
    owned = {true, false, true, false, true};
    assert(refresh_owned_commands<5>(query, prepare, apply));
    assert(preparations == 1 && (applied == std::vector<size_t>{0, 2, 4}));
    applied.clear();
    assert(!refresh_owned_commands<5>(
        [](size_t, bool&)
        {
            return false;
        },
        prepare, apply));
    assert(preparations == 1 && applied.empty());
    assert(!refresh_owned_commands<5>(
        query,
        []
        {
            return false;
        },
        apply));
    assert(applied.empty());
    assert(!refresh_owned_commands<5>(query, prepare,
                                      [&](size_t index)
                                      {
                                          applied.push_back(index);
                                          return false;
                                      }));
    assert((applied == std::vector<size_t>{0}));
    applied.clear();
    owned = {false, true, false, false, false};
    assert(refresh_owned_commands<5>(query, prepare, apply));
    assert((applied == std::vector<size_t>{1})); // New event reads ownership again.
}
