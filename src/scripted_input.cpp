// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
#include "scripted_input.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <rex/cvar.h>
#include <rex/input/device_assignment.h>
#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_bind.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/presenter.h>

namespace fm4_static_recomp {
namespace {

using rex::X_RESULT;
using rex::X_STATUS;

constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x464D3453);  // FM4S
constexpr rex::input::DeviceId kKeyboard = static_cast<rex::input::DeviceId>(0x464D344B);  // FM4K

struct Step {
  // Times are relative to the anchor-th firing of the event named anchor, or
  // to process start when anchor is empty.
  std::string anchor;
  size_t anchor_index = 0;
  double start = 0.0;
  double end = 0.0;
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;
  // SET:<option>=<value> tokens, applied once when the step starts.
  std::vector<std::string> sets;
  // KEY:<name> tokens: keys the scripted keyboard holds.
  std::vector<uint16_t> keys;
};

std::mutex g_mutex;
std::string g_path;
std::filesystem::file_time_type g_loaded_time;
std::chrono::steady_clock::time_point g_next_reload_check;
std::vector<Step> g_steps;
uint32_t g_last_packet = 0;
const auto g_start = std::chrono::steady_clock::now();
// Seconds since process start of each firing of each input event.
std::unordered_map<std::string, std::vector<double>> g_events;
bool g_recording = false;
// The SET tokens already applied, as "<start> <token>", so a reload keeps them done.
std::unordered_set<std::string> g_sets_applied;

double Now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
}

int16_t Axis(double value) {
  return int16_t(std::clamp(value, -1.0, 1.0) * 32767.0);
}

bool ParseControl(const std::string& token, Step& step) {
  static const std::unordered_map<std::string, uint16_t> buttons = {
      {"UP", rex::input::X_INPUT_GAMEPAD_DPAD_UP},
      {"DOWN", rex::input::X_INPUT_GAMEPAD_DPAD_DOWN},
      {"LEFT", rex::input::X_INPUT_GAMEPAD_DPAD_LEFT},
      {"RIGHT", rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT},
      {"START", rex::input::X_INPUT_GAMEPAD_START},
      {"BACK", rex::input::X_INPUT_GAMEPAD_BACK},
      {"LS", rex::input::X_INPUT_GAMEPAD_LEFT_THUMB},
      {"RS", rex::input::X_INPUT_GAMEPAD_RIGHT_THUMB},
      {"LB", rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER},
      {"RB", rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER},
      {"A", rex::input::X_INPUT_GAMEPAD_A},
      {"B", rex::input::X_INPUT_GAMEPAD_B},
      {"X", rex::input::X_INPUT_GAMEPAD_X},
      {"Y", rex::input::X_INPUT_GAMEPAD_Y},
  };
  if (token.starts_with("KEY:")) {
    const rex::ui::VirtualKey key = rex::ui::ParseVirtualKey(token.substr(4));
    if (key == rex::ui::VirtualKey::kNone) {
      return false;
    }
    step.keys.push_back(static_cast<uint16_t>(key));
    return true;
  }
  if (token.starts_with("SET:") && token.find('=') != std::string::npos) {
    step.sets.push_back(token.substr(4));
    return true;
  }
  if (auto button = buttons.find(token); button != buttons.end()) {
    step.buttons |= button->second;
    return true;
  }
  const size_t equals = token.find('=');
  if (equals == std::string::npos) {
    return false;
  }
  const std::string name = token.substr(0, equals);
  const double value = std::strtod(token.c_str() + equals + 1, nullptr);
  if (name == "LT") {
    step.left_trigger = uint8_t(std::clamp(value, 0.0, 1.0) * 255.0);
  } else if (name == "RT") {
    step.right_trigger = uint8_t(std::clamp(value, 0.0, 1.0) * 255.0);
  } else if (name == "LX") {
    step.thumb_lx = Axis(value);
  } else if (name == "LY") {
    step.thumb_ly = Axis(value);
  } else if (name == "RX") {
    step.thumb_rx = Axis(value);
  } else if (name == "RY") {
    step.thumb_ry = Axis(value);
  } else {
    return false;
  }
  return true;
}

