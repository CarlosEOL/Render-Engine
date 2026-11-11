#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include "glad/glad.h"
#include "WindowManager.h"
#include "ObjectLoader.h"
#include "Astronomy.h"
#ifdef _WIN32
#include <GLFW/glfw3native.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <cstdint>
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

    float timeSinceLastLog = 0.0f;
    constexpr double timeRates[] = {1.0, 60.0, 3600.0, 86400.0, 2592000.0, 31557600.0};
    constexpr double quantumRates[] = {.1, .25, 1, 2, 4};
#ifdef _WIN32
    WNDPROC originalWindowProcedure = nullptr;

    LRESULT CALLBACK InterfaceKeyProcedure(HWND control, UINT message, WPARAM key, LPARAM detail) {
        const auto previous = reinterpret_cast<WNDPROC>(GetPropW(control,L"RenderEnginePreviousControlProc"));
        if (message == WM_KEYDOWN) {
            const int id = GetDlgCtrlID(control);
            const HWND parent = GetParent(control);
            if (key == VK_TAB) {
                const HWND next = GetNextDlgTabItem(parent,control,GetKeyState(VK_SHIFT)<0);
                if (next) SetFocus(next);
                return 0;
            }
            if (key == VK_RETURN && (id==3020 || id==2101 || id==2103)) {
                SendMessageW(parent,WM_COMMAND,id==3020?3014:2104,0);
                return 0;
            }
            const bool launcher = id>=3000;
            const bool searching = id==2100;
            int command = 0;
            if (key>=VK_F1 && key<=VK_F10) command=GLFW_KEY_F1+static_cast<int>(key-VK_F1);
            if (key==VK_ESCAPE) command=GLFW_KEY_ESCAPE;
            if (!launcher && !searching) {
                if (key==VK_SPACE || key=='G' || key=='F' || key=='L' || key=='R') command=static_cast<int>(key);
                if (key==VK_OEM_COMMA) command=GLFW_KEY_COMMA;
                if (key==VK_OEM_PERIOD) command=GLFW_KEY_PERIOD;
            }
            if (command) { SendMessageW(parent,WM_COMMAND,command,0); return 0; }
        }
        return CallWindowProcW(previous,control,message,key,detail);
    }
#endif
}

namespace _UTILITY_
{
    bool EndsWith(const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() &&
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
}

void WindowManager::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    auto* manager = static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (manager) manager->ResizeInterface();
    if (manager && manager->visionProfile > 0 && width > 0 && height > 0) {
        const float zoom = std::tan(glm::radians(manager->fieldOfView * 0.5f))
            / std::tan(glm::radians(CameraOptics::FieldOfView(manager->visionProfile, manager->lensAspect) * 0.5f));
        manager->lensAspect = static_cast<float>(width) / height;
        manager->fieldOfView = glm::degrees(2.0f * std::atan(zoom
            * std::tan(glm::radians(CameraOptics::FieldOfView(manager->visionProfile, manager->lensAspect) * 0.5f))));
    }
}

void WindowManager::ScrollCallback(GLFWwindow* window, double, double yOffset) {
    auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    manager.fieldOfView = glm::clamp(manager.fieldOfView - 3.0f * static_cast<float>(yOffset),
                                    manager.visionProfile > 0 ? 2.0f : 15.0f, 90.0f);
}

void WindowManager::KeyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (action != GLFW_PRESS)
        return;
    auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (key == GLFW_KEY_F10 || key == 3030) { manager.OpenMainMenu(); return; }
    if (manager.mainMenu.Visible()) return;
    if (key == GLFW_KEY_F3) { manager.scenePanel.Toggle(); manager.ResizeInterface(); return; }
    if (key == GLFW_KEY_F2) { manager.trajectories=!manager.trajectories; return; }
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
        else { manager.showBlackHole=true; manager.ResetView(); }
        manager.SyncCameraAngles(); return;
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
        if (key==GLFW_KEY_L) manager.starSystem.ToggleLanding(manager.simulationTime,cameraFront,cameraUp);
        else if (key==GLFW_KEY_F) manager.starSystem.ToggleFollow(manager.simulationTime);
        else if (key==GLFW_KEY_HOME) manager.starSystem.Overview(cameraFront,cameraUp);
        else if (key==GLFW_KEY_LEFT_BRACKET) manager.starSystem.speedAu=glm::max(1e-12,manager.starSystem.speedAu*.5);
        else if (key==GLFW_KEY_RIGHT_BRACKET) manager.starSystem.speedAu=glm::min(1e6,manager.starSystem.speedAu*2);
        else if (key==GLFW_KEY_N) { /* Catalogued solar surfaces retain their seed. */ }
        else if (key==GLFW_KEY_H || key==GLFW_KEY_B || key==GLFW_KEY_R) return;
        else goto sharedControls;
        manager.SyncCameraAngles(); manager.RefreshPanel(); return;
    }
