#include "kh3ap/features/ui/options_menu/callback_state.h"
#include <cassert>
#include <stdexcept>
#include <vector>

int main()
{
    using namespace kh3ap;
    using namespace kh3ap::options_menu_detail;
    std::vector<std::string> reports;
    CallbackState state{[&](CallbackKind, std::wstring_view, std::string_view error)
    {
        reports.emplace_back(error);
    }};
    RuntimeOptionsItemDefinition item;
    item.label = L"Example";
    item.choices = {{L"on", L"On"}, {L"off", L"Off"}};
    // Existing definitions may intentionally omit a reader and start at zero.
    assert(state.selection(item) == 0 && reports.empty());
    item.selected_choice = []() -> size_t { throw std::runtime_error("read_failed"); };
    assert(!state.selection(item));
    assert(!state.selection(item));
    assert(reports == std::vector<std::string>{"read_failed"});
    item.selected_choice = [] { return size_t{1}; };
    assert(state.selection(item) == 1);
    item.selected_choice = []() -> size_t { throw std::runtime_error("read_failed"); };
    assert(!state.selection(item) && reports.size() == 2);
    // Invalid indices do not masquerade as the last valid choice.
    item.selected_choice = [] { return size_t{2}; };
    assert(!state.selection(item));
    assert(reports.back() == "selected_choice_out_of_range");
    const auto report_count = reports.size();
    assert(!state.selection(item) && reports.size() == report_count);

    assert(state.read<bool>(&item, item.label, CallbackKind::ItemAvailability, [] { return false; }) == false);
    assert(reports.size() == report_count); // Ordinary unavailability is not a callback error.
    auto action = [] { throw std::runtime_error("write_failed"); };
    assert(!state.run(&item, item.label, CallbackKind::Apply, action));
    assert(!state.run(&item, item.label, CallbackKind::Apply, action));
    assert(reports.size() == report_count + 1);
    assert(state.run(&item, item.label, CallbackKind::Apply, [] {}));
    assert(!state.run(&item, item.label, CallbackKind::Apply, action));
    assert(reports.size() == report_count + 2);
    assert(!state.read<int>(&item, item.label, CallbackKind::Value, []() -> int { throw 7; }));
    assert(reports.back() == "unknown_exception");
    CallbackState broken_reporter{[](CallbackKind, std::wstring_view, std::string_view) { throw 7; }};
    assert(!broken_reporter.selection(item));
}
