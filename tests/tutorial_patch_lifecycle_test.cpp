#include "kh3ap/features/tutorials/tutorial_patch_lifecycle.h"

#include <array>
#include <cassert>

int main()
{
    kh3ap::TutorialPatchLifecycle lifecycle{};
    constexpr std::wstring_view context = L"/Game/Levels/tt/tt_01/umap/tt_01_gameflow";
    constexpr std::wstring_view prefix = L"/Game/Levels/tt/tt_01/";

    assert(!lifecycle.applied());
    assert(!lifecycle.should_attempt(false, context, prefix));
    assert(!lifecycle.should_attempt(true, context, L"/Game/Levels/he/"));
    assert(lifecycle.should_attempt(true, context, prefix));

    // Missing functions only suppress duplicate diagnostics. A later lifecycle
    // event must still be able to resolve and patch the newly loaded function.
    assert(lifecycle.note_unavailable());
    assert(!lifecycle.note_unavailable());
    assert(lifecycle.should_attempt(true, context, prefix));
    lifecycle.record_result(true);
    assert(lifecycle.applied());
    assert(!lifecycle.should_attempt(true, context, prefix));

    // A fresh function generation clears both terminal results and diagnostic
    // suppression. Rejected bytecode stays terminal until that reset.
    lifecycle.reset();
    assert(!lifecycle.applied());
    assert(lifecycle.should_attempt(true, context, prefix));
    assert(lifecycle.note_unavailable());
    lifecycle.record_result(false);
    assert(!lifecycle.applied());
    assert(!lifecycle.should_attempt(true, context, prefix));
    lifecycle.reset();
    assert(lifecycle.should_attempt(true, context, prefix));
    assert(lifecycle.note_unavailable());

    // Streamed tutorial variants own independent results and diagnostics.
    std::array<kh3ap::TutorialPatchLifecycle, 2> variants{};
    variants[0].record_result(false);
    assert(!variants[0].should_attempt(true, true));
    assert(variants[1].should_attempt(true, true));
    assert(variants[0].note_unavailable());
    assert(variants[1].note_unavailable());
    variants.fill({});
    assert(variants[0].should_attempt(true, true));
    assert(variants[1].should_attempt(true, true));
    assert(variants[0].note_unavailable());

    // Empty prefixes intentionally match; the callers that require configured
    // nonempty paths/prefixes retain those explicit checks before lookup.
    assert(lifecycle.should_attempt(true, context, {}));
    assert(!lifecycle.should_attempt(true, false));
}