sharedControls:
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
        manager.showBlackHole = !manager.showBlackHole;
        manager.ResetView();
    } else if (key == 701 && manager.blackHoleReady) {
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
        const auto sun = glm::normalize(-manager.lastPlanetPosition);
        const auto side = glm::normalize(glm::cross(sun, glm::vec3(0, 1, 0)));
        const auto approach = glm::normalize(0.35f * sun + 0.94f * side);
        cameraPos = manager.lastPlanetPosition + 0.32f * approach;
        cameraFront = -approach;
        yaw = glm::degrees(std::atan2(cameraFront.z, cameraFront.x));
        pitch = glm::degrees(std::asin(cameraFront.y));
        manager.cameraSpeed = 0.05f;
        manager.ApplyVisionProfile(manager.visionProfile);
        firstMouse = true;
    } else if (key == 702 && manager.blackHoleReady) {
        manager.inStarSystem=false;
        manager.pendingSystemTarget=0;
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
            + glm::normalize(manager.lastSpacecraftPosition) * 0.3f + glm::vec3(0.0f, 0.03f, 0.0f);
        cameraFront = glm::normalize(manager.lastSpacecraftPosition - cameraPos);
        yaw = glm::degrees(std::atan2(cameraFront.z, cameraFront.x));
        pitch = glm::degrees(std::asin(cameraFront.y));
        manager.cameraSpeed = 0.05f;
        manager.ApplyVisionProfile(manager.visionProfile);
        firstMouse = true;
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
        yaw = glm::degrees(std::atan2(cameraFront.z, cameraFront.x));
        pitch = glm::degrees(std::asin(cameraFront.y));
        manager.cameraSpeed = 0.05f;
    } else if (key == GLFW_KEY_H && manager.blackHoleReady) {
        manager.followSelected = false;
        manager.showBlackHole = true;
        manager.landed = false;
        manager.followPlanet = false;
        manager.followSpacecraft = false;
        manager.observerGravity = !manager.observerGravity;
        manager.horizonReached = false;
        manager.observer = {glm::dvec3(cameraPos), glm::dvec3(0.0)};
        cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    } else if (key == GLFW_KEY_F6 && manager.blackHoleReady) {
        manager.showBlackHole = !manager.inStarSystem;
        manager.photonView = !manager.photonView;
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
    } else if (key == GLFW_KEY_RIGHT_BRACKET) {
        manager.cameraSpeed = glm::min(50.0f, manager.cameraSpeed * 2.0f);
    } else if (key == GLFW_KEY_COMMA) {
        manager.timeRateIndex = glm::max(0, manager.timeRateIndex - 1);
    } else if (key == GLFW_KEY_PERIOD) {
        manager.timeRateIndex = glm::min(manager.inStarSystem?5:2, manager.timeRateIndex + 1);
    } else if (key == GLFW_KEY_ESCAPE) {
        manager.SetMouseCaptured(false);
    }
    manager.UpdateTitle();
}

void WindowManager::ResetView() {
    followSelected = false;
    landed = false;
    observerGravity = false;
    horizonReached = false;
    cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
    followPlanet = false;
    followSpacecraft = false;
    cameraPos = glm::vec3(0.0f, 0.0f, showBlackHole ? 6.0f : 3.0f);
    cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
    yaw = -90.0f;
    pitch = 0.0f;
    ApplyVisionProfile(visionProfile);
    cameraSpeed = 1.0f;
    firstMouse = true;
}

