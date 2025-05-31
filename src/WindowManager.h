#pragma once

//CONTEXT: MANAGES GLFW WINDOWS
// I intentionally left out glad so the header won't be included in memoryTiles.Cpp, also then it won't need to compile it again in Tile.h

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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
    GLFWwindow* window;
};
