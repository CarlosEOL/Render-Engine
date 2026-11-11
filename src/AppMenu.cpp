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
    font = UiTheme::CreateUiFont(window, 11, FW_NORMAL);
    titleFont = UiTheme::CreateUiFont(window, 30, FW_SEMIBOLD);
    cardFont = UiTheme::CreateUiFont(window, 15, FW_SEMIBOLD);
    SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) | WS_CLIPCHILDREN);
    const auto add = [&](const wchar_t *type, const wchar_t *text, int id, DWORD style = 0) {
        const HWND control =
            CreateWindowExW(0, type, text, WS_CHILD | style, 0, 0, 0, 0, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(id == 3001 ? titleFont : font), TRUE);
        controls.push_back(control);
    };
    const DWORD card = BS_OWNERDRAW | WS_TABSTOP;
    add(L"STATIC", L"", 3000);
    add(L"STATIC", L"Render Engine", 3001, SS_CENTER);
    add(L"STATIC", L"Choose a laboratory. Explore at your own pace.", 3002, SS_CENTER);
    add(L"BUTTON", L"Planetorium\nThe Solar System at true scale and the Gargantua black hole system.", 3010, card);
    add(L"BUTTON", L"Quantum Simulation\nWavefunctions, tunnelling, hydrogen orbitals and entangled spins.", 3011,
        card);
    add(L"BUTTON", L"Solar System\nSun, eight planets, moons, dwarf planets and 4,096 asteroids, all at 1:1 scale.",
        3012, card);
    add(L"BUTTON", L"Gargantua\nA lensed black hole with ocean worlds, orbiting stars and a spacecraft.", 3013, card);
    add(L"STATIC",
        L"Solar System distances and radii share one physical scale; small bodies get selection markers. "
        L"Gargantua keeps an illustrative scale.",
        3003, SS_CENTER);
    add(L"STATIC", L"F10 returns here from any scene.   Drop an OBJ or FBX file on the window to view a model.", 3004,
        SS_CENTER);
    add(L"LISTBOX", L"", 3020,
        WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP);
    SendDlgItemMessageW(window, 3020, LB_SETITEMHEIGHT, 0, MulDiv(30, GetDpiForWindow(window), 96));
    for (const auto &name : names)
        SendDlgItemMessageA(window, 3020, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    SendDlgItemMessageW(window, 3020, LB_SETCURSEL, 0, 0);
    add(L"EDIT", L"", 3021, ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
    const int margin = MulDiv(14, GetDpiForWindow(window), 96);
    SendDlgItemMessageW(window, 3021, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(margin, margin));
    add(L"BUTTON", L"Open selected state", 3014, BS_OWNERDRAW | WS_TABSTOP);
    add(L"BUTTON", L"Back", 3015, BS_OWNERDRAW | WS_TABSTOP);
    Show(Page::Home);
}

void AppMenu::Show(Page next)
{
    visible = true;
    page = next;
    const auto window = static_cast<HWND>(parent);
    for (auto control : controls)
    {
        const int id = GetDlgCtrlID(static_cast<HWND>(control));
        const bool common = id <= 3002 || id == 3004;
        const bool show = common || (page == Page::Home && (id == 3010 || id == 3011)) ||
                          (page == Page::Planetorium && (id == 3012 || id == 3013 || id == 3003)) ||
                          (page == Page::Quantum && (id == 3020 || id == 3021 || id == 3014)) ||
                          (page != Page::Home && id == 3015);
        ShowWindow(static_cast<HWND>(control), show ? SW_SHOW : SW_HIDE);
    }
    SetDlgItemTextW(window, 3001,
                    page == Page::Home          ? L"Render Engine"
                    : page == Page::Planetorium ? L"Planetorium"
                                                : L"Quantum Simulation");
    SetDlgItemTextW(window, 3002,
                    page == Page::Home ? L"Choose a laboratory. Explore at your own pace."
                    : page == Page::Planetorium
                        ? L"Select a system to explore."
                        : L"Select a state. Brightness shows probability density; color shows phase.");
    RECT rect;
    GetClientRect(window, &rect);
    Resize(rect.right, rect.bottom);
    if (page == Page::Quantum)
        SendDlgItemMessageW(window, 3020, LB_SETTOPINDEX, std::max(0, SelectedQuantum()), 0);
    SetFocus(GetDlgItem(window, page == Page::Home ? 3010 : page == Page::Planetorium ? 3012 : 3020));
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
}

void AppMenu::Hide()
{
    visible = false;
    for (auto control : controls)
        ShowWindow(static_cast<HWND>(control), SW_HIDE);
}

void AppMenu::Resize(int width, int height)
{
    if (!parent)
        return;
    const auto window = static_cast<HWND>(parent);
    const float scale = GetDpiForWindow(window) / 96.0f;
    const auto px = [scale](int value) { return static_cast<int>(value * scale); };
    const int w = std::min(width - px(48), px(960));
    const int x = (width - w) / 2;
    const int top = std::max(px(24), (height - px(560)) / 2);
    const auto place = [&](int id, int dx, int dy, int cw, int ch) {
        MoveWindow(GetDlgItem(window, id), x + dx, top + dy, cw, ch, TRUE);
    };
    MoveWindow(GetDlgItem(window, 3000), 0, 0, width, height, TRUE);
    place(3001, 0, 0, w, px(64));
    place(3002, 0, px(72), w, px(28));
    const int gap = px(20);
    const int half = (w - gap) / 2;
    const int y = px(132);
    const int buttonHeight = px(40);
    const int noteHeight = px(48);
    const int cardHeight = std::max(px(120), std::min(px(180), height - top - y - px(20) - noteHeight - px(16) -
                                                                   buttonHeight - px(56)));
    for (int id : {3010, 3012})
        place(id, 0, y, half, cardHeight);
    for (int id : {3011, 3013})
        place(id, half + gap, y, half, cardHeight);
    place(3003, 0, y + cardHeight + px(20), w, noteHeight);
    const int listHeight = std::max(px(160), std::min(px(340), height - top - y - px(16) - buttonHeight - px(56)));
    place(3020, 0, y, half, listHeight);
    place(3021, half + gap, y, half, listHeight);
    place(3014, half + gap, y + listHeight + px(16), half, buttonHeight);
    const int backY = page == Page::Quantum ? y + listHeight + px(16)
                      : page == Page::Planetorium ? y + cardHeight + px(20) + noteHeight + px(16)
                                                  : y + cardHeight + px(20);
    place(3015, 0, backY, px(120), buttonHeight);
    MoveWindow(GetDlgItem(window, 3004), 0, height - px(44), width, px(24), TRUE);
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

void AppMenu::DrawItem(const void *drawItem) const
{
    const auto &item = *static_cast<const DRAWITEMSTRUCT *>(drawItem);
    if (item.CtlID == 3020)
        UiTheme::DrawListItem(item, static_cast<HFONT>(font));
    else
        UiTheme::DrawButton(item, static_cast<HFONT>(item.CtlID == 3014 || item.CtlID == 3015 ? font : cardFont),
                            static_cast<HFONT>(font), item.CtlID == 3014);
}

void *AppMenu::ControlColor(void *dc, int id) const
{
    const auto context = static_cast<HDC>(dc);
    const bool box = id == 3020 || id == 3021;
    SetTextColor(context, id == 3002 || id == 3003 || id == 3004 ? UiTheme::muted : UiTheme::text);
    SetBkColor(context, box ? UiTheme::surface : UiTheme::window);
    return UiTheme::Brush(box ? UiTheme::surface : UiTheme::window);
}

void AppMenu::Release()
{
    for (auto control : controls)
        DestroyWindow(static_cast<HWND>(control));
    controls.clear();
    for (auto object : {font, titleFont, cardFont})
        if (object)
            DeleteObject(static_cast<HGDIOBJ>(object));
    parent = font = titleFont = cardFont = nullptr;
}
