#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include "glad/glad.h"
#include "WindowManager.h"
#include "ObjectLoader.h"
#include "Astronomy.h"
#ifdef _WIN32
#include "UiTheme.h"
#endif
#ifdef _WIN32
#include <GLFW/glfw3native.h>
#endif

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <sstream>
using namespace std;

// Static definitions
bool WindowManager::isFocused = false;
double WindowManager::mouseX = 0.0;
double WindowManager::mouseY = 0.0;
glm::vec3 WindowManager::cameraPos   = glm::vec3(0.0f, 0.0f, 3.0f);
glm::vec3 WindowManager::cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 WindowManager::cameraUp    = glm::vec3(0.0f, 1.0f, 0.0f);
std::vector<float> WindowManager::currentModelVertices;

namespace {
    bool firstMouse = true;
    float yaw   = -90.0f;
    float pitch = 0.0f;
    double lastX = WIDTH / 2.0;
    double lastY = HEIGHT / 2.0;

    constexpr double timeRates[] = {1.0, 60.0, 3600.0, 86400.0, 2592000.0, 31557600.0};
    constexpr double quantumRates[] = {.1, .25, 1, 2, 4};
#ifdef _WIN32
    WNDPROC originalWindowProcedure = nullptr;

    bool IsButton(HWND control) {
        wchar_t name[16];
        return GetClassNameW(control, name, 16) && lstrcmpiW(name, L"Button") == 0;
    }

    // Panel controls take keyboard focus away from the GLFW window. Keys that the control does not need are sent
    // on to the GLFW window as the same key messages, so every scene shortcut works while the browser is focused.
    LRESULT CALLBACK InterfaceKeyProcedure(HWND control, UINT message, WPARAM key, LPARAM detail) {
        const auto previous = reinterpret_cast<WNDPROC>(GetPropW(control,L"RenderEnginePreviousControlProc"));
        const int id = GetDlgCtrlID(control);
        const HWND parent = GetParent(control);
        const bool launcher = id>=3000;
        const bool searching = id==2100;
        const bool listBox = id==2101 || id==2103;
        const bool infoBox = id==2102;
        const bool listNavigation = key==VK_UP || key==VK_DOWN || key==VK_PRIOR || key==VK_NEXT;
        const bool textNavigation = listNavigation || key==VK_LEFT || key==VK_RIGHT
            || key==VK_HOME || key==VK_END;

        if (message==WM_MOUSEMOVE || message==WM_MOUSELEAVE || message==WM_NCDESTROY)
            UiTheme::TrackHover(control, message);
        // After a mouse pick in a scene list, hand the keyboard back to the 3D view for WASD flight.
        if (message==WM_LBUTTONUP && listBox) {
            const LRESULT result = CallWindowProcW(previous,control,message,key,detail);
            SetFocus(parent);
            return result;
        }
        if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
            if (key == VK_TAB) {
                const HWND next = GetNextDlgTabItem(parent,control,GetKeyState(VK_SHIFT)<0);
                if (next) SetFocus(next);
                return 0;
            }
            if (searching) {
                if (key == VK_RETURN) {
                    SetFocus(GetDlgItem(parent,2101));
                    return 0;
                }
                if (key == VK_ESCAPE) {
                    SendMessageW(parent,message,key,detail);
                    SetFocus(GetDlgItem(parent,2101));
                    return 0;
                }
                if (key>=VK_F1 && key<=VK_F10)
                    SendMessageW(parent,message,key,detail);
                return CallWindowProcW(previous,control,message,key,detail);
            }
            if (launcher) {
                if (key == VK_RETURN && (id==3020 || IsButton(control))) {
                    SendMessageW(parent,WM_COMMAND,id==3020?3014:id,0);
                    return 0;
                }
                if ((key>=VK_F1 && key<=VK_F10) || key==VK_ESCAPE)
                    SendMessageW(parent,message,key,detail);
                return CallWindowProcW(previous,control,message,key,detail);
            }
            if (key == VK_RETURN && (listBox || id==2104)) {
                SendMessageW(parent,WM_COMMAND,2104,0);
                return 0;
            }
            if (key == VK_ESCAPE) {
                SetFocus(parent);
                SendMessageW(parent,message,key,detail);
                return 0;
            }
            // Arrow/Page keys still drive the lists and the information box, and Space presses a focused button.
            if ((listBox && listNavigation) || (infoBox && textNavigation) || (key == VK_SPACE && IsButton(control)))
                return CallWindowProcW(previous,control,message,key,detail);
            if (GetKeyState(VK_CONTROL)<0 || GetKeyState(VK_MENU)<0)
                return CallWindowProcW(previous,control,message,key,detail);
            SendMessageW(parent,message,key,detail);
            return 0;
        }
        if (message == WM_KEYUP || message == WM_SYSKEYUP) {
            // F10's release would otherwise start keyboard menu mode and stall the render loop.
            if (key != VK_F10)
                SendMessageW(parent,message,key,detail);
            return key == VK_F10 ? 0 : CallWindowProcW(previous,control,message,key,detail);
        }
        // Letters are global shortcuts here; the search field handles filtering, so suppress list type-ahead.
        // Control characters still reach the info box so Ctrl+C copies.
        if (message == WM_CHAR && (listBox || infoBox) && key >= 0x20)
            return 0;
        return CallWindowProcW(previous,control,message,key,detail);
    }
    bool BuildToastTexture(const std::string& text, int dpi, int& width, int& height,
                           std::vector<unsigned char>& pixels) {
        const int wideLength = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
        if (wideLength <= 1)
            return false;
        std::wstring wide(static_cast<size_t>(wideLength), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wide[0], wideLength);
        wide.resize(static_cast<size_t>(wideLength - 1));

        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        if (!dc)
            return false;

        const int fontSize = -MulDiv(30, dpi, 96);
        HFONT font = CreateFontW(fontSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(dc, font);

        SIZE textSize = {};
        GetTextExtentPoint32W(dc, wide.c_str(), static_cast<int>(wide.size()), &textSize);

        const int paddingX = MulDiv(28, dpi, 96);
        const int paddingY = MulDiv(12, dpi, 96);
        width = textSize.cx + paddingX * 2;
        height = textSize.cy + paddingY * 2;
        if (width <= 0 || height <= 0) {
            SelectObject(dc, oldFont);
            DeleteObject(font);
            DeleteDC(dc);
            return false;
        }

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap || !bits) {
            SelectObject(dc, oldFont);
            DeleteObject(font);
            DeleteDC(dc);
            return false;
        }
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        std::memset(bits, 0, static_cast<size_t>(width) * height * 4);

        // Dark rounded panel behind the text so it stays readable over the scene.
        const int radius = std::max(1, std::min(width, height) / 2);
        const int limitX = std::max(radius, width - 1 - radius);
        const int limitY = std::max(radius, height - 1 - radius);
        std::vector<unsigned char> backgroundAlpha(static_cast<size_t>(width) * height, 0);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int dx = x - std::clamp(x, radius, limitX);
                const int dy = y - std::clamp(y, radius, limitY);
                if (dx * dx + dy * dy <= radius * radius)
                    backgroundAlpha[static_cast<size_t>(y) * width + x] = 190;
            }
        }

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT rect = { 0, 0, width, height };
        DrawTextW(dc, wide.c_str(), static_cast<int>(wide.size()), &rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        const auto* source = static_cast<const unsigned char*>(bits);
        pixels.resize(static_cast<size_t>(width) * height * 4);
        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
            const unsigned char b = source[i * 4 + 0];
            const unsigned char g = source[i * 4 + 1];
            const unsigned char r = source[i * 4 + 2];
            const unsigned char luminance = std::max(std::max(r, g), b);
            if (luminance > 0) {
                pixels[i * 4 + 0] = 255;
                pixels[i * 4 + 1] = 255;
                pixels[i * 4 + 2] = 255;
                pixels[i * 4 + 3] = luminance;
            } else {
                pixels[i * 4 + 0] = 0;
                pixels[i * 4 + 1] = 0;
                pixels[i * 4 + 2] = 0;
                pixels[i * 4 + 3] = backgroundAlpha[i];
            }
        }

        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        SelectObject(dc, oldFont);
        DeleteObject(font);
        DeleteDC(dc);
        return true;
    }
#endif
}

void WindowManager::FramebufferSizeCallback(GLFWwindow* window, int, int) {
    if (auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(window)))
        manager->UpdateLensAspect();
}

float WindowManager::SceneAspect() const {
    int fullWidth, fullHeight, logicalWidth, logicalHeight;
    glfwGetFramebufferSize(window, &fullWidth, &fullHeight);
    glfwGetWindowSize(window, &logicalWidth, &logicalHeight);
    if (fullWidth <= 0 || fullHeight <= 0 || logicalWidth <= 0)
        return lensAspect;
    const int sceneWidth = fullWidth - scenePanel.Width() * fullWidth / logicalWidth;
    return static_cast<float>(glm::max(1, sceneWidth)) / fullHeight;
}

// Lens profiles define their field of view from the scene aspect; keep the user's zoom when it changes.
void WindowManager::UpdateLensAspect() {
    const float aspect = SceneAspect();
    if (visionProfile > 0 && aspect != lensAspect) {
        const float zoom = std::tan(glm::radians(fieldOfView * 0.5f))
            / std::tan(glm::radians(CameraOptics::FieldOfView(visionProfile, lensAspect) * 0.5f));
        fieldOfView = glm::degrees(2.0f * std::atan(zoom
            * std::tan(glm::radians(CameraOptics::FieldOfView(visionProfile, aspect) * 0.5f))));
    }
    lensAspect = aspect;
}

