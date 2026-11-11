#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "UiTheme.h"
#include <map>
#include <string>

namespace
{
std::map<COLORREF, HBRUSH> brushes;
bool darkMenus = false;
constexpr const wchar_t *hoverProperty = L"RenderEngineHover";

// Layouts of the undocumented menu-bar drawing messages (user32 "UAH" messages).
constexpr UINT WM_UAHDRAWMENU = 0x0091;
constexpr UINT WM_UAHDRAWMENUITEM = 0x0092;
struct UAHMENU
{
    HMENU menu;
    HDC dc;
    DWORD flags;
};
union UAHMENUITEMMETRICS {
    struct
    {
        DWORD cx, cy;
    } bar[2];
    struct
    {
        DWORD cx, cy;
    } popup[4];
};
struct UAHMENUPOPUPMETRICS
{
    DWORD widths[4];
    DWORD updateMaxWidths : 2;
};
struct UAHMENUITEM
{
    int position;
    UAHMENUITEMMETRICS metrics;
    UAHMENUPOPUPMETRICS popupMetrics;
};
struct UAHDRAWMENUITEM
{
    DRAWITEMSTRUCT draw;
    UAHMENU menu;
    UAHMENUITEM item;
};

int Scaled(HWND window, int value)
{
    return MulDiv(value, GetDpiForWindow(window), 96);
}

std::wstring WindowText(HWND window)
{
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    text.resize(GetWindowTextW(window, text.data(), static_cast<int>(text.size())));
    return text;
}

void RoundedRect(HDC dc, const RECT &rect, int radius, COLORREF fill, COLORREF edge)
{
    const auto oldPen = SelectObject(dc, GetStockObject(DC_PEN));
    const auto oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCPenColor(dc, edge);
    SetDCBrushColor(dc, fill);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}
} // namespace

