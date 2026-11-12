#pragma once
class ControlsPanel {
public:
    bool Create(void* owner);
    void Toggle();
    void Hide();
    void Release();
private:
    friend struct ControlsPanelWin32;
    void* window=nullptr;
    void* owner=nullptr;
    void* brush=nullptr;
    void* font=nullptr;
};