void WindowManager::ScrollCallback(GLFWwindow* window, double, double yOffset) {
    auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (yOffset == 0.0 || manager.mainMenu.Visible() || manager.quantumMode)
        return;
    const double factor = yOffset > 0.0 ? 10.0 : 0.1;
    if (manager.inStarSystem) {
        manager.starSystem.speedAu = glm::clamp(manager.starSystem.speedAu * factor, 1e-15, 1e6);
        manager.ShowSpeedToast(static_cast<float>(manager.starSystem.speedAu), true);
    } else {
        manager.cameraSpeed = glm::clamp(manager.cameraSpeed * static_cast<float>(factor), 0.001f, 100000.0f);
        manager.ShowSpeedToast(manager.cameraSpeed, false);
    }
    manager.UpdateTitle();
}

void WindowManager::KeyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS)
        return;
    auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (key == GLFW_KEY_F10 || key == 3030) { manager.OpenMainMenu(); return; }
    if (manager.mainMenu.Visible()) return;
    if (key == GLFW_KEY_F3) { manager.scenePanel.Toggle(); manager.ResizeInterface(); manager.UpdateTitle(); return; }
    if (key == GLFW_KEY_F2) { manager.trajectories=!manager.trajectories; manager.UpdateTitle(); return; }
    if (key == GLFW_KEY_F1) {
        showWireframe = !showWireframe;
        shaderProgram = showWireframe ? flatShader : phongShader;
        manager.UpdateTitle();
        return;
    }
    if (manager.quantumMode) {
        if (key==GLFW_KEY_SPACE) manager.simulationPlaying=!manager.simulationPlaying;
        else if (key==GLFW_KEY_R) manager.quantum.Select(manager.quantum.Selected());
        else if (key==GLFW_KEY_COMMA) manager.quantumRateIndex=glm::max(0,manager.quantumRateIndex-1);
        else if (key==GLFW_KEY_PERIOD) manager.quantumRateIndex=glm::min(4,manager.quantumRateIndex+1);
        manager.UpdateTitle();
        return;
    }
    if (key==GLFW_KEY_F4) {
        if (manager.inStarSystem && manager.pendingSystemTarget==manager.activeSystemTarget)
            manager.starSystem.Overview(cameraFront,cameraUp);
        else if (manager.pendingSystemTarget!=0) manager.EnterSelectedSystem(true);
        else if (manager.blackHoleReady) { manager.showBlackHole=true; manager.ResetView(); }
        manager.SyncCameraAngles(); manager.UpdateTitle(); return;
    }
    if (key==GLFW_KEY_G) {
        if (manager.pendingSystemTarget!=0) { manager.EnterSelectedSystem(false); return; }
        if (manager.inStarSystem) {
            manager.inStarSystem=false; manager.showBlackHole=true;
            manager.simulationTime=manager.localTime=0; manager.timeRateIndex=0;
            manager.blackHole.ResetSpacecraft(0); manager.ResetView();
        }
    }
    if (manager.inStarSystem) {
        bool handled = true;
        if (key==GLFW_KEY_L) manager.starSystem.ToggleLanding(manager.simulationTime,cameraFront,cameraUp);
        else if (key==GLFW_KEY_F) manager.starSystem.ToggleFollow(manager.simulationTime);
        else if (key==GLFW_KEY_HOME) manager.starSystem.Overview(cameraFront,cameraUp);
        else if (key==GLFW_KEY_LEFT_BRACKET) { manager.starSystem.speedAu=glm::max(1e-12,manager.starSystem.speedAu*.5); manager.ShowSpeedToast(static_cast<float>(manager.starSystem.speedAu),true); }
        else if (key==GLFW_KEY_RIGHT_BRACKET) { manager.starSystem.speedAu=glm::min(1e6,manager.starSystem.speedAu*2); manager.ShowSpeedToast(static_cast<float>(manager.starSystem.speedAu),true); }
        else if (key==GLFW_KEY_N) { /* Catalogued solar surfaces retain their seed. */ }
        else if (key==GLFW_KEY_H || key==GLFW_KEY_B || key==GLFW_KEY_R) return;
        else handled = false;
        if (handled) { manager.SyncCameraAngles(); manager.RefreshPanel(); return; }
    }
    if (key >= 1000 && key <= 1006) {
        manager.ApplyVisionProfile(key - 1000);
    } else if (key >= 1100 && key <= 1108) {
        const float exposureLevels[] = {-8, -4, -2, 0, 2, 4, 8, 12, 16};
        manager.exposureEv = exposureLevels[key - 1100];
    } else if (key == 1200 || key == 1201 || key == GLFW_KEY_F7) {
        manager.hdrMode = key == GLFW_KEY_F7 ? !manager.hdrMode : key == 1200;
    } else if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
        manager.exposureEv = glm::min(16.0f, manager.exposureEv + 1.0f);
    } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
        manager.exposureEv = glm::max(-12.0f, manager.exposureEv - 1.0f);
    } else if (key == GLFW_KEY_SPACE) {
        manager.simulationPlaying = !manager.simulationPlaying;
    } else if (key == 700 && manager.blackHoleReady) {
        manager.inStarSystem=false;
        manager.pendingSystemTarget=0;
        manager.simulationTime=manager.localTime=0; manager.timeRateIndex=0;
        manager.blackHole.ResetSpacecraft(0);
        manager.showBlackHole = !manager.showBlackHole;
        manager.ResetView();
    } else if (key == 701 && manager.blackHoleReady) {
        if (manager.inStarSystem) { manager.simulationTime=manager.localTime=0; manager.timeRateIndex=0; manager.blackHole.ResetSpacecraft(0); }
        manager.inStarSystem=false;
        manager.pendingSystemTarget=0;
        manager.ClearObserverModes();
        manager.showBlackHole = true;
        manager.followPlanet = true;
        manager.lastPlanetPosition = BlackHole::PlanetPosition(manager.simulationTime);
        const auto sun = glm::normalize(-manager.lastPlanetPosition);
        const auto side = glm::normalize(glm::cross(sun, glm::vec3(0, 1, 0)));
        const auto approach = glm::normalize(0.35f * sun + 0.94f * side);
        cameraPos = manager.lastPlanetPosition + 0.32f * approach;
        cameraFront = -approach;
        manager.SyncCameraAngles();
        manager.cameraSpeed = 0.05f;
        manager.ShowSpeedToast(manager.cameraSpeed, false);
        manager.ApplyVisionProfile(manager.visionProfile);
    } else if (key == 702 && manager.blackHoleReady) {
        if (manager.inStarSystem) { manager.simulationTime=manager.localTime=0; manager.timeRateIndex=0; manager.blackHole.ResetSpacecraft(0); }
        manager.inStarSystem=false;
        manager.pendingSystemTarget=0;
        manager.ClearObserverModes();
        manager.blackHole.Update(manager.simulationTime);
        manager.showBlackHole = true;
        manager.followSpacecraft = true;
        manager.lastSpacecraftPosition = manager.blackHole.SpacecraftPosition();
        cameraPos = manager.lastSpacecraftPosition
            + glm::normalize(manager.lastSpacecraftPosition) * 0.3f + glm::vec3(0.0f, 0.03f, 0.0f);
        cameraFront = glm::normalize(manager.lastSpacecraftPosition - cameraPos);
        manager.SyncCameraAngles();
        manager.cameraSpeed = 0.05f;
        manager.ShowSpeedToast(manager.cameraSpeed, false);
        manager.ApplyVisionProfile(manager.visionProfile);
    } else if (key == GLFW_KEY_B && manager.blackHoleReady) {
        manager.blackHole.Update(manager.simulationTime);
        manager.blackHole.BoostSpacecraft();
    } else if (key == GLFW_KEY_R && manager.blackHoleReady) {
        manager.blackHole.ResetSpacecraft(manager.simulationTime);
    } else if (key == GLFW_KEY_N && manager.blackHoleReady) {
        manager.blackHole.RegenerateTerrain();
    } else if (key == GLFW_KEY_L && manager.blackHoleReady) {
        manager.ToggleLanding();
    } else if (key == GLFW_KEY_G && manager.blackHoleReady && manager.selectedTarget >= 0) {
        const auto target = manager.blackHole.ResolveTarget(manager.selectedTarget, manager.simulationTime);
        manager.ResetView();
        manager.showBlackHole = true;
        auto offset = cameraPos - target.position;
        if (glm::length(offset) < 0.001f)
            offset = glm::vec3(0, 0, 1);
        cameraPos = target.position + glm::normalize(offset) * glm::max(0.35f, target.radius * 6.0f);
        cameraFront = glm::normalize(target.position - cameraPos);
        manager.SyncCameraAngles();
        manager.cameraSpeed = 0.05f;
        manager.ShowSpeedToast(manager.cameraSpeed, false);
    } else if (key == GLFW_KEY_H && manager.blackHoleReady) {
        const bool falling = !manager.observerGravity;
        manager.ClearObserverModes();
        manager.showBlackHole = true;
        manager.observerGravity = falling;
        manager.observer = {glm::dvec3(cameraPos), glm::dvec3(0.0)};
    } else if (key == GLFW_KEY_F6 && manager.blackHoleReady) {
        manager.showBlackHole = !manager.inStarSystem;
        manager.photonView = !manager.photonView;
        if (manager.inStarSystem && manager.photonView) manager.starSystem.SetPhotonDetector();
        manager.blackHole.SetPhotonView(manager.photonView, cameraPos);
    } else if (key == GLFW_KEY_F && manager.blackHoleReady && manager.selectedTarget >= 0) {
        manager.followSelected = !manager.followSelected || manager.followingTarget != manager.selectedTarget;
        manager.followingTarget = manager.selectedTarget;
        manager.lastSelectedPosition = manager.blackHole.ResolveTarget(manager.followingTarget, manager.simulationTime).position;
        manager.followPlanet = false;
        manager.followSpacecraft = false;
        manager.landed = false;
        manager.observerGravity = false;
    } else if (key == GLFW_KEY_HOME) {
        manager.ResetView();
    } else if (key == GLFW_KEY_LEFT_BRACKET) {
        manager.cameraSpeed = glm::max(0.005f, manager.cameraSpeed * 0.5f);
        manager.ShowSpeedToast(manager.cameraSpeed, false);
    } else if (key == GLFW_KEY_RIGHT_BRACKET) {
        manager.cameraSpeed = glm::min(50.0f, manager.cameraSpeed * 2.0f);
        manager.ShowSpeedToast(manager.cameraSpeed, false);
    } else if (key == GLFW_KEY_COMMA) {
        manager.timeRateIndex = glm::max(0, manager.timeRateIndex - 1);
    } else if (key == GLFW_KEY_PERIOD) {
        manager.timeRateIndex = glm::min(manager.inStarSystem?5:2, manager.timeRateIndex + 1);
    } else if (key == GLFW_KEY_ESCAPE) {
        if (manager.zoomDragging) {
#ifdef _WIN32
            ReleaseCapture();
#endif
        }
        manager.zoomDragging = false;
        manager.SetMouseCaptured(false);
    }
    manager.UpdateTitle();
}

