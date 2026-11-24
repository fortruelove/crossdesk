#include "crossdesk_ui.h"
#include "ui/model_update.h"

#include <slint-platform.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include "platform/windows/gui/slint_backend.h"

static_assert(crossdesk::PreferSoftwareForWindowsOpenGl(
    "Intel(R) UHD Graphics 630"));
static_assert(crossdesk::PreferSoftwareForWindowsOpenGl("Intel UHD Graphics 770"));
static_assert(crossdesk::PreferSoftwareForWindowsOpenGl("Intel(R) HD Graphics 4600"));
static_assert(crossdesk::PreferSoftwareForWindowsOpenGl("Intel(R) Iris(R) Xe Graphics"));
static_assert(crossdesk::PreferSoftwareForWindowsOpenGl("Intel(R) Graphics"));
static_assert(crossdesk::PreferSoftwareForWindowsOpenGl("Intel(R) Arc(TM) 140V GPU"));
static_assert(crossdesk::PreferSoftwareForWindowsOpenGl("Intel(R) Arc(TM) Graphics 130T"));
static_assert(crossdesk::PreferSoftwareForWindowsOpenGl("Intel(R) Arc(TM) B390 GPU"));
static_assert(!crossdesk::PreferSoftwareForWindowsOpenGl(
    "Intel(R) Iris(R) Xe MAX Graphics"));
static_assert(!crossdesk::PreferSoftwareForWindowsOpenGl("Intel(R) Arc(TM) A770 Graphics"));
static_assert(!crossdesk::PreferSoftwareForWindowsOpenGl("Intel(R) Arc(TM) B580 Graphics"));
static_assert(!crossdesk::PreferSoftwareForWindowsOpenGl("Intel(R) Arc(TM) Graphics"));
static_assert(!crossdesk::PreferSoftwareForWindowsOpenGl(
    "NVIDIA GeForce RTX 4090"));
static_assert(!crossdesk::PreferSoftwareForWindowsOpenGl("AMD Radeon(TM) Graphics"));
#endif

namespace {
// Match the retained software buffer used by the Harmony desktop adapter.
// A full window snapshot would conceal missing damage/redraw notifications.
class TestWindow : public slint::platform::WindowAdapter {
 public:
  slint::platform::SoftwareRenderer software{
      slint::platform::SoftwareRenderer::RepaintBufferType::ReusedBuffer};
  std::vector<slint::Rgb8Pixel> pixels = std::vector<slint::Rgb8Pixel>(1280 * 720);
  bool dirty = true;
  slint::PhysicalSize size() override { return slint::PhysicalSize({1280, 720}); }
  slint::platform::AbstractRenderer& renderer() override { return software; }
  void request_redraw() override { dirty = true; }
  void Render() {
    if (!dirty && !window().has_active_animations()) return;
    dirty = false;
    software.render(pixels, 1280);
  }
};

class TestPlatform : public slint::platform::Platform {
 public:
  TestWindow* adapter = nullptr;
  std::unique_ptr<slint::platform::WindowAdapter> create_window_adapter() override {
    auto result = std::make_unique<TestWindow>();
    adapter = result.get();
    return result;
  }
};
}  // namespace

