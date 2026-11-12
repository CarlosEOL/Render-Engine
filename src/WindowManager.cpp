#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include "glad/glad.h"
#include "WindowManager.h"
#include "InterstellarWorlds.h"
#include "ObjectLoader.h"
#include "Astronomy.h"
#include "CameraMotion.h"
#ifdef _WIN32
#include <GLFW/glfw3native.h>
#endif
#ifdef RENDER_ENGINE_DX12
#include "dx12/Dx12SceneBridge.h"
#endif

#include <algorithm>
#include <cmath>
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
glm::dvec3 WindowManager::cameraPos = glm::dvec3(0.0,0.0,3.0);
glm::vec3 WindowManager::cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 WindowManager::cameraUp    = glm::vec3(0.0f, 1.0f, 0.0f);
std::vector<float> WindowManager::currentModelVertices;

namespace {
    void SetApplicationTitle(GLFWwindow* window,const std::string& title) {
#ifdef RENDER_ENGINE_DX12
        const auto identified=title+" | DX12";glfwSetWindowTitle(window,identified.c_str());
#else
        glfwSetWindowTitle(window,title.c_str());
#endif
    }
    void PresentFrame(GLFWwindow* window) {
#ifdef RENDER_ENGINE_DX12
        NativeDX12::PresentScene();
#else
        glfwSwapBuffers(window);
#endif
    }
    bool firstMouse = true;
    float yaw   = -90.0f;
    float pitch = 0.0f;
    double lastX = WIDTH / 2.0;
    double lastY = HEIGHT / 2.0;