bool LoadScript(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    REXLOG_ERROR("Scripted input: cannot open {}", path);
    return false;
  }
  g_steps.clear();
  std::string line;
  std::string anchor;
  std::unordered_map<std::string, size_t> anchor_counts;
  size_t anchor_index = 0;
  while (std::getline(file, line)) {
    if (const size_t comment = line.find('#'); comment != std::string::npos) {
      line.resize(comment);
    }
    if (line.starts_with('@')) {
      std::istringstream name(line.substr(1));
      anchor.clear();
      name >> anchor;
      anchor_index = anchor_counts[anchor]++;
      continue;
    }
    std::istringstream tokens(line);
    Step step;
    step.anchor = anchor;
    step.anchor_index = anchor_index;
    double duration = 0.0;
    if (!(tokens >> step.start >> duration)) {
      continue;
    }
    step.end = step.start + duration;
    std::string token;
    while (tokens >> token) {
      if (!ParseControl(token, step)) {
        REXLOG_ERROR("Scripted input: unknown control '{}' in {}", token, path);
        return false;
      }
    }
    g_steps.push_back(step);
  }
  REXLOG_INFO("Scripted input: {} steps from {}", g_steps.size(), path);
  return true;
}

// The script may be extended while the game runs, to drive it interactively.
void ReloadIfChanged() {
  const auto now = std::chrono::steady_clock::now();
  if (now < g_next_reload_check) {
    return;
  }
  g_next_reload_check = now + std::chrono::milliseconds(100);
  std::error_code error;
  const auto written = std::filesystem::last_write_time(g_path, error);
  if (!error && written != g_loaded_time) {
    g_loaded_time = written;
    LoadScript(g_path);
  }
}

class ScriptedInputDriver final : public rex::input::InputDriver {
 public:
  ScriptedInputDriver() : InputDriver(nullptr, 0) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo info;
    info.id = kDevice;
    info.name = "FM4 scripted gamepad";
    out.push_back(std::move(info));
  }
  X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out) override {
    if (id != kDevice) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    if (out) {
      *out = {};
      std::lock_guard lock(g_mutex);
      ReloadIfChanged();
      const double now = Now();
      Step active;
      uint32_t packet = 0;
      for (size_t i = 0; i < g_steps.size(); ++i) {
        const Step& step = g_steps[i];
        double base = 0.0;
        if (!step.anchor.empty()) {
          const auto fired = g_events.find(step.anchor);
          if (fired == g_events.end() || fired->second.size() <= step.anchor_index) {
            continue;
          }
          base = fired->second[step.anchor_index];
        }
        if (now < base + step.start || now >= base + step.end) {
          continue;
        }
        for (const std::string& set : step.sets) {
          if (g_sets_applied.insert(std::format("{} {}", step.start, set)).second) {
            const size_t equals = set.find('=');
            rex::cvar::SetFlagByName(set.substr(0, equals), set.substr(equals + 1));
            REXLOG_INFO("Scripted input: set {}", set);
          }
        }
        if (!step.keys.empty()) {
          continue;
        }
        packet = packet * 31 + uint32_t(i + 1);
        active.buttons |= step.buttons;
        active.left_trigger = std::max(active.left_trigger, step.left_trigger);
        active.right_trigger = std::max(active.right_trigger, step.right_trigger);
        if (step.thumb_lx) active.thumb_lx = step.thumb_lx;
        if (step.thumb_ly) active.thumb_ly = step.thumb_ly;
        if (step.thumb_rx) active.thumb_rx = step.thumb_rx;
        if (step.thumb_ry) active.thumb_ry = step.thumb_ry;
      }
      if (packet != g_last_packet) {
        g_last_packet = packet;
        REXLOG_INFO("Scripted input: t={:.2f} buttons={:04X} lx={} rt={}", now, active.buttons,
                    active.thumb_lx, active.right_trigger);
      }
      out->packet_number = packet;
      out->gamepad.buttons = active.buttons;
      out->gamepad.left_trigger = active.left_trigger;
      out->gamepad.right_trigger = active.right_trigger;
      out->gamepad.thumb_lx = active.thumb_lx;
      out->gamepad.thumb_ly = active.thumb_ly;
      out->gamepad.thumb_rx = active.thumb_rx;
      out->gamepad.thumb_ry = active.thumb_ry;
    }
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                 rex::input::X_INPUT_CAPABILITIES* out) override {
    if (id != kDevice) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    if (out) {
      *out = {};
      out->type = 1;
      out->sub_type = 1;
      out->gamepad.buttons = UINT16_MAX;
      out->gamepad.left_trigger = UINT8_MAX;
      out->gamepad.right_trigger = UINT8_MAX;
      out->gamepad.thumb_lx = INT16_MAX;
      out->gamepad.thumb_ly = INT16_MAX;
      out->gamepad.thumb_rx = INT16_MAX;
      out->gamepad.thumb_ry = INT16_MAX;
    }
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetDeviceVibration(rex::input::DeviceId id, rex::input::X_INPUT_VIBRATION*) override {
    return id == kDevice ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }
  X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t,
                              rex::input::X_INPUT_KEYSTROKE*) override {
    return id == kDevice ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
  }
};

