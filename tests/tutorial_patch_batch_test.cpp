#include "kh3ap/features/tutorials/tutorial_patch_batch.h"

#include <cassert>

namespace
{
    using Result = kh3ap::RuntimeTutorialPopupBypassResult;

    auto first_patch(uint8_t* script, size_t size) noexcept -> Result
    {
        assert(size == 3 && script[0] == 0);
        script[0] = 1;
        return Result::Patched;
    }

    auto rejected_patch(uint8_t* script, size_t size) noexcept -> Result
    {
        assert(size == 3 && script[0] == 1 && script[1] == 0);
        script[1] = 1;
        return Result::Rejected;
    }

    auto last_patch(uint8_t* script, size_t size) noexcept -> Result
    {
        assert(size == 3 && script[1] == 1 && script[2] == 0);
        script[2] = 1;
        return Result::AlreadyPatched;
    }
}

int main()
{
    using namespace kh3ap::tutorial_patch;

    uint8_t script[3]{};
    constexpr std::array<TutorialPatcher, 3> patchers{
            first_patch, rejected_patch, last_patch};
    const auto results = apply_tutorial_patch_batch(script, sizeof(script), patchers);
    assert(script[0] == 1 && script[1] == 1 && script[2] == 1);
    assert((results == std::array{Result::Patched, Result::Rejected, Result::AlreadyPatched}));
    assert(combine_tutorial_patch_results(results) == Result::Rejected);
    assert(combine_tutorial_patch_results(std::array{Result::Rejected, Result::Patched}) ==
           Result::Rejected);
    assert(combine_tutorial_patch_results(std::array{Result::AlreadyPatched, Result::Patched}) ==
           Result::Patched);
    assert(combine_tutorial_patch_results(std::array{Result::Patched, Result::AlreadyPatched}) ==
           Result::Patched);
    assert(combine_tutorial_patch_results(std::array{Result::AlreadyPatched, Result::AlreadyPatched}) ==
           Result::AlreadyPatched);
}
