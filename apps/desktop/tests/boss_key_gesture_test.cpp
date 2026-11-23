#include "gui/features/input/boss_key_gesture.h"

#include <cstdlib>
#include <iostream>

#ifdef _WIN32
#include "platform/windows/gui/boss_key.h"
#endif

namespace {
void Expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}
}  // namespace

int main() {
  using crossdesk::BossKeyGesture;
  BossKeyGesture gesture;
  gesture.Configure('A', 'B', 500);
  Expect(!gesture.Input('A', true, 10), "The first key alone cannot trigger");
  Expect(gesture.Input('B', true, 20), "The chord must trigger when both keys are held");
  Expect(!gesture.Input('B', true, 30), "Auto-repeat cannot trigger again");
  gesture.Input('A', false, 40);
  gesture.Input('B', false, 50);
  gesture.Input('B', true, 60);
  Expect(gesture.Input('A', true, 70), "Either key may be pressed first");

  gesture.Configure('A', 'A', 500);
  Expect(!gesture.Input('A', true, 1000), "A single tap cannot trigger a double tap");
  Expect(!gesture.Input('A', true, 1100), "Holding a key cannot produce a double tap");
  gesture.Input('A', false, 1200);
  Expect(gesture.Input('A', true, 1300), "A second distinct press must trigger");
  gesture.Input('A', false, 1310);
  Expect(!gesture.Input('A', true, 1400), "One tap cannot belong to two gestures");
  gesture.Input('A', false, 1410);
  Expect(!gesture.Input('A', true, 2000), "An expired first tap cannot trigger");
  gesture.Input('A', false, 2010);
  gesture.Input('B', true, 2020);
  Expect(!gesture.Input('A', true, 2030), "An intervening key cancels the double tap");

  gesture.Configure(0, 'A', 500);
  Expect(!gesture.Input('A', true, 3000), "Incomplete settings must disable the shortcut");

#ifdef _WIN32
  auto& boss_key = crossdesk::WindowsBossKey::Instance();
  const HWND window = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                     HWND_MESSAGE, nullptr, nullptr, nullptr);
  Expect(window != nullptr, "The settings test window must open");
  Expect(boss_key.BeginCapture(window, false, 2), "Windows must enable key capture");
  KBDLLHOOKSTRUCT key{VK_ESCAPE};
  Expect(!boss_key.HandleKey(WM_KEYDOWN, key, nullptr), "Capture must stay in the settings window");
  Expect(boss_key.HandleKey(WM_KEYDOWN, key, window), "Escape must be consumed while capturing");
  const auto escape = boss_key.TakeCapture();
  Expect(escape && escape->slot == 2 && escape->key == VK_ESCAPE,
         "The settings UI must receive Escape for the selected field");
  boss_key.HandleKey(WM_KEYUP, key, window);
  Expect(boss_key.BeginCapture(window, true, 1), "A second capture must be possible");
  key.vkCode = VK_F9;
  Expect(boss_key.HandleKey(WM_KEYDOWN, key, window), "The first key must be captured");
  key.vkCode = VK_F10;
  Expect(!boss_key.HandleKey(WM_KEYDOWN, key, window), "Capture must stop after the first key");
  const auto first = boss_key.TakeCapture();
  Expect(first && first->gallery && first->slot == 1 && first->key == VK_F9,
         "The result must identify the first key and its settings window");
  DestroyWindow(window);
#endif
  std::cout << "Boss key gesture and capture checks passed.\n";
}
