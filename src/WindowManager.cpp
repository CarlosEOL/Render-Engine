#include "glad/glad.h"
#include "WindowManager.h"
#include "ObjectLoader.h"

#include <iostream>
using namespace std;

// Static definitions
bool WindowManager::isFocused = false;
double WindowManager::mouseX = 0.0;
double WindowManager::mouseY = 0.0;
glm::vec3 WindowManager::cameraPos   = glm::vec3(0.0f, 0.0f, 3.0f);
glm::vec3 WindowManager::cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 WindowManager::cameraUp    = glm::vec3(0.0f, 1.0f, 0.0f);

namespace {
    bool firstMouse = true;
    float yaw   = -90.0f;
    float pitch = 0.0f;
    float lastX = WIDTH / 2.0f;
    float lastY = HEIGHT / 2.0f;

    float timeSinceLastLog = 0.0f;
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
}

void WindowManager::MouseCallback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw   += xoffset;
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
    glfwSetInputMode(window, GLFW_CURSOR, focused ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    std::cout << (focused ? "[Focus] Mouse locked\n" : "[Focus] Mouse unlocked\n");
}
///*
void WindowManager::DropCallback(GLFWwindow* window, int count, const char** paths) {
    for (int i = 0; i < count; ++i) {
        std::string path(paths[i]);
        std::cout << "[Drop] File dropped: " << path << std::endl;

        if (path.empty()) {
            std::cerr << "[Drop] Ignored empty path\n";
            return;
        }

        glm::vec3 center; //Object center
        std::vector<float> modelVertices; //Store Loaded Vertices
        
        if (_UTILITY_::EndsWith(path, ".obj"))
        {
            if (ObjectLoader::LoadOBJ(path, modelVertices, center))
            {
                cout << "[Drop] OBJ loaded successfully" << endl;
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
    float speed = 2.5f * deltaTime;
    glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        cameraPos += speed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        cameraPos -= speed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        cameraPos -= right * speed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        cameraPos += right * speed;
}

unsigned int WindowManager::shaderProgram = 0;
unsigned int WindowManager::VAO, WindowManager::VBO, WindowManager::EBO;
float WindowManager::verticeDistance = 3.0f;

const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec2 TexCoord;

void main() {
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
})";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

void main() {
    FragColor = vec4(1.0); // pure white
})";

float vertices[] = {
    // positions       
    -0.5f, -0.5f, -0.5f,
     0.5f, -0.5f, -0.5f, 
     0.5f,  0.5f, -0.5f, 
     0.5f,  0.5f, -0.5f, 
    -0.5f,  0.5f, -0.5f, 
    -0.5f, -0.5f, -0.5f,

    -0.5f, -0.5f,  0.5f, 
     0.5f, -0.5f,  0.5f, 
     0.5f,  0.5f,  0.5f, 
     0.5f,  0.5f,  0.5f, 
    -0.5f,  0.5f,  0.5f, 
    -0.5f, -0.5f,  0.5f,

    -0.5f,  0.5f,  0.5f, 
    -0.5f,  0.5f, -0.5f, 
    -0.5f, -0.5f, -0.5f, 
    -0.5f, -0.5f, -0.5f, 
    -0.5f, -0.5f,  0.5f, 
    -0.5f,  0.5f,  0.5f,

     0.5f,  0.5f,  0.5f, 
     0.5f,  0.5f, -0.5f, 
     0.5f, -0.5f, -0.5f, 
     0.5f, -0.5f, -0.5f, 
     0.5f, -0.5f,  0.5f, 
     0.5f,  0.5f,  0.5f,

    -0.5f, -0.5f, -0.5f, 
     0.5f, -0.5f, -0.5f, 
     0.5f, -0.5f,  0.5f, 
     0.5f, -0.5f,  0.5f, 
    -0.5f, -0.5f,  0.5f, 
    -0.5f, -0.5f, -0.5f,

    -0.5f,  0.5f, -0.5f, 
     0.5f,  0.5f, -0.5f, 
     0.5f,  0.5f,  0.5f, 
     0.5f,  0.5f,  0.5f, 
    -0.5f,  0.5f,  0.5f, 
    -0.5f,  0.5f, -0.5f,
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

void WindowManager::Update(float& dT) {
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), WIDTH / (float)HEIGHT, 0.1f, 100.0f);

    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glUseProgram(shaderProgram);
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 36); // Draw the cube (36 vertices) //
    glBindVertexArray(0);
    
    glfwSwapBuffers(window);
    glfwPollEvents();

    ProcessCamera(window, dT);

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
    window = glfwCreateWindow(WIDTH, HEIGHT, "Render-Engine A0.1", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        cerr << "Failed to initialize GLAD\n";
    }

    glEnable(GL_DEPTH_TEST);

    glViewport(0, 0, WIDTH, HEIGHT);
    glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
    glfwSetWindowFocusCallback(window, WindowFocusCallback);
    WindowFocusCallback(window, 1); // Manually focus the mouse in the window //
    glfwSetCursorPosCallback(window, MouseCallback); // Then read mouse input
    glfwSetDropCallback(window, DropCallback); // Then read dropped files

    unsigned int vShader = createShader(GL_VERTEX_SHADER, vertexShaderSource);
    unsigned int fShader = createShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vShader);
    glAttachShader(shaderProgram, fShader);
    glLinkProgram(shaderProgram);

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
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    glBindVertexArray(0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    std::cout << "Program ID: " << shaderProgram << endl;
}

bool WindowManager::isClosed() {
    return glfwWindowShouldClose(window);
}