void WindowManager::ClearObserverModes() {
    followSelected = followPlanet = followSpacecraft = false;
    landed = observerGravity = horizonReached = false;
    cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
}

void WindowManager::ResetView() {
    ClearObserverModes();
    cameraPos = glm::vec3(0.0f, 0.0f, showBlackHole ? 6.0f : 3.0f);
    cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    yaw = -90.0f;
    pitch = 0.0f;
    ApplyVisionProfile(visionProfile);
    cameraSpeed = 1.0f;
    ShowSpeedToast(cameraSpeed, false);
    firstMouse = true;
}

void WindowManager::ApplyVisionProfile(int profile) {
    visionProfile = profile;
    lensAspect = SceneAspect();
    fieldOfView = CameraOptics::FieldOfView(profile, lensAspect);
}

void WindowManager::ToggleLanding() {
    followSelected = false;
    landed = !landed;
    observerGravity = false;
    horizonReached = false;
    showBlackHole = true;
    followPlanet = false;
    followSpacecraft = false;
    if (landed) {
        landedPlanet = BlackHole::NearestPlanet(cameraPos, simulationTime);
        lastPlanetPosition = BlackHole::PlanetPosition(simulationTime, landedPlanet);
        cameraUp = glm::normalize(cameraPos - lastPlanetPosition);
        cameraPos = lastPlanetPosition + cameraUp
            * (blackHole.PlanetSurfaceRadius(cameraUp, simulationTime, landedPlanet) + 0.001f);
        auto tangent = cameraFront - cameraUp * glm::dot(cameraFront, cameraUp);
        if (glm::length(tangent) < 0.01f) {
            const auto sun = glm::normalize(-lastPlanetPosition);
            tangent = sun - cameraUp * glm::dot(sun, cameraUp);
        }
        if (glm::length(tangent) < 0.01f) {
            const auto axis = std::abs(cameraUp.y) < 0.9f
                ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            tangent = glm::cross(cameraUp, axis);
        }
        cameraFront = glm::normalize(tangent);
        cameraSpeed = 0.005f;
        ShowSpeedToast(cameraSpeed, false);
    } else {
        cameraPos += cameraUp * 0.004f;
        cameraUp = glm::vec3(0, 1, 0);
    }
    yaw = glm::degrees(std::atan2(cameraFront.z, cameraFront.x));
    pitch = glm::degrees(std::asin(glm::clamp(cameraFront.y, -1.0f, 1.0f)));
    firstMouse = true;
}

void WindowManager::UpdateObserver(double previousTime, float realSeconds) {
    if (landed) {
        const auto center = BlackHole::PlanetPosition(simulationTime, landedPlanet);
        const auto rotation = BlackHole::PlanetRotation(simulationTime)
            * glm::transpose(BlackHole::PlanetRotation(previousTime));
        cameraPos = center + glm::vec3(rotation * glm::vec4(cameraPos - lastPlanetPosition, 0.0f));
        cameraFront = glm::normalize(glm::vec3(rotation * glm::vec4(cameraFront, 0.0f)));
        cameraUp = glm::normalize(glm::vec3(rotation * glm::vec4(cameraUp, 0.0f)));
        lastPlanetPosition = center;
    }
    if (!observerGravity)
        return;
    observer.position = glm::dvec3(cameraPos);
    // The fall is driven by the observer's proper time, not the playback clock,
    // so pressing H always plunges the camera even while the simulation is paused.
    double remainingProper = glm::max(0.0, static_cast<double>(realSeconds));
    const double horizon = BlackHole::HorizonRadius();
    const double boundary = horizon * 1.015;
    const double innerLimit = horizon * 0.08;
    while (remainingProper > 0.0 && observerGravity) {
        const double radius = glm::length(observer.position);
        if (radius <= innerLimit) {
            observer.position = glm::normalize(observer.position) * innerLimit;
            observerGravity = false;
            horizonReached = true;
            UpdateTitle();
            break;
        }
        if (radius <= boundary)
            horizonReached = true;
        const double properStep = glm::min(remainingProper, 0.02);
        double interval = 0.002;
        if (radius > horizon) {
            const double clockRate = glm::max(OrbitalMechanics::ObserverClockRate(radius,
                glm::length(observer.velocity), BlackHole::GravityParameter(), horizon), 0.02);
            interval = glm::clamp(properStep / clockRate, 0.0001, 0.08);
        } else {
            // Do not let one step jump across the singularity and out the far side.
            const double speed = glm::max(glm::length(observer.velocity), 0.001);
            interval = glm::min(0.002, 0.3 * radius / speed);
        }
        OrbitalMechanics::AdvanceParticle(observer, BlackHole::GravityParameter(), 0.0, interval);
        localTime += radius > horizon ? properStep : interval;
        remainingProper -= properStep;
        if (glm::length(observer.position) <= innerLimit) {
            observer.position = glm::normalize(observer.position) * innerLimit;
            observerGravity = false;
            horizonReached = true;
            UpdateTitle();
            break;
        }
    }
    cameraPos = glm::vec3(observer.position);
}

void WindowManager::SyncCameraAngles() {
    yaw=glm::degrees(std::atan2(cameraFront.z,cameraFront.x));
    pitch=glm::degrees(std::asin(glm::clamp(cameraFront.y,-1.0f,1.0f)));
    firstMouse=true;
}

void WindowManager::ResizeInterface() {
    if (!window) return;
    int width,height;
    glfwGetWindowSize(window,&width,&height);
    mainMenu.Resize(width,height);
    scenePanel.Resize(width,height);
    UpdateLensAspect();
}

void WindowManager::FocusScene() {
#ifdef _WIN32
    SetFocus(glfwGetWin32Window(window));
#endif
}

void WindowManager::OpenMainMenu() {
    simulationPlaying=false;
    SetMouseCaptured(false);
    scenePanel.SetVisible(false);
#ifdef _WIN32
    SetMenu(glfwGetWin32Window(window),nullptr);
    DrawMenuBar(glfwGetWin32Window(window));
#endif
    mainMenu.Show(AppMenu::Page::Home);
    lastTitle="Render-Engine | Main menu";
    glfwSetWindowTitle(window,lastTitle.c_str());
}

// Shared entry into any scene from the main menu or a dropped model.
void WindowManager::ShowScene() {
    ClearObserverModes();
    photonView=false;
    blackHole.SetPhotonView(false,cameraPos);
    simulationPlaying=false;
    SetMouseCaptured(false);
    mainMenu.Hide();
    scenePanel.ShowBodies(true);
    scenePanel.SetVisible(true);
#ifdef _WIN32
    SetMenu(glfwGetWin32Window(window),static_cast<HMENU>(nativeMenu));
    DrawMenuBar(glfwGetWin32Window(window));
#endif
}

namespace {
    void ReportUnavailable(const wchar_t* scene) {
#ifdef _WIN32
        std::wstring text = std::wstring(scene) + L" could not start because its OpenGL resources failed to load. "
            L"The console window lists the shader or texture error.";
        MessageBoxW(nullptr, text.c_str(), L"Render-Engine", MB_OK | MB_ICONWARNING);
#endif
    }
}

const std::vector<std::pair<int,std::string>>& WindowManager::StarCatalogue() {
    if (starCatalogue.empty()) {
        starCatalogue={{-2,"Sun / Solar System"},{0,"Gargantua / Black hole"}};
        for (int id=4;id<7010;++id)
            if (id!=9) starCatalogue.push_back({id,blackHole.ResolveTarget(id,0).name});
    }
    return starCatalogue;
}

void WindowManager::StartSolarSystem() {
    if (!starSystemReady) { ReportUnavailable(L"The Solar System"); return; }
    ShowScene();
    inStarSystem=true; quantumMode=false; showBlackHole=false;
    simulationTime=localTime=0; timeRateIndex=0;
    pendingSystemTarget=activeSystemTarget=-2;
    selectedTarget=-1;
    starSystem.LoadSolarSystem(); previewSystem.LoadSolarSystem();
    scenePanel.SetTitle("Star / object browser");
    scenePanel.SetCatalogue(StarCatalogue());
    std::vector<std::string> bodies={"Sun"};
    for (const auto& body:starSystem.Planets()) bodies.push_back(body.name);
    scenePanel.SetPlanets(bodies);
    starSystem.Overview(cameraFront,cameraUp);
    fieldOfView=45; SyncCameraAngles(); ResizeInterface(); RefreshPanel(); UpdateTitle();
    FocusScene();
}

