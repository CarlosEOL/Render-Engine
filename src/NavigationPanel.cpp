#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#endif
#include "NavigationPanel.h"
#include "UiTheme.h"
#include "Astronomy.h"
#include "StellarPhotometry.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <stdexcept>
#ifdef _WIN32
namespace {
std::string Number(double value,int precision=3){std::ostringstream out;out<<std::setprecision(precision)<<value;return out.str();}
int Icon(HIMAGELIST images,const NavigationEntry& entry){
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,96,96);ReleaseDC(nullptr,screen);auto old=SelectObject(dc,bitmap);UiTheme::Fill(dc,{0,0,96,96},RGB(255,0,255));
    const auto tint=entry.color;const bool stellar=entry.kind==NavigationEntry::Kind::Star,core=entry.kind==NavigationEntry::Kind::Core;
    if(entry.kind==NavigationEntry::Kind::Barycentre || entry.kind==NavigationEntry::Kind::Galaxy || entry.kind==NavigationEntry::Kind::Universe || entry.kind==NavigationEntry::Kind::Group){
        auto pen=CreatePen(PS_SOLID,2,UiTheme::Accent);auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));Ellipse(dc,9,29,87,67);Ellipse(dc,31,10,65,85);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
    }else{
        if(entry.name=="Saturn"){auto ring=CreatePen(PS_SOLID,9,RGB(165,147,113));auto oldPen=SelectObject(dc,ring),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));Ellipse(dc,2,36,94,69);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(ring);}
        for(int i=0;i<30;++i){const double shade=stellar?.75+i*.008:core?.05:.12+i*.025;const int r=static_cast<int>(std::clamp(tint.r*shade*255,0.0,255.0)),g=static_cast<int>(std::clamp(tint.g*shade*255,0.0,255.0)),b=static_cast<int>(std::clamp(tint.b*shade*255,0.0,255.0));auto brush=CreateSolidBrush(RGB(r,g,b));auto previous=SelectObject(dc,brush),pen=SelectObject(dc,GetStockObject(NULL_PEN));Ellipse(dc,14+i/3,14+i/3,83-i,83-i);SelectObject(dc,previous);SelectObject(dc,pen);DeleteObject(brush);}
        if(core){auto pen=CreatePen(PS_SOLID,3,RGB(230,170,84));auto previous=SelectObject(dc,pen),brush=SelectObject(dc,GetStockObject(NULL_BRUSH));Ellipse(dc,9,35,89,61);SelectObject(dc,brush);SelectObject(dc,previous);DeleteObject(pen);}
    }
    SelectObject(dc,old);DeleteDC(dc);int index=ImageList_AddMasked(images,bitmap,RGB(255,0,255));DeleteObject(bitmap);return index;
}
}
struct NavigationPanelWin32 {
    static RECT Card(HWND list,int index){
        RECT icon;ListView_GetItemRect(list,index,&icon,LVIR_ICON);
        const int width=UiTheme::Px(list,170),center=(icon.left+icon.right)/2;
        return {center-width/2+4,icon.top-4,center+width/2-4,icon.top+UiTheme::Px(list,166)};
    }
    static LRESULT PaintCard(NavigationPanel& panel,NMLVCUSTOMDRAW* draw){
        const int index=static_cast<int>(draw->nmcd.dwItemSpec);if(index<0 || index>=static_cast<int>(panel.entries.size()))return CDRF_DODEFAULT;
        const auto list=draw->nmcd.hdr.hwndFrom;const auto& entry=panel.entries[index];const bool selected=(ListView_GetItemState(list,index,LVIS_SELECTED)&LVIS_SELECTED)!=0;
        RECT card=Card(list,index);UiTheme::Rounded(draw->nmcd.hdc,card,selected?RGB(24,48,61):UiTheme::Panel,selected?UiTheme::Accent:UiTheme::Border,UiTheme::Px(list,8));
        const int center=(card.left+card.right)/2,top=card.top+UiTheme::Px(list,8);
        ImageList_Draw(static_cast<HIMAGELIST>(panel.images),index,draw->nmcd.hdc,center-48,top,ILD_TRANSPARENT);
        RECT title{card.left+8,top+100,card.right-8,top+100+UiTheme::Px(list,35)};
        std::wstring name(entry.name.begin(),entry.name.end());UiTheme::TextAt(draw->nmcd.hdc,list,name,title,12,selected?UiTheme::Accent:UiTheme::Text,FW_SEMIBOLD,DT_CENTER|DT_WORDBREAK);
        std::wostringstream detail;
        if(entry.diameterKm>0)detail<<std::fixed<<std::setprecision(entry.diameterKm<1000?1:0)<<entry.diameterKm<<L" km";
        if(entry.temperature>0)detail<<L"\n"<<std::fixed<<std::setprecision(0)<<entry.temperature-273.15<<L" C";
        if(entry.satelliteCount>0)detail<<L"\n"<<entry.satelliteCount<<L" satellites";
        RECT body{card.left+8,title.bottom,card.right-8,card.bottom-8};UiTheme::TextAt(draw->nmcd.hdc,list,detail.str(),body,11,UiTheme::Muted,FW_NORMAL,DT_CENTER|DT_WORDBREAK);
        return CDRF_SKIPDEFAULT;
    }
    static LRESULT CALLBACK Control(HWND control,UINT message,WPARAM key,LPARAM detail,UINT_PTR,DWORD_PTR data){
        auto& panel=*reinterpret_cast<NavigationPanel*>(data);
        if(GetDlgCtrlID(control)==5 && panel.mode==NavigationPanel::Mode::Overview && (message==WM_LBUTTONDOWN || message==WM_LBUTTONDBLCLK)){
            POINT point{GET_X_LPARAM(detail),GET_Y_LPARAM(detail)};
            for(int i=0;i<static_cast<int>(panel.entries.size());++i){const auto card=Card(control,i);if(PtInRect(&card,point)){
                ListView_SetItemState(control,-1,0,LVIS_SELECTED|LVIS_FOCUSED);ListView_SetItemState(control,i,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);SetFocus(control);
                if(message==WM_LBUTTONDBLCLK){panel.Dispatch(NavigationPanel::Action::Open);panel.Refresh();}return 0;
            }}
        }
        if(message==WM_KEYDOWN){
            if(key==VK_ESCAPE || key==VK_F3 || key==VK_F1){panel.Hide();return 0;}
            if(key==VK_RETURN){if(GetDlgCtrlID(control)==2)panel.Refresh();else panel.Dispatch(NavigationPanel::Action::Select);return 0;}
        }
        return DefSubclassProc(control,message,key,detail);
    }
    static LRESULT CALLBACK Header(HWND header,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR,DWORD_PTR){
        if(message==WM_PAINT || message==WM_PRINTCLIENT){PAINTSTRUCT paint={};HDC dc=message==WM_PAINT?BeginPaint(header,&paint):reinterpret_cast<HDC>(wParam);RECT r;GetClientRect(header,&r);UiTheme::Fill(dc,r,UiTheme::Raised);for(int i=0;i<Header_GetItemCount(header);++i){wchar_t text[128];HDITEMW item={};item.mask=HDI_TEXT;item.pszText=text;item.cchTextMax=128;Header_GetItem(header,i,&item);Header_GetItemRect(header,i,&r);r.left+=10;UiTheme::TextAt(dc,header,text,r,12,UiTheme::Muted,FW_SEMIBOLD,DT_LEFT|DT_VCENTER|DT_SINGLELINE);}if(message==WM_PAINT)EndPaint(header,&paint);return 0;}
        return DefSubclassProc(header,message,wParam,lParam);
    }
    static LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
        auto* panel=reinterpret_cast<NavigationPanel*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){panel=static_cast<NavigationPanel*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);panel->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(panel));}
        if(!panel)return DefWindowProcW(window,message,wParam,lParam);
        switch(message){
        case WM_CREATE:panel->Build();return 0;
        case WM_SIZE:panel->Layout();return 0;
        case WM_CLOSE:panel->Hide();return 0;
        case WM_TIMER:KillTimer(window,1);panel->Refresh();return 0;
        case WM_COMMAND:
            if(LOWORD(wParam)==2 && HIWORD(wParam)==EN_CHANGE && panel->mode==NavigationPanel::Mode::Finder)SetTimer(window,1,180,nullptr);
            if(HIWORD(wParam)==BN_CLICKED){switch(LOWORD(wParam)){case 3:panel->Refresh();break;case 4:panel->Dispatch(NavigationPanel::Action::Current);break;case 7:panel->Dispatch(NavigationPanel::Action::Select);break;case 8:panel->Dispatch(NavigationPanel::Action::Go);break;case 9:panel->Dispatch(NavigationPanel::Action::Back);break;case 10:panel->Dispatch(NavigationPanel::Action::Open);break;case 11:panel->Hide();break;}}return 0;
        case WM_NOTIFY:{const auto* header=reinterpret_cast<NMHDR*>(lParam);
            if(header->idFrom==5 && header->code==LVN_ITEMCHANGED && !panel->filling){const auto* item=reinterpret_cast<NMLISTVIEW*>(lParam);if((item->uChanged&LVIF_STATE) && (item->uNewState&LVIS_SELECTED) && !(item->uOldState&LVIS_SELECTED))panel->Dispatch(NavigationPanel::Action::Select);return 0;}
            if(header->idFrom==5 && header->code==NM_DBLCLK){panel->Dispatch(panel->mode==NavigationPanel::Mode::Overview?NavigationPanel::Action::Open:NavigationPanel::Action::Select);return 0;}
            if(header->idFrom==5 && header->code==NM_CUSTOMDRAW){auto* draw=reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);if(draw->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;if(draw->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){if(panel->mode==NavigationPanel::Mode::Overview)return PaintCard(*panel,draw);draw->clrText=UiTheme::Text;draw->clrTextBk=draw->nmcd.dwItemSpec%2?UiTheme::Raised:UiTheme::Panel;return CDRF_DODEFAULT;}}
            break;}
        case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor(reinterpret_cast<HDC>(wParam),UiTheme::Text);SetBkColor(reinterpret_cast<HDC>(wParam),UiTheme::Panel);return reinterpret_cast<LRESULT>(panel->brush);
        case WM_ERASEBKGND:{RECT r;GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(wParam),&r,static_cast<HBRUSH>(panel->brush));return 1;}
        }
        return DefWindowProcW(window,message,wParam,lParam);
    }
};
#endif
bool NavigationPanel::Create(void* parent,Search find,Activate select){owner=parent;search=std::move(find);activate=std::move(select);
#ifdef _WIN32
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_LISTVIEW_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW type={};type.lpfnWndProc=NavigationPanelWin32::Procedure;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.lpszClassName=L"RenderEngineNavigation";
    if(!RegisterClassW(&type) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    brush=CreateSolidBrush(UiTheme::Panel);RECT r;GetWindowRect(static_cast<HWND>(parent),&r);
    window=CreateWindowExW(WS_EX_TOOLWINDOW,type.lpszClassName,L"Find object",WS_OVERLAPPEDWINDOW,r.left+60,r.top+60,860,620,static_cast<HWND>(parent),nullptr,type.hInstance,this);
    if(!window){Release();return false;}UiTheme::DarkTitle(static_cast<HWND>(window));return true;
#else
    return false;
#endif
}
void NavigationPanel::Build(){
#ifdef _WIN32
    auto native=static_cast<HWND>(window);const auto add=[&](const wchar_t* type,const wchar_t* label,int id,DWORD style){auto control=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style,0,0,0,0,native,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(UiTheme::Font(native,13)),TRUE);SetWindowSubclass(control,NavigationPanelWin32::Control,1,reinterpret_cast<DWORD_PTR>(this));if(std::wstring(type)==L"BUTTON")UiTheme::Button(control);return control;};
    add(L"STATIC",L"Enter object name",1,0);add(L"EDIT",L"",2,WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP);add(L"BUTTON",L"Search",3,WS_TABSTOP);add(L"BUTTON",L"Current",4,WS_TABSTOP);
    auto list=add(WC_LISTVIEWW,L"",5,LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);ListView_SetBkColor(list,UiTheme::Panel);ListView_SetTextBkColor(list,UiTheme::Panel);ListView_SetTextColor(list,UiTheme::Text);
    SetWindowSubclass(ListView_GetHeader(list),NavigationPanelWin32::Header,1,0);
    add(L"STATIC",L"",6,0);add(L"BUTTON",L"Go to",8,WS_TABSTOP);add(L"BUTTON",L"Back",9,WS_TABSTOP);add(L"BUTTON",L"Open satellites",10,WS_TABSTOP);add(L"BUTTON",L"Close",11,WS_TABSTOP);
#endif
}
void NavigationPanel::Layout(){
#ifdef _WIN32
    if(!window)return;auto native=static_cast<HWND>(window);RECT r;GetClientRect(native,&r);int m=UiTheme::Px(native,16),h=UiTheme::Px(native,32),w=r.right;
    const bool overview=mode==Mode::Overview;const auto place=[&](int id,int x,int y,int width,int height){MoveWindow(GetDlgItem(native,id),x,y,std::max(1,width),std::max(1,height),TRUE);};
    place(1,m,m,w-2*m,h);place(2,m,m+h,w-3*m-220,h);place(3,w-m-210,m+h,100,h);place(4,w-m-100,m+h,100,h);
    place(9,m,m,100,h);place(5,m,overview?m+h+8:m+2*h+12,w-2*m,r.bottom-(overview?m+h+8:m+2*h+12)-2*h-m-12);
    place(6,m,r.bottom-2*h-m,w-2*m,h);place(8,w-m-210,r.bottom-h-m,100,h);place(11,w-m-100,r.bottom-h-m,100,h);place(10,m,r.bottom-h-m,180,h);
    for(int id:{2,3,4})ShowWindow(GetDlgItem(native,id),overview?SW_HIDE:SW_SHOW);ShowWindow(GetDlgItem(native,9),overview?SW_SHOW:SW_HIDE);ShowWindow(GetDlgItem(native,10),overview?SW_SHOW:SW_HIDE);
    if(overview)place(1,m+115,m,w-2*m-115,h);
#endif
}
bool NavigationPanel::Visible() const{
#ifdef _WIN32
    return window && IsWindowVisible(static_cast<HWND>(window));
#else
    return false;
#endif
}
void NavigationPanel::Toggle(Mode value,bool show){
#ifdef _WIN32
    if(!window)return;if(show && Visible() && mode==value){Hide();return;}mode=value;auto native=static_cast<HWND>(window);
    SetWindowTextW(native,mode==Mode::Finder?L"Find object":mode==Mode::Nearby?L"Nearby star systems":L"System overview");
    SetDlgItemTextW(native,1,mode==Mode::Finder?L"Enter object name":mode==Mode::Nearby?L"Search radius (light-years)":L"System hierarchy");
    SetDlgItemTextW(native,2,mode==Mode::Nearby?L"10":L"");
    auto list=GetDlgItem(native,5);SetWindowLongPtrW(list,GWL_STYLE,(GetWindowLongPtrW(list,GWL_STYLE)&~LVS_TYPEMASK)|(mode==Mode::Overview?LVS_ICON:LVS_REPORT));
    RECT ownerRect;GetWindowRect(static_cast<HWND>(owner),&ownerRect);
    const int width=std::min(static_cast<int>(ownerRect.right-ownerRect.left)-40,UiTheme::Px(native,mode==Mode::Overview?1400:920));
    const int height=std::min(static_cast<int>(ownerRect.bottom-ownerRect.top)-80,UiTheme::Px(native,mode==Mode::Overview?370:620));
    SetWindowPos(native,nullptr,ownerRect.left+20,ownerRect.top+60,width,height,SWP_NOZORDER);Layout();Refresh();if(show){ShowWindow(native,SW_SHOW);SetFocus(GetDlgItem(native,mode==Mode::Overview?5:2));}
#endif
}
void NavigationPanel::Refresh(){
#ifdef _WIN32
    if(!window || !search)return;KillTimer(static_cast<HWND>(window),1);char query[512];GetDlgItemTextA(static_cast<HWND>(window),2,query,512);
    try{double radius=10;if(mode==Mode::Nearby){size_t used=0;radius=std::stod(query,&used);if(used!=std::string(query).size() || !std::isfinite(radius) || radius<=0)throw std::invalid_argument("Enter a positive search radius in light-years.");}
        entries=search(mode,query,radius);Fill();SetDlgItemTextA(static_cast<HWND>(window),6,(std::to_string(entries.size())+(entries.size()==1000?" results shown; narrow the search":" objects")+"  |  Click selects; Go to travels.").c_str());
    }catch(const std::exception& error){SetDlgItemTextA(static_cast<HWND>(window),6,error.what());}
#endif
}
void NavigationPanel::Fill(){
#ifdef _WIN32
    auto native=static_cast<HWND>(window),list=GetDlgItem(native,5);filling=true;SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);
    while(ListView_DeleteColumn(list,0)){};
    const wchar_t* finderColumns[]={L"Object",L"Parent",L"Type"};const wchar_t* nearColumns[]={L"System",L"Class",L"Distance (ly)",L"Luminosity (Sun)",L"Est. bol. mag",L"Planets"};
    for(int i=0;i<(mode==Mode::Nearby?6:3);++i){LVCOLUMNW column={};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t*>(mode==Mode::Nearby?nearColumns[i]:finderColumns[i]);column.cx=UiTheme::Px(native,i<2?mode==Mode::Nearby?200:220:mode==Mode::Nearby?110:330);ListView_InsertColumn(list,i,&column);}
    if(images){ListView_SetImageList(list,nullptr,LVSIL_NORMAL);ImageList_Destroy(static_cast<HIMAGELIST>(images));images=nullptr;}
    if(mode==Mode::Overview){images=ImageList_Create(96,96,ILC_COLOR32|ILC_MASK,32,16);ListView_SetImageList(list,static_cast<HIMAGELIST>(images),LVSIL_NORMAL);ListView_SetIconSpacing(list,UiTheme::Px(native,170),UiTheme::Px(native,170));SetDlgItemTextA(native,1,scopeName.c_str());}
    int index=0;for(const auto& entry:entries){
        std::string label=entry.name;if(mode==Mode::Overview){if(entry.diameterKm>0)label+="\n"+Number(entry.diameterKm,5)+" km";if(entry.temperature>0)label+="\n"+Number(entry.temperature-273.15,3)+" C";}
        LVITEMA item={};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=index;item.pszText=label.data();item.lParam=index;if(mode==Mode::Overview){item.mask|=LVIF_IMAGE;item.iImage=Icon(static_cast<HIMAGELIST>(images),entry);}SendMessageA(list,LVM_INSERTITEMA,0,reinterpret_cast<LPARAM>(&item));
        const auto cell=[&](int column,const std::string& text){LVITEMA sub={};sub.iSubItem=column;sub.pszText=const_cast<char*>(text.c_str());SendMessageA(list,LVM_SETITEMTEXTA,index,reinterpret_cast<LPARAM>(&sub));};
        if(mode==Mode::Nearby){cell(1,entry.type);cell(2,Number(entry.distanceAu/Astronomy::auPerLightYear,6));cell(3,entry.luminosity>0?Number(entry.luminosity):"--");double pc=entry.distanceAu/206264.80624709636;cell(4,entry.luminosity>0 && pc>0?Number(StellarPhotometry::ApparentMagnitude(entry.luminosity,entry.distanceAu)):"--");cell(5,entry.planetCount>=0?std::to_string(entry.planetCount):"--");}
        else{cell(1,entry.parent);cell(2,entry.type);}++index;
    }
    SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);filling=false;