// The keys of the script's KEY: steps as the keyboard driver turns them into a
// pad, through the bindings FM4 hands it (keybind overrides).
class ScriptedKeyboardDriver final : public rex::input::InputDriver {
 public:
  ScriptedKeyboardDriver() : InputDriver(nullptr, 0) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  bool is_keyboard_and_mouse() const override { return true; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo info;
    info.id = kKeyboard;
    info.name = "FM4 scripted keyboard";
    info.synthetic = true;
    out.push_back(std::move(info));
  }
  X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out) override {
    if (id != kKeyboard) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    if (!out) {
      return X_ERROR_SUCCESS;
    }
    *out = {};
    bool key_down[256] = {};
    uint32_t packet = 0;
    {
      std::lock_guard lock(g_mutex);
      ReloadIfChanged();
      const double now = Now();
      for (size_t i = 0; i < g_steps.size(); ++i) {
        const Step& step = g_steps[i];
        double base = 0.0;
        if (!step.anchor.empty()) {
          const auto fired = g_events.find(step.anchor);
          if (fired == g_events.end() || fired->second.size() <= step.anchor_index) {
            continue;
          }
          base = fired->second[step.anchor_index];
        }
        if (step.keys.empty() || now < base + step.start || now >= base + step.end) {
          continue;
        }
        packet = packet * 31 + uint32_t(i + 1);
        for (uint16_t key : step.keys) {
          key_down[key] = true;
        }
      }
    }
    const auto binds = rex::input::mnk::KeybindOverrides();
    if (!binds) {
      return X_ERROR_SUCCESS;
    }
    bool keys[256];
    held_keys_.Apply(binds, key_down, keys);
    struct Target {
      const char* option;
      uint16_t button;
      int8_t* axis8;
      int16_t* axis16;
      int sign;
    };
    int8_t lt = 0, rt = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
    const Target targets[] = {
        {"keybind_a", rex::input::X_INPUT_GAMEPAD_A, nullptr, nullptr, 0},
        {"keybind_b", rex::input::X_INPUT_GAMEPAD_B, nullptr, nullptr, 0},
        {"keybind_x", rex::input::X_INPUT_GAMEPAD_X, nullptr, nullptr, 0},
        {"keybind_y", rex::input::X_INPUT_GAMEPAD_Y, nullptr, nullptr, 0},
        {"keybind_left_shoulder", rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER, nullptr, nullptr, 0},
        {"keybind_right_shoulder", rex::input::X_INPUT_GAMEPAD_RIGHT_SHOULDER, nullptr, nullptr, 0},
        {"keybind_lstick_press", rex::input::X_INPUT_GAMEPAD_LEFT_THUMB, nullptr, nullptr, 0},
        {"keybind_rstick_press", rex::input::X_INPUT_GAMEPAD_RIGHT_THUMB, nullptr, nullptr, 0},
        {"keybind_back", rex::input::X_INPUT_GAMEPAD_BACK, nullptr, nullptr, 0},
        {"keybind_start", rex::input::X_INPUT_GAMEPAD_START, nullptr, nullptr, 0},
        {"keybind_dpad_up", rex::input::X_INPUT_GAMEPAD_DPAD_UP, nullptr, nullptr, 0},
        {"keybind_dpad_down", rex::input::X_INPUT_GAMEPAD_DPAD_DOWN, nullptr, nullptr, 0},
        {"keybind_dpad_left", rex::input::X_INPUT_GAMEPAD_DPAD_LEFT, nullptr, nullptr, 0},
        {"keybind_dpad_right", rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT, nullptr, nullptr, 0},
        {"keybind_left_trigger", 0, &lt, nullptr, 1},
        {"keybind_right_trigger", 0, &rt, nullptr, 1},
        {"keybind_lstick_left", 0, nullptr, &lx, -1},
        {"keybind_lstick_right", 0, nullptr, &lx, 1},
        {"keybind_lstick_up", 0, nullptr, &ly, 1},
        {"keybind_lstick_down", 0, nullptr, &ly, -1},
        {"keybind_rstick_left", 0, nullptr, &rx, -1},
        {"keybind_rstick_right", 0, nullptr, &rx, 1},
        {"keybind_rstick_up", 0, nullptr, &ry, 1},
        {"keybind_rstick_down", 0, nullptr, &ry, -1},
    };
    uint16_t buttons = 0;
    for (const Target& target : targets) {
      const auto bind = binds->find(std::string_view(target.option));
      if (bind == binds->end() || !rex::input::mnk::BindPressed(keys, bind->second, false)) {
        continue;
      }
      buttons |= target.button;
      if (target.axis8) *target.axis8 = 1;
      if (target.axis16) *target.axis16 = int16_t(*target.axis16 + target.sign * 32767);
    }
    out->packet_number = packet;
    out->gamepad.buttons = buttons;
    out->gamepad.left_trigger = lt ? 255 : 0;
    out->gamepad.right_trigger = rt ? 255 : 0;
    out->gamepad.thumb_lx = lx;
    out->gamepad.thumb_ly = ly;
    out->gamepad.thumb_rx = rx;
    out->gamepad.thumb_ry = ry;
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                 rex::input::X_INPUT_CAPABILITIES* out) override {
    if (id != kKeyboard) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    if (out) {
      *out = {};
      out->type = 1;
      out->sub_type = 1;
    }
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetDeviceVibration(rex::input::DeviceId id, rex::input::X_INPUT_VIBRATION*) override {
    return id == kKeyboard ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }
  X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t,
                              rex::input::X_INPUT_KEYSTROKE*) override {
    return id == kKeyboard ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
  }

 private:
  rex::input::mnk::HeldKeyMask held_keys_;
};

