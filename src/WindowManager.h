#pragma once
#include "VehicleInterior.h"
#include "ObjectEditor.h"
#include "KerrSpacetime.h"

#include <vector>
#include <array>
#include "BlackHole.h"
#include "CameraOptics.h"
#include "StarSystem.h"
#include "ScenePanel.h"
#include "AppMenu.h"
#include "QuantumSimulation.h"

//CONTEXT: MANAGES GLFW WINDOWS
// I intentionally left out glad so the header won't be included in memoryTiles.Cpp, also then it won't need to compile it again in Tile.h

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include "GLFW/glfw3.h"

const unsigned int WIDTH = 1980, HEIGHT = 1080; //Window Size

using namespace std;

class WindowManager
{
public:
    inline static bool showWireframe = true;
    
    void InitializeGLFW();
    void MakeNewWindow();
    void Update(float&);
    bool isClosed();
    void ReleaseGraphics();
    double GetSimulationTime() const { return simulationTime; }
    int GetFrameLimit() const { return !isFocused || mainMenu.Visible() || idleFrame ? glm::min(frameLimit,30) : frameLimit; }
    bool RunRenderChecks(const std::string& directory);

    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void MouseCallback(GLFWwindow* window, double xPos, double yPos);
    static void WindowFocusCallback(GLFWwindow* window, int focused);
    static void DropCallback(GLFWwindow* window, int count, const char** paths);

    static bool isFocused;
    static double mouseX, mouseY;
    static unsigned int shaderProgram, flatShader, phongShader;
    static unsigned int VAO, VBO, EBO;
    static float verticeDistance;

    static glm::dvec3 cameraPos;
    static glm::vec3 cameraFront;
    static glm::vec3 cameraUp;

    static vector<float> currentModelVertices;
    
    //Public Constructor to Call when new window is created
    WindowManager()
    {
        InitializeGLFW();
    }

    static void ProcessCamera(GLFWwindow*, const float&);

private:
    static void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    void SetMouseCaptured(bool captured);
    void UpdateTitle();
    void ResetView();
    void ToggleLanding();
    void UpdateObserver(double previousTime, float realSeconds);
    void InitializeOverlay();
    void ReleaseOverlay();
    void ShowSpeedToast(float speed, bool starSystem);
    void ShowStatusToast(const std::string& text,bool small=true);
    void ShowZoomToast(bool force=false);
    void RenderSpeedToast();
    void ApplyVisionProfile(int profile);
    void LookAtPlanet();
    void SetVehicleView(int vehicle);
    void AlignCameraUp(const glm::vec3& up);
    void HandleInterfaceCommand(int id, int notification);
    void OpenMainMenu();
    void StartSolarSystem();
    void StartGargantua();
    void StartQuantum(int index);
    void SelectCatalogue(int id);
    void EnterSelectedSystem(bool overview);
    void RefreshPanel();
    void RefreshObjectEditor();
    void ResizeInterface();
    void SyncCameraAngles();
    void PrepareCurrentBrowser();
    void UpdateNearbySystem();
    int frameLimit = 30;
    float blackHoleResolution = .5f;
    float lensFlareStrength = .45f;
    bool sceneCacheValid = false;
    bool idleFrame = false;
    std::array<double,24> lastSceneSignature = {};
    AppMenu mainMenu;
    ScenePanel scenePanel;
    StarSystem starSystem;
    StarSystem previewSystem;
    QuantumSimulation quantum;
    bool starSystemReady = false;
    bool quantumReady = false;
    bool inStarSystem = false;
    bool quantumMode = false;
    bool trajectories = true;
    int pendingSystemTarget = 0;
    int activeSystemTarget = -2;
    glm::dvec3 systemOriginAu = glm::dvec3(0);
    double cameraClockRate=1;
    VehicleInterior vehicleInterior;
    ObjectEditor objectEditor;
    int interiorVehicle=0;
    glm::dmat3 lastCabinRotation=glm::dmat3(1);
    int quantumRateIndex = 2;
    void* nativeMenu = nullptr;
    CameraOptics optics;
    bool opticsReady = false;
    int visionProfile = 0;
    float lensAspect = static_cast<float>(WIDTH) / HEIGHT;
    float exposureEv = 0.0f;
    bool hdrMode = true;
    bool landed = false;
    bool observerGravity = false;
    bool horizonReached = false;
    int landedPlanet = 0;
    int selectedTarget = -1;
    bool photonView = false;
    OrbitalMechanics::ParticleState observer = {};
    KerrSpacetime::Observer kerrObserver = {};
    bool kerrObserverValid=false;
    double observerSpin=.8,pendingObserverProper=0;

    GLFWwindow* window;
    BlackHole blackHole;
    bool blackHoleReady = false;
    bool showBlackHole = false;
    bool followPlanet = false;
    bool followSpacecraft = false;
    bool followSelected = false;
    bool followSelectedRotation = false;
    glm::dmat3 lastSelectedRotation = glm::dmat3(1);
    int followingTarget = -1;
    glm::dvec3 lastSelectedPosition = glm::dvec3(0);
    glm::dvec3 lastPlanetPosition = glm::dvec3(0.0);
    glm::dvec3 lastSpacecraftPosition = glm::dvec3(0.0);
    bool mouseCaptured = false;
    bool zoomDragging = false;
    float cameraSpeed = 1.0f;
    float fieldOfView = 45.0f;
    double simulationTime = 0.0;
    double localTime = 0.0;
    bool simulationPlaying = false;
    int timeRateIndex = 0;
    float titleUpdateElapsed = 0.0f;
    bool showingDefaultCube = true;
    unsigned int overlayProgram = 0;
    unsigned int overlayVao = 0;
    unsigned int overlayVbo = 0;
    unsigned int overlayTexture = 0;
    int overlayWidth = 0;
    int overlayHeight = 0;
    double speedToastUntil = 0.0;
    double nextZoomToast=0;
};
