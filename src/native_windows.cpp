#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include "rom.hpp"
#include <memory>
#include <sstream>
#include <string>

namespace {
std::unique_ptr<Rom> loaded;
std::string status = "No ROM loaded. Click Open ROM to inspect a local file.";
constexpr int open_button_id = 1001;
void open_rom(HWND window) {
  char filename[MAX_PATH] = {};
  OPENFILENAMEA dialog = {};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window;
  dialog.lpstrFilter = "SNES ROM (*.sfc;*.smc)\0*.sfc;*.smc\0All files (*.*)\0*.*\0";
  dialog.lpstrFile = filename;
  dialog.nMaxFile = MAX_PATH;
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameA(&dialog)) return;
  try {
    auto next = std::make_unique<Rom>(filename);
    std::ostringstream out;
    out << "Local LoROM loaded (" << next->data.size() << " bytes).\n"
        << "RESET vector: $" << std::hex << std::uppercase << next->reset() << "\n"
        << "Header checksum pair: "
        << (next->checksum_pair_valid() ? "consistent" : "not consistent") << "\n"
        << "Execution and rendering are not implemented.";
    loaded = std::move(next);
    status = out.str();
  } catch (const std::exception& e) {
    loaded.reset();
    status = std::string("Could not load ROM: ") + e.what();
  }
  InvalidateRect(window, nullptr, TRUE);
}
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      CreateWindowA("BUTTON", "Open ROM...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    20, 20, 140, 32, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(open_button_id)),
                    GetModuleHandleA(nullptr), nullptr);
      return 0;
    case WM_COMMAND:
      if (LOWORD(wparam) == open_button_id) open_rom(window);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint;
      HDC dc = BeginPaint(window, &paint);
      RECT area = {20, 75, 750, 440};
      std::string display = "TMNT Native - ROM inspection prototype\n\n" + status;
      DrawTextA(dc, display.c_str(), -1, &area, DT_LEFT | DT_TOP | DT_WORDBREAK);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcA(window, message, wparam, lparam);
  }
}
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
  WNDCLASSA wc = {};
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.lpszClassName = "TmntNativePrototypeWindow";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (!RegisterClassA(&wc)) return 1;
  HWND window = CreateWindowA(wc.lpszClassName, "TMNT Native - ROM Prototype",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              800, 500, nullptr, nullptr, instance, nullptr);
  if (!window) return 1;
  ShowWindow(window, show);
  UpdateWindow(window);
  MSG message;
  while (GetMessageA(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageA(&message);
  }
  return static_cast<int>(message.wParam);
}
