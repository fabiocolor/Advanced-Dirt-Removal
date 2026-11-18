#include "recovery_feedback.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <list>
#include <mutex>
#include <system_error>
#include <thread>

namespace recovery_feedback {
namespace {
constexpr wchar_t panelClass[] = L"FabioColor.ADR.RecoveryProgress";
constexpr UINT_PTR refreshTimer = 1;
constexpr int actionButton = 100;

struct Panel {
 std::shared_ptr<State> state;
 HWND owner = nullptr;
 HWND window = nullptr, status = nullptr, details = nullptr, progress = nullptr, button = nullptr;
 HFONT font = nullptr;
 std::chrono::steady_clock::time_point opened = std::chrono::steady_clock::now();
 std::atomic<bool> stop{false}, exited{false};
 std::thread thread;
 bool marquee = false;
};

std::mutex panelsMutex;
std::list<Panel> panels;

bool isBusy(int phase) {
 return phase == State::Queued || phase == State::Running;
}

void refresh(Panel& panel) {
 auto& state = *panel.state;
 const int phase = state.phase.load();
 const int total = state.total.load();
 const int done = std::clamp(state.done.load(), 0, std::max(0, total));
 const bool busy = isBusy(phase);
 const bool cancelling = busy && state.cancel.load();
 const int percent = total > 0 ? static_cast<int>(100LL * done / total) : 0;
 const double elapsed = busy
  ? std::chrono::duration<double>(std::chrono::steady_clock::now() - panel.opened).count()
  : state.seconds.load();

 wchar_t status[128]{};
 switch (phase) {
  case State::Queued:
   std::swprintf(status, 128, L"%ls", cancelling ? L"Cancelling..." : L"Waiting for Resolve to start calculation...");
   break;
  case State::Running:
   std::swprintf(status, 128, L"%ls %d / %d frames (%d%%)", cancelling ? L"Cancelling" : L"Calculating", done, total, percent);
   break;
  case State::Complete:
   std::swprintf(status, 128, L"Complete: %d frames", done);
   break;
  case State::Cancelled:
   std::swprintf(status, 128, L"Cancelled: %d / %d frames saved", done, total);
   break;
  default:
   std::swprintf(status, 128, L"Calculation failed. See the plug-in error.");
 }
 wchar_t details[128]{};
 std::swprintf(details, 128, L"Elapsed: %.1f seconds%ls", elapsed, cancelling ? L" (stopping at a safe boundary)" : L"");

 const bool marquee = phase == State::Queued;
 if (marquee != panel.marquee) {
  panel.marquee = marquee;
  const auto style = GetWindowLongPtrW(panel.progress, GWL_STYLE);
  SetWindowLongPtrW(panel.progress, GWL_STYLE, marquee ? style | PBS_MARQUEE : style & ~PBS_MARQUEE);
  SendMessageW(panel.progress, PBM_SETMARQUEE, marquee, 30);
 }
 if (!marquee) SendMessageW(panel.progress, PBM_SETPOS, percent, 0);
 SetWindowTextW(panel.status, status);
 SetWindowTextW(panel.details, details);
 SetWindowTextW(panel.button, busy ? L"Cancel" : L"Close");
 EnableWindow(panel.button, !cancelling);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
 if (message == WM_NCCREATE) {
  const auto* info = reinterpret_cast<CREATESTRUCTW*>(lParam);
  SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(info->lpCreateParams));
 }
 auto* panel = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
 if (panel) {
  switch (message) {
   case WM_COMMAND:
    if (LOWORD(wParam) != actionButton || HIWORD(wParam) != BN_CLICKED) break;
    [[fallthrough]];
   case WM_CLOSE:
    if (isBusy(panel->state->phase.load())) {
     panel->state->cancel = true;
     refresh(*panel);
    } else {
     DestroyWindow(window);
    }
    return 0;
   case WM_TIMER:
    if (panel->stop.load()) DestroyWindow(window);
    else refresh(*panel);
    return 0;
   case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
 }
 return DefWindowProcW(window, message, wParam, lParam);
}

HINSTANCE module() {
 HMODULE instance = nullptr;
 GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(panelClass), &instance);
 return instance;
}

// Run the Win32 message loop independently so a synchronous host render can
// neither freeze the progress bar nor prevent a cancel request.
void runPanel(Panel& panel) {
 const HINSTANCE instance = module();
 INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS};
 if (!instance || !InitCommonControlsEx(&controls)) return;
 WNDCLASSW windowClass{};
 windowClass.lpfnWndProc = windowProc;
 windowClass.hInstance = instance;
 windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
 windowClass.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
 windowClass.lpszClassName = panelClass;
 if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;

