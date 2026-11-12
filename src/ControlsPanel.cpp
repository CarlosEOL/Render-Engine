#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#endif
#include "ControlsPanel.h"
#include "UiTheme.h"
#ifdef _WIN32
namespace {
constexpr wchar_t controls[]=L"FLIGHT\r\n\r\nW A S D  Move     R / F  Up / down\r\nQ / E  Roll     RMB  Look around\r\nWheel  Adjust flight speed gradually\r\n[ / ]  Halve / double flight speed\r\nShift + RMB  Zoom     MMB  Reset zoom\r\n\r\nSELECTION & NAVIGATION\r\n\r\nClick  Select visible object\r\nG  Go to selection     Home  Overview\r\nL  Land / lift off\r\nShift + F / T  Follow position\r\nShift + R  Follow rotation\r\nShift + L  Look at planet\r\nF8  Cycle vehicle interiors / free flight\r\nCockpit: seated look-around; Home exits\r\n\r\nTIME & DISPLAY\r\n\r\nSpace  Play / pause     < / >  Time rate\r\nF2  Orbit paths     F3  Object information\r\nF4  All systems     Shift + F4  Current system\r\nF5  Object editor     F6  Photon samples\r\nF9  Controls     F10  Main menu\r\nView menu  Exposure, lenses, render quality\r\n\r\nQUANTUM\r\n\r\nCtrl + R  Restart state     Home  Frame state\r\nSpace  Play / pause     < / >  Time rate\r\nFlight and look controls rotate the 3D view.\r\n\r\nF9 or Esc closes this panel.";
}
struct ControlsPanelWin32 {
    static LRESULT CALLBACK Edit(HWND control,UINT message,WPARAM key,LPARAM detail,UINT_PTR,DWORD_PTR data){
        if(message==WM_KEYDOWN && (key==VK_F9 || key==VK_ESCAPE)){reinterpret_cast<ControlsPanel*>(data)->Hide();return 0;}
        return DefSubclassProc(control,message,key,detail);
    }
    static LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
        auto* panel=reinterpret_cast<ControlsPanel*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){panel=static_cast<ControlsPanel*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);panel->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(panel));}
        if(!panel)return DefWindowProcW(window,message,wParam,lParam);
        if(message==WM_CLOSE || (message==WM_KEYDOWN && (wParam==VK_ESCAPE || wParam==VK_F9))){panel->Hide();return 0;}
        if(message==WM_SIZE){MoveWindow(GetDlgItem(window,1),16,16,LOWORD(lParam)-32,HIWORD(lParam)-32,TRUE);return 0;}
        if(message==WM_CTLCOLORSTATIC || message==WM_CTLCOLOREDIT){SetTextColor(reinterpret_cast<HDC>(wParam),UiTheme::Text);SetBkColor(reinterpret_cast<HDC>(wParam),UiTheme::Panel);return reinterpret_cast<LRESULT>(panel->brush);}
        if(message==WM_ERASEBKGND){RECT r;GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(wParam),&r,static_cast<HBRUSH>(panel->brush));return 1;}
        return DefWindowProcW(window,message,wParam,lParam);
    }
};
#endif
bool ControlsPanel::Create(void* parent){
    owner=parent;
#ifdef _WIN32
    WNDCLASSW type={};type.lpfnWndProc=ControlsPanelWin32::Procedure;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.lpszClassName=L"RenderEngineControls";
    if(!RegisterClassW(&type) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    brush=CreateSolidBrush(UiTheme::Panel);
    font=CreateFontW(-MulDiv(10,GetDpiForWindow(static_cast<HWND>(parent)),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    RECT p;GetWindowRect(static_cast<HWND>(parent),&p);
    window=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"Controls",WS_OVERLAPPEDWINDOW,p.left+40,p.top+50,460,650,static_cast<HWND>(parent),nullptr,type.hInstance,this);
    if(!window){Release();return false;}
    UiTheme::DarkTitle(static_cast<HWND>(window));
    auto edit=CreateWindowExW(0,L"EDIT",controls,WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,16,16,412,570,static_cast<HWND>(window),reinterpret_cast<HMENU>(1),type.hInstance,nullptr);
    SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);SetWindowSubclass(edit,ControlsPanelWin32::Edit,1,reinterpret_cast<DWORD_PTR>(this));
    return edit!=nullptr;
#else
    return false;
#endif
}
void ControlsPanel::Toggle(){
#ifdef _WIN32
    if(!window)return;if(IsWindowVisible(static_cast<HWND>(window)))Hide();else{ShowWindow(static_cast<HWND>(window),SW_SHOW);SetFocus(GetDlgItem(static_cast<HWND>(window),1));}
#endif
}
void ControlsPanel::Hide(){
#ifdef _WIN32
    if(window)ShowWindow(static_cast<HWND>(window),SW_HIDE);if(owner)SetFocus(static_cast<HWND>(owner));
#endif
}
void ControlsPanel::Release(){
#ifdef _WIN32
    if(window)DestroyWindow(static_cast<HWND>(window));if(font)DeleteObject(static_cast<HFONT>(font));if(brush)DeleteObject(static_cast<HBRUSH>(brush));
#endif
    window=font=brush=nullptr;
}
