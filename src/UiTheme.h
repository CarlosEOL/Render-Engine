#pragma once
// Shared look of the native Win32 interface: one palette and the owner-draw routines that use it.
// Include after <windows.h>.

namespace UiTheme
{
constexpr COLORREF window = RGB(10, 14, 21);
constexpr COLORREF panel = RGB(16, 21, 30);
constexpr COLORREF surface = RGB(25, 32, 44);
constexpr COLORREF surfaceHover = RGB(34, 43, 59);
constexpr COLORREF border = RGB(44, 55, 74);
constexpr COLORREF accent = RGB(92, 163, 255);
constexpr COLORREF accentDim = RGB(30, 58, 96);
constexpr COLORREF text = RGB(228, 234, 243);
constexpr COLORREF muted = RGB(138, 151, 171);

HBRUSH Brush(COLORREF color);
HFONT CreateUiFont(HWND window, int points, int weight);
void ReleaseResources();

// Hover state for owner-drawn buttons, fed from the control's subclass procedure.
void TrackHover(HWND control, UINT message);
bool Hovered(HWND control);

// A button whose text contains a newline is drawn as a card: title line, then a muted description.
void DrawButton(const DRAWITEMSTRUCT &item, HFONT titleFont, HFONT bodyFont, bool primary);
void DrawListItem(const DRAWITEMSTRUCT &item, HFONT font);

// Dark popup menus and menu bar. Uses undocumented uxtheme exports and messages when present;
// on systems without them the menus keep the light system look.
void EnableDarkMenus();
bool HandleMenuBarMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result);
void PaintMenuBarSeparator(HWND window);
} // namespace UiTheme
