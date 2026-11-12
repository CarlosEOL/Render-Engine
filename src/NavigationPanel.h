#pragma once
#include "NavigationCatalog.h"
#include <functional>
class NavigationPanel {
public:
    enum class Mode {Finder,Nearby,Overview};
    enum class Action {Select,Go,Open,Back,Current};
    using Search=std::function<std::vector<NavigationEntry>(Mode,const std::string&,double)>;
    using Activate=std::function<void(Action,const NavigationEntry&)>;
    bool Create(void* owner,Search search,Activate activate);
    void Toggle(Mode mode,bool show=true);
    void SetQuery(const std::string& text);
    void Refresh();
    void Hide();
    void Release();
    void* NativeHandle() const{return window;}
    bool Visible() const;
    Mode CurrentMode() const{return mode;}
    void SetScopeName(const std::string& name){scopeName=name;}
private:
    friend struct NavigationPanelWin32;
    void Build();void Layout();void Fill();void Dispatch(Action action);
    void *window=nullptr,*owner=nullptr,*brush=nullptr,*images=nullptr;
    Mode mode=Mode::Finder;
    Search search;Activate activate;
    std::string scopeName;
    std::vector<NavigationEntry> entries;
    bool filling=false;
};
