#include "kh3ap/features/ui/map_overlay/map_gfx_value_layout.h"
#include <array>
#include <cassert>
#include <cmath>
#include <limits>

using namespace kh3ap;
int main() {
    // Nonzero reflected offsets, negative integer values, and both native
    // number widths are supported without a packed C++ ASValue assumption.
    MapGfxValueLayout layout{48,3,8,16,8,32};
    std::array<std::byte,50> storage{};
    storage.front()=storage.back()=std::byte{0x55};
    auto* value=storage.data()+1;
    assert(layout.write_number(value,42.75));
    assert(layout.read_type(value)==ASValueType::Number && layout.read_number(value)==42.75);
    const int32_t negative=-12345;
    layout.write_type(value,ASValueType::Int);
    std::memcpy(value+8,&negative,sizeof(negative));
    assert(layout.read_number(value)==negative);
    layout.number_size=4;
    assert(layout.write_number(value,-0.5) && layout.read_number(value)==-0.5);
    assert(layout.write_number(value,std::numeric_limits<double>::quiet_NaN()));
    assert(std::isnan(*layout.read_number(value))); // caller's projection rejects NaN

    int object{};
    assert(layout.write_object(value,&object));
    assert(layout.read_object(value)==&object && !layout.read_number(value));
    layout.write_type(value,ASValueType::DisplayObject);
    assert(layout.read_object(value)==&object);
    for (auto kind:{ASValueType::Undefined,ASValueType::Null,ASValueType::Boolean,
                    ASValueType::String,ASValueType::StringW}) {
        layout.write_type(value,kind);
        assert(!layout.read_number(value) && !layout.read_object(value));
    }
    assert(!layout.write_object(value,nullptr));
    assert(!layout.read_type(nullptr) && !layout.read_number(nullptr) && !layout.read_object(nullptr));
    assert(!layout.write_number(nullptr,1) && !layout.write_object(nullptr,&object));
    assert(storage.front()==std::byte{0x55} && storage.back()==std::byte{0x55});

    // Malformed reflected fields must fail closed before touching the buffer.
    const auto before=storage;
    layout.type=48;
    assert(!layout.write_number(value,1) && !layout.write_object(value,&object));
    assert(!layout.read_type(value));
    layout.type=3;
    layout.number=46;
    assert(!layout.write_number(value,1));
    layout.write_type(value,ASValueType::Number);
    assert(!layout.read_number(value));
    layout.object=-1;
    assert(!layout.write_object(value,&object));
    layout.write_type(value,ASValueType::Object);
    assert(!layout.read_object(value));
    layout.number=16; layout.number_size=3;
    assert(!layout.write_number(value,1));
    layout.size=-1;
    assert(!layout.write_type(value,ASValueType::Int) && !layout.write_object(value,&object));
    assert(storage.front()==before.front() && storage.back()==before.back());
}
