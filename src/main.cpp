#include <iostream>
#include <chrono>
#include <string>

#include "WindowManager.h"

using namespace std;

int main(int argc, char** argv) {
    using clock = chrono::steady_clock;
    float deltaTime = 0.0f;

    WindowManager window;
    window.MakeNewWindow();
    cout << "Made a new window.\n" << endl;
    if (argc > 1 && string(argv[1]) == "--benchmark") {
        const int status = window.RunBenchmark();
        window.ReleaseGraphics();
        glfwTerminate();
        return status;
    }

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
