#pragma once
#include <string>
#include <vector>

// Native controls remain accessible to keyboard navigation and UI automation.
class AppMenu
{
  public:
    enum class Page
    {
        Home,
        Planetorium,
        Quantum
    };
    void Create(void *parent, const std::vector<std::string> &quantumNames);
    void Show(Page page);
    void Hide();
    void Resize(int width, int height);
    bool Visible() const
    {
        return visible;
    }
    int SelectedQuantum() const;
    void SetQuantumInformation(const std::string &text);
    void *ControlColor(void *dc) const;
    void Release();

  private:
    Page page = Page::Home;
    bool visible = true;
    void *parent = nullptr;
    void *menu = nullptr;
    void *font = nullptr;
    void *titleFont = nullptr;
    void *brush = nullptr;
    std::vector<void *> controls;
};
