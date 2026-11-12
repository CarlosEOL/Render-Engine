#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "ScenePanel.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include "UiTheme.h"

void ScenePanel::Create(void *nativeParent)
{
#ifdef _WIN32
    parent = nativeParent;
    const auto window = static_cast<HWND>(parent);
    const UINT dpi = GetDpiForWindow(window);
    font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    backgroundBrush = CreateSolidBrush(UiTheme::Panel);
    SetWindowLongPtrW(window, GWL_STYLE, GetWindowLongPtrW(window, GWL_STYLE) | WS_CLIPCHILDREN);
    const auto add = [&](const wchar_t *type, const wchar_t *text, int id, DWORD style) {
        const auto control =
            CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        controls.push_back(control);
        if(std::wstring(type)==L"BUTTON")UiTheme::Button(control);
    };
    add(L"STATIC", L"", 2099, SS_LEFT);
    add(L"STATIC", L"Star browser", 2105, SS_LEFT);
    add(L"EDIT", L"", 2100, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
    SendDlgItemMessageW(window, 2100, 0x1501, TRUE, reinterpret_cast<LPARAM>(L"Search name or catalogue number"));
    add(L"LISTBOX", L"", 2101, WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP);
    add(L"EDIT", L"", 2102, ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
    add(L"STATIC", L"Planets / objects", 2106, SS_LEFT);
    add(L"LISTBOX", L"", 2103, WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_TABSTOP);
    for(int id:{2101,2103})SendDlgItemMessageW(window,id,LB_SETITEMHEIGHT,0,UiTheme::Px(window,33));
    SendDlgItemMessageW(window,2105,WM_SETFONT,reinterpret_cast<WPARAM>(UiTheme::Font(window,15,FW_SEMIBOLD)),TRUE);
    SendDlgItemMessageW(window,2106,WM_SETFONT,reinterpret_cast<WPARAM>(UiTheme::Font(window,12,FW_SEMIBOLD)),TRUE);
    add(L"BUTTON", L"Go to selection    G", 2104, BS_PUSHBUTTON | WS_TABSTOP);
    add(L"BUTTON", L"Play    Space", 2107, BS_PUSHBUTTON | WS_TABSTOP);
    add(L"STATIC",L"PAUSED",2108,SS_LEFT);
    add(L"STATIC",L"Magnitude limit 7.00",2109,SS_LEFT);
    add(L"BUTTON",L"-",2110,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"BUTTON",L"0",2111,BS_PUSHBUTTON|WS_TABSTOP);
    add(L"BUTTON",L"+",2112,BS_PUSHBUTTON|WS_TABSTOP);
    SendDlgItemMessageW(window,2108,WM_SETFONT,reinterpret_cast<WPARAM>(UiTheme::Font(window,11)),TRUE);
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
    widthPixels = std::min(static_cast<int>(304 * scale), width * 28 / 100);
    const int x = width - widthPixels;
    const int margin = static_cast<int>(12 * scale);
    const int bodyWidth = widthPixels - 2 * margin;
    const auto place = [&](int id, int y, int h, int inset) {
        MoveWindow(GetDlgItem(window, id), x + inset, y, widthPixels - 2 * inset, h, TRUE);
    };
    MoveWindow(GetDlgItem(window, 2099), x, 0, widthPixels, height, TRUE);
    place(2105, margin, static_cast<int>(24 * scale), margin);
    place(2100, static_cast<int>(42 * scale), static_cast<int>(27 * scale), margin);
    const int contentTop=static_cast<int>((currentSystemOnly?77:77)*scale);
    const int contentBottom=height-static_cast<int>(190*scale);
    const int listHeight=browserVisible?(informationVisible?(contentBottom-contentTop)/2:contentBottom-contentTop):0;
    place(2101,contentTop,std::max(60,listHeight),margin);
    place(2106,static_cast<int>(42*scale),static_cast<int>(27*scale),margin);
    place(2103,contentTop,std::max(60,listHeight),margin);
    const int infoTop=browserVisible?contentTop+listHeight+static_cast<int>(12*scale):margin;
    place(2102,infoTop,std::max(60,contentBottom-infoTop),margin);
    place(2108,height-static_cast<int>(140*scale),static_cast<int>(44*scale),margin);
    MoveWindow(GetDlgItem(window,2109),x+margin,height-static_cast<int>(178*scale),bodyWidth-static_cast<int>(94*scale),static_cast<int>(28*scale),TRUE);
    for(int i=0;i<3;++i)MoveWindow(GetDlgItem(window,2110+i),width-margin-static_cast<int>((90-i*30)*scale),height-static_cast<int>(182*scale),static_cast<int>(26*scale),static_cast<int>(28*scale),TRUE);
    place(2107,height-static_cast<int>(88*scale),static_cast<int>(34*scale),margin);
    place(2104,height-static_cast<int>(46*scale),static_cast<int>(34*scale),margin);
    RefreshVisibility();
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
    RefreshVisibility();
}

void ScenePanel::RefreshVisibility() {
#ifdef _WIN32
    for (auto control:controls) {
        const int id=GetDlgCtrlID(static_cast<HWND>(control));
        bool show=visible && (browserVisible || informationVisible);
        if (id==2102) show=visible && informationVisible;
        else if (id==2100 || id==2101) show=visible && browserVisible && !currentSystemOnly;
        else if (id==2103 || id==2106) show=visible && browserVisible && currentSystemOnly;
        else if (id==2105 || id==2104) show=visible && browserVisible;
        ShowWindow(static_cast<HWND>(control),show?SW_SHOW:SW_HIDE);
    }
    if (parent) SetDlgItemTextA(static_cast<HWND>(parent),2106,systemName.c_str());
#endif
}

void ScenePanel::ToggleBrowser(bool local) {
    browserVisible=!(visible && browserVisible && currentSystemOnly==local);
    currentSystemOnly=local; visible=true;
    SetTitle(local?"Current system (Shift+F4)":"All star systems (F4)");
#ifdef _WIN32
    if(parent){RECT rect;GetClientRect(static_cast<HWND>(parent),&rect);Resize(rect.right,rect.bottom);}
#endif
}
void ScenePanel::ToggleInformation() {
    informationVisible=!informationVisible; visible=true;
#ifdef _WIN32
    if(parent){RECT rect;GetClientRect(static_cast<HWND>(parent),&rect);Resize(rect.right,rect.bottom);}
#endif
}
void ScenePanel::SetCurrentSystemName(const std::string& name) {
    systemName=name;
    RefreshVisibility();
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
#ifdef _WIN32
    if (parent) SetDlgItemTextA(static_cast<HWND>(parent),2100,"");
#endif
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
    if (parent) {
        const HWND info=GetDlgItem(static_cast<HWND>(parent),2102);
        const auto firstLine=SendMessageW(info,EM_GETFIRSTVISIBLELINE,0,0);
        SetWindowTextA(info,windowsText.c_str());
        SendMessageW(info,EM_LINESCROLL,0,firstLine);
    }
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
void ScenePanel::SetPlayback(bool playing,const std::string& clock){
#ifdef _WIN32
    if(!parent)return;
    const auto window=static_cast<HWND>(parent);
    const auto label=playing?L"Pause    Space":L"Play    Space";
    if(UiTheme::Label(GetDlgItem(window,2107))!=label)SetDlgItemTextW(window,2107,label);
    SetDlgItemTextA(window,2108,clock.c_str());
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
    SetTextColor(static_cast<HDC>(context), UiTheme::Text);
    SetBkColor(static_cast<HDC>(context), UiTheme::Panel);
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

void ScenePanel::SetMagnitude(float value){
#ifdef _WIN32
    if(!parent)return;std::ostringstream label;label<<"Magnitude limit "<<std::fixed<<std::setprecision(2)<<value;
    SetDlgItemTextA(static_cast<HWND>(parent),2109,label.str().c_str());
#endif
}
