#include "WindowManager.h"
#include "glad/glad.h"
#include <filesystem>
#include <fstream>
#include <iostream>

// Optional hidden-window GPU smoke checks; normal application startup never runs this.
bool WindowManager::RunRenderChecks(const std::string &directory)
{
    if (!blackHoleReady || !starSystemReady || !quantumReady || !opticsReady)
        return false;
    std::filesystem::create_directories(directory);
    glfwSwapInterval(0);
    // Exercise the real startup transition, including the menu's OpenGL clear-color state.
    float menuFrame=.016f;
    Update(menuFrame);
    bool success = true;
    const auto capture = [&](const char *name) {
        speedToastUntil = 0;
        float dt = .016f;
        Update(dt);
        glFinish();
        const auto error = glGetError();
        if (error != GL_NO_ERROR)
        {
            std::cerr << name << " GL error " << error << "\n";
            success = false;
        }
        int width, height, logicalWidth, logicalHeight;
        glfwGetFramebufferSize(window, &width, &height);
        glfwGetWindowSize(window, &logicalWidth, &logicalHeight);
        width -= scenePanel.Width() * width / logicalWidth;
        std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
        glReadBuffer(GL_FRONT);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        std::ofstream image(std::filesystem::path(directory) / (std::string(name) + ".ppm"), std::ios::binary);
        image << "P6\n" << width << " " << height << "\n255\n";
        for (int y = height - 1; y >= 0; --y)
            image.write(reinterpret_cast<const char *>(pixels.data() + static_cast<size_t>(y) * width * 3),
                        static_cast<std::streamsize>(width) * 3);
        size_t lit = 0;
        for (size_t i = 0; i < pixels.size(); i += 3)
            if (pixels[i] > 20 || pixels[i + 1] > 20 || pixels[i + 2] > 20)
                ++lit;
        std::cout << name << ": " << width << "x" << height << ", lit=" << lit << ", GL=" << error << "\n";
        if (lit < 10)
            success = false;
        return pixels;
    };
    const auto &examples = QuantumStates::Examples();
    for (int index = 0; index < static_cast<int>(examples.size()); ++index)
    {
        const auto &e = examples[index];
        if (e.family == QuantumStates::Family::Hydrogen && e.state == 4)
        {
            StartQuantum(index);
            capture("quantum-hydrogen-3d");
        }
        if (e.family == QuantumStates::Family::Oscillator && e.state == 7)
        {
            StartQuantum(index);
            quantum.Advance(1.2);
            capture("quantum-coherent-3d");
        }
        if (e.family == QuantumStates::Family::Numerical && e.state == 0)
        {
            StartQuantum(index);
            quantum.Advance(3);
            capture("quantum-tunneling-3d");
        }
    }
    StartSolarSystem();
    KeyCallback(window, GLFW_KEY_F4, 0, GLFW_PRESS, 0);
    if (scenePanel.IsCurrentSystemBrowser())
        success = false;
    SelectCatalogue(20);
    KeyCallback(window, GLFW_KEY_F4, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    if (!scenePanel.IsCurrentSystemBrowser() || pendingSystemTarget != activeSystemTarget)
        success = false;
    KeyCallback(window, GLFW_KEY_F4, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    KeyCallback(window, GLFW_KEY_F3, 0, GLFW_PRESS, 0);
    if (scenePanel.Width() != 0)
        success = false;
    KeyCallback(window, GLFW_KEY_F3, 0, GLFW_PRESS, 0);
    KeyCallback(window, GLFW_KEY_F4, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    capture("solar-overview");
    starSystem.SelectPlanet(2);
    starSystem.GoToSelected(simulationTime, cameraFront, cameraUp);
    capture("solar-earth");
    StartGargantua();
    const auto original = capture("gargantua-lensing");
    const auto cached = capture("gargantua-cached");
    if (!idleFrame || original != cached)
    {
        std::cerr << "Cached frame mismatch\n";
        success = false;
    }
    KeyCallback(window, 701, 0, GLFW_PRESS, 0);
    capture("miller-orbit");
    ToggleLanding();
    fieldOfView = 80;
    capture("miller-surface");
    StartSolarSystem();
    starSystem.SelectPlanet(-1);
    starSystem.GoToSelected(simulationTime, cameraFront, cameraUp);
    starSystem.camera = glm::dvec3(0, 0, 2);
    cameraFront={0,0,-1};cameraUp={0,1,0};
    trajectories=false;
    exposureEv = 4;
    ApplyVisionProfile(2);
    capture("camera-flare");
    // Isolate actual plasma outside the photosphere from the camera's optical flare.
    lensFlareStrength=0;
    exposureEv=-2;
    ApplyVisionProfile(0);
    fieldOfView=45;
    starSystem.camera={0,0,3.5*Astronomy::solarRadiusAu};
    KeyCallback(window,1321,0,GLFW_PRESS,0);
    KeyCallback(window,1322,0,GLFW_PRESS,0);
    const auto active=capture("solar-prominences");
    const auto paused=capture("solar-prominences-paused");
    if(active!=paused || !idleFrame) {
        std::cerr<<"Stellar activity changed while paused\n";
        success=false;
    }
    KeyCallback(window,1320,0,GLFW_PRESS,0);
    const auto disabled=capture("solar-prominences-disabled");
    size_t exteriorPlasma=0;
    for(size_t i=0;i<active.size();i+=3)
        if(disabled[i]<5 && disabled[i+1]<5 && disabled[i+2]<5
            && (active[i]>20 || active[i+1]>20 || active[i+2]>20))++exteriorPlasma;
    std::cout<<"Exterior plasma pixels: "<<exteriorPlasma<<"\n";
    if(exteriorPlasma<100)success=false;
    KeyCallback(window,1321,0,GLFW_PRESS,0);
    simulationTime+=180;
    if(active==capture("solar-prominences-evolved"))success=false;
    StartGargantua();
    const auto companion=blackHole.ResolveTarget(4,simulationTime);
    cameraPos=companion.position+glm::dvec3(0,0,companion.radius*3.5);
    cameraFront={0,0,-1};cameraUp={0,1,0};
    selectedTarget=4;
    capture("gargantua-stellar-prominences");
    return success;
}
