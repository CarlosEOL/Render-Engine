#pragma once

//CONTEXT: MANAGES GLFW WINDOWS
// I intentionally left out glad so the header won't be included in memoryTiles.Cpp, also then it won't need to compile it again in Tile.h

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "GLFW/glfw3.h"

class WindowManager
{
public:
    void InitializeGLFW();
    void MakeNewWindow(int, int);
    void Update();
    bool isClosed();

    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void MouseCallback(GLFWwindow* window, double& xPos, double& yPos);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void MouseCursorCallback(GLFWwindow* window, double xpos, double ypos);
    static void MouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void WindowCloseCallback(GLFWwindow* window);
    static void WindowRefreshCallback(GLFWwindow* window);
    static void WindowFocusCallback(GLFWwindow* window, int focused);

    static bool isFocused;
    static double mouseX, mouseY;
    static unsigned int shaderProgram;
    static unsigned int VAO, VBO, EBO;
    static float verticeDistance;
    
    //Public Constructor to Call when new window is created
    WindowManager()
    {
        InitializeGLFW();
    }

    static void processInput(GLFWwindow* window)
    {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
            // move forward
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            // move backward
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            // strafe left
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            // strafe right
        }  
    }

private:
    GLFWwindow* window;
};
