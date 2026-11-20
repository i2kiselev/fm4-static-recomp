// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace fm4_static_recomp {

enum class DefaultStage { kBeforeGpuPlugin, kAfterGpuPlugin };

// The values run.ps1 used to pass, set only while a setting still has its
// compiled-in value, so a launch option or fm4_static_recomp.toml wins. GPU settings
// exist only once the plugin is loaded, hence the two stages.
void ApplyBuiltInDefaults(DefaultStage stage);

// The value ApplyBuiltInDefaults gives `name`, if it gives one.
std::optional<std::string_view> BuiltInDefault(std::string_view name);

// The same for settings that depend on the GPU, once the device is chosen and before the
// game starts.
void ApplyGpuVendorDefaults(uint32_t vendor_id);

}  // namespace fm4_static_recomp
