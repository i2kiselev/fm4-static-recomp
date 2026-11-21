// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "ui/signage_page.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include "track_signage.h"
#include "track_signage_runtime.h"

namespace fm4_static_recomp::ui {
namespace {

using hostui::MenuPicture;
using hostui::MenuRow;
using hostui::MenuScreen;
using signage::Board;
using signage::Choices;
using signage::Track;

bool EnabledNow() { return rex::cvar::GetFlagByName("fm4_track_signage") != "false"; }

bool FromCommandLine() {
  return rex::cvar::GetFlagSource("fm4_track_signage") == rex::cvar::Source::kCommandLine;
}

std::string Lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; });
  return text;
}

std::string Count(size_t n, const char* one, const char* many) {
  return std::to_string(n) + " " + (n == 1 ? one : many);
}

// The page's state: the choices and the switch as saved, and the drafted ones.
struct Draft {
  Choices saved = signage::CurrentChoices();
  Choices choices = saved;
  // The switch as saved: a launch option overrides it for the session, and is never written to the file.
  bool saved_enabled = FromCommandLine() ? signage::SavedEnabled() : EnabledNow();
  bool enabled = saved_enabled;
  bool leave_warned = false;
  bool applied = false;
  bool save_failed = false;
  const Track* open_track = nullptr;  // the track whose boards the page lists, or the main list
  MenuScreen* screen = nullptr;

  bool Pending() const { return choices != saved || enabled != saved_enabled; }
  void Changed() {
    leave_warned = false;
    applied = false;
    save_failed = false;
  }
  std::string Choice(const Track& track, const Board& board) const {
    const auto found = choices.find(signage::ChoiceKey(track, board));
    return found == choices.end() ? std::string() : found->second;
  }
  void Choose(const Track& track, const Board& board, const std::string& file) {
    if (file.empty()) {
      choices.erase(signage::ChoiceKey(track, board));
    } else {
      choices[signage::ChoiceKey(track, board)] = file;
    }
    Changed();
  }
  size_t ChosenOn(const Track& track) const {
    size_t n = 0;
    for (const Board& board : track.boards) n += Choice(track, board).empty() ? 0 : 1;
    return n;
  }
  void Apply() {
    if (!Pending()) return;
    if (!signage::SaveChoices(choices, enabled)) {
      save_failed = true;
      return;
    }
    saved = choices;
    saved_enabled = enabled;
    leave_warned = false;
    applied = true;
  }
};