void WindowManager::StartGargantua() {
    if (!blackHoleReady) { ReportUnavailable(L"The Gargantua system"); return; }
    ShowScene();
    inStarSystem=quantumMode=false; showBlackHole=true; pendingSystemTarget=0;
    timeRateIndex=0; simulationTime=localTime=0;
    blackHole.ResetSpacecraft(0);
    scenePanel.SetTitle("Star / object browser");
    scenePanel.SetCatalogue(StarCatalogue());
    SelectCatalogue(0);
    ResetView(); ResizeInterface(); UpdateTitle();
    FocusScene();
}

void WindowManager::StartQuantum(int index) {
    if (!quantumReady) { ReportUnavailable(L"The quantum simulation"); return; }
    ShowScene();
    inStarSystem=false; showBlackHole=false; quantumMode=true;
    quantum.Select(index);
    scenePanel.ShowBodies(false);
    scenePanel.SetTitle("Quantum state browser");
    std::vector<std::pair<int,std::string>> catalogue;
    const auto& examples=QuantumStates::Examples();
    for (int i=0;i<static_cast<int>(examples.size());++i) catalogue.push_back({i,examples[i].name});
    scenePanel.SetCatalogue(std::move(catalogue)); scenePanel.SetPlanets({});
    ResizeInterface(); RefreshPanel(); UpdateTitle();
    FocusScene();
}
void WindowManager::SelectCatalogue(int id) {
    if (quantumMode) { quantum.Select(id); simulationPlaying=false; RefreshPanel(); return; }
    pendingSystemTarget=id;
    if (id==0) {
        scenePanel.SetPlanets({"Gargantua","Miller's planet","Ocean planet 2","Ocean planet 3","Orbiting star 1","Orbiting star 2","Orbiting star 3","Orbiting star 4","Orbiting star 5","Spacecraft"});
        selectedTarget=0;
    } else {
        if (id==-2) previewSystem.LoadSolarSystem();
        else previewSystem.Generate(id,blackHole.ResolveTarget(id,simulationTime).name);
        std::vector<std::string> names={previewSystem.Host().name};
        for (const auto& planet:previewSystem.Planets()) names.push_back(planet.name);
        scenePanel.SetPlanets(names);
        selectedTarget=id>=0?id:-1;
        if (showBlackHole && id>=0) {
            cameraFront=glm::normalize(blackHole.ResolveTarget(id,simulationTime).position-cameraPos);
            SyncCameraAngles();
        }
    }
    RefreshPanel();
}

void WindowManager::EnterSelectedSystem(bool overview) {
    if (!starSystemReady || pendingSystemTarget==0) return;
    if (!inStarSystem || activeSystemTarget!=pendingSystemTarget) {
        if (pendingSystemTarget==-2) starSystem.LoadSolarSystem();
        else starSystem.Generate(pendingSystemTarget,previewSystem.Host().name);
        starSystem.SelectPlanet(previewSystem.Selected());
        activeSystemTarget=pendingSystemTarget;
        inStarSystem=true; showBlackHole=false; observerGravity=landed=false;
    }
    if (overview) starSystem.Overview(cameraFront,cameraUp);
    else starSystem.GoToSelected(simulationTime,cameraFront,cameraUp);
    SyncCameraAngles(); RefreshPanel(); UpdateTitle();
}

namespace {
    const char* const gargantuaBodies[] = {"Gargantua", "Miller's planet", "Ocean planet 2", "Ocean planet 3",
        "Orbiting star 1", "Orbiting star 2", "Orbiting star 3", "Orbiting star 4", "Orbiting star 5", "Spacecraft"};

    std::string TargetName(const BlackHole& blackHole, int id) {
        return id >= 0 && id < 10 ? gargantuaBodies[id] : blackHole.ResolveTarget(id, 0).name;
    }

    std::string Clock(double seconds) {
        const auto whole = static_cast<std::uint64_t>(glm::max(0.0, seconds));
        std::ostringstream text;
        if (whole >= 86400) text << whole / 86400 << "d ";
        text << std::setfill('0') << std::setw(2) << whole / 3600 % 24 << ':' << std::setw(2) << whole / 60 % 60
             << ':' << std::setw(2) << whole % 60;
        return text.str();
    }

    std::string Rate(double rate) {
        std::ostringstream text;
        if (rate >= 86400) text << rate / 86400 << " days/s";
        else if (rate >= 3600) text << rate / 3600 << " h/s";
        else if (rate >= 60) text << rate / 60 << " min/s";
        else text << rate << "x";
        return text.str();
    }
}

void WindowManager::RefreshPanel() {
    if (mainMenu.Visible()) return;
    if (quantumMode) scenePanel.SetInformation(quantum.Information());
    else if (pendingSystemTarget!=0) {
        const auto& system=inStarSystem && activeSystemTarget==pendingSystemTarget?starSystem:previewSystem;
        scenePanel.HighlightPlanet(system.Selected()+1);
        auto info=system.Information(simulationTime);
        if (system.IsSolarSystem()) info+="\n1:1 physical radii and distances\nJ2000 frozen Keplerian elements\n4096 representative belt/KBO markers\nPlanet surfaces are procedural illustrations.\n";
        info+=inStarSystem && activeSystemTarget==pendingSystemTarget
            ? "\nControls\nWASD QE  fly\nWheel or [ ]  flight speed\nRight mouse  look around\nShift + right drag  zoom,  middle click  reset zoom\nLeft click  select a body\nG  go to selection\nF  follow selection\nL  land / lift off\nF4 or Home  system overview\n"
            : "\nG or double-click enters this system.\n";
        scenePanel.SetInformation(info);
    } else {
        scenePanel.HighlightPlanet(selectedTarget>=0 && selectedTarget<10 ? selectedTarget : -1);
        scenePanel.SetInformation(
            "GARGANTUA (inspired by Interstellar)\n"
            "Supermassive black hole, Schwarzschild model\n"
            "Mass and age are illustrative.\n"
            "Spin and Kerr frame dragging are not modeled.\n\n"
            "Scaled demonstration system with ocean-only planets and a force-integrated spacecraft.\n\n"
            "Controls\n"
            "Space  play / pause,  < >  time rate\n"
            "WASD QE  fly,  Shift  boost\n"
            "Wheel or [ ]  flight speed\n"
            "Right mouse  look around\n"
            "Shift + right drag  zoom,  middle click  reset\n"
            "Left click  select,  G  go,  F  follow\n"
            "L  land / lift off,  H  free fall\n"
            "B  boost craft,  R  reset craft\n"
            "F2  orbits,  F3  panel,  F4  overview\n"
            "F6  photon view,  F10  main menu\n");
    }
}

void WindowManager::HandleInterfaceCommand(int id,int notification) {
#ifdef _WIN32
    if (id==3010) mainMenu.Show(AppMenu::Page::Planetorium);
    else if (id==3011) {
        mainMenu.Show(AppMenu::Page::Quantum);
        mainMenu.SetQuantumInformation(QuantumStates::Examples()[std::max(0,mainMenu.SelectedQuantum())].explanation);
    } else if (id==3015) mainMenu.Show(AppMenu::Page::Home);
    else if (id==3012) StartSolarSystem();
    else if (id==3013) StartGargantua();
    else if (id==3014) StartQuantum(std::max(0,mainMenu.SelectedQuantum()));
    else if (id==3020 && notification==LBN_SELCHANGE)
        mainMenu.SetQuantumInformation(QuantumStates::Examples()[std::max(0,mainMenu.SelectedQuantum())].explanation);
    else if (id==3020 && notification==LBN_DBLCLK) StartQuantum(std::max(0,mainMenu.SelectedQuantum()));
    else if (id==2100 && notification==EN_CHANGE) scenePanel.Filter();
    else if (id==2101 && (notification==LBN_SELCHANGE || notification==LBN_DBLCLK)) {
        const int choice=scenePanel.SelectedStar();
        if (choice!=-1) SelectCatalogue(choice);
        if (notification==LBN_DBLCLK && !quantumMode) HandleInterfaceCommand(2104,0);
        UpdateTitle();
    } else if (id==2103 && (notification==LBN_SELCHANGE || notification==LBN_DBLCLK)) {
        const int body=scenePanel.SelectedPlanet();
        if (body>=0) {
            if (pendingSystemTarget==0) selectedTarget=body;
            else {
                previewSystem.SelectPlanet(body-1);
                if (inStarSystem && activeSystemTarget==pendingSystemTarget) starSystem.SelectPlanet(body-1);
            }
            RefreshPanel();
            UpdateTitle();
        }
        if (notification==LBN_DBLCLK) HandleInterfaceCommand(2104,0);
    } else if (id==2104) {
        KeyCallback(window,GLFW_KEY_G,0,GLFW_PRESS,0);
        FocusScene();
    } else if (id<2100) KeyCallback(window,id,0,GLFW_PRESS,0);
#endif
}

void WindowManager::UpdateTitle() {
    if (mainMenu.Visible()) return;
    SyncMenu();
    UpdateStatus();
    std::ostringstream title;
    title << "Render-Engine | ";
    if (quantumMode) title << "Quantum | " << QuantumStates::Examples()[quantum.Selected()].name;
    else title << (inStarSystem ? starSystem.Host().name : showBlackHole ? "Gargantua" : "Model viewer");
    title << (simulationPlaying ? " | Playing " : " | Paused ")
          << (quantumMode ? Rate(quantumRates[quantumRateIndex]) : Rate(timeRates[timeRateIndex]));
    if (title.str() != lastTitle) {
        lastTitle = title.str();
        glfwSetWindowTitle(window, lastTitle.c_str());
    }
}