#endif
}
void NavigationPanel::Dispatch(Action action){
#ifdef _WIN32
    if(filling || !activate)return;int index=ListView_GetNextItem(GetDlgItem(static_cast<HWND>(window),5),-1,LVNI_SELECTED);
    if(action!=Action::Back && action!=Action::Current && (index<0 || index>=static_cast<int>(entries.size())))return;
    activate(action,index>=0 && index<static_cast<int>(entries.size())?entries[index]:NavigationEntry{});
    if(action==Action::Open || action==Action::Back || action==Action::Current)Refresh();
#endif
}
void NavigationPanel::Hide(){
#ifdef _WIN32
    if(window){KillTimer(static_cast<HWND>(window),1);ShowWindow(static_cast<HWND>(window),SW_HIDE);}if(owner)SetFocus(static_cast<HWND>(owner));
#endif
}
void NavigationPanel::Release(){
#ifdef _WIN32
    if(window)DestroyWindow(static_cast<HWND>(window));if(images)ImageList_Destroy(static_cast<HIMAGELIST>(images));if(brush)DeleteObject(static_cast<HBRUSH>(brush));
#endif
    window=images=brush=nullptr;
}

void NavigationPanel::SetQuery(const std::string& text){
#ifdef _WIN32
    if(window)SetDlgItemTextA(static_cast<HWND>(window),2,text.c_str());
#endif
}
