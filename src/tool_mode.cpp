// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "tool_mode.h"

#include <string>

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(fm4_install_only, false, "Forza Motorsport 4",
                    "Install --fm4_disc2_packages and --fm4_dlc_packages, then quit");
REXCVAR_DEFINE_STRING(fm4_write_graphics_schema, "", "Forza Motorsport 4",
                      "Write the GRAPHICS rows as JSON to this file, then quit");
REXCVAR_DEFINE_STRING(fm4_dump_disc_shaders, "", "Forza Motorsport 4",
                      "Write every shader on the disc to this folder, then quit");
REXCVAR_DEFINE_STRING(fm4_build_shader_cache, "", "Forza Motorsport 4",
                      "Add the shipped shader list's shaders and pipelines from the disc to this "
                      "shareable shader cache folder, then quit");
REXCVAR_DEFINE_STRING(fm4_shader_list_dir, "", "Forza Motorsport 4",
                      "Folder of the shipped shader list for --fm4_build_shader_cache");
REXCVAR_DEFINE_STRING(fm4_export_ui_fonts, "", "Forza Motorsport 4",
                      "Write the disc's UI fonts, decompressed, to this folder, then quit");
REXCVAR_DEFINE_BOOL(fm4_wheel_probe, false, "Forza Motorsport 4",
                    "Write the connected controllers and what the racing wheel bindings read "
                    "from them to stdout as JSON lines until stdin closes (the launcher's WHEEL "
                    "panel), then quit");

namespace fm4_static_recomp {

ToolMode CurrentToolMode() {
  if (REXCVAR_GET(fm4_wheel_probe)) {
    return ToolMode::kWheelProbe;
  }
  if (!std::string(REXCVAR_GET(fm4_write_graphics_schema)).empty()) {
    return ToolMode::kSchema;
  }
  if (!std::string(REXCVAR_GET(fm4_export_ui_fonts)).empty()) {
    return ToolMode::kUiFonts;
  }
  if (!std::string(REXCVAR_GET(fm4_dump_disc_shaders)).empty() ||
      !std::string(REXCVAR_GET(fm4_build_shader_cache)).empty()) {
    return ToolMode::kShaderCache;
  }
  return REXCVAR_GET(fm4_install_only) ? ToolMode::kInstall : ToolMode::kNone;
}

}  // namespace fm4_static_recomp