std::unique_ptr<rex::system::IInputSystem> CreateInputSystem(bool) {
  auto input = std::make_unique<rex::input::InputSystem>(nullptr);
  input->AddDriver(std::make_unique<ScriptedInputDriver>());
  input->AddDriver(std::make_unique<ScriptedKeyboardDriver>());
  input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());
  return input;
}

bool WriteBmp(const std::string& path, const rex::ui::RawImage& image) {
  const uint32_t row_bytes = (image.width * 3 + 3) & ~3u;
  std::vector<uint8_t> file(54 + size_t(row_bytes) * image.height);
  auto put32 = [&](size_t offset, uint32_t value) { std::memcpy(&file[offset], &value, 4); };
  file[0] = 'B';
  file[1] = 'M';
  put32(2, uint32_t(file.size()));
  put32(10, 54);
  put32(14, 40);
  put32(18, image.width);
  put32(22, uint32_t(-int32_t(image.height)));  // top-down
  file[26] = 1;
  file[28] = 24;
  put32(34, row_bytes * image.height);
  for (uint32_t y = 0; y < image.height; ++y) {
    const uint8_t* source = image.data.data() + size_t(image.stride) * y;
    uint8_t* dest = &file[54 + size_t(row_bytes) * y];
    for (uint32_t x = 0; x < image.width; ++x) {
      dest[x * 3 + 0] = source[x * 4 + 2];
      dest[x * 3 + 1] = source[x * 4 + 1];
      dest[x * 3 + 2] = source[x * 4 + 0];
    }
  }
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
  return bool(out);
}

