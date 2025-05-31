#include <iostream>
#include <chrono>

#include "Utility.h"
#include "WindowManager.h"

using namespace std;
using namespace UTILITY;

int main() {
    using clock = chrono::steady_clock;
    auto lastTime = clock::now();
    float deltaTime = 0.0f;

    WindowManager window;
    window.MakeNewWindow();
    cout << "Made a new window.\n" << endl;

    // Main application loop
    while (!window.isClosed()) {
        auto now = clock::now();
        chrono::duration<float> elapsed = now - lastTime;
        deltaTime = elapsed.count();
        lastTime = now;

        window.Update(deltaTime);
    }

    glfwTerminate();
    return 0;
}