namespace UiTheme
{
HBRUSH Brush(COLORREF color)
{
    auto &brush = brushes[color];
    if (!brush)
        brush = CreateSolidBrush(color);
    return brush;
}

HFONT CreateUiFont(HWND window, int points, int weight)
{
    return CreateFontW(-MulDiv(points, GetDpiForWindow(window), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                       L"Segoe UI");
}

void ReleaseResources()
{
    for (auto &[color, brush] : brushes)
        DeleteObject(brush);
    brushes.clear();
}

void TrackHover(HWND control, UINT message)
{
    if (message == WM_MOUSEMOVE && !GetPropW(control, hoverProperty))
    {
        SetPropW(control, hoverProperty, reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, control, 0};
        TrackMouseEvent(&track);
        InvalidateRect(control, nullptr, FALSE);
    }
    else if (message == WM_MOUSELEAVE || message == WM_NCDESTROY)
    {
        if (RemovePropW(control, hoverProperty) && message == WM_MOUSELEAVE)
            InvalidateRect(control, nullptr, FALSE);
    }
}

bool Hovered(HWND control)
{
    return GetPropW(control, hoverProperty) != nullptr;
}

void DrawButton(const DRAWITEMSTRUCT &item, HFONT titleFont, HFONT bodyFont, bool primary)
{
    const HDC dc = item.hDC;
    const HWND control = item.hwndItem;
    const bool pressed = item.itemState & ODS_SELECTED;
    const bool focused = item.itemState & ODS_FOCUS;
    const bool disabled = item.itemState & ODS_DISABLED;
    const bool hover = Hovered(control) && !disabled;
    const COLORREF behind = GetDlgCtrlID(control) >= 3000 ? window : panel;
    FillRect(dc, &item.rcItem, Brush(behind));

    COLORREF fill = pressed ? surface : hover ? surfaceHover : surface;
    COLORREF edge = focused || hover ? accent : border;
    COLORREF foreground = disabled ? muted : text;
    if (primary && !disabled)
    {
        fill = pressed ? RGB(70, 135, 220) : hover ? RGB(122, 182, 255) : accent;
        edge = focused ? text : fill;
        foreground = window;
    }
    RoundedRect(dc, item.rcItem, Scaled(control, 10), fill, edge);

    const auto text = WindowText(control);
    const auto newline = text.find(L'\n');
    SetBkMode(dc, TRANSPARENT);
    const auto oldFont = SelectObject(dc, titleFont);
    if (newline == std::wstring::npos)
    {
        RECT label = item.rcItem;
        SetTextColor(dc, foreground);
        DrawTextW(dc, text.c_str(), -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, oldFont);
        return;
    }

    // Card: title and description stacked and centred vertically, with a chevron on the right.
    const int padding = Scaled(control, 24);
    const std::wstring title = text.substr(0, newline);
    const std::wstring body = text.substr(newline + 1);
    RECT titleRect{item.rcItem.left + padding, 0, item.rcItem.right - 2 * padding, 0};
    RECT bodyRect = titleRect;
    DrawTextW(dc, title.c_str(), -1, &titleRect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, bodyFont);
    DrawTextW(dc, body.c_str(), -1, &bodyRect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    const int gap = Scaled(control, 6);
    const int blockHeight = (titleRect.bottom - titleRect.top) + gap + (bodyRect.bottom - bodyRect.top);
    const int top = (item.rcItem.top + item.rcItem.bottom - blockHeight) / 2;
    OffsetRect(&titleRect, 0, top - titleRect.top);
    OffsetRect(&bodyRect, 0, titleRect.bottom + gap - bodyRect.top);

    SelectObject(dc, titleFont);
    SetTextColor(dc, foreground);
    DrawTextW(dc, title.c_str(), -1, &titleRect, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, bodyFont);
    SetTextColor(dc, muted);
    DrawTextW(dc, body.c_str(), -1, &bodyRect, DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, titleFont);
    SetTextColor(dc, hover || focused ? accent : muted);
    RECT chevron{item.rcItem.right - 2 * padding, item.rcItem.top, item.rcItem.right - padding / 2, item.rcItem.bottom};
    const wchar_t arrow[] = {0x203A, 0};
    DrawTextW(dc, arrow, -1, &chevron, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldFont);
}

void DrawListItem(const DRAWITEMSTRUCT &item, HFONT font)
{
    const HDC dc = item.hDC;
    RECT rect = item.rcItem;
    if (item.itemID == static_cast<UINT>(-1))
    {
        FillRect(dc, &rect, Brush(surface));
        return;
    }
    const bool selected = item.itemState & ODS_SELECTED;
    FillRect(dc, &rect, Brush(selected ? accentDim : surface));
    if (selected)
    {
        RECT bar{rect.left, rect.top + 3, rect.left + Scaled(item.hwndItem, 3), rect.bottom - 3};
        FillRect(dc, &bar, Brush(accent));
    }
    const auto length = SendMessageW(item.hwndItem, LB_GETTEXTLEN, item.itemID, 0);
    if (length <= 0)
        return;
    std::wstring label(static_cast<size_t>(length) + 1, L'\0');
    label.resize(SendMessageW(item.hwndItem, LB_GETTEXT, item.itemID, reinterpret_cast<LPARAM>(label.data())));
    rect.left += Scaled(item.hwndItem, 12);
    rect.right -= Scaled(item.hwndItem, 6);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, selected ? RGB(255, 255, 255) : text);
    const auto oldFont = SelectObject(dc, font);
    DrawTextW(dc, label.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void EnableDarkMenus()
{
    enum class PreferredAppMode
    {
        Default,
        AllowDark,
        ForceDark
    };
    using SetPreferredAppMode = PreferredAppMode(WINAPI *)(PreferredAppMode);
    using FlushMenuThemes = void(WINAPI *)();
    const HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!uxtheme)
        return;
    const auto setMode = reinterpret_cast<SetPreferredAppMode>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    const auto flush = reinterpret_cast<FlushMenuThemes>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (!setMode)
        return;
    setMode(PreferredAppMode::ForceDark);
    if (flush)
        flush();
    darkMenus = true;
}

bool HandleMenuBarMessage(HWND window, UINT message, WPARAM, LPARAM lParam, LRESULT &result)
{
    if (!darkMenus)
        return false;
    if (message == WM_UAHDRAWMENU)
    {
        const auto *menu = reinterpret_cast<UAHMENU *>(lParam);
        MENUBARINFO info{sizeof(info)};
        if (!GetMenuBarInfo(window, OBJID_MENU, 0, &info))
            return false;
        RECT windowRect;
        GetWindowRect(window, &windowRect);
        RECT bar = info.rcBar;
        OffsetRect(&bar, -windowRect.left, -windowRect.top);
        bar.top -= 1;
        FillRect(menu->dc, &bar, Brush(panel));
        result = 0;
        return true;
    }
    if (message == WM_UAHDRAWMENUITEM)
    {
        auto *draw = reinterpret_cast<UAHDRAWMENUITEM *>(lParam);
        wchar_t label[128] = {};
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_STRING;
        info.dwTypeData = label;
        info.cch = 127;
        GetMenuItemInfoW(draw->menu.menu, draw->item.position, TRUE, &info);
        const UINT state = draw->draw.itemState;
        const bool hot = state & (ODS_HOTLIGHT | ODS_SELECTED);
        FillRect(draw->menu.dc, &draw->draw.rcItem, Brush(hot ? surfaceHover : panel));
        SetBkMode(draw->menu.dc, TRANSPARENT);
        SetTextColor(draw->menu.dc, state & (ODS_GRAYED | ODS_DISABLED | ODS_INACTIVE) ? muted : text);
        DrawTextW(draw->menu.dc, label, -1, &draw->draw.rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | (state & ODS_NOACCEL ? DT_HIDEPREFIX : 0));
        result = 0;
        return true;
    }
    return false;
}

// Windows draws a one-pixel light line between the menu bar and the client area after the bar itself.
void PaintMenuBarSeparator(HWND window)
{
    if (!darkMenus)
        return;
    MENUBARINFO info{sizeof(info)};
    if (!GetMenuBarInfo(window, OBJID_MENU, 0, &info))
        return;
    RECT client;
    GetClientRect(window, &client);
    MapWindowPoints(window, nullptr, reinterpret_cast<POINT *>(&client), 2);
    RECT windowRect;
    GetWindowRect(window, &windowRect);
    OffsetRect(&client, -windowRect.left, -windowRect.top);
    RECT line{client.left, client.top - 1, client.right, client.top};
    const HDC dc = GetWindowDC(window);
    FillRect(dc, &line, Brush(panel));
    ReleaseDC(window, dc);
}
} // namespace UiTheme
