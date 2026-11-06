#include "kh3ap/features/moogle_shop/shop_movie_patch.h"
#include "kh3ap/features/moogle_shop/shop_movie_patch_data.h"
#include "kh3ap/features/journal/ingredient_journal_movie_data.h"
#include <cassert>
#include <fstream>
#include <iterator>

using namespace kh3ap;

int main(int argc, char** argv)
{
    const std::vector<uint8_t> original{'a','b','c','d','e','f'};
    const std::vector<uint8_t> expected{'d','e','f','!','a','b','c'};
    const std::vector<uint8_t> delta{0,3,0,0,0,3,0,0,0, 1,1,0,0,0,'!', 0,0,0,0,0,3,0,0,0};
    const auto apply = [&](std::span<const uint8_t> source, std::span<const uint8_t> patch,
                           std::vector<uint8_t>& out) {
        return decode_shop_movie_patch(source, patch, 6, shop_movie_fingerprint(original),
                                       7, shop_movie_fingerprint(expected), out);
    };
    std::vector<uint8_t> out;
    assert(apply(original, delta, out) && out == expected);
    for (size_t length = 0; length < delta.size(); ++length)
    {
        out = {42};
        assert(!apply(original, std::span{delta}.first(length), out));
        assert(out == std::vector<uint8_t>{42});
    }
    for (size_t index = 0; index < delta.size(); ++index)
    {
        auto broken = delta;
        broken[index] ^= 0xff;
        out = {42};
        assert(!apply(original, broken, out));
        assert(out == std::vector<uint8_t>{42});
    }
    auto wrong_movie = original;
    wrong_movie[0] = 'z';
    assert(!apply(wrong_movie, delta, out));
    assert(!apply(std::span{original}.first(5), delta, out));
    assert(!decode_shop_movie_patch(original, delta, 6, shop_movie_fingerprint(original),
                                   UINT32_MAX, 0, out));

    // Optional local integration fixture; the vanilla/modified movies are not
    // committed. Proves the production decoder exactly applies the audited
    // original-helper/constructor splices while sourcing game code at runtime.
    if (argc == 3 || argc == 4)
    {
        const auto read = [](const char* path) {
            std::ifstream stream(path, std::ios::binary);
            assert(stream.good());
            return std::vector<uint8_t>{std::istreambuf_iterator<char>{stream}, {}};
        };
        namespace p = shop_movie_patch;
        const auto vanilla = read(argv[1]), modified = read(argv[2]);
        if (argc == 4) {
            namespace ingredients = ingredient_journal_movie;
            assert(decode_shop_movie_patch(vanilla, ingredients::delta_bytes(), ingredients::original_size,
                ingredients::original_hash, ingredients::patched_size, ingredients::patched_hash, out));
        } else {
            assert(decode_shop_movie_patch(vanilla, p::delta_bytes(), p::original_size, p::original_hash,
                                          p::patched_size, p::patched_hash, out));
        }
        assert(out == modified);
    }
}
