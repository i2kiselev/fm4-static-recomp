// Portions copyright (c) 2026, Pinyon Shift contributors (BSD 3-Clause: see THIRD_PARTY_NOTICES.md).
// Copyright (c) 2026, i2kiselev (BSD 3-Clause: see LICENSE in the repository root).
#include "config/host_config.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fm4_static_recomp::config {
namespace {

bool IsSpace(char c) { return c == ' ' || c == '\t'; }

// The value range of `name`'s line within `text`: [value_begin, value_end)
// and the line's [line_begin, line_end) excluding the line ending.
struct Match {
  size_t line_begin = std::string_view::npos;
  size_t line_end = 0;
  size_t value_begin = 0;
};

Match Find(std::string_view text, std::string_view name) {
  size_t line_begin = 0;
  while (line_begin <= text.size()) {
    size_t line_end = text.find('\n', line_begin);
    if (line_end == std::string_view::npos) {
      line_end = text.size();
    }
    size_t content_end = line_end;
    if (content_end > line_begin && text[content_end - 1] == '\r') {
      --content_end;
    }
    size_t at = line_begin;
    while (at < content_end && IsSpace(text[at])) {
      ++at;
    }
    if (text.substr(at, name.size()) == name) {
      size_t after = at + name.size();
      while (after < content_end && IsSpace(text[after])) {
        ++after;
      }
      if (after < content_end && text[after] == '=') {
        return Match{line_begin, content_end, after + 1};
      }
    }
    if (line_end == text.size()) {
      break;
    }
    line_begin = line_end + 1;
  }
  return Match{};
}

std::string_view Trim(std::string_view value) {
  while (!value.empty() && (IsSpace(value.front()) || value.front() == '\r')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (IsSpace(value.back()) || value.back() == '\r')) {
    value.remove_suffix(1);
  }
  return value;
}

}  // namespace

std::optional<std::string> GetValue(std::string_view text, std::string_view name) {
  const Match match = Find(text, name);
  if (match.line_begin == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view value =
      Trim(text.substr(match.value_begin, match.line_end - match.value_begin));
  // A TOML basic string, as the launcher writes it: '#' inside is no comment, and \\ and \" are
  // escapes (a wheel's device path, \\?\HID#..., holds both). Other backslashes stay as written.
  if (value.size() >= 2 && value.front() == '"') {
    std::string out;
    for (size_t i = 1; i < value.size() && value[i] != '"'; ++i) {
      if (value[i] == '\\' && i + 1 < value.size() && (value[i + 1] == '\\' || value[i + 1] == '"')) {
        ++i;
      }
      out.push_back(value[i]);
    }
    if (out.empty()) {
      return std::nullopt;
    }
    return out;
  }
  value = value.substr(0, value.find('#'));
  value = Trim(value);
  while (!value.empty() && value.front() == '"') {
    value.remove_prefix(1);
  }
  while (!value.empty() && value.back() == '"') {
    value.remove_suffix(1);
  }
  if (value.empty()) {
    return std::nullopt;
  }
  return std::string(value);
}

std::string SetValue(std::string_view text, std::string_view name, std::string_view literal) {
  const std::string line = std::string(name) + " = " + std::string(literal);
  const Match match = Find(text, name);
  if (match.line_begin != std::string_view::npos) {
    std::string out(text.substr(0, match.line_begin));
    out += line;
    out += text.substr(match.line_end);
    return out;
  }
  const std::string_view newline = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
  std::string_view trimmed = text;
  while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == '\n')) {
    trimmed.remove_suffix(1);
  }
  std::string out(trimmed);
  if (!out.empty()) {
    out += newline;
  }
  out += line;
  out += newline;
  return out;
}

std::string RemoveValue(std::string_view text, std::string_view name) {
  const Match match = Find(text, name);
  if (match.line_begin == std::string_view::npos) {
    return std::string(text);
  }
  const size_t newline = text.find('\n', match.line_end);
  const size_t end = newline == std::string_view::npos ? text.size() : newline + 1;
  return std::string(text.substr(0, match.line_begin)) + std::string(text.substr(end));
}

std::string Quote(std::string_view value) {
  std::string out = "\"";
  for (char c : value) {
    if (c != '"' && c != '\\' && c != '\r' && c != '\n') {
      out.push_back(c);
    }
  }
  out.push_back('"');
  return out;
}

std::optional<std::string> ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

bool WriteAtomically(const std::filesystem::path& path, std::string_view text) {
  std::string body(text);
  const std::string newline = body.find("\r\n") != std::string::npos ? "\r\n" : "\n";
  while (!body.empty() && (body.back() == '\r' || body.back() == '\n')) {
    body.pop_back();
  }
  body += newline;
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(body.data(), std::streamsize(body.size())) || !output.flush()) {
      std::filesystem::remove(temporary, error);
      return false;
    }
  }
#if defined(_WIN32)
  if (MoveFileExW(temporary.c_str(), path.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    return true;
  }
#else
  std::filesystem::rename(temporary, path, error);
  if (!error) {
    return true;
  }
#endif
  std::filesystem::remove(temporary, error);
  return false;
}

std::optional<std::filesystem::path> Backup(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    return std::nullopt;
  }
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &seconds);
#else
  gmtime_r(&seconds, &utc);
#endif
  char stamp[32];
  std::snprintf(stamp, sizeof(stamp), "%04d%02d%02dT%02d%02d%02d%03dZ", utc.tm_year + 1900,
                utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
                int(milliseconds));
  const auto directory = path.parent_path() / "backups";
  std::filesystem::create_directories(directory, error);
  const auto destination =
      directory / (path.stem().string() + "-" + std::string(stamp) + ".toml");
  std::filesystem::copy_file(path, destination, std::filesystem::copy_options::overwrite_existing,
                             error);
  if (error) {
    return std::nullopt;
  }
  return destination;
}

HostConfig::HostConfig(std::filesystem::path path) : path_(std::move(path)) {}

bool HostConfig::Load() {
  std::error_code error;
  if (!std::filesystem::exists(path_, error) && !error) {
    text_.clear();
    loaded_ = true;
    dirty_ = false;
    return true;
  }
  auto text = ReadFile(path_);
  if (!text) {
    return false;
  }
  text_ = std::move(*text);
  loaded_ = true;
  dirty_ = false;
  return true;
}

void HostConfig::Set(std::string_view name, std::string_view literal) {
  std::string updated = SetValue(text_, name, literal);
  if (updated != text_) {
    text_ = std::move(updated);
    dirty_ = true;
  }
}

void HostConfig::Remove(std::string_view name) {
  std::string updated = RemoveValue(text_, name);
  if (updated != text_) {
    text_ = std::move(updated);
    dirty_ = true;
  }
}

bool HostConfig::Save() {
  if (!dirty_) {
    return true;
  }
  if (!loaded_) {
    return false;
  }
  if (!backed_up_) {
    Backup(path_);
    backed_up_ = true;
  }
  if (!WriteAtomically(path_, text_)) {
    return false;
  }
  dirty_ = false;
  return true;
}

}  // namespace fm4_static_recomp::config