void WindowManager::ApplyVisionProfile(int profile) {
    visionProfile = profile;
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
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
    } else {
        cameraPos += cameraUp * 0.004f;
        cameraUp = glm::vec3(0, 1, 0);
    }
    yaw = glm::degrees(std::atan2(cameraFront.z, cameraFront.x));
    pitch = glm::degrees(std::asin(glm::clamp(cameraFront.y, -1.0f, 1.0f)));
    firstMouse = true;
}

void WindowManager::UpdateObserver(double previousTime) {
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
    double remaining = simulationTime - previousTime;
    const double boundary = BlackHole::HorizonRadius() * 1.015;
    while (remaining > 0.0) {
        const double radius = glm::length(observer.position);
        if (radius <= boundary) {
            observer.position = glm::normalize(observer.position) * boundary;
            observerGravity = false;
            horizonReached = true;
            simulationPlaying = false;
            simulationTime -= remaining;
            UpdateTitle();
            break;
        }
        const double interval = glm::min(remaining,
            0.01 * std::sqrt(radius * radius * radius / BlackHole::GravityParameter()));
        localTime += interval * OrbitalMechanics::ObserverClockRate(radius,
            glm::length(observer.velocity), BlackHole::GravityParameter(), BlackHole::HorizonRadius());
        OrbitalMechanics::AdvanceParticle(observer, BlackHole::GravityParameter(), 0.0, interval);
        remaining -= interval;
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
    glfwSetWindowTitle(window,"Render-Engine | Main menu");
}

void WindowManager::StartSolarSystem() {
    if (!starSystemReady) return;
    inStarSystem=true; quantumMode=false; showBlackHole=false;
    simulationPlaying=false; simulationTime=localTime=0; timeRateIndex=0;
    pendingSystemTarget=activeSystemTarget=-2;
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
    starSystem.Overview(cameraFront,cameraUp);
    fieldOfView=45; SyncCameraAngles(); ResizeInterface(); RefreshPanel(); UpdateTitle();
#ifdef _WIN32
    SetFocus(glfwGetWin32Window(window));
#endif
}

void WindowManager::StartGargantua() {
    if (!blackHoleReady) return;
    inStarSystem=quantumMode=false; showBlackHole=true; pendingSystemTarget=0;
    simulationPlaying=false; timeRateIndex=0; simulationTime=localTime=0;
    blackHole.ResetSpacecraft(0);
    mainMenu.Hide(); scenePanel.SetVisible(true); scenePanel.SetTitle("Star / object browser");
#ifdef _WIN32
    SetMenu(glfwGetWin32Window(window),static_cast<HMENU>(nativeMenu));
    DrawMenuBar(glfwGetWin32Window(window));
#endif
    std::vector<std::pair<int,std::string>> catalogue={{-2,"Sun / Solar System"},{0,"Gargantua / Black hole"}};
    for (int id=4;id<7010;++id) if (id!=9) catalogue.push_back({id,blackHole.ResolveTarget(id,simulationTime).name});
    scenePanel.SetCatalogue(std::move(catalogue));
    SelectCatalogue(0);
    ResetView(); ResizeInterface(); UpdateTitle();
#ifdef _WIN32
    SetFocus(glfwGetWin32Window(window));
#endif
}

void WindowManager::StartQuantum(int index) {
    if (!quantumReady) return;
    inStarSystem=false; showBlackHole=false; quantumMode=true;
    simulationPlaying=false; quantum.Select(index);
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
    ResizeInterface(); RefreshPanel(); UpdateTitle();
#ifdef _WIN32
    SetFocus(glfwGetWin32Window(window));
#endif
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

void WindowManager::RefreshPanel() {
    if (mainMenu.Visible()) return;
    if (quantumMode) scenePanel.SetInformation(quantum.Information());
    else if (pendingSystemTarget!=0) {
        const auto& system=inStarSystem && activeSystemTarget==pendingSystemTarget?starSystem:previewSystem;
        auto info=system.Information(simulationTime);
        if (system.IsSolarSystem()) info+="\n1:1 physical radii and distances\nJ2000 frozen Keplerian elements\n4096 representative belt/KBO markers\nPlanet surfaces are procedural illustrations.\n";
        scenePanel.SetInformation(info);
    } else {
        std::string info="GARGANTUA (inspired by Interstellar)\nSupermassive black hole\nClassification: Schwarzschild model\nMass / age: illustrative, not measured\nSpin and Kerr frame dragging: unmodeled\n\n";
        if (selectedTarget>=0) info+="Selected: "+blackHole.ResolveTarget(selectedTarget,simulationTime).name+"\n";
        info+="\nScaled demonstration system\nOcean-only black-hole planets\nForce-integrated spacecraft and boosters\n\nSpace  Play / pause\nF2  Orbit paths\nF3  Information panel\nF4  Overview\nF  Follow selected object\nG  Go to selected object\nL  Land / lift off\nF6  Photon view\nF10  Main menu\n";
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
        glfwSetWindowTitle(window,title.str().c_str()); return;
    }
    std::ostringstream title;
    const auto seconds = static_cast<std::uint64_t>(localTime);
    const auto worldSeconds = static_cast<std::uint64_t>(simulationTime);
    title << "Render-Engine | " << (inStarSystem ? starSystem.Host().name : showBlackHole ? "Gargantua-inspired" : "Model")
          << " | " << CameraOptics::Name(visionProfile)
          << (hdrMode ? " | HDR" : " | Pure exposure")
          << " | EV " << std::showpos << exposureEv << std::noshowpos
          << (followPlanet ? " | Miller flyby" : "")
          << (followSpacecraft ? " | Spacecraft" : "")
          << (followSelected ? " | Following selection" : "")
          << (landed ? " | Surface free-cam" : "")
          << (observerGravity ? " | Falling free-cam" : "")
          << (horizonReached ? " | Exterior horizon limit" : "")
          << (photonView ? " | Photon samples: fly aside to inspect" : "")
          << (showBlackHole && selectedTarget >= 0 ? " | Selected " + blackHole.ResolveTarget(selectedTarget, simulationTime).name + " [G]" : "")
          << (simulationPlaying ? " | Playing" : " | Paused")
          << " | Local " << std::setfill('0') << std::setw(2) << seconds / 3600
          << ':' << std::setw(2) << seconds / 60 % 60 << ':' << std::setw(2) << seconds % 60
          << " | World " << std::setw(2) << worldSeconds / 3600
          << ':' << std::setw(2) << worldSeconds / 60 % 60 << ':' << std::setw(2) << worldSeconds % 60
          << std::setfill(' ') << " | " << timeRates[timeRateIndex] << "x time"
          << " | Speed " << std::fixed << std::setprecision(6) << (inStarSystem?starSystem.speedAu:cameraSpeed)
          << (inStarSystem?" AU/s":" units/s")
          << (showBlackHole ? std::string(" | Craft ") + blackHole.SpacecraftStatus() : "")
          << (showBlackHole ? " | Light age " + std::to_string(
              blackHole.SpacecraftLightAge(cameraPos, simulationTime)) + " s" : "")
          << " | RMB flight | Esc cursor";
    glfwSetWindowTitle(window, title.str().c_str());
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
    auto& state=*static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (state.mainMenu.Visible() || state.quantumMode) return;
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
        manager.SetMouseCaptured(!manager.mouseCaptured);
    }
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
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
    const auto& manager = *static_cast<WindowManager*>(glfwGetWindowUserPointer(window));
    if (!isFocused || !manager.mouseCaptured)
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
    if (manager.inStarSystem) {
        manager.starSystem.Move(window,deltaTime,cameraFront,cameraUp);
        return;
    }
    glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));

    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        speed *= 5.0f;
        
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
            ? BlackHole::HorizonRadius() * 1.015f : 0.26f;
        if (glm::length(cameraPos) < minimumRadius)
            cameraPos = glm::normalize(cameraPos + glm::vec3(0.0f, 0.0f, 0.0001f)) * minimumRadius;
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
        glfwSwapBuffers(window); return;
    }
    if (quantumMode) {
        if (simulationPlaying) quantum.Advance(glm::min(static_cast<double>(dT),.1)*quantumRates[quantumRateIndex]);
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
    if (!inStarSystem) UpdateObserver(previousTime);
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
        glPolygonMode(GL_FRONT_AND_BACK, showWireframe ? GL_LINE : GL_FILL);
    } else {
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

        // Choose draw count from loaded vertices
        GLsizei drawCount = currentModelVertices.empty() ? 36 : currentModelVertices.size() / 6;
        glDrawArrays(GL_TRIANGLES, 0, drawCount);

        glBindVertexArray(0);
    }
    
    optics.Render(visionProfile, fieldOfView, exposureEv, hdrMode);
    glPolygonMode(GL_FRONT_AND_BACK, showWireframe ? GL_LINE : GL_FILL);
    glfwSwapBuffers(window);

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
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
}