void WindowManager::UpdateStatus() {
    std::ostringstream status;
    status << std::fixed;
    if (quantumMode) {
        status << (simulationPlaying ? "Playing" : "Paused") << " at " << Rate(quantumRates[quantumRateIndex])
               << "\nt = " << std::setprecision(3) << quantum.Time()
               << "\nSpace play / pause,  R restart,  < > rate";
        scenePanel.SetStatus(status.str());
        return;
    }
    status << (simulationPlaying ? "Playing" : "Paused") << " at " << Rate(timeRates[timeRateIndex])
           << "\nWorld " << Clock(simulationTime);
    if (showBlackHole) status << "    Local " << Clock(localTime);
    const double speed = inStarSystem ? starSystem.speedAu : cameraSpeed;
    status << "\nFlight speed " << std::setprecision(speed < 0.01 ? 6 : speed < 10 ? 3 : 1) << speed
           << (inStarSystem ? " AU/s" : " units/s");
    const char* mode = followPlanet ? "Following Miller's planet" : followSpacecraft ? "Following the spacecraft"
        : followSelected ? "Following the selection" : landed ? "Landed, surface free-cam"
        : observerGravity ? "Falling toward the black hole" : horizonReached ? (glm::length(cameraPos) < BlackHole::HorizonRadius() ? "Inside the event horizon" : "Horizon crossed")
        : nullptr;
    if (mode) status << '\n' << mode;
    if (showBlackHole) {
        status << "\nCraft: " << blackHole.SpacecraftStatus() << "\nCraft light age " << std::setprecision(2)
               << blackHole.SpacecraftLightAge(cameraPos, simulationTime) << " s";
        if (selectedTarget >= 0) status << "\nSelected " << TargetName(blackHole, selectedTarget);
    }
    if (photonView) status << "\nPhoton view on: fly sideways to inspect";
    if (visionProfile > 0 || exposureEv != 0.0f || !hdrMode)
        status << '\n' << CameraOptics::Name(visionProfile) << (hdrMode ? ", HDR" : ", pure exposure")
               << ", EV " << std::showpos << std::setprecision(0) << exposureEv << std::noshowpos;
    scenePanel.SetStatus(status.str());
}

void WindowManager::SyncMenu() {
#ifdef _WIN32
    const auto menu = static_cast<HMENU>(nativeMenu);
    if (!menu) return;
    const auto check = [&](UINT id, bool on) { CheckMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
    const auto enable = [&](UINT id, bool on) { EnableMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED)); };
    const bool gargantua = !inStarSystem && !quantumMode && blackHoleReady;
    const bool space = !quantumMode;
    check(GLFW_KEY_SPACE, simulationPlaying);
    check(GLFW_KEY_F2, trajectories);
    check(GLFW_KEY_F3, scenePanel.Visible());
    check(GLFW_KEY_F6, photonView);
    check(GLFW_KEY_F1, showWireframe);
    check(GLFW_KEY_F, followSelected || (inStarSystem && starSystem.IsFollowing()));
    check(GLFW_KEY_L, landed || (inStarSystem && starSystem.IsLanded()));
    check(GLFW_KEY_H, observerGravity);
    check(700, showBlackHole);
    check(701, followPlanet);
    check(702, followSpacecraft);
    for (UINT id : {700u, 701u, 702u, static_cast<UINT>(GLFW_KEY_H), static_cast<UINT>(GLFW_KEY_B), static_cast<UINT>(GLFW_KEY_R)})
        enable(id, gargantua);
    for (UINT id : {static_cast<UINT>(GLFW_KEY_F4), static_cast<UINT>(GLFW_KEY_HOME), static_cast<UINT>(GLFW_KEY_LEFT_BRACKET),
                    static_cast<UINT>(GLFW_KEY_RIGHT_BRACKET), static_cast<UINT>(GLFW_KEY_G), static_cast<UINT>(GLFW_KEY_F),
                    static_cast<UINT>(GLFW_KEY_L), static_cast<UINT>(GLFW_KEY_F2), static_cast<UINT>(GLFW_KEY_F6)})
        enable(id, space);
    enable(GLFW_KEY_F1, !inStarSystem && !quantumMode && !showBlackHole);
    CheckMenuRadioItem(menu, 1000, 1006, 1000 + visionProfile, MF_BYCOMMAND);
    CheckMenuRadioItem(menu, 1200, 1201, hdrMode ? 1200 : 1201, MF_BYCOMMAND);
    const float exposureLevels[] = {-8, -4, -2, 0, 2, 4, 8, 12, 16};
    for (int level = 0; level < 9; ++level) check(1100 + level, exposureLevels[level] == exposureEv);
#endif
}
void WindowManager::AlignCameraUp(const glm::vec3& up) {
    const auto target = glm::normalize(up);
    const auto axis = glm::cross(cameraUp, target);
    const float sine = glm::length(axis);
    const float cosine = glm::clamp(glm::dot(cameraUp, target), -1.0f, 1.0f);
    if (sine > 0.000001f) {
        cameraFront = glm::normalize(glm::vec3(glm::rotate(glm::mat4(1),
            std::atan2(sine, cosine), axis / sine) * glm::vec4(cameraFront, 0)));
    } else if (cosine < 0.0f) {
        const auto right = glm::normalize(glm::cross(cameraFront, cameraUp));
        cameraFront = glm::vec3(glm::rotate(glm::mat4(1), glm::pi<float>(), right) * glm::vec4(cameraFront, 0));
    }
    cameraUp = target;
}

void WindowManager::SetMouseCaptured(bool captured) {
    mouseCaptured = captured;
    firstMouse = true;
    glfwSetInputMode(window, GLFW_CURSOR, captured && isFocused ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, captured && isFocused ? GLFW_TRUE : GLFW_FALSE);
}

void WindowManager::MouseButtonCallback(GLFWwindow* window, int button, int action, int) {
    auto& manager=*static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (manager.mainMenu.Visible()) return;
    // A click on the scene takes keyboard focus back from the side panel so WASD and shortcuts reach GLFW.
    if (action == GLFW_PRESS) manager.FocusScene();
    if (manager.quantumMode) return;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            const bool shift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                || glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            if (shift) {
                manager.zoomDragging = true;
                firstMouse = true;
#ifdef _WIN32
                SetCapture(glfwGetWin32Window(window));
#endif
            } else {
                manager.SetMouseCaptured(!manager.mouseCaptured);
            }
        } else if (action == GLFW_RELEASE) {
            if (manager.zoomDragging) {
#ifdef _WIN32
                ReleaseCapture();
#endif
                manager.zoomDragging = false;
            }
        }
    }
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        manager.fieldOfView = CameraOptics::FieldOfView(manager.visionProfile, manager.lensAspect);
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if ((!manager.showBlackHole && !manager.inStarSystem) || manager.mouseCaptured)
            return;
        double x, y;
        int width, height;
        glfwGetCursorPos(window, &x, &y);
        glfwGetWindowSize(window, &width, &height);
        width-=manager.scenePanel.Width();
        if (width <= 0 || height <= 0)
            return;
        if (x>=width) return;
        const auto projection = glm::perspective(glm::radians(manager.fieldOfView),
            static_cast<float>(width) / height, 0.01f, 2000.0f);
        const auto origin=manager.inStarSystem?glm::vec3(0):cameraPos;
        const auto view = glm::lookAt(origin, origin + cameraFront, cameraUp);
        const auto screen = CameraOptics::MapScreen(manager.visionProfile,
            glm::vec2(2.0f * static_cast<float>(x) / width - 1.0f, 1.0f - 2.0f * static_cast<float>(y) / height),
            static_cast<float>(width) / height, manager.fieldOfView);
        const auto point = glm::inverse(projection * view) * glm::vec4(screen, 1.0f, 1.0f);
        const auto direction = glm::normalize(glm::vec3(point) / point.w - origin);
        if (manager.inStarSystem) manager.starSystem.Pick(direction,manager.simulationTime);
        else {
            manager.selectedTarget = manager.blackHole.PickTarget(cameraPos, direction, manager.simulationTime);
            if (manager.selectedTarget>=10 || (manager.selectedTarget>=4 && manager.selectedTarget<=8))
                manager.SelectCatalogue(manager.selectedTarget);
            else manager.pendingSystemTarget=0;
        }
        manager.RefreshPanel();
        manager.UpdateTitle();
    }
}

void WindowManager::MouseCallback(GLFWwindow* window, double xpos, double ypos) {
    auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!isFocused)
        return;
    if (manager.zoomDragging) {
        if (firstMouse) {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
            return;
        }
        const float delta = static_cast<float>(lastY - ypos);
        lastX = xpos;
        lastY = ypos;
        manager.fieldOfView = glm::clamp(manager.fieldOfView - delta * 0.1f,
            manager.visionProfile > 0 ? 2.0f : 15.0f, 90.0f);
        return;
    }
    if (!manager.mouseCaptured)
        return;
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = static_cast<float>(xpos - lastX);
    float yoffset = static_cast<float>(lastY - ypos);
    lastX = xpos;
    lastY = ypos;

    const float sensitivity = 0.1f * std::tan(glm::radians(manager.fieldOfView * 0.5f))
        / std::tan(glm::radians(22.5f));
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    if (manager.landed || (manager.inStarSystem && manager.starSystem.IsLanded())) {
        cameraFront = glm::vec3(glm::rotate(glm::mat4(1), glm::radians(-xoffset), cameraUp)
            * glm::vec4(cameraFront, 0));
        const auto right = glm::normalize(glm::cross(cameraFront, cameraUp));
        const auto candidate = glm::vec3(glm::rotate(glm::mat4(1), glm::radians(yoffset), right)
            * glm::vec4(cameraFront, 0));
        if (std::abs(glm::dot(candidate, cameraUp)) < 0.995f)
            cameraFront = glm::normalize(candidate);
        return;
    }
    yaw = std::remainder(yaw + xoffset, 360.0f);
    pitch += yoffset;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    WindowManager::cameraFront = glm::normalize(front);
}

