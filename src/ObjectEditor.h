#pragma once
#include <functional>
#include <string>

// Modeless, selection-bound native tool window; it owns no graphics resources.
class ObjectEditor {
public:
  bool Create(void *owner, std::function<void(double)> applySpin);
  void SetObject(const std::string &name, bool editable, double spin,
                 double referenceRadiusAu);
  void Toggle();
  void Hide();
  bool Visible() const { return visible; }
  bool Editable() const { return editable; }
  void *NativeHandle() const { return window; }
  void Release();
  enum Control {
    SpinValue = 4101,
    SpinSlider = 4102,
    Apply = 4103,
    Reset = 4104,
    Decrease = 4105,
    Increase = 4106,
    Close = 4107,
    ObjectName = 4108,
    Details = 4109,
    Status = 4110
  };

private:
  friend struct ObjectEditorWin32;
  void BuildControls();
  void Layout();
  void SyncControls();
  void ApplyValue(double value);
  void ApplyText();
  void SetStatus(const wchar_t *text);
  void *window = nullptr;
  void *owner = nullptr;
  void *font = nullptr;
  void *brush = nullptr;
  bool visible = false, editable = false, syncing = false;
  double spin = .8, referenceRadiusAu = 1;
  std::string objectName;
  std::function<void(double)> applySpin;
};
