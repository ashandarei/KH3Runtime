#include <cassert>
#include <string_view>

struct UObject
{
    bool valid{true};
    UObject* root{};
    UObject* helper{};
};

struct WeakObject
{
    UObject* object{};
    auto Get() const -> UObject* { return object && object->valid ? object : nullptr; }
};

auto is_uobject_valid_fast(UObject* object) -> bool { return object && object->valid; }

auto try_read_reflected_object_property_value(UObject* movie, std::wstring_view name) -> UObject*
{
    assert(is_uobject_valid_fast(movie));
    assert(name == L"Root");
    return movie->root;
}

auto try_gfx_invoke_string_arg_object(int, UObject* root, std::wstring_view method,
                                     std::wstring_view name) -> UObject*
{
    assert(is_uobject_valid_fast(root));
    assert(method == L"getChildByName");
    assert(name == L"kh3apIngredients");
    return root->helper;
}

struct Journal
{
    struct Config
    {
        int gfx_paths{};
    } config;
    WeakObject root, overlay_movie;
    bool movie_ready{true};

    // Exercise the actual resolver used by publication, input and focus cleanup.
#include "journal_overlay_remaining_under_test.h"
};

int main()
{
    UObject home_helper;
    UObject home_root{true, nullptr, &home_helper};
    UObject overlay_helper;
    UObject overlay_root{true, nullptr, &overlay_helper};
    UObject movie{true, &overlay_root};
    Journal journal;
    journal.root.object = &home_root;

    // Cold first open: the patched home is ready, but icon loads have delayed
    // creation of the dedicated movie. Never publish into the home helper.
    assert(journal.helper() == nullptr);

    // Once loading finishes, every operation resolves the dedicated instance.
    journal.overlay_movie.object = &movie;
    assert(journal.helper() == &overlay_helper);

    // Native home focus can go away on entry, then return on Back/reopen.
    // Cleanup must keep addressing the same overlay throughout those changes.
    journal.root.object = nullptr;
    assert(journal.helper() == &overlay_helper);
    journal.root.object = &home_root;
    assert(journal.helper() == &overlay_helper);

    // Missing/stale overlay state must never redirect input or cleanup home.
    movie.valid = false;
    assert(journal.helper() == nullptr);
    movie.valid = true;
    movie.root = nullptr;
    assert(journal.helper() == nullptr);
    movie.root = &overlay_root;
    overlay_root.valid = false;
    assert(journal.helper() == nullptr);
    overlay_root.valid = true;
    overlay_root.helper = nullptr;
    assert(journal.helper() == nullptr);
    overlay_root.helper = &overlay_helper;
    journal.movie_ready = false;
    assert(journal.helper() == nullptr);
    journal.movie_ready = true;
    assert(journal.helper() == &overlay_helper);
}
