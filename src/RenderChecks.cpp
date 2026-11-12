#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#endif
#include "UiTheme.h"
#include "WindowManager.h"
#ifdef _WIN32
#include <GLFW/glfw3native.h>
#endif
#include "InterstellarWorlds.h"
#include "GalacticCore.h"
#include "CelestialPhenomena.h"
#include "MilkyWayModel.h"
#include "../tests/KerrCaptureReference.h"
#include "ObjectLoader.h"
#include "glad/glad.h"
#ifdef RENDER_ENGINE_DX12
#include "dx12/Dx12SceneBridge.h"
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <source_location>
#include <chrono>

// Optional hidden-window GPU smoke checks; normal application startup never runs this.
bool WindowManager::RunRenderChecks(const std::string &directory,bool uiOnly,bool galaxyOnly,bool lensingOnly)
{
    if (!blackHoleReady || !starSystemReady || !quantumReady || !opticsReady)
        return false;
    std::filesystem::create_directories(directory);
#ifdef RENDER_ENGINE_DX12
    NativeDX12::SetSceneSwapInterval(0);
#else
    glfwSwapInterval(0);
#endif
    // Exercise the real startup transition, including the menu's OpenGL clear-color state.
    float menuFrame=.016f;
    Update(menuFrame);
    bool success = true;
    std::set<unsigned> failedLines;
    unsigned captures=0,uiCaptures=0;
    const auto fail=[&](const std::source_location where=std::source_location::current()){
        success=false;
        if(failedLines.insert(where.line()).second)
            std::cerr<<"RENDER CHECK FAILED at "<<where.file_name()<<":"<<where.line()<<"\n";
    };
#ifdef _WIN32
    // Capture our own native controls without showing or moving a desktop
    // window. This complements the GPU-only scene captures below.
    const auto captureUi=[&](const char* name,void* target=nullptr){
        ++uiCaptures;
        const HWND native=target?static_cast<HWND>(target):glfwGetWin32Window(window);RECT rect{};GetClientRect(native,&rect);
        HDC screen=GetDC(native),dc=CreateCompatibleDC(screen);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=rect.right;info.bmiHeader.biHeight=-rect.bottom;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        unsigned char* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
        ReleaseDC(native,screen);
        if(!bitmap || !dc){std::cerr<<"UI capture allocation failed\n";fail();if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return;}
        auto previous=SelectObject(dc,bitmap);UiTheme::Fill(dc,rect,UiTheme::Background);
        if(!target && !mainMenu.Visible()){
            std::vector<unsigned char> scene(static_cast<size_t>(rect.right)*rect.bottom*3);
            glReadBuffer(GL_FRONT);glPixelStorei(GL_PACK_ALIGNMENT,1);
            glReadPixels(0,0,rect.right,rect.bottom,GL_RGB,GL_UNSIGNED_BYTE,scene.data());
            for(int y=0;y<rect.bottom;++y)for(int x=0;x<rect.right;++x){
                const auto p=pixels+(static_cast<size_t>(y)*rect.right+x)*4;
                const auto source=scene.data()+(static_cast<size_t>(rect.bottom-1-y)*rect.right+x)*3;
                p[0]=source[2];p[1]=source[1];p[2]=source[0];p[3]=255;
            }
        }
        std::vector<HWND> children;
        EnumChildWindows(native,+[](HWND child,LPARAM data)->BOOL{
            if(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)reinterpret_cast<std::vector<HWND>*>(data)->push_back(child);
            return TRUE;
        },reinterpret_cast<LPARAM>(&children));
        std::stable_sort(children.begin(),children.end(),[](HWND a,HWND b){return (GetDlgCtrlID(a)==3000 || GetDlgCtrlID(a)==2099) && GetDlgCtrlID(b)!=3000 && GetDlgCtrlID(b)!=2099;});
        for(auto child:children){
            if(GetDlgCtrlID(child)==3020){
                const auto selected=SendMessageW(child,LB_GETCURSEL,0,0);
                RECT row{},client{};GetClientRect(child,&client);
                const auto found=SendMessageW(child,LB_GETITEMRECT,selected,reinterpret_cast<LPARAM>(&row));
                if(selected==LB_ERR || found==LB_ERR || row.top<0 || row.bottom>client.bottom || row.bottom-row.top!=UiTheme::Px(child,35)){
                    std::cerr<<name<<" selected Quantum row is hidden or incorrectly sized\n";fail();
                }
            }
            RECT bounds;GetWindowRect(child,&bounds);MapWindowPoints(nullptr,native,reinterpret_cast<POINT*>(&bounds),2);
            if(bounds.left<0 || bounds.top<0 || bounds.right>rect.right || bounds.bottom>rect.bottom){std::cerr<<name<<" control clipped: "<<GetDlgCtrlID(child)<<"\n";fail();}
            const int saved=SaveDC(dc);SetViewportOrgEx(dc,bounds.left,bounds.top,nullptr);
            IntersectClipRect(dc,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top);
            SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND|PRF_CHILDREN);
            RestoreDC(dc,saved);
        }
        GdiFlush();std::ofstream file(std::filesystem::path(directory)/(std::string(name)+".ppm"),std::ios::binary);
        file<<"P6\n"<<rect.right<<' '<<rect.bottom<<"\n255\n";
        for(int y=0;y<rect.bottom;++y)for(int x=0;x<rect.right;++x){const auto p=pixels+(static_cast<size_t>(y)*rect.right+x)*4;const char rgb[]={char(p[2]),char(p[1]),char(p[0])};file.write(rgb,3);}
        if(!file){std::cerr<<"UI capture save failed\n";fail();}
        SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
        std::cout<<"UI capture: "<<name<<"; controls="<<children.size()<<"\n";
    };
    mainMenu.Show(AppMenu::Page::Home);captureUi("ui-main-menu");
    mainMenu.Show(AppMenu::Page::Planetorium);captureUi("ui-destinations");
    HandleInterfaceCommand(3011,BN_CLICKED);captureUi("ui-quantum-library");
    int uiWidth,uiHeight;glfwGetWindowSize(window,&uiWidth,&uiHeight);
    glfwSetWindowSize(window,997,733);
    mainMenu.Show(AppMenu::Page::Planetorium);captureUi("ui-destinations-compact");
    HandleInterfaceCommand(3011,BN_CLICKED);captureUi("ui-quantum-library-compact");
    glfwSetWindowSize(window,uiWidth,uiHeight);
    mainMenu.Show(AppMenu::Page::Home);
    StartSolarSystem();
    const HWND inputWindow=glfwGetWin32Window(window);
    const HWND information=GetDlgItem(inputWindow,2102);
    const auto informationShown=[&](){return (GetWindowLongPtrW(information,GWL_STYLE)&WS_VISIBLE)!=0;};
    const bool initialInformation=informationShown(),initialTrajectories=trajectories;
    KeyCallback(window,GLFW_KEY_F2,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    if(informationShown()==initialInformation || trajectories!=initialTrajectories)fail();
    KeyCallback(window,GLFW_KEY_F2,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    KeyCallback(window,GLFW_KEY_F2,0,GLFW_PRESS,0);
    if(informationShown()!=initialInformation || trajectories==initialTrajectories)fail();
    KeyCallback(window,GLFW_KEY_F2,0,GLFW_PRESS,0);
    HandleInterfaceCommand(1362,0);
    if(informationShown()==initialInformation || trajectories!=initialTrajectories)fail();
    HandleInterfaceCommand(1362,0);
    const bool initialPlayback=simulationPlaying;
    const auto inputFront=cameraFront,inputUp=cameraUp;
    const HWND playButton=GetDlgItem(inputWindow,2107);
    for(int click=0;click<2;++click){
        SetFocus(playButton);SendMessageW(playButton,BM_CLICK,0,0);
        if(GetFocus()!=inputWindow || !isFocused || simulationPlaying!=(click==0?!initialPlayback:initialPlayback)){
            std::cerr<<"Play/pause click did not restore viewport input\n";fail();
        }
        SetMouseCaptured(false);
        const auto beforeLook=cameraFront;
        MouseButtonCallback(window,GLFW_MOUSE_BUTTON_RIGHT,GLFW_PRESS,0);
        MouseCallback(window,100,100);MouseCallback(window,120,100);
        if(cameraFront==beforeLook){std::cerr<<"Mouse look stopped after Play/pause\n";fail();}
        SetMouseCaptured(false);
    }
    cameraFront=inputFront;cameraUp=inputUp;
    std::cout<<"Input checks: Shift+F2 information, F2 orbits, menu action and Play/pause mouse look\n";
    const auto finderCamera=starSystem.camera;const double finderTime=simulationTime;
    navigationPanel.Toggle(NavigationPanel::Mode::Finder,false);navigationPanel.SetQuery("Earth");navigationPanel.Refresh();
    captureUi("ui-object-finder",navigationPanel.NativeHandle());
    const auto foundEarth=navigationCatalog.Find("Earth",simulationTime);
    if(foundEarth.empty())fail();else {
        ListView_SetItemState(GetDlgItem(static_cast<HWND>(navigationPanel.NativeHandle()),5),0,LVIS_SELECTED,LVIS_SELECTED);
        if(!navigationSelectionValid || navigationSelection.name!="Earth" || starSystem.Selected()!=2){std::cerr<<"Click selection did not reach the selected object\n";fail();}
        if(GetDlgItem(static_cast<HWND>(navigationPanel.NativeHandle()),7)){std::cerr<<"Redundant Select button remains\n";fail();}
    }
    if(starSystem.camera!=finderCamera || simulationTime!=finderTime){std::cerr<<"Object selection moved the camera\n";fail();}
    navigationScope=navigationCatalog.Object(-2,2,simulationTime);
    navigationPanel.Toggle(NavigationPanel::Mode::Overview,false);captureUi("ui-earth-satellites",navigationPanel.NativeHandle());
    ActivateNavigation(NavigationPanel::Action::Back,{});navigationPanel.Refresh();captureUi("ui-solar-hierarchy",navigationPanel.NativeHandle());
    navigationScope=navigationCatalog.Object(-2,4,simulationTime);navigationPanel.Refresh();captureUi("ui-jupiter-satellites",navigationPanel.NativeHandle());
    navigationPanel.Toggle(NavigationPanel::Mode::Nearby,false);navigationPanel.SetQuery("20");navigationPanel.Refresh();captureUi("ui-nearby-systems",navigationPanel.NativeHandle());
    navigationPanel.Hide();navigationSelectionValid=false;mainMenu.Show(AppMenu::Page::Home);
    if(uiOnly){
        StartSolarSystem();float dt=.016f;Update(dt);captureUi("ui-star-browser-and-clock");
        const bool before=simulationPlaying;HandleInterfaceCommand(2107,BN_CLICKED);
        if(simulationPlaying==before)fail();HandleInterfaceCommand(2107,BN_CLICKED);
        std::cout<<"UI checks "<<(success?"PASSED":"FAILED")<<": "<<uiCaptures<<" captures; "<<failedLines.size()<<" assertion locations failed\n";
        return success;
    }
#endif
    const auto capture = [&](const char *name,bool keepToast=false) {
        ++captures;
        std::cout<<"[Capture] "<<name<<"\n";
        if(!keepToast)speedToastUntil = 0;
        float dt = .016f;
        const auto renderStart=std::chrono::steady_clock::now();
        Update(dt);
        glFinish();
        if(galaxyOnly)std::cout<<"Galaxy frame including bake/readiness: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-renderStart).count()<<" ms\n";
        if(selectedTarget==9 && showBlackHole){
            const auto relative=blackHole.ObservedSpacecraftPosition(cameraPos,simulationTime)-cameraPos;
            std::cout<<"Craft distance="<<glm::length(relative)*Astronomy::metresPerAu
                <<" m; aim="<<glm::dot(glm::normalize(relative),glm::dvec3(cameraFront))
                <<"; age="<<blackHole.SpacecraftLightAge(cameraPos,simulationTime)<<" s\n";
        }
        const auto error = glGetError();
        if (error != GL_NO_ERROR)
        {
            std::cerr << name << " GL error " << error << "\n";
            fail();
        }
        int width, height, logicalWidth, logicalHeight;
        glfwGetFramebufferSize(window, &width, &height);
        glfwGetWindowSize(window, &logicalWidth, &logicalHeight);
        width -= scenePanel.Width() * width / logicalWidth;
        std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
        glReadBuffer(GL_FRONT);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        const auto readError=glGetError();
        if(readError!=GL_NO_ERROR){std::cerr<<name<<" readback error "<<readError<<"\n";fail();}
        std::ofstream image(std::filesystem::path(directory) / (std::string(name) + ".ppm"), std::ios::binary);
        image << "P6\n" << width << " " << height << "\n255\n";
        for (int y = height - 1; y >= 0; --y)
            image.write(reinterpret_cast<const char *>(pixels.data() + static_cast<size_t>(y) * width * 3),
                        static_cast<std::streamsize>(width) * 3);
        image.flush();
        if(!image){std::cerr<<"Could not save capture "<<name<<"\n";fail();}
        size_t lit = 0;
        for (size_t i = 0; i < pixels.size(); i += 3)
            if (pixels[i] > 20 || pixels[i + 1] > 20 || pixels[i + 2] > 20)
                ++lit;
        std::cout << name << ": " << width << "x" << height << ", lit=" << lit << ", GL=" << error << "\n";
        if (lit < 10)
            fail();
        return pixels;
    };
    const auto checkLensing=[&](){
        StartGargantua();scenePanel.SetVisible(false);ResizeInterface();
        selectedTarget=-1;trajectories=false;visionProfile=0;lensFlareStrength=0;
        fieldOfView=45;blackHoleResolution=1;exposureEv=0;
        KeyCallback(window,1200,0,GLFW_PRESS,0);
        for(double testSpin:{.8,.98,-.8})for(int budget:{160,384}){
            blackHole.SetSpin(testSpin);blackHole.SetRayBudget(budget);sceneCacheValid=false;
            blackHole.SetLensingDiagnostic(false);
            capture(("kerr-shadow-"+std::to_string(budget)+"-spin-"+std::to_string(static_cast<int>(std::round(testSpin*100)))).c_str());
            blackHole.SetLensingDiagnostic(true);sceneCacheValid=false;
            const auto mask=capture(("kerr-ray-status-"+std::to_string(budget)+"-spin-"+std::to_string(static_cast<int>(std::round(testSpin*100)))).c_str());
            int w,h;glfwGetFramebufferSize(window,&w,&h);
            size_t captured=0,unresolved=0,total=0;
            for(int y=h/2-h/25;y<h/2+h/25;++y)for(int x=w/2-h/25;x<w/2+h/25;++x){
                const auto p=(static_cast<size_t>(y)*w+x)*3;
                if(mask[p]>100 && mask[p+1]<30){if(mask[p+2]<30)++captured;else if(mask[p+2]>100)++unresolved;}
                ++total;
            }
            std::cout<<"Ray status spin="<<testSpin<<" budget="<<budget<<"; center captured="<<captured<<" unresolved="<<unresolved<<" total="<<total<<"\n";
            if(captured<total*.9){std::cerr<<"Center of Kerr shadow was not captured\n";fail();}
            unsigned compared=0,mismatches=0,unfinished=0;
            const auto toDisk=glm::transpose(InterstellarWorlds::Orientation());
            for(int y=h/16;y<h*15/16;y+=std::max(1,h/60))for(int x=w/16;x<w*15/16;x+=std::max(1,w/80)){
                const auto p=(static_cast<size_t>(y)*w+x)*3;
                const bool red=mask[p]>100 && mask[p+1]<30 && mask[p+2]<30;
                const bool blue=mask[p+2]>100 && mask[p]<30 && mask[p+1]<30;
                const bool purple=mask[p]>100 && mask[p+2]>100 && mask[p+1]<30;
                if(!red && !blue && !purple)continue;
                const double u=(2*(x+.5)/w-1)*static_cast<double>(w)/h*std::tan(glm::radians(22.5));
                const double v=(2*(y+.5)/h-1)*std::tan(glm::radians(22.5));
                const auto ray=KerrSpacetime::PastRay(toDisk*cameraPos,toDisk*glm::normalize(glm::dvec3(u,v,-1)),BlackHole::HorizonRadius(),blackHole.Spin());
                const auto reference=KerrCaptureReference::Evaluate(ray,BlackHole::HorizonRadius(),blackHole.Spin());
                if(std::abs(reference.margin)<.02)continue; // Exclude the critical curve's pixel-scale ambiguity.
                ++compared;
                if(purple)++unfinished;else if(red!=reference.captured)++mismatches;
            }
            std::cout<<"Carter capture reference: compared="<<compared<<" mismatches="<<mismatches<<" unfinished="<<unfinished<<"\n";
            if(mismatches || (budget==384 && unfinished)){std::cerr<<"GPU rays disagree with the independent capture potential\n";fail();}

        }
        blackHole.SetLensingDiagnostic(false);blackHole.SetSpin(0);sceneCacheValid=false;
        capture("schwarzschild-shadow-reference");
        blackHole.SetSpin(.8);blackHole.SetRayBudget(160);sceneCacheValid=false;
        std::cout<<"Lensing checks "<<(success?"PASSED":"FAILED")<<"\n";
    };
    if(lensingOnly){checkLensing();return success;}
    const auto checkGalaxy=[&](){
        StartSolarSystem();scenePanel.SetVisible(false);ResizeInterface();
        trajectories=false;visionProfile=0;lensFlareStrength=0;fieldOfView=75;
        KeyCallback(window,1200,0,GLFW_PRESS,0);exposureEv=0;
        SetMagnitudeLimit(10.2f);
        const auto galaxyView=[&](const glm::dvec3& modelCamera,const glm::dvec3& modelAim,const char* name){
            const auto world=MilkyWayModel::ToWorldAu(modelCamera);
            starSystem.camera=world-systemOriginAu;
            cameraFront=glm::normalize(glm::vec3(MilkyWayModel::Orientation()*(modelAim-modelCamera)));
            const auto north=glm::vec3(MilkyWayModel::Orientation()[1]);
            cameraUp=std::abs(glm::dot(cameraFront,north))>.98f?glm::vec3(MilkyWayModel::Orientation()[2]):north;
            blackHole.InvalidateSky();sceneCacheValid=false;
            return capture(name);
        };
        fieldOfView=65;
        const auto face=galaxyView({0,32,0},{0,0,0},"milky-way-face-on");
        size_t blueArms=0,warmCore=0;
        for(size_t p=0;p<face.size();p+=3){
            if(face[p+2]>face[p]*1.15 && face[p+2]>35)++blueArms;
            if(face[p]>face[p+2]*1.12 && face[p]>70)++warmCore;
        }
        std::cout<<"Milky Way color structure: blue-arm pixels="<<blueArms<<"; warm-core pixels="<<warmCore<<"\n";
        if(blueArms<500 || warmCore<50)fail();
        const auto builds=blackHole.MilkyWayCacheBuilds();
        cameraFront=glm::normalize(cameraFront+cameraUp*.15f);sceneCacheValid=false;
        capture("milky-way-face-on-rotated");
        if(blackHole.MilkyWayCacheBuilds()!=builds){std::cerr<<"Galaxy cache rebuilt on rotation\n";fail();}
        galaxyView({0,16,27},{0,0,0},"milky-way-inclined");
        galaxyView({0,0,34},{0,0,0},"milky-way-edge-on");
        fieldOfView=100;
        const auto interior=galaxyView({MilkyWayModel::solarRadiusKpc,.000001,0},{0,0,0},"milky-way-solar-dust-lanes");
        int viewWidth,viewHeight;glfwGetFramebufferSize(window,&viewWidth,&viewHeight);
        double polar=0;for(int x=0;x<viewWidth;++x)for(int c=0;c<3;++c)polar+=interior[(static_cast<size_t>(viewHeight-1)*viewWidth+x)*3+c];
        polar/=viewWidth*3;
        std::cout<<"Interior off-plane mean display value="<<polar<<"\n";
        if(polar>40){std::cerr<<"Interior galaxy became an all-sky fog\n";fail();}
        galaxyView({7,.42,0},{5,0,0},"milky-way-volume-transition-low");
        galaxyView({7,1.48,0},{5,0,0},"milky-way-volume-transition-high");
        std::cout<<"Galaxy checks "<<(success?"PASSED":"FAILED")<<": "<<captures<<" scene captures; "<<failedLines.size()<<" assertion locations failed\n";
    };
    if(galaxyOnly){checkGalaxy();return success;}
    // Check many seeds before GPU captures. A range error must become a
    // named failure, not an interactive CRT assertion hidden behind the app.
    try {
        for(int id=10;id<1034;++id){StarSystem survey;survey.Generate(id,"Render-check survey");}
        std::cout<<"Generator preflight: 1024 systems passed\n";
    }catch(const std::exception& error){std::cerr<<"Generator preflight: "<<error.what()<<"\n";fail();return false;}
    // Exercise the legacy model-viewer layout and real import path as well as
    // the simulation scenes. The FBX deliberately has three vertices/one face
    // and a translated node, covering the previous face-count/transform bugs.
    const auto objPath=std::filesystem::path(directory)/"probe.obj";
    {std::ofstream model(objPath);model<<"v -1 -1 0\nv 1 -1 0\nv 0 1 0\nvn 0 0 1\nf 1//1 2//1 3//1\n";}
    const auto objFile=objPath.string();const char* droppedObj[]={objFile.c_str()};
    DropCallback(window,1,droppedObj);showWireframe=false;capture("model-imported-obj");
    const auto fbxPath=std::filesystem::path(directory)/"probe.fbx";
    {std::ofstream model(fbxPath);model<<R"(; FBX 7.4.0 project file
FBXHeaderExtension: {
 FBXHeaderVersion: 1003
 FBXVersion: 7400
}
Objects: {
 Geometry: 1, "Geometry::Triangle", "Mesh" {
  Vertices: *9 { a: -1,-1,0,1,-1,0,0,1,0 }
  PolygonVertexIndex: *3 { a: 0,1,-3 }
 }
 Model: 2, "Model::Triangle", "Mesh" {
  Version: 232
  Properties70: {
   P: "Lcl Translation", "Lcl Translation", "", "A",2,0,0
  }
 }
}
Connections: {
 C: "OO",1,2
 C: "OO",2,0
}
)";}
    std::vector<float> importedFbx;glm::vec3 importedCenter;
    const auto fbxFile=fbxPath.string();
    if(!ObjectLoader::LoadFBX(fbxFile,importedFbx,importedCenter) || importedFbx.size()!=18 || std::abs(importedCenter.x-2)>1e-4){
        std::cerr<<"FBX import lost geometry or node translation\n";fail();
    }else{
        const char* droppedFbx[]={fbxFile.c_str()};DropCallback(window,1,droppedFbx);capture("model-imported-fbx");
    }
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
        if((e.family==QuantumStates::Family::Box && e.state==2)
            || (e.family==QuantumStates::Family::Packet && e.state==1)
            || (e.family==QuantumStates::Family::Spin && e.state==5)
            || (e.family==QuantumStates::Family::Bell && e.state==1)){
            StartQuantum(index);quantum.Advance(.75);
            capture((std::string("quantum-family-")+std::to_string(static_cast<int>(e.family))).c_str());
        }
    }
    StartSolarSystem();
    KeyCallback(window, GLFW_KEY_F4, 0, GLFW_PRESS, 0);
    if (scenePanel.IsCurrentSystemBrowser())
        fail();
    SelectCatalogue(20);
    KeyCallback(window, GLFW_KEY_F4, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    if (!scenePanel.IsCurrentSystemBrowser() || pendingSystemTarget != activeSystemTarget)
        fail();
    KeyCallback(window, GLFW_KEY_F4, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    KeyCallback(window, GLFW_KEY_F2, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    if (scenePanel.Width() != 0)
        fail();
    KeyCallback(window, GLFW_KEY_F2, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    KeyCallback(window, GLFW_KEY_F4, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    capture("solar-overview");
#ifdef _WIN32
    captureUi("ui-star-browser-and-clock");
    const bool wasPlaying=simulationPlaying;HandleInterfaceCommand(2107,BN_CLICKED);
    if(simulationPlaying==wasPlaying)fail();HandleInterfaceCommand(2107,BN_CLICKED);
#endif
    // A visible background star can be selected from inside another system.
    // This goes through the same screen-coordinate routine as a mouse click.
    visionProfile=0;fieldOfView=45;
    SetMagnitudeLimit(20);
    const int clickedStar=47;
    cameraFront=glm::normalize(glm::vec3(blackHole.ResolveTarget(clickedStar,simulationTime).position-
        systemOriginAu-starSystem.camera));cameraUp={0,1,0};
    int clickW,clickH;glfwGetWindowSize(window,&clickW,&clickH);
    const auto clickCamera=starSystem.camera;const auto clickDirection=cameraFront;
    SelectAtScreen((clickW-scenePanel.Width())*.5,clickH*.5);
    if(pendingSystemTarget!=clickedStar || starSystem.camera!=clickCamera || cameraFront!=clickDirection){
        std::cerr<<"On-screen star selection failed or moved camera\n";fail();
    }
    capture("visible-star-selected-from-solar");
    KeyCallback(window,GLFW_KEY_G,0,GLFW_PRESS,0);
    if(activeSystemTarget!=clickedStar || starSystem.Host().systemSeed!=Universe::SystemSeed(clickedStar))fail();
    const auto clickedPlanets=starSystem.Planets();capture("visible-star-visited-system");
    StartSolarSystem();SelectCatalogue(clickedStar);KeyCallback(window,GLFW_KEY_G,0,GLFW_PRESS,0);
    if(starSystem.Planets().size()!=clickedPlanets.size() || starSystem.Planets()[0].seed!=clickedPlanets[0].seed)fail();
    StartSolarSystem();
    starSystem.SelectPlanet(2);
    starSystem.GoToSelected(simulationTime, cameraFront, cameraUp);
    capture("solar-earth");
    const auto cloudEarth=starSystem.Position(2,simulationTime),cloudRadial=glm::normalize(-cloudEarth);
    const auto cloudTangent=glm::normalize(glm::cross(cloudRadial,glm::dvec3(0,1,0)));
    trajectories=false;lensFlareStrength=0;
    for(double altitudeKm:{500.0,80.0,5.0}){
        starSystem.camera=cloudEarth+cloudRadial*(Astronomy::earthRadiusAu+altitudeKm*1000/Astronomy::metresPerAu);
        cameraFront=glm::vec3(altitudeKm>100?-cloudRadial:glm::normalize(cloudTangent+cloudRadial*.3));cameraUp=glm::vec3(altitudeKm>100?cloudTangent:cloudRadial);fieldOfView=65;
        starSystem.SetEarthClouds(false);sceneCacheValid=false;const auto clear=capture(("earth-clouds-"+std::to_string(int(altitudeKm))+"km-off").c_str());
        starSystem.SetEarthClouds(true);sceneCacheValid=false;const auto cloudy=capture(("earth-clouds-"+std::to_string(int(altitudeKm))+"km-on").c_str());
        if(altitudeKm==500 && clear==cloudy){std::cerr<<"Earth cloud shell made no visible contribution\n";fail();}
    }
    starSystem.SelectPlanet(2);starSystem.GoToSelected(simulationTime,cameraFront,cameraUp);fieldOfView=45;
    const auto moon=std::find_if(starSystem.Planets().begin(),starSystem.Planets().end(),[](const auto& p){return p.name=="Moon";});
    if(moon==starSystem.Planets().end())return false;
    const int moonIndex=static_cast<int>(moon-starSystem.Planets().begin());
    const auto moonCenter=starSystem.Position(moonIndex,simulationTime);
    starSystem.SelectPlanet(moonIndex);
    starSystem.camera=moonCenter+glm::normalize(-moonCenter)*(3*moon->radiusEarth*Astronomy::earthRadiusAu);
    cameraFront=glm::normalize(glm::vec3(moonCenter-starSystem.camera));cameraUp={0,1,0};fieldOfView=45;
    visionProfile=0;lensFlareStrength=0;exposureEv=0;
    const auto lunar=capture("moon-neutral-regolith");
    int moonWidth,moonHeight,moonLogicalW,moonLogicalH;
    glfwGetFramebufferSize(window,&moonWidth,&moonHeight);glfwGetWindowSize(window,&moonLogicalW,&moonLogicalH);
    moonWidth-=scenePanel.Width()*moonWidth/moonLogicalW;
    size_t moonBright=0,moonNeutral=0;
    for(int y=moonHeight*2/5;y<moonHeight*3/5;++y)for(int x=moonWidth*2/5;x<moonWidth*3/5;++x){
        const size_t p=(static_cast<size_t>(y)*moonWidth+x)*3;
        const int r=lunar[p],g=lunar[p+1],b=lunar[p+2];
        if(std::max({r,g,b})>30){++moonBright;if(std::abs(r-g)<4 && std::abs(g-b)<8)++moonNeutral;}
    }
    std::cout<<"Lunar neutral pixels: "<<moonNeutral<<" / "<<moonBright<<"\n";
    if(moonBright<100 || moonNeutral<moonBright*.98){std::cerr<<"Moon received a climate colour palette\n";fail();}
    trajectories=false;
    starSystem.SelectPlanet(5);starSystem.GoToSelected(simulationTime,cameraFront,cameraUp);
    const auto saturn=starSystem.Position(5,simulationTime);
    const double saturnRadius=starSystem.Planets()[5].radiusEarth*Astronomy::earthRadiusAu;
    starSystem.camera=saturn+glm::normalize(glm::dvec3(0,.8,1))*(saturnRadius*6);
    cameraFront=glm::normalize(glm::vec3(saturn-starSystem.camera));cameraUp={0,1,0};
    capture("saturn-banded-ice-rings");
    const auto ringFrame=glm::dmat3(glm::rotate(glm::dmat4(1),glm::radians(26.73),glm::dvec3(1,0,0)));
    starSystem.camera=saturn+ringFrame*glm::dvec3(105000000/Astronomy::metresPerAu,30/Astronomy::metresPerAu,0);
    cameraFront=glm::vec3(ringFrame*glm::dvec3(0,-.06,-1));cameraUp=glm::vec3(ringFrame[1]);fieldOfView=80;
    capture("saturn-ring-particle-proximity");
    starSystem.SelectPlanet(2);starSystem.GoToSelected(simulationTime,cameraFront,cameraUp);
    const auto scatteringEarth=starSystem.Position(2,simulationTime),sun=glm::normalize(-scatteringEarth);
    const auto tangent=glm::normalize(glm::cross(sun,glm::dvec3(0,1,0)));
    for(int low=0;low<2;++low){
        const double angle=glm::radians(low?1.0:25.0);
        const auto upright=sun*std::sin(angle)+tangent*std::cos(angle);
        starSystem.camera=scatteringEarth+upright*(Astronomy::earthRadiusAu*3);
        starSystem.ToggleLanding(simulationTime,cameraFront,cameraUp);
        const auto horizon=glm::normalize(sun-upright*std::sin(angle));
        cameraFront=glm::vec3(horizon*std::cos(glm::radians(8.0))+upright*std::sin(glm::radians(8.0)));
        cameraUp=glm::vec3(upright);fieldOfView=70;
        capture(low?"earth-low-sun-scattering":"earth-day-sun-scattering");
        starSystem.ToggleLanding(simulationTime,cameraFront,cameraUp);
    }
    StartGargantua();
    const auto beforeSelectionPosition=cameraPos;const auto beforeSelectionFront=cameraFront;
    SelectCatalogue(100);
    if(cameraPos!=beforeSelectionPosition || cameraFront!=beforeSelectionFront){std::cerr<<"Single selection moved camera\n";fail();}
    SelectCatalogue(0);
    const auto original = capture("gargantua-lensing");
    const auto cached = capture("gargantua-cached");
    if (!idleFrame || original != cached)
    {
        std::cerr << "Cached frame mismatch\n";
        fail();
    }
    KeyCallback(window,1330,0,GLFW_PRESS,0);
    const auto nonSpinning=capture("gargantua-spin-zero");
    KeyCallback(window,1332,0,GLFW_PRESS,0);
    const auto spinning=capture("gargantua-kerr-spin-08");
    if(nonSpinning==spinning){std::cerr<<"Kerr spin did not alter light propagation\n";fail();}
    const auto cameraBeforeEditor=cameraPos;const auto timeBeforeEditor=simulationTime;
    KeyCallback(window,GLFW_KEY_F5,0,GLFW_PRESS,0);
    if(!objectEditor.Visible() || !objectEditor.Editable())fail();
#ifdef _WIN32
    const auto editor=static_cast<HWND>(objectEditor.NativeHandle());
    if(!editor)return false;
    SetDlgItemTextA(editor,ObjectEditor::SpinValue,"0.650000");
    SendMessageW(editor,WM_COMMAND,MAKEWPARAM(ObjectEditor::Apply,BN_CLICKED),0);
    if(blackHole.Spin()!=.65 || sceneCacheValid)fail();
    const auto editedSpin=capture("object-editor-spin-065");
    if(editedSpin==spinning)fail();
    for(const char* invalid:{"1.0","nan","infinity","0.4junk",""}){
        SetDlgItemTextA(editor,ObjectEditor::SpinValue,invalid);
        SendMessageW(editor,WM_COMMAND,MAKEWPARAM(ObjectEditor::Apply,BN_CLICKED),0);
        if(blackHole.Spin()!=.65)fail();
    }
    SendDlgItemMessageW(editor,ObjectEditor::SpinSlider,TBM_SETPOS,TRUE,-5000);
    SendMessageW(editor,WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(GetDlgItem(editor,ObjectEditor::SpinSlider)));
    if(blackHole.Spin()!=-.5)fail();
    capture("object-editor-reverse-spin");
    KeyCallback(window,1331,0,GLFW_PRESS,0);
    char editorValue[40];GetDlgItemTextA(editor,ObjectEditor::SpinValue,editorValue,40);
    if(std::string(editorValue)!="0.500000")fail();
    SelectCatalogue(10);
    if(objectEditor.Editable() || IsWindowEnabled(GetDlgItem(editor,ObjectEditor::SpinSlider)))fail();
    SendMessageW(editor,WM_COMMAND,MAKEWPARAM(ObjectEditor::Reset,BN_CLICKED),0);
    if(blackHole.Spin()!=.5)fail();
    SelectCatalogue(0);
    SendMessageW(editor,WM_COMMAND,MAKEWPARAM(ObjectEditor::Reset,BN_CLICKED),0);
    if(blackHole.Spin()!=.8)fail();
#endif
    KeyCallback(window,GLFW_KEY_F5,0,GLFW_PRESS,0);
    if(objectEditor.Visible() || cameraPos!=cameraBeforeEditor || simulationTime!=timeBeforeEditor)fail();
    const auto displayBrightness=[](const auto& pixels){double sum=0;for(auto value:pixels)sum+=value;return sum;};
    lensFlareStrength=0;exposureEv=0;KeyCallback(window,1200,0,GLFW_PRESS,0);
    KeyCallback(window,1205,0,GLFW_PRESS,0);const auto hdrDiskOff=capture("black-hole-hdr-unassisted");
    KeyCallback(window,1204,0,GLFW_PRESS,0);const auto hdrDiskOn=capture("black-hole-hdr-visible");
    if(hdrDiskOn!=hdrDiskOff){std::cerr<<"HDR depends on legacy disk lift\n";fail();}
    KeyCallback(window,1201,0,GLFW_PRESS,0);optics.SetAutoExposure(true);
    KeyCallback(window,1205,0,GLFW_PRESS,0);const auto pureDiskOff=capture("black-hole-auto-unassisted");
    KeyCallback(window,1204,0,GLFW_PRESS,0);const auto pureDiskOn=capture("black-hole-auto-visible");
    std::cout<<"Black hole display sums HDR "<<displayBrightness(hdrDiskOff)<<" -> "<<displayBrightness(hdrDiskOn)
        <<"; Auto "<<displayBrightness(pureDiskOff)<<" -> "<<displayBrightness(pureDiskOn)<<"\n";
    if(displayBrightness(hdrDiskOn)<=displayBrightness(pureDiskOn)){std::cerr<<"HDR failed to preserve faint background light\n";fail();}
    KeyCallback(window,1200,0,GLFW_PRESS,0);lensFlareStrength=.45f;
    cameraPos={0,0,650};cameraFront={0,0,-1};cameraUp={0,1,0};fieldOfView=4;
    capture("gargantua-visible-from-mann-distance");
    ResetView();
    KeyCallback(window, 701, 0, GLFW_PRESS, 0);
    capture("miller-orbit");
    ToggleLanding();
    fieldOfView = 80;
    capture("miller-surface");
    // Foreground air must only affect rays intersecting its own shell, even
    // when the accretion disk is directly behind the planet. Disable camera
    // optics here so this tests atmospheric/lensing composition in scene RGB.
    ToggleLanding();followPlanet=false;followSpacecraft=false;followSelected=false;
    trajectories=false;selectedTarget=-1;lensFlareStrength=0;visionProfile=0;exposureEv=0;
    const auto millerCenter=BlackHole::PlanetPosition(simulationTime);
    const auto millerOutward=glm::normalize(millerCenter);
    const auto millerUp=glm::normalize(glm::cross(millerOutward,glm::dvec3(0,1,0)));
    const auto millerSide=glm::normalize(glm::cross(millerOutward,millerUp));
    fieldOfView=65;
    for(double radii:{3.0,1.029,1.031}){
        const auto offset=glm::normalize(millerOutward*(radii>2?.94:.35)+millerSide*(radii>2?.35:.94));
        cameraPos=millerCenter+offset*(Astronomy::millerRadiusAu*radii);
        const auto toPlanet=-offset;
        const auto skyTangent=glm::normalize(-millerOutward-toPlanet*glm::dot(-millerOutward,toPlanet));
        cameraFront=glm::vec3(radii>2?toPlanet:glm::normalize(toPlanet*.2+skyTangent*.98));
        cameraUp=glm::vec3(radii>2?millerUp:glm::normalize(offset-glm::dvec3(cameraFront)*glm::dot(offset,glm::dvec3(cameraFront))));
        const auto label=std::string("miller-atmosphere-")+std::to_string(radii);
        blackHole.SetAtmospheresEnabled(false);sceneCacheValid=false;
        capture((label+"-off").c_str());
        GLint width=0,height=0;glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,optics.SceneTexture());
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
        std::vector<float> noAir(static_cast<size_t>(width)*height*4);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,noAir.data());
        blackHole.SetAtmospheresEnabled(true);sceneCacheValid=false;
        capture((label+"-on").c_str());
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,optics.SceneTexture());std::vector<float> air(noAir.size());
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,air.data());
        const double aspect=static_cast<double>(width)/height,tangent=std::tan(glm::radians(static_cast<double>(fieldOfView))*.5);
        const auto right=glm::normalize(glm::cross(glm::dvec3(cameraFront),glm::dvec3(cameraUp)));
        const auto up=glm::normalize(glm::cross(right,glm::dvec3(cameraFront)));
        const auto origin=cameraPos-millerCenter;
        // Four-pixel allowance excludes the linear reconstruction footprint.
        const double shellRadius=Astronomy::millerRadiusAu*1.025+glm::length(origin)*tangent*8/height;
        size_t leaked=0,affected=0,nonfinite=0;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            const auto direction=glm::normalize(glm::dvec3(cameraFront)+right*((2*(x+.5)/width-1)*tangent*aspect)+up*((2*(y+.5)/height-1)*tangent));
            const double along=glm::dot(origin,direction);
            const bool intersects=along<0 && glm::dot(origin,origin)-along*along<shellRadius*shellRadius;
            const size_t pixel=(static_cast<size_t>(y)*width+x)*4;
            bool changed=false;
            for(int c=0;c<3;++c){
                if(!std::isfinite(air[pixel+c])){fail();++nonfinite;}
                if(std::abs(air[pixel+c]-noAir[pixel+c])>.00001f)changed=true;
            }
            if(changed){++affected;if(!intersects)++leaked;}
        }
        std::cout<<label<<" texture="<<width<<"x"<<height<<" affected="<<affected<<"; air outside shell="<<leaked<<"; nonfinite="<<nonfinite<<"\n";
        if(leaked>0 || affected<100){std::cerr<<"Miller atmosphere escaped its foreground shell\n";fail();}
    }
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
        fail();
    }
    KeyCallback(window,1320,0,GLFW_PRESS,0);
    const auto disabled=capture("solar-prominences-disabled");
    size_t exteriorPlasma=0;
    for(size_t i=0;i<active.size();i+=3)
        if(int(active[i])>int(disabled[i])+20 || int(active[i+1])>int(disabled[i+1])+20
            || int(active[i+2])>int(disabled[i+2])+20)++exteriorPlasma;
    std::cout<<"Exterior plasma pixels: "<<exteriorPlasma<<"\n";
    if(exteriorPlasma<100)fail();
    KeyCallback(window,1321,0,GLFW_PRESS,0);
    simulationTime+=180;
    if(active==capture("solar-prominences-evolved"))fail();
    StartGargantua();
    const auto companion=blackHole.ResolveTarget(4,simulationTime);
    cameraPos=companion.position+glm::dvec3(0,0,companion.radius*3.5);
    cameraFront={0,0,-1};cameraUp={0,1,0};
    selectedTarget=4;
    capture("gargantua-stellar-prominences");
    StartSolarSystem();
    starSystem.SelectPlanet(-1);
    starSystem.GoToSelected(simulationTime,cameraFront,cameraUp);
    trajectories=false;
    exposureEv=0;
    lensFlareStrength=0;
    starSystem.camera={0,0,3.5*Astronomy::solarRadiusAu};
    cameraFront={0,0,-1};cameraUp={0,1,0};
    ApplyVisionProfile(2);fieldOfView=45;
    KeyCallback(window,1325,0,GLFW_PRESS,0);
    const auto natural=capture("solar-natural-color");
    KeyCallback(window,1324,0,GLFW_PRESS,0);
    const auto warm=capture("solar-warm-color");
    if(natural==warm)fail();
    // Keep angular size, view direction and exposure fixed across lens comparisons.
    starSystem.camera={0,0,.25};
    cameraFront=glm::normalize(glm::vec3(.18,.06,-1));
    std::vector<std::vector<unsigned char>> profileDifferences;
    for(int lens=1;lens<=6;++lens) {
        ApplyVisionProfile(lens);fieldOfView=45;
        lensFlareStrength=0;
        const auto off=capture(("lens-"+std::to_string(lens)+"-off").c_str());
        lensFlareStrength=.45f;
        const auto on=capture(("lens-"+std::to_string(lens)+"-on").c_str());
        size_t changed=0;
        size_t darkened=0;
        std::vector<unsigned char> difference(on.size());
        for(size_t i=0;i<on.size();++i) {
            if(int(on[i])+1<int(off[i]))++darkened;
            difference[i]=static_cast<unsigned char>(std::max(0,int(on[i])-int(off[i])));
            if(difference[i]>8)++changed;
        }
        std::cout<<"Lens "<<lens<<" flare channels changed: "<<changed<<"\n";
        if(changed<100 || darkened>0)fail();
        for(const auto& other:profileDifferences)if(difference==other)fail();
        profileDifferences.push_back(std::move(difference));
    }
    // A fully occulted Sun must not leave a synthetic flare on top of the foreground planet.
    const auto earth=starSystem.Position(2,simulationTime);
    const auto outward=glm::normalize(earth);
    starSystem.camera=earth+outward*(3*Astronomy::earthRadiusAu);
    cameraFront=glm::vec3(-outward);cameraUp={0,1,0};
    ApplyVisionProfile(2);fieldOfView=45;
    lensFlareStrength=0;
    const auto occultedOff=capture("solar-occulted-flare-off");
    lensFlareStrength=.45f;
    const auto occultedOn=capture("solar-occulted-flare-on");
    glActiveTexture(GL_TEXTURE1);std::array<float,64> occultedSources;
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,occultedSources.data());glActiveTexture(GL_TEXTURE0);
    for(int source=0;source<8;++source)
        if(occultedSources[source*8+2]>0 && glm::length(glm::vec2(occultedSources[source*8]-.5f,occultedSources[source*8+1]-.5f))<.005f){
            std::cerr<<"Occluded Sun produced lens flare\n";fail();
        }
    // Reference-like framing: bright source above the optical center.
    starSystem.camera={0,0,.8};
    cameraFront=glm::normalize(glm::vec3(0,-.15,-1));cameraUp={0,1,0};
    ApplyVisionProfile(2);fieldOfView=45;
    optics.SetReferenceGlare(false);
    const auto referenceOff=capture("reference-glare-off");
    optics.SetReferenceGlare(true);
    const auto referenceOn=capture("reference-glare-on");
    if(referenceOff==referenceOn)fail();
    // A genuine line-of-sight eclipse: Moon covers the photosphere while the
    // separate corona remains outside its silhouette. No scripted animation.
    KeyCallback(window,1200,0,GLFW_PRESS,0);lensFlareStrength=0;exposureEv=0;
    const auto eclipseMoon=starSystem.Position(13,simulationTime);
    const double lunarRadius=starSystem.Planets()[13].radiusEarth*Astronomy::earthRadiusAu;
    const auto eclipseRay=glm::normalize(eclipseMoon);
    const double lunarDistance=lunarRadius*glm::length(eclipseMoon)/(Astronomy::solarRadiusAu*1.03-lunarRadius);
    starSystem.camera=eclipseMoon+eclipseRay*lunarDistance;cameraFront=glm::vec3(-eclipseRay);cameraUp={0,1,0};fieldOfView=3;
    starSystem.SetStellarActivity(false);starSystem.SetStellarCorona(false);sceneCacheValid=false;
    const auto eclipseOff=capture("solar-eclipse-corona-off");
    const auto centerRadiance=[&](){glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,optics.SceneInputTexture());GLint w,h;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);std::vector<float> pixels(static_cast<size_t>(w)*h*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data());const auto p=(static_cast<size_t>(h/2)*w+w/2)*4;return glm::vec3(pixels[p],pixels[p+1],pixels[p+2]);};
    const auto coveredOff=centerRadiance();
    starSystem.SetStellarCorona(true);sceneCacheValid=false;const auto eclipseOn=capture("solar-eclipse-corona-on");
    if(eclipseOff==eclipseOn || glm::length(centerRadiance()-coveredOff)>.00001f){std::cerr<<"Corona missing or leaking through the lunar silhouette\n";fail();}
    starSystem.SetStellarActivity(true);starSystem.camera={0,0,.8};cameraFront=glm::normalize(glm::vec3(0,-.15,-1));cameraUp={0,1,0};fieldOfView=45;
    SetMagnitudeLimit(7);lensFlareStrength=.45f;
    // Camera adaptation continues independently of paused scene dynamics.
    KeyCallback(window,1201,0,GLFW_PRESS,0);
    KeyCallback(window,1202,0,GLFW_PRESS,0);
    exposureEv=0;
    const auto autoStar=capture("pure-auto-star");
    exposureEv=2;
    if(autoStar==capture("pure-auto-positive-bias"))fail();
    exposureEv=0;
    cameraFront={0,0,1};
    capture("pure-auto-dark-first");
    float adaptationStep=.1f;
    for(int frame=0;frame<80;++frame)Update(adaptationStep);
    capture("pure-auto-dark-adapted");
    cameraFront=glm::normalize(glm::vec3(0,-.15,-1));
    const auto brightFirst=capture("pure-auto-bright-first");
    for(int frame=0;frame<30;++frame)Update(adaptationStep);
    const auto brightAdapted=capture("pure-auto-bright-adapted");
    if(!idleFrame || brightFirst==brightAdapted)fail();
    KeyCallback(window,1203,0,GLFW_PRESS,0);
    if(autoStar==capture("pure-manual-star"))fail();
    // Glare controls and lens profiles must not affect measured exposure.
    KeyCallback(window,1201,0,GLFW_PRESS,0);
    exposureEv=0;
    const auto meterState=[](){
        GLint active;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        glActiveTexture(GL_TEXTURE2);
        std::array<float,4> state;
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,state.data());
        glActiveTexture(active);return state;
    };
    optics.SetAutoExposure(true);optics.SetReferenceGlare(false);lensFlareStrength=0;
    const auto cleanFrame=capture("exposure-independent-glare-off");
    const auto glareOffState=meterState();
    optics.SetAutoExposure(true);optics.SetReferenceGlare(true);lensFlareStrength=1;
    const auto glareFrame=capture("exposure-independent-glare-on");
    const auto glareOnState=meterState();
    for(size_t i=0;i<cleanFrame.size();++i)
        if(int(glareFrame[i])+1<int(cleanFrame[i])){fail();std::cerr<<"Glare darkened the scene\n";break;}
    optics.SetAutoExposure(true);visionProfile=3;
    capture("exposure-independent-nikon");
    const auto nikonState=meterState();
    if(glareOffState[0]!=glareOnState[0] || glareOffState[1]!=glareOnState[1]
        || glareOffState[0]!=nikonState[0]){
        std::cerr<<"Optical glare affected metered exposure\n";fail();
    }
    float minPanEv=glareOffState[1],maxPanEv=glareOffState[1];
    for(int side:{-1,1}){
        cameraFront=glm::normalize(glm::vec3(side*.18f,-.15,-1));
        optics.SetAutoExposure(true);
        capture(side<0?"exposure-pan-left":"exposure-pan-right");
        const auto state=meterState();
        minPanEv=std::min(minPanEv,state[1]);maxPanEv=std::max(maxPanEv,state[1]);
    }
    std::cout<<"Glare-independent EV: "<<glareOffState[0]<<", "<<glareOnState[0]
             <<", "<<nikonState[0]<<"; pan span="<<maxPanEv-minPanEv<<"\n";
    if(maxPanEv-minPanEv>.2f)fail();
    KeyCallback(window,1200,0,GLFW_PRESS,0);
    // A resolved, erupting star must contribute one centered optical source.
    StartSolarSystem();starSystem.SelectPlanet(-1);
    starSystem.camera={0,0,3.5*Astronomy::solarRadiusAu};
    cameraFront={0,0,-1};cameraUp={0,1,0};
    ApplyVisionProfile(2);fieldOfView=45;exposureEv=0;lensFlareStrength=1;trajectories=false;
    KeyCallback(window,1321,0,GLFW_PRESS,0);KeyCallback(window,1322,0,GLFW_PRESS,0);
    const auto readSources=[](){
        GLint active;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE1);
        std::array<float,64> sources;
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,sources.data());
        glActiveTexture(active);return sources;
    };
    for(int activity=1;activity>=0;--activity){
        KeyCallback(window,activity?1321:1320,0,GLFW_PRESS,0);
        capture(activity?"erupting-star-one-glare-center":"quiet-star-one-glare-center");
        const auto sources=readSources();int count=0,hostCenters=0;
        for(int source=0;source<8;++source)if(sources[source*8+2]>0){
            ++count;
            const glm::vec2 offset(sources[source*8]-.5f,sources[source*8+1]-.5f);
            if(glm::length(offset)<1e-5f)++hostCenters;
            else {
                const float hostRadius=.5f/(3.5f*std::tan(glm::radians(22.5f)));
                GLint sourceViewport[4];glGetIntegerv(GL_VIEWPORT,sourceViewport);
                const float aspect=static_cast<float>(sourceViewport[2])/sourceViewport[3];
                if(glm::length(offset*glm::vec2(aspect,1))<hostRadius*1.9f+.004f)fail();
            }
        }
        std::cout<<"Stellar activity "<<activity<<" optical sources="<<count<<"; host centers="<<hostCenters<<"\n";
        if(hostCenters!=1)fail();
    }
    KeyCallback(window,1321,0,GLFW_PRESS,0);
    // Isolated eligibility test: identical bright pixels with alpha zero are
    // visible plasma, whereas alpha one is a potential optical emitter.
    CameraOptics sourceProbe;
    if(!sourceProbe.Initialize())return false;
    Atmosphere maskAtmosphere;
    if(!maskAtmosphere.Initialize())return false;
    GLint oldViewport[4];GLfloat oldClear[4];
    glGetIntegerv(GL_VIEWPORT,oldViewport);glGetFloatv(GL_COLOR_CLEAR_VALUE,oldClear);
    for(int eligible=0;eligible<=1;++eligible){
        glViewport(0,0,128,128);sourceProbe.BeginFrame();
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);glScissor(56,56,16,16);
        glClearColor(12,3,1,static_cast<float>(eligible));glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        maskAtmosphere.Render(glm::perspective(glm::radians(45.f),1.f,.1f,100.f),
            glm::vec3(0),glm::vec3(0,0,-4),1,false,glm::vec3(0,10,0));
        sourceProbe.Render(2,45,0,true,1,false,0);
        const auto sources=readSources();int count=0;
        for(int source=0;source<8;++source)if(sources[source*8+2]>0)++count;
        std::cout<<"Optical eligibility "<<eligible<<" source count="<<count<<"\n";
        if((eligible==0 && count!=0) || (eligible==1 && count==0) || glGetError()!=GL_NO_ERROR)fail();
    }
    GLint readBuffer;glGetIntegerv(GL_READ_BUFFER,&readBuffer);
    int brightnessSignals[2]={};
    for(int level=0;level<2;++level){
        const float radiance=level==0?2.f:8.f;
        glViewport(0,0,128,128);sourceProbe.BeginFrame();
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);glScissor(56,56,16,16);
        glClearColor(radiance,radiance*.4f,radiance*.1f,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
        sourceProbe.Render(2,45,0,true,1,false,0);
        unsigned char sample[4]={};glReadBuffer(GL_BACK);glReadPixels(14,64,1,1,GL_RGB,GL_UNSIGNED_BYTE,sample);
        brightnessSignals[level]=int(sample[0])+sample[1]+sample[2];
    }
    glReadBuffer(readBuffer);
    std::cout<<"Glare brightness response: "<<brightnessSignals[0]<<" -> "<<brightnessSignals[1]<<"\n";
    if(brightnessSignals[1]<=brightnessSignals[0])fail();
    glUseProgram(0);sourceProbe.Release();maskAtmosphere.Release();glActiveTexture(GL_TEXTURE0);
    glViewport(oldViewport[0],oldViewport[1],oldViewport[2],oldViewport[3]);
    glClearColor(oldClear[0],oldClear[1],oldClear[2],oldClear[3]);
    // Cool stellar photospheres remain emissive after a landed planet has used
    // the shared draw program's terrain uniforms and local high-detail mesh.
    StartSolarSystem();SelectCatalogue(6);EnterSelectedSystem(false);
    starSystem.SelectPlanet(-1);starSystem.GoToSelected(simulationTime,cameraFront,cameraUp);
    starSystem.camera={0,0,3.5*starSystem.Host().radiusSolar*Astronomy::solarRadiusAu};
    cameraFront={0,0,-1};cameraUp={0,1,0};fieldOfView=45;visionProfile=0;lensFlareStrength=0;exposureEv=0;
    const auto stellarCamera=starSystem.camera;
    const auto starBefore=capture("cool-star-emissive-before-landing");
    starSystem.SelectPlanet(0);starSystem.GoToSelected(simulationTime,cameraFront,cameraUp);
    starSystem.ToggleLanding(simulationTime,cameraFront,cameraUp);
    capture("generated-planet-surface-lod");
    starSystem.ToggleLanding(simulationTime,cameraFront,cameraUp);
    starSystem.SelectPlanet(-1);starSystem.camera=stellarCamera;
    cameraFront={0,0,-1};cameraUp={0,1,0};
    const auto starAfter=capture("cool-star-emissive-after-landing");
    if(starBefore!=starAfter){std::cerr<<"Planet state contaminated stellar rendering\n";fail();}
    size_t brightStellarPixels=0;
    for(size_t i=0;i<starAfter.size();i+=3)if(starAfter[i]>160)++brightStellarPixels;
    if(brightStellarPixels<1000){std::cerr<<"Cool star lacks emissive radiance\n";fail();}
    StartSolarSystem();
    const auto galaxyTarget=blackHole.ResolveTarget(1610,simulationTime);
    const glm::dvec3 arrivalOffset(0,0,galaxyTarget.radius*5.0);
    starSystem.camera=galaxyTarget.position+arrivalOffset;
    cameraFront={0,0,-1};cameraUp={0,1,0};
    const auto arrivalTime=simulationTime;
    const auto firstGalaxyImage=capture("camera-proximity-galaxy-star");
    if(activeSystemTarget!=1610 || starSystem.Host().id!=1610)fail();
    const auto firstPlanetSeed=starSystem.Planets()[0].seed;
    starSystem.camera={0,0,starSystem.DetailRadiusAu()*100};
    const auto beforeFarSolves=starSystem.PositionEvaluations();
    simulationPlaying=true;
    capture("distant-star-gpu-only-tier");
    simulationPlaying=false;
    const auto farSolves=starSystem.PositionEvaluations()-beforeFarSolves;
    std::cout<<"Inactive-system orbital evaluations="<<farSolves<<"\n";
    if(farSolves!=0)fail();
    SelectCatalogue(-2);EnterSelectedSystem(false);
    simulationTime=arrivalTime;
    starSystem.camera=galaxyTarget.position+arrivalOffset;
    cameraFront={0,0,-1};cameraUp={0,1,0};
    const auto revisitedGalaxyImage=capture("camera-proximity-same-star-revisit");
    if(activeSystemTarget!=1610 || starSystem.Planets()[0].seed!=firstPlanetSeed)fail();
    if(firstGalaxyImage!=revisitedGalaxyImage){std::cerr<<"Galaxy revisit changed the same seeded view\n";fail();}
    StartGargantua();
    for(int planet=1;planet<=2;++planet){
        const auto target=blackHole.ResolveTarget(planet+1,simulationTime);
        selectedTarget=planet+1;
        const auto radial=glm::normalize(-target.position);
        cameraPos=target.position+radial*(static_cast<double>(target.radius)*5.0);
        cameraFront=glm::vec3(-radial);cameraUp={0,1,0};
        ToggleLanding();fieldOfView=80;
        capture(planet==1?"mann-ice-surface-lod":"edmunds-rock-surface-lod");
        ToggleLanding();
    }
    StartGargantua();
    trajectories=false;selectedTarget=-1;visionProfile=0;lensFlareStrength=0;exposureEv=0;fieldOfView=100;
    const double infallRs=BlackHole::HorizonRadius();observerSpin=blackHole.Spin();
    kerrObserver=KerrSpacetime::MakeStationaryObserver({infallRs*3,.1,.05},infallRs,observerSpin);
    kerrObserverValid=true;observerGravity=false;horizonReached=true;
    const double outerHorizon=KerrSpacetime::Horizon(infallRs,observerSpin);
    std::vector<unsigned char> beforeHorizon;
    for(double fraction:{1.02,1.001,.999,.98,.85}){
        for(int step=0;step<100000 && KerrSpacetime::Radius(kerrObserver.state.position,infallRs,observerSpin)>outerHorizon*fraction;++step)
            KerrSpacetime::AdvanceObserver(kerrObserver,infallRs,observerSpin,.1,Astronomy::lightSpeedAu);
        cameraPos=InterstellarWorlds::Orientation()*kerrObserver.state.position;
        observer.position=cameraPos;
        observer.velocity=InterstellarWorlds::Orientation()*KerrSpacetime::ObserverCoordinateVelocity(kerrObserver,infallRs,observerSpin)*Astronomy::lightSpeedAu;
        cameraFront=glm::normalize(glm::vec3(cameraPos));cameraUp=glm::vec3(InterstellarWorlds::Orientation()*glm::dvec3(0,0,1));
        sceneCacheValid=false;
        const auto view=capture((std::string("kerr-infall-horizon-")+std::to_string(fraction)).c_str());
        if(fraction==1.001)beforeHorizon=view;
        if(fraction==.999){
            double change=0;for(size_t i=0;i<view.size();++i)change+=std::abs(int(view[i])-int(beforeHorizon[i]));change/=view.size();
            std::cout<<"Horizon-crossing mean display change: "<<change<<" / 255\n";
            if(change>12){std::cerr<<"Horizon crossing introduced a display discontinuity\n";fail();}
        }
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,optics.SceneTexture());
        GLint w,h;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
        std::vector<float> hdr(static_cast<size_t>(w)*h*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,hdr.data());glBindTexture(GL_TEXTURE_2D,0);
        for(float value:hdr)if(!std::isfinite(value)){std::cerr<<"Nonfinite interior-observer radiance\n";fail();break;}
    }
    kerrObserverValid=false;horizonReached=false;
    // The actual H control advances a physical observer while world playback
    // is paused, without manufacturing an infinite outside-time jump.
    cameraPos=InterstellarWorlds::Orientation()*glm::dvec3(infallRs*3,.1,.05);
    const int previousRate=timeRateIndex;timeRateIndex=2;const double pausedWorld=simulationTime,oldLocal=localTime;
    KeyCallback(window,GLFW_KEY_H,0,GLFW_PRESS,0);
    for(int frame=0;frame<200 && !horizonReached;++frame)UpdateObserver(simulationTime,.05f);
    if(!horizonReached || simulationTime!=pausedWorld || localTime<=oldLocal || !std::isfinite(cameraClockRate)){
        std::cerr<<"Physical H camera did not cross the horizon with paused world time\n";fail();
    }
    observerGravity=false;kerrObserverValid=false;horizonReached=false;pendingObserverProper=0;timeRateIndex=previousRate;
    StartGargantua();
    KeyCallback(window,702,0,GLFW_PRESS,0);
    capture("mann-spacecraft-departure");
    simulationTime=111*Astronomy::secondsPerDay;
    capture("gargantua-spacecraft-periapsis");
    cameraPos=blackHole.SpacecraftPosition()+glm::normalize(blackHole.SpacecraftPosition())*(500/Astronomy::metresPerAu);
    lastSpacecraftPosition=blackHole.SpacecraftPosition();cameraUp={0,1,0};
    cameraFront=glm::normalize(glm::vec3(blackHole.ObservedSpacecraftPosition(cameraPos,simulationTime)-cameraPos));
    const auto nearKerrShip=capture("endurance-close-kerr");
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,blackHole.LensingSourceDepthTexture());
    GLint shipWidth,shipHeight;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&shipWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&shipHeight);
    std::vector<float> shipDepth(static_cast<size_t>(shipWidth)*shipHeight);
    glGetTexImage(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT,GL_FLOAT,shipDepth.data());
    glBindTexture(GL_TEXTURE_2D,optics.SceneTexture());
    std::vector<float> shipKerrHdr(shipDepth.size()*4);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,shipKerrHdr.data());
    KeyCallback(window,1330,0,GLFW_PRESS,0);
    const auto nearStaticShip=capture("endurance-close-schwarzschild");
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,optics.SceneTexture());
    GLint hdrWidth,hdrHeight;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&hdrWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&hdrHeight);
    std::vector<float> shipHdr(static_cast<size_t>(hdrWidth)*hdrHeight*4);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,shipHdr.data());glBindTexture(GL_TEXTURE_2D,0);
    size_t solidShipPixels=0;
    if(hdrWidth!=shipWidth || hdrHeight!=shipHeight)fail();
    else for(size_t pixel=0;pixel<shipDepth.size();++pixel){
        const double metres=(std::exp2(shipDepth[pixel]*std::log2(1.0+20000.0*1e12))-1.0)/1e12*Astronomy::metresPerAu;
        if(metres<1000000){
            ++solidShipPixels;
            for(int c=0;c<3;++c)if(std::abs(shipKerrHdr[pixel*4+c]-shipHdr[pixel*4+c])>.00001f)fail();
        }
    }
    std::cout<<"Depth-identified foreground ship pixels stable across spin: "<<solidShipPixels<<"\n";
    if(solidShipPixels<100)fail();
    for(float value:shipHdr)if(!std::isfinite(value)){std::cerr<<"Nonfinite ship HDR radiance\n";fail();break;}
    KeyCallback(window,1332,0,GLFW_PRESS,0);
    simulationTime=180*Astronomy::secondsPerDay;
    capture("gargantua-spacecraft-escape");
    if(blackHole.SpacecraftEnergy()<=0 || !blackHole.RangersDetached())fail();
    SetVehicleView(1);capture("endurance-interior");
    const auto cabinUp=cameraUp;
    KeyCallback(window,GLFW_KEY_R,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    KeyCallback(window,GLFW_KEY_F,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    if(followSelected || followSpacecraft || cameraUp!=cabinUp)fail();
    KeyCallback(window,GLFW_KEY_L,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    capture("endurance-interior-looking-at-planet");
    if(glm::dot(cameraUp,glm::vec3(lastCabinRotation[1]))<.99999f)fail();
    SetVehicleView(2);capture("ranger-1-detached-interior");
    SetVehicleView(3);capture("ranger-2-detached-interior");
    SetVehicleView(0);
    StartSolarSystem();starSystem.SelectPlanet(2);
    KeyCallback(window,GLFW_KEY_F,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    if(!starSystem.IsFollowing())fail();
    capture("compact-position-follow-notification",true);
    fieldOfView=22.5;
    const auto beforeZoom=capture("zoom-readout-base");
    ShowZoomToast(true);
    if(speedToastUntil-glfwGetTime()>1.01 || speedToastUntil-glfwGetTime()<.8)fail();
    const auto duringZoom=capture("compact-zoom-readout",true);
    speedToastUntil=glfwGetTime()-1;
    const auto expiredZoom=capture("zoom-readout-expired",true);
    if(beforeZoom==duringZoom || beforeZoom!=expiredZoom){std::cerr<<"Zoom toast did not appear/disappear independently\n";fail();}
    fieldOfView=45;
    KeyCallback(window,GLFW_KEY_R,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    if(!starSystem.IsRotationFollowing())fail();
    KeyCallback(window,GLFW_KEY_L,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    if(glm::dot(cameraFront,glm::normalize(glm::vec3(starSystem.Position(2,simulationTime)-starSystem.camera)))<.99f)fail();
    int originalWidth,originalHeight;glfwGetWindowSize(window,&originalWidth,&originalHeight);
    glfwSetWindowSize(window,997,733);sceneCacheValid=false;capture("window-resize-odd-small");
    glfwSetWindowSize(window,1235,811);sceneCacheValid=false;capture("window-resize-odd-medium");
    glfwSetWindowSize(window,originalWidth,originalHeight);sceneCacheValid=false;capture("window-resize-restored");
#ifdef RENDER_ENGINE_DX12
    // Exercise actual NGX evaluations, convergence caching, invalidation and
    // resource retirement. Unsupported hardware keeps the native path.
    StartGargantua();trajectories=false;
    NativeDX12::SetDlssMode(NativeDX12::DlssMode::Quality);
    const auto dlssBefore=NativeDX12::DlssEvaluationCount();
    capture("dlss-quality-first-frame");
    if(NativeDX12::DlssEvaluationCount()>dlssBefore){
        for(int frame=0;frame<32;++frame){float dt=.016f;Update(dt);}
        const auto settledCount=NativeDX12::DlssEvaluationCount();
        capture("dlss-quality-converged");
        if(!idleFrame || NativeDX12::DlssEvaluationCount()!=settledCount)fail();
        const auto verifyOpticalMask=[&](const CameraOptics& camera,bool requireExcluded){
        GLint inputW,inputH,outputW,outputH;
        const auto readOpticalMask=[&](unsigned texture,GLint& w,GLint& h){
            glBindTexture(GL_TEXTURE_2D,texture);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
            std::vector<float> image(static_cast<size_t>(w)*h*4);
            glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,image.data());return image;
        };
        const auto maskInput=readOpticalMask(camera.SceneInputTexture(),inputW,inputH);
        const auto maskOutput=readOpticalMask(camera.SceneTexture(),outputW,outputH);
        size_t protectedMask=0,eligibleMask=0,maskErrors=0;
        for(int y=0;y<outputH;++y)for(int x=0;x<outputW;++x){
            const int sx=std::clamp(static_cast<int>((x+.5)*inputW/outputW),1,inputW-2);
            const int sy=std::clamp(static_cast<int>((y+.5)*inputH/outputH),1,inputH-2);
            float low=1,high=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
                const auto alpha=maskInput[(static_cast<size_t>(sy+dy)*inputW+sx+dx)*4+3];low=std::min(low,alpha);high=std::max(high,alpha);
            }
            const auto alpha=maskOutput[(static_cast<size_t>(y)*outputW+x)*4+3];
            if(high<.01f){++protectedMask;if(alpha>.01f)++maskErrors;}
            if(low>.99f){++eligibleMask;if(alpha<.99f)++maskErrors;}
        }
        std::cout<<"DLSS optical eligibility: excluded="<<protectedMask<<" eligible="<<eligibleMask<<" errors="<<maskErrors<<"\n";
        if((requireExcluded && protectedMask<100) || eligibleMask<100 || maskErrors)fail();
        };
        verifyOpticalMask(optics,false);
        auto resets=NativeDX12::DlssResetCount();cameraFront=glm::normalize(cameraFront+glm::vec3(.01,0,0));
        capture("dlss-camera-history-reset");
        if(NativeDX12::DlssResetCount()<=resets)fail();
        resets=NativeDX12::DlssResetCount();simulationPlaying=true;
        capture("dlss-playing-history-reset");simulationPlaying=false;
        if(NativeDX12::DlssResetCount()<=resets)fail();
        for(int mode=2;mode<=4;++mode){
            NativeDX12::SetDlssMode(static_cast<NativeDX12::DlssMode>(mode));sceneCacheValid=false;
            capture((std::string("dlss-mode-")+std::to_string(mode)).c_str());
        }
        glfwSetWindowSize(window,1001,735);sceneCacheValid=false;capture("dlss-resize-odd");
        glfwSetWindowSize(window,originalWidth,originalHeight);sceneCacheValid=false;
        CameraOptics dlssMaskProbe;
        if(!dlssMaskProbe.Initialize())return false;
        NativeDX12::SetDlssMode(NativeDX12::DlssMode::Quality);
        for(int eligible=0;eligible<=1;++eligible){
            const auto maskSize=NativeDX12::PrepareSceneReconstruction(256,256,false);
            glViewport(0,0,maskSize.x,maskSize.y);dlssMaskProbe.BeginFrame();
            glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);glScissor(maskSize.x/3,maskSize.y/3,maskSize.x/3,maskSize.y/3);
            glClearColor(12,3,1,static_cast<float>(eligible));glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);glViewport(0,0,256,256);
            dlssMaskProbe.Render(2,45,0,true,1,false,.016f);
            verifyOpticalMask(dlssMaskProbe,eligible==0);
            const auto sources=readSources();int count=0;
            for(int source=0;source<8;++source)if(sources[source*8+2]>0)++count;
            std::cout<<"DLSS optical source count, eligibility "<<eligible<<": "<<count<<"\n";
            if((eligible==0 && count!=0) || (eligible==1 && count==0))fail();
        }
        dlssMaskProbe.Release();glUseProgram(0);glActiveTexture(GL_TEXTURE0);
        std::cout<<"DLSS actual evaluations="<<NativeDX12::DlssEvaluationCount()<<"; history resets="<<NativeDX12::DlssResetCount()<<"\n";
    }else std::cout<<"DLSS fallback: "<<NativeDX12::DlssStatus()<<"\n";
    NativeDX12::SetDlssMode(NativeDX12::DlssMode::Off);sceneCacheValid=false;
    capture("dlss-disabled-native-fallback");
#endif
    // Synthetic HDR input makes exposure response measurable without relying on
    // scene composition. Readback here is confined to this opt-in test harness.
    ExposureMeter probe;
    if(!probe.Initialize())return false;
    unsigned int meterInput;glGenTextures(1,&meterInput);
    const glm::vec3 noHost(-1,-1,0);
    const auto input=[&](float value,float sourceRadius,float sourceValue=-1){
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,meterInput);
        std::vector<float> values(32*18*4,value);
        if(sourceValue>=0){
            const int extent=sourceRadius<.005f?1:8;
            for(int y=0;y<extent;++y)for(int x=0;x<extent;++x)
                for(int channel=0;channel<3;++channel)values[((y+4)*32+x+8)*4+channel]=sourceValue;
        }
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,32,18,0,GL_RGBA,GL_FLOAT,values.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D,0);
    };
    const auto read=[&](){
        std::array<float,4> state;
        glBindTexture(GL_TEXTURE_2D,probe.Texture());
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,state.data());
        glBindTexture(GL_TEXTURE_2D,0);return state;
    };
    input(.01f,.05f);probe.Update(meterInput,noHost,0);
    const auto initial=read();
    input(100,.05f);probe.Update(meterInput,noHost,0);
    const auto frozen=read();
    probe.Update(meterInput,noHost,.1f);
    const auto darkening=read();
    for(int frame=0;frame<20;++frame)probe.Update(meterInput,noHost,.25f);
    const auto settled=read();
    input(.01f,.05f);probe.Update(meterInput,noHost,.1f);
    const auto brightening=read();
    input(0,0);probe.Reset();probe.Update(meterInput,noHost,0);
    const auto black=read();
    input(.1f,.0001f,10000);probe.Reset();probe.Update(meterInput,noHost,0);
    const auto unresolved=read();
    input(.1f,.05f,10000);probe.Reset();probe.Update(meterInput,noHost,0);
    const auto resolved=read();
    const auto probeError=glGetError();
    const bool meterValid=std::isfinite(initial[0]) && frozen[0]==initial[0]
        && darkening[0]<initial[0] && darkening[0]>darkening[1]
        && std::abs(settled[0]-settled[1])<.01f
        && brightening[0]>settled[0] && brightening[0]<brightening[1]
        && (initial[0]-darkening[0])>(brightening[0]-settled[0])
        && black[0]==8 && std::abs(unresolved[0]-resolved[0])<.01f && probeError==GL_NO_ERROR;
    std::cout<<"Exposure meter EV: "<<initial[0]<<" -> "<<darkening[0]
             <<" -> "<<settled[0]<<"; brightening "<<brightening[0]
             <<"; black "<<black[0]<<"; valid="<<meterValid<<"\n";
    if(!meterValid)fail();
    probe.Release();glDeleteTextures(1,&meterInput);
    StartSolarSystem();trajectories=false;visionProfile=2;exposureEv=0;lensFlareStrength=.45f;
    for(int core:{GalacticCores::firstId,GalacticCores::tonId}){
        SelectCatalogue(core);EnterSelectedSystem(true);fieldOfView=55;
        KeyCallback(window,1200,0,GLFW_PRESS,0);capture(core==GalacticCores::tonId?"ton-618-hdr":"sagittarius-a-hdr");
        KeyCallback(window,1201,0,GLFW_PRESS,0);optics.SetAutoExposure(true);capture(core==GalacticCores::tonId?"ton-618-auto":"sagittarius-a-auto");
    }
    KeyCallback(window,1200,0,GLFW_PRESS,0);visionProfile=0;lensFlareStrength=0;
    for(const auto& nebula:CelestialPhenomena::Nebulae()){
        const auto found=navigationCatalog.Find(nebula.name,simulationTime);
        if(found.empty()){fail();continue;}
        ActivateNavigation(NavigationPanel::Action::Go,found.front());capture(("nebula-"+std::to_string(nebula.id-CelestialPhenomena::firstId)).c_str());
    }
    StartSolarSystem();trajectories=false;starSystem.camera={0,0,100};cameraFront=glm::normalize(glm::vec3(MilkyWayModel::CenterAu()));cameraUp={0,1,0};fieldOfView=75;
    const unsigned beforeSky=blackHole.MilkyWayCacheBuilds();
    SetMagnitudeLimit(3.6f);capture("magnitude-limit-3-60");
    SetMagnitudeLimit(7);capture("magnitude-limit-7-00");
    SetMagnitudeLimit(10.2f);capture("magnitude-limit-10-20");
    cameraFront=glm::normalize(glm::vec3(MilkyWayModel::Orientation()[1]));capture("milky-way-galactic-pole");
    if(blackHole.MilkyWayCacheBuilds()>beforeSky+1){std::cerr<<"Sky rebaked on rotation or magnitude changes\n";fail();}
    SetMagnitudeLimit(7);
    checkLensing();
    checkGalaxy();
    std::cout<<"Render checks "<<(success?"PASSED":"FAILED")<<": "<<captures<<" scene captures; "<<uiCaptures<<" UI captures; "<<failedLines.size()<<" assertion locations failed\n";
    return success;
}
