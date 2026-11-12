#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#endif
#include "KerrSpacetime.h"
#include "ObjectEditor.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <utility>

#ifdef _WIN32
struct ObjectEditorWin32 {
  static LRESULT CALLBACK ControlProcedure(HWND control, UINT message,
                                           WPARAM key, LPARAM detail, UINT_PTR,
                                           DWORD_PTR data) {
    auto &editor = *reinterpret_cast<ObjectEditor *>(data);
    if (message == WM_KEYDOWN) {
      if (key == VK_ESCAPE || key == VK_F5) {
        editor.Hide();
        return 0;
      }
      if (key == VK_RETURN) {
        if (GetDlgCtrlID(control) == ObjectEditor::SpinValue)
          editor.ApplyText();
        else
          SendMessageW(static_cast<HWND>(editor.window), WM_COMMAND,
                       GetDlgCtrlID(control),
                       reinterpret_cast<LPARAM>(control));
        return 0;
      }
      if (key == VK_TAB) {
        const auto next = GetNextDlgTabItem(static_cast<HWND>(editor.window),
                                            control, GetKeyState(VK_SHIFT) < 0);
        if (next)
          SetFocus(next);
        return 0;
      }
    }
    if (message == WM_CHAR &&
        (key == VK_RETURN || key == VK_ESCAPE || key == VK_TAB))
      return 0;
    return DefSubclassProc(control, message, key, detail);
  }
  static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam,
                                    LPARAM lParam) {
    auto *editor = reinterpret_cast<ObjectEditor *>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      editor = static_cast<ObjectEditor *>(
          reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
      editor->window = window;
      SetWindowLongPtrW(window, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(editor));
    }
    if (!editor)
      return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE:
      editor->BuildControls();
      return 0;
    case WM_CLOSE:
      editor->Hide();
      return 0;
    case WM_SIZE:
      editor->Layout();
      return 0;
    case WM_HSCROLL:
      if (!editor->syncing && editor->editable &&
          reinterpret_cast<HWND>(lParam) ==
              GetDlgItem(window, ObjectEditor::SpinSlider))
        editor->ApplyValue(SendDlgItemMessageW(window, ObjectEditor::SpinSlider,
                                               TBM_GETPOS, 0, 0) /
                           10000.0);
      return 0;
    case WM_COMMAND:
      if (HIWORD(wParam) == BN_CLICKED) {
        switch (LOWORD(wParam)) {
        case ObjectEditor::Apply:
          editor->ApplyText();
          break;
        case ObjectEditor::Reset:
          editor->ApplyValue(.8);
          break;
        case ObjectEditor::Decrease:
          editor->ApplyValue(std::max(-.98, editor->spin - .01));
          break;
        case ObjectEditor::Increase:
          editor->ApplyValue(std::min(.98, editor->spin + .01));
          break;
        case ObjectEditor::Close:
          editor->Hide();
          break;
        }
      }
      return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
      SetTextColor(reinterpret_cast<HDC>(wParam), RGB(224, 232, 242));
      SetBkColor(reinterpret_cast<HDC>(wParam), RGB(19, 24, 33));
      return reinterpret_cast<LRESULT>(editor->brush);
    case WM_ERASEBKGND: {
      RECT r;
      GetClientRect(window, &r);
      FillRect(reinterpret_cast<HDC>(wParam), &r,
               static_cast<HBRUSH>(editor->brush));
      return 1;
    }
    case WM_NCDESTROY:
      SetWindowLongPtrW(window, GWLP_USERDATA, 0);
      break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
  }
};
#endif

