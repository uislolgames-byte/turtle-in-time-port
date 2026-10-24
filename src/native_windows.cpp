#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include "rom.hpp"
#include "cpu_probe.hpp"
#include "../demo/presentation.hpp"
#include <memory>
#include <sstream>
#include <string>

namespace {
std::unique_ptr<Rom> loaded;
std::string status = "No ROM loaded. Click Open ROM to inspect a local file.";
constexpr int open_button_id = 1001;
constexpr int probe_button_id = 1002;
constexpr int scene_button_id = 1003;
constexpr const char* scene_class = "TmntSyntheticSceneWindow";
HWND scene_window = nullptr;
system816::PreviewFrame scene;
std::vector<uint8_t> scene_pixels;
bool paint_scene(HDC dc, const RECT& area) {
  const auto place = synthetic_demo::fit(area.right-area.left,area.bottom-area.top,scene.width,scene.height);
  if (!place.width || !place.height) return true;
  BITMAPINFO bitmap = {};
  bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap.bmiHeader.biWidth = static_cast<LONG>(scene.width);
  bitmap.bmiHeader.biHeight = -static_cast<LONG>(scene.height); // top-down
  bitmap.bmiHeader.biPlanes = 1;
  bitmap.bmiHeader.biBitCount = 32;
  bitmap.bmiHeader.biCompression = BI_RGB;
  const int old_mode = SetStretchBltMode(dc,COLORONCOLOR);
  const int result = StretchDIBits(dc,area.left+place.x,area.top+place.y,place.width,place.height,
    0,0,static_cast<int>(scene.width),static_cast<int>(scene.height),scene_pixels.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
  if (old_mode) SetStretchBltMode(dc,old_mode);
  return result != 0 && result != static_cast<int>(GDI_ERROR);
}
LRESULT CALLBACK scene_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_SIZE: InvalidateRect(window,nullptr,TRUE); return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint;
      const HDC dc = BeginPaint(window,&paint);
      RECT client; GetClientRect(window,&client);
      RECT label = {12,12,client.right-12,60};
      DrawTextA(dc,"Original synthetic CPU/DMA scene - diagnostic snapshot. No game running.",-1,&label,DT_LEFT | DT_WORDBREAK);
      RECT area = {12,70,client.right-12,client.bottom-12};
      if (!paint_scene(dc,area)) TextOutA(dc,12,70,"Presentation failed",19);
      EndPaint(window,&paint); return 0;
    }
    case WM_DESTROY: scene_window = nullptr; return 0;
    default: return DefWindowProcA(window,message,wparam,lparam);
  }
}
void open_scene(HWND owner) {
  if (scene_window) { ShowWindow(scene_window,SW_RESTORE); SetForegroundWindow(scene_window); return; }
  try {
    scene = synthetic_demo::scene_frame();
    scene_pixels = synthetic_demo::dib_pixels(scene);
    scene_window = CreateWindowA(scene_class,"Synthetic scene - C++ diagnostic",WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,CW_USEDEFAULT,600,420,owner,nullptr,GetModuleHandleA(nullptr),nullptr);
    if (!scene_window) throw std::runtime_error("Could not create synthetic scene window");
    ShowWindow(scene_window,SW_SHOW); UpdateWindow(scene_window);
  } catch (const std::exception& e) {
    status = std::string("Synthetic scene failed: ")+e.what(); InvalidateRect(owner,nullptr,TRUE);
  }
}
// CI exercises the real button/paint/lifecycle path plus GDI color/orientation readback.
// This is an automated smoke check, not an interactive user acceptance test.
bool test_scene_window(HWND owner) {
  const std::string original_status = status;
  SendMessageA(owner,WM_COMMAND,scene_button_id,0);
  if (!scene_window || scene.width != 128 || scene.height != 64) return false;
  const HWND first = scene_window;
  SendMessageA(owner,WM_COMMAND,scene_button_id,0);
  if (scene_window != first) return false;
  const HDC screen = GetDC(owner);
  const HDC memory = CreateCompatibleDC(screen);
  const HBITMAP bitmap = CreateCompatibleBitmap(screen,256,128);
  ReleaseDC(owner,screen);
  if (!memory || !bitmap) { if (memory) DeleteDC(memory); if (bitmap) DeleteObject(bitmap); return false; }
  const HGDIOBJ previous = SelectObject(memory,bitmap);
  const RECT area = {0,0,256,128};
  bool ok = paint_scene(memory,area);
  for (unsigned y = 0; ok && y < scene.height; ++y) for (unsigned x = 0; ok && x < scene.width; ++x) {
    const auto p = scene.pixels[y*scene.width+x];
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
      ok = ok && GetPixel(memory,static_cast<int>(x)*2+dx,static_cast<int>(y)*2+dy) == RGB(p.r,p.g,p.b);
  }
  SelectObject(memory,previous); DeleteObject(bitmap); DeleteDC(memory);
  SetWindowPos(scene_window,nullptr,0,0,180,140,SWP_NOMOVE | SWP_NOZORDER);
  UpdateWindow(scene_window);
  SendMessageA(scene_window,WM_CLOSE,0,0);
  ok = ok && !scene_window && IsWindow(owner);
  SendMessageA(owner,WM_COMMAND,scene_button_id,0);
  ok = ok && scene_window && status == original_status && !loaded;
  if (scene_window) SendMessageA(scene_window,WM_CLOSE,0,0);
  MSG pending;
  if (PeekMessageA(&pending,nullptr,WM_QUIT,WM_QUIT,PM_NOREMOVE)) ok = false;
  return ok;
}
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
        << "Game execution and rendering are not implemented.";
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
      CreateWindowA("BUTTON", "Probe CPU", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    175, 20, 140, 32, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(probe_button_id)),
                    GetModuleHandleA(nullptr), nullptr);
      CreateWindowA("BUTTON", "Synthetic scene", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    330, 20, 160, 32, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(scene_button_id)),
                    GetModuleHandleA(nullptr), nullptr);
      return 0;
    case WM_COMMAND:
      if (LOWORD(wparam) == scene_button_id) open_scene(window);
      if (LOWORD(wparam) == open_button_id) open_rom(window);
      if (LOWORD(wparam) == probe_button_id && loaded) {
        status = format_cpu_probe(probe_reset(*loaded));
        InvalidateRect(window, nullptr, TRUE);
      }
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
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR command, int show) {
  WNDCLASSA wc = {};
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.lpszClassName = "TmntNativePrototypeWindow";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  if (!RegisterClassA(&wc)) return 1;
  WNDCLASSA preview = wc;
  preview.lpfnWndProc = scene_proc;
  preview.lpszClassName = scene_class;
  if (!RegisterClassA(&preview)) return 1;
  HWND window = CreateWindowA(wc.lpszClassName, "TMNT Native - ROM Prototype",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              800, 500, nullptr, nullptr, instance, nullptr);
  if (!window) return 1;
  ShowWindow(window, show);
  UpdateWindow(window);
  if (std::string(command) == "--self-test-window") {
    const bool ok = test_scene_window(window);
    DestroyWindow(window);
    return ok ? 0 : 1;
  }
  MSG message = {};
  while (GetMessageA(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageA(&message);
  }
  return static_cast<int>(message.wParam);
}