void WindowManager::WindowFocusCallback(GLFWwindow* window, int focused) {
    isFocused = focused;
    auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (manager.zoomDragging) {
#ifdef _WIN32
        ReleaseCapture();
#endif
    }
    manager.zoomDragging = false;
    manager.SetMouseCaptured(false);
}
void WindowManager::DropCallback(GLFWwindow* window, int count, const char** paths)
{
    auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (paths && count > 0)
        manager.LoadModel(paths[count - 1]);
}

void WindowManager::LoadModel(const std::string& path)
{
    const auto dot = path.find_last_of("./\\");
    std::string extension = dot != std::string::npos && path[dot] == '.' ? path.substr(dot) : "";
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    glm::vec3 center;
    std::vector<float> modelVertices;
    bool loaded = false;
    if (extension == ".obj") loaded = ObjectLoader::LoadOBJ(path, modelVertices, center);
    else if (extension == ".fbx") loaded = ObjectLoader::LoadFBX(path, modelVertices, center);
    else {
        std::cerr << "[Drop] Unsupported file format: " << path << std::endl;
        return;
    }
    if (!loaded || modelVertices.size() < 18) {
        std::cerr << "[Drop] Could not load a triangle mesh from " << path << std::endl;
        return;
    }
    currentModelVertices = std::move(modelVertices);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, currentModelVertices.size() * sizeof(float), currentModelVertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    std::cout << "[Drop] Loaded " << currentModelVertices.size() / 6 << " vertices from " << path << std::endl;

    showingDefaultCube = false;
    ShowScene();
    scenePanel.SetVisible(false);
    inStarSystem = quantumMode = showBlackHole = false;
    pendingSystemTarget = 0;
    ResetView();
    ResizeInterface();
    UpdateTitle();
    FocusScene();
}
void WindowManager::ProcessCamera(GLFWwindow* window, const float& deltaTime) {
    if (!isFocused)
        return;
    auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    float speed = manager.cameraSpeed * deltaTime;
    if (manager.inStarSystem) {
        manager.starSystem.Move(window,deltaTime,cameraFront,cameraUp);
        return;
    }
    glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));

    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        speed *= 5.0f;

    // During a free fall gravity owns the trajectory; the mouse still aims the view.
    if (!manager.observerGravity) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            cameraPos += speed * cameraFront;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            cameraPos -= speed * cameraFront;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            cameraPos -= right * speed;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            cameraPos += right * speed;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
            cameraPos -= cameraUp * speed;
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
            cameraPos += cameraUp * speed;
    }

    // The redshift observer model uses static exterior Schwarzschild observers.
    if (manager.landed) {
        const auto center = BlackHole::PlanetPosition(manager.simulationTime, manager.landedPlanet);
        manager.AlignCameraUp(cameraPos - center);
        cameraPos = center + cameraUp * (manager.blackHole.PlanetSurfaceRadius(cameraUp, manager.simulationTime, manager.landedPlanet) + 0.001f);
    } else if (manager.showBlackHole) {
        const int planet = BlackHole::NearestPlanet(cameraPos, manager.simulationTime);
        const auto planetCenter = BlackHole::PlanetPosition(manager.simulationTime, planet);
        const auto offset = cameraPos - planetCenter;
        const float distance = glm::length(offset);
        const auto radial = distance > 0.00001f ? offset / distance : glm::vec3(0, 0, 1);
        const float surface = manager.blackHole.PlanetSurfaceRadius(radial, manager.simulationTime, planet) + 0.0003f;
        if (distance < surface)
            cameraPos = planetCenter + radial * surface;
        const float minimumRadius = manager.observerGravity || manager.horizonReached
            ? BlackHole::HorizonRadius() * 0.005f : 0.26f;
        if (glm::length(cameraPos) < minimumRadius)
            cameraPos = glm::normalize(cameraPos + glm::vec3(0.0f, 0.0f, 0.0001f)) * minimumRadius;
    }
}

unsigned int WindowManager::shaderProgram = 0, WindowManager::flatShader = 0, WindowManager::phongShader = 0;
unsigned int WindowManager::VAO, WindowManager::VBO, WindowManager::EBO;
float WindowManager::verticeDistance = 3.0f;

const char* phongVertextSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

out vec3 FragPos;
out vec3 Normal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position = projection * view * vec4(FragPos, 1.0);
}
)";

const char* phongFragmentSrc = R"(
#version 330 core
in vec3 FragPos;
in vec3 Normal;

out vec4 FragColor;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform vec3 objectColor;

void main()
{
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);

    float diff = max(dot(norm, lightDir), 0.0);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), 32.0);

    vec3 ambient = 0.1 * lightColor;
    vec3 diffuse = diff * lightColor * 1.2;
    vec3 specular = spec * lightColor * 2.5;

    vec3 result = (ambient + diffuse + specular) * objectColor;
    FragColor = vec4(result, 1.0);
}
)";

const char* flatVertexSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
)";

const char* flatFragmentSrc = R"(
#version 330 core
out vec4 FragColor;

void main() {
    FragColor = vec4(1.0);
}
)";

float vertices[] = {
    // Back face (0, 0, -1)
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
     0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
    -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,

    // Front face (0, 0, 1)
    -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
     0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
    -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,

    // Left face (-1, 0, 0)
    -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
    -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
    -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
    -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
    -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
    -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,

    // Right face (1, 0, 0)
     0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
     0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
     0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
     0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
     0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
     0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,

    // Bottom face (0, -1, 0)
    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
     0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
     0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
     0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
    -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
    -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,

    // Top face (0, 1, 0)
    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
     0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
     0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
    -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
    -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f
};


unsigned int createShader(GLenum type, const char* source) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "SHADER CREATE ERROR:\n" << infoLog << std::endl;
    }
    return shader;
}

