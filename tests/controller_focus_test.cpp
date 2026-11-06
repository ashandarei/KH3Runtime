#include "kh3ap/core/input/controller_focus.h"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
  using Focus = kh3ap::RuntimeControllerFocus;
  using Route = Focus::Route;
  Focus input;
  const Focus::Key square{0, L"Gamepad_FaceButton_Left"};
  const Focus::Key stick{0, L"Gamepad_LeftX"};
  assert(input.button(square, 0, true) == Route::forward);
  assert(input.axis(stick, 0.8F, true) == 0.8F);
  std::vector<Focus::Key> released, neutral;
  input.lose_focus([&](auto k) { released.push_back(k); },
                   [&](auto k) { neutral.push_back(k); });
  assert(released == std::vector<Focus::Key>{square});
  assert(neutral == std::vector<Focus::Key>{stick});
  // Repeats and duplicate presses may not activate a newly focused menu.
  assert(input.button(square, 2, false) == Route::consume);
  assert(input.button(square, 0, true) == Route::consume);
  assert(input.button(square, 1, true) == Route::consume);
  assert(input.button(square, 0, true) == Route::forward);
  assert(input.button(square, 1, true) == Route::release);
  // A background-only press never receives a gameplay release.
  assert(input.button(square, 0, false) == Route::consume);
  assert(input.button(square, 1, false) == Route::consume);
  assert(input.axis(stick, -1.0F, false) == 0.0F);
  // If an event arrives before LostFocus, release the forwarded press once.
  assert(input.button(square, 0, true) == Route::forward);
  assert(input.button(square, 2, false) == Route::release);
  released.clear(); neutral.clear();
  input.lose_focus([&](auto k) { released.push_back(k); },
                   [&](auto k) { neutral.push_back(k); });
  assert(released.empty() && neutral.empty());
  assert(input.button({1, square.second}, 0, true) == Route::forward);
  assert(input.button(square, 2, true) == Route::consume);
  assert(input.button({1, square.second}, 1, true) == Route::release);
  // Disabling the guard permits background buttons/axes and does not emit
  // synthetic releases on focus loss. Re-enabling neutralizes held input.
  Focus toggle;
  assert(toggle.button(square, 0, false) == Route::consume);
  assert(toggle.button(square, 0, false, false) == Route::forward);
  assert(toggle.axis(stick, 0.75F, false, false) == 0.75F);
  released.clear(); neutral.clear();
  toggle.lose_focus([&](auto k) { released.push_back(k); },
                    [&](auto k) { neutral.push_back(k); }, false);
  assert(released.empty() && neutral.empty());
  assert(toggle.button(square, 2, false, false) == Route::forward);
  toggle.lose_focus([&](auto k) { released.push_back(k); },
                    [&](auto k) { neutral.push_back(k); }, true);
  assert(released == std::vector<Focus::Key>{square});
  assert(neutral == std::vector<Focus::Key>{stick});
  assert(toggle.button(square, 2, false, true) == Route::consume);
  assert(toggle.axis(stick, 0.75F, false, true) == 0.0F);
  assert(toggle.button(square, 1, false, true) == Route::consume);
  assert(toggle.button(square, 0, true, true) == Route::forward);
  assert(toggle.button(square, 1, true, true) == Route::release);
  std::cout << "Controller focus tests passed\n";
}
