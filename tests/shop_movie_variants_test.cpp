#include "kh3ap/features/moogle_shop/shop_movie_patch_variants.h"
#include <cassert>
#include <fstream>
#include <iterator>
using namespace kh3ap;
int main(int argc,char** argv) {
    assert(load_embedded_movie_resource(L"KH3AP_MISSING_MOVIE_RESOURCE",1).empty());
    assert(load_embedded_movie_resource(L"KH3AP_SHOP_PATCH_EGS",shop_movie_patch::delta_size+1).empty());
    assert(shop_movie_patch::delta_bytes().size()==shop_movie_patch::delta_size);
    assert(shop_movie_patch_steam::delta_bytes().size()==shop_movie_patch_steam::delta_size);
    assert(shop_movie_patch::delta_bytes().data()==shop_movie_patch::delta_bytes().data());
    std::vector<uint8_t> out{42};
    assert(decode_supported_shop_movie({},out).empty());
    assert(out==std::vector<uint8_t>{42});
    for(auto size:{shop_movie_patch::original_size,shop_movie_patch_steam::original_size}) {
        std::vector<uint8_t> unknown(size,0);
        assert(decode_supported_shop_movie(unknown,out).empty());
        assert(out==std::vector<uint8_t>{42});
    }
    if(argc==5) {
        const auto read=[](const char* path) {
            std::ifstream stream(path,std::ios::binary);assert(stream.good());
            return std::vector<uint8_t>{std::istreambuf_iterator<char>{stream},{}};
        };
        for(int i:{1,3}) {
            auto original=read(argv[i]);const auto expected=read(argv[i+1]);
            assert(decode_supported_shop_movie(original,out)==(i==1?"egs":"steam"));
            assert(out==expected);
            original[original.size()/2]^=1;
            assert(decode_supported_shop_movie(original,out).empty());
            assert(out==expected); // Reject changed assets without touching output.
        }
    }
}
