/*
 * @Author: DI JUNKUN
 * @Date: 2026-09-07
 * Copyright (c) 2026 by DI JUNKUN, All Rights Reserved.
 */

#ifndef _SLINT_BACKEND_H_
#define _SLINT_BACKEND_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace crossdesk {

struct SlintBackendSelection {
  bool success = true;
  std::string backend;
  std::string diagnostic;
};

inline constexpr char kSlintRendererProbeArgument[] = "--slint-renderer-probe";
inline constexpr char kSlintSoftwareRendererArgument[] = "--software-render";
inline constexpr int kSlintRendererProbePreferSoftware = 3;

constexpr bool PreferSoftwareForWindowsOpenGl(std::string_view renderer) {
  const auto has_prefix = [&renderer](std::string_view prefix) {
    return renderer.substr(0, prefix.size()) == prefix;
  };
  constexpr std::string_view intel = "Intel ";
  constexpr std::string_view intel_registered = "Intel(R) ";
  if (has_prefix(intel_registered))
    renderer.remove_prefix(intel_registered.size());
  else if (has_prefix(intel))
    renderer.remove_prefix(intel.size());
  else
    return false;

  if (has_prefix("HD Graphics") || has_prefix("UHD Graphics") ||
      renderer == "Graphics" || has_prefix("Graphics "))
    return true;
  if (has_prefix("Iris ") || has_prefix("Iris("))
    return renderer.find("MAX") == std::string_view::npos;

  // Arc is used for both integrated and discrete GPUs. Only explicit
  // processor graphics model names identify an integrated Arc renderer.
  constexpr std::string_view arc = "Arc ";
  constexpr std::string_view arc_trademark = "Arc(TM) ";
  constexpr std::string_view graphics = "Graphics ";
  if (has_prefix(arc_trademark))
    renderer.remove_prefix(arc_trademark.size());
  else if (has_prefix(arc))
    renderer.remove_prefix(arc.size());
  else
    return false;
  if (has_prefix(graphics)) renderer.remove_prefix(graphics.size());
  const auto model = renderer.substr(0, renderer.find(' '));
  return model == "130T" || model == "140T" || model == "130V" ||
         model == "140V" || model == "B370" || model == "B390";
}

// Call before creating any Slint components. The executable's main() must
// dispatch kSlintRendererProbeArgument before services, settings or the daemon.
SlintBackendSelection ConfigureWindowsSlintBackend(
    uint32_t probe_timeout_ms = 10000);

// Internal child-process entry point; never call in the main application.
int RunSlintRendererProbe(bool require_software = false);

}  // namespace crossdesk

#endif
