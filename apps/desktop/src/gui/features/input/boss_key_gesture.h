#pragma once

#include <cstdint>
#include <optional>

namespace crossdesk {

class BossKeyGesture {
 public:
  void Configure(int first, int second, uint32_t tap_interval) {
    first_ = first;
    second_ = second;
    tap_interval_ = tap_interval;
    Reset();
  }

  void Reset() {
    first_down_ = second_down_ = false;
    first_tap_.reset();
  }

  bool Input(int key, bool down, uint32_t time) {
    if (!first_ || !second_) return false;
    if (first_ == second_) {
      if (key != first_) {
        if (down) first_tap_.reset();
        return false;
      }
      const bool pressed = down && !first_down_;
      first_down_ = down;
      if (!pressed) return false;
      if (first_tap_ && time - *first_tap_ <= tap_interval_) {
        first_tap_.reset();
        return true;
      }
      first_tap_ = time;
      return false;
    }
    if (key != first_ && key != second_) return false;
    bool& key_down = key == first_ ? first_down_ : second_down_;
    const bool pressed = down && !key_down;
    key_down = down;
    return pressed && first_down_ && second_down_;
  }

 private:
  int first_ = 0;
  int second_ = 0;
  uint32_t tap_interval_ = 0;
  bool first_down_ = false;
  bool second_down_ = false;
  std::optional<uint32_t> first_tap_;
};

}  // namespace crossdesk