// A film request ("<seconds> <csv>") samples the picture as fast as it can be captured:
// one line per capture with its time and the mean brightness of a 4x4 grid of cells.
// An output ending in ".gray" instead holds each new picture as a record of its time
// (double, seconds since process start, the time base of input events) and
// kGrayWidth x kGrayHeight bytes of luminance, a repeated picture left out.
constexpr uint32_t kGrayWidth = 192;
constexpr uint32_t kGrayHeight = 108;

void FilmGray(rex::ui::Presenter* source, double seconds, const std::string& output) {
  std::ofstream out(output + ".tmp", std::ios::binary);
  const double end = Now() + seconds;
  REXLOG_INFO("Film: {} s to {} from {:.2f}", seconds, output, Now());
  rex::ui::RawImage image;
  std::vector<uint8_t> frame(kGrayWidth * kGrayHeight);
  std::vector<uint8_t> previous;
  while (Now() < end) {
    if (!source->CaptureGuestOutput(image) || !image.width || !image.height) {
      continue;
    }
    const double t = Now();
    for (uint32_t y = 0; y < kGrayHeight; ++y) {
      const uint8_t* row = image.data.data() + size_t(image.stride) * (y * image.height / kGrayHeight);
      for (uint32_t x = 0; x < kGrayWidth; ++x) {
        const uint8_t* pixel = row + size_t(x * image.width / kGrayWidth) * 4;
        frame[y * kGrayWidth + x] = uint8_t((pixel[0] * 77 + pixel[1] * 150 + pixel[2] * 29) >> 8);
      }
    }
    if (frame == previous) {
      continue;
    }
    previous = frame;
    out.write(reinterpret_cast<const char*>(&t), sizeof(t));
    out.write(reinterpret_cast<const char*>(frame.data()), std::streamsize(frame.size()));
  }
  out.close();
  std::error_code error;
  std::filesystem::rename(output + ".tmp", output, error);
}

void Film(rex::ui::Presenter* source, double seconds, const std::string& output) {
  if (output.ends_with(".gray")) {
    FilmGray(source, seconds, output);
    return;
  }
  std::ofstream out(output + ".tmp");
  const auto start = std::chrono::steady_clock::now();
  const auto end = start + std::chrono::duration<double>(seconds);
  rex::ui::RawImage image;
  while (std::chrono::steady_clock::now() < end) {
    if (!source->CaptureGuestOutput(image) || !image.width || !image.height) {
      continue;
    }
    const double t =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    uint64_t sums[16] = {};
    uint32_t counts[16] = {};
    for (uint32_t y = 0; y < image.height; y += 4) {
      const uint8_t* row = image.data.data() + size_t(image.stride) * y;
      for (uint32_t x = 0; x < image.width; x += 4) {
        const uint32_t cell = (y * 4 / image.height) * 4 + x * 4 / image.width;
        sums[cell] += row[x * 4] + row[x * 4 + 1] + row[x * 4 + 2];
        ++counts[cell];
      }
    }
    out << t;
    for (int i = 0; i < 16; ++i) {
      out << ',' << double(sums[i]) / (3.0 * std::max(1u, counts[i]));
    }
    out << '\n';
  }
  out.close();
  std::error_code error;
  std::filesystem::rename(output + ".tmp", output, error);
}

#if defined(_WIN32)
// Windows' XINPUT_STATE; xinput.h itself clashes with the SDK's input names.
struct PadState {
  DWORD packet;
  WORD buttons;
  BYTE left_trigger;
  BYTE right_trigger;
  SHORT thumb_lx;
  SHORT thumb_ly;
  SHORT thumb_rx;
  SHORT thumb_ry;
};

