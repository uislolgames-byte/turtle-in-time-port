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
#include <utility>

namespace {
std::unique_ptr<Rom> loaded;
std::string status = "No ROM loaded. Click Open ROM to inspect a local file.";
constexpr int open_button_id = 1001;
constexpr int probe_button_id = 1002;
constexpr int scene_button_id = 1003;
constexpr const char* scene_class = "TmntSyntheticSceneWindow";
constexpr int previous_stage_id = 2001, next_stage_id = 2002, reset_stage_id = 2003;
HWND scene_window = nullptr;
std::unique_ptr<synthetic_demo::SceneSession> scene_session;
std::string scene_error;
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
void update_scene_controls(HWND window) {
  EnableWindow(GetDlgItem(window,previous_stage_id),scene_session->stage() > 0);
  EnableWindow(GetDlgItem(window,next_stage_id),scene_session->stage()+1 < synthetic_demo::stage_count);
}
void change_scene(HWND window, unsigned stage, bool reset = false) {
  try {
    if (reset) scene_session->reset(); else scene_session->select(stage);
    auto frame = scene_session->frame();
    auto pixels = synthetic_demo::dib_pixels(frame);
    scene = std::move(frame); scene_pixels = std::move(pixels); scene_error.clear();
  } catch (const std::exception& e) { scene_error = std::string("Synthetic scene failed: ")+e.what(); }
  update_scene_controls(window); InvalidateRect(window,nullptr,TRUE);
}
LRESULT CALLBACK scene_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      for (const auto& button : std::vector<std::pair<int,const char*>>{{previous_stage_id,"Previous"},{next_stage_id,"Next"},{reset_stage_id,"Reset"}})
        CreateWindowA("BUTTON",button.second,WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
          12+(button.first-previous_stage_id)*112,70,104,30,window,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(button.first)),GetModuleHandleA(nullptr),nullptr);
      return 0;
    case WM_COMMAND:
      if (LOWORD(wparam) == previous_stage_id && scene_session->stage() > 0) change_scene(window,scene_session->stage()-1);
      if (LOWORD(wparam) == next_stage_id && scene_session->stage()+1 < synthetic_demo::stage_count) change_scene(window,scene_session->stage()+1);
      if (LOWORD(wparam) == reset_stage_id) change_scene(window,synthetic_demo::initial_stage,true);
      return 0;
    case WM_SIZE: InvalidateRect(window,nullptr,TRUE); return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint;
      const HDC dc = BeginPaint(window,&paint);
      RECT client; GetClientRect(window,&client);
      RECT label = {12,12,client.right-12,60};
      const std::string text = scene_error.empty() ? "Synthetic diagnostic - no game running.\nStage "+
        std::to_string(scene_session->stage()+1)+"/"+std::to_string(synthetic_demo::stage_count)+": "+synthetic_demo::stage_name(scene_session->stage()) : scene_error;
      DrawTextA(dc,text.c_str(),-1,&label,DT_LEFT | DT_WORDBREAK);
      RECT area = {12,112,client.right-12,client.bottom-12};
      if (!paint_scene(dc,area)) TextOutA(dc,12,112,"Presentation failed",19);
      EndPaint(window,&paint); return 0;
    }
    case WM_DESTROY: scene_window = nullptr; scene_session.reset(); return 0;
    default: return DefWindowProcA(window,message,wparam,lparam);
  }
}
void open_scene(HWND owner) {
  if (scene_window) { ShowWindow(scene_window,SW_RESTORE); SetForegroundWindow(scene_window); return; }
  try {
    scene_session = std::make_unique<synthetic_demo::SceneSession>();
    scene_error.clear();
    scene = scene_session->frame();
    scene_pixels = synthetic_demo::dib_pixels(scene);
    scene_window = CreateWindowA(scene_class,"Synthetic scene - C++ diagnostic",WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,CW_USEDEFAULT,600,420,owner,nullptr,GetModuleHandleA(nullptr),nullptr);
    if (!scene_window) throw std::runtime_error("Could not create synthetic scene window");
    update_scene_controls(scene_window);
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
  const auto matches_gdi = [&]() {
    bool match = paint_scene(memory,area);
    for (unsigned y = 0; match && y < scene.height; ++y) for (unsigned x = 0; match && x < scene.width; ++x) {
      const auto p = scene.pixels[y*scene.width+x];
      for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
        match = match && GetPixel(memory,static_cast<int>(x)*2+dx,static_cast<int>(y)*2+dy) == RGB(p.r,p.g,p.b);
    }
    return match;
  };
  bool ok = matches_gdi();
  const auto initial_pixels = scene.pixels;
  for (unsigned n = 0; n < synthetic_demo::initial_stage; ++n) SendMessageA(scene_window,WM_COMMAND,previous_stage_id,0);
  ok = ok && scene_session->stage() == 0 && !IsWindowEnabled(GetDlgItem(scene_window,previous_stage_id));
  SendMessageA(scene_window,WM_COMMAND,previous_stage_id,0); // Boundary is safe even for direct commands.
  for (unsigned n = 0; n < synthetic_demo::stage_count; ++n) {
    ok = ok && scene_session->stage() == n && scene_error.empty() && matches_gdi();
    SendMessageA(scene_window,WM_COMMAND,next_stage_id,0);
  }
  ok = ok && scene_session->stage() == synthetic_demo::stage_count-1 && !IsWindowEnabled(GetDlgItem(scene_window,next_stage_id));
  SendMessageA(scene_window,WM_COMMAND,reset_stage_id,0);
  ok = ok && scene_session->stage() == synthetic_demo::initial_stage && scene.pixels == initial_pixels && matches_gdi();
  SelectObject(memory,previous); DeleteObject(bitmap); DeleteDC(memory);
  SetWindowPos(scene_window,nullptr,0,0,180,140,SWP_NOMOVE | SWP_NOZORDER);
  UpdateWindow(scene_window);
  SendMessageA(scene_window,WM_CLOSE,0,0);
  ok = ok && !scene_window && !scene_session && IsWindow(owner);
  SendMessageA(owner,WM_COMMAND,scene_button_id,0);
  ok = ok && scene_window && scene_session->stage() == synthetic_demo::initial_stage && scene.pixels == initial_pixels && status == original_status && !loaded;
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
