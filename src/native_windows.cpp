#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include "rom.hpp"
#include "cpu_probe.hpp"
#include "../demo/presentation.hpp"
#include "../demo/input_scene.hpp"
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
bool paint_frame(HDC dc, const RECT& area, const system816::PreviewFrame& frame, const std::vector<uint8_t>& pixels) {
  const auto place = synthetic_demo::fit(area.right-area.left,area.bottom-area.top,frame.width,frame.height);
  if (!place.width || !place.height) return true;
  BITMAPINFO bitmap = {};
  bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap.bmiHeader.biWidth = static_cast<LONG>(frame.width);
  bitmap.bmiHeader.biHeight = -static_cast<LONG>(frame.height); // top-down
  bitmap.bmiHeader.biPlanes = 1;
  bitmap.bmiHeader.biBitCount = 32;
  bitmap.bmiHeader.biCompression = BI_RGB;
  const int old_mode = SetStretchBltMode(dc,COLORONCOLOR);
  const int result = StretchDIBits(dc,area.left+place.x,area.top+place.y,place.width,place.height,
    0,0,static_cast<int>(frame.width),static_cast<int>(frame.height),pixels.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
  if (old_mode) SetStretchBltMode(dc,old_mode);
  return result != 0 && result != static_cast<int>(GDI_ERROR);
}
bool paint_scene(HDC dc, const RECT& area) { return paint_frame(dc,area,scene,scene_pixels); }

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
constexpr int input_button_id = 1004, blank_input_id = 3001, dim_input_id = 3002, reset_input_id = 3003;
constexpr const char* input_class = "TmntSyntheticInputWindow";
HWND input_window = nullptr;
std::unique_ptr<synthetic_demo::InputScene> input_scene;
std::vector<uint8_t> input_pixels;
unsigned keyboard_buttons = 0, toggle_buttons = 0;
std::string input_error;
void refresh_input(HWND window, bool reset = false) {
  try {
    if (reset) input_scene->reset(); else input_scene->set_buttons(keyboard_buttons | toggle_buttons);
    input_pixels = synthetic_demo::dib_pixels(input_scene->frame()); input_error.clear();
  } catch (const std::exception& e) { input_error = e.what(); }
  SendMessageA(GetDlgItem(window,blank_input_id),BM_SETCHECK,(input_scene->buttons() & 1) ? BST_CHECKED : BST_UNCHECKED,0);
  SendMessageA(GetDlgItem(window,dim_input_id),BM_SETCHECK,(input_scene->buttons() & 2) ? BST_CHECKED : BST_UNCHECKED,0);
  InvalidateRect(window,nullptr,TRUE);
}
bool input_key(HWND window, WPARAM key, bool down) {
  const unsigned bit = key == 'Z' ? 1 : key == 'X' ? 2 : 0;
  if (!bit) return false;
  const unsigned next = down ? keyboard_buttons | bit : keyboard_buttons & ~bit;
  if (next != keyboard_buttons) { keyboard_buttons = next; refresh_input(window); }
  return true;
}
LRESULT CALLBACK input_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      for (const auto& button : std::vector<std::pair<int,const char*>>{{blank_input_id,"B: blank [Z]"},{dim_input_id,"Y: dim [X]"},{reset_input_id,"Reset"}})
        CreateWindowA("BUTTON",button.second,WS_CHILD | WS_VISIBLE |
          (button.first == reset_input_id ? BS_PUSHBUTTON : BS_AUTOCHECKBOX | BS_PUSHLIKE),
          12+(button.first-blank_input_id)*150,70,142,30,window,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(button.first)),GetModuleHandleA(nullptr),nullptr);
      return 0;
    case WM_COMMAND:
      if (HIWORD(wparam) != BN_CLICKED) return 0;
      if (LOWORD(wparam) == blank_input_id || LOWORD(wparam) == dim_input_id) {
        const unsigned bit = LOWORD(wparam) == blank_input_id ? 1 : 2;
        if (SendMessageA(GetDlgItem(window,LOWORD(wparam)),BM_GETCHECK,0,0) == BST_CHECKED) toggle_buttons |= bit;
        else toggle_buttons &= ~bit;
        refresh_input(window);
      }
      if (LOWORD(wparam) == reset_input_id) { keyboard_buttons = toggle_buttons = 0; refresh_input(window,true); }
      return 0;
    case WM_KEYDOWN: if (input_key(window,wparam,true)) return 0; break;
    case WM_KEYUP: if (input_key(window,wparam,false)) return 0; break;
    case WM_ACTIVATE:
      if (LOWORD(wparam) == WA_INACTIVE && keyboard_buttons) { keyboard_buttons = 0; refresh_input(window); }
      return 0;
    case WM_SIZE: InvalidateRect(window,nullptr,TRUE); return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint; const HDC dc = BeginPaint(window,&paint); RECT client; GetClientRect(window,&client);
      RECT label = {12,12,client.right-12,60};
      const std::string text = input_error.empty() ? "Original input diagnostic - no game running.\nHold Z (B) to blank, X (Y) to dim; buttons toggle the same inputs." : input_error;
      DrawTextA(dc,text.c_str(),-1,&label,DT_LEFT | DT_WORDBREAK);
      const RECT area = {12,112,client.right-12,client.bottom-12};
      if (!paint_frame(dc,area,input_scene->frame(),input_pixels)) TextOutA(dc,12,112,"Presentation failed",19);
      EndPaint(window,&paint); return 0;
    }
    case WM_DESTROY:
      input_window = nullptr; input_scene.reset(); input_pixels.clear(); keyboard_buttons = toggle_buttons = 0; return 0;
  }
  return DefWindowProcA(window,message,wparam,lparam);
}
// Forward Z/X even when a child button has focus, without polling global key state.
bool input_message(const MSG& message) {
  if (!input_window || !(message.hwnd == input_window || IsChild(input_window,message.hwnd)) ||
      (message.message != WM_KEYDOWN && message.message != WM_KEYUP) || (message.wParam != 'Z' && message.wParam != 'X')) return false;
  SendMessageA(input_window,message.message,message.wParam,message.lParam); return true;
}
void open_input(HWND owner) {
  if (input_window) { ShowWindow(input_window,SW_RESTORE); SetForegroundWindow(input_window); return; }
  try {
    input_scene = std::make_unique<synthetic_demo::InputScene>(); input_pixels = synthetic_demo::dib_pixels(input_scene->frame());
    keyboard_buttons = toggle_buttons = 0; input_error.clear();
    input_window = CreateWindowA(input_class,"Synthetic input - C++ diagnostic",WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,CW_USEDEFAULT,600,420,owner,nullptr,GetModuleHandleA(nullptr),nullptr);
    if (!input_window) throw std::runtime_error("Could not create input diagnostic window");
    ShowWindow(input_window,SW_SHOW); UpdateWindow(input_window);
  } catch (const std::exception& e) { status = std::string("Input diagnostic failed: ")+e.what(); InvalidateRect(owner,nullptr,TRUE); }
}
bool test_input_window(HWND owner) {
  const std::string original_status = status;
  SendMessageA(owner,WM_COMMAND,input_button_id,0);
  if (!input_window || !input_scene) return false;
  const HWND first = input_window; const auto released = input_scene->frame().pixels;
  SendMessageA(owner,WM_COMMAND,input_button_id,0); if (input_window != first) return false;
  const HDC screen = GetDC(owner), memory = CreateCompatibleDC(screen); const HBITMAP bitmap = CreateCompatibleBitmap(screen,256,128); ReleaseDC(owner,screen);
  if (!memory || !bitmap) { if (memory) DeleteDC(memory); if (bitmap) DeleteObject(bitmap); return false; }
  const HGDIOBJ old = SelectObject(memory,bitmap); const RECT area = {0,0,256,128};
  const auto matches = [&]() {
    if (!input_error.empty() || !paint_frame(memory,area,input_scene->frame(),input_pixels)) return false;
    for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 128; ++x) {
      const auto pixel = input_scene->frame().pixels[y*128+x];
      for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
        if (GetPixel(memory,static_cast<int>(x)*2+dx,static_cast<int>(y)*2+dy) != RGB(pixel.r,pixel.g,pixel.b)) return false;
    }
    return true;
  };
  bool ok = matches() && input_scene->buttons() == 0;
  SendMessageA(input_window,WM_KEYDOWN,'Z',0); ok = ok && input_scene->trace().display == 128 && matches();
  const auto renders = input_scene->renders(); SendMessageA(input_window,WM_KEYDOWN,'Z',static_cast<LPARAM>(1u<<30)); ok = ok && input_scene->renders() == renders;
  SendMessageA(input_window,WM_KEYDOWN,'X',0); ok = ok && input_scene->buttons() == 3 && input_scene->trace().display == 128 && matches();
  SendMessageA(input_window,WM_KEYUP,'Z',0); ok = ok && input_scene->buttons() == 2 && input_scene->trace().display == 7 && matches();
  SendMessageA(input_window,WM_ACTIVATE,WA_INACTIVE,0); ok = ok && input_scene->buttons() == 0 && input_scene->frame().pixels == released && matches();
  SendMessageA(GetDlgItem(input_window,dim_input_id),BM_CLICK,0,0); ok = ok && input_scene->buttons() == 2 && matches();
  SendMessageA(input_window,WM_KEYDOWN,'Z',0); SendMessageA(input_window,WM_KEYUP,'Z',0); ok = ok && input_scene->buttons() == 2;
  SendMessageA(input_window,WM_ACTIVATE,WA_INACTIVE,0); ok = ok && input_scene->buttons() == 2;
  SendMessageA(GetDlgItem(input_window,reset_input_id),BM_CLICK,0,0); ok = ok && input_scene->buttons() == 0 && input_scene->frame().pixels == released && matches();
  MSG child = {}; child.hwnd = GetDlgItem(input_window,blank_input_id); child.message = WM_KEYDOWN; child.wParam = 'X';
  ok = ok && input_message(child) && input_scene->buttons() == 2; child.message = WM_KEYUP;
  ok = ok && input_message(child) && input_scene->buttons() == 0;
  child.hwnd = owner; child.message = WM_KEYDOWN; ok = ok && !input_message(child) && input_scene->buttons() == 0;
  SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory);
  SendMessageA(input_window,WM_CLOSE,0,0); ok = ok && !input_window && !input_scene && IsWindow(owner);
  SendMessageA(owner,WM_COMMAND,input_button_id,0); ok = ok && input_window && input_scene->buttons() == 0 && input_scene->frame().pixels == released && status == original_status && !loaded;
  if (input_window) SendMessageA(input_window,WM_CLOSE,0,0);
  MSG pending; if (PeekMessageA(&pending,nullptr,WM_QUIT,WM_QUIT,PM_NOREMOVE)) ok = false;
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
      CreateWindowA("BUTTON", "Input demo", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    505, 20, 140, 32, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(input_button_id)),GetModuleHandleA(nullptr),nullptr);
      return 0;
    case WM_COMMAND:
      if (LOWORD(wparam) == input_button_id) open_input(window);
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
  WNDCLASSA inputs = wc; inputs.lpfnWndProc = input_proc; inputs.lpszClassName = input_class;
  if (!RegisterClassA(&inputs)) return 1;
  HWND window = CreateWindowA(wc.lpszClassName, "TMNT Native - ROM Prototype",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              800, 500, nullptr, nullptr, instance, nullptr);
  if (!window) return 1;
  ShowWindow(window, show);
  UpdateWindow(window);
  if (std::string(command) == "--self-test-window" || std::string(command) == "--self-test-input-window") {
    const bool ok = std::string(command) == "--self-test-window" ? test_scene_window(window) : test_input_window(window);
    DestroyWindow(window);
    return ok ? 0 : 1;
  }
  MSG message = {};
  while (GetMessageA(&message, nullptr, 0, 0) > 0) {
    if (input_message(message)) continue;
    TranslateMessage(&message);
    DispatchMessageA(&message);
  }
  return static_cast<int>(message.wParam);
}
