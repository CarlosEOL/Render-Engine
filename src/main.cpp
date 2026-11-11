#include <iostream>
#include <chrono>
#include <thread>

#include "WindowManager.h"

using namespace std;

int main(int argc,char** argv) {
    using clock = chrono::steady_clock;
    float deltaTime = 0.0f;

    WindowManager window;
    const bool renderChecks=argc>1 && std::string(argv[1])=="--render-check";
    if(renderChecks)glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    window.MakeNewWindow();
    cout << "Made a new window.\n" << endl;
    if(renderChecks) {
        const bool success=window.RunRenderChecks(argc>2?argv[2]:"build/render-checks");
        window.ReleaseGraphics();glfwTerminate();return success?0:1;
    }

    // Main application loop
    auto lastTime = clock::now();
    while (!window.isClosed()) {
        auto now = clock::now();
        chrono::duration<float> elapsed = now - lastTime;
        deltaTime = elapsed.count();
        lastTime = now;

        window.Update(deltaTime);
        const int limit=window.GetFrameLimit();
        if(limit>0)std::this_thread::sleep_until(now+chrono::duration_cast<clock::duration>(chrono::duration<double>(1.0/limit)));
    }

    window.ReleaseGraphics();
    glfwTerminate();
    return 0;
}
