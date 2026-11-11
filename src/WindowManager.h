#pragma once

#include <vector>
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

#include "GLFW/glfw3.h"

// Preferred client size; MakeNewWindow shrinks it to fit the monitor's work area.
const unsigned int WIDTH = 1600, HEIGHT = 900;

using namespace std;

class WindowManager
{
public:
    inline static bool showWireframe = true;
    
    void InitializeGLFW();
    bool MakeNewWindow();
    void Update(float&);
    bool isClosed();
    void ReleaseGraphics();
    int RunBenchmark();
    double GetSimulationTime() const { return simulationTime; }

    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void MouseCallback(GLFWwindow* window, double xPos, double yPos);
    static void WindowFocusCallback(GLFWwindow* window, int focused);
    static void DropCallback(GLFWwindow* window, int count, const char** paths);

    static bool isFocused;
    static double mouseX, mouseY;
    static unsigned int shaderProgram, flatShader, phongShader;
    static unsigned int VAO, VBO, EBO;
    static float verticeDistance;

    static glm::vec3 cameraPos;
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
    void UpdateObserver(double previousTime);
    void ApplyVisionProfile(int profile);
    float SceneAspect() const;
    void UpdateLensAspect();
    void ClearObserverModes();
    void SyncMenu();
    void UpdateStatus();
    void ShowScene();
    void FocusScene();
    void LoadModel(const std::string& path);
    const std::vector<std::pair<int, std::string>>& StarCatalogue();
    std::vector<std::pair<int, std::string>> starCatalogue;
    void AlignCameraUp(const glm::vec3& up);
    void HandleInterfaceCommand(int id, int notification);
    void OpenMainMenu();
    void StartSolarSystem();
    void StartGargantua();
    void StartQuantum(int index);
    void SelectCatalogue(int id);
    void EnterSelectedSystem(bool overview);
    void RefreshPanel();
    void ResizeInterface();
    void SyncCameraAngles();
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
    int quantumRateIndex = 2;
    void* nativeMenu = nullptr;
    CameraOptics optics;
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

    GLFWwindow* window;
    BlackHole blackHole;
    bool blackHoleReady = false;
    bool showBlackHole = false;
    bool followPlanet = false;
    bool followSpacecraft = false;
    bool followSelected = false;
    int followingTarget = -1;
    glm::vec3 lastSelectedPosition = glm::vec3(0);
    glm::vec3 lastPlanetPosition = glm::vec3(0.0f);
    glm::vec3 lastSpacecraftPosition = glm::vec3(0.0f);
    bool mouseCaptured = false;
    float cameraSpeed = 1.0f;
    float fieldOfView = 45.0f;
    double simulationTime = 0.0;
    double localTime = 0.0;
    bool simulationPlaying = false;
    int timeRateIndex = 0;
    float titleUpdateElapsed = 0.0f;
    std::string lastTitle;
    bool showingDefaultCube = true;
};