int main() {
  auto platform = std::make_unique<TestPlatform>();
  auto* backend = platform.get();
  slint::platform::set_platform(std::move(platform));
  auto stream = crossdesk::ui::StreamWindow::create();
  auto& adapter = *backend->adapter;
  stream->window().dispatch_resize_event(slint::LogicalSize({1280, 720}));
  stream->show();
  stream->set_secure_attention_supported(false);
  stream->set_privacy_visible(true);
  auto rows = std::make_shared<slint::VectorModel<crossdesk::ui::NetworkStatsRow>>();
  stream->set_stats_rows(rows);
  std::vector<crossdesk::ui::NetworkStatsRow> stats(4);
  unsigned samples = 0;
  bool stats_visible = false;
  stream->on_toggle_stats([&] { stats_visible = !stats_visible; });
  // Synchronize inside a Slint timer, exactly as GuiApplication does. Updating
  // before update_timers_and_animations would use the previous animation tick.
  slint::Timer sync;
  sync.start(slint::TimerMode::Repeated, std::chrono::milliseconds(16), [&] {
    stream->set_stats_visible(stats_visible);
    // Keep values changing while the panel animates, as during a live session.
    stats[0].inbound = slint::SharedString(std::to_string(++samples) + " kbps");
    crossdesk::ui::UpdateModelRows(*rows, stats);
  });
  const auto tick = [&] {
    slint::platform::update_timers_and_animations();
    adapter.Render();
  };
  const auto settle = [&](int frames = 12) {
    for (int i = 0; i < frames; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
      tick();
    }
  };
  settle();
  const float top = stream->get_video_area_top();
  // The initial control is only the logo. Wait for the toolbar reveal before
  // clicking network status, just as a user does in a new session.
  const slint::LogicalPosition logo({19, top + 19});
  stream->window().dispatch_pointer_move_event(logo);
  stream->window().dispatch_pointer_press_event(logo, slint::PointerEventButton::Left);
  stream->window().dispatch_pointer_release_event(logo, slint::PointerEventButton::Left);
  settle(20);
  for (bool dock_right : {false, true}) {
    if (dock_right) {
      stream->invoke_begin_control_drag(5, 20);
      stream->invoke_move_control_drag(1275, 20);
      stream->invoke_end_control_drag();
      settle();
    }
    // With privacy visible and no Windows shortcuts, the toolbar is 344 px.
    // Its logo moves to the trailing edge when docked on the right.
    const int control_x = dock_right ? int(stream->get_video_area_width()) - 344 : 0;
    const slint::LogicalPosition button({float(control_x + (dock_right ? 214 : 250)), top + 19});
    stream->window().dispatch_pointer_move_event(button);
    settle(35); // Include the tooltip that appears before the click.
    for (bool expanded : {true, false, true, false}) {
      stream->window().dispatch_pointer_press_event(button, slint::PointerEventButton::Left);
      stream->window().dispatch_pointer_release_event(button, slint::PointerEventButton::Left);
      settle(); // No pointer movement: animation must finish on its own.
      if (stats_visible != expanded) {
        std::cerr << "Network status button did not toggle the panel; dock_right="
                  << dock_right << '\n';
        return 1;
      }
      for (int y : {65, 195}) {
        const auto pixel = adapter.pixels[(int(top) + y) * 1280 + control_x + 4];
        const bool painted = pixel.r >= 240 && pixel.g >= 240 && pixel.b >= 240;
        const bool cleared = pixel.r == 0 && pixel.g == 0 && pixel.b == 0;
        if (expanded ? !painted : !cleared) {
          std::cerr << "Stats must fully " << (expanded ? "expand" : "collapse")
                    << " without pointer movement; dock_right=" << dock_right
                    << ", y=" << y << ", pixel=" << int(pixel.r) << ','
                    << int(pixel.g) << ',' << int(pixel.b) << '\n';
          return 1;
        }
        if (stream->invoke_is_local_control_area(control_x + 4, y) != expanded) {
          std::cerr << "Stats input area must match the rendered panel\n";
          return 1;
        }
      }
    }
  }
  stream->set_custom_titlebar(true);
  auto displays = std::make_shared<slint::VectorModel<slint::SharedString>>(
      std::vector<slint::SharedString>{"Display 1 (OMEN 34c)", "Display 2 (SQ17TH)"});
  stream->set_displays(displays);
  int switched_to = -1;
  stream->on_switch_display([&](int display) { switched_to = display; });
  const auto click = [&](slint::LogicalPosition position) {
    stream->window().dispatch_pointer_move_event(position);
    stream->window().dispatch_pointer_press_event(position, slint::PointerEventButton::Left);
    stream->window().dispatch_pointer_release_event(position, slint::PointerEventButton::Left);
    tick();
  };
  const slint::LogicalPosition second_display({300, 16});
  const slint::LogicalPosition status({490, 16});
  const slint::LogicalPosition outside({900, 350});
  click(second_display);
  if (switched_to != 1 || stream->get_selected_display() != 1) {
    std::cerr << "Clicking a title-bar display must switch monitors\n";
    return 1;
  }
  stream->window().dispatch_pointer_move_event(status);
  tick();
  if (!stream->get_title_stats_visible() || !stream->invoke_is_local_control_area(460, 40)) {
    std::cerr << "Hovering over connection status must show the title-bar panel\n";
    return 1;
  }
  stream->window().dispatch_pointer_move_event(outside);
  tick();
  if (stream->get_title_stats_visible()) {
    std::cerr << "The unpinned title-bar panel must close on pointer exit\n";
    return 1;
  }
  click(status);
  stream->window().dispatch_pointer_move_event(outside);
  tick();
  if (!stream->get_title_stats_visible() || !stream->get_local_input_active()) {
    std::cerr << "Clicking connection status must pin the title-bar panel\n";
    return 1;
  }
  click(slint::LogicalPosition({465, 72}));
  if (!stream->get_title_stats_visible()) {
    std::cerr << "Clicking inside the panel must keep it pinned\n";
    return 1;
  }
  click(outside);
  if (stream->get_title_stats_visible() || stream->get_local_input_active()) {
    std::cerr << "Clicking outside must dismiss the title-bar panel\n";
    return 1;
  }
  click(status);
  click(status);
  if (stream->get_title_stats_visible() || stream->get_local_input_active()) {
    std::cerr << "Clicking connection status again must hide the panel without reopening on hover\n";
    return 1;
  }
  stream->window().dispatch_pointer_move_event(outside);
  stream->window().dispatch_pointer_move_event(status);
  tick();
  if (!stream->get_title_stats_visible()) {
    std::cerr << "Moving back to connection status must show the preview again\n";
    return 1;
  }
  click(status);
  stream->set_fullscreen_enabled(true);
  tick();
  if (stream->get_title_stats_visible() || stream->get_local_input_active()) {
    std::cerr << "Going fullscreen must dismiss the windowed title-bar panel\n";
    return 1;
  }
  std::cout << "Stream stats rendering and title-bar controls passed.\n";
}
