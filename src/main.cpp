#include <iostream>
#include <chrono>
#include <thread>

#include "WindowManager.h"
#ifdef RENDER_ENGINE_DX12
#include "dx12/Dx12SceneBridge.h"
#endif

using namespace std;

int RunEngine(int argc,char** argv) {
    using clock = chrono::steady_clock;
    float deltaTime = 0.0f;

    WindowManager window;
    const bool uiChecks=argc>1 && std::string(argv[1])=="--ui-check";
    const bool galaxyChecks=argc>1 && std::string(argv[1])=="--galaxy-check";
    const bool renderChecks=uiChecks || galaxyChecks || (argc>1 && std::string(argv[1])=="--render-check");
    if(renderChecks){std::cout<<std::unitbuf;std::cerr<<std::unitbuf;}
#ifdef RENDER_ENGINE_DX12
    bool warp=false;for(int i=1;i<argc;++i)if(std::string(argv[i])=="--warp")warp=true;
    NativeDX12::ConfigureSceneBridge(renderChecks,warp);
#endif
    if(renderChecks)glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    window.MakeNewWindow();
    cout << "Made a new window.\n" << endl;
    if(renderChecks) {
        bool success=window.RunRenderChecks(argc>2?argv[2]:"build/render-checks",uiChecks,galaxyChecks);
#ifdef RENDER_ENGINE_DX12
        const auto errors=NativeDX12::SceneValidationErrors();
        std::cout<<"DX12 scene validation errors="<<errors<<"\n";
        success=success && errors==0;
#endif
        window.ReleaseGraphics();
#ifdef RENDER_ENGINE_DX12
        NativeDX12::ReleaseSceneBridge();
#endif
        glfwTerminate();return success?0:1;
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
#ifdef RENDER_ENGINE_DX12
    const auto errors=NativeDX12::SceneValidationErrors();
    NativeDX12::ReleaseSceneBridge();
    if(errors)return 1;
#endif
    glfwTerminate();
    return 0;
}

int main(int argc,char** argv){
    try{return RunEngine(argc,argv);}
    catch(const std::exception& error){std::cerr<<"Render-Engine: "<<error.what()<<"\n";return 1;}
}
