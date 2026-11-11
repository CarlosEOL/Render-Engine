#include <iostream>
#include <chrono>

#include "WindowManager.h"

using namespace std;

int main() {
    using clock = chrono::steady_clock;
    float deltaTime = 0.0f;

    WindowManager window;
    window.MakeNewWindow();
    cout << "Made a new window.\n" << endl;

    // Main application loop
    auto lastTime = clock::now();
    while (!window.isClosed()) {
        auto now = clock::now();
        chrono::duration<float> elapsed = now - lastTime;
        deltaTime = elapsed.count();
        lastTime = now;

        window.Update(deltaTime);
    }

    window.ReleaseGraphics();
    glfwTerminate();
    return 0;
}
