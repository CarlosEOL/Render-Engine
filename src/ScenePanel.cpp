#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "UiTheme.h"
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
    font = UiTheme::CreateUiFont(window, 10, FW_NORMAL);
    headerFont = UiTheme::CreateUiFont(window, 8, FW_SEMIBOLD);
    headlineFont = UiTheme::CreateUiFont(window, 13, FW_SEMIBOLD);
    SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) | WS_CLIPCHILDREN);
    const auto add = [&](const wchar_t *type, const wchar_t *text, int id, DWORD style) {
        const auto control =
            CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        const bool header = id == 2105 || id == 2106;
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(header ? headerFont : font), TRUE);
        controls.push_back(control);
    };
    const DWORD list = WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP;
    add(L"STATIC", L"", 2099, SS_LEFT);
    add(L"STATIC", L"", 2107, SS_OWNERDRAW);
    add(L"STATIC", L"STARS AND OBJECTS", 2105, SS_LEFT);
    add(L"EDIT", L"", 2100, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
    SendDlgItemMessageW(window, 2100, 0x1501 /* EM_SETCUEBANNER */, TRUE,
                        reinterpret_cast<LPARAM>(L"Search by name or catalogue number"));
    add(L"LISTBOX", L"", 2101, list);
    add(L"STATIC", L"BODIES IN THIS SYSTEM", 2106, SS_LEFT);
    add(L"LISTBOX", L"", 2103, list);
    add(L"BUTTON", L"Go to selection  (G)", 2104, BS_OWNERDRAW | WS_TABSTOP);
    add(L"EDIT", L"", 2102, ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
    const int rowHeight = MulDiv(26, GetDpiForWindow(window), 96);
    SendDlgItemMessageW(window, 2101, LB_SETITEMHEIGHT, 0, rowHeight);
    SendDlgItemMessageW(window, 2103, LB_SETITEMHEIGHT, 0, rowHeight);
    const int margin = MulDiv(8, GetDpiForWindow(window), 96);
    for (int id : {2100, 2102})
        SendDlgItemMessageW(window, id, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
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
    const int statusHeight = px(140);
    const int labelHeight = px(20);
    const int searchHeight = px(26);
    const int buttonHeight = px(30);
    const int bodiesFixed = bodiesVisible ? labelHeight + buttonHeight + 3 * gap : 0;
    const int sectionGap = px(10);
    const int fixed = 2 * margin + statusHeight + labelHeight + searchHeight + 4 * gap + bodiesFixed +
                      (bodiesVisible ? 2 : 1) * sectionGap;
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
    y += sectionGap;
    stack(2105, labelHeight);
    stack(2100, searchHeight);
    stack(2101, starHeight);
    if (bodiesVisible)
    {
        y += sectionGap;
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
    {
        std::string header = title;
        std::transform(header.begin(), header.end(), header.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        SetDlgItemTextA(static_cast<HWND>(parent), 2105, header.c_str());
    }
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
        InvalidateRect(GetDlgItem(static_cast<HWND>(parent), 2107), nullptr, FALSE);
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

void ScenePanel::HighlightPlanet(int row)
{
#ifdef _WIN32
    if (parent && SelectedPlanet() != row)
        SendDlgItemMessageW(static_cast<HWND>(parent), 2103, LB_SETCURSEL, row, 0);
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

void *ScenePanel::ControlColor(void *context, int id) const
{
#ifdef _WIN32
    const auto dc = static_cast<HDC>(context);
    const bool box = id == 2100 || id == 2101 || id == 2103;
    SetTextColor(dc, id == 2105 || id == 2106 ? UiTheme::muted : UiTheme::text);
    SetBkColor(dc, box ? UiTheme::surface : UiTheme::panel);
    return UiTheme::Brush(box ? UiTheme::surface : UiTheme::panel);
#else
    return nullptr;
#endif
}

void ScenePanel::DrawItem(const void *drawItem) const
{
#ifdef _WIN32
    const auto &item = *static_cast<const DRAWITEMSTRUCT *>(drawItem);
    if (item.CtlID == 2101 || item.CtlID == 2103)
    {
        UiTheme::DrawListItem(item, static_cast<HFONT>(font));
        return;
    }
    if (item.CtlID == 2104)
    {
        UiTheme::DrawButton(item, static_cast<HFONT>(headerFont), static_cast<HFONT>(font), true);
        return;
    }
    // Status block: the first line is the headline (play state and rate), the rest are details.
    RECT rect = item.rcItem;
    FillRect(item.hDC, &rect, UiTheme::Brush(UiTheme::panel));
    SetBkMode(item.hDC, TRANSPARENT);
    const auto oldFont = SelectObject(item.hDC, headlineFont);
    size_t start = 0;
    for (int line = 0; start <= lastStatus.size(); ++line)
    {
        const size_t end = std::min(lastStatus.find('\n', start), lastStatus.size());
        const std::string text = lastStatus.substr(start, end - start);
        SelectObject(item.hDC, line == 0 ? headlineFont : font);
        SetTextColor(item.hDC, line == 0 && text.rfind("Playing", 0) == 0 ? UiTheme::accent : UiTheme::text);
        RECT measured = rect;
        DrawTextA(item.hDC, text.c_str(), -1, &measured, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        DrawTextA(item.hDC, text.c_str(), -1, &rect, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        rect.top += (measured.bottom - measured.top) + (line == 0 ? MulDiv(4, GetDpiForWindow(item.hwndItem), 96) : 0);
        start = end + 1;
    }
    SelectObject(item.hDC, oldFont);
#endif
}

void ScenePanel::Release()
{
#ifdef _WIN32
    for (auto control : controls)
        DestroyWindow(static_cast<HWND>(control));
    for (auto object : {font, headerFont, headlineFont})
        if (object)
            DeleteObject(static_cast<HGDIOBJ>(object));
#endif
    controls.clear();
    parent = font = headerFont = headlineFont = nullptr;
}
