#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "AppMenu.h"
#include <algorithm>
#include <windows.h>
#include "UiTheme.h"

void AppMenu::Create(void *nativeParent, const std::vector<std::string> &names)
{
    parent = nativeParent;
    const auto window = static_cast<HWND>(parent);
    menu=GetMenu(window);
    UiTheme::DarkTitle(window);
    const UINT dpi = GetDpiForWindow(window);
    font = CreateFontW(-MulDiv(12, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    titleFont = CreateFontW(-MulDiv(32, dpi, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    brush = CreateSolidBrush(UiTheme::Background);
    SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) | WS_CLIPCHILDREN);
    const auto add = [&](const wchar_t *type, const wchar_t *text, int id, DWORD style = 0) {
        const HWND control =
            CreateWindowExW(0, type, text, WS_CHILD | style, 0, 0, 0, 0, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(id == 3001 ? titleFont : font), TRUE);
        controls.push_back(control);
        if(std::wstring(type)==L"BUTTON")UiTheme::Button(control);
    };
    add(L"STATIC", L"", 3000);
    UiTheme::Canvas(GetDlgItem(window,3000));
    add(L"STATIC", L"Render Engine", 3001, SS_LEFT);
    add(L"STATIC", L"Two laboratories. One universe to explore.", 3002, SS_LEFT);
    add(L"STATIC",L"SIMULATION LABORATORY",3005,SS_LEFT);
    add(L"STATIC",L"WASD  Move     /     Right mouse  Look     /     Space  Play or pause     /     F10  Return here",3004,SS_LEFT);
    SendDlgItemMessageW(window,3005,WM_SETFONT,reinterpret_cast<WPARAM>(UiTheme::Font(window,11,FW_SEMIBOLD)),TRUE);
    SendDlgItemMessageW(window,3004,WM_SETFONT,reinterpret_cast<WPARAM>(UiTheme::Font(window,11)),TRUE);
    add(L"BUTTON", L"Planetorium\nExplore celestial systems", 3010, BS_MULTILINE | WS_TABSTOP);
    add(L"BUTTON", L"Quantum Simulation\nExplore particle states", 3011, BS_MULTILINE | WS_TABSTOP);
    add(L"BUTTON", L"Solar System - 1:1 scale\nSun, eight planets, moons, dwarf planets and asteroids", 3012,
        BS_MULTILINE | WS_TABSTOP);
    add(L"BUTTON", L"Gargantua system\nMiller, Mann, Edmunds and spacecraft", 3013, BS_MULTILINE | WS_TABSTOP);
    add(L"STATIC",
        L"Fly freely, select a destination, and press G to visit.\nPause whenever you want a closer look.",
        3003, SS_LEFT);
    add(L"LISTBOX", L"", 3020, WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP);
    SendDlgItemMessageW(window,3020,LB_SETITEMHEIGHT,0,UiTheme::Px(window,35));
    for (const auto &name : names)
        SendDlgItemMessageA(window, 3020, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    SendDlgItemMessageW(window, 3020, LB_SETCURSEL, 0, 0);
    add(L"EDIT", L"", 3021, ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
    add(L"BUTTON", L"Open selected state", 3014, WS_TABSTOP);
    add(L"BUTTON", L"Back", 3015, WS_TABSTOP);
    Show(Page::Home);
}

void AppMenu::Show(Page next)
{
    visible = true;
    page = next;
    const auto window = static_cast<HWND>(parent);
    if(GetMenu(window)){SetMenu(window,nullptr);DrawMenuBar(window);}
    for (auto control : controls)
    {
        const int id = GetDlgCtrlID(static_cast<HWND>(control));
        const bool common = id <= 3002 || id==3004 || id==3005;
        const bool show = common || (page == Page::Home && (id == 3010 || id == 3011)) ||
                          (page == Page::Planetorium && (id == 3012 || id == 3013 || id == 3003)) ||
                          (page == Page::Quantum && (id == 3020 || id == 3021 || id == 3014)) ||
                          (page != Page::Home && id == 3015);
        ShowWindow(static_cast<HWND>(control), show ? SW_SHOW : SW_HIDE);
    }
    SetDlgItemTextW(window, 3001,
                    page == Page::Home          ? L"Explore the extraordinary."
                    : page == Page::Planetorium ? L"Choose your destination."
                                                : L"A closer look at possibility.");
    SetDlgItemTextW(window, 3002,
                    page == Page::Home ? L"From distant worlds to the smallest particle. Choose a laboratory to begin."
                    : page == Page::Planetorium
                        ? L"Explore our Solar System, or trace the edge of a rotating black hole."
                        : L"Select a state. Brightness shows probability density; color shows phase.");
    RECT rect;
    GetClientRect(window, &rect);
    Resize(rect.right, rect.bottom);
    SetFocus(GetDlgItem(window, page == Page::Home ? 3010 : page == Page::Planetorium ? 3012 : 3020));
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_ERASE|RDW_UPDATENOW);
}

void AppMenu::Hide()
{
    visible = false;
    for (auto control : controls)
        ShowWindow(static_cast<HWND>(control), SW_HIDE);
    if(parent && menu && !GetMenu(static_cast<HWND>(parent))){SetMenu(static_cast<HWND>(parent),static_cast<HMENU>(menu));DrawMenuBar(static_cast<HWND>(parent));}
}

void AppMenu::Resize(int width, int height)
{
    if (!parent)
        return;
    const auto window = static_cast<HWND>(parent);
    const float scale = GetDpiForWindow(window) / 96.0f;
    const int w = std::min(width - 32, static_cast<int>(1000 * scale));
    const bool compact=height<720*scale;
    const int x = (width - w) / 2;
    const int top = std::max(static_cast<int>(16*scale), (height - static_cast<int>(590 * scale)) / 2);
    const auto place = [&](int id, int dx, int dy, int cw, int ch) {
        MoveWindow(GetDlgItem(window, id), x + dx, top + dy, cw, ch, TRUE);
    };
    MoveWindow(GetDlgItem(window, 3000), 0, 0, width, height, TRUE);
    place(3005,0,0,w,static_cast<int>(24*scale));
    place(3001, 0, static_cast<int>((compact?26:36)*scale), w, static_cast<int>(70 * scale));
    place(3002, 0, static_cast<int>((compact?94:114) * scale), w, static_cast<int>(48 * scale));
    const int gap = static_cast<int>(24 * scale);
    const int half = (w - gap) / 2;
    const int y = static_cast<int>((compact?150:184) * scale);
    const int cardHeight = std::max(static_cast<int>(140*scale), std::min(static_cast<int>(245 * scale), height - top - y - static_cast<int>(120*scale)));
    for (int id : {3010, 3012})
        place(id, 0, y, half, cardHeight);
    for (int id : {3011, 3013})
        place(id, half + gap, y, half, cardHeight);
    place(3003, 0, y + cardHeight + static_cast<int>(20*scale), w, static_cast<int>(46 * scale));
    ShowWindow(GetDlgItem(window,3003),visible && page==Page::Planetorium && !compact?SW_SHOW:SW_HIDE);
    const int listHeight = std::max(130, std::min(static_cast<int>(330 * scale), height - top - y - 90));
    place(3020, 0, y, half, listHeight);
    place(3021, half + gap, y, half, listHeight);
    place(3014, half + gap, y + listHeight + 16, half, static_cast<int>(42 * scale));
    place(3015, 0, y + (page == Page::Quantum ? listHeight+static_cast<int>(12*scale) : cardHeight+static_cast<int>((compact?24:76)*scale)), static_cast<int>(130 * scale),
          static_cast<int>(42 * scale));
    place(3004,0,std::min(height-top-static_cast<int>(32*scale),y+(page==Page::Quantum?listHeight:cardHeight)+static_cast<int>(125*scale)),w,static_cast<int>(30*scale));
    ShowWindow(GetDlgItem(window,3004),visible && !(compact && page==Page::Quantum)?SW_SHOW:SW_HIDE);
    InvalidateRect(window, nullptr, TRUE);
}

int AppMenu::SelectedQuantum() const
{
    return static_cast<int>(SendDlgItemMessageW(static_cast<HWND>(parent), 3020, LB_GETCURSEL, 0, 0));
}
void AppMenu::SetQuantumInformation(const std::string &text)
{
    SetDlgItemTextA(static_cast<HWND>(parent), 3021, text.c_str());
}
void *AppMenu::ControlColor(void *dc) const
{
    SetTextColor(static_cast<HDC>(dc), UiTheme::Text);
    SetBkColor(static_cast<HDC>(dc), UiTheme::Background);
    return brush;
}
void AppMenu::Release()
{
    for (auto control : controls)
        DestroyWindow(static_cast<HWND>(control));
    controls.clear();
    for (auto object : {font, titleFont, brush})
        if (object)
            DeleteObject(static_cast<HGDIOBJ>(object));
    parent = font = titleFont = brush = nullptr;
}
