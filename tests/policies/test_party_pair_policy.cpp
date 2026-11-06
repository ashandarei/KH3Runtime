#include "kh3ap/features/party/party_pair_policy.h"
#include <cassert>

int main()
{
    using Pair = std::array<std::size_t, 2>;
    using kh3ap::runtime_party_physical_choices;
    for (std::size_t member = 0; member <= 25; ++member)
    {
        assert((runtime_party_physical_choices(Pair{0, member}) == Pair{member, 0}));
        assert((runtime_party_physical_choices(Pair{member, 0}) == Pair{member, 0}));
        for (std::size_t second = 1; member && second <= 25; ++second)
            assert((runtime_party_physical_choices(Pair{member, second}) == Pair{member, second}));
    }
}