void WindowManager::MakeNewWindow() {
    window = glfwCreateWindow(WIDTH, HEIGHT, "Render-Engine A0.1 | Playing", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        cerr << "Failed to initialize GLAD\n";
    }

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
    optics.Initialize();
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
    AppendMenuW(menu, MF_STRING, 3030, L"Main menu (F10)");
    AppendMenuW(menu, MF_STRING, 700, L"Black hole / Model");
    AppendMenuW(menu, MF_STRING, 701, L"Miller flyby");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_F2, L"Orbits (F2)");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_F3, L"Information (F3)");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_F4, L"Overview (F4)");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_SPACE, L"Play / Pause");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_HOME, L"Home view");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_LEFT_BRACKET, L"Slower [");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_RIGHT_BRACKET, L"Faster ]");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_COMMA, L"Time slower <");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_PERIOD, L"Time faster >");
    AppendMenuW(menu, MF_STRING, 702, L"Spacecraft");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_B, L"Boost (B)");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_R, L"Reset craft (R)");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_N, L"New terrain (N)");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_L, L"Land / lift off (L)");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_H, L"Gravity (H)");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_G, L"Go to selection (G)");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_F6, L"Photon view (F6)");
    AppendMenuW(menu, MF_STRING, GLFW_KEY_F, L"Follow selection (F)");
    const HMENU visionMenu = CreatePopupMenu();
    AppendMenuW(visionMenu, MF_STRING, 1000, L"Free camera");
    AppendMenuW(visionMenu, MF_STRING, 1001, L"Human vision (approx.)");
    AppendMenuW(visionMenu, MF_STRING, 1002, L"Canon EF 50mm f/1.2L (Lensfun)");
    AppendMenuW(visionMenu, MF_STRING, 1003, L"Nikkor 14-24mm at 14mm (Lensfun)");
    AppendMenuW(visionMenu, MF_STRING, 1004, L"ZEISS Otus 85mm (Lensfun)");
    AppendMenuW(visionMenu, MF_STRING, 1005, L"Ideal 200mm telephoto");
    AppendMenuW(visionMenu, MF_STRING, 1006, L"ZEISS Distagon 35mm f/2 (Lensfun)");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(visionMenu), L"Vision / Lens");
    const HMENU exposureMenu = CreatePopupMenu();
    AppendMenuW(exposureMenu, MF_STRING, 1200, L"HDR tone mapping (F7)");
    AppendMenuW(exposureMenu, MF_STRING, 1201, L"Pure exposure (F7)");
    AppendMenuW(exposureMenu, MF_SEPARATOR, 0, nullptr);
    const wchar_t* exposureNames[] = {L"-8 EV", L"-4 EV", L"-2 EV", L"0 EV", L"+2 EV", L"+4 EV", L"+8 EV", L"+12 EV", L"+16 EV"};
    for (int level = 0; level < 9; ++level)
        AppendMenuW(exposureMenu, MF_STRING, 1100 + level, exposureNames[level]);
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(exposureMenu), L"Exposure / HDR");
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
                bounds->ptMinTrackSize={1000,720}; return 0;
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
    OpenMainMenu();
#endif
}

void WindowManager::ReleaseGraphics() {
    quantum.Release();
    starSystem.Release();
    optics.Release();
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

int WindowManager::RunBenchmark() {
    using clock = std::chrono::steady_clock;
    glfwSwapInterval(0);
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