    float timeSinceLastLog = 0.0f;
    constexpr double timeRates[] = {1.0, 60.0, 3600.0, 86400.0, 2592000.0, 31557600.0};
    constexpr double quantumRates[] = {.1, .25, 1, 2, 4};
#ifdef _WIN32
    WNDPROC originalWindowProcedure = nullptr;

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
                if (key == VK_RETURN && id==3020) {
                    SendMessageW(parent,WM_COMMAND,3014,0);
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
            // Arrow/Page keys still drive the lists and the information box,
            // but every other key is forwarded to the GLFW window so global
            // shortcuts and camera movement keep working while the browser
            // owns keyboard focus.
            if ((listBox && listNavigation) || (infoBox && textNavigation))
                return CallWindowProcW(previous,control,message,key,detail);
            if (GetKeyState(VK_CONTROL)<0 || GetKeyState(VK_MENU)<0)
                return CallWindowProcW(previous,control,message,key,detail);
            SendMessageW(parent,message,key,detail);
            return 0;
        }
        if (message == WM_KEYUP || message == WM_SYSKEYUP) {
            SendMessageW(parent,message,key,detail);
            return CallWindowProcW(previous,control,message,key,detail);
        }
        if (message == WM_CHAR) {
            // Letters are global shortcuts here; the search field handles
            // filtering, so suppress the list box type-ahead.
            if (listBox || infoBox)
                return 0;
        }
        return CallWindowProcW(previous,control,message,key,detail);
    }

    bool BuildToastTexture(const std::string& text, int dpi, int& width, int& height,
                           std::vector<unsigned char>& pixels,int fontPixels=30) {
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

        const int fontSize = -MulDiv(fontPixels, dpi, 96);
        HFONT font = CreateFontW(fontSize, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(dc, font);

        SIZE textSize = {};
        GetTextExtentPoint32W(dc, wide.c_str(), static_cast<int>(wide.size()), &textSize);

        const int paddingX = MulDiv(fontPixels<20?14:28, dpi, 96);
        const int paddingY = MulDiv(fontPixels<20?7:12, dpi, 96);
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

namespace _UTILITY_
{
    bool EndsWith(const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() &&
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    std::string SpeedLabel(double speed,bool physical) {
        std::ostringstream text;
        const double metres=speed*Astronomy::metresPerAu;
        if(!physical)text<<std::fixed<<std::setprecision(2)<<speed<<" units/s";
        else if(metres<1000)text<<std::fixed<<std::setprecision(2)<<metres<<" m/s";
        else if(metres<1e8)text<<std::fixed<<std::setprecision(1)<<metres/1000<<" km/s";
        else text<<std::fixed<<std::setprecision(4)<<speed<<" AU/s";
        return text.str();
    }
}

void WindowManager::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (manager) manager->ResizeInterface();
}

void WindowManager::ScrollCallback(GLFWwindow* window, double, double yOffset) {
    auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (yOffset == 0.0 || manager.mainMenu.Visible())
        return;
    const double factor = std::pow(1.15,glm::clamp(yOffset,-8.0,8.0));
    if (manager.inStarSystem) {
        manager.starSystem.speedAu = glm::clamp(manager.starSystem.speedAu * factor, 1e-15, 1e6);
        manager.ShowSpeedToast(static_cast<float>(manager.starSystem.speedAu), true);
    } else {
        manager.cameraSpeed = glm::clamp(manager.cameraSpeed * static_cast<float>(factor), manager.showBlackHole?1e-12f:.001f, 100000.0f);
        manager.ShowSpeedToast(manager.cameraSpeed, false);
    }
    manager.UpdateTitle();
}

void WindowManager::KeyCallback(GLFWwindow* window, int key, int, int action, int mods) {
    if (action != GLFW_PRESS)
        return;
    auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    manager.sceneCacheValid=false;
    if (key == GLFW_KEY_F10 || key == 3030) { manager.OpenMainMenu(); return; }
    if (manager.mainMenu.Visible()) return;
    if(key==1202 || key==1203){manager.optics.SetAutoExposure(key==1202);manager.UpdateTitle();return;}
    if(key==1204 || key==1205){manager.optics.SetBlackHoleVisibility(key==1204);return;}
    if(key==1223 || key==1224){manager.optics.SetReferenceGlare(key==1223);return;}
    if(key==1324 || key==1325){
        manager.starSystem.SetWarmStellarColor(key==1324);
        manager.blackHole.SetWarmStellarColor(key==1324);
        return;
    }
    if(key==1320 || key==1321){
        manager.starSystem.SetStellarActivity(key==1321);
        manager.blackHole.SetStellarActivity(key==1321);
        return;
    }
    if(key==1322){
        if(manager.inStarSystem){
            manager.starSystem.SetStellarActivity(true);
            manager.starSystem.TriggerStellarFlare(manager.simulationTime);
        }else{
            manager.blackHole.SetStellarActivity(true);
            manager.blackHole.TriggerStellarFlare(manager.simulationTime);
        }
        return;
    }
    if(key==1303){manager.frameLimit=30;manager.blackHoleResolution=.5f;return;}
    if(key>=1300 && key<=1302){manager.frameLimit=key==1300?30:key==1301?60:120;return;}
    if(key>=1310 && key<=1312){manager.blackHoleResolution=key==1310?.5f:key==1311?.75f:1;return;}
    if(key>=1330 && key<=1333){
        const double values[]={0,.5,.8,.98};manager.blackHole.SetSpin(values[key-1330]);manager.RefreshPanel();return;
    }
    if(key>=1340 && key<=1342){manager.blackHole.SetRayBudget(key==1340?96:key==1341?160:256);manager.RefreshPanel();return;}
    if(key>=1220 && key<=1222){manager.lensFlareStrength=key==1220?0:key==1221?.45f:1;return;}
    if (key == GLFW_KEY_F3) { manager.scenePanel.ToggleInformation(); manager.ResizeInterface(); return; }
    if (key == GLFW_KEY_F2) { manager.trajectories=!manager.trajectories; return; }
    if (key == GLFW_KEY_F5) { manager.SetMouseCaptured(false);manager.RefreshObjectEditor();manager.objectEditor.Toggle();return; }
    if (manager.quantumMode) {
        if (key==GLFW_KEY_SPACE) manager.simulationPlaying=!manager.simulationPlaying;
        else if ((key==GLFW_KEY_R && (mods&GLFW_MOD_CONTROL)) || key==703) manager.quantum.Select(manager.quantum.Selected());
        else if (key==GLFW_KEY_COMMA) manager.quantumRateIndex=glm::max(0,manager.quantumRateIndex-1);
        else if (key==GLFW_KEY_PERIOD) manager.quantumRateIndex=glm::min(4,manager.quantumRateIndex+1);
        else if (key==GLFW_KEY_HOME) {
            cameraPos={0,0,manager.quantum.ViewDistance()};cameraFront={0,0,-1};cameraUp={0,1,0};
        }
        manager.UpdateTitle();
        return;
    }
    if(key==GLFW_KEY_F8 || (key>=708 && key<=710)){
        manager.SetVehicleView(key==GLFW_KEY_F8?(manager.interiorVehicle+1)%4:key-707);return;
    }
    if((key==GLFW_KEY_L && (mods&GLFW_MOD_SHIFT)) || key==707){manager.LookAtPlanet();return;}
    if((key==GLFW_KEY_F && (mods&GLFW_MOD_SHIFT)) || key==705)key=GLFW_KEY_T;
    if(manager.interiorVehicle && (key==GLFW_KEY_T || key==704 || (key==GLFW_KEY_R && (mods&GLFW_MOD_SHIFT)))){
        manager.ShowStatusToast("Object following is available in free flight");return;
    }
    if(manager.interiorVehicle && key==GLFW_KEY_L){manager.ShowStatusToast("F8 or Home leaves the cockpit");return;}
    if ((key==GLFW_KEY_R && (mods&GLFW_MOD_SHIFT)) || key==704) {
        if (manager.inStarSystem) manager.starSystem.ToggleRotationFollow(manager.simulationTime);
        else if (manager.showBlackHole && manager.selectedTarget>=0) {
            manager.followSelectedRotation=!(manager.followSelected && manager.followSelectedRotation
                && manager.followingTarget==manager.selectedTarget);
            manager.followSelected=manager.followSelectedRotation;
            manager.followingTarget=manager.selectedTarget;
            manager.lastSelectedPosition=manager.blackHole.ResolveTarget(manager.selectedTarget,manager.simulationTime).position;
            manager.lastSelectedRotation=glm::dmat3(manager.blackHole.TargetRotation(manager.selectedTarget,manager.simulationTime));
            manager.followPlanet=manager.followSpacecraft=manager.landed=manager.observerGravity=false;
        }
        manager.ShowStatusToast((manager.inStarSystem?manager.starSystem.IsRotationFollowing():manager.followSelectedRotation)
            ?"Rotation follow on":"Rotation follow off");
        manager.RefreshPanel(); manager.UpdateTitle(); return;
    }
    if (key==GLFW_KEY_F4) {
        if(mods&GLFW_MOD_SHIFT)manager.PrepareCurrentBrowser();
        manager.scenePanel.ToggleBrowser((mods&GLFW_MOD_SHIFT)!=0);
        manager.ResizeInterface(); return;
    }
    if (key==706) { manager.PrepareCurrentBrowser(); manager.scenePanel.ToggleBrowser(true); manager.ResizeInterface(); return; }
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
        else if (key==GLFW_KEY_T) {
            manager.starSystem.ToggleFollow(manager.simulationTime);
            manager.ShowStatusToast(manager.starSystem.IsFollowing()?"Position follow on":"Position follow off");
        }
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
        manager.followSelected = false;
        manager.landed = false;
        manager.observerGravity = false;
        manager.horizonReached = false;
        cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
        manager.showBlackHole = true;
        manager.followPlanet = true;
        manager.followSpacecraft = false;
        manager.lastPlanetPosition = BlackHole::PlanetPosition(manager.simulationTime);
        const auto sun = glm::normalize(glm::vec3(-manager.lastPlanetPosition));
        const auto side = glm::normalize(glm::cross(sun, glm::vec3(0, 1, 0)));
        const auto approach = glm::normalize(0.35f * sun + 0.94f * side);
        const double radius=manager.blackHole.ResolveTarget(1,manager.simulationTime).radius;
        cameraPos = manager.lastPlanetPosition + radius*5.0 * glm::dvec3(approach);
        cameraFront = -approach;
        yaw = glm::degrees(std::atan2(cameraFront.z, cameraFront.x));
        pitch = glm::degrees(std::asin(cameraFront.y));
        manager.cameraSpeed = static_cast<float>(radius*.25);
        manager.ShowSpeedToast(manager.cameraSpeed, false);
        manager.ApplyVisionProfile(manager.visionProfile);
        firstMouse = true;
    } else if (key == 702 && manager.blackHoleReady) {
        manager.interiorVehicle=0;
        if (manager.inStarSystem) { manager.simulationTime=manager.localTime=0; manager.timeRateIndex=0; manager.blackHole.ResetSpacecraft(0); }
        manager.inStarSystem=false;
        manager.pendingSystemTarget=0;
        manager.selectedTarget=9;
        manager.followSelected = false;
        manager.landed = false;
        manager.observerGravity = false;
        manager.horizonReached = false;
        cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
        manager.blackHole.Update(manager.simulationTime);
        manager.showBlackHole = true;
        manager.followPlanet = false;
        manager.followSpacecraft = true;
        manager.lastSpacecraftPosition = manager.blackHole.SpacecraftPosition();
        cameraPos = manager.lastSpacecraftPosition
            + glm::normalize(manager.lastSpacecraftPosition) * (500.0/Astronomy::metresPerAu);
        cameraFront = glm::normalize(manager.lastSpacecraftPosition - cameraPos);
        yaw = glm::degrees(std::atan2(cameraFront.z, cameraFront.x));
        pitch = glm::degrees(std::asin(cameraFront.y));
        manager.cameraSpeed = static_cast<float>(10.0/Astronomy::metresPerAu);
        manager.ShowSpeedToast(manager.cameraSpeed, false);
        manager.ApplyVisionProfile(manager.visionProfile);
        manager.RefreshPanel();
        firstMouse = true;
    } else if (key == GLFW_KEY_B && manager.blackHoleReady) {
        manager.blackHole.Update(manager.simulationTime);
        manager.blackHole.BoostSpacecraft();
    } else if ((key == 703 || (key == GLFW_KEY_R && (mods&GLFW_MOD_CONTROL))) && manager.blackHoleReady) {
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
        cameraPos = target.position + glm::normalize(offset) * glm::max(Astronomy::observerClearanceAu*10, static_cast<double>(target.radius) * 6.0);
        cameraFront = glm::normalize(target.position - cameraPos);
        yaw = glm::degrees(std::atan2(cameraFront.z, cameraFront.x));
        pitch = glm::degrees(std::asin(cameraFront.y));
        manager.cameraSpeed = glm::max(static_cast<float>(1.0/Astronomy::metresPerAu),target.radius*.25f);
        manager.ShowSpeedToast(manager.cameraSpeed, false);
    } else if (key == GLFW_KEY_H && manager.blackHoleReady) {
        manager.followSelected = false;
        manager.showBlackHole = true;
        manager.landed = false;
        manager.followPlanet = false;
        manager.followSpacecraft = false;
        manager.observerGravity = !manager.observerGravity;
        manager.horizonReached = false;
        manager.observer = {glm::dvec3(cameraPos), glm::dvec3(0.0)};
        manager.kerrObserverValid=false;manager.pendingObserverProper=0;
        cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    } else if (key == GLFW_KEY_F6 && manager.blackHoleReady) {
        manager.showBlackHole = !manager.inStarSystem;
        manager.photonView = !manager.photonView;
        if (manager.inStarSystem && manager.photonView) manager.starSystem.SetPhotonDetector();
        manager.blackHole.SetPhotonView(manager.photonView, cameraPos);
    } else if (key == GLFW_KEY_T && manager.blackHoleReady && manager.selectedTarget >= 0) {
        manager.followSelectedRotation=false;
        manager.followSelected = !manager.followSelected || manager.followingTarget != manager.selectedTarget;
        manager.followingTarget = manager.selectedTarget;
        manager.lastSelectedPosition = manager.blackHole.ResolveTarget(manager.followingTarget, manager.simulationTime).position;
        manager.followPlanet = false;
        manager.followSpacecraft = false;
        manager.landed = false;
        manager.observerGravity = false;
        manager.ShowStatusToast(manager.followSelected?"Position follow on":"Position follow off");
    } else if (key == GLFW_KEY_HOME) {
        manager.ResetView();
    } else if (key == GLFW_KEY_LEFT_BRACKET) {
        manager.cameraSpeed = glm::max(manager.showBlackHole?1e-12f:.005f, manager.cameraSpeed * 0.5f);
        manager.ShowSpeedToast(manager.cameraSpeed, false);
    } else if (key == GLFW_KEY_RIGHT_BRACKET) {
        manager.cameraSpeed = glm::min(50.0f, manager.cameraSpeed * 2.0f);
        manager.ShowSpeedToast(manager.cameraSpeed, false);
    } else if (key == GLFW_KEY_COMMA) {
        manager.timeRateIndex = glm::max(0, manager.timeRateIndex - 1);
    } else if (key == GLFW_KEY_PERIOD) {
        manager.timeRateIndex = glm::min(5, manager.timeRateIndex + 1);
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

void WindowManager::ResetView() {
    interiorVehicle=0;
    followSelected = false;
    landed = false;
    observerGravity = false;
    horizonReached = false;
    cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    followPlanet = false;
    followSpacecraft = false;
    cameraPos = glm::dvec3(0.0,0.0,showBlackHole?BlackHole::HorizonRadius()*12.0:3.0);
    cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    yaw = -90.0f;
    pitch = 0.0f;
    ApplyVisionProfile(visionProfile);
    cameraSpeed = 1.0f;
    ShowSpeedToast(cameraSpeed, false);
    firstMouse = true;
}

void WindowManager::SetVehicleView(int vehicle){
    if(!blackHoleReady)return;
    if(inStarSystem || quantumMode)StartGargantua();
    const int previous=interiorVehicle;
    interiorVehicle=glm::clamp(vehicle,0,3);
    landed=observerGravity=followPlanet=followSpacecraft=followSelected=false;
    inStarSystem=quantumMode=false;showBlackHole=true;pendingSystemTarget=0;selectedTarget=9;
    blackHole.Update(simulationTime);
    const auto pose=blackHole.CockpitPose(interiorVehicle?interiorVehicle:std::max(1,previous),simulationTime);
    if(interiorVehicle){
        cameraPos=pose.position;lastCabinRotation=pose.rotation;
        cameraFront=glm::vec3(pose.rotation*glm::dvec3(0,0,-1));
        cameraUp=glm::vec3(pose.rotation*glm::dvec3(0,1,0));
        fieldOfView=75;
    }else{
        cameraPos=pose.position+pose.rotation*glm::dvec3(0,0,500/Astronomy::metresPerAu);
        cameraFront=glm::normalize(glm::vec3(pose.position-cameraPos));cameraUp=glm::vec3(pose.rotation[1]);
        cameraSpeed=static_cast<float>(10/Astronomy::metresPerAu);
    }
    sceneCacheValid=false;SyncCameraAngles();RefreshPanel();UpdateTitle();
    ShowStatusToast(interiorVehicle==1?"Endurance interior":interiorVehicle==2?"Ranger 1 interior":interiorVehicle==3?"Ranger 2 interior":"Free flight");
}

void WindowManager::LookAtPlanet(){
    glm::dvec3 target;std::string name;
    if(inStarSystem){
        int planet=starSystem.Selected();
        if(planet<0){
            double closest=std::numeric_limits<double>::infinity();
            for(int i=0;i<static_cast<int>(starSystem.Planets().size());++i){
                const double d=glm::length(starSystem.Position(i,simulationTime)-starSystem.camera);
                if(d<closest){closest=d;planet=i;}
            }
        }
        if(planet<0)return;
        target=starSystem.Position(planet,simulationTime)-starSystem.camera;name=starSystem.Planets()[planet].name;
    }else if(showBlackHole){
        const int planet=selectedTarget>=1 && selectedTarget<=3?selectedTarget-1:BlackHole::NearestPlanet(cameraPos,simulationTime);
        target=BlackHole::PlanetPosition(simulationTime,planet)-cameraPos;name=InterstellarWorlds::Get(planet).name;
    }else return;
    if(glm::length(target)<1e-15)return;
    cameraFront=glm::normalize(glm::vec3(target));
    if(interiorVehicle)cameraUp=glm::vec3(lastCabinRotation[1]);
    if(std::abs(glm::dot(cameraFront,cameraUp))>.999f){
        const auto right=interiorVehicle?glm::vec3(lastCabinRotation[0]):glm::vec3(1,0,0);
        cameraFront=glm::normalize(cameraFront+right*.05f);
    }
    SyncCameraAngles();sceneCacheValid=false;ShowStatusToast("Looking at "+name);
}

void WindowManager::ApplyVisionProfile(int profile) {
    visionProfile = profile;
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    int logicalWidth,logicalHeight;
    glfwGetWindowSize(window,&logicalWidth,&logicalHeight);
    if (logicalWidth>0) width-=scenePanel.Width()*width/logicalWidth;
    if (width > 0 && height > 0)
        lensAspect = static_cast<float>(width) / height;
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
        cameraPos = lastPlanetPosition + glm::dvec3(cameraUp)
            * (blackHole.PlanetSurfaceRadius(cameraUp, simulationTime, landedPlanet) + Astronomy::observerClearanceAu);
        auto tangent = cameraFront - cameraUp * glm::dot(cameraFront, cameraUp);
        if (glm::length(tangent) < 0.01f) {
            const auto sun = glm::normalize(glm::vec3(-lastPlanetPosition));
            tangent = sun - cameraUp * glm::dot(sun, cameraUp);
        }
        if (glm::length(tangent) < 0.01f) {
            const auto axis = std::abs(cameraUp.y) < 0.9f
                ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            tangent = glm::cross(cameraUp, axis);
        }
        cameraFront = glm::normalize(tangent);
        cameraSpeed = static_cast<float>(2.0/Astronomy::metresPerAu);
        ShowSpeedToast(cameraSpeed, false);
    } else {
        cameraPos += glm::dvec3(cameraUp) * (20.0/Astronomy::metresPerAu);
        cameraUp = glm::vec3(0, 1, 0);
    }
    yaw = glm::degrees(std::atan2(cameraFront.z, cameraFront.x));
    pitch = glm::degrees(std::asin(glm::clamp(cameraFront.y, -1.0f, 1.0f)));
    firstMouse = true;
}

void WindowManager::UpdateObserver(double previousTime, float realSeconds) {
    if (landed) {
        const auto center = BlackHole::PlanetPosition(simulationTime, landedPlanet);
        const auto rotation = BlackHole::PlanetRotation(simulationTime,landedPlanet)
            * glm::transpose(BlackHole::PlanetRotation(previousTime,landedPlanet));
        cameraPos = center + glm::dmat3(rotation)*(cameraPos-lastPlanetPosition);
        cameraFront = glm::normalize(glm::vec3(rotation * glm::vec4(cameraFront, 0.0f)));
        cameraUp = glm::normalize(glm::vec3(rotation * glm::vec4(cameraUp, 0.0f)));
        lastPlanetPosition = center;
    }
    if (!observerGravity)
        return;
    const auto fromDisk=InterstellarWorlds::Orientation(),toDisk=glm::transpose(fromDisk);
    const double rs=BlackHole::HorizonRadius(),spin=blackHole.Spin();
    if(!kerrObserverValid || observerSpin!=spin){
        if(kerrObserverValid){
            const auto beta=KerrSpacetime::ObserverBeta(kerrObserver,rs,observerSpin);
            kerrObserver=KerrSpacetime::MakeObserver(toDisk*cameraPos,rs,spin,beta);
        }else kerrObserver=KerrSpacetime::MakeStationaryObserver(toDisk*cameraPos,rs,spin);
        kerrObserverValid=true;observerSpin=spin;
    }
    pendingObserverProper+=std::max(0.0,static_cast<double>(realSeconds))*timeRates[timeRateIndex];
    const double oldCoordinate=kerrObserver.coordinateSeconds;
    const double horizon=KerrSpacetime::Horizon(rs,spin);
    const double innerLimit=std::max(rs*.03,rs*.5*(1-std::sqrt(1-spin*spin))*1.05);
    for(int step=0;step<512 && pendingObserverProper>0;++step){
        const double radius=KerrSpacetime::Radius(kerrObserver.state.position,rs,spin);
        if(radius<=horizon)horizonReached=true;
        if(radius<=innerLimit){
            observerGravity=false;pendingObserverProper=0;
            simulationPlaying=false;
            ShowStatusToast("Modeled interior limit reached",true);break;
        }
        const double interval=std::min(pendingObserverProper,std::max(.001,.002*radius/Astronomy::lightSpeedAu));
        KerrSpacetime::AdvanceObserver(kerrObserver,rs,spin,interval,Astronomy::lightSpeedAu);
        pendingObserverProper-=interval;localTime+=interval;
    }
    cameraPos=fromDisk*kerrObserver.state.position;
    if(KerrSpacetime::Radius(kerrObserver.state.position,rs,spin)<=horizon)horizonReached=true;
    observer.position=cameraPos;
    observer.velocity=fromDisk*KerrSpacetime::ObserverCoordinateVelocity(kerrObserver,rs,spin)*Astronomy::lightSpeedAu;
    cameraClockRate=1/KerrSpacetime::ObserverTimeComponent(kerrObserver,rs,spin);
    if(simulationPlaying)simulationTime=previousTime+kerrObserver.coordinateSeconds-oldCoordinate;
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
    if (visionProfile>0 && height>0 && width>scenePanel.Width()) {
        const float nextAspect=static_cast<float>(width-scenePanel.Width())/height;
        const float zoom=std::tan(glm::radians(fieldOfView*.5f))
            /std::tan(glm::radians(CameraOptics::FieldOfView(visionProfile,lensAspect)*.5f));
        lensAspect=nextAspect;
        fieldOfView=glm::degrees(2*std::atan(zoom*std::tan(glm::radians(CameraOptics::FieldOfView(visionProfile,lensAspect)*.5f))));
    }
}

void WindowManager::OpenMainMenu() {
    objectEditor.Hide();
    interiorVehicle=0;
    simulationPlaying=false;
    SetMouseCaptured(false);
    scenePanel.SetVisible(false);
#ifdef _WIN32
    SetMenu(glfwGetWin32Window(window),nullptr);
    DrawMenuBar(glfwGetWin32Window(window));
#endif
    mainMenu.Show(AppMenu::Page::Home);
    SetApplicationTitle(window,"Render-Engine | Main menu");
}

void WindowManager::PrepareCurrentBrowser() {
    if(quantumMode)return;
    if(inStarSystem) {
        pendingSystemTarget=activeSystemTarget;
        scenePanel.SetCurrentSystemName(starSystem.Host().name);
        std::vector<std::string> names={starSystem.Host().name};
        for(const auto& body:starSystem.Planets())names.push_back(body.name);
        scenePanel.SetPlanets(names);
    }else{
        const int previous=selectedTarget;
        SelectCatalogue(0);selectedTarget=previous;
        scenePanel.SetCurrentSystemName("Gargantua system");
    }
    RefreshPanel();
}

void WindowManager::StartSolarSystem() {
    interiorVehicle=0;
    sceneCacheValid=false;idleFrame=false;
    trajectories=true;
    if (!starSystemReady) return;
    inStarSystem=true; quantumMode=false; showBlackHole=false;
    simulationPlaying=false; simulationTime=localTime=0; timeRateIndex=0;
    pendingSystemTarget=activeSystemTarget=-2;
    systemOriginAu=glm::dvec3(0);
    starSystem.LoadSolarSystem(); previewSystem.LoadSolarSystem();
    mainMenu.Hide(); scenePanel.SetVisible(true);
#ifdef _WIN32
    SetMenu(glfwGetWin32Window(window),static_cast<HMENU>(nativeMenu));
    DrawMenuBar(glfwGetWin32Window(window));
#endif
    scenePanel.SetTitle("Star / object browser");
    std::vector<std::pair<int,std::string>> catalogue={{-2,"Sun / Solar System"},{0,"Gargantua / Black hole"}};
    for (int id=4;id<7010;++id) {
        if (id==9) continue;
        catalogue.push_back({id,blackHole.ResolveTarget(id,simulationTime).name});
    }
    scenePanel.SetCatalogue(std::move(catalogue));
    std::vector<std::string> bodies={"Sun"};
    for (const auto& body:starSystem.Planets()) bodies.push_back(body.name);
    scenePanel.SetPlanets(bodies);
    scenePanel.SetCurrentSystemName(starSystem.Host().name);
    scenePanel.ShowBrowser(true);
    starSystem.Overview(cameraFront,cameraUp);
    fieldOfView=45; SyncCameraAngles(); ResizeInterface(); RefreshPanel(); UpdateTitle();
#ifdef _WIN32
    SetFocus(glfwGetWin32Window(window));
#endif
}

void WindowManager::StartGargantua() {
    sceneCacheValid=false;idleFrame=false;
    if (!blackHoleReady) return;
    inStarSystem=quantumMode=false; showBlackHole=true; pendingSystemTarget=0;
    simulationPlaying=false; timeRateIndex=0; simulationTime=localTime=0;
    blackHole.ResetSpacecraft(0);
    trajectories=false;
    mainMenu.Hide(); scenePanel.SetVisible(true); scenePanel.SetTitle("Star / object browser");
#ifdef _WIN32
    SetMenu(glfwGetWin32Window(window),static_cast<HMENU>(nativeMenu));
    DrawMenuBar(glfwGetWin32Window(window));
#endif
    std::vector<std::pair<int,std::string>> catalogue={{-2,"Sun / Solar System"},{0,"Gargantua / Black hole"}};
    for (int id=4;id<7010;++id) if (id!=9) catalogue.push_back({id,blackHole.ResolveTarget(id,simulationTime).name});
    scenePanel.SetCatalogue(std::move(catalogue));
    SelectCatalogue(0);
    scenePanel.SetCurrentSystemName("Gargantua system");scenePanel.ShowBrowser(true);
    ResetView(); ResizeInterface(); UpdateTitle();
#ifdef _WIN32
    SetFocus(glfwGetWin32Window(window));
#endif
}

void WindowManager::StartQuantum(int index) {
    interiorVehicle=0;
    sceneCacheValid=false;idleFrame=false;
    if (!quantumReady) return;
    inStarSystem=false; showBlackHole=false; quantumMode=true;
    simulationPlaying=false; quantum.Select(index);
    landed=observerGravity=horizonReached=followSelected=followPlanet=followSpacecraft=false;
    cameraPos={0,0,quantum.ViewDistance()};cameraFront={0,0,-1};cameraUp={0,1,0};
    cameraSpeed=4;fieldOfView=45;SyncCameraAngles();
    SetMouseCaptured(false); mainMenu.Hide(); scenePanel.SetVisible(true);
#ifdef _WIN32
    SetMenu(glfwGetWin32Window(window),static_cast<HMENU>(nativeMenu));
    DrawMenuBar(glfwGetWin32Window(window));
#endif
    scenePanel.SetTitle("Quantum state browser");
    std::vector<std::pair<int,std::string>> catalogue;
    const auto& examples=QuantumStates::Examples();
    for (int i=0;i<static_cast<int>(examples.size());++i) catalogue.push_back({i,examples[i].name});
    scenePanel.SetCatalogue(std::move(catalogue)); scenePanel.SetPlanets({});
    scenePanel.SetCurrentSystemName("Quantum states");scenePanel.ShowBrowser(false);
    ResizeInterface(); RefreshPanel(); UpdateTitle();
#ifdef _WIN32
    SetFocus(glfwGetWin32Window(window));
#endif
}

void WindowManager::SelectCatalogue(int id) {
    if (quantumMode) {
        quantum.Select(id); simulationPlaying=false;
        cameraPos={0,0,quantum.ViewDistance()};cameraFront={0,0,-1};cameraUp={0,1,0};
        RefreshPanel(); return;
    }
    pendingSystemTarget=id;
    if (id==0) {
        scenePanel.SetPlanets({"Gargantua","Miller's planet","Mann's planet","Edmunds' planet","Orbiting star 1","Orbiting star 2","Orbiting star 3","Orbiting star 4","Orbiting star 5","Spacecraft"});
        selectedTarget=0;
    } else {
        if (id==-2) previewSystem.LoadSolarSystem();
        else previewSystem.Generate(id,blackHole.ResolveTarget(id,simulationTime).name);
        std::vector<std::string> names={previewSystem.Host().name};
        for (const auto& planet:previewSystem.Planets()) names.push_back(planet.name);
        scenePanel.SetPlanets(names);
        selectedTarget=id>=0?id:-1;
    }
    RefreshPanel();
}

void WindowManager::EnterSelectedSystem(bool overview) {
    if (!starSystemReady || pendingSystemTarget==0) return;
    interiorVehicle=0;
    if (!inStarSystem || activeSystemTarget!=pendingSystemTarget) {
        if (pendingSystemTarget==-2) starSystem.LoadSolarSystem();
        else starSystem.Generate(pendingSystemTarget,previewSystem.Host().name);
        starSystem.SelectPlanet(previewSystem.Selected());
        activeSystemTarget=pendingSystemTarget;
        systemOriginAu=activeSystemTarget==-2?glm::dvec3(0):blackHole.ResolveTarget(activeSystemTarget,simulationTime).position;
        inStarSystem=true; showBlackHole=false; observerGravity=landed=false;
    }
    if (overview) starSystem.Overview(cameraFront,cameraUp);
    else starSystem.GoToSelected(simulationTime,cameraFront,cameraUp);
    SyncCameraAngles(); RefreshPanel(); UpdateTitle();
}

void WindowManager::UpdateNearbySystem(){
    if(!blackHoleReady || quantumMode || interiorVehicle || (!inStarSystem && !showBlackHole))return;
    if(inStarSystem && (starSystem.IsLanded() || starSystem.IsFollowing()))return;
    const auto worldCamera=inStarSystem?systemOriginAu+starSystem.camera:cameraPos;
    const auto nearby=blackHole.NearbyStars(worldCamera);
    int nearest=-1;double distance=3;
    for(int id:nearby){
        const double d=glm::length(blackHole.ResolveTarget(id,simulationTime).position-worldCamera);
        if(d<distance){nearest=id;distance=d;}
    }
    if(nearest<0 || (inStarSystem && nearest==activeSystemTarget))return;
    const auto direction=cameraFront,up=cameraUp;
    const double flightSpeed=inStarSystem?starSystem.speedAu:cameraSpeed;
    SelectCatalogue(nearest);EnterSelectedSystem(false);
    starSystem.camera=worldCamera-systemOriginAu;starSystem.speedAu=flightSpeed;
    starSystem.Update(simulationTime,cameraFront,cameraUp);
    cameraFront=direction;cameraUp=up;
    sceneCacheValid=false;SyncCameraAngles();
}

void WindowManager::RefreshObjectEditor(){
    if(!objectEditor.NativeHandle())return;
    std::string name="No object selected";
    const bool editable=blackHoleReady && !quantumMode && pendingSystemTarget==0 && selectedTarget==0;
    if(quantumMode)name="Quantum state";
    else if(pendingSystemTarget!=0){
        const auto& system=inStarSystem && activeSystemTarget==pendingSystemTarget?starSystem:previewSystem;
        const int body=system.Selected();
        name=body>=0?system.Planets()[body].name:system.Host().name;
    }else if(selectedTarget>=0)name=selectedTarget==0?"Gargantua":blackHole.ResolveTarget(selectedTarget,simulationTime).name;
    objectEditor.SetObject(name,editable,blackHole.Spin(),BlackHole::HorizonRadius());
}

void WindowManager::RefreshPanel() {
    RefreshObjectEditor();
    if (mainMenu.Visible()) return;
    if (quantumMode) scenePanel.SetInformation(quantum.Information());
    else if (pendingSystemTarget!=0) {
        const auto& system=inStarSystem && activeSystemTarget==pendingSystemTarget?starSystem:previewSystem;
        auto info=system.Information(simulationTime);
        if (system.IsSolarSystem()) info+="\n1:1 physical radii and distances\nJ2000 frozen Keplerian elements\n4096 representative belt/KBO markers\nPlanet surfaces are procedural illustrations.\n";
        scenePanel.SetInformation(info);
    } else {
        std::string info="GARGANTUA (inspired by Interstellar)\nSupermassive black hole\nMass: 100 million solar masses\nSchwarzschild reference radius: 1.974 AU\nCraft dynamics: Schwarzschild approximation\nAge: fictional / unspecified\n\n";
        if (selectedTarget>=0) info+="Selected: "+blackHole.ResolveTarget(selectedTarget,simulationTime).name+"\n";
        std::ostringstream rayModel;
        rayModel<<"Ray model: "<<(std::abs(blackHole.Spin())>.0001?"GPU Kerr":"GPU Schwarzschild")
            <<"\nDimensionless spin: "<<blackHole.Spin()<<"\nMaximum ray steps: "<<blackHole.RayBudget()<<"\n";
        info+=rayModel.str();
        if(kerrObserverValid && (observerGravity || horizonReached))
            info+="Free fall uses rotating black-hole gravity.\nThe outer horizon is crossed continuously.\nThe inner-horizon region is outside this model.\n";
        if(selectedTarget>=1 && selectedTarget<=3){
            const auto& planet=InterstellarWorlds::Get(selectedTarget-1);
            std::ostringstream details;
            details<<planet.classification<<"\nRadius: "<<planet.radiusEarth<<" Earth\nMass: "<<planet.massEarth
                <<" Earth\nOrbit: "<<planet.orbit.semiMajorAxis<<" AU; eccentricity "<<planet.orbit.eccentricity
                <<"\nOrbital elements and masses are illustrative model choices.\n";
            info+=details.str();
        }
        std::ostringstream clock;
        clock<<std::setprecision(12)<<"\nCamera clock / distant clock: "<<cameraClockRate<<"\n";
        info+=clock.str();
        if(selectedTarget==9){
            const double r=glm::length(blackHole.SpacecraftPosition());
            const double speedUnits=Astronomy::metresPerAu/1000;
            std::ostringstream telemetry;
            telemetry<<blackHole.SpacecraftStatus()<<"\nSpeed: "<<blackHole.SpacecraftSpeedAu()*speedUnits
                <<" km/s\nLocal escape speed: "<<std::sqrt(2*BlackHole::GravityParameter()/r)*speedUnits
                <<" km/s\nFuel: "<<blackHole.SpacecraftFuelSeconds()/3600<<" engine hours\n"
                <<"Endurance proper time: "<<blackHole.SpacecraftProperTime()/3600<<" hours\n"
                <<"Rangers: "<<(blackHole.RangersDetached()?"detached / coasting":"docked")<<"\n"
                <<"Orbital energy: "<<(blackHole.SpacecraftEnergy()>0?"positive / unbound":"negative / bound")<<"\n";
            info+=telemetry.str();
        }
        if(landed)info+="\nHigh-detail surface: 100 m around camera\nCoarser terrain beyond 100 m\n";
        info+="\nMann: frozen world, 50-650 AU orbit\nEdmunds: rocky world, 1200 AU semimajor axis\nSpacecraft: departure near Mann, gravity-driven inbound coast and finite-fuel periapsis burn\nUse > for day/month/year rates to follow the long transfer.\nEarth and Saturn are in the Solar System preset.\n";
        info+="\nPhysical AU distances and body radii\nMiller radius: 6371 km (Earth scale)\nMiller orbit: 23.7 AU, 12 horizons\nWider illustrative orbit; vehicle dynamics use Schwarzschild.\nObserver envelope: Hubble 13.2 x 4.2 m\n\nQ/E  Roll\nR/F  Up/down\nShift+F / T  Follow position\nShift+R  Follow rotation\nShift+L  Look at planet\nF8  Vehicle interior / free flight\nSpace  Play / pause\nF2  Orbit paths\nF3  Information\nF4  All star systems\nShift+F4  Current system only\nHome  Overview\nG  Go to selection\nL  Land / lift off\nF6  Photon samples\nF10  Main menu\nWheel  Flight speed +/-15%\nShift+RMB  Drag to zoom\nMMB  Reset zoom\n";
        scenePanel.SetInformation(info);
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
    else if (id==3014) StartQuantum(mainMenu.SelectedQuantum());
    else if (id==3020 && notification==LBN_SELCHANGE)
        mainMenu.SetQuantumInformation(QuantumStates::Examples()[std::max(0,mainMenu.SelectedQuantum())].explanation);
    else if (id==3020 && notification==LBN_DBLCLK) StartQuantum(mainMenu.SelectedQuantum());
    else if (id==2100 && notification==EN_CHANGE) scenePanel.Filter();
    else if (id==2101 && (notification==LBN_SELCHANGE || notification==LBN_DBLCLK)) {
        const int choice=scenePanel.SelectedStar();
        if (choice!=-1) SelectCatalogue(choice);
        if (notification==LBN_DBLCLK && !quantumMode) HandleInterfaceCommand(2104,0);
    } else if (id==2103 && (notification==LBN_SELCHANGE || notification==LBN_DBLCLK)) {
        const int body=scenePanel.SelectedPlanet();
        if (body>=0) {
            if (pendingSystemTarget==0) selectedTarget=body;
            else {
                previewSystem.SelectPlanet(body-1);
                if (inStarSystem && activeSystemTarget==pendingSystemTarget) starSystem.SelectPlanet(body-1);
            }
            RefreshPanel();
        }
        if (notification==LBN_DBLCLK) HandleInterfaceCommand(2104,0);
    } else if (id==2104) {
        KeyCallback(window,GLFW_KEY_G,0,GLFW_PRESS,0);
        SetFocus(glfwGetWin32Window(window));
    } else if (id<2100) KeyCallback(window,id,0,GLFW_PRESS,0);
#endif
}

void WindowManager::UpdateTitle() {
    if (mainMenu.Visible()) return;
    if (quantumMode) {
        std::ostringstream title;
        title<<"Quantum Simulation | "<<QuantumStates::Examples()[quantum.Selected()].name
            <<(simulationPlaying?" | Playing":" | Paused")<<" | t="<<std::fixed<<std::setprecision(3)
            <<quantum.Time()<<" | "<<quantumRates[quantumRateIndex]<<"x | Space play/pause | R restart | F10 menu";
        SetApplicationTitle(window,title.str()); return;
    }
    std::ostringstream title;
    const auto seconds = static_cast<std::uint64_t>(localTime);
    const auto worldSeconds = static_cast<std::uint64_t>(simulationTime);
    title << "Render-Engine | " << (inStarSystem ? starSystem.Host().name : showBlackHole ? "Gargantua-inspired" : "Model")
          << " | " << CameraOptics::Name(visionProfile)
          << (hdrMode ? " | HDR" : optics.IsAutoExposure()?" | Pure exposure (auto)":" | Pure exposure (manual)")
          << ((!hdrMode && optics.IsAutoExposure())?" | EV bias ":" | EV ") << std::showpos << exposureEv << std::noshowpos
          << (followPlanet ? " | Miller flyby" : "")
          << (followSpacecraft ? " | Spacecraft" : "")
          << (followSelected ? (followSelectedRotation?" | Following rotation":" | Following position") : "")
          << (landed ? " | Surface free-cam" : "")
          << (observerGravity ? " | Falling free-cam" : "")
          << (horizonReached ? (KerrSpacetime::Radius(glm::transpose(InterstellarWorlds::Orientation())*cameraPos,BlackHole::HorizonRadius(),blackHole.Spin()) < KerrSpacetime::Horizon(BlackHole::HorizonRadius(),blackHole.Spin())
              ? " | Inside event horizon" : " | Horizon crossed") : "")
          << (photonView ? " | Photon samples: fly aside to inspect" : "")
          << (showBlackHole && selectedTarget >= 0 ? " | Selected " + blackHole.ResolveTarget(selectedTarget, simulationTime).name + " [G]" : "")
          << (simulationPlaying ? " | Playing" : " | Paused")
          << " | Local " << std::setfill('0') << std::setw(2) << seconds / 3600
          << ':' << std::setw(2) << seconds / 60 % 60 << ':' << std::setw(2) << seconds % 60
          << " | World " << std::setw(2) << worldSeconds / 3600
          << ':' << std::setw(2) << worldSeconds / 60 % 60 << ':' << std::setw(2) << worldSeconds % 60
          << std::setfill(' ') << " | " << timeRates[timeRateIndex] << "x time"
          << " | Speed " << _UTILITY_::SpeedLabel(inStarSystem?starSystem.speedAu:cameraSpeed,inStarSystem || showBlackHole)
          << (showBlackHole ? std::string(" | Craft ") + blackHole.SpacecraftStatus() : "")
          << (showBlackHole ? " | Light age " + std::to_string(
              blackHole.SpacecraftLightAge(cameraPos, simulationTime)) + " s" : "")
          << " | Wheel speed | Shift+RMB zoom | MMB reset | Esc cursor";
    SetApplicationTitle(window, title.str());
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
                manager.ShowZoomToast(true);
            }
        }
    }
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        manager.fieldOfView = CameraOptics::FieldOfView(manager.visionProfile, manager.lensAspect);
        if(manager.interiorVehicle)manager.fieldOfView=75;
        manager.ShowZoomToast(true);
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
        const auto view = glm::lookAt(glm::vec3(0), cameraFront, cameraUp);
        const auto screen = CameraOptics::MapScreen(manager.visionProfile,
            glm::vec2(2.0f * static_cast<float>(x) / width - 1.0f, 1.0f - 2.0f * static_cast<float>(y) / height),
            static_cast<float>(width) / height, manager.fieldOfView);
        const auto point = glm::inverse(projection * view) * glm::vec4(screen, 1.0f, 1.0f);
        const auto direction = glm::normalize(glm::vec3(point));
        if (manager.inStarSystem) manager.starSystem.Pick(direction,manager.simulationTime);
        else {
            manager.selectedTarget = manager.blackHole.PickTarget(cameraPos, direction, manager.simulationTime);
            if (manager.selectedTarget>=0) {
                if (manager.selectedTarget>=10 || (manager.selectedTarget>=4 && manager.selectedTarget<=8))
                    manager.SelectCatalogue(manager.selectedTarget);
                else manager.pendingSystemTarget=0;
            }
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
        manager.ShowZoomToast();
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

    CameraMotion::Look(cameraFront,cameraUp,glm::radians(-xoffset),glm::radians(yoffset),
        manager.interiorVehicle || manager.landed || (manager.inStarSystem && manager.starSystem.IsLanded()));
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
///*
void WindowManager::DropCallback(GLFWwindow* window, int count, const char** paths)
{
    for (int i = 0; i < count; ++i)
    {
        if (!paths || count <= 0)
        {
            std::cerr << "[Drop] Ignored empty path\n";
            return;
        }
        
        std::string path(paths[i]);
        std::cout << "[Drop] File dropped: " << path << std::endl;

        glm::vec3 center; //Object center
        std::vector<float> modelVertices; //Store Loaded Vertices
        
        if (_UTILITY_::EndsWith(path, ".obj"))
        {
            if (ObjectLoader::LoadOBJ(path, modelVertices, center))
            {
                cout << "[Drop] OBJ loaded successfully" << endl;

                currentModelVertices = std::move(modelVertices);
                static_cast<WindowManager*>(glfwGetWindowUserPointer(window))->showingDefaultCube = false;
                auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
                manager.showBlackHole = false;
                manager.inStarSystem=manager.quantumMode=false;
                manager.mainMenu.Hide(); manager.scenePanel.SetVisible(false);
                manager.ResetView();
                manager.UpdateTitle();

                glBindBuffer(GL_ARRAY_BUFFER, VBO);
                glBufferData(GL_ARRAY_BUFFER, currentModelVertices.size() * sizeof(float), currentModelVertices.data(), GL_STATIC_DRAW);
                glBindBuffer(GL_ARRAY_BUFFER, 0);

                std::cout << "[Drop] Updated model buffer with " << currentModelVertices.size() / 6 << " vertices." << std::endl;
            }
            else
            {
                cout << "[Drop] OBJ failed to load" << endl;
            }
        }
        else if (_UTILITY_::EndsWith(path, ".fbx"))
        {
            if (ObjectLoader::LoadFBX(path, modelVertices, center))
            {
                cout << "[Drop] FBX loaded successfully" << endl;
                
                currentModelVertices = std::move(modelVertices);
                static_cast<WindowManager*>(glfwGetWindowUserPointer(window))->showingDefaultCube = false;
                auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
                manager.showBlackHole = false;
                manager.inStarSystem=manager.quantumMode=false;
                manager.mainMenu.Hide(); manager.scenePanel.SetVisible(false);
                manager.ResetView();
                manager.UpdateTitle();

                glBindBuffer(GL_ARRAY_BUFFER, VBO);
                glBufferData(GL_ARRAY_BUFFER, currentModelVertices.size() * sizeof(float), currentModelVertices.data(), GL_STATIC_DRAW);
                glBindBuffer(GL_ARRAY_BUFFER, 0);

                std::cout << "[Drop] Updated model buffer with " << currentModelVertices.size() / 6 << " vertices." << std::endl;
            }
            else
            {
                cout << "[Drop] FBX failed to load" << endl;
            }
        }
        else
        {
            std::cerr << "[Drop] Unsupported file format: " << path << std::endl;
        }
    }
}
//*/
void WindowManager::ProcessCamera(GLFWwindow* window, const float& deltaTime) {
    if (!isFocused)
        return;
    auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    float speed = manager.cameraSpeed * deltaTime;
    if(manager.interiorVehicle)return; // Seated look-around keeps the cockpit's up axis.
    const bool grounded=manager.landed || (manager.inStarSystem && manager.starSystem.IsLanded());
    if (!grounded) {
        const float bank=(glfwGetKey(window,GLFW_KEY_E)==GLFW_PRESS?1.0f:0.0f)
            -(glfwGetKey(window,GLFW_KEY_Q)==GLFW_PRESS?1.0f:0.0f);
        CameraMotion::Roll(cameraFront,cameraUp,glm::radians(60.0f)*bank*deltaTime);
    }
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
        const bool modifiers=glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS
            || glfwGetKey(window,GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS
            || glfwGetKey(window,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS
            || glfwGetKey(window,GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS;
        if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS && !modifiers)
            cameraPos -= cameraUp * speed;
        if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS && !modifiers)
            cameraPos += cameraUp * speed;
    }

    // The redshift observer model uses static exterior Schwarzschild observers.
    if (manager.landed) {
        const auto center = BlackHole::PlanetPosition(manager.simulationTime, manager.landedPlanet);
        manager.AlignCameraUp(cameraPos - center);
        cameraPos = center + glm::dvec3(cameraUp) * (manager.blackHole.PlanetSurfaceRadius(cameraUp, manager.simulationTime, manager.landedPlanet) + Astronomy::observerClearanceAu);
    } else if (manager.showBlackHole) {
        const int planet = BlackHole::NearestPlanet(cameraPos, manager.simulationTime);
        const auto planetCenter = BlackHole::PlanetPosition(manager.simulationTime, planet);
        const auto offset = cameraPos - planetCenter;
        const double distance = glm::length(offset);
        const auto radial = distance > 1e-14 ? offset / distance : glm::dvec3(0, 0, 1);
        const double surface = manager.blackHole.PlanetSurfaceRadius(glm::vec3(radial), manager.simulationTime, planet) + Astronomy::observerClearanceAu;
        if (distance < surface)
            cameraPos = planetCenter + radial * surface;
        const float minimumRadius = manager.observerGravity || manager.horizonReached
            ? BlackHole::HorizonRadius() * 0.005f : BlackHole::HorizonRadius()*1.015f;
        if (glm::length(cameraPos) < minimumRadius)
            cameraPos = glm::normalize(cameraPos + glm::dvec3(0,0,1e-12)) * static_cast<double>(minimumRadius);
    }

    static bool togglePressed = false;
    if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS && !togglePressed) {
        showWireframe = !showWireframe;
        togglePressed = true;
        glPolygonMode(GL_FRONT_AND_BACK, showWireframe ? GL_LINE : GL_FILL);
        shaderProgram = showWireframe ? flatShader : phongShader;
    }
    if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_RELEASE) {
        togglePressed = false;
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
    if (starSystem || showBlackHole) {
        const double metres=static_cast<double>(speed)*Astronomy::metresPerAu;
        if(metres<1000)text<<std::fixed<<std::setprecision(2)<<metres<<" m/s";
        else if(metres<1e8)text<<std::fixed<<std::setprecision(1)<<metres/1000<<" km/s";
        else text<<std::fixed<<std::setprecision(4)<<speed<<" AU/s";
    } else {
        if (std::abs(speed - std::round(speed)) < 0.001f)
            text << std::fixed << std::setprecision(0) << speed << "x";
        else if (speed >= 1.0f)
            text << std::fixed << std::setprecision(1) << speed << "x";
        else
            text << std::fixed << std::setprecision(2) << speed << "x";
    }
    ShowStatusToast(text.str(),false);
}

void WindowManager::ShowStatusToast(const std::string& text,bool small){
    speedToastUntil = glfwGetTime() + (small?1.4:1.0);
#ifdef _WIN32
    if (!overlayProgram || !overlayTexture)
        return;
    int dpi = 96;
    if (window)
        dpi = static_cast<int>(GetDpiForWindow(glfwGetWin32Window(window)));
    int width = 0, height = 0;
    std::vector<unsigned char> pixels;
    if (!BuildToastTexture(text, dpi, width, height, pixels,small?15:30))
        return;
    glBindTexture(GL_TEXTURE_2D, overlayTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    overlayWidth = width;
    overlayHeight = height;
#else
    (void)text;
    (void)small;
#endif
}

void WindowManager::ShowZoomToast(bool force){
    const double now=glfwGetTime();
    if(!force && now<nextZoomToast)return;
    nextZoomToast=now+.1;
    const double base=interiorVehicle?75:CameraOptics::FieldOfView(visionProfile,lensAspect);
    const double magnification=std::tan(glm::radians(base)*.5)/std::tan(glm::radians(static_cast<double>(fieldOfView))*.5);
    std::ostringstream message;
    message<<"Zoom "<<std::fixed<<std::setprecision(2)<<magnification<<"x  |  "<<std::setprecision(1)<<fieldOfView<<" deg";
    ShowStatusToast(message.str());
    speedToastUntil=now+1.0;
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
    glfwPollEvents();
    if (isClosed()) return;
    int fullWidth,fullHeight,logicalWidth,logicalHeight;
    glfwGetFramebufferSize(window,&fullWidth,&fullHeight);
    glfwGetWindowSize(window,&logicalWidth,&logicalHeight);
    if (fullWidth<=0 || fullHeight<=0 || logicalWidth<=0) return;
    const int sceneWidth=fullWidth-scenePanel.Width()*fullWidth/logicalWidth;
    glViewport(0,0,glm::max(1,sceneWidth),fullHeight);
    if (mainMenu.Visible()) {
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glClearColor(.005f,.01f,.02f,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        PresentFrame(window); return;
    }
    if (quantumMode) {
        ProcessCamera(window,dT);
        idleFrame=false;
        if (simulationPlaying) quantum.Advance(glm::min(static_cast<double>(dT),.1)*quantumRates[quantumRateIndex]);
        titleUpdateElapsed+=dT;
        if (titleUpdateElapsed>=.2f) { RefreshPanel(); UpdateTitle(); titleUpdateElapsed=0; }
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        glClearColor(0.0f,0.0f,0.0f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        quantum.Render(sceneWidth,fullHeight,fieldOfView,cameraPos,cameraFront,cameraUp);
        PresentFrame(window); return;
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
    if (blackHoleReady && showBlackHole && !inStarSystem && (glm::length(cameraPos)<20000 || interiorVehicle || followSpacecraft))
        blackHole.Update(simulationTime);
    if(interiorVehicle){
        const auto pose=blackHole.CockpitPose(interiorVehicle,simulationTime);
        cameraFront=glm::normalize(glm::vec3(pose.rotation*glm::transpose(lastCabinRotation)*glm::dvec3(cameraFront)));
        cameraUp=glm::vec3(pose.rotation[1]);cameraPos=pose.position;lastCabinRotation=pose.rotation;
    }
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
        if (followSelectedRotation) {
            const auto rotation=glm::dmat3(blackHole.TargetRotation(followingTarget,simulationTime));
            auto precisePosition=glm::dvec3(cameraPos);
            CameraMotion::Carry(precisePosition,cameraFront,cameraUp,glm::dvec3(lastSelectedPosition),
                glm::dvec3(position),rotation*glm::transpose(lastSelectedRotation));
            cameraPos=precisePosition;
            lastSelectedRotation=rotation;
        } else cameraPos += position - lastSelectedPosition;
        lastSelectedPosition = position;
    }
    ProcessCamera(window, dT);
    UpdateNearbySystem();
    if(!falling){
        if(inStarSystem)cameraClockRate=starSystem.ObserverClockRate(simulationTime);
        else if(showBlackHole){
            glm::dvec3 velocity(0);
            if(interiorVehicle)velocity=blackHole.CockpitPose(interiorVehicle,simulationTime).velocity;
            else if(landed){
                velocity=(BlackHole::PlanetPosition(simulationTime+.5,landedPlanet)-BlackHole::PlanetPosition(simulationTime-.5,landedPlanet));
                const auto local=glm::transpose(glm::dmat3(BlackHole::PlanetRotation(simulationTime,landedPlanet)))
                    *(cameraPos-BlackHole::PlanetPosition(simulationTime,landedPlanet));
                velocity+=(glm::dmat3(BlackHole::PlanetRotation(simulationTime+.5,landedPlanet))
                    -glm::dmat3(BlackHole::PlanetRotation(simulationTime-.5,landedPlanet)))*local;
            }else if(followSpacecraft || (followSelected && followingTarget==9)){
                // Close following shares the craft's physical coordinate velocity.
                velocity=blackHole.SpacecraftVelocity();
            }
            cameraClockRate=blackHole.ObserverClockRate(cameraPos,simulationTime,velocity);
        }else cameraClockRate=1;
        if(showBlackHole && kerrObserverValid && horizonReached && observerSpin==blackHole.Spin()
            && glm::length(cameraPos-InterstellarWorlds::Orientation()*kerrObserver.state.position)<1e-9)
            cameraClockRate=1/KerrSpacetime::ObserverTimeComponent(kerrObserver,BlackHole::HorizonRadius(),observerSpin);
        if(simulationTime>previousTime)localTime+=(simulationTime-previousTime)*cameraClockRate;
    }

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
    glm::mat4 view = showBlackHole?glm::lookAt(glm::vec3(0),cameraFront,cameraUp)
        :glm::lookAt(glm::vec3(cameraPos),glm::vec3(cameraPos)+cameraFront,cameraUp);
    const float aspectRatio = static_cast<float>(framebufferWidth) / framebufferHeight;
    glm::mat4 projection = glm::perspective(glm::radians(fieldOfView), aspectRatio,
        showBlackHole ? static_cast<float>(.1/Astronomy::metresPerAu) : .01f, showBlackHole?1e15f:2000.0f);

    const auto preciseCamera=inStarSystem?starSystem.camera:cameraPos;
    optics.SetStellarHint(glm::vec3(-1,-1,0));
    optics.SetBlackHoleHint(glm::vec3(-1,-1,0));
    if(showBlackHole && !inStarSystem){
        const auto offset=-cameraPos;const double along=glm::dot(offset,glm::dvec3(cameraFront));
        if(along>0){
            const auto right=glm::normalize(glm::cross(cameraFront,cameraUp)),up=glm::normalize(glm::cross(right,cameraFront));
            const double halfHeight=along*std::tan(glm::radians(static_cast<double>(fieldOfView))*.5);
            optics.SetBlackHoleHint(glm::vec3(.5+.5*glm::dot(offset,glm::dvec3(right))/(halfHeight*aspectRatio),
                .5+.5*glm::dot(offset,glm::dvec3(up))/halfHeight,BlackHole::HorizonRadius()*4.3/(halfHeight*2)));
        }
    }
    if(inStarSystem) {
        const auto offset=-starSystem.camera;
        const double along=glm::dot(offset,glm::dvec3(cameraFront));
        if(along>0) {
            const auto right=glm::normalize(glm::cross(cameraFront,cameraUp));
            const auto up=glm::normalize(glm::cross(right,cameraFront));
            const double halfHeight=along*std::tan(glm::radians(static_cast<double>(fieldOfView))*.5);
            optics.SetStellarHint(glm::vec3(.5+.5*glm::dot(offset,glm::dvec3(right))/(halfHeight*aspectRatio),
                .5+.5*glm::dot(offset,glm::dvec3(up))/halfHeight,
                starSystem.Host().radiusSolar*Astronomy::solarRadiusAu/(halfHeight*2)));
        }
    }
    const std::array<double,24> signature={preciseCamera.x,preciseCamera.y,preciseCamera.z,
        cameraFront.x,cameraFront.y,cameraFront.z,cameraUp.x,cameraUp.y,cameraUp.z,
        simulationTime,fieldOfView,static_cast<double>(sceneWidth),static_cast<double>(fullHeight),
        static_cast<double>(showBlackHole),static_cast<double>(inStarSystem),static_cast<double>(activeSystemTarget),
        static_cast<double>(selectedTarget),static_cast<double>(starSystem.Selected()),static_cast<double>(trajectories),
        static_cast<double>(photonView),static_cast<double>(observerGravity),static_cast<double>(horizonReached),blackHoleResolution,static_cast<double>(interiorVehicle)};
    idleFrame=sceneCacheValid && !simulationPlaying && !observerGravity && (showBlackHole || inStarSystem) && signature==lastSceneSignature;
    if(idleFrame) {
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,sceneWidth,fullHeight);
        optics.Render(visionProfile,fieldOfView,exposureEv,hdrMode,lensFlareStrength,true,dT);
        RenderSpeedToast();PresentFrame(window);return;
    }
    lastSceneSignature=signature;

    if(showBlackHole && !inStarSystem)
        glViewport(0,0,glm::max(1,static_cast<int>(sceneWidth*blackHoleResolution)),glm::max(1,static_cast<int>(fullHeight*blackHoleResolution)));
    optics.BeginFrame();
    glClearColor(0.0f,0.0f,0.0f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (inStarSystem && starSystemReady) {
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        if(blackHoleReady){
            const auto skyProjection=glm::perspective(glm::radians(fieldOfView),aspectRatio,.00001f,20000.f);
            const auto skyView=glm::lookAt(glm::vec3(0),cameraFront,cameraUp);
            blackHole.RenderBackground(skyProjection*skyView,systemOriginAu+starSystem.camera);
            if(photonView)blackHole.RenderBackground(skyProjection*skyView,systemOriginAu+starSystem.camera,true,simulationTime,systemOriginAu+starSystem.camera);
        }
        starSystem.Render(fieldOfView,aspectRatio,cameraFront,cameraUp,simulationTime,trajectories,photonView);
    } else if (showBlackHole && blackHoleReady) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        const bool physicalObserver=kerrObserverValid && observerSpin==blackHole.Spin() && (observerGravity || horizonReached) && !interiorVehicle
            && glm::length(cameraPos-InterstellarWorlds::Orientation()*kerrObserver.state.position)<1e-9;
        const auto physicalBeta=physicalObserver?InterstellarWorlds::Orientation()*KerrSpacetime::ObserverBeta(kerrObserver,BlackHole::HorizonRadius(),observerSpin):glm::dvec3(0);
        if(glm::length(cameraPos)<20000 || interiorVehicle)blackHole.Render(projection * view, cameraPos, simulationTime,
            interiorVehicle?glm::vec3(blackHole.CockpitPose(interiorVehicle,simulationTime).velocity)
                :observerGravity || horizonReached ? glm::vec3(observer.velocity) : glm::vec3(0.0f),trajectories,selectedTarget,interiorVehicle,glm::vec3(physicalBeta),physicalObserver);
        else blackHole.RenderBackground(projection*view,cameraPos);
        if(interiorVehicle)vehicleInterior.Render(projection*view,glm::mat3(lastCabinRotation),interiorVehicle>=2,
            static_cast<float>(blackHole.SpacecraftFuelSeconds()/InterstellarWorlds::FuelSeconds));
        glPolygonMode(GL_FRONT_AND_BACK, showWireframe ? GL_LINE : GL_FILL);
    } else {
        //Apply Lighting When Out of Wireframe Mode
        if (!showWireframe)
        {
            glUseProgram(phongShader);

            glUniformMatrix4fv(glGetUniformLocation(phongShader, "model"), 1, GL_FALSE, glm::value_ptr(model));
            glUniformMatrix4fv(glGetUniformLocation(phongShader, "view"), 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(glGetUniformLocation(phongShader, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

            glm::vec3 dynamicLight = glm::vec3(cameraPos) + glm::vec3(0, 0, -2);
            glUniform3f(glGetUniformLocation(phongShader, "lightPos"), dynamicLight.x, dynamicLight.y, dynamicLight.z);
            const auto viewPosition=glm::vec3(cameraPos);
            glUniform3fv(glGetUniformLocation(phongShader,"viewPos"),1,glm::value_ptr(viewPosition));
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

        // Choose draw count from loaded vertices
        GLsizei drawCount = currentModelVertices.empty() ? 36 : currentModelVertices.size() / 6;
        glDrawArrays(GL_TRIANGLES, 0, drawCount);

        glBindVertexArray(0);
    }
    
    glViewport(0,0,sceneWidth,fullHeight);
    optics.Render(visionProfile, fieldOfView, exposureEv, hdrMode,lensFlareStrength,false,dT);
    sceneCacheValid=true;
    RenderSpeedToast();
    glPolygonMode(GL_FRONT_AND_BACK, showWireframe ? GL_LINE : GL_FILL);
    PresentFrame(window);

    // Print camera transformations
    timeSinceLastLog += dT;

    if (timeSinceLastLog >= 1.0f)
    {
        std::cout << "Camera Pos: ("
                  << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ") "
                  << "| Front: ("
                  << cameraFront.x << ", " << cameraFront.y << ", " << cameraFront.z << ")"
                  << std::endl;

        timeSinceLastLog = 0.0f; // Reset timer
    }
}

void WindowManager::InitializeGLFW()
{
    glfwInit();
#ifdef RENDER_ENGINE_DX12
    glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif
}

void WindowManager::MakeNewWindow() {
    window = glfwCreateWindow(WIDTH, HEIGHT, "Render-Engine A0.1 | Playing", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
    }
#ifdef RENDER_ENGINE_DX12
    NativeDX12::InitializeSceneBridge(glfwGetWin32Window(window));
#else
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        cerr << "Failed to initialize GLAD\n";
    }
#endif

    glEnable(GL_DEPTH_TEST);

    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glfwSetWindowUserPointer(window, this);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetWindowFocusCallback(window, WindowFocusCallback);
    WindowFocusCallback(window, 1); // Manually focus the mouse in the window //
    glfwSetCursorPosCallback(window, MouseCallback); // Then read mouse input
    glfwSetDropCallback(window, DropCallback); // Then read dropped files

    flatShader = compileShaderProgram(flatVertexSrc, flatFragmentSrc);
    phongShader = compileShaderProgram(phongVertextSrc, phongFragmentSrc);
    blackHoleReady = blackHole.Initialize();
    if(!vehicleInterior.Initialize())blackHoleReady=false;
    opticsReady=optics.Initialize();
    InitializeOverlay();
    starSystemReady = starSystem.Initialize();
    quantumReady = quantum.Initialize();
    shaderProgram = flatShader;

    cout << "Flat Shader ID: " << flatShader << endl;

    currentModelVertices.assign(std::begin(vertices), std::end(vertices));
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    int success;
    char infoLog[512];
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cerr << "SHADER LINK ERROR:\n" << infoLog << endl;
    }

    glUseProgram(shaderProgram);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, currentModelVertices.size() * sizeof(float), currentModelVertices.data(), GL_STATIC_DRAW);

    // Positions
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Normals
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    std::cout << "Program ID: " << shaderProgram << endl;
    UpdateTitle();

#ifdef _WIN32
    const HWND nativeWindow = glfwGetWin32Window(window);
    const HMENU menu = CreateMenu();
    nativeMenu=menu;
    const HMENU sceneMenu=CreatePopupMenu(),timeMenu=CreatePopupMenu(),navigationMenu=CreatePopupMenu(),viewMenu=CreatePopupMenu();
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(sceneMenu),L"Scene");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(timeMenu),L"Time");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(navigationMenu),L"Navigation");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(viewMenu),L"View");
    AppendMenuW(sceneMenu,MF_STRING,3030,L"Main menu (F10)");
    AppendMenuW(sceneMenu,MF_STRING,700,L"Black hole / Model");
    AppendMenuW(sceneMenu,MF_STRING,701,L"Miller flyby");
    AppendMenuW(sceneMenu,MF_STRING,703,L"Restart experiment / craft (Ctrl+R)");
    AppendMenuW(sceneMenu,MF_STRING,GLFW_KEY_N,L"New terrain (N)");
    AppendMenuW(timeMenu,MF_STRING,GLFW_KEY_SPACE,L"Play / pause (Space)");
    AppendMenuW(timeMenu,MF_STRING,GLFW_KEY_COMMA,L"Slower time (<)");
    AppendMenuW(timeMenu,MF_STRING,GLFW_KEY_PERIOD,L"Faster time (>)");
    AppendMenuW(navigationMenu,MF_STRING,GLFW_KEY_HOME,L"Overview / leave cockpit (Home)");
    AppendMenuW(navigationMenu,MF_STRING,GLFW_KEY_G,L"Go to selection (G)");
    AppendMenuW(navigationMenu,MF_STRING,707,L"Look at planet (Shift+L)");
    AppendMenuW(navigationMenu,MF_STRING,705,L"Follow position (Shift+F / T)");
    AppendMenuW(navigationMenu,MF_STRING,704,L"Follow rotation (Shift+R)");
    AppendMenuW(navigationMenu,MF_STRING,GLFW_KEY_L,L"Land / lift off (L)");
    AppendMenuW(navigationMenu,MF_STRING,GLFW_KEY_H,L"Gravity (H)");
    AppendMenuW(navigationMenu,MF_STRING,GLFW_KEY_LEFT_BRACKET,L"Slower flight [");
    AppendMenuW(navigationMenu,MF_STRING,GLFW_KEY_RIGHT_BRACKET,L"Faster flight ]");
    AppendMenuW(navigationMenu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(navigationMenu,MF_STRING,702,L"Endurance exterior");
    AppendMenuW(navigationMenu,MF_STRING,708,L"Endurance interior");
    AppendMenuW(navigationMenu,MF_STRING,709,L"Ranger 1 interior");
    AppendMenuW(navigationMenu,MF_STRING,710,L"Ranger 2 interior");
    AppendMenuW(navigationMenu,MF_STRING,GLFW_KEY_F8,L"Cycle vehicle view (F8)");
    AppendMenuW(navigationMenu,MF_STRING,GLFW_KEY_B,L"Boost (B)");
    AppendMenuW(viewMenu,MF_STRING,GLFW_KEY_F2,L"Orbits (F2)");
    AppendMenuW(viewMenu,MF_STRING,GLFW_KEY_F3,L"Information (F3)");
    AppendMenuW(viewMenu,MF_STRING,GLFW_KEY_F4,L"Star systems (F4)");
    AppendMenuW(viewMenu,MF_STRING,706,L"Current system (Shift+F4)");
    AppendMenuW(viewMenu,MF_STRING,GLFW_KEY_F6,L"Photon view (F6)");
    AppendMenuW(viewMenu,MF_STRING,GLFW_KEY_F5,L"Object editor (F5)");
    const HMENU visionMenu = CreatePopupMenu();
    AppendMenuW(visionMenu, MF_STRING, 1000, L"Free camera");
    AppendMenuW(visionMenu, MF_STRING, 1001, L"Human vision (approx.)");
    AppendMenuW(visionMenu, MF_STRING, 1002, L"Canon EF 50mm f/1.2L (Lensfun)");
    AppendMenuW(visionMenu, MF_STRING, 1003, L"Nikkor 14-24mm at 14mm (Lensfun)");
    AppendMenuW(visionMenu, MF_STRING, 1004, L"ZEISS Otus 85mm (Lensfun)");
    AppendMenuW(visionMenu, MF_STRING, 1005, L"Ideal 200mm telephoto");
    AppendMenuW(visionMenu, MF_STRING, 1006, L"ZEISS Distagon 35mm f/2 (Lensfun)");
    AppendMenuW(visionMenu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(visionMenu,MF_STRING,1220,L"Flare off");
    AppendMenuW(visionMenu,MF_STRING,1221,L"Subtle flare - default");
    AppendMenuW(visionMenu,MF_STRING,1222,L"Standard flare");
    AppendMenuW(visionMenu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(visionMenu,MF_STRING,1223,L"Reference glare on - default");
    AppendMenuW(visionMenu,MF_STRING,1224,L"Reference glare off");
    AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(visionMenu), L"Vision / Lens");
    const HMENU exposureMenu = CreatePopupMenu();
    AppendMenuW(exposureMenu, MF_STRING, 1200, L"HDR tone mapping (F7)");
    AppendMenuW(exposureMenu, MF_STRING, 1201, L"Pure exposure / auto adaptation (F7)");
    AppendMenuW(exposureMenu,MF_STRING,1202,L"Auto exposure on - default (Pure mode)");
    AppendMenuW(exposureMenu,MF_STRING,1203,L"Auto exposure off / manual EV");
    AppendMenuW(exposureMenu,MF_STRING,1204,L"Black hole visibility assist on - default");
    AppendMenuW(exposureMenu,MF_STRING,1205,L"Black hole visibility assist off");
    AppendMenuW(exposureMenu, MF_SEPARATOR, 0, nullptr);
    const wchar_t* exposureNames[] = {L"-8 EV", L"-4 EV", L"-2 EV", L"0 EV", L"+2 EV", L"+4 EV", L"+8 EV", L"+12 EV", L"+16 EV"};
    for (int level = 0; level < 9; ++level)
        AppendMenuW(exposureMenu, MF_STRING, 1100 + level, exposureNames[level]);
    AppendMenuW(viewMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(exposureMenu), L"Exposure / HDR");
    const HMENU performance=CreatePopupMenu();
    AppendMenuW(performance,MF_STRING,1303,L"Cooler mode: 30 FPS / 50% resolution");
    AppendMenuW(performance,MF_SEPARATOR,0,nullptr);
    AppendMenuW(performance,MF_STRING,1300,L"30 FPS - default");
    AppendMenuW(performance,MF_STRING,1301,L"60 FPS");
    AppendMenuW(performance,MF_STRING,1302,L"120 FPS");
    AppendMenuW(performance,MF_SEPARATOR,0,nullptr);
    AppendMenuW(performance,MF_STRING,1310,L"Black hole 50% resolution - default");
    AppendMenuW(performance,MF_STRING,1311,L"Black hole 75% resolution");
    AppendMenuW(performance,MF_STRING,1312,L"Black hole 100% resolution");
    AppendMenuW(performance,MF_SEPARATOR,0,nullptr);
    AppendMenuW(performance,MF_STRING,1330,L"Black hole spin 0 - Schwarzschild");
    AppendMenuW(performance,MF_STRING,1331,L"Kerr spin 0.5");
    AppendMenuW(performance,MF_STRING,1332,L"Kerr spin 0.8 - default");
    AppendMenuW(performance,MF_STRING,1333,L"Kerr spin 0.98 - demanding");
    AppendMenuW(performance,MF_STRING,1340,L"Ray budget: 96 - faster / approximate");
    AppendMenuW(performance,MF_STRING,1341,L"Ray budget: 160 - default");
    AppendMenuW(performance,MF_STRING,1342,L"Ray budget: 256 - higher accuracy");
    AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(performance),L"Performance");
    const HMENU activity=CreatePopupMenu();
    AppendMenuW(activity,MF_STRING,1321,L"Stellar activity on - default");
    AppendMenuW(activity,MF_STRING,1320,L"Stellar activity off");
    AppendMenuW(activity,MF_SEPARATOR,0,nullptr);
    AppendMenuW(activity,MF_STRING,1322,L"Trigger eruption on visible stars");
    AppendMenuW(activity,MF_SEPARATOR,0,nullptr);
    AppendMenuW(activity,MF_STRING,1324,L"Warm stellar color - default (artistic)");
    AppendMenuW(activity,MF_STRING,1325,L"Natural stellar color (three-band spectrum)");
    AppendMenuW(viewMenu,MF_POPUP,reinterpret_cast<UINT_PTR>(activity),L"Stellar activity");
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
            if ((message==WM_CTLCOLORSTATIC || message==WM_CTLCOLOREDIT || message==WM_CTLCOLORLISTBOX) && manager) {
                const int id=GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
                return reinterpret_cast<LRESULT>(id>=3000?manager->mainMenu.ControlColor(reinterpret_cast<void*>(wParam)):
                    manager->scenePanel.ControlColor(reinterpret_cast<void*>(wParam)));
            }
            if (message==WM_GETMINMAXINFO) {
                auto* bounds=reinterpret_cast<MINMAXINFO*>(lParam);
                bounds->ptMinTrackSize={900,640}; return 0;
            }
            return CallWindowProcW(originalWindowProcedure, handle, message, wParam, lParam);
        })));
    SetMenu(nativeWindow, menu);
    DrawMenuBar(nativeWindow);
    scenePanel.Create(nativeWindow);
    scenePanel.SetVisible(false);
    std::vector<std::string> quantumNames;
    for (const auto& example:QuantumStates::Examples()) quantumNames.push_back(example.name);
    mainMenu.Create(nativeWindow,quantumNames);
    EnumChildWindows(nativeWindow,+[](HWND control,LPARAM)->BOOL {
        SetPropW(control,L"RenderEnginePreviousControlProc",reinterpret_cast<HANDLE>(
            SetWindowLongPtrW(control,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(InterfaceKeyProcedure))));
        return TRUE;
    },0);
    if(!objectEditor.Create(nativeWindow,[this](double spin){
        blackHole.SetSpin(spin);sceneCacheValid=false;RefreshPanel();UpdateTitle();
    }))std::cerr<<"Object editor could not be created\n";
    OpenMainMenu();
#endif
}

void WindowManager::ReleaseGraphics() {
    objectEditor.Release();
    vehicleInterior.Release();
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
    const HMENU menu = static_cast<HMENU>(nativeMenu);
    SetWindowLongPtrW(nativeWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWindowProcedure));
    RemovePropW(nativeWindow, L"RenderEngineWindow");
    SetMenu(nativeWindow, nullptr);
    DestroyMenu(menu);
#endif
}

bool WindowManager::isClosed() {
    return glfwWindowShouldClose(window);
}