 const HWND owner = IsWindow(panel.owner) ? panel.owner : nullptr;
 const UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
 const auto px = [dpi](int size) { return MulDiv(size, dpi ? dpi : 96, 96); };
 const DWORD style = WS_CAPTION | WS_SYSMENU;
 const DWORD extraStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
 RECT size{0, 0, px(470), px(144)};
 AdjustWindowRectExForDpi(&size, style, FALSE, extraStyle, dpi ? dpi : 96);
 RECT anchor{};
 if (!owner || !GetWindowRect(owner, &anchor)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &anchor, 0);
 const int width = size.right - size.left, height = size.bottom - size.top;
 wchar_t title[128]{};
 MultiByteToWideChar(CP_UTF8, 0, panel.state->title.c_str(), -1, title, 128);
 panel.window = CreateWindowExW(extraStyle, panelClass, title, style,
  anchor.left + (anchor.right - anchor.left - width) / 2,
  anchor.top + (anchor.bottom - anchor.top - height) / 2,
  width, height, owner, nullptr, instance, &panel);
 if (!panel.window) return;

 const auto child = [&](const wchar_t* cls, DWORD flags, int x, int y, int w, int h, int id = 0) {
  return CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE | flags,
   px(x), px(y), px(w), px(h), panel.window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
 };
 panel.status = child(L"STATIC", SS_NOPREFIX, 16, 16, 438, 22);
 panel.progress = child(PROGRESS_CLASSW, 0, 16, 43, 438, 15);
 panel.details = child(L"STATIC", SS_NOPREFIX, 16, 69, 438, 22);
 panel.button = child(L"BUTTON", BS_PUSHBUTTON, 354, 100, 100, 28, actionButton);
 if (panel.status && panel.details && panel.progress && panel.button &&
     SetTimer(panel.window, refreshTimer, 100, nullptr)) {
  panel.font = CreateFontW(-px(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
   DEFAULT_PITCH, L"Segoe UI");
  for (HWND label : {panel.status, panel.details, panel.button})
   SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(panel.font), FALSE);
  SendMessageW(panel.progress, PBM_SETRANGE32, 0, 100);
  refresh(panel);
  ShowWindow(panel.window, SW_SHOWNOACTIVATE);
  panel.state->presented = IsWindowVisible(panel.window) != FALSE;

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
   TranslateMessage(&message);
   DispatchMessageW(&message);
  }
 }
 if (IsWindow(panel.window)) DestroyWindow(panel.window);
 if (panel.font) DeleteObject(panel.font);
 panel.state->presented = false;
}
}

void show(const std::shared_ptr<State>& state) {
 HWND owner = GetActiveWindow();
 if (!owner) owner = GetForegroundWindow();
 DWORD pid = 0;
 GetWindowThreadProcessId(owner, &pid);
 if (pid != GetCurrentProcessId()) return;

 std::lock_guard<std::mutex> lock(panelsMutex);
 for (auto it = panels.begin(); it != panels.end();) {
  if (it->exited.load()) {
   it->thread.join();
   it = panels.erase(it);
  } else {
   if (!isBusy(it->state->phase.load())) it->stop = true;
   ++it;
  }
 }
 panels.emplace_back();
 auto& panel = panels.back();
 panel.state = state;
 panel.owner = owner;
 try {
  panel.thread = std::thread([&panel] {
   runPanel(panel);
   panel.exited = true;
  });
 } catch (const std::system_error&) {
  panels.pop_back();
 }
}

void shutdown() {
 std::lock_guard<std::mutex> lock(panelsMutex);
 for (auto& panel : panels) panel.stop = true;
 for (auto& panel : panels) panel.thread.join();
 panels.clear();
 if (auto instance = module()) UnregisterClassW(panelClass, instance);
}
}
