// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
#include "ui/hostui/menu.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

#include <rex/ui/keybinds.h>

namespace fm4_static_recomp::hostui {
namespace {

// Whether `key` is the key of one of the bind's tokens, modifiers aside.
bool BindHasKey(std::string_view bind, rex::ui::VirtualKey key) {
  while (!bind.empty()) {
    const size_t comma = bind.find(',');
    std::string_view token = bind.substr(0, comma);
    bind = comma == std::string_view::npos ? std::string_view() : bind.substr(comma + 1);
    while (token.starts_with(' '))
      token.remove_prefix(1);
    while (token.ends_with(' '))
      token.remove_suffix(1);
    const size_t plus = token.rfind('+');
    if (!token.empty() &&
        rex::ui::ParseVirtualKey(plus == std::string_view::npos || plus + 1 == token.size()
                                     ? token
                                     : token.substr(plus + 1)) == key) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::optional<NavCommand> KeyCommand(const MenuKeys& keys, rex::ui::VirtualKey key, bool repeat) {
  using rex::ui::VirtualKey;
  std::optional<NavCommand> direction;
  std::optional<NavCommand> button;
  switch (key) {
    case VirtualKey::kUp:
    case VirtualKey::kNumpad8:
      direction = NavCommand::kUp;
      break;
    case VirtualKey::kDown:
    case VirtualKey::kNumpad2:
      direction = NavCommand::kDown;
      break;
    case VirtualKey::kLeft:
    case VirtualKey::kNumpad4:
      direction = NavCommand::kLeft;
      break;
    case VirtualKey::kRight:
    case VirtualKey::kNumpad6:
      direction = NavCommand::kRight;
      break;
    case VirtualKey::kSpace:
    case VirtualKey::kReturn:
      button = NavCommand::kAccept;
      break;
    case VirtualKey::kBack:
    case VirtualKey::kEscape:
      button = NavCommand::kBack;
      break;
    default:
      if (BindHasKey(keys.up, key)) {
        direction = NavCommand::kUp;
      } else if (BindHasKey(keys.down, key)) {
        direction = NavCommand::kDown;
      } else if (BindHasKey(keys.left, key)) {
        direction = NavCommand::kLeft;
      } else if (BindHasKey(keys.right, key)) {
        direction = NavCommand::kRight;
      } else if (BindHasKey(keys.accept, key)) {
        button = NavCommand::kAccept;
      } else if (BindHasKey(keys.back, key)) {
        button = NavCommand::kBack;
      } else if (BindHasKey(keys.apply, key)) {
        button = NavCommand::kApply;
      } else if (BindHasKey(keys.clear, key)) {
        button = NavCommand::kClear;
      } else if (BindHasKey(keys.previous_tab, key)) {
        button = NavCommand::kPreviousTab;
      } else if (BindHasKey(keys.next_tab, key)) {
        button = NavCommand::kNextTab;
      }
      break;
  }
  if (direction) {
    return direction;
  }
  return repeat ? std::nullopt : button;
}

MenuScreen::MenuScreen(std::string title, std::vector<MenuRow> rows)
    : title_(std::move(title)), rows_(std::move(rows)) {
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].is_enabled()) {
      focus_ = i;
      break;
    }
  }
}

bool MenuScreen::SetFocus(size_t index) {
  if (index >= rows_.size() || !rows_[index].is_enabled()) {
    return false;
  }
  focus_ = index;
  return true;
}

MenuScreen::Result MenuScreen::CaptureKey(const std::string& key) {
  if (!capturing()) {
    return Result::kNone;
  }
  capture_.key(key);
  return Result::kChanged;
}

MenuScreen::Result MenuScreen::Handle(NavCommand command) {
  if (capturing()) {
    if (command != NavCommand::kBack) {
      return Result::kNone;
    }
    capture_.cancel();
    return Result::kChanged;
  }
  if (command == NavCommand::kPreviousTab || command == NavCommand::kNextTab) {
    return Result::kNone;
  }
  if (command == NavCommand::kBack || command == NavCommand::kClose) {
    return leave_ && !leave_() ? Result::kChanged : Result::kBack;
  }
  if (command == NavCommand::kApply) {
    if (!apply_) {
      return Result::kNone;
    }
    apply_();
    return Result::kChanged;
  }
  if (rows_.empty()) {
    return Result::kNone;
  }
  if (command == NavCommand::kUp || command == NavCommand::kDown) {
    const size_t count = rows_.size();
    size_t index = focus_;
    for (size_t step = 0; step < count; ++step) {
      index = command == NavCommand::kDown ? (index + 1) % count : (index + count - 1) % count;
      if (rows_[index].is_enabled()) {
        const bool moved = index != focus_;
        focus_ = index;
        return moved ? Result::kChanged : Result::kNone;
      }
    }
    return Result::kNone;
  }
  if (status().busy) {
    return Result::kNone;
  }
  if (MenuRow& row = rows_[focus_]; row.slots && row.is_enabled()) {
    const size_t last = std::max<size_t>(row.slots().size(), 1) - 1;
    const size_t slot = std::min(slot_, last);
    switch (command) {
      case NavCommand::kLeft:
      case NavCommand::kRight: {
        const size_t next =
            command == NavCommand::kLeft ? (slot ? slot - 1 : 0) : std::min(slot + 1, last);
        if (next == slot) {
          return Result::kNone;
        }
        slot_ = next;
        return Result::kChanged;
      }
      case NavCommand::kAccept:
        if (!row.pick) {
          return Result::kNone;
        }
        row.pick(slot);
        return Result::kChanged;
      case NavCommand::kClear:
        if (!row.clear || !row.clear_label || row.clear_label(slot).empty()) {
          return Result::kNone;
        }
        row.clear(slot);
        return Result::kChanged;
      default:
        return Result::kNone;
    }
  }
  if (command == NavCommand::kClear) {
    return Result::kNone;
  }
  const int direction = command == NavCommand::kLeft ? -1 : 1;
  if (rows_[focus_].slider) {
    return Slide(direction);
  }
  MenuRow& row = rows_[focus_];
  if (!row.is_enabled() || !row.adjust) {
    return Result::kNone;
  }
  row.adjust(direction);
  return Result::kChanged;
}

MenuScreen::Result MenuScreen::Slide(int steps) {
  if (focus_ >= rows_.size() || !rows_[focus_].slider || !rows_[focus_].is_enabled() ||
      status().busy) {
    return Result::kNone;
  }
  const MenuSlider& slider = *rows_[focus_].slider;
  const auto [low, high] = slider.range();
  const int current = slider.get();
  const int next = std::clamp(current + steps, low, high);
  if (next == current) {
    return Result::kNone;
  }
  slider.set(next);
  return Result::kChanged;
}

MenuScreen::Result MenuScreen::SlideTo(float fraction) {
  if (focus_ >= rows_.size() || !rows_[focus_].slider) {
    return Result::kNone;
  }
  const auto [low, high] = rows_[focus_].slider->range();
  const int target =
      low + int(std::lround(std::clamp(fraction, 0.0f, 1.0f) * float(high - low)));
  return Slide(target - rows_[focus_].slider->get());
}

std::vector<NavCommand> PadNavigator::Poll(uint16_t buttons, int16_t thumb_lx, int16_t thumb_ly,
                                           uint64_t now_ms, bool stick_x_free) {
  if (stick_x_free) {
    thumb_lx = 0;
  }
  int direction = -1;
  if ((buttons & kDpadUp) || thumb_ly > kStickThreshold) {
    direction = int(NavCommand::kUp);
  } else if ((buttons & kDpadDown) || thumb_ly < -kStickThreshold) {
    direction = int(NavCommand::kDown);
  } else if ((buttons & kDpadLeft) || thumb_lx < -kStickThreshold) {
    direction = int(NavCommand::kLeft);
  } else if ((buttons & kDpadRight) || thumb_lx > kStickThreshold) {
    direction = int(NavCommand::kRight);
  }

  std::vector<NavCommand> commands;
  if (!primed_) {
    primed_ = true;
    previous_buttons_ = buttons;
    held_direction_ = direction;
    next_repeat_ms_ = UINT64_MAX;  // a direction held at open waits for release
    return commands;
  }
  if (direction != held_direction_) {
    held_direction_ = direction;
    if (direction >= 0) {
      commands.push_back(NavCommand(direction));
      next_repeat_ms_ = now_ms + kRepeatDelayMs;
    }
  } else if (direction >= 0 && now_ms >= next_repeat_ms_) {
    commands.push_back(NavCommand(direction));
    next_repeat_ms_ = now_ms + kRepeatIntervalMs;
  }
  const uint16_t pressed = buttons & ~previous_buttons_;
  if (pressed & kButtonA) {
    commands.push_back(NavCommand::kAccept);
  }
  if (pressed & kButtonB) {
    commands.push_back(NavCommand::kBack);
  }
  if (pressed & kButtonX) {
    commands.push_back(NavCommand::kClear);
  }
  if (pressed & kButtonY) {
    commands.push_back(NavCommand::kApply);
  }
  if (pressed & kLeftShoulder) {
    commands.push_back(NavCommand::kPreviousTab);
  }
  if (pressed & kRightShoulder) {
    commands.push_back(NavCommand::kNextTab);
  }
  if ((pressed & kStart) && !(buttons & kBack)) {
    commands.push_back(NavCommand::kClose);
  }
  previous_buttons_ = buttons;
  return commands;
}

size_t FirstVisibleRow(size_t row_count, size_t list_begin, size_t visible_rows, size_t focus,
                       std::optional<size_t> kept_first) {
  if (kept_first && focus >= *kept_first && focus < *kept_first + visible_rows) {
    return *kept_first;
  }
  if (row_count - list_begin > visible_rows && focus >= list_begin + visible_rows / 2) {
    return std::min(focus - visible_rows / 2, row_count - visible_rows);
  }
  return list_begin;
}

}  // namespace fm4_static_recomp::hostui