std::string ControlsText(const PadState& pad) {
  using namespace rex::input;
  static constexpr std::pair<uint16_t, const char*> kButtons[] = {
      {X_INPUT_GAMEPAD_DPAD_UP, "UP"},     {X_INPUT_GAMEPAD_DPAD_DOWN, "DOWN"},
      {X_INPUT_GAMEPAD_DPAD_LEFT, "LEFT"}, {X_INPUT_GAMEPAD_DPAD_RIGHT, "RIGHT"},
      {X_INPUT_GAMEPAD_START, "START"},    {X_INPUT_GAMEPAD_BACK, "BACK"},
      {X_INPUT_GAMEPAD_LEFT_THUMB, "LS"},  {X_INPUT_GAMEPAD_RIGHT_THUMB, "RS"},
      {X_INPUT_GAMEPAD_LEFT_SHOULDER, "LB"}, {X_INPUT_GAMEPAD_RIGHT_SHOULDER, "RB"},
      {X_INPUT_GAMEPAD_A, "A"},            {X_INPUT_GAMEPAD_B, "B"},
      {X_INPUT_GAMEPAD_X, "X"},            {X_INPUT_GAMEPAD_Y, "Y"},
  };
  std::string text;
  for (const auto& [mask, name] : kButtons) {
    if (pad.buttons & mask) {
      text += ' ';
      text += name;
    }
  }
  // Two decimals, and rest noise below 0.05 dropped, so a stick at rest does
  // not split the recording into a line per poll.
  auto value = [&text](const char* name, double v) {
    const long hundredths = std::lround(v * 100.0);
    if (std::abs(hundredths) >= 5) {
      text += std::format(" {}={:.2f}", name, hundredths / 100.0);
    }
  };
  value("LT", pad.left_trigger / 255.0);
  value("RT", pad.right_trigger / 255.0);
  value("LX", pad.thumb_lx / 32767.0);
  value("LY", pad.thumb_ly / 32767.0);
  value("RX", pad.thumb_rx / 32767.0);
  value("RY", pad.thumb_ry / 32767.0);
  return text;
}

// Polls the first connected XInput pad and writes each span of unchanged input
// as a script line; an input event starts a new time base ("@name"), so a
// replay can follow the recording from that event on whatever the load times.
void RecordInput(std::string path) {
  using GetState = DWORD(WINAPI*)(DWORD, PadState*);
  const HMODULE xinput = LoadLibraryW(L"xinput1_4.dll");
  const auto get_state =
      xinput ? reinterpret_cast<GetState>(GetProcAddress(xinput, "XInputGetState")) : nullptr;
  std::ofstream out(path);
  if (!get_state || !out) {
    REXLOG_ERROR("Input recording: cannot record to {}", path);
    return;
  }
  REXLOG_INFO("Input recording to {}", path);
  out << "# FM4 input recording\n";
  double base = 0.0;
  size_t events_seen = 0;
  std::string held;
  double held_since = 0.0;
  auto close_span = [&](double now) {
    if (!held.empty()) {
      out << std::format("{:.3f} {:.3f}{}\n", held_since - base, now - held_since, held);
      out.flush();
    }
    held_since = now;
  };
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
    std::string current;
    for (DWORD user = 0; user < 4; ++user) {
      PadState state;
      if (get_state(user, &state) == ERROR_SUCCESS) {
        current = ControlsText(state);
        break;
      }
    }
    std::vector<std::pair<std::string, double>> fired;
    {
      std::lock_guard lock(g_mutex);
      for (const auto& [name, times] : g_events) {
        for (double t : times) {
          fired.emplace_back(name, t);
        }
      }
    }
    std::sort(fired.begin(), fired.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    for (; events_seen < fired.size(); ++events_seen) {
      const auto& [name, t] = fired[events_seen];
      close_span(t);
      base = t;
      out << '@' << name << '\n';
    }
    if (current != held) {
      close_span(Now());
      held = std::move(current);
    }
  }
}
#else
void RecordInput(std::string path) {
  REXLOG_WARN("Input recording is not supported on this platform: {}", path);
}
#endif

