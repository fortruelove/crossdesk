#pragma once

#include <windows.h>

#include <algorithm>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gui/features/input/boss_key_gesture.h"

namespace crossdesk {

class WindowsBossKey {
 public:
  struct Capture {
    bool gallery;
    int slot;
    int key;
  };

  static WindowsBossKey& Instance() {
    static WindowsBossKey boss_key;
    return boss_key;
  }

  ~WindowsBossKey() {
    if (hook_thread_.joinable()) {
      PostThreadMessageW(hook_thread_id_, WM_QUIT, 0, 0);
      hook_thread_.join();
    }
  }

  bool Configure(int first, int second) {
    if (first && second && !EnsureHook()) return false;
    std::lock_guard lock(mutex_);
    gui_thread_id_ = GetCurrentThreadId();
    gesture_.Configure(first, second, GetDoubleClickTime());
    return true;
  }

  bool BeginCapture(HWND window, bool gallery, int slot) {
    if (!window || !EnsureHook()) return false;
    std::lock_guard lock(mutex_);
    gui_thread_id_ = GetCurrentThreadId();
    gesture_.Reset();
    capture_ = Capture{gallery, slot, 0};
    capture_window_ = window;
    captured_.reset();
    return true;
  }

  void CancelCapture() {
    std::lock_guard lock(mutex_);
    capture_.reset();
    captured_.reset();
  }

  std::optional<Capture> TakeCapture() {
    std::lock_guard lock(mutex_);
    return std::exchange(captured_, std::nullopt);
  }

  bool TakeToggle() {
    std::lock_guard lock(mutex_);
    return std::exchange(toggle_, false);
  }

  // The remote keyboard capture hook can run before the global hook.
  // Repeated notifications for the same press cannot trigger the gesture twice.
  bool HandleKey(WPARAM message, const KBDLLHOOKSTRUCT& event, HWND foreground) {
    if ((event.flags & LLKHF_INJECTED) || event.vkCode < VK_BACK || event.vkCode > 0xfe)
      return false;
    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    if (!down && message != WM_KEYUP && message != WM_SYSKEYUP) return false;

    const int key = static_cast<int>(event.vkCode);
    std::lock_guard lock(mutex_);
    if (key == consumed_key_) {
      if (!down) {
        consumed_key_ = 0;
        gesture_.Input(key, false, event.time);
      }
      return true;
    }
    if (!hidden_ && (!foreground || GetWindowThreadProcessId(foreground, nullptr) != gui_thread_id_)) {
      gesture_.Reset();
      return false;
    }
    if (capture_ && foreground == capture_window_) {
      if (down) {
        capture_->key = key;
        captured_ = std::exchange(capture_, std::nullopt);
        consumed_key_ = key;
      }
      return down;
    }
    if (!gesture_.Input(key, down, event.time)) return false;
    toggle_ = true;
    consumed_key_ = key;
    return true;
  }

  bool hidden() const { return !hidden_windows_.empty(); }

  void Hide() {
    if (!hidden()) {
      const auto window = GetForegroundWindow();
      if (window && GetWindowThreadProcessId(window, nullptr) == GetCurrentThreadId())
        foreground_ = window;
    }
    // Only hide the GUI thread's windows. Host-side privacy windows can live
    // in this process too and must stay visible.
    EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
      auto& windows = *reinterpret_cast<std::vector<HWND>*>(data);
      if (IsWindowVisible(window)) {
        if (std::find(windows.begin(), windows.end(), window) == windows.end())
          windows.push_back(window);
        ShowWindow(window, SW_HIDE);
      }
      return TRUE;
    }, reinterpret_cast<LPARAM>(&hidden_windows_));
    std::lock_guard lock(mutex_);
    hidden_ = hidden();
  }

  void Restore() {
    const auto gui_thread_id = GetCurrentThreadId();
    for (const auto window : hidden_windows_) {
      if (GetWindowThreadProcessId(window, nullptr) == gui_thread_id) ShowWindow(window, SW_SHOWNA);
    }
    if (foreground_ && GetWindowThreadProcessId(foreground_, nullptr) == gui_thread_id)
      SetForegroundWindow(foreground_);
    hidden_windows_.clear();
    foreground_ = nullptr;
    std::lock_guard lock(mutex_);
    hidden_ = false;
    gesture_.Reset();
  }

  static std::string KeyName(int key) {
    if (!key) return {};
    const auto scan = MapVirtualKeyW(key, MAPVK_VK_TO_VSC_EX);
    const auto name_param = ((scan & 0xff) << 16) | ((scan & 0xff00) ? 1 << 24 : 0);
    wchar_t name[64]{};
    const int length = GetKeyNameTextW(name_param, name, 64);
    char utf8[192]{};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, name, length,
                                        utf8, sizeof(utf8), nullptr, nullptr);
    return bytes > 0 ? std::string(utf8, bytes) : "VK " + std::to_string(key);
  }

 private:
  static LRESULT CALLBACK Hook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && data && Instance().HandleKey(
        message, *reinterpret_cast<const KBDLLHOOKSTRUCT*>(data), GetForegroundWindow()))
      return 1;
    return CallNextHookEx(nullptr, code, message, data);
  }

  bool EnsureHook() {
    if (hook_thread_.joinable()) return true;
    std::promise<DWORD> started;
    auto result = started.get_future();
    hook_thread_ = std::thread([started = std::move(started)]() mutable {
      MSG message;
      PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
      const auto hook = SetWindowsHookExW(WH_KEYBOARD_LL, Hook, GetModuleHandleW(nullptr), 0);
      started.set_value(hook ? GetCurrentThreadId() : 0);
      if (!hook) return;
      while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      UnhookWindowsHookEx(hook);
    });
    hook_thread_id_ = result.get();
    if (!hook_thread_id_) hook_thread_.join();
    return hook_thread_id_ != 0;
  }

  std::mutex mutex_;
  BossKeyGesture gesture_;
  std::thread hook_thread_;
  DWORD hook_thread_id_ = 0;
  DWORD gui_thread_id_ = 0;
  std::optional<Capture> capture_;
  std::optional<Capture> captured_;
  HWND capture_window_ = nullptr;
  int consumed_key_ = 0;
  bool toggle_ = false;
  bool hidden_ = false;
  HWND foreground_ = nullptr;
  std::vector<HWND> hidden_windows_;
};

}  // namespace crossdesk
