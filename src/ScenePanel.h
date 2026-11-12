#pragma once
#include <string>
#include <utility>
#include <vector>

class ScenePanel
{
  public:
    void Create(void *parent);
    void Resize(int width, int height);
    void Toggle();
    void ToggleBrowser(bool currentSystemOnly);
    void ShowBrowser(bool local) { browserVisible=false; ToggleBrowser(local); }
    bool IsCurrentSystemBrowser() const { return currentSystemOnly; }
    void ToggleInformation();
    void SetCurrentSystemName(const std::string& name);
    void SetVisible(bool show);
    void SetTitle(const std::string &title);
    void SetCatalogue(std::vector<std::pair<int, std::string>> items);
    void Filter();
    void SetInformation(const std::string &text);
    void SetPlanets(const std::vector<std::string> &names);
    void SetPlayback(bool playing, const std::string& clock);
    int SelectedStar() const;
    int SelectedPlanet() const;
    int Width() const
    {
        return visible && (browserVisible || informationVisible) ? widthPixels : 0;
    }
    bool Visible() const
    {
        return visible;
    }
    void *ControlColor(void *dc) const;
    void Release();

  private:
    bool visible = true;
    bool browserVisible = true;
    bool informationVisible = true;
    bool currentSystemOnly = true;
    void RefreshVisibility();
    std::string systemName;
    int widthPixels = 0;
    void *parent = nullptr;
    void *font = nullptr;
    void *backgroundBrush = nullptr;
    std::vector<void *> controls;
    std::vector<std::pair<int, std::string>> catalogue;
    std::string lastInformation;
};
