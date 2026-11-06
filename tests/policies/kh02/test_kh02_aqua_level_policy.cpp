#include "kh3ap/features/kh02/progress/kh02_aqua_level_policy.h"
#include "kh3ap/features/kh02/progress/kh02_progress_policy.h"
#include <cassert>
#include <climits>

int main()
{
    using namespace kh3ap::kh02_level;
    using namespace kh3ap::kh02_progress;
    // The source table is loaded at runtime. This monotonic fixture includes its
    // first real boundary and cap; intermediate rows test multi-level traversal.
    assert(scale_award(5000, 247, 5) == 101);
    assert(scale_award(25, 5, 247) == 1235);
    assert(scale_award(5000, 247, 247) == 5000);
    assert(scale_award(0, 247, 5) == 0);
    assert(scale_award(1, 947, 5) == 1);
    assert(scale_award(INT_MAX, 1, INT_MAX) == maximum_sora_exp);
    Curve curve{};
    for (size_t index = 0; index < curve.size(); ++index)
    {
        curve[index] = {initial_exp + static_cast<int>(index) * 45000, 43, 46, 39, 35};
    }
    curve[1].experience = 445667;
    curve.back().experience = maximum_exp;
    validate(curve);
    assert(level(curve, initial_exp) == 50);
    assert(level(curve, add_experience(initial_exp, 4074)) == 50);
    assert(level(curve, add_experience(initial_exp, 4075)) == 51);
    assert(level(curve, curve[10].experience) == 60);
    assert(level(curve, add_experience(initial_exp, INT_MAX)) == 99);
    assert(add_experience(maximum_exp, 5000) == maximum_exp);
    bool rejected = false;
    try
    {
        (void)add_experience(initial_exp, -1);
    }
    catch (...)
    {
        rejected = true;
    }
    assert(rejected);

    Snapshot state;
    state.flags = {{"gameflow_DW", 110}, {"gameflow_DW_AP_AQUA_EXP", maximum_exp}};
    const auto frozen = freeze(state);
    assert(*from_json(to_json(frozen)) == *frozen);
    Bank bank;
    assert(bank.update(frozen, bank.revision));
    bank.slots[50] = bank.active;
    state.flags["gameflow_DW_AP_AQUA_EXP"] = initial_exp;
    assert(bank.update(freeze(state), bank.revision));
    bank.slots[51] = bank.active;
    bank.load(50);
    assert(bank.active->flags.at("gameflow_DW_AP_AQUA_EXP") == maximum_exp);
    bank.load(51);
    assert(bank.active->flags.at("gameflow_DW_AP_AQUA_EXP") == initial_exp);
    assert(enter_flags(frozen->flags, 200).at("gameflow_DW_AP_AQUA_EXP") == maximum_exp);
    assert(enter_flags(frozen->flags, 102).at("gameflow_DW_AP_AQUA_EXP") == maximum_exp);
    const auto prior_revision = bank.revision;
    bank.new_game();
    assert(!bank.active);
    assert(!bank.update(frozen, prior_revision));
    for (int invalid : {initial_exp - 1, maximum_exp + 1, -1})
    {
        auto json = to_json(frozen);
        json["flags"]["gameflow_DW_AP_AQUA_EXP"] = invalid;
        rejected = false;
        try
        {
            (void)from_json(json);
        }
        catch (...)
        {
            rejected = true;
        }
        assert(rejected);
    }
}
