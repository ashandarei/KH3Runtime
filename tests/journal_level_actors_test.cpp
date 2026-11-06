#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct PropertyClass
{
    std::wstring name{L"ObjectProperty"};
    auto GetName() const -> const std::wstring& { return name; }
};

struct Property
{
    int offset{};
    int size{sizeof(void*)};
    PropertyClass type;
    auto GetClass() const -> const PropertyClass& { return type; }
    auto GetOffset_Internal() const -> int { return offset; }
    auto GetElementSize() const -> int { return size; }
};

struct UObject
{
    bool valid{true};
    UObject* outer{};
    std::map<std::wstring, Property> properties;
    std::map<std::wstring, UObject*> values;
    auto GetOuterPrivate() const -> UObject* { return outer; }
    auto GetPropertyByNameInChain(const wchar_t* name) -> Property*
    {
        const auto found = properties.find(name);
        return found == properties.end() ? nullptr : &found->second;
    }
};

auto is_uobject_valid_fast(UObject* object) -> bool { return object && object->valid; }
auto try_read_reflected_object_property_value(UObject* object, const wchar_t* name) -> UObject*
{
    return object->values[name];
}

// Only these fixture address ranges may be read. Invalid arrays must be rejected
// before trying to read actor pointers, just as with passive runtime reads.
std::map<uint64_t, std::vector<uint8_t>> readable_memory;
auto try_read_process_memory_bytes(uint64_t address, size_t size)
    -> std::optional<std::vector<uint8_t>>
{
    const auto found = readable_memory.find(address);
    if (found == readable_memory.end() || found->second.size() != size)
    {
        return {};
    }
    return found->second;
}
template <class T> auto store(uint64_t address, const T& value) -> void
{
    std::vector<uint8_t> bytes(sizeof(T));
    std::memcpy(bytes.data(), &value, bytes.size());
    readable_memory[address] = std::move(bytes);
}
template <class T> auto memory(uint64_t address) -> std::optional<T>
{
    const auto bytes = try_read_process_memory_bytes(address, sizeof(T));
    if (!bytes)
    {
        return {};
    }
    T value{};
    std::memcpy(&value, bytes->data(), sizeof(T));
    return value;
}

struct Journal
{
#include "journal_level_actors_remaining_under_test.h"
};

auto main() -> int
{
    UObject package_world, gameplay_world, level, settings, food_box;
    level.outer = &package_world;
    settings.outer = &level;
    food_box.outer = &level;
    level.properties[L"OwningWorld"].offset = 0xd8;
    level.properties[L"WorldSettings"].offset = 0x310;
    level.values[L"WorldSettings"] = &settings;
    level.values[L"OwningWorld"] = &package_world;
    struct Header
    {
        uint64_t data;
        int32_t count, capacity;
    };
    constexpr uint64_t actor_data = 0x1000;
    const auto header_address = reinterpret_cast<uint64_t>(&level) + 0xb8;
    const UObject* actor_array[]{&settings, &food_box};
    store(header_address, Header{actor_data, 2, 2});
    store(actor_data, actor_array);
    Journal journal;
    const auto inactive = journal.authored_level_actors(&level, &package_world);
    assert(inactive && inactive->size() == 2 && inactive->back() == &food_box);

    // Streaming changes OwningWorld, while the level's package outer stays put.
    // The journal must still accept the same source and the same actor array.
    level.values[L"OwningWorld"] = &gameplay_world;
    const auto streamed = journal.authored_level_actors(&level, &package_world);
    assert(streamed && *streamed == *inactive);
    assert(!journal.authored_level_actors(nullptr, &package_world));
    assert(!journal.authored_level_actors(&level, nullptr));
    level.valid = false;
    assert(!journal.authored_level_actors(&level, &package_world));
    level.valid = true;
    package_world.valid = false;
    assert(!journal.authored_level_actors(&level, &package_world));
    package_world.valid = true;

    // A gameplay owner is not the package identity used to resolve this level.
    assert(!journal.authored_level_actors(&level, &gameplay_world));
    level.outer = &gameplay_world;
    level.values[L"OwningWorld"] = &package_world;
    assert(!journal.authored_level_actors(&level, &package_world));
    level.outer = &package_world;

    level.properties[L"OwningWorld"].offset = 0xe0;
    assert(!journal.authored_level_actors(&level, &package_world));
    level.properties[L"OwningWorld"].offset = 0xd8;
    level.properties[L"WorldSettings"].size = 4;
    assert(!journal.authored_level_actors(&level, &package_world));
    level.properties[L"WorldSettings"].size = sizeof(void*);

    settings.valid = false;
    assert(!journal.authored_level_actors(&level, &package_world));
    settings.valid = true;
    level.values[L"WorldSettings"] = &food_box;
    assert(!journal.authored_level_actors(&level, &package_world));
    level.values[L"WorldSettings"] = &settings;
    store(header_address, Header{actor_data, 3, 2});
    assert(!journal.authored_level_actors(&level, &package_world));
    store(header_address, Header{actor_data, 2, 65537});
    assert(!journal.authored_level_actors(&level, &package_world));
    store(header_address, Header{actor_data, 2, 2});
    readable_memory.erase(actor_data);
    assert(!journal.authored_level_actors(&level, &package_world));
}
