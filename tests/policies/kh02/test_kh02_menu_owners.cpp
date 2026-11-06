#include "kh3ap/features/kh02/challenges/kh02_challenge_input_router.h"
#include "kh3ap/features/kh02/wardrobe/kh02_wardrobe_edit_transaction.h"
#include <cassert>
#include <stdexcept>
#include <vector>

using namespace kh3ap;
using Json = nlohmann::json;

static void wardrobe_transactions()
{
    Json current{{"color_mode", 0}, {"top", {1, 2, 3}}, {"bottom", {4, 5, 6}}};
    const Json original = current;
    int writes{};
    bool reject_write{};
    Kh02WardrobeEditTransaction edit;
    edit.configure([&] { return current.dump(); },
                   [&](const std::string& value)
                   {
                       if (reject_write)
                       {
                           throw std::runtime_error("write rejected");
                       }
                       current = Json::parse(value);
                       ++writes;
                   });
    edit.begin(current);
    current["color_mode"] = 2;
    edit.capture_editor(current);
    current["top"] = Json::array({7, 8, 9});
    edit.capture_palette(current);
    current["top"] = Json::array({10, 11, 12});
    edit.rollback_palette();
    assert(current["top"] == Json::array({7, 8, 9}));
    edit.rollback_editor(true);
    assert(current["top"] == original["top"] && current["color_mode"] == 2);
    assert(edit.editing());
    edit.cancel();
    assert(current == original && !edit.editing());
    const int after_cancel = writes;
    edit.cancel();
    assert(writes == after_cancel);

    edit.begin(current);
    current["top"] = Json::array({13, 14, 15});
    edit.commit(current);
    edit.cancel();
    assert(current["top"] == Json::array({13, 14, 15}));
    edit.begin(current);
    reject_write = true;
    try
    {
        edit.cancel();
        assert(false);
    }
    catch (const std::runtime_error&)
    {
    }
    assert(edit.editing());
    reject_write = false;
    edit.cancel();
    assert(!edit.editing());

    for (int index = 0; index < 12; ++index)
    {
        edit.remember_single(Json::array({index, 0, 0}));
    }
    assert(edit.single_colors().size() == 10);
    edit.remember_single(Json::array({5, 0, 0}));
    assert(edit.single_colors().size() == 10 && edit.single_colors().front()[0] == 5);
    edit.remember_gradient(Json::array({Json::array({1, 2, 3}), Json::array({4, 5, 6})}));
    edit.finish();
    assert(edit.gradient_colors().size() == 1 && edit.single_colors().size() == 10);
}

static void menu_input_ownership()
{
    using Action = Kh02ChallengeInputRouter::Action;
    Kh02ChallengeInputRouter router;
    Kh02ChallengeInputRouter::Context closed{};
    Kh02ChallengeInputRouter::Context log{true, true, false, false};
    Kh02ChallengeInputRouter::Context wardrobe{true, true, true, false};
    std::vector<std::pair<Action, int>> actions;
    auto dispatch = [&](Action action, int delta)
    {
        actions.emplace_back(action, delta);
        return action == Action::ConfirmWardrobe;
    };
    auto failure = [](const char*) { assert(false); };
    assert(!router.route(0, L"Down", 0, false, 0, closed, dispatch, failure));
    assert(!router.route(0, L"Down", 2, false, 10, log, dispatch, failure));
    assert(!router.route(0, L"Down", 1, false, 20, log, dispatch, failure));
    assert(actions.empty());
    assert(router.route(0, L"Gamepad_DPad_Down", 0, true, 100, log, dispatch, failure));
    assert(actions.size() == 1 && actions.back() == std::make_pair(Action::Vertical, 1));
    assert(router.route(0, L"Gamepad_LeftStick_Down", 0, true, 110, log, dispatch, failure));
    assert(actions.size() == 1);
    assert(router.poll_vertical(449) == 0);
    assert(router.poll_vertical(450) == 1);
    assert(router.vertical_stats().steps == 1);
    router.reset(); // Closing preserves the physical press's release owner.
    assert(router.route(0, L"Gamepad_DPad_Down", 1, true, 460, closed, dispatch, failure));
    assert(router.route(0, L"Gamepad_LeftStick_Down", 1, true, 470, closed, dispatch, failure));
    assert(!router.route(0, L"Gamepad_DPad_Down", 1, true, 480, closed, dispatch, failure));

    actions.clear();
    assert(router.route(0, L"PageDown", 0, false, 500, log, dispatch, failure));
    assert(actions.back() == std::make_pair(Action::Vertical, 8));
    assert(router.poll_vertical(900) == 0); // Page changes do not become held directions.
    router.route(0, L"PageDown", 1, false, 910, log, dispatch, failure);

    actions.clear();
    assert(router.route(0, L"Right", 0, false, 1000, wardrobe, dispatch, failure));
    assert(actions.back() == std::make_pair(Action::Horizontal, 1));
    assert(router.poll_horizontal(1350) == 1);
    assert(router.poll_horizontal(1399) == 0);
    assert(router.poll_horizontal(1400) == 1);
    assert(router.horizontal_stats().steps == 2);
    assert(router.route(0, L"Enter", 0, false, 1410, wardrobe, dispatch, failure));
    assert(actions[actions.size() - 2].first == Action::ConfirmWardrobe);
    assert(actions.back().first == Action::DecideFeedback);
    assert(router.poll_horizontal(1800) == 0);

    int failures{};
    auto throws = [](Action, int) -> bool { throw std::runtime_error("menu failure"); };
    assert(router.route(0, L"Escape", 0, false, 1900, log, throws, [&](const char*) { ++failures; }));
    assert(failures == 1);
    assert(router.route(0, L"Escape", 1, false, 1910, closed, dispatch, failure));
}

int main()
{
    wardrobe_transactions();
    menu_input_ownership();
}
