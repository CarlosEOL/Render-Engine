#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "ScenePanel.h"
#include <algorithm>
#include <cctype>

void ScenePanel::Create(void *nativeParent)
{
#ifdef _WIN32
    parent = nativeParent;
    const auto window = static_cast<HWND>(parent);
    const UINT dpi = GetDpiForWindow(window);
    font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    backgroundBrush = CreateSolidBrush(RGB(19, 24, 33));
    SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) | WS_CLIPCHILDREN);
    const auto add = [&](const wchar_t *type, const wchar_t *text, int id, DWORD style) {
        const auto control =
            CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        controls.push_back(control);
    };
    add(L"STATIC", L"", 2099, SS_LEFT);
    add(L"STATIC", L"Star browser", 2105, SS_LEFT);
    add(L"EDIT", L"", 2100, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
    SendDlgItemMessageW(window, 2100, 0x1501, TRUE, reinterpret_cast<LPARAM>(L"Search name or catalogue number"));
    add(L"LISTBOX", L"", 2101, WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP);
    add(L"EDIT", L"", 2102, ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
    add(L"STATIC", L"Planets / objects", 2106, SS_LEFT);
    add(L"LISTBOX", L"", 2103, WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP);
    add(L"BUTTON", L"Go to selection (G)", 2104, BS_PUSHBUTTON | WS_TABSTOP);
    RECT rect;
    GetClientRect(window, &rect);
    Resize(rect.right, rect.bottom);
#endif
}

void ScenePanel::Resize(int width, int height)
{
#ifdef _WIN32
    if (!parent)
        return;
    const auto window = static_cast<HWND>(parent);
    const float scale = GetDpiForWindow(window) / 96.0f;
    widthPixels = std::min(static_cast<int>(360 * scale), width / 2);
    const int x = width - widthPixels;
    const int margin = static_cast<int>(12 * scale);
    const int bodyWidth = widthPixels - 2 * margin;
    const auto place = [&](int id, int y, int h, int inset) {
        MoveWindow(GetDlgItem(window, id), x + inset, y, widthPixels - 2 * inset, h, TRUE);
    };
    MoveWindow(GetDlgItem(window, 2099), x, 0, widthPixels, height, TRUE);
    place(2105, margin, static_cast<int>(24 * scale), margin);
    place(2100, static_cast<int>(42 * scale), static_cast<int>(27 * scale), margin);
    place(2101, static_cast<int>(77 * scale), static_cast<int>(142 * scale), margin);
    const int bottom = height - static_cast<int>(240 * scale);
    place(2102, static_cast<int>(230 * scale), std::max(60, bottom - static_cast<int>(238 * scale)), margin);
    place(2106, bottom, static_cast<int>(24 * scale), margin);
    place(2103, bottom + static_cast<int>(28 * scale), static_cast<int>(148 * scale), margin);
    place(2104, height - static_cast<int>(48 * scale), static_cast<int>(32 * scale), margin);
    InvalidateRect(window, nullptr, TRUE);
#endif
}

void ScenePanel::Toggle()
{
    SetVisible(!visible);
}

void ScenePanel::SetVisible(bool show)
{
    visible = show;
#ifdef _WIN32
    for (auto control : controls)
        ShowWindow(static_cast<HWND>(control), visible ? SW_SHOW : SW_HIDE);
#endif
}

void ScenePanel::SetTitle(const std::string &title)
{
#ifdef _WIN32
    if (parent)
        SetDlgItemTextA(static_cast<HWND>(parent), 2105, title.c_str());
#endif
}

void ScenePanel::SetCatalogue(std::vector<std::pair<int, std::string>> items)
{
    catalogue = std::move(items);
    Filter();
}

void ScenePanel::Filter()
{
#ifdef _WIN32
    if (!parent)
        return;
    const auto window = static_cast<HWND>(parent);
    char query[256];
    GetDlgItemTextA(window, 2100, query, 256);
    std::string filter = query;
    std::transform(filter.begin(), filter.end(), filter.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto list = GetDlgItem(window, 2101);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto &item : catalogue)
    {
        auto name = item.second;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!filter.empty() && name.find(filter) == std::string::npos)
            continue;
        const auto row = SendMessageA(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.second.c_str()));
        SendMessageW(list, LB_SETITEMDATA, row, item.first);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
#endif
}

void ScenePanel::SetInformation(const std::string &text)
{
    if (lastInformation == text)
        return;
    lastInformation = text;
#ifdef _WIN32
    std::string windowsText;
    for (char c : text)
    {
        if (c == '\n' && (windowsText.empty() || windowsText.back() != '\r'))
            windowsText += '\r';
        windowsText += c;
    }
    if (parent)
        SetDlgItemTextA(static_cast<HWND>(parent), 2102, windowsText.c_str());
#endif
}

void ScenePanel::SetPlanets(const std::vector<std::string> &names)
{
#ifdef _WIN32
    if (!parent)
        return;
    const auto list = GetDlgItem(static_cast<HWND>(parent), 2103);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto &name : names)
        SendMessageA(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
#endif
}

int ScenePanel::SelectedStar() const
{
#ifdef _WIN32
    const auto list = GetDlgItem(static_cast<HWND>(parent), 2101);
    const auto row = SendMessageW(list, LB_GETCURSEL, 0, 0);
    return row == LB_ERR ? -1 : static_cast<int>(SendMessageW(list, LB_GETITEMDATA, row, 0));
#else
    return -1;
#endif
}

int ScenePanel::SelectedPlanet() const
{
#ifdef _WIN32
    return static_cast<int>(SendDlgItemMessageW(static_cast<HWND>(parent), 2103, LB_GETCURSEL, 0, 0));
#else
    return -1;
#endif
}

void *ScenePanel::ControlColor(void *context) const
{
#ifdef _WIN32
    SetTextColor(static_cast<HDC>(context), RGB(224, 232, 242));
    SetBkColor(static_cast<HDC>(context), RGB(19, 24, 33));
#endif
    return backgroundBrush;
}

void ScenePanel::Release()
{
#ifdef _WIN32
    for (auto control : controls)
        DestroyWindow(static_cast<HWND>(control));
    if (font)
        DeleteObject(static_cast<HFONT>(font));
    if (backgroundBrush)
        DeleteObject(static_cast<HBRUSH>(backgroundBrush));
#endif
    controls.clear();
    parent = font = backgroundBrush = nullptr;
}
