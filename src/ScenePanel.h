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
    void SetVisible(bool show);
    void SetTitle(const std::string &title);
    void SetCatalogue(const std::vector<std::pair<int, std::string>> &items);
    void Filter();
    void SetInformation(const std::string &text);
    void SetStatus(const std::string &text);
    void SetPlanets(const std::vector<std::string> &names);
    void ShowBodies(bool show);
    int SelectedStar() const;
    int SelectedPlanet() const;
    int Width() const
    {
        return visible ? widthPixels : 0;
    }
    bool Visible() const
    {
        return visible;
    }
    void *ControlColor(void *dc) const;
    void Release();

  private:
    bool visible = true;
    bool bodiesVisible = true;
    int widthPixels = 0;
    void *parent = nullptr;
    void *font = nullptr;
    void *backgroundBrush = nullptr;
    std::vector<void *> controls;
    void *statusFont = nullptr;
    std::vector<std::pair<int, std::string>> catalogue;
    std::vector<std::string> lowercaseNames;
    std::string lastInformation;
    std::string lastStatus;
};
