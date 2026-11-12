#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#pragma comment(lib,"msimg32.lib")
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"dwmapi.lib")

// Shared native painting keeps keyboard focus, text selection and accessible
// control names. Nothing in this theme runs in the 3D render loop.
namespace UiTheme {
inline constexpr COLORREF Background=RGB(8,12,20),Panel=RGB(14,21,32),
    Raised=RGB(20,30,44),Border=RGB(39,54,72),Text=RGB(230,238,248),
    Muted=RGB(141,159,181),Accent=RGB(91,222,211),Violet=RGB(172,151,245);
inline int Px(HWND window,int value){return MulDiv(value,GetDpiForWindow(window),96);}
inline HFONT Font(HWND window,int pixels,int weight=FW_NORMAL){
    struct Cache{
        std::map<std::tuple<UINT,int,int>,HFONT> fonts;
        ~Cache(){for(auto& [key,font]:fonts)DeleteObject(font);}
    };
    static Cache cache;
    const auto key=std::make_tuple(GetDpiForWindow(window),pixels,weight);
    auto& font=cache.fonts[key];
    if(!font)font=CreateFontW(-Px(window,pixels),0,0,0,weight,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    return font;
}
inline void Fill(HDC dc,RECT rect,COLORREF color){auto brush=CreateSolidBrush(color);FillRect(dc,&rect,brush);DeleteObject(brush);}
inline void Rounded(HDC dc,RECT rect,COLORREF fill,COLORREF stroke,int radius){
    const auto brush=CreateSolidBrush(fill);const auto pen=CreatePen(PS_SOLID,1,stroke);
    const auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);
    RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius,radius);
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(brush);DeleteObject(pen);
}
inline void TextAt(HDC dc,HWND window,const std::wstring& text,RECT rect,int pixels,COLORREF color,int weight=FW_NORMAL,UINT flags=DT_LEFT|DT_WORDBREAK){
    const auto previous=SelectObject(dc,Font(window,pixels,weight));
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);
    DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&rect,flags|DT_NOPREFIX);
    SelectObject(dc,previous);
}
inline std::wstring Label(HWND control){const int n=GetWindowTextLengthW(control);std::wstring text(n+1,L'\0');GetWindowTextW(control,text.data(),n+1);text.resize(n);return text;}
inline void PaintButton(HWND control,HDC dc){
    RECT r;GetClientRect(control,&r);const bool compact=r.bottom<Px(control,230);const int pad=Px(control,compact?16:22);
    const auto id=GetDlgCtrlID(control);const bool card=id>=3010 && id<=3013;
    const bool enabled=IsWindowEnabled(control),focus=GetFocus()==control,
        hover=GetPropW(control,L"RenderEngineHover")!=nullptr,
        pressed=(SendMessageW(control,BM_GETSTATE,0,0)&BST_PUSHED)!=0;
    const auto accent=id==3011?Violet:Accent;
    Fill(dc,r,Background);
    Rounded(dc,r,pressed?RGB(26,57,65):hover?RGB(25,39,55):Raised,
        focus?accent:hover?RGB(70,102,120):Border,Px(control,14));
    if(card){
        const auto art=static_cast<HBITMAP>(GetPropW(control,L"RenderEngineCardArt"));
        if(art){
            BITMAP info;GetObjectW(art,sizeof(info),&info);HDC source=CreateCompatibleDC(dc);auto previous=SelectObject(source,art);
            const int x=r.right*43/100,y=pad/2,w=r.right-x-pad/2,h=r.bottom-Px(control,48);
            const int half=info.bmWidth/2,offset=id==3011?half:0;
            const int saved=SaveDC(dc);
            // Fade into the plain card rather than adding another visible box.
            for(int i=0;i<20;++i){int left=w*i/20,right=w*(i+1)/20;BLENDFUNCTION blend={AC_SRC_OVER,0,static_cast<BYTE>((hover?125:95)*std::min(1.0,i/8.0)),0};
                AlphaBlend(dc,x+left,y,right-left,h,source,offset+half*i/20,0,half*(i+1)/20-half*i/20,info.bmHeight,blend);}
            RestoreDC(dc,saved);SelectObject(source,previous);DeleteDC(source);
        }
    }
    auto label=Label(control);
    if(!card){
        RECT text=r;InflateRect(&text,-Px(control,8),0);
        TextAt(dc,control,label,text,13,enabled?Text:Muted,FW_SEMIBOLD,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        return;
    }
    const auto split=label.find(L'\n');
    const auto title=label.substr(0,split),description=split==std::wstring::npos?L"":label.substr(split+1);
    const int icon=Px(control,compact?28:40),cx=pad+icon/2,cy=pad+icon/2;
    auto pen=CreatePen(PS_SOLID,Px(control,1),accent);const auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    if(id==3011){
        Ellipse(dc,cx-icon/2,cy-icon/5,cx+icon/2,cy+icon/5);
        Ellipse(dc,cx-icon/5,cy-icon/2,cx+icon/5,cy+icon/2);
    }else{
        Ellipse(dc,cx-icon/2,cy-icon/2,cx+icon/2,cy+icon/2);
        Ellipse(dc,cx-icon/2,cy-icon/5,cx+icon/2,cy+icon/5);
    }
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
    const int titleY=pad+icon+Px(control,compact?12:18);
    RECT titleRect{pad,titleY,r.right-pad,titleY+Px(control,32)};
    TextAt(dc,control,title,titleRect,compact?19:23,Text,FW_SEMIBOLD,DT_SINGLELINE|DT_END_ELLIPSIS);
    RECT body{pad,titleY+Px(control,compact?31:37),r.right-pad,r.bottom-Px(control,38)};
    TextAt(dc,control,description,body,compact?12:13,Muted);
    RECT footer{pad,r.bottom-Px(control,32),r.right-pad,r.bottom-Px(control,10)};
    TextAt(dc,control,id==3010?L"EXPLORE SYSTEMS  \u2192":id==3011?L"DISCOVER PARTICLE STATES  \u2192":L"ENTER SYSTEM  \u2192",footer,11,accent,FW_SEMIBOLD);
}
inline void PaintCanvas(HWND control,HDC dc){
    RECT r;GetClientRect(control,&r);Fill(dc,r,Background);
    // Quiet, deterministic decoration. No animation, textures or CPU polling.
    for(int i=0;i<95;++i){
        const int x=static_cast<int>((static_cast<unsigned>(i)*7919+137)%std::max(1L,r.right));
        const int y=static_cast<int>((static_cast<unsigned>(i)*3571+83)%std::max(1L,r.bottom));
        const int brightness=25+i%4*6;SetPixelV(dc,x,y,RGB(brightness,brightness+9,brightness+15));
    }
    auto pen=CreatePen(PS_SOLID,1,RGB(20,34,47));const auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    const int size=std::min(r.right,r.bottom);
    for(int i=0;i<3;++i)Ellipse(dc,r.right-size/2-i*size/5,-size/2-i*size/5,r.right+size/2+i*size/5,size/2+i*size/5);
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
}
inline LRESULT CALLBACK Procedure(HWND control,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR kind){
    if(message==WM_PAINT || message==WM_PRINTCLIENT){
        PAINTSTRUCT paint{};const auto dc=message==WM_PAINT?BeginPaint(control,&paint):reinterpret_cast<HDC>(w);
        if(kind==1)PaintButton(control,dc);else PaintCanvas(control,dc);
        if(message==WM_PAINT)EndPaint(control,&paint);return 0;
    }
    if(message==WM_ERASEBKGND)return 1;
    if(kind==1 && message==WM_MOUSEMOVE && !GetPropW(control,L"RenderEngineHover")){
        SetPropW(control,L"RenderEngineHover",reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,control,0};TrackMouseEvent(&tracking);InvalidateRect(control,nullptr,FALSE);
    }
    if(message==WM_MOUSELEAVE){RemovePropW(control,L"RenderEngineHover");InvalidateRect(control,nullptr,FALSE);}
    const auto result=DefSubclassProc(control,message,w,l);
    if(message==WM_SETFOCUS || message==WM_KILLFOCUS || message==WM_ENABLE || message==BM_SETSTATE || message==WM_SETTEXT)
        InvalidateRect(control,nullptr,FALSE);
    if(message==WM_NCDESTROY)RemovePropW(control,L"RenderEngineHover");
    return result;
}
inline void Button(HWND control){SetWindowSubclass(control,Procedure,0x5245,1);}
inline void Canvas(HWND control){SetWindowSubclass(control,Procedure,0x5245,2);}
inline bool DrawList(const DRAWITEMSTRUCT* item){
    if(!item || item->CtlType!=ODT_LISTBOX)return false;
    const auto control=item->hwndItem;Fill(item->hDC,item->rcItem,Panel);
    if(item->itemID==static_cast<UINT>(-1))return true;
    RECT rect=item->rcItem;InflateRect(&rect,-Px(control,3),-Px(control,2));
    const bool selected=(item->itemState&ODS_SELECTED)!=0;
    if(selected)Rounded(item->hDC,rect,RGB(23,55,63),RGB(35,83,91),Px(control,7));
    const auto length=SendMessageW(control,LB_GETTEXTLEN,item->itemID,0);
    if(length<0)return true;
    std::wstring text(static_cast<size_t>(length)+1,L'\0');SendMessageW(control,LB_GETTEXT,item->itemID,reinterpret_cast<LPARAM>(text.data()));text.resize(length);
    rect.left+=Px(control,12);rect.right-=Px(control,12);
    TextAt(item->hDC,control,text,rect,13,selected?Accent:Text,selected?FW_SEMIBOLD:FW_NORMAL,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
    if(item->itemState&ODS_FOCUS){RECT focus=item->rcItem;InflateRect(&focus,-2,-2);DrawFocusRect(item->hDC,&focus);}
    return true;
}
inline void DarkTitle(HWND window){
    const BOOL dark=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    const COLORREF color=Background;DwmSetWindowAttribute(window,DWMWA_CAPTION_COLOR,&color,sizeof(color));
}
}
#endif
