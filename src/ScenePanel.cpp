#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "ScenePanel.h"
#include <algorithm>
#include <cctype>

namespace
{
std::string Lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string WindowsLineEndings(const std::string &text)
{
    std::string converted;
    converted.reserve(text.size() + 32);
    for (char c : text)
    {
        if (c == '\n' && (converted.empty() || converted.back() != '\r'))
            converted += '\r';
        converted += c;
    }
    return converted;
}
} // namespace

void ScenePanel::Create(void *nativeParent)
{
#ifdef _WIN32
    parent = nativeParent;
    const auto window = static_cast<HWND>(parent);
    const UINT dpi = GetDpiForWindow(window);
    font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    statusFont = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    backgroundBrush = CreateSolidBrush(RGB(19, 24, 33));
    SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) | WS_CLIPCHILDREN);
    const auto add = [&](const wchar_t *type, const wchar_t *text, int id, DWORD style) {
        const auto control =
            CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(id == 2107 ? statusFont : font), TRUE);
        controls.push_back(control);
    };
    add(L"STATIC", L"", 2099, SS_LEFT);
    add(L"STATIC", L"", 2107, SS_LEFT | SS_NOPREFIX);
    add(L"STATIC", L"Star browser", 2105, SS_LEFT);
    add(L"EDIT", L"", 2100, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
    SendDlgItemMessageW(window, 2100, 0x1501 /* EM_SETCUEBANNER */, TRUE,
                        reinterpret_cast<LPARAM>(L"Search name or catalogue number"));
    add(L"LISTBOX", L"", 2101, WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP);
    add(L"STATIC", L"Planets / objects", 2106, SS_LEFT);
    add(L"LISTBOX", L"", 2103, WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP);
    add(L"BUTTON", L"Go to selection (G)", 2104, BS_PUSHBUTTON | WS_TABSTOP);
    add(L"EDIT", L"", 2102, ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
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
    const auto px = [scale](float value) { return static_cast<int>(value * scale); };
    widthPixels = std::min(px(340), width / 2);
    const int x = width - widthPixels;
    const int margin = px(12);
    const int gap = px(6);
    const int statusHeight = px(124);
    const int labelHeight = px(20);
    const int searchHeight = px(26);
    const int buttonHeight = px(30);
    const int bodiesFixed = bodiesVisible ? labelHeight + buttonHeight + 3 * gap : 0;
    const int fixed = 2 * margin + statusHeight + labelHeight + searchHeight + 4 * gap + bodiesFixed;
    const int flexible = std::max(3 * px(48), height - fixed);
    const int starHeight = flexible * 30 / 100;
    const int planetHeight = bodiesVisible ? flexible * 22 / 100 : 0;
    const int infoHeight = flexible - starHeight - planetHeight;

    MoveWindow(GetDlgItem(window, 2099), x, 0, widthPixels, height, TRUE);
    int y = margin;
    const auto stack = [&](int id, int h) {
        MoveWindow(GetDlgItem(window, id), x + margin, y, widthPixels - 2 * margin, h, TRUE);
        y += h + gap;
    };
    stack(2107, statusHeight);
    stack(2105, labelHeight);
    stack(2100, searchHeight);
    stack(2101, starHeight);
    if (bodiesVisible)
    {
        stack(2106, labelHeight);
        stack(2103, planetHeight);
        stack(2104, buttonHeight);
    }
    stack(2102, infoHeight);
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
    {
        const int id = GetDlgCtrlID(static_cast<HWND>(control));
        const bool body = id == 2103 || id == 2104 || id == 2106;
        ShowWindow(static_cast<HWND>(control), visible && (bodiesVisible || !body) ? SW_SHOW : SW_HIDE);
    }
#endif
}

void ScenePanel::ShowBodies(bool show)
{
    if (show == bodiesVisible)
        return;
    bodiesVisible = show;
    SetVisible(visible);
#ifdef _WIN32
    RECT rect;
    GetClientRect(static_cast<HWND>(parent), &rect);
    Resize(rect.right, rect.bottom);
#endif
}

void ScenePanel::SetTitle(const std::string &title)
{
#ifdef _WIN32
    if (parent)
        SetDlgItemTextA(static_cast<HWND>(parent), 2105, title.c_str());
#endif
}

void ScenePanel::SetCatalogue(const std::vector<std::pair<int, std::string>> &items)
{
    if (items == catalogue)
        return;
    catalogue = items;
    lowercaseNames.clear();
    lowercaseNames.reserve(catalogue.size());
    for (const auto &item : catalogue)
        lowercaseNames.push_back(Lowercase(item.second));
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
    const std::string filter = Lowercase(query);
    const auto list = GetDlgItem(window, 2101);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    SendMessageW(list, LB_INITSTORAGE, catalogue.size(), catalogue.size() * 24);
    for (size_t i = 0; i < catalogue.size(); ++i)
    {
        if (!filter.empty() && lowercaseNames[i].find(filter) == std::string::npos)
            continue;
        const auto row = SendMessageA(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(catalogue[i].second.c_str()));
        SendMessageW(list, LB_SETITEMDATA, row, catalogue[i].first);
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
    if (!parent)
        return;
    const auto edit = GetDlgItem(static_cast<HWND>(parent), 2102);
    const auto firstLine = SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    SetWindowTextA(edit, WindowsLineEndings(text).c_str());
    SendMessageW(edit, EM_LINESCROLL, 0, firstLine);
#endif
}

void ScenePanel::SetStatus(const std::string &text)
{
    if (lastStatus == text)
        return;
    lastStatus = text;
#ifdef _WIN32
    if (parent)
        SetDlgItemTextA(static_cast<HWND>(parent), 2107, WindowsLineEndings(text).c_str());
#endif
}

void ScenePanel::SetPlanets(const std::vector<std::string> &names)
{
#ifdef _WIN32
    if (!parent)
        return;
    const auto list = GetDlgItem(static_cast<HWND>(parent), 2103);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto &name : names)
        SendMessageA(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
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
    for (auto object : {font, statusFont, backgroundBrush})
        if (object)
            DeleteObject(static_cast<HGDIOBJ>(object));
#endif
    controls.clear();
    parent = font = statusFont = backgroundBrush = nullptr;
}