// The pictures a board can take: DEFAULT (empty) first, then the folder's, by name.
std::vector<std::string> Options() {
  std::vector<std::string> options{std::string()};
  for (const signage::Picture& picture : signage::Pictures()) options.push_back(picture.file);
  std::sort(options.begin() + 1, options.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
  return options;
}

const signage::Picture* FindPicture(const std::vector<signage::Picture>& pictures, const std::string& file) {
  for (const auto& picture : pictures) {
    if (Lower(picture.file) == Lower(file)) return &picture;
  }
  return nullptr;
}

// A preview for the description panel, sharing the image's pixels (so its texture is kept).
MenuPicture PictureOf(const std::shared_ptr<const signage::Image>& image, std::string caption) {
  if (!image || !image->width) return {};
  return MenuPicture{std::shared_ptr<const std::vector<uint8_t>>(image, &image->rgba), image->width, image->height,
                     std::move(caption)};
}

MenuRow Heading(std::string label, bool divider = true) {
  MenuRow row;
  row.label = std::move(label);
  row.heading = true;
  row.divider_above = divider;
  return row;
}

std::vector<MenuRow> MainRows(const std::shared_ptr<Draft>& draft);
std::vector<MenuRow> TrackRows(const std::shared_ptr<Draft>& draft, const Track& track);

void Open(const std::shared_ptr<Draft>& draft, const Track* track, size_t focus = SIZE_MAX) {
  draft->open_track = track;
  draft->screen->ReplaceRows(track ? TrackRows(draft, *track) : MainRows(draft), focus);
}

MenuRow EnabledRow(const std::shared_ptr<Draft>& draft) {
  MenuRow row;
  row.label = "TRACK SIGNAGE";
  row.description =
      "Your own pictures on the track's boards. Choose a track below, then a picture for each of "
      "its boards; SAVE & APPLY shows them, even mid-race. OFF shows the default boards everywhere and "
      "keeps your choices for later.";
  row.value = [draft] { return std::string(draft->enabled ? "ON" : "OFF"); };
  row.adjust = [draft](int) {
    draft->enabled = !draft->enabled;
    draft->Changed();
  };
  row.note = [draft] {
    if (!draft->enabled || draft->Pending()) return std::string();
    const size_t shown = signage::ShownCount();
    return shown ? Count(shown, "board shows your picture now.", "boards show your pictures now.")
                 : std::string("No board shows a picture yet.");
  };
  row.badge = [] { return FromCommandLine() ? std::string("Set by a launch option for this session.") : std::string(); };
  row.unapplied = [draft] { return draft->enabled != draft->saved_enabled; };
  return row;
}

MenuRow TrackRow(const std::shared_ptr<Draft>& draft, const Track& track) {
  MenuRow row;
  row.label = std::string(track.name);
  row.description = "Opens the track's boards: each can show a picture of yours or its default. B comes back here.";
  row.value = [&track] { return Count(track.boards.size(), "BOARD", "BOARDS"); };
  row.adjust = [draft, &track](int) { Open(draft, &track); };
  row.note = [draft, &track] {
    const size_t chosen = draft->ChosenOn(track);
    return chosen ? Count(chosen, "board has a picture of yours.", "boards have pictures of yours.")
                  : std::string("Every board shows its default.");
  };
  row.unapplied = [draft, &track] {
    for (const Board& board : track.boards) {
      const std::string key = signage::ChoiceKey(track, board);
      const auto a = draft->choices.find(key), b = draft->saved.find(key);
      if ((a == draft->choices.end()) != (b == draft->saved.end()) || (a != draft->choices.end() && a->second != b->second)) {
        return true;
      }
    }
    return false;
  };
  return row;
}

MenuRow FolderRow() {
  MenuRow row;
  row.label = "SIGNAGE FOLDER";
  row.description =
      "Opens the folder your pictures come from, in Explorer. Put PNG files there, named as you like: "
      "every board can use any of them, and they are stretched to the board's shape (a bigger picture "
      "gives a sharper board). A picture you change there changes on track within a second or two.";
  row.value = [] { return std::string("OPEN"); };
  row.adjust = [](int) { signage::OpenFolder(); };
  row.note = [] {
    const size_t pictures = signage::Pictures().size();
    const auto folder = signage::Folder();
    return Count(pictures, "picture", "pictures") + " in " +
           (folder.empty() ? std::string("the signage folder") : rex::path_to_utf8(folder)) + ".";
  };
  return row;
}

MenuRow BoardRow(const std::shared_ptr<Draft>& draft, const Track& track, const Board& board) {
  MenuRow row;
  row.label = std::string(board.name);
  row.description = "LEFT and RIGHT choose the picture this board shows: DEFAULT, or one of the signage folder. "
                    "It is " + std::to_string(board.width) + " x " + std::to_string(board.height) +
                    "; a picture of that shape fits best, and 2x or 4x its size is sharper.";
  row.value = [draft, &track, &board] {
    const std::string file = draft->Choice(track, board);
    return file.empty() ? std::string("DEFAULT") : file;
  };
  row.adjust = [draft, &track, &board](int direction) {
    const std::vector<std::string> options = Options();
    const std::string current = draft->Choice(track, board);
    size_t index = 0;
    for (size_t i = 0; i < options.size(); ++i) {
      if (Lower(options[i]) == Lower(current)) index = i;
    }
    index = (index + options.size() + (direction < 0 ? options.size() - 1 : 1)) % options.size();
    draft->Choose(track, board, options[index]);
  };
  row.note = [draft, &track, &board] {
    const std::string file = draft->Choice(track, board);
    if (file.empty()) return std::string("Shows the default board.");
    const auto pictures = signage::Pictures();
    const signage::Picture* picture = FindPicture(pictures, file);
    if (!picture) return file + " is not in the signage folder now: the default board shows.";
    const uint32_t scale = signage::ScaleFor(board, picture->width, picture->height);
    return file + ": " + std::to_string(picture->width) + " x " + std::to_string(picture->height) + ", shown at " +
           std::to_string(board.width * scale) + " x " + std::to_string(board.height * scale) + ".";
  };
  row.pictures = [draft, &track, &board] {
    std::vector<MenuPicture> out;
    if (auto game = PictureOf(signage::BoardPreview(track, board), "DEFAULT"); game.rgba) out.push_back(game);
    const std::string file = draft->Choice(track, board);
    if (!file.empty()) {
      const auto pictures = signage::Pictures();
      if (const signage::Picture* picture = FindPicture(pictures, file)) {
        if (auto yours = PictureOf(picture->thumbnail, "YOURS"); yours.rgba) out.push_back(yours);
      }
    }
    return out;
  };
  row.unapplied = [draft, &track, &board] {
    const std::string key = signage::ChoiceKey(track, board);
    const auto a = draft->choices.find(key), b = draft->saved.find(key);
    return (a == draft->choices.end()) != (b == draft->saved.end()) || (a != draft->choices.end() && a->second != b->second);
  };
  return row;
}

MenuRow ResetRow(const std::shared_ptr<Draft>& draft, const Track& track) {
  MenuRow row;
  row.label = "RESET ALL BOARDS";
  row.description = "Puts every board of this track back to its default (with SAVE & APPLY). Your pictures stay in the folder.";
  row.value = [] { return std::string("RESET"); };
  row.adjust = [draft, &track](int) {
    for (const Board& board : track.boards) draft->Choose(track, board, std::string());
    // This row has nothing left to reset and greys out: the focus goes to the first board.
    draft->screen->ReplaceRows(TrackRows(draft, track), 1);
  };
  row.enabled = [draft, &track] { return draft->ChosenOn(track) > 0; };
  return row;
}

std::vector<MenuRow> MainRows(const std::shared_ptr<Draft>& draft) {
  std::vector<MenuRow> rows;
  rows.push_back(EnabledRow(draft));
  rows.push_back(Heading("TRACKS"));
  for (const Track& track : signage::Tracks()) rows.push_back(TrackRow(draft, track));
  rows.push_back(Heading("PICTURES"));
  rows.push_back(FolderRow());
  return rows;
}

std::vector<MenuRow> TrackRows(const std::shared_ptr<Draft>& draft, const Track& track) {
  std::vector<MenuRow> rows;
  rows.push_back(Heading(std::string(track.name), false));
  for (const Board& board : track.boards) rows.push_back(BoardRow(draft, track, board));
  rows.push_back(Heading("", true));
  rows.push_back(ResetRow(draft, track));
  return rows;
}

}  // namespace

