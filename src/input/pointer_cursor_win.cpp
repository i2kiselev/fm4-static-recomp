// Copyright (c) 2026, i2kiselev
// BSD 3-Clause License: see LICENSE in the repository root.
#include "input/pointer_cursor.h"

#include <cstring>

#include <rex/logging.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fm4_static_recomp::input {

#ifdef _WIN32
namespace {

// The window's cursor is SDL's, and that SDL lives in the runtime DLL (the game's own copy of SDL cannot reach
// the window). It shows the system arrow at many moments (each WM_SETCURSOR, coming back from the idle auto-hide,
// focus and enter events), so the pointer goes in where it sets it: the runtime DLL's import of SetCursor is
// pointed at ours, which turns the system arrow into the pointer. Every other cursor, and none (hidden: mouse
// look, the idle auto-hide), passes through as it was.
constexpr const wchar_t* kRuntimeDll = L"rexruntime.dll";
HCURSOR g_cursor = nullptr;
HCURSOR g_arrow = nullptr;
using SetCursorFn = HCURSOR(WINAPI*)(HCURSOR);
SetCursorFn g_set_cursor = nullptr;

HCURSOR WINAPI PointerSetCursor(HCURSOR cursor) {
  return g_set_cursor(cursor == g_arrow && g_cursor ? g_cursor : cursor);
}

// The import address table slot of user32's SetCursor in `module`, or nullptr.
void** FindSetCursorImport(HMODULE module) {
  auto* base = reinterpret_cast<uint8_t*>(module);
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress) return nullptr;
  for (auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); desc->Name; ++desc) {
    if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), "user32.dll") != 0) continue;
    auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(
        base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++slots) {
      if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
      const auto* by_name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), "SetCursor") == 0) {
        return reinterpret_cast<void**>(&slots->u1.Function);
      }
    }
  }
  return nullptr;
}

HCURSOR MakeCursor(const PointerImage& image) {
  BITMAPV5HEADER header = {};
  header.bV5Size = sizeof(header);
  header.bV5Width = image.size;
  header.bV5Height = -image.size;  // top down
  header.bV5Planes = 1;
  header.bV5BitCount = 32;
  header.bV5Compression = BI_BITFIELDS;
  header.bV5RedMask = 0x00FF0000;
  header.bV5GreenMask = 0x0000FF00;
  header.bV5BlueMask = 0x000000FF;
  header.bV5AlphaMask = 0xFF000000;
  void* bits = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (!color || !bits) return nullptr;
  auto* pixels = static_cast<uint32_t*>(bits);
  for (size_t i = 0; i < image.argb.size(); ++i) pixels[i] = image.argb[i];  // BGRA in memory: ARGB words
  HBITMAP mask = CreateBitmap(image.size, image.size, 1, 1, nullptr);
  ICONINFO info = {};
  info.fIcon = FALSE;
  info.xHotspot = DWORD(image.hot_x);
  info.yHotspot = DWORD(image.hot_y);
  info.hbmMask = mask;
  info.hbmColor = color;
  HCURSOR cursor = reinterpret_cast<HCURSOR>(CreateIconIndirect(&info));
  DeleteObject(color);
  if (mask) DeleteObject(mask);
  return cursor;
}

}  // namespace

bool InstallPointerCursor(void* native_window, int window_height) {
  if (g_set_cursor) return false;
  HMODULE runtime = GetModuleHandleW(kRuntimeDll);
  void** slot = runtime ? FindSetCursorImport(runtime) : nullptr;
  if (!slot) {
    REXLOG_WARN("Mouse pointer: no SetCursor import in the runtime DLL; keeping the system arrow");
    return false;
  }
  const PointerImage image = DrawPointer(PointerSize(window_height));
  g_cursor = MakeCursor(image);
  if (!g_cursor) {
    REXLOG_WARN("Mouse pointer: cannot make it ({}); keeping the system arrow", GetLastError());
    return false;
  }
  g_arrow = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512))  /* IDC_ARROW */;
  DWORD old_protect = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect)) {
    DestroyCursor(g_cursor);
    g_cursor = nullptr;
    return false;
  }
  g_set_cursor = reinterpret_cast<SetCursorFn>(*slot);
  *slot = reinterpret_cast<void*>(&PointerSetCursor);
  VirtualProtect(slot, sizeof(void*), old_protect, &old_protect);
  // The arrow on screen now becomes the pointer at the next WM_SETCURSOR.
  if (HWND window = static_cast<HWND>(native_window)) {
    PostMessageW(window, WM_SETCURSOR, WPARAM(window), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
  }
  REXLOG_INFO("Mouse pointer: {} px", image.size);
  return true;
}
#else
bool InstallPointerCursor(void*, int) { return false; }
#endif

}  // namespace fm4_static_recomp::input