unsigned int compileShaderProgram(const char* vertexSrc, const char* fragmentSrc) {
    unsigned int vertexShader = createShader(GL_VERTEX_SHADER, vertexSrc);
    unsigned int fragmentShader = createShader(GL_FRAGMENT_SHADER, fragmentSrc);

    unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int success;
    char infoLog[512];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cerr << "SHADER LINK ERROR:\n" << infoLog << std::endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

void WindowManager::InitializeOverlay() {
#ifdef _WIN32
    const char* vertex = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUv;
out vec2 vUv;
uniform vec4 bounds;
void main() {
    vUv = vec2(aUv.x, 1.0 - aUv.y);
    gl_Position = vec4(bounds.xy + aPos * bounds.zw, 0.0, 1.0);
}
)";
    const char* fragment = R"(
#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D overlay;
uniform float opacity;
void main() {
    vec4 texel = texture(overlay, vUv);
    fragColor = vec4(texel.rgb, texel.a * opacity);
}
)";
    overlayProgram = compileShaderProgram(vertex, fragment);
    if (!overlayProgram)
        return;
    const float vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f
    };
    glGenVertexArrays(1, &overlayVao);
    glGenBuffers(1, &overlayVbo);
    glBindVertexArray(overlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(0);
    glGenTextures(1, &overlayTexture);
    glBindTexture(GL_TEXTURE_2D, overlayTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
#endif
}

void WindowManager::ReleaseOverlay() {
    if (overlayProgram) { glDeleteProgram(overlayProgram); overlayProgram = 0; }
    if (overlayVao) { glDeleteVertexArrays(1, &overlayVao); overlayVao = 0; }
    if (overlayVbo) { glDeleteBuffers(1, &overlayVbo); overlayVbo = 0; }
    if (overlayTexture) { glDeleteTextures(1, &overlayTexture); overlayTexture = 0; }
    overlayWidth = overlayHeight = 0;
}

void WindowManager::ShowSpeedToast(float speed, bool starSystem) {
    std::ostringstream text;
    text << "Flight speed ";
    if (starSystem) {
        if (std::abs(speed) >= 1e-3f)
            text << std::fixed << std::setprecision(4) << speed << " AU/s";
        else
            text << std::scientific << std::setprecision(2) << speed << " AU/s";
    } else {
        if (std::abs(speed - std::round(speed)) < 0.001f)
            text << std::fixed << std::setprecision(0) << speed << "x";
        else if (speed >= 1.0f)
            text << std::fixed << std::setprecision(1) << speed << "x";
        else
            text << std::fixed << std::setprecision(2) << speed << "x";
    }
    speedToastUntil = glfwGetTime() + 1.0;
#ifdef _WIN32
    if (!overlayProgram || !overlayTexture)
        return;
    int dpi = 96;
    if (window)
        dpi = static_cast<int>(GetDpiForWindow(glfwGetWin32Window(window)));
    int width = 0, height = 0;
    std::vector<unsigned char> pixels;
    if (!BuildToastTexture(text.str(), dpi, width, height, pixels))
        return;
    glBindTexture(GL_TEXTURE_2D, overlayTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    overlayWidth = width;
    overlayHeight = height;
#else
    (void)speed;
    (void)starSystem;
#endif
}

void WindowManager::RenderSpeedToast() {
    if (!overlayProgram || !overlayTexture || overlayWidth <= 0 || overlayHeight <= 0)
        return;
    const double remaining = speedToastUntil - glfwGetTime();
    if (remaining <= 0.0)
        return;
    const float opacity = static_cast<float>(glm::clamp(remaining / 0.2, 0.0, 1.0));
    int viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0)
        return;
    const float halfWidth = static_cast<float>(overlayWidth) / viewport[2];
    const float halfHeight = static_cast<float>(overlayHeight) / viewport[3];
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(overlayProgram);
    glUniform4f(glGetUniformLocation(overlayProgram, "bounds"), 0.0f, 0.0f, halfWidth, halfHeight);
    glUniform1f(glGetUniformLocation(overlayProgram, "opacity"), opacity);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, overlayTexture);
    glUniform1i(glGetUniformLocation(overlayProgram, "overlay"), 0);
    glBindVertexArray(overlayVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}


void WindowManager::Update(float& dT) {
    // Window drags and menu tracking block this loop; one long frame must not advance the simulation by hours.
    dT = glm::min(dT, 0.1f);
    // The main menu covers the whole window and a minimized window shows nothing, so wait for input instead of spinning.
    if (mainMenu.Visible() || glfwGetWindowAttrib(window, GLFW_ICONIFIED)) glfwWaitEvents();
    else glfwPollEvents();
    if (isClosed() || mainMenu.Visible()) return;
    int fullWidth,fullHeight,logicalWidth,logicalHeight;
    glfwGetFramebufferSize(window,&fullWidth,&fullHeight);
    glfwGetWindowSize(window,&logicalWidth,&logicalHeight);
    if (fullWidth<=0 || fullHeight<=0 || logicalWidth<=0) return;
    const int sceneWidth=fullWidth-scenePanel.Width()*fullWidth/logicalWidth;
    glViewport(0,0,glm::max(1,sceneWidth),fullHeight);
    if (quantumMode) {
        if (simulationPlaying) quantum.Advance(static_cast<double>(dT)*quantumRates[quantumRateIndex]);
        titleUpdateElapsed+=dT;
        if (titleUpdateElapsed>=.2f) { RefreshPanel(); UpdateTitle(); titleUpdateElapsed=0; }
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        quantum.Render(static_cast<float>(sceneWidth)/fullHeight);
        glfwSwapBuffers(window); return;
    }
    const double previousTime = simulationTime;
    if (simulationPlaying) {
        const double interval = static_cast<double>(dT) * timeRates[timeRateIndex];
        simulationTime += interval;
    }
    titleUpdateElapsed += dT;
    if (titleUpdateElapsed >= 0.2f) {
        UpdateTitle();
        RefreshPanel();
        titleUpdateElapsed = 0.0f;
    }

    const bool falling = observerGravity;
    if (!inStarSystem) UpdateObserver(previousTime, dT);
    if (!falling && simulationTime > previousTime) {
        double speed = 0.0;
        if (landed) {
            const auto center = BlackHole::PlanetPosition(previousTime, landedPlanet);
            speed = std::sqrt(BlackHole::GravityParameter() / glm::length(center));
        }
        localTime += (simulationTime - previousTime) * (showBlackHole ? OrbitalMechanics::ObserverClockRate(
            glm::max(static_cast<double>(glm::length(cameraPos)), BlackHole::HorizonRadius() * 1.015),
            speed, BlackHole::GravityParameter(), BlackHole::HorizonRadius()) : 1.0);
    }
    if (blackHoleReady && showBlackHole && !inStarSystem)
        blackHole.Update(simulationTime);
    if (inStarSystem) starSystem.Update(simulationTime,cameraFront,cameraUp);
    if (showBlackHole && followPlanet) {
        const auto planetPosition = BlackHole::PlanetPosition(simulationTime);
        cameraPos += planetPosition - lastPlanetPosition;
        lastPlanetPosition = planetPosition;
    }
    if (showBlackHole && followSpacecraft) {
        const auto craftPosition = blackHole.SpacecraftPosition();
        cameraPos += craftPosition - lastSpacecraftPosition;
        lastSpacecraftPosition = craftPosition;
    }
    if (showBlackHole && followSelected) {
        const auto position = blackHole.ResolveTarget(followingTarget, simulationTime).position;
        cameraPos += position - lastSelectedPosition;
        lastSelectedPosition = position;
    }
    ProcessCamera(window, dT);

    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    framebufferWidth=sceneWidth;
    if (framebufferWidth == 0 || framebufferHeight == 0) {
        return;
    }

    glm::mat4 model = glm::mat4(1.0f);
    if (showingDefaultCube) {
        const float angle = static_cast<float>(glm::radians(std::fmod(GetSimulationTime() * 30.0, 360.0)));
        model = glm::rotate(model, angle, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    const float aspectRatio = static_cast<float>(framebufferWidth) / framebufferHeight;
    glm::mat4 projection = glm::perspective(glm::radians(fieldOfView), aspectRatio,
        landed ? 0.0001f : 0.01f, landed ? 100.0f : 2000.0f);

    optics.BeginFrame();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (inStarSystem && starSystemReady) {
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        starSystem.Render(fieldOfView,aspectRatio,cameraFront,cameraUp,simulationTime,trajectories,photonView);
    } else if (showBlackHole && blackHoleReady) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        blackHole.Render(projection * view, cameraPos, simulationTime,
            observerGravity || horizonReached ? glm::vec3(observer.velocity) : glm::vec3(0.0f),trajectories,selectedTarget);
    } else {
        glPolygonMode(GL_FRONT_AND_BACK, showWireframe ? GL_LINE : GL_FILL);
        //Apply Lighting When Out of Wireframe Mode
        if (!showWireframe)
        {
            glUseProgram(phongShader);

            glUniformMatrix4fv(glGetUniformLocation(phongShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(phongShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(phongShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

            glm::vec3 dynamicLight = cameraPos + glm::vec3(0, 0, -2);
            glUniform3f(glGetUniformLocation(phongShader, "lightPos"), dynamicLight.x, dynamicLight.y, dynamicLight.z);
            glUniform3f(glGetUniformLocation(phongShader, "viewPos"), cameraPos.x, cameraPos.y, cameraPos.z);
            glUniform3f(glGetUniformLocation(phongShader, "lightColor"), 1.0f, 1.0f, 0.0f);
            glUniform3f(glGetUniformLocation(phongShader, "objectColor"), 1.0f, 1.0f, 1.0f);
        }
        else
        {
            glUseProgram(flatShader);

            glUniformMatrix4fv(glGetUniformLocation(flatShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(flatShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(flatShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        }

        glBindVertexArray(VAO);

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(currentModelVertices.size() / 6));

        glBindVertexArray(0);
    }
    
    optics.Render(visionProfile, fieldOfView, exposureEv, hdrMode);
    RenderSpeedToast();
    glfwSwapBuffers(window);
}

void WindowManager::InitializeGLFW()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
}

bool WindowManager::MakeNewWindow() {
    int areaX = 0, areaY = 0, areaWidth = WIDTH, areaHeight = HEIGHT;
    float scale = 1.0f;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
        glfwGetMonitorWorkarea(monitor, &areaX, &areaY, &areaWidth, &areaHeight);
        glfwGetMonitorContentScale(monitor, &scale, nullptr);
    }
    const int width = std::min(static_cast<int>(WIDTH * scale), areaWidth * 9 / 10);
    const int height = std::min(static_cast<int>(HEIGHT * scale), areaHeight * 8 / 10);
    window = glfwCreateWindow(width, height, "Render-Engine", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create an OpenGL 3.3 core window\n";
        return false;
    }
    glfwSetWindowPos(window, areaX + (areaWidth - width) / 2, areaY + (areaHeight - height) / 2);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "Failed to load OpenGL functions\n";
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glfwSetWindowUserPointer(window, this);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetWindowFocusCallback(window, WindowFocusCallback);
    WindowFocusCallback(window, 1);
    glfwSetCursorPosCallback(window, MouseCallback);
    glfwSetDropCallback(window, DropCallback);

    flatShader = compileShaderProgram(flatVertexSrc, flatFragmentSrc);
    phongShader = compileShaderProgram(phongVertextSrc, phongFragmentSrc);
    blackHoleReady = blackHole.Initialize();
    optics.Initialize();
    InitializeOverlay();
    starSystemReady = starSystem.Initialize();
    quantumReady = quantum.Initialize();
    shaderProgram = flatShader;

    currentModelVertices.assign(std::begin(vertices), std::end(vertices));
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, currentModelVertices.size() * sizeof(float), currentModelVertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

#ifdef _WIN32
    const HWND nativeWindow = glfwGetWin32Window(window);
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(nativeWindow, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    UiTheme::EnableDarkMenus();

    const HMENU menu = CreateMenu();
    nativeMenu = menu;
    const auto popup = [&](const wchar_t* title, std::initializer_list<std::pair<UINT, const wchar_t*>> items) {
        const HMENU submenu = CreatePopupMenu();
        for (const auto& [id, text] : items)
            AppendMenuW(submenu, id ? MF_STRING : MF_SEPARATOR, id, text);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), title);
        return submenu;
    };
    AppendMenuW(menu, MF_STRING, 3030, L"Main menu");
    popup(L"Scene", {
        {700, L"Gargantua black hole (off: model viewer)"},
        {701, L"Miller flyby"},
        {702, L"Spacecraft view"},
        {0, nullptr},
        {GLFW_KEY_F4, L"Overview\tF4"},
        {GLFW_KEY_HOME, L"Home view\tHome"}});
    popup(L"Time", {
        {GLFW_KEY_SPACE, L"Playing\tSpace"},
        {GLFW_KEY_COMMA, L"Slower time\t<"},
        {GLFW_KEY_PERIOD, L"Faster time\t>"}});
    popup(L"Camera", {
        {GLFW_KEY_LEFT_BRACKET, L"Slower flight\t["},
        {GLFW_KEY_RIGHT_BRACKET, L"Faster flight\t]"},
        {0, nullptr},
        {GLFW_KEY_G, L"Go to selection\tG"},
        {GLFW_KEY_F, L"Follow selection\tF"},
        {GLFW_KEY_L, L"Landed on surface\tL"},
        {GLFW_KEY_H, L"Free fall\tH"}});
    popup(L"Spacecraft", {
        {GLFW_KEY_B, L"Boost\tB"},
        {GLFW_KEY_R, L"Reset craft\tR"}});
    const HMENU viewMenu = popup(L"View", {
        {GLFW_KEY_F3, L"Information panel\tF3"},
        {GLFW_KEY_F2, L"Orbit paths\tF2"},
        {GLFW_KEY_F6, L"Photon view\tF6"},
        {GLFW_KEY_F1, L"Wireframe model\tF1"},
        {0, nullptr}});
    const HMENU visionMenu = CreatePopupMenu();
    AppendMenuW(visionMenu, MF_STRING, 1000, L"Free camera");
    AppendMenuW(visionMenu, MF_STRING, 1001, L"Human vision (approx.)");
    AppendMenuW(visionMenu, MF_STRING, 1002, L"Canon EF 50mm f/1.2L (Lensfun)");
    AppendMenuW(visionMenu, MF_STRING, 1003, L"Nikkor 14-24mm at 14mm (Lensfun)");
    AppendMenuW(visionMenu, MF_STRING, 1004, L"ZEISS Otus 85mm (Lensfun)");
    AppendMenuW(visionMenu, MF_STRING, 1005, L"Ideal 200mm telephoto");
    AppendMenuW(visionMenu, MF_STRING, 1006, L"ZEISS Distagon 35mm f/2 (Lensfun)");
    AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(visionMenu), L"Lens");
    const HMENU exposureMenu = CreatePopupMenu();
    AppendMenuW(exposureMenu, MF_STRING, 1200, L"HDR tone mapping\tF7");
    AppendMenuW(exposureMenu, MF_STRING, 1201, L"Pure exposure\tF7");
    AppendMenuW(exposureMenu, MF_SEPARATOR, 0, nullptr);
    const wchar_t* exposureNames[] = {L"-8 EV", L"-4 EV", L"-2 EV", L"0 EV", L"+2 EV", L"+4 EV", L"+8 EV", L"+12 EV", L"+16 EV"};
    for (int level = 0; level < 9; ++level)
        AppendMenuW(exposureMenu, MF_STRING, 1100 + level, exposureNames[level]);
    AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(exposureMenu), L"Exposure");

    SetPropW(nativeWindow, L"RenderEngineWindow", window);
    originalWindowProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(nativeWindow, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(+[](HWND handle, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
            auto* glfwWindow=static_cast<GLFWwindow*>(GetPropW(handle,L"RenderEngineWindow"));
            auto* manager=glfwWindow?static_cast<WindowManager*>(glfwGetWindowUserPointer(glfwWindow)):nullptr;
            if (message==WM_COMMAND && manager) {
                if (LOWORD(wParam)==3030) manager->OpenMainMenu();
                else manager->HandleInterfaceCommand(LOWORD(wParam),HIWORD(wParam));
                return 0;
            }
            if (message==WM_SIZE && manager) manager->ResizeInterface();
            // F10's release would otherwise start keyboard menu mode, a modal loop that stalls rendering.
            if (message==WM_SYSKEYUP && wParam==VK_F10) return 0;
            if ((message==WM_CTLCOLORSTATIC || message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX
                 || message==WM_CTLCOLORBTN) && manager) {
                const int id=GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
                return reinterpret_cast<LRESULT>(id>=3000?manager->mainMenu.ControlColor(reinterpret_cast<void*>(wParam),id):
                    manager->scenePanel.ControlColor(reinterpret_cast<void*>(wParam),id));
            }
            if (message==WM_DRAWITEM && manager) {
                const auto* item=reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
                if (item->CtlID>=3000) manager->mainMenu.DrawItem(item);
                else manager->scenePanel.DrawItem(item);
                return TRUE;
            }
            LRESULT menuResult=0;
            if (UiTheme::HandleMenuBarMessage(handle,message,wParam,lParam,menuResult)) return menuResult;
            if (message==WM_NCPAINT || message==WM_NCACTIVATE) {
                const LRESULT result=CallWindowProcW(originalWindowProcedure, handle, message, wParam, lParam);
                UiTheme::PaintMenuBarSeparator(handle);
                return result;
            }
            if (message==WM_GETMINMAXINFO) {
                auto* bounds=reinterpret_cast<MINMAXINFO*>(lParam);
                const UINT dpi=GetDpiForWindow(handle);
                bounds->ptMinTrackSize={MulDiv(900,dpi,96),MulDiv(600,dpi,96)};
                return 0;
            }
            return CallWindowProcW(originalWindowProcedure, handle, message, wParam, lParam);
        })));
    scenePanel.Create(nativeWindow);
    scenePanel.SetVisible(false);
    std::vector<std::string> quantumNames;
    for (const auto& example:QuantumStates::Examples()) quantumNames.push_back(example.name);
    mainMenu.Create(nativeWindow,quantumNames);
    EnumChildWindows(nativeWindow,+[](HWND control,LPARAM)->BOOL {
        wchar_t type[16];
        GetClassNameW(control, type, 16);
        // Single-line edits need the dark file-dialog theme for their border; everything else uses dark Explorer scroll bars.
        const bool singleLineEdit = lstrcmpiW(type, L"Edit") == 0 && !(GetWindowLongW(control, GWL_STYLE) & ES_MULTILINE);
        SetWindowTheme(control, singleLineEdit ? L"DarkMode_CFD" : L"DarkMode_Explorer", nullptr);
        SetPropW(control,L"RenderEnginePreviousControlProc",reinterpret_cast<HANDLE>(
            SetWindowLongPtrW(control,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(InterfaceKeyProcedure))));
        return TRUE;
    },0);
    OpenMainMenu();
#endif
    glfwShowWindow(window);
    return true;
}
void WindowManager::ReleaseGraphics() {
    quantum.Release();
    starSystem.Release();
    optics.Release();
    ReleaseOverlay();
    blackHole.Release();
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(flatShader);
    glDeleteProgram(phongShader);
#ifdef _WIN32
    const HWND nativeWindow = glfwGetWin32Window(window);
    EnumChildWindows(nativeWindow,+[](HWND control,LPARAM)->BOOL {
        const auto previous=GetPropW(control,L"RenderEnginePreviousControlProc");
        if (previous) SetWindowLongPtrW(control,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));
        RemovePropW(control,L"RenderEnginePreviousControlProc");
        return TRUE;
    },0);
    mainMenu.Release();
    scenePanel.Release();
    UiTheme::ReleaseResources();
    const HMENU menu = static_cast<HMENU>(nativeMenu);
    SetWindowLongPtrW(nativeWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWindowProcedure));
    RemovePropW(nativeWindow, L"RenderEngineWindow");
    SetMenu(nativeWindow, nullptr);
    DestroyMenu(menu);
#endif
}

int WindowManager::RunBenchmark() {
    using clock = std::chrono::steady_clock;
    glfwSwapInterval(0);
    glfwSetWindowSize(window, 1920, 1080);
    {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        std::cout << "framebuffer " << width << 'x' << height << std::endl;
    }
    const std::pair<const char*, std::function<void()>> stages[] = {
        {"gargantua", [&] { StartGargantua(); }},
        {"miller-flyby", [&] { StartGargantua(); KeyCallback(window, 701, 0, GLFW_PRESS, 0); }},
        {"solar-system", [&] { StartSolarSystem(); }},
        {"quantum", [&] { StartQuantum(0); }},
    };
    std::cout << "scene start_ms frames mean_ms p50_ms p95_ms max_ms" << std::endl;
    for (const auto& [name, start] : stages) {
        const auto startBegin = clock::now();
        start();
        const double startMs = std::chrono::duration<double, std::milli>(clock::now() - startBegin).count();
        simulationPlaying = true;
        std::vector<double> frames;
        auto last = clock::now();
        const auto begin = last;
        while (!isClosed() && clock::now() - begin < std::chrono::seconds(6)) {
            const auto now = clock::now();
            float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            Update(dt);
            if (now - begin > std::chrono::seconds(1)) frames.push_back(dt * 1000.0);
        }
        if (frames.empty()) return 1;
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
        glFinish();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glReadBuffer(GL_FRONT);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        std::ofstream image(std::string("benchmark-") + name + ".ppm", std::ios::binary);
        image << "P6\n" << width << ' ' << height << "\n255\n";
        for (int row = height - 1; row >= 0; --row)
            image.write(reinterpret_cast<const char*>(pixels.data()) + static_cast<size_t>(row) * width * 3, width * 3);
        std::sort(frames.begin(), frames.end());
        double sum = 0;
        for (double f : frames) sum += f;
        std::cout << name << ' ' << std::fixed << std::setprecision(3) << startMs << ' ' << frames.size() << ' ' << std::fixed << std::setprecision(3)
                  << sum / frames.size() << ' ' << frames[frames.size() / 2] << ' '
                  << frames[frames.size() * 95 / 100] << ' ' << frames.back() << std::endl;
    }
    return 0;
}

bool WindowManager::isClosed() {
    return glfwWindowShouldClose(window);
}