// Autovista opens behind a loading screen that takes longer or shorter with the
// save and the settings, so "autovista_shown" marks the first picture after it
// where the dark loading screen has brightened into the scene.
void WatchSceneShown(std::function<rex::ui::Presenter*()> presenter) {
  constexpr double kDark = 45.0;
  constexpr double kShown = 60.0;
  size_t handled = 0;
  rex::ui::RawImage image;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
      std::lock_guard lock(g_mutex);
      const auto opened = g_events.find("autovista");
      if (opened == g_events.end() || opened->second.size() <= handled) {
        continue;
      }
      handled = opened->second.size();
    }
    rex::ui::Presenter* const source = presenter();
    bool dark = false;
    for (const double end = Now() + 30.0; source && Now() < end;) {
      if (!source->CaptureGuestOutput(image) || !image.width || !image.height) {
        continue;
      }
      uint64_t sum = 0;
      uint32_t count = 0;
      for (uint32_t y = 0; y < image.height; y += 8) {
        const uint8_t* row = image.data.data() + size_t(image.stride) * y;
        for (uint32_t x = 0; x < image.width; x += 8) {
          sum += (row[x * 4] * 77 + row[x * 4 + 1] * 150 + row[x * 4 + 2] * 29) >> 8;
          ++count;
        }
      }
      const double mean = double(sum) / std::max(1u, count);
      dark = dark || mean < kDark;
      if (dark && mean > kShown) {
        MarkInputEvent("autovista_shown");
        break;
      }
    }
  }
}

}  // namespace

void MarkInputEvent(const std::string& name) {
  const double now = Now();
  {
    std::lock_guard lock(g_mutex);
    g_events[name].push_back(now);
  }
  REXLOG_INFO("Input event {} at {:.2f}", name, now);
}

void StartCaptureWatcher(std::function<rex::ui::Presenter*()> presenter) {
  if (!g_path.empty() || g_recording) {
    std::thread(WatchSceneShown, presenter).detach();
  }
  if (g_path.empty()) {
    return;
  }
  std::thread([presenter = std::move(presenter)] {
    const std::filesystem::path request = g_path + ".shot";
    const std::filesystem::path film_request = g_path + ".film";
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      std::error_code error;
      if (std::filesystem::exists(film_request, error)) {
        double seconds = 0.0;
        std::string output;
        {
          std::ifstream file(film_request);
          file >> seconds >> output;
        }
        std::filesystem::remove(film_request, error);
        if (rex::ui::Presenter* source = presenter(); source && seconds > 0.0 && !output.empty()) {
          Film(source, seconds, output);
        }
        continue;
      }
      if (!std::filesystem::exists(request, error)) {
        continue;
      }
      std::string output;
      {
        std::ifstream file(request);
        std::getline(file, output);
      }
      std::filesystem::remove(request, error);
      rex::ui::Presenter* source = presenter();
      rex::ui::RawImage image;
      if (output.empty() || !source || !source->CaptureGuestOutput(image) || !image.width ||
          !image.height || !WriteBmp(output + ".tmp", image)) {
        REXLOG_WARN("Scripted input: capture to '{}' failed", output);
        continue;
      }
      std::filesystem::rename(output + ".tmp", output, error);
    }
  }).detach();
}

void ConfigureScriptedInput(rex::RuntimeConfig& config) {
#if defined(_WIN32)
  char* path = nullptr;
  size_t length = 0;
  if (_dupenv_s(&path, &length, "FM4_STATIC_RECOMP_INPUT_RECORD") == 0 && path) {
    if (*path) {
      g_recording = true;
      std::thread(RecordInput, std::string(path)).detach();
    }
    std::free(path);
    path = nullptr;
  }
  if (_dupenv_s(&path, &length, "FM4_STATIC_RECOMP_INPUT_SCRIPT") != 0 || !path) {
    return;
  }
  g_path = path;
  std::free(path);
#else
  const char* record_env = std::getenv("FM4_STATIC_RECOMP_INPUT_RECORD");
  if (record_env && *record_env) {
    g_recording = true;
    std::thread(RecordInput, std::string(record_env)).detach();
  }
  const char* script_env = std::getenv("FM4_STATIC_RECOMP_INPUT_SCRIPT");
  if (!script_env || !*script_env) {
    return;
  }
  g_path = script_env;
#endif
  if (!g_path.empty() && LoadScript(g_path)) {
    std::error_code error;
    g_loaded_time = std::filesystem::last_write_time(g_path, error);
    config.input_factory = &CreateInputSystem;
  }
}

}  // namespace fm4_static_recomp