bool ObjectEditor::Create(void *nativeOwner,
                          std::function<void(double)> callback) {
  owner = nativeOwner;
  applySpin = std::move(callback);
#ifdef _WIN32
  INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_BAR_CLASSES};
  if (!InitCommonControlsEx(&controls))
    return false;
  WNDCLASSW type = {};
  type.lpfnWndProc = ObjectEditorWin32::Procedure;
  type.hInstance = GetModuleHandleW(nullptr);
  type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  type.lpszClassName = L"RenderEngineObjectEditor";
  if (!RegisterClassW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;
  const auto parent = static_cast<HWND>(owner);
  const int dpi = GetDpiForWindow(parent);
  RECT rect = {0, 0, MulDiv(392, dpi, 96), MulDiv(350, dpi, 96)}, parentRect;
  GetWindowRect(parent, &parentRect);
  constexpr DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
  constexpr DWORD extended = WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT;
  AdjustWindowRectEx(&rect, style, FALSE, extended);
  window = CreateWindowExW(
      extended, type.lpszClassName, L"Object editor", style,
      std::max(parentRect.left,
               parentRect.right - (rect.right - rect.left) - 16),
      parentRect.top + 48, rect.right - rect.left, rect.bottom - rect.top,
      parent, nullptr, type.hInstance, this);
  return window != nullptr;
#else
  return false;
#endif
}
void ObjectEditor::BuildControls() {
#ifdef _WIN32
  const auto native = static_cast<HWND>(window);
  brush = CreateSolidBrush(RGB(19, 24, 33));
  font = CreateFontW(-MulDiv(10, GetDpiForWindow(native), 72), 0, 0, 0,
                     FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     DEFAULT_PITCH, L"Segoe UI");
  const auto add = [&](const wchar_t *type, const wchar_t *text, int id,
                       DWORD style) {
    const auto child = CreateWindowExW(
        0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, native,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SetWindowSubclass(child, ObjectEditorWin32::ControlProcedure, 1,
                      reinterpret_cast<DWORD_PTR>(this));
  };
  add(L"STATIC", L"Selected object", ObjectName, SS_LEFT);
  add(L"STATIC", L"Dimensionless spin (a/M): -0.98 to +0.98", 4111, SS_LEFT);
  add(L"EDIT", L"0.800000", SpinValue, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
  add(L"BUTTON", L"Apply", Apply, BS_PUSHBUTTON | WS_TABSTOP);
  add(TRACKBAR_CLASSW, L"", SpinSlider, TBS_HORZ | TBS_NOTICKS | WS_TABSTOP);
  SendDlgItemMessageW(native, SpinSlider, TBM_SETRANGEMIN, FALSE, -9800);
  SendDlgItemMessageW(native, SpinSlider, TBM_SETRANGEMAX, TRUE, 9800);
  SendDlgItemMessageW(native, SpinSlider, TBM_SETLINESIZE, 0, 1);
  SendDlgItemMessageW(native, SpinSlider, TBM_SETPAGESIZE, 0, 100);
  SendDlgItemMessageW(native, SpinValue, EM_SETLIMITTEXT, 40, 0);
  add(L"BUTTON", L"-0.01", Decrease, BS_PUSHBUTTON | WS_TABSTOP);
  add(L"BUTTON", L"+0.01", Increase, BS_PUSHBUTTON | WS_TABSTOP);
  add(L"BUTTON", L"Reset to 0.8", Reset, BS_PUSHBUTTON | WS_TABSTOP);
  add(L"STATIC", L"", Details, SS_LEFT);
  add(L"STATIC", L"Select Gargantua to edit its spin.", Status, SS_LEFT);
  add(L"BUTTON", L"Close (F5 / Esc)", Close, BS_PUSHBUTTON | WS_TABSTOP);
  Layout();
  SyncControls();
#endif
}
void ObjectEditor::Layout() {
#ifdef _WIN32
  if (!window)
    return;
  const auto native = static_cast<HWND>(window);
  const int dpi = GetDpiForWindow(native);
  const auto place = [&](int id, int x, int y, int width, int height) {
    MoveWindow(GetDlgItem(native, id), MulDiv(x, dpi, 96), MulDiv(y, dpi, 96),
               MulDiv(width, dpi, 96), MulDiv(height, dpi, 96), TRUE);
  };
  place(ObjectName, 16, 12, 360, 26);
  place(4111, 16, 46, 360, 22);
  place(SpinValue, 16, 74, 246, 27);
  place(Apply, 274, 74, 102, 27);
  place(SpinSlider, 12, 108, 368, 30);
  place(Decrease, 16, 144, 82, 28);
  place(Increase, 108, 144, 82, 28);
  place(Reset, 204, 144, 172, 28);
  place(Details, 16, 184, 360, 86);
  place(Status, 16, 272, 360, 36);
  place(Close, 230, 312, 146, 28);
#endif
}
void ObjectEditor::SetObject(const std::string &name, bool canEdit,
                             double value, double rs) {
  if (objectName == name && editable == canEdit && spin == value &&
      referenceRadiusAu == rs)
    return;
  objectName = name;
  editable = canEdit;
  spin = value;
  referenceRadiusAu = rs;
  SyncControls();
  SetStatus(editable ? L"Slider edits apply live. Enter applies a typed value."
                     : L"Spin is editable only for Gargantua. Other objects "
                       L"are read-only.");
}
void ObjectEditor::SyncControls() {
#ifdef _WIN32
  if (!window)
    return;
  syncing = true;
  const auto native = static_cast<HWND>(window);
  SetDlgItemTextA(native, ObjectName,
                  objectName.empty() ? "No object selected"
                                     : objectName.c_str());
  SetWindowTextA(native, ("Object editor - " + objectName).c_str());
  for (int id : {SpinValue, SpinSlider, Apply, Reset, Decrease, Increase})
    EnableWindow(GetDlgItem(native, id), editable);
  std::ostringstream value;
  value << std::fixed << std::setprecision(6) << spin;
  SetDlgItemTextA(native, SpinValue, editable ? value.str().c_str() : "");
  SendDlgItemMessageW(native, SpinSlider, TBM_SETPOS, TRUE,
                      static_cast<LPARAM>(std::lround(spin * 10000)));
  std::ostringstream details;
  if (editable) {
    details << std::fixed << std::setprecision(4)
            << "Horizon: " << KerrSpacetime::Horizon(referenceRadiusAu, spin)
            << " AU\nDisk inner stable orbit: "
            << KerrSpacetime::ProgradeIsco(referenceRadiusAu, spin)
            << " AU\nAffects light bending and disk appearance.\nVehicle "
               "orbits use a Schwarzschild approximation.";
  } else
    details << "Select Gargantua in the star browser to edit\nblack-hole spin. "
               "Selection does not move the camera.";
  SetDlgItemTextA(native, Details, details.str().c_str());
  syncing = false;
#endif
}
void ObjectEditor::SetStatus(const wchar_t *text) {
#ifdef _WIN32
  if (window)
    SetDlgItemTextW(static_cast<HWND>(window), Status, text);
#else
  (void)text;
#endif
}
void ObjectEditor::ApplyValue(double value) {
  if (!editable || syncing)
    return;
  if (!std::isfinite(value) || value < -.98 || value > .98) {
    SetStatus(L"Enter a finite number between -0.98 and +0.98.");
    return;
  }
  spin = std::round(value * 1e6) / 1e6;
  SyncControls();
  SetStatus(L"Applied. The current view updates immediately.");
  if (applySpin)
    applySpin(spin);
}
void ObjectEditor::ApplyText() {
#ifdef _WIN32
  if (!editable)
    return;
  wchar_t text[128];
  GetDlgItemTextW(static_cast<HWND>(window), SpinValue, text, 128);
  wchar_t *end = nullptr;
  errno = 0;
  const double value = std::wcstod(text, &end);
  if (end == text) {
    SetStatus(L"Enter a spin value, for example 0.85.");
    return;
  }
  while (*end && std::iswspace(*end))
    ++end;
  if (*end || errno == ERANGE) {
    SetStatus(L"Enter a valid number between -0.98 and +0.98.");
    return;
  }
  ApplyValue(value);
#endif
}
void ObjectEditor::Toggle() {
  if (visible) {
    Hide();
    return;
  }
  visible = true;
#ifdef _WIN32
  // Opt-in render checks use a hidden owner and must remain noninteractive.
  if (window && IsWindowVisible(static_cast<HWND>(owner))) {
    ShowWindow(static_cast<HWND>(window), SW_SHOWNORMAL);
    SetFocus(
        GetDlgItem(static_cast<HWND>(window), editable ? SpinValue : Close));
  }
#endif
}
void ObjectEditor::Hide() {
  visible = false;
#ifdef _WIN32
  if (window)
    ShowWindow(static_cast<HWND>(window), SW_HIDE);
  if (owner && IsWindowVisible(static_cast<HWND>(owner)))
    SetFocus(static_cast<HWND>(owner));
#endif
}
void ObjectEditor::Release() {
#ifdef _WIN32
  if (window)
    DestroyWindow(static_cast<HWND>(window));
  if (font)
    DeleteObject(static_cast<HFONT>(font));
  if (brush)
    DeleteObject(static_cast<HBRUSH>(brush));
#endif
  window = owner = font = brush = nullptr;
  visible = false;
  applySpin = {};
}