std::unique_ptr<MenuScreen> CreateSignagePage() {
  auto draft = std::make_shared<Draft>();
  auto screen = std::make_unique<MenuScreen>("SIGNAGE", MainRows(draft));
  draft->screen = screen.get();
  screen->set_apply([draft] { draft->Apply(); });
  // B on a track's boards goes back to the tracks (the draft stays); closing the menu or changing tab does not.
  screen->set_back([draft] {
    if (!draft->open_track) return false;
    size_t focus = 2;
    const auto tracks = signage::Tracks();
    for (size_t i = 0; i < tracks.size(); ++i) {
      if (&tracks[i] == draft->open_track) focus = 2 + i;
    }
    Open(draft, nullptr, focus);
    return true;
  });
  screen->set_leave([draft] {
    if (!draft->Pending() || draft->leave_warned) return true;
    draft->leave_warned = true;
    return false;
  });
  screen->set_status([draft] {
    if (draft->save_failed) return hostui::MenuStatus{.text = "COULD NOT SAVE SIGNAGE.TOML", .warning = true};
    if (draft->Pending()) return hostui::MenuStatus{.text = "UNSAVED CHANGES", .warning = draft->leave_warned};
    if (draft->applied && FromCommandLine() && draft->enabled != EnabledNow()) {
      return hostui::MenuStatus{.text = "SAVED. A LAUNCH OPTION KEEPS THE SWITCH THIS SESSION."};
    }
    if (draft->applied) return hostui::MenuStatus{.text = "SAVED. THE BOARDS CHANGE ON TRACK NOW."};
    return hostui::MenuStatus{};
  });
  screen->set_update([draft](bool closing) {
    if (closing && draft->Pending()) REXLOG_INFO("Signage page: discarded unapplied changes");
  });
  return screen;
}

}  // namespace fm4_static_recomp::ui
