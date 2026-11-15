// Copyright (c) 2026, FugaziScript
// BSD 3-Clause License: see LICENSE in the repository root.
#include "wheel/wheel_probe.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <rex/cvar.h>

#include "wheel/wheel_devices.h"
#include "wheel/wheel_mapping.h"
#include "wheel/wheel_settings.h"

namespace fm4_static_recomp::wheel {

namespace {

void AppendString(std::string& out, std::string_view text) {
  out += '"';
  for (const char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char escaped[8];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
          out += escaped;
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

template <typename T>
void AppendArray(std::string& out, const std::vector<T>& values) {
  out += '[';
  for (size_t i = 0; i < values.size(); ++i) {
    out += (i ? "," : "") + std::to_string(int(values[i]));
  }
  out += ']';
}

std::string Describe(const Snapshot& s, const Composer& composer) {
  std::string out = "{\"ready\":";
  out += s.ready ? "true" : "false";
  out += ",\"status\":";
  AppendString(out, s.haptic_status);
  out += ",\"ffb_device\":";
  AppendString(out, s.ffb_name);
  out += ",\"test_busy\":";
  out += s.test_busy ? "true" : "false";
  const Device* base = composer.Base(s);
  out += ",\"base\":";
  AppendString(out, base ? DeviceMatch(*base, s.devices) : "");
  out += ",\"devices\":[";
  for (size_t i = 0; i < s.devices.size(); ++i) {
    const Device& d = s.devices[i];
    out += i ? ",{" : "{";
    out += "\"match\":";
    AppendString(out, DeviceMatch(d, s.devices));
    out += ",\"name\":";
    AppendString(out, d.name);
    out += ",\"path\":";
    AppendString(out, d.path);
    char ids[16];
    std::snprintf(ids, sizeof(ids), "%04x:%04x", d.vendor, d.product);
    out += ",\"ids\":";
    AppendString(out, ids);
    out += ",\"wheel\":";
    out += d.wheel ? "true" : "false";
    out += ",\"axes\":";
    AppendArray(out, d.axes);
    out += ",\"rest\":";
    AppendArray(out, d.rest);
    out += ",\"buttons\":";
    std::vector<int> buttons(d.buttons.begin(), d.buttons.end());
    AppendArray(out, buttons);
    out += ",\"hats\":";
    AppendArray(out, d.hats);
    out += '}';
  }
  const auto pad = composer.Compose(s);
  out += "],\"pad\":{\"steering\":" + std::to_string(int16_t(pad.thumb_lx));
  out += ",\"throttle\":" + std::to_string(int(pad.right_trigger));
  out += ",\"brake\":" + std::to_string(int(pad.left_trigger));
  out += ",\"clutch\":" + std::to_string(int(composer.Pedal(s, 2)));
  out += ",\"buttons\":" + std::to_string(int(uint16_t(pad.buttons)));
  out += ",\"gear\":" + std::to_string(composer.shifter() ? int(int16_t(pad.thumb_rx)) : 0);
  // Also how the launcher tells a build that has the probe (WheelProbe.Supported).
  out += "},\"probe\":\"fm4-wheel-probe/1\"}\n";
  return out;
}

}  // namespace

int RunWheelProbe() {
  // Shared with the command thread, which may outlive this function in getline.
  struct State {
    // Opened with the saved settings; the launcher's unsaved values arrive as `set` lines.
    DeviceMonitor monitor{ReadSettings(), true};
    std::mutex mutex;
    std::shared_ptr<const Composer> composer = std::make_shared<Composer>(ReadSettings());
    std::atomic<bool> quit{false};
  };
  auto state = std::make_shared<State>();
  std::thread([state] {
    std::string line;
    while (!state->quit && std::getline(std::cin, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line == "quit") {
        break;
      }
      if (line == "test") {
        state->monitor.TestForce(ReadSettings());
      } else if (line == "testrumble") {
        state->monitor.TestForce(ReadSettings(), true);
      } else if (line.rfind("set ", 0) == 0) {
        const size_t space = line.find(' ', 4);
        const std::string name = "fm4_wheel_" + line.substr(4, space - 4);
        const std::string value = space == std::string::npos ? "" : line.substr(space + 1);
        rex::cvar::SetFlagByName(name, value);
        auto composer = std::make_shared<const Composer>(ReadSettings());
        std::lock_guard lock(state->mutex);
        state->composer = std::move(composer);
      }
    }
    state->quit = true;
  }).detach();
  while (!state->quit) {
    std::shared_ptr<const Composer> composer;
    {
      std::lock_guard lock(state->mutex);
      composer = state->composer;
    }
    const std::string line = Describe(state->monitor.Read(), *composer);
    if (std::fwrite(line.data(), 1, line.size(), stdout) != line.size() || std::fflush(stdout)) {
      state->quit = true;  // the launcher has gone
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(33));
  }
  return 0;
}

}  // namespace fm4_static_recomp::wheel
