#include "Dx12Renderer.h"
#include "Astronomy.h"
#include "KerrSpacetime.h"
#include "ObjectEditor.h"
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace {
struct Application {
  HWND window = nullptr;
  NativeDX12::Renderer renderer;
  ObjectEditor editor;
  NativeDX12::Frame frame;
  glm::dvec3 camera{0, -28, 6}, forward{0, 1, 0}, up{0, 0, 1};
  double speed = .15, seconds = 0, timeScale = 3600, fov = 50;
  UINT pendingWidth = 1280, pendingHeight = 720;
  bool dirty = true, resetExposure = true, playing = false, minimized = false,
       dragging = false;
  POINT lastMouse{};
  void Reset() {
    camera = {0, -28, 6};
    forward = glm::normalize(-camera);
    up = glm::normalize(
        glm::cross(glm::cross(forward, glm::dvec3(0, 0, 1)), forward));
    fov = 50;
    dirty = resetExposure = true;
  }
  void Spin(double value) {
    if (!std::isfinite(value))
      return;
    frame.metric.y = static_cast<float>(std::clamp(value, -.98, .98));
    dirty = resetExposure = true;
    editor.SetObject("Gargantua", true, frame.metric.y,
                     Astronomy::gargantuaHorizonAu);
  }
  void Mode(int value) {
    frame.optics.z = static_cast<float>(value);
    resetExposure = true;
  }
  void Fill(double realSeconds) {
    frame.camera = glm::vec4(camera, 0);
    frame.forward = glm::vec4(forward, 0);
    frame.up = glm::vec4(up, 0);
    frame.right = glm::vec4(glm::normalize(glm::cross(forward, up)), 0);
    frame.metric.x = static_cast<float>(Astronomy::gargantuaHorizonAu);
    frame.metric.z = static_cast<float>(
        KerrSpacetime::ProgradeIsco(frame.metric.x, frame.metric.y));
    frame.optics.x = static_cast<float>(std::tan(glm::radians(fov) * .5));
    frame.optics.w = static_cast<float>(std::fmod(seconds, 1e7));
    frame.viewport.z = static_cast<float>(realSeconds);
    frame.viewport.w = resetExposure ? 1.0f : 0.0f;
  }
  void Command(UINT command) {
    switch (command) {
    case 101:
      playing = !playing;
      break;
    case 102:
      Reset();
      break;
    case 103:
      editor.Toggle();
      break;
    case 110:
      Mode(0);
      break;
    case 111:
      Mode(1);
      break;
    case 112:
      Mode(2);
      break;
    case 113:
      frame.display.x = 1 - frame.display.x;
      resetExposure = true;
      break;
    case 114:
      DestroyWindow(window);
      break;
    }
  }
};
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w, LPARAM l) {
  auto *app =
      reinterpret_cast<Application *>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    app = static_cast<Application *>(
        reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    app->window = window;
  }
  if (!app)
    return DefWindowProcW(window, message, w, l);
  switch (message) {
  case WM_SIZE:
    app->minimized = w == SIZE_MINIMIZED;
    app->pendingWidth = LOWORD(l);
    app->pendingHeight = HIWORD(l);
    return 0;
  case WM_COMMAND:
    app->Command(LOWORD(w));
    return 0;
  case WM_KEYDOWN:
    if (l & (1LL << 30))
      return 0;
    if (w == VK_SPACE)
      app->playing = !app->playing;
    else if (w == VK_F5)
      app->editor.Toggle();
    else if (w == VK_F7)
      app->Mode((static_cast<int>(app->frame.optics.z) + 1) % 3);
    else if (w == VK_HOME)
      app->Reset();
    else if (w == VK_OEM_COMMA)
      app->timeScale = std::max(.001, app->timeScale / 10);
    else if (w == VK_OEM_PERIOD)
      app->timeScale = std::min(1e7, app->timeScale * 10);
    else if (w == VK_OEM_PLUS)
      app->frame.optics.y = std::min(12.0f, app->frame.optics.y + .25f);
    else if (w == VK_OEM_MINUS)
      app->frame.optics.y = std::max(-18.0f, app->frame.optics.y - .25f);
    return 0;
  case WM_RBUTTONDOWN:
    app->dragging = true;
    app->lastMouse = {static_cast<short>(LOWORD(l)),
                      static_cast<short>(HIWORD(l))};
    SetCapture(window);
    return 0;
  case WM_RBUTTONUP:
    app->dragging = false;
    ReleaseCapture();
    return 0;
  case WM_CAPTURECHANGED:
    app->dragging = false;
    return 0;
  case WM_MOUSEMOVE:
    if (app->dragging) {
      const POINT point{static_cast<short>(LOWORD(l)),
                        static_cast<short>(HIWORD(l))};
      const double dx = (point.x - app->lastMouse.x) * .002,
                   dy = (point.y - app->lastMouse.y) * .002;
      app->lastMouse = point;
      auto right = glm::normalize(glm::cross(app->forward, app->up));
      auto rotation = glm::angleAxis(-dx, app->up) * glm::angleAxis(-dy, right);
      app->forward = glm::normalize(rotation * app->forward);
      app->up = glm::normalize(rotation * app->up);
      app->dirty = true;
    }
    return 0;
  case WM_MOUSEWHEEL: {
    const double steps = static_cast<short>(HIWORD(w)) / 120.0;
    if (LOWORD(w) & MK_SHIFT) {
      app->fov = std::clamp(app->fov * std::pow(1.08, -steps), 1.0, 110.0);
      app->dirty = true;
    } else
      app->speed = std::clamp(app->speed * std::pow(1.12, steps), 1e-9, 1000.0);
    return 0;
  }
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window, message, w, l);
}
void Require(bool value, const char *reason) {
  if (!value)
    throw std::runtime_error(reason);
}
int Smoke(Application &app, const std::filesystem::path &path) {
  std::filesystem::create_directories(path);
  std::ofstream report(path / "report.txt");
  report << "Native DX12 migration preview\nAdapter: "
         << app.renderer.AdapterName()
         << "\nDebug layer: " << app.renderer.DebugLayer() << "\n";
  for (double spin : {-.8, 0.0, .8, .98}) {
    NativeDX12::Frame probe;
    probe.camera = {20, 2, 3, 0};
    probe.forward = glm::vec4(glm::normalize(glm::dvec3(-1, .24, -.04)), 0);
    probe.metric = {2, static_cast<float>(spin), .02, 0};
    auto reference = KerrSpacetime::PastRay(
        glm::dvec3(probe.camera), glm::dvec3(probe.forward), 2, probe.metric.y);
    KerrSpacetime::AdvanceRay(reference, 2, probe.metric.y, probe.metric.z);
    const auto gpu = app.renderer.ProbeKerr(probe);
    const double error =
        glm::length(glm::dvec3(gpu[0], gpu[1], gpu[2]) - reference.position);
    Require(error < 5e-5 && std::abs(gpu[3] - reference.timeMomentum) < 5e-5,
            "HLSL Kerr reference diverges from CPU");
    report << "Kerr spin " << spin << ": GPU/CPU position error=" << error
           << " AU\n";
  }
  auto capture = [&](const char *name, bool dirty = true) {
    app.Fill(1.0 / 60);
    app.renderer.Draw(app.frame, dirty, false);
    app.resetExposure = false;
    auto image = app.renderer.Capture(path / (std::string(name) + ".ppm"));
    size_t lit = 0;
    for (size_t i = 0; i < image.size(); i += 3)
      if (std::max({image[i], image[i + 1], image[i + 2]}) > 16)
        ++lit;
    std::cout << name << ": " << app.renderer.Width() << "x"
              << app.renderer.Height() << ", lit=" << lit << "\n";
    report << name << ": lit=" << lit << "\n";
    Require(lit > 500, "Native scene is unexpectedly empty");
    return image;
  };
  app.Spin(.8);
  const auto kerr = capture("dx12-kerr-hdr");
  const auto draws = app.renderer.SceneDrawCount();
  const auto cached = capture("dx12-paused-cache", false);
  Require(cached == kerr && app.renderer.SceneDrawCount() == draws,
          "Paused DX12 scene was recomputed or changed");
  Require(app.renderer.SceneFinite(),
          "Native HDR scene contains nonfinite values");
  app.Spin(0);
  const auto zero = capture("dx12-spin-zero");
  Require(zero != kerr, "DX12 spin has no visible effect");
  app.Spin(-.8);
  const auto reverse = capture("dx12-reverse-spin");
  Require(reverse != zero && reverse != kerr, "Reverse spin has no effect");
  app.Spin(.8);
  app.Mode(1);
  const auto automatic = capture("dx12-pure-auto");
  const auto ev = app.renderer.ReadExposure();
  for (float value : ev)
    Require(std::isfinite(value), "DX12 exposure contains nonfinite values");
  Require(ev[0] >= -18 && ev[0] <= 8 && std::abs(ev[0] - ev[1]) < .001,
          "DX12 exposure failed initialization");
  app.frame.optics.y = 1;
  const auto brighter = capture("dx12-auto-plus-one-ev", false);
  Require(brighter != automatic, "Manual EV has no effect");
  app.frame.optics.y = 0;
  app.Mode(0);
  app.renderer.Resize(997, 563);
  capture("dx12-resized-odd-dimensions");
  app.renderer.Resize(1, 1);
  app.Fill(.016);
  app.renderer.Draw(app.frame, true, false);
  app.renderer.WaitIdle();
  app.renderer.Resize(960, 540);
  capture("dx12-restored");
  app.editor.Toggle();
  Require(app.editor.Visible() && app.editor.Editable(),
          "Native spin editor failed");
  SetDlgItemTextW(static_cast<HWND>(app.editor.NativeHandle()),
                  ObjectEditor::SpinValue, L"0.6");
  SendMessageW(static_cast<HWND>(app.editor.NativeHandle()), WM_COMMAND,
               ObjectEditor::Apply, 0);
  Require(std::abs(app.frame.metric.y - .6) < 1e-6,
          "Native editor did not update DX12 state");
  app.editor.Hide();
  capture("dx12-editor-spin-06");
  app.renderer.WaitIdle();
  const unsigned errors = app.renderer.ErrorCount();
  Require(errors == 0, "DX12 debug validation errors");
  report << "Exposure EV=" << ev[0] << ", target=" << ev[1]
         << "\nValidation errors=" << errors << "\nPASS\n";
  std::cout << "Native DX12 checks PASS; validation errors=" << errors << "\n";
  return 0;
}
} // namespace
int main(int argc, char **argv) {
  try {
    bool smoke = false, warp = false;
    std::filesystem::path output = "build/dx12/checks";
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--warp")
        warp = true;
      else if (argument == "--smoke") {
        smoke = true;
        if (i + 1 < argc && argv[i + 1][0] != '-')
          output = argv[++i];
      } else
        throw std::runtime_error(
            "Usage: Render-Engine-DX12 [--smoke [directory]] [--warp]");
    }
    Application app;
    app.Reset();
    SetProcessDPIAware();
    WNDCLASSW type{};
    type.lpfnWndProc = WindowProc;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"RenderEngineNativeDX12";
    type.hCursor = LoadCursor(nullptr, IDC_ARROW);
    Require(RegisterClassW(&type) != 0, "Cannot register DX12 window");
    HMENU menu = CreateMenu(), view = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 101, L"Play / pause [Space]");
    AppendMenuW(view, MF_STRING, 110, L"HDR [F7 cycles]");
    AppendMenuW(view, MF_STRING, 111, L"Pure Auto Exposure");
    AppendMenuW(view, MF_STRING, 112, L"Pure Manual Exposure");
    AppendMenuW(view, MF_STRING, 113, L"Toggle disk visibility assistance");
    AppendMenuW(view, MF_STRING, 103, L"Gargantua editor [F5]");
    AppendMenuW(view, MF_STRING, 102, L"Reset camera [Home]");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"View");
    RECT bounds{0, 0, 1280, 720};
    AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, TRUE);
    HWND window = CreateWindowW(
        type.lpszClassName, L"Render-Engine — DX12 migration preview",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, menu,
        type.hInstance, &app);
    Require(window != nullptr, "Cannot create DX12 window");
    app.renderer.Initialize(window, app.pendingWidth, app.pendingHeight, warp);
    Require(app.editor.Create(window, [&](double spin) { app.Spin(spin); }),
            "Cannot create object editor");
    app.editor.SetObject("Gargantua", true, .8, Astronomy::gargantuaHorizonAu);
    if (smoke) {
      const int result = Smoke(app, output);
      app.editor.Release();
      DestroyWindow(window);
      return result;
    }
    ShowWindow(window, SW_SHOW);
    auto previous = std::chrono::steady_clock::now();
    double titleTime = 0;
    bool running = true;
    while (running) {
      MSG message{};
      while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
          running = false;
          break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      if (!running)
        break;
      if (app.minimized || !app.pendingWidth || !app.pendingHeight) {
        WaitMessage();
        previous = std::chrono::steady_clock::now();
        continue;
      }
      if (app.pendingWidth != app.renderer.Width() ||
          app.pendingHeight != app.renderer.Height()) {
        app.renderer.Resize(app.pendingWidth, app.pendingHeight);
        app.dirty = app.resetExposure = true;
      }
      const auto now = std::chrono::steady_clock::now();
      const double dt = std::clamp(
          std::chrono::duration<double>(now - previous).count(), 0.0, .1);
      previous = now;
      if (app.playing) {
        app.seconds += dt * app.timeScale;
        app.dirty = true;
      }
      if (GetForegroundWindow() == window) {
        auto down = [](int key) {
          return (GetAsyncKeyState(key) & 0x8000) != 0;
        };
        const auto right = glm::normalize(glm::cross(app.forward, app.up));
        glm::dvec3 movement(0);
        if (down('W'))
          movement += app.forward;
        if (down('S'))
          movement -= app.forward;
        if (down('D'))
          movement += right;
        if (down('A'))
          movement -= right;
        if (down('R'))
          movement += app.up;
        if (down('F'))
          movement -= app.up;
        if (glm::length(movement) > 0) {
          app.camera += glm::normalize(movement) * app.speed * dt;
          const double minimum =
              KerrSpacetime::Horizon(Astronomy::gargantuaHorizonAu,
                                     app.frame.metric.y) *
              1.03;
          if (glm::length(app.camera) < minimum)
            app.camera = glm::normalize(app.camera) * minimum;
          app.dirty = true;
        }
        const double roll = (down('Q') ? 1.0 : 0.0) - (down('E') ? 1.0 : 0.0);
        if (roll != 0) {
          app.up =
              glm::normalize(glm::angleAxis(roll * dt, app.forward) * app.up);
          app.dirty = true;
        }
      }
      app.Fill(dt);
      app.renderer.Draw(app.frame, app.dirty, true);
      app.dirty = app.resetExposure = false;
      titleTime += dt;
      if (titleTime > .25) {
        std::wostringstream title;
        title << L"DX12 migration preview | "
              << (app.playing ? L"Playing" : L"Paused") << L" | "
              << (app.frame.optics.z == 0   ? L"HDR"
                  : app.frame.optics.z == 1 ? L"Pure Auto"
                                            : L"Pure Manual")
              << L" | time " << std::fixed << std::setprecision(1)
              << app.seconds / 3600 << L" h @ " << app.timeScale
              << L"x | flight " << app.speed << L" AU/s";
        SetWindowTextW(window, title.str().c_str());
        titleTime = 0;
      }
    }
    app.editor.Release();
    app.renderer.WaitIdle();
    return app.renderer.ErrorCount() ? 1 : 0;
  } catch (const std::exception &error) {
    std::cerr << "DX12: " << error.what() << "\n";
    return 1;
  }
}
