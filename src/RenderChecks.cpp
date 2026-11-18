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
#include "NearbyGalaxies.h"
#include "WormholePhysics.h"
#include "TesseractSpace.h"
#include "CameraZoom.h"
#include "../tests/KerrCaptureReference.h"
#include "ObjectLoader.h"
#include "AssetManager.h"
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
#include <functional>
unsigned int compileShaderProgram(const char*,const char*);

// Optional hidden-window GPU smoke checks; normal application startup never runs this.
bool WindowManager::RunRenderChecks(const std::string &directory,bool uiOnly,bool galaxyOnly,bool lensingOnly,bool wormholeOnly,bool weatherOnly)
{
    if (!blackHoleReady || !starSystemReady || !quantumReady || !opticsReady)
        return false;
    std::filesystem::create_directories(directory);
    // Authored reference captures teleport between poses. Test actual motion
    // adaptation separately below rather than treating those jumps as flight.
    blackHole.SetAdaptiveSkyQuality(false);
    adaptiveBlackHoleQuality=false;
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
    if(blackHole.Catalogue().Positions().size()!=Universe::StarCount || Universe::StarCount<2000000)fail();
    for(int index:{Universe::LegacyStarCount,Universe::LegacyStarCount+262143,800000,1500000,Universe::StarCount-1}){
        const int id=Universe::StarId(index);const auto p=Universe::StarPosition(index);
        if(Universe::StarIndex(id)!=index || GalacticCores::IsCore(id) || CelestialPhenomena::IsNebula(id)
            || p!=blackHole.Catalogue().Positions()[index] || !Universe::GalaxyAt(Universe::GalaxyForStar(index)).Contains(p))fail();
        if(blackHole.NearestStar(p)!=id)fail();
        if(blackHole.PickCatalogueStar(p+glm::dvec3(0,0,10),glm::vec3(0,0,-1),.00001)!=id)fail();
        StarSystem first,again;first.Generate(id,Universe::StarName(index));again.Generate(id,Universe::StarName(index));
        if(first.Planets().size()!=again.Planets().size() || first.Planets().front().seed!=again.Planets().front().seed)fail();
    }
    const auto remoteName=Universe::StarName(Universe::StarCount-1);
    const auto remoteFound=navigationCatalog.Find(remoteName,0);
    if(remoteFound.empty() || remoteFound.front().name!=remoteName || remoteFound.front().system!=Universe::StarId(Universe::StarCount-1))fail();
    std::cout<<"Procedural catalogue: "<<Universe::StarCount<<" stars; sampled galaxy bounds, stable IDs, nearest lookup, planets and name search passed\n";
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
    for(int profile=0;profile<7;++profile){
        ApplyVisionProfile(profile);SetMouseCaptured(false);zoomDragging=true;
        MouseCallback(window,100,100);MouseCallback(window,100,-10000);
        if(std::abs(CameraMagnification()-20)>1e-3 || std::abs(EffectiveMagnitudeLimit()-13.50515f)>.001f || limitingMagnitude!=7)fail();
        zoomDragging=false;MouseButtonCallback(window,GLFW_MOUSE_BUTTON_MIDDLE,GLFW_PRESS,0);
        if(std::abs(CameraMagnification()-1)>1e-5 || EffectiveMagnitudeLimit()!=7)fail();
    }
    ApplyVisionProfile(0);SyncCameraMagnitude();speedToastUntil=0;
    std::cout<<"Zoom checks: 20x on all seven profiles; magnitude 7 -> 13.505 -> 7; middle-click reset passed\n";
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
    const auto capture = [&](const char *name,bool keepToast=false,const std::function<void()>& renderOverride={},bool allowDark=false) {
        ++captures;
        std::cout<<"[Capture] "<<name<<"\n";
        if(!keepToast)speedToastUntil = 0;
        float dt = .016f;
        const auto renderStart=std::chrono::steady_clock::now();
        if(renderOverride)renderOverride();else Update(dt);
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
        if (lit < 10 && !allowDark)
            fail();
        return pixels;
    };
    const auto checkOceanWeather=[&](){
        StartSolarSystem();starSystem.SelectPlanet(2);simulationPlaying=false;
        const auto cloudEarth=starSystem.Position(2,simulationTime),cloudRadial=glm::normalize(-cloudEarth);
        trajectories=false;lensFlareStrength=0;
        // Locate an actual procedural ocean landing, then compare the reflection
        // contribution with/without volumetric weather. Cloud changes in the sky
        // alone cannot satisfy the water-only difference-of-differences check.
        const auto readWaterScene=[&](){
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,optics.SceneInputTexture());GLint w,h;
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
            std::vector<float> result(static_cast<size_t>(w)*h*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,result.data());return result;
        };
        const auto weatherState=starSystem.WeatherState(2,simulationTime);
        const auto weatherFrame=starSystem.WeatherFrame(2,simulationTime);
        int stormIndex=0;double mostLight=-2;
        for(int i=0;i<3;++i)if(weatherState.storms[i].w>0){
            const double light=glm::dot(weatherFrame*glm::dvec3(weatherState.storms[i]),cloudRadial);
            if(light>mostLight){mostLight=light;stormIndex=i;}
        }
        const auto stormCentre=glm::dvec3(weatherState.storms[stormIndex]);
        const auto stormEast=glm::normalize(glm::cross(glm::dvec3(0,1,0),stormCentre));
        const auto stormNorth=glm::cross(stormCentre,stormEast);
        bool oceanFound=false;
        for(int pose=0;pose<12 && !oceanFound;++pose){
            if(starSystem.IsLanded())starSystem.ToggleLanding(simulationTime,cameraFront,cameraUp);

            const double azimuth=pose*glm::two_pi<double>()/12;
            const double wall=.22*weatherState.shapes[stormIndex].x;
            const auto radial=weatherFrame*glm::normalize(stormCentre*std::cos(wall)
                +(stormEast*std::cos(azimuth)+stormNorth*std::sin(azimuth))*std::sin(wall));
            starSystem.camera=cloudEarth+radial*(Astronomy::earthRadiusAu*1.1);
            starSystem.ToggleLanding(simulationTime,cameraFront,cameraUp);
            const auto acrossEye=weatherFrame*stormCentre-radial*glm::dot(weatherFrame*stormCentre,radial);
            cameraFront=glm::normalize(glm::vec3(acrossEye)-cameraUp*.003f);
            cameraFront=glm::normalize(cameraFront-cameraUp*.12f);fieldOfView=100;visionProfile=0;exposureEv=0;
            starSystem.SetOceanReflections(false);starSystem.SetEarthClouds(false);sceneCacheValid=false;float dt=.016f;Update(dt);
            const auto water=readWaterScene();size_t receivers=0;
            for(size_t p=0;p<water.size();p+=4)if(water[p+3]>=.099f && water[p+3]<=.405f)++receivers;
            oceanFound=receivers>10000;
        }
        if(!oceanFound){std::cerr<<"No ocean receiver found for weather reflection check\n";fail();}
        else{
            capture("earth-ocean-clear-ssr-off");const auto clearOff=readWaterScene();
            starSystem.SetOceanReflections(true);sceneCacheValid=false;capture("earth-ocean-clear-ssr-on");const auto clearOn=readWaterScene();
            starSystem.SetEarthClouds(true);starSystem.SetOceanReflections(false);sceneCacheValid=false;capture("earth-ocean-clouds-ssr-off");const auto cloudsOff=readWaterScene();
            starSystem.SetOceanReflections(true);sceneCacheValid=false;capture("earth-ocean-clouds-ssr-on");const auto cloudsOn=readWaterScene();
            size_t reflectedWeather=0;
            for(size_t p=0;p<clearOff.size();p+=4)if(clearOff[p+3]>=.099f && clearOff[p+3]<=.405f){
                float change=0;for(int c=0;c<3;++c)change+=std::abs((cloudsOn[p+c]-cloudsOff[p+c])-(clearOn[p+c]-clearOff[p+c]));
                if(change>.0001f)++reflectedWeather;
            }
            std::cout<<"Volumetric weather reflected on ocean pixels="<<reflectedWeather<<"\n";
            if(reflectedWeather<100)fail();
            starSystem.SetWeather(0,0);sceneCacheValid=false;const auto calm=capture("earth-ocean-calm-wind");
            starSystem.SetWeather(3,1);sceneCacheValid=false;const auto windy=capture("earth-ocean-strong-wind");
            if(calm==windy){std::cerr<<"Wind speed made no visible change to water/reflections\n";fail();}
            starSystem.SetWeather(1,1);
        }
        starSystem.SetOceanReflections(true);starSystem.SetEarthClouds(true);
        if(starSystem.IsLanded())starSystem.ToggleLanding(simulationTime,cameraFront,cameraUp);
    };
    const auto checkWeather=[&](){
        StartSolarSystem();scenePanel.SetVisible(false);ResizeInterface();showWireframe=false;glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
        simulationPlaying=false;simulationTime=0;trajectories=false;selectedTarget=-1;
        visionProfile=0;fieldOfView=45;lensFlareStrength=0;exposureEv=0;KeyCallback(window,1200,0,GLFW_PRESS,0);
        GLint target,viewport[4];glGetIntegerv(GL_FRAMEBUFFER_BINDING,&target);glGetIntegerv(GL_VIEWPORT,viewport);
        const char* vertex=R"(#version 330 core
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);})";
        const auto fragment=std::string("#version 330 core\n")+PlanetWeather::ShaderFunctions()+R"(
uniform vec3 windNormal;
out vec4 result;
void main(){float i=gl_FragCoord.x-.5;vec3 p=vec3(i*3.25,10,21);result=vec4(weatherWaveHeight(p,windNormal),weatherWind(windNormal),0,1);})";
        const auto program=compileShaderProgram(vertex,fragment.c_str());unsigned texture,fbo,vao;
        glGenTextures(1,&texture);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,128,1,0,GL_RGBA,GL_FLOAT,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);glViewport(0,0,128,1);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);
        double maximumError=0;
        for(float wind:{0.f,1.f,3.f}){
            const auto state=PlanetWeather::Make(PlanetWeather::Miller(),4321,wind,1);glUseProgram(program);PlanetWeather::Upload(program,state);
            const auto normal=glm::normalize(glm::vec3(.3,.4,.7));glUniform3fv(glGetUniformLocation(program,"windNormal"),1,glm::value_ptr(normal));
            glDrawArrays(GL_TRIANGLES,0,3);glBindTexture(GL_TEXTURE_2D,texture);std::array<float,512> values{};glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,values.data());
            for(int i=0;i<128;++i){maximumError=std::max(maximumError,std::abs(values[i*4]-PlanetWeather::WaveHeight(state,{i*3.25,10,21},normal)));
                if(std::abs(values[i*4+1]-PlanetWeather::WindSpeed(state,normal))>.01)fail();}
        }
        std::cout<<"CPU/GPU wind-wave maximum height error="<<maximumError<<" metres\n";if(maximumError>.002)fail();
        glBindFramebuffer(GL_FRAMEBUFFER,target);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);glEnable(GL_DEPTH_TEST);glBindVertexArray(0);
        glDeleteProgram(program);glDeleteTextures(1,&texture);glDeleteFramebuffers(1,&fbo);glDeleteVertexArrays(1,&vao);
        const auto weather=starSystem.WeatherState(2,0);const auto earth=starSystem.Position(2,0);
        int active=-1;double illumination=-2;
        for(int i=0;i<3;++i)if(weather.storms[i].w>0){const double day=glm::dot(starSystem.WeatherFrame(2,0)*glm::dvec3(weather.storms[i]),glm::normalize(-earth));if(day>illumination){active=i;illumination=day;}}
        if(active<0){std::cerr<<"Earth has no visible seeded cyclone\n";fail();return;}
        const auto normal=glm::dvec3(weather.storms[active]);
        const auto orientation=starSystem.WeatherFrame(2,0);
        const auto radial=orientation*normal;
        starSystem.camera=earth+radial*(Astronomy::earthRadiusAu*4);
        cameraFront=glm::vec3(-radial);cameraUp=glm::vec3(orientation[1]);
        starSystem.SetWeather(1,0);sceneCacheValid=false;const auto quiet=capture("weather-earth-cloud-distribution");
        starSystem.SetWeather(1,1);sceneCacheValid=false;const auto storm=capture("weather-earth-typhoon-eye-and-bands");
        if(quiet==storm){std::cerr<<"Cyclone winds/clouds made no visible contribution\n";fail();}
        sceneCacheValid=false;const auto paused=capture("weather-earth-storm-paused");if(paused!=storm)fail();
        simulationTime=3600;const auto nextEarth=starSystem.Position(2,simulationTime);
        const auto nextRadial=starSystem.WeatherFrame(2,simulationTime)*normal;
        starSystem.camera=nextEarth+nextRadial*(Astronomy::earthRadiusAu*4);cameraFront=glm::vec3(-nextRadial);cameraUp=glm::vec3(starSystem.WeatherFrame(2,simulationTime)[1]);
        sceneCacheValid=false;if(capture("weather-earth-storm-one-hour")==storm)fail();
        starSystem.SetWeather(1,1);simulationTime=0;
        {
            const auto pole=glm::dvec3(starSystem.WeatherState(2,0).auroraAxis);
            const auto east=glm::normalize(glm::cross(glm::dvec3(0,0,1),pole)),north=glm::cross(pole,east);
            glm::dvec3 oval{};double darkest=2;
            for(int hemisphere:{-1,1})for(int i=0;i<16;++i){
                const double angle=i*glm::two_pi<double>()/16;
                const auto n=pole*(hemisphere*std::cos(.32))+(east*std::cos(angle)+north*std::sin(angle))*std::sin(.32);
                const double day=glm::dot(orientation*n,glm::normalize(-earth));if(day<darkest){darkest=day;oval=n;}
            }
            const auto radial=orientation*oval;starSystem.camera=earth+radial*(Astronomy::earthRadiusAu*2.2);
            cameraFront=glm::vec3(-radial);cameraUp=glm::vec3(orientation*east);
            starSystem.SetAurorasEnabled(false);sceneCacheValid=false;const auto off=capture("earth-polar-aurora-off");
            starSystem.SetAurorasEnabled(true);sceneCacheValid=false;const auto on=capture("earth-polar-aurora-night");
            size_t green=0;for(size_t p=0;p<on.size();p+=3)if(on[p+1]>off[p+1]+3 && on[p+1]>on[p]*1.2)++green;
            std::cout<<"Night-side polar aurora green emission pixels="<<green<<"\n";
            if(green<40){std::cerr<<"Eligible magnetic planet has no polar auroral emission\n";fail();}
        }
        StartGargantua();scenePanel.SetVisible(false);ResizeInterface();selectedTarget=-1;trajectories=false;simulationPlaying=false;
        blackHoleResolution=1;blackHole.SetWeather(1,1);blackHole.SetAtmospheresEnabled(true);
        const auto centre=BlackHole::PlanetPosition(0),toward=glm::normalize(-centre);
        const auto side=glm::normalize(glm::cross(toward,glm::dvec3(0,1,0)));
        landed=followPlanet=followSelected=followSpacecraft=false;
        fieldOfView=65;visionProfile=0;lensFlareStrength=0;exposureEv=0;
        const auto readLinearScene=[&](){
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,optics.SceneInputTexture());GLint w,h;
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
            std::vector<float> pixels(static_cast<size_t>(w)*h*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data());return pixels;
        };
        for(bool sunset:{false,true}){
            const auto radial=glm::normalize(toward*(sunset?.02:.5)+side*(sunset?.9998:.866));
            cameraPos=centre+radial*(blackHole.PlanetSurfaceRadius(glm::vec3(radial),0)+2/Astronomy::metresPerAu);
            cameraUp=glm::vec3(radial);cameraFront=glm::vec3(toward);
            KeyCallback(window,1200,0,GLFW_PRESS,0);sceneCacheValid=false;capture(sunset?"miller-sunset-gargantua-hdr":"miller-daylight-gargantua-hdr");
            if(!sunset){
                const auto throughCloud=readLinearScene();
                blackHole.SetWeatherCloudsEnabled(false);sceneCacheValid=false;capture("miller-disc-clear-air");
                const auto clearAir=readLinearScene();size_t extinguished=0;
                for(size_t p=0;p<clearAir.size();p+=4)if(clearAir[p+3]>2.9f
                    && std::max({clearAir[p],clearAir[p+1],clearAir[p+2]})>1
                    && throughCloud[p+3]>=2 && throughCloud[p+3]<2.0005f)++extinguished;
                std::cout<<"Bright disk pixels extinguished beyond the former one-percent cloud floor="<<extinguished<<"\n";
                if(extinguished<100){std::cerr<<"Cloud tracing retained its HDR transmission floor\n";fail();}
                blackHole.SetAtmospheresEnabled(false);sceneCacheValid=false;capture("miller-disc-without-atmosphere");
                blackHole.SetAccretionCloudsEnabled(false);sceneCacheValid=false;capture("miller-disc-plane-only");
                blackHole.SetAccretionCloudsEnabled(true);blackHole.SetAtmospheresEnabled(true);blackHole.SetWeatherCloudsEnabled(true);sceneCacheValid=false;
            }
            KeyCallback(window,1201,0,GLFW_PRESS,0);optics.SetAutoExposure(true);sceneCacheValid=false;
            capture(sunset?"miller-sunset-gargantua-pure":"miller-daylight-gargantua-pure");
            if(!sunset){
                cameraFront=glm::normalize(glm::vec3(-toward+radial*glm::dot(toward,radial)));
                optics.SetAutoExposure(true);sceneCacheValid=false;const auto horizon=capture("miller-pure-daylight-away-from-gargantua");
                double luminance=0;for(auto value:horizon)luminance+=value;luminance/=horizon.size();
                std::cout<<"Miller daylight away from Gargantua mean display="<<luminance<<"\n";
                if(luminance<8){std::cerr<<"Miller daytime remained dark when the bright disk left view\n";fail();}
            }
        }
        const auto mann=BlackHole::PlanetPosition(0,1),toGargantua=glm::normalize(-mann);
        const auto mannSide=glm::normalize(glm::cross(toGargantua,glm::dvec3(0,1,0)));
        for(bool ground:{false,true}){
            const auto radial=glm::normalize(toGargantua*(ground?.35:-.6)+mannSide*(ground?.93675:.8));
            cameraPos=mann+radial*(ground?blackHole.PlanetSurfaceRadius(glm::vec3(radial),0,1)+2/Astronomy::metresPerAu:InterstellarWorlds::Get(1).radiusEarth*Astronomy::earthRadiusAu*5);
            cameraFront=glm::vec3(ground?glm::normalize(toGargantua-radial*glm::dot(toGargantua,radial)+radial*.10):glm::normalize(-radial+toGargantua*.4));cameraUp=glm::vec3(radial);
            if(!ground)cameraUp=glm::vec3(mannSide);fieldOfView=ground?65:100;
            KeyCallback(window,1200,0,GLFW_PRESS,0);sceneCacheValid=false;capture(ground?"mann-frozen-daylight-hdr":"mann-orbit-hdr");
            KeyCallback(window,1201,0,GLFW_PRESS,0);optics.SetAutoExposure(true);sceneCacheValid=false;
            capture(ground?"mann-frozen-daylight-pure":"mann-orbit-pure");
        }
        KeyCallback(window,1200,0,GLFW_PRESS,0);
        const auto ship=blackHole.SpacecraftPosition();
        const auto shipFrame=glm::dmat3(blackHole.TargetRotation(9,simulationTime));
        fieldOfView=45;
        for(bool oblique:{false,true}){
            const auto offset=glm::normalize(shipFrame*(oblique?glm::dvec3(.65,.25,1):glm::dvec3(0,0,1)));
            cameraPos=ship+offset*((oblique?180.:140.)/Astronomy::metresPerAu);
            cameraFront=glm::normalize(glm::vec3(blackHole.ObservedSpacecraftPosition(cameraPos,simulationTime)-cameraPos));
            cameraUp=glm::vec3(shipFrame[1]);sceneCacheValid=false;
            capture(oblique?"endurance-module-details-oblique":"endurance-twelve-module-face");
        }
        std::cout<<"Weather checks "<<(success?"PASSED":"FAILED")<<"\n";
    };
    const auto checkLensing=[&](){
        StartGargantua();scenePanel.SetVisible(false);ResizeInterface();
        // A fixed geodesic-reference camera, independent of cinematic framing.
        cameraPos={0,0,BlackHole::HorizonRadius()*12.0};cameraFront={0,0,-1};cameraUp={0,1,0};
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
                const auto right=glm::normalize(glm::cross(glm::dvec3(cameraFront),glm::dvec3(cameraUp)));
                const auto up=glm::normalize(glm::cross(right,glm::dvec3(cameraFront)));
                const auto ray=KerrSpacetime::PastRay(toDisk*cameraPos,toDisk*glm::normalize(glm::dvec3(cameraFront)+right*u+up*v),BlackHole::HorizonRadius(),blackHole.Spin());
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
    const auto checkWormhole=[&](){
        StartSolarSystem();scenePanel.SetVisible(false);navigationPanel.Hide();ResizeInterface();
        simulationPlaying=false;simulationTime=0;trajectories=false;lensFlareStrength=0;visionProfile=0;
        navigationSelectionValid=false;fieldOfView=45;exposureEv=0;
        KeyCallback(window,1200,0,GLFW_PRESS,0);
        int mouth=-1;
        for(int i=0;i<static_cast<int>(starSystem.Planets().size());++i)if(starSystem.Planets()[i].wormhole)mouth=i;
        if(mouth<0){std::cerr<<"Saturn wormhole missing\n";fail();return;}
        const auto& body=starSystem.Planets()[mouth];
        const double mu=Astronomy::solarGravityAu*starSystem.Planets()[5].massEarth*5.97217e24/1.98847e30;
        const double period=glm::two_pi<double>()/OrbitalMechanics::MeanMotion(mu,body.orbit.semiMajorAxis);
        const auto relative=[&](double t){return starSystem.Position(mouth,t)-starSystem.Position(5,t);};
        if(body.parent!=5 || body.massEarth!=0 || !body.wormhole || body.atmosphere
            || glm::length(relative(0)-relative(period))>1e-12
            || glm::length(relative(0)-relative(period*.25))<.01){std::cerr<<"Wormhole Saturn-relative orbit failed\n";fail();}
        const double M=WormholePhysics::MassLength(),a=WormholePhysics::SpinLength();
        if(M+std::sqrt(M*M-a*a)>=1 || 1-2*M+a*a<=0){std::cerr<<"Wormhole throat is not outside the Kerr horizon\n";fail();}
        const auto entries=navigationCatalog.Find("Saturn Kerr Wormhole",0);
        if(entries.size()!=1 || !entries.front().wormhole || navigationCatalog.Parent(entries.front()).name!="Saturn"){
            std::cerr<<"Wormhole name/hierarchy lookup failed\n";fail();return;
        }
        const auto beforeSelection=starSystem.camera;
        ActivateNavigation(NavigationPanel::Action::Select,entries.front());
        if(starSystem.camera!=beforeSelection || starSystem.Selected()!=mouth){std::cerr<<"Wormhole selection moved the camera\n";fail();}
        ActivateNavigation(NavigationPanel::Action::Go,entries.front());
        const auto followOffset=starSystem.camera-starSystem.Position(mouth,0);
        if(std::abs(glm::length(followOffset)/WormholePhysics::ThroatRadiusAu()-12)>1e-6){std::cerr<<"Go to wormhole failed\n";fail();}
        starSystem.ToggleFollow(0);starSystem.Update(period*.25,cameraFront,cameraUp);
        if(glm::length(starSystem.camera-starSystem.Position(mouth,period*.25)-followOffset)>1e-12){std::cerr<<"Wormhole orbital follow failed\n";fail();}
        starSystem.SelectPlanet(mouth);starSystem.GoToSelected(0,cameraFront,cameraUp);
        const auto nonmaterialFront=cameraFront,nonmaterialUp=cameraUp;
        starSystem.ToggleRotationFollow(0);starSystem.Update(period*.25,cameraFront,cameraUp);
        if(glm::length(cameraFront-nonmaterialFront)>1e-6 || glm::length(cameraUp-nonmaterialUp)>1e-6){std::cerr<<"Wormhole acquired a material surface rotation\n";fail();}
        starSystem.SelectPlanet(mouth);
        starSystem.ToggleLanding(0,cameraFront,cameraUp);
        if(starSystem.IsLanded()){std::cerr<<"Wormhole acquired a planetary surface\n";fail();}
#ifdef _WIN32
        navigationPanel.Toggle(NavigationPanel::Mode::Finder,false);navigationPanel.SetQuery("wormhole");navigationPanel.Refresh();
        captureUi("ui-saturn-wormhole-finder",navigationPanel.NativeHandle());
        navigationScope=navigationCatalog.Object(-2,5,0);navigationPanel.Toggle(NavigationPanel::Mode::Overview,false);
        captureUi("ui-saturn-wormhole-hierarchy",navigationPanel.NativeHandle());navigationPanel.Hide();
#endif
        std::cout<<"Saturn wormhole orbit: "<<period/86400<<" days; selection, Go to, follow and hierarchy checked\n";
        const auto center=starSystem.Position(mouth,0);
        const auto offset=glm::normalize(glm::dvec3(0,.15,1))*WormholePhysics::ThroatRadiusAu()*12.0;
        starSystem.camera=center+offset;cameraFront=glm::normalize(glm::vec3(-offset));cameraUp={0,1,0};
        starSystem.SetWormholeEnabled(false);sceneCacheValid=false;const auto off=capture("saturn-wormhole-disabled");
        starSystem.SetWormholeEnabled(true);sceneCacheValid=false;const auto view=capture("saturn-wormhole-exterior");
        sceneCacheValid=false;const auto paused=capture("saturn-wormhole-paused");
        if(view!=paused){std::cerr<<"Paused wormhole view changed\n";fail();}
        size_t changed=0;for(size_t p=0;p<view.size();p+=3)if(std::abs(int(view[p])-int(off[p]))+std::abs(int(view[p+1])-int(off[p+1]))+std::abs(int(view[p+2])-int(off[p+2]))>8)++changed;
        if(changed<1000){std::cerr<<"Wormhole did not refract the scene\n";fail();}
        starSystem.SetWormholeDiagnostic(true);sceneCacheValid=false;const auto mask=capture("saturn-wormhole-ray-sides");
        size_t other=0,local=0,unfinished=0;
        for(size_t p=0;p<mask.size();p+=3){
            if(mask[p]<30 && mask[p+1]>100 && mask[p+2]<30)++other;
            if(mask[p]<30 && mask[p+1]<30 && mask[p+2]>100)++local;
            if(mask[p]>100 && mask[p+1]<30 && mask[p+2]>100)++unfinished;
        }
        std::cout<<"Wormhole rays: other exterior="<<other<<"; same exterior="<<local<<"; unresolved="<<unfinished<<"; refracted="<<changed<<"\n";
        if(other<5000 || local<5000 || unfinished>mask.size()/3*.01){std::cerr<<"Wormhole exterior classification failed\n";fail();}
        starSystem.camera=center+glm::dvec3(0,12*WormholePhysics::ThroatRadiusAu(),0);cameraFront={0,-1,0};cameraUp={0,0,-1};sceneCacheValid=false;
        const auto polar=capture("saturn-wormhole-spin-axis-status");size_t polarUnresolved=0,polarOther=0;
        for(size_t p=0;p<polar.size();p+=3){if(polar[p]>100 && polar[p+1]<30 && polar[p+2]>100)++polarUnresolved;if(polar[p]<30 && polar[p+1]>100 && polar[p+2]<30)++polarOther;}
        std::cout<<"Wormhole polar rays: other exterior="<<polarOther<<"; unresolved="<<polarUnresolved<<"\n";
        if(polarOther<5000 || polarUnresolved>polar.size()/3*.01){std::cerr<<"Spin-axis coordinate singularity\n";fail();}
        starSystem.SetWormholeDiagnostic(false);
        // Put Saturn directly in front of the mouth. No lens operation may
        // overwrite the opaque disk in front of the local metric patch.
        const auto saturn=starSystem.Position(5,0);
        const auto toward=glm::normalize(center-saturn);
        starSystem.camera=saturn-toward*starSystem.Planets()[5].radiusEarth*Astronomy::earthRadiusAu*3.0;
        cameraFront=glm::vec3(toward);cameraUp={0,1,0};
        starSystem.SetWormholeEnabled(false);sceneCacheValid=false;const auto foreground=capture("saturn-wormhole-occluded-off");
        starSystem.SetWormholeEnabled(true);sceneCacheValid=false;const auto blocked=capture("saturn-wormhole-occluded-on");
        int w,h;glfwGetFramebufferSize(window,&w,&h);
        for(int y=h/2-20;y<h/2+20;++y)for(int x=w/2-20;x<w/2+20;++x){const auto p=(static_cast<size_t>(y)*w+x)*3;for(int c=0;c<3;++c)if(foreground[p+c]!=blocked[p+c]){std::cerr<<"Wormhole overwrote foreground Saturn\n";fail();y=h;x=w;break;}}
        starSystem.camera=center+offset*.19;cameraFront=glm::normalize(glm::vec3(-offset));cameraUp={0,1,0};sceneCacheValid=false;
        capture("saturn-wormhole-approach");
        starSystem.camera=starSystem.Position(2,0)+glm::dvec3(0,0,Astronomy::earthRadiusAu*5);
        cameraFront={0,0,-1};cameraUp={0,1,0};
        starSystem.SetWormholeEnabled(false);sceneCacheValid=false;const auto distant=capture("saturn-wormhole-distant-off");
        starSystem.SetWormholeEnabled(true);sceneCacheValid=false;if(distant!=capture("saturn-wormhole-distant-on")){std::cerr<<"Distant wormhole changed Earth view\n";fail();}
        const auto paired=navigationCatalog.Find("Gargantua Wormhole",0);
        if(paired.size()!=1 || paired.front().system!=0 || paired.front().body!=12 || !paired.front().wormhole){std::cerr<<"Paired mouth lookup failed\n";fail();}
        const double throat=WormholePhysics::ThroatRadiusAu();
        const auto saturnMouth=StarSystem::SaturnWormholePosition(0),gargantuaMouth=BlackHole::GargantuaWormholePosition(0);
        const auto transfer=WormholePhysics::SaturnToGargantuaRotation();
        starSystem.camera=saturnMouth+glm::dvec3(0,0,-throat*2);starSystem.speedAu=throat;
        cameraFront={0,0,-1};cameraUp={0,1,0};wormholeCooldown=0;
        UpdateWormholeTransit(saturnMouth+glm::dvec3(0,0,throat*2),0,.016f);
        if(inStarSystem || !showBlackHole || glm::length(cameraPos-gargantuaMouth)>6*throat || cameraSpeed!=throat
            || glm::length(cameraFront-glm::vec3(transfer*glm::dvec3(0,0,-1)))>1e-5){std::cerr<<"Saturn to Gargantua camera transfer failed\n";fail();}
        capture("wormhole-arrival-gargantua");
        cameraPos=gargantuaMouth+glm::dvec3(0,0,throat*12);cameraFront={0,0,-1};cameraUp={0,1,0};sceneCacheValid=false;
        capture("gargantua-wormhole-saturn-destination");
        cameraPos=gargantuaMouth+glm::dvec3(0,0,-throat*2);wormholeCooldown=0;
        UpdateWormholeTransit(gargantuaMouth+glm::dvec3(0,0,throat*2),0,.016f);
        if(!inStarSystem || !starSystem.IsSolarSystem() || glm::length(starSystem.camera-saturnMouth)>6*throat || starSystem.speedAu!=throat
            || glm::length(cameraFront-glm::vec3(glm::transpose(transfer)*glm::dvec3(0,0,-1)))>1e-5){std::cerr<<"Gargantua to Saturn camera transfer failed\n";fail();}
        capture("wormhole-arrival-saturn");
        starSystem.camera=saturnMouth+glm::dvec3(0,0,-throat*2);
        UpdateWormholeTransit(saturnMouth+glm::dvec3(0,0,throat*2),0,.016f);
        if(!inStarSystem){std::cerr<<"Wormhole arrival cooldown did not prevent bounce\n";fail();}
        wormholeCooldown=0;
        std::cout<<"Wormhole checks "<<(success?"PASSED":"FAILED")<<": "<<failedLines.size()<<" failed locations\n";
    };
    if(wormholeOnly){checkWormhole();return success;}
    if(lensingOnly){checkLensing();return success;}
    const auto checkGalaxy=[&](){
        StartSolarSystem();scenePanel.SetVisible(false);ResizeInterface();
        showWireframe=false;glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
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
        // Isolate galactic light from stars/planets so a backwards projected
        // image cannot hide inside a merely nonempty scene capture.
        MilkyWaySky isolatedSky;GalaxyBackground isolatedGalaxies;
        if(!isolatedSky.Initialize() || !isolatedGalaxies.Initialize()){fail();return;}
        auto spiralPath=std::filesystem::path("assets/galaxy.png");
#ifdef _WIN32
        wchar_t executablePath[32768];GetModuleFileNameW(nullptr,executablePath,32768);
        spiralPath=std::filesystem::path(executablePath).parent_path()/spiralPath;
#endif
        const auto porousSpiral=AssetManager::LoadTexture(spiralPath.string().c_str(),true);
        GLint sw,sh;glBindTexture(GL_TEXTURE_2D,porousSpiral);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&sw);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&sh);
        std::vector<float> spiralPixels(static_cast<size_t>(sw)*sh*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,spiralPixels.data());
        size_t alphaSamples=0,openGaps=0,translucent=0;
        for(int y=0;y<sh;++y)for(int x=0;x<sw;++x){const double dx=(x+.5)/sw*2-1,dy=(y+.5)/sh*2-1,r=std::hypot(dx,dy);
            if(r<.2 || r>.8)continue;const float a=spiralPixels[(static_cast<size_t>(y)*sw+x)*4+3];
            ++alphaSamples;if(a<.25)++openGaps;if(a>=.25 && a<.95)++translucent;
        }
        std::cout<<"Spiral interior alpha: open gaps="<<openGaps<<" translucent="<<translucent<<" samples="<<alphaSamples<<"\n";
        if(!alphaSamples || openGaps<alphaSamples*.10 || translucent<alphaSamples*.10){std::cerr<<"Spiral PNG became an opaque filled disk\n";fail();}
        glDeleteTextures(1,&porousSpiral);
        const auto isolatedView=[&](const char* name,const glm::dvec3& modelCamera,const glm::dvec3& aim,bool backdrop=false,float limit=10.2f,bool allowDark=false,bool resetSky=true){
            const auto world=MilkyWayModel::ToWorldAu(modelCamera);
            const auto front=glm::normalize(glm::vec3(MilkyWayModel::Orientation()*(aim-modelCamera)));
            auto up=glm::vec3(MilkyWayModel::Orientation()[1]);if(std::abs(glm::dot(front,up))>.98f)up=glm::vec3(MilkyWayModel::Orientation()[2]);
            const auto vp=glm::perspective(glm::radians(65.f),static_cast<float>(viewWidth)/viewHeight,.00001f,20000.f)*glm::lookAt(glm::vec3(0),front,up);
            if(resetSky)isolatedSky.Invalidate();
            return capture(name,false,[&](){
                glViewport(0,0,viewWidth,viewHeight);optics.BeginFrame();glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                if(backdrop)isolatedGalaxies.Render(vp,world,limit);else isolatedSky.Render(vp,world);
                optics.Render(0,65,0,true,0,false,.016f);
#ifdef RENDER_ENGINE_DX12
                NativeDX12::PresentScene();
#else
                glfwSwapBuffers(window);
#endif
            },allowDark);
        };
        // Measure the actual sprite over a coloured sky, with a bright source
        // underneath its outskirts. A soft halo must survive without covering
        // background light or retaining a plate-like perimeter.
        {
            const auto& m31=NearbyGalaxies::Catalog[0];const float fov=static_cast<float>(m31.majorArcmin/60*2.4);
            const auto front=glm::vec3(m31.Direction()),up=glm::vec3(m31.Frame()[0]);
            const auto vp=glm::perspective(glm::radians(fov),static_cast<float>(viewWidth)/viewHeight,.00001f,20000.f)*glm::lookAt(glm::vec3(0),front,up);
            const double ry=std::tan(glm::radians(m31.majorArcmin/120))/std::tan(glm::radians(static_cast<double>(fov))*.5)*viewHeight*.5;
            const double rx=ry*m31.minorArcmin/m31.majorArcmin;
            const int markerX=viewWidth/2,markerY=viewHeight/2+static_cast<int>(ry*.85);
            capture("galaxy-translucent-edge-sky",false,[&](){
                glViewport(0,0,viewWidth,viewHeight);optics.BeginFrame();glClearColor(.01f,.015f,.02f,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                glEnable(GL_SCISSOR_TEST);glScissor(markerX,markerY,1,1);glClearColor(.6f,.7f,.8f,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
                isolatedGalaxies.Render(vp,glm::dvec3(0),10.2f);optics.Render(0,fov,0,true,0,false,.016f);
#ifdef RENDER_ENGINE_DX12
                NativeDX12::PresentScene();
#else
                glfwSwapBuffers(window);
#endif
            });
            glBindTexture(GL_TEXTURE_2D,optics.SceneInputTexture());std::vector<float> pixels(static_cast<size_t>(viewWidth)*viewHeight*4);
            glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data());
            double flux[3]={};size_t samples[3]={};
            for(int y=0;y<viewHeight;++y)for(int x=0;x<viewWidth;++x){
                if(std::abs(x-markerX)<3 && std::abs(y-markerY)<3)continue;
                const double r=std::hypot((x+.5-viewWidth*.5)/rx,(y+.5-viewHeight*.5)/ry);
                const int bin=r<.15?0:r>.45 && r<.6?1:r>.8 && r<.92?2:-1;if(bin<0)continue;
                const auto p=(static_cast<size_t>(y)*viewWidth+x)*4;
                flux[bin]+=std::max(0.f,pixels[p]-.01f)+std::max(0.f,pixels[p+1]-.015f)+std::max(0.f,pixels[p+2]-.02f);++samples[bin];
            }
            for(int i=0;i<3;++i)flux[i]/=std::max<size_t>(samples[i],1);
            const auto marker=(static_cast<size_t>(markerY)*viewWidth+markerX)*4;
            std::cout<<"Galaxy core/mid/outskirts signal="<<flux[0]<<" / "<<flux[1]<<" / "<<flux[2]<<"\n";
            if(flux[0]<.02 || flux[2]<=0 || flux[2]>flux[0]*.08 || flux[1]<flux[2]*3
                || pixels[marker]<.595 || pixels[marker+1]<.695 || pixels[marker+2]<.795){std::cerr<<"Galaxy outskirts failed feathering/background transmission\n";fail();}
        }
        for(const auto p:std::array<glm::dvec3,3>{{{18.2,.68,0},{0,8,0},{0,-8,0}}}){
            const auto away=p.x>0?glm::dvec3(1,0,0):glm::dvec3(0,p.y>0?1:-1,0);
            const auto image=isolatedView(("mw-no-mirror-"+std::to_string(int(p.x>0?0:p.y>0?1:2))).c_str(),p,p+away,false,10.2f,true);
            if(*std::max_element(image.begin(),image.end())>1){std::cerr<<"Milky Way projected behind the observer\n";fail();}
        }
        std::vector<unsigned char> previousAngle;double previousFlux=0;
        for(int degrees=90;degrees>=0;degrees-=5){
            const double angle=glm::radians(static_cast<double>(degrees));
            const auto image=isolatedView(("mw-inclination-"+std::to_string(degrees)).c_str(),{0,32*std::sin(angle),32*std::cos(angle)},{0,0,0});
            double flux=0;for(auto value:image)flux+=value;
            if(!previousAngle.empty()){
                const double ratio=flux/std::max(previousFlux,1.0);
                std::cout<<"Milky Way inclination "<<degrees<<" integrated display ratio="<<ratio<<"\n";
                if(ratio<.65 || ratio>1.35){std::cerr<<"Abrupt top/side galactic transition\n";fail();}
            }
            previousFlux=flux;previousAngle=image;
        }
        const auto distant=isolatedView("galaxies-outside-milky-way",{0,80,0},{0,80,-100},true,12.f);
        isolatedView("galaxies-default-magnitude",{0,80,0},{0,80,-100},true,7.f);
        if(distant==isolatedView("galaxies-magnitude-low",{0,80,0},{0,80,-100},true,4.f,true)){std::cerr<<"Galaxy magnitude filtering had no effect\n";fail();}
        if(distant!=isolatedView("galaxies-outside-paused",{0,80,0},{0,80,-100},true,12.f)){std::cerr<<"Static distant galaxies changed\n";fail();}
        for(int i=1;i<=3;++i){
            const auto group=Universe::GalaxyAt(i);
            for(int edge=0;edge<=1;++edge){
                const auto position=group.centerAu+group.rotation[edge?0:1]*group.axesAu.x*6.0;
                const auto image=isolatedView(("nearby-galaxy-"+std::to_string(i)+(edge?"-edge":"-face")).c_str(),MilkyWayModel::ToModelKpc(position),MilkyWayModel::ToModelKpc(group.centerAu),true,12.f);
                size_t centerLit=0;
                for(int y=viewHeight/2-20;y<viewHeight/2+20;++y)for(int x=viewWidth/2-20;x<viewWidth/2+20;++x){const auto p=(static_cast<size_t>(y)*viewWidth+x)*3;if(std::max({image[p],image[p+1],image[p+2]})>20)++centerLit;}
                std::cout<<"Nearby galaxy "<<i<<(edge?" edge":" face")<<" center samples="<<centerLit<<"\n";
                if(centerLit<30){std::cerr<<"Nearby galaxy disappeared with inclination\n";fail();}
            }
        }
        // Exercise real translation, then stopping, without resetting the
        // cache/motion sample between frames. The authored views above reset
        // independently so camera teleports never masquerade as flight.
        isolatedSky.Invalidate();
        glm::dvec3 adaptivePosition(0,.35,32);
        auto adaptiveWorld=MilkyWayModel::ToWorldAu(adaptivePosition);
        isolatedSky.ObserveCamera(adaptiveWorld,.016,65,viewHeight);
        isolatedView("mw-adaptive-stationary-start",adaptivePosition,{0,0,0},false,10.2f,false,false);
        adaptivePosition.x+=.03;adaptiveWorld=MilkyWayModel::ToWorldAu(adaptivePosition);
        isolatedSky.ObserveCamera(adaptiveWorld,.016,65,viewHeight);
        isolatedView("mw-adaptive-motion-start",adaptivePosition,{0,0,0},false,10.2f,false,false);
        if(!isolatedSky.BakedMovingQuality() || isolatedSky.CacheWidth()!=1024 || isolatedSky.RaySteps()!=48){std::cerr<<"Fast translation did not choose preview quality\n";fail();}
        glfwSetTime(isolatedSky.CacheTimestamp()+.12);
        isolatedSky.ObserveCamera(adaptiveWorld,.016,65,viewHeight);
        const auto preview=isolatedView("mw-adaptive-moving-preview",adaptivePosition,{0,0,0},false,10.2f,false,false);
        glfwSetTime(isolatedSky.CacheTimestamp()+.30);
        isolatedSky.ObserveCamera(adaptiveWorld,.016,65,viewHeight);
        if(!isolatedSky.NeedsFrame(adaptiveWorld)){std::cerr<<"Stopping failed to wake sky refinement\n";fail();}
        isolatedView("mw-adaptive-refinement-start",adaptivePosition,{0,0,0},false,10.2f,false,false);
        if(isolatedSky.BakedMovingQuality() || isolatedSky.CacheWidth()!=2048 || isolatedSky.RaySteps()!=192){std::cerr<<"Stationary sky failed to choose refined quality\n";fail();}
        glfwSetTime(isolatedSky.CacheTimestamp()+.35);
        isolatedSky.ObserveCamera(adaptiveWorld,.016,65,viewHeight);
        const auto refined=isolatedView("mw-adaptive-stationary-refined",adaptivePosition,{0,0,0},false,10.2f,false,false);
        const auto detailEnergy=[&](const std::vector<unsigned char>& image){
            // Measure the resolved dust lane, not the enormous faint halo:
            // 8-bit quantization of that smooth halo otherwise dominates a
            // whole-frame Laplacian despite containing no resolved structure.
            const int halfHeight=std::max(3,viewHeight/175);
            double sum=0;for(int y=viewHeight/2-halfHeight;y<viewHeight/2+halfHeight;++y)for(int x=viewWidth/4;x<viewWidth*3/4;++x){
                const auto p=(static_cast<size_t>(y)*viewWidth+x)*3;
                const double center=image[p]+image[p+1]+image[p+2];
                const auto intensity=[&](size_t i){return image[i]+image[i+1]+image[i+2];};
                const double laplacian=intensity(p-3)+intensity(p+3)+intensity(p-viewWidth*3)+intensity(p+viewWidth*3)-4*center;
                sum+=laplacian*laplacian;
            }return sum;
        };
        std::cout<<"Adaptive galaxy: preview 1024x512 / 48 steps; refined 2048x1024 / 192 steps; detail energy "<<detailEnergy(preview)<<" -> "<<detailEnergy(refined)<<"\n";
        if(preview==refined || detailEnergy(refined)<=detailEnergy(preview)*1.2 || isolatedSky.NeedsFrame(adaptiveWorld)){std::cerr<<"Stationary sky failed to sharpen and return to idle\n";fail();}
        const auto refinedBuilds=isolatedSky.CacheBuilds();
        isolatedView("mw-adaptive-refined-rotation",adaptivePosition,{.3,0,0},false,10.2f,false,false);
        if(isolatedSky.CacheBuilds()!=refinedBuilds || isolatedSky.MovingQuality())fail();
        isolatedSky.ObserveCamera(adaptiveWorld+glm::dvec3(1,0,0),.016,65,viewHeight);
        if(isolatedSky.MovingQuality()){std::cerr<<"Subpixel navigation degraded galaxy quality\n";fail();}
        // Isolate Andromeda's diffuse light: stars cannot conceal the old
        // 1.25-radius disappearance, and a rear-facing billboard cannot pass.
        isolatedGalaxies.SetAdaptiveQuality(false);
        const auto& m31=NearbyGalaxies::Catalog[0];
        const auto closeView=[&](const std::string& name,const glm::dvec3& local,const glm::dvec3& aim,bool allowDark=false,bool localVolumes=true){
            const auto world=m31.Center()+m31.Frame()*local*m31.Radius();
            const auto front=glm::normalize(glm::vec3(m31.Frame()*(aim-local)));
            auto up=glm::vec3(m31.Frame()[1]);if(std::abs(glm::dot(front,up))>.98f)up=glm::vec3(m31.Frame()[2]);
            const auto vp=glm::perspective(glm::radians(65.f),static_cast<float>(viewWidth)/viewHeight,.00001f,20000.f)*glm::lookAt(glm::vec3(0),front,up);
            return capture(name.c_str(),false,[&](){
                glViewport(0,0,viewWidth,viewHeight);optics.BeginFrame();glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                isolatedGalaxies.Render(vp,world,10.2f,localVolumes);optics.Render(0,65,0,true,0,false,.016f);
#ifdef RENDER_ENGINE_DX12
                NativeDX12::PresentScene();
#else
                glfwSwapBuffers(window);
#endif
            },allowDark);
        };
        const auto incoming=glm::transpose(m31.Frame())*(-m31.Direction());
        for(const double radius:{4.,3.25,3.,2.75,2.25,1.3,1.,.5}){
            const auto image=closeView("andromeda-approach-"+std::to_string(radius),incoming*radius,glm::dvec3(0));
            size_t lit=0;double flux=0;for(size_t i=0;i<image.size();i+=3){const auto peak=std::max({image[i],image[i+1],image[i+2]});if(peak>12)++lit;flux+=peak;}
            std::cout<<"Andromeda approach radius="<<radius<<" diffuse pixels="<<lit<<" mean="<<flux/(image.size()/3)<<"\n";
            if(lit<100){std::cerr<<"Andromeda disappeared on approach\n";fail();}
        }
        const auto inside=glm::dvec3(.55,.008,.05);
        closeView("andromeda-diffuse-interior",inside,{0,0,0});
        const auto cachedBuilds=isolatedGalaxies.VolumeBuilds();
        closeView("andromeda-interior-look-around",inside,{0,.15,.4});
        if(isolatedGalaxies.VolumeBuilds()!=cachedBuilds || isolatedGalaxies.NeedsFrame()){
            std::cerr<<"Looking around rebaked the galaxy volume or prevented idle reuse\n";fail();
        }
        // The background-only remote sky must not invalidate the local cache.
        closeView("andromeda-remote-sky",inside,{0,0,0},false,false);
        closeView("andromeda-interior-cache-reuse",inside,{0,0,0});
        if(isolatedGalaxies.VolumeBuilds()!=cachedBuilds)fail();
        const auto away=closeView("andromeda-forward-only",{0,2.2,0},{0,3.2,0},true);
        const int cx=viewWidth/2,cy=viewHeight/2;
        for(int y=cy-30;y<cy+30;++y)for(int x=cx-30;x<cx+30;++x){const auto i=(static_cast<size_t>(y)*viewWidth+x)*3;
            if(std::max({away[i],away[i+1],away[i+2]})>2){std::cerr<<"Andromeda reflected onto its rear hemisphere\n";fail();y=cy+30;break;}}
        for(const double boundary:{2.25,3.25}){
            const auto before=closeView("andromeda-blend-before-"+std::to_string(boundary),incoming*(boundary-.0001),glm::dvec3(0));
            const auto after=closeView("andromeda-blend-after-"+std::to_string(boundary),incoming*(boundary+.0001),glm::dvec3(0));
            double delta=0;for(size_t i=0;i<before.size();++i)delta+=std::abs(int(before[i])-int(after[i]));delta/=before.size();
            std::cout<<"Andromeda blend boundary="<<boundary<<" mean RGB delta="<<delta<<"\n";
            if(delta>1){std::cerr<<"Discontinuous Andromeda approach blend\n";fail();}
        }
        const auto& m32=NearbyGalaxies::Catalog[5];
        const auto toM31=[&](const glm::dvec3& world){return glm::transpose(m31.Frame())*(world-m31.Center())/m31.Radius();};
        closeView("andromeda-m32-close-volumes",toM31(m32.Center()+m32.Frame()[1]*m32.Radius()),toM31(m32.Center()));
        if(isolatedGalaxies.VolumeWidth(4)==0 || isolatedGalaxies.VolumeWidth(9)==0){std::cerr<<"Andromeda/companion volume handoff lost a host\n";fail();}
        // Fast translation uses the preview; stopping refines exactly once,
        // finishes the fade and returns the paused scene to idle reuse.
        isolatedGalaxies.SetAdaptiveQuality(true);
        const auto at=[&](const glm::dvec3& p){return m31.Center()+m31.Frame()*p*m31.Radius();};
        isolatedGalaxies.ObserveCamera(at(inside),.1);
        const auto flight=inside+glm::dvec3(.005,0,0);
        isolatedGalaxies.ObserveCamera(at(flight),.1);
        closeView("andromeda-moving-volume",flight,{0,0,0});
        if(isolatedGalaxies.VolumeWidth(4)!=512){std::cerr<<"Fast galaxy travel missed preview quality\n";fail();}
        glfwSetTime(glfwGetTime()+.5);isolatedGalaxies.ObserveCamera(at(flight),.016);
        closeView("andromeda-refinement-start",flight,{0,0,0});
        if(isolatedGalaxies.VolumeWidth(4)!=1024 || !isolatedGalaxies.NeedsFrame())fail();
        // Translation while the first fade is unfinished must remain pending.
        const auto pendingLocal=flight+glm::dvec3(.0003,0,0);
        isolatedGalaxies.ObserveCamera(at(pendingLocal),1.0);
        const auto refinementBuilds=isolatedGalaxies.VolumeBuilds();
        glfwSetTime(glfwGetTime()+.4);
        closeView("andromeda-refinement-terminal",pendingLocal,{0,0,0});
        if(isolatedGalaxies.VolumeBuilds()!=refinementBuilds || !isolatedGalaxies.NeedsFrame())fail();
        closeView("andromeda-pending-translation",pendingLocal,{0,0,0});
        if(isolatedGalaxies.VolumeBuilds()!=refinementBuilds+1)fail();
        glfwSetTime(glfwGetTime()+.4);
        closeView("andromeda-refined-idle",pendingLocal,{0,0,0});
        if(isolatedGalaxies.NeedsFrame()){std::cerr<<"Settled galaxy volume prevented idle reuse\n";fail();}
        isolatedSky.Release();isolatedGalaxies.Release();sceneCacheValid=false;
        const auto difference=[](const std::vector<unsigned char>& a,const std::vector<unsigned char>& b){
            double sum=0;if(a.size()!=b.size())return 1e9;for(size_t i=0;i<a.size();++i)sum+=std::abs(int(a[i])-int(b[i]));return sum/a.size();
        };
        struct Boundary {double value;bool radial;};
        int pair=0;
        for(const auto boundary:std::array<Boundary,6>{{{.2,false},{.4,false},{1.5,false},{2.6,false},{12.5,true},{20,true}}}){
            const auto p=boundary.radial?glm::dvec3(boundary.value,.8,0):glm::dvec3(7,boundary.value,0);
            const auto delta=boundary.radial?glm::dvec3(.0001,0,0):glm::dvec3(0,.0001,0);
            const auto before=galaxyView(p-delta,{0,0,0},("mw-lod-boundary-"+std::to_string(pair)+"-before").c_str());
            const auto after=galaxyView(p+delta,{0,0,0},("mw-lod-boundary-"+std::to_string(pair)+"-after").c_str());
            const double error=difference(before,after);std::cout<<"Milky Way LOD boundary "<<pair++<<" mean RGB delta="<<error<<"\n";
            if(error>1.0){std::cerr<<"Discontinuous Milky Way LOD boundary\n";fail();}
        }
        galaxyView({8.277,.2,0},{0,0,0},"mw-cache-flight-start");
        const double startTime=glfwGetTime();
        glfwSetTime(0); // Hold the refresh pending independently of GPU/readback duration.
        const auto newWorld=MilkyWayModel::ToWorldAu({8.277,.85,0});
        starSystem.camera=newWorld-systemOriginAu;
        const auto pending=capture("mw-cache-flight-pending");
        const auto count=blackHole.MilkyWayCacheBuilds();
        glfwSetTime(startTime+5.0);const auto refresh=capture("mw-cache-refresh-start");
        if(blackHole.MilkyWayCacheBuilds()!=count+1){std::cerr<<"Pending translation refresh was lost\n";fail();}
        const double refreshTime=blackHole.MilkyWayCacheTimestamp();
        glfwSetTime(refreshTime+.16);std::cout<<"Cache midpoint blend="<<blackHole.MilkyWayCacheBlend()<<"\n";const auto midpoint=capture("mw-cache-refresh-midpoint");
        std::cout<<"Actual cache draw blend="<<blackHole.MilkyWayLastDrawBlend()<<"; age="<<glfwGetTime()-blackHole.MilkyWayCacheTimestamp()<<"; bakes="<<blackHole.MilkyWayCacheBuilds()<<"\n";
        glfwSetTime(refreshTime+.50);const auto settled=capture("mw-cache-refresh-settled");
        std::cout<<"Galaxy cache crossfade: refresh step="<<difference(pending,refresh)<<"; midpoint delta="<<difference(refresh,midpoint)<<"; remaining="<<difference(midpoint,settled)<<"; total change="<<difference(refresh,settled)<<"\n";
        if(difference(pending,refresh)>.15 || difference(refresh,settled)<.02 || midpoint==refresh || midpoint==settled){std::cerr<<"Volume cache did not crossfade\n";fail();}
        float idleDelta=.016f;Update(idleDelta);
        if(!idleFrame){std::cerr<<"Settled sky prevented paused-frame reuse\n";fail();}
        const auto settledBuilds=blackHole.MilkyWayCacheBuilds();
        cameraFront=glm::normalize(cameraFront+cameraUp*.03f);capture("mw-cache-rotation-reuse");
        if(blackHole.MilkyWayCacheBuilds()!=settledBuilds)fail();
        starSystem.camera=MilkyWayModel::ToWorldAu({8.277,1.0,0})-systemOriginAu;
        glfwSetTime(glfwGetTime()+1);capture("mw-cache-continued-flight-start");
        const auto flightBuilds=blackHole.MilkyWayCacheBuilds();
        starSystem.camera=MilkyWayModel::ToWorldAu({8.277,1.2,0})-systemOriginAu;
        glfwSetTime(blackHole.MilkyWayCacheTimestamp()+.6);capture("mw-cache-continued-flight-terminal");
        if(blackHole.MilkyWayCacheBuilds()!=flightBuilds || blackHole.MilkyWayLastDrawBlend()!=1.f){std::cerr<<"Continued travel replaced an unfinished fade\n";fail();}
        capture("mw-cache-continued-flight-next-refresh");
        if(blackHole.MilkyWayCacheBuilds()!=flightBuilds+1){std::cerr<<"Continued travel lost its next refresh\n";fail();}
        glfwSetTime(blackHole.MilkyWayCacheTimestamp()+.6);capture("mw-cache-continued-flight-settled");
        Update(idleDelta);if(!idleFrame)fail();
        std::cout<<"Galaxy checks "<<(success?"PASSED":"FAILED")<<": "<<captures<<" scene captures; "<<failedLines.size()<<" assertion locations failed\n";
    };
    const auto checkSagittarius=[&](){
        StartSolarSystem();scenePanel.SetVisible(false);ResizeInterface();SelectCatalogue(GalacticCores::firstId);EnterSelectedSystem(true);
        simulationPlaying=false;simulationTime=0;trajectories=false;visionProfile=0;fieldOfView=55;lensFlareStrength=0;exposureEv=0;SetMagnitudeLimit(7);
        KeyCallback(window,1200,0,GLFW_PRESS,0);
        const auto flow=capture("sgr-a-hot-flow-hdr");
        size_t blue=0;for(size_t p=0;p<flow.size();p+=3)if(flow[p+2]>flow[p]*1.5 && flow[p+2]>50)++blue;
        if(blue<1000){std::cerr<<"Sagittarius flow has no resolved blue volume\n";fail();}
        if(flow!=capture("sgr-a-hot-flow-paused")){std::cerr<<"Paused Sagittarius plasma changed\n";fail();}
        simulationTime=120;const auto moved=capture("sgr-a-hot-flow-120-seconds");if(moved==flow){std::cerr<<"Sagittarius flow did not advance with simulation time\n";fail();}
        blackHole.SetHotFlowDiagnostic(true);sceneCacheValid=false;capture("sgr-a-flow-transmission");
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,optics.SceneInputTexture());GLint w,h;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
        std::vector<float> transmission(static_cast<size_t>(w)*h*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,transmission.data());
        float minimum=1;size_t measured=0;
        for(int y=h/3;y<h*2/3;++y)for(int x=w/3;x<w*2/3;++x){const auto p=(static_cast<size_t>(y)*w+x)*4;
            if(transmission[p]>.5f && std::abs(transmission[p]-transmission[p+1])<.0001f && std::abs(transmission[p]-transmission[p+2])<.0001f){minimum=std::min(minimum,transmission[p]);++measured;}}
        std::cout<<"Sgr A flow: blue pixels="<<blue<<"; sampled transmission minimum="<<minimum<<"; samples="<<measured<<"\n";
        if(measured<1000 || minimum<.75f){std::cerr<<"Sagittarius flow became optically thick\n";fail();}
        blackHole.SetHotFlowDiagnostic(false);blackHole.SetLensingDiagnostic(true);sceneCacheValid=false;
        const auto status=capture("sgr-a-ray-status");size_t disk=0,unresolved=0;
        for(size_t p=0;p<status.size();p+=3){if(status[p]<30 && status[p+1]>100 && status[p+2]<30)++disk;if(status[p]>100 && status[p+1]<30 && status[p+2]>100)++unresolved;}
        if(disk>50 || unresolved>status.size()/3*.03){std::cerr<<"Sagittarius used an opaque disk or exhausted excessive rays\n";fail();}
        blackHole.SetLensingDiagnostic(false);sceneCacheValid=false;KeyCallback(window,1201,0,GLFW_PRESS,0);optics.SetAutoExposure(true);
        capture("sgr-a-hot-flow-auto");KeyCallback(window,1200,0,GLFW_PRESS,0);
        std::cout<<"Sagittarius checks "<<(success?"PASSED":"FAILED")<<"\n";
    };
    const auto checkNatureJourney=[&](){
        StartSolarSystem();scenePanel.SetVisible(false);ResizeInterface();simulationTime=0;simulationPlaying=false;
        trajectories=false;lensFlareStrength=0;fieldOfView=75;visionProfile=0;exposureEv=0;KeyCallback(window,1200,0,GLFW_PRESS,0);
        const auto earth=starSystem.Position(2,0);const auto rotation=starSystem.WeatherFrame(2,0);const auto& body=starSystem.Planets()[2];
        const PlanetVegetation::Profile profile{body.seed,body.radiusEarth*Astronomy::earthRadiusAu*Astronomy::metresPerAu,body.temperature,body.seaLevel,body.life};
        std::array<bool,3> seen{};
        for(int i=0;i<24000 && !std::all_of(seen.begin(),seen.end(),[](bool b){return b;});++i){
            const double y=1-2*(i+.5)/24000,angle=i*2.399963229728653;
            const glm::dvec3 n(std::sqrt(1-y*y)*std::cos(angle),y,std::sqrt(1-y*y)*std::sin(angle));
            const auto habitat=PlanetVegetation::Sample(profile,n);
            if(!habitat.suitable || habitat.grass<.4 || habitat.forest<.08 || seen[habitat.species] || glm::dot(rotation*n,glm::normalize(-earth))<.2)continue;
            const auto radial=rotation*n;const auto east=glm::normalize(glm::cross(glm::dvec3(0,1,0),radial));
            starSystem.camera=earth+radial*((profile.radiusMetres*(1+habitat.elevation)+3)/Astronomy::metresPerAu);
            cameraUp=glm::vec3(radial);cameraFront=glm::normalize(glm::vec3(east)-cameraUp*.06f);
            starSystem.SetVegetationEnabled(true);sceneCacheValid=false;float dt=.016f;Update(dt);
            if(starSystem.VisibleTrees()==0 || starSystem.VisibleGrassSprites()==0)continue;
            const auto name="life-earth-species-"+std::to_string(habitat.species);
            const auto on=capture(name.c_str());const auto trees=starSystem.VisibleTrees(),grass=starSystem.VisibleGrassSprites();
            starSystem.SetVegetationEnabled(false);sceneCacheValid=false;const auto off=capture((name+"-off").c_str());
            size_t change=0;for(size_t p=0;p<on.size();p+=3)if(std::abs(int(on[p])-off[p])+std::abs(int(on[p+1])-off[p+1])+std::abs(int(on[p+2])-off[p+2])>5)++change;
            std::cout<<name<<": trees="<<trees<<" grass="<<grass<<" changed="<<change<<" pixels\n";
            if(change<50 || trees>2000 || grass>6000)fail();seen[habitat.species]=true;starSystem.SetVegetationEnabled(true);
        }
        if(!std::all_of(seen.begin(),seen.end(),[](bool b){return b;})){std::cerr<<"Missing visible living-planet tree species / grass\n";fail();}
        for(int id=GalacticCores::firstId;id<GalacticCores::firstId+GalacticCores::count;++id){
            SelectCatalogue(id);EnterSelectedSystem(true);const auto core=GalacticCores::Get(id);
            const auto frame=core.hotFlow?GalacticCores::HotFlowFrame():InterstellarWorlds::Orientation();
            const double horizon=KerrSpacetime::Horizon(core.RadiusAu(),core.spin);
            starSystem.camera=frame*glm::dvec3(horizon*.98,0,0);horizonReached=observerGravity=false;blackHoleJourney.Reset();
            UpdateBlackHoleJourney(frame*glm::dvec3(horizon*1.1,0,0),.25f);
            if(!observerGravity || !blackHoleJourney.inside){std::cerr<<"Core horizon entry blocked: "<<id<<"\n";fail();}
            for(int step=0;step<64;++step)UpdateBlackHoleJourney(starSystem.camera,.25f);
            if(blackHoleJourney.tesseract){std::cerr<<"Ordinary black hole acquired Gargantua destination\n";fail();}
        }
        StartGargantua();trajectories=false;selectedTarget=-1;const auto frame=InterstellarWorlds::Orientation();
        const double horizon=KerrSpacetime::Horizon(BlackHole::HorizonRadius(),blackHole.Spin());
        cameraPos=frame*glm::dvec3(0,0,horizon*.98);cameraFront=glm::normalize(glm::vec3(-cameraPos));cameraUp=glm::vec3(frame[1]);
        const auto originalFront=cameraFront,originalUp=cameraUp;fieldOfView=63;cameraSpeed=.013;
        UpdateBlackHoleJourney(frame*glm::dvec3(0,0,horizon*1.1),.25f);
        if(!blackHoleJourney.inside || observerGravity)fail();
        capture("gargantua-event-horizon-interior",false,{},true);
        for(int step=0;step<64;++step)UpdateBlackHoleJourney(cameraPos,.25f);
        if(blackHoleJourney.tesseract){std::cerr<<"Stationary camera still entered the tesseract on a timer\n";fail();}
        for(double depth:{.91,.80,.70,.54}){
            cameraPos=frame*glm::dvec3(0,0,horizon*depth);const auto position=cameraPos;
            simulationPlaying=true;
            const double time=simulationTime;const bool playback=simulationPlaying;
            UpdateBlackHoleJourney(position,.016f);
            if(cameraPos!=position || cameraFront!=originalFront || cameraUp!=originalUp || fieldOfView!=63 || cameraSpeed!=.013
                || simulationTime!=time || simulationPlaying!=playback){std::cerr<<"Spatial reveal reset the observer pose or controls\n";fail();}
            simulationPlaying=false;
            sceneCacheValid=false;capture(("gargantua-descent-depth-"+std::to_string(int(depth*100))).c_str(),false,{},true);
        }
        if(blackHoleJourney.interiorBlend!=1 || observerGravity || blackHoleJourney.atLimit)fail();
        const double scale=TesseractSpace::Scale(BlackHole::HorizonRadius());
        cameraPos=frame*glm::dvec3(0,2,0)*scale;cameraFront=glm::vec3(frame*glm::dvec3(0,0,-1));cameraUp=glm::vec3(frame[1]);
        UpdateBlackHoleJourney(cameraPos,.016f);const auto landingStart=cameraPos;
        ToggleLanding();if(cameraPos!=landingStart || !interiorLanding)fail();
        double maximumStep=0;
        for(int step=0;step<100;++step){const auto before=cameraPos;MoveInterior({},.05f);maximumStep=std::max(maximumStep,glm::length(cameraPos-before)/scale);}
        const auto foot=glm::transpose(frame)*cameraPos/scale;
        if(!interiorGrounded || std::abs(foot.y-TesseractSpace::deckTop-TesseractSpace::eyeHeight)>1e-8 || maximumStep>.8){std::cerr<<"Interior landing snapped or missed its deck\n";fail();}
        const auto grounded=cameraPos;MoveInterior(frame*glm::dvec3(.5,0,0)*scale,.016f);
        if(!interiorGrounded || glm::length(cameraPos-grounded)<scale*.49)fail();
        sceneCacheValid=false;capture("gargantua-tesseract-landed-deck");
        const auto liftoff=cameraPos;ToggleLanding();if(cameraPos!=liftoff || interiorLanding)fail();
        MoveInterior(frame*glm::dvec3(0,2,1)*scale,.016f);
        if(glm::length(cameraPos-liftoff-frame*glm::dvec3(0,2,1)*scale)>1e-12)fail();
        sceneCacheValid=false;capture("gargantua-tesseract-bookshelves");
        observerGravity=true;kerrObserverValid=false;interiorDrift=false;const auto fallingStart=cameraPos;
        UpdateObserver(simulationTime,.1f);
        if(glm::length(cameraPos-fallingStart)<=0 || glm::length(cameraPos-fallingStart)>scale*.01 || !observerGravity){std::cerr<<"Continuous interior free fall failed\n";fail();}
        observerGravity=false;
        cameraFront=glm::normalize(glm::vec3(frame*glm::dvec3(1,.2,-.8)));sceneCacheValid=false;capture("gargantua-tesseract-lattice");
        std::cout<<"Seamless interior: unchanged pose/FOV/speed/time at four depths; freecam, gradual landing, walking and liftoff passed\n";
        KeyCallback(window,GLFW_KEY_HOME,0,GLFW_PRESS,0);if(blackHoleJourney.inside || blackHoleJourney.tesseract)fail();
        std::cout<<"Nature and interior journey checks "<<(success?"PASSED":"FAILED")<<"\n";
    };
    const auto checkGargantua=[&](){
        StartGargantua();scenePanel.SetVisible(false);ResizeInterface();simulationPlaying=false;simulationTime=0;
        selectedTarget=-1;trajectories=false;visionProfile=0;fieldOfView=55;lensFlareStrength=0;exposureEv=0;
        blackHole.SetSpin(.8);blackHole.SetRayBudget(256);KeyCallback(window,1200,0,GLFW_PRESS,0);
        const auto emission=[](unsigned texture){
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);GLint w,h;
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
            std::vector<float> pixels(static_cast<size_t>(w)*h*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data());
            double sum=0;size_t area=0;for(size_t p=0;p<pixels.size();p+=4){const float peak=std::max({pixels[p],pixels[p+1],pixels[p+2]});if(pixels[p+3]>.5f && peak>.2f){sum+=peak;++area;}}
            return std::pair<double,size_t>(sum,area);
        };
        blackHole.SetGargantuaDisk(1.03f/.24f,3.f);sceneCacheValid=false;capture("gargantua-disc-previous-baseline");const auto old=emission(optics.SceneInputTexture());
        blackHole.SetGargantuaDisk(8.f,3.f);sceneCacheValid=false;capture("gargantua-disc-larger");const auto larger=emission(optics.SceneInputTexture());
        blackHole.SetGargantuaDisk(8.f,6.4f);sceneCacheValid=false;const auto bright=capture("gargantua-cinematic-hdr");const auto brighter=emission(optics.SceneInputTexture());
        std::cout<<"Gargantua disc: emissive area "<<old.second<<" -> "<<larger.second<<"; linear flux "<<larger.first<<" -> "<<brighter.first<<"\n";
        if(larger.second<old.second*1.3 || brighter.first<larger.first*1.6){std::cerr<<"Gargantua disc did not become larger and brighter\n";fail();}
        size_t clipped=0;for(size_t p=0;p<bright.size();p+=3)if(bright[p]>250 && bright[p+1]>250 && bright[p+2]>250)++clipped;
        if(clipped>bright.size()/3*.01){std::cerr<<"HDR erased the disc's highlight detail\n";fail();}
        blackHole.SetHotFlowDiagnostic(true);sceneCacheValid=false;capture("gargantua-disc-transmission");
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,optics.SceneInputTexture());GLint tw,th;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&tw);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&th);
        std::vector<float> trans(static_cast<size_t>(tw)*th*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,trans.data());
        size_t translucent=0;
        for(size_t p=0;p<trans.size();p+=4)if(trans[p]>.02f && trans[p]<.98f
            && std::abs(trans[p]-trans[p+1])<.0001f && std::abs(trans[p]-trans[p+2])<.0001f)++translucent;
        std::cout<<"Thin disk partially transmitting pixels="<<translucent<<"\n";
        if(translucent<300){std::cerr<<"Accretion disk is an opaque cutout\n";fail();}
        blackHole.SetHotFlowDiagnostic(false);sceneCacheValid=false;
        KeyCallback(window,1201,0,GLFW_PRESS,0);optics.SetAutoExposure(true);const auto pure=capture("gargantua-cinematic-auto");
        glBindTexture(GL_TEXTURE_2D,optics.SceneInputTexture());GLint pw,ph;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&pw);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&ph);
        std::vector<float> diskPixels(static_cast<size_t>(pw)*ph*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,diskPixels.data());
        std::vector<int> diskDisplay;
        if(static_cast<size_t>(pw)*ph*3==pure.size())for(size_t p=0;p<diskPixels.size();p+=4){
            if(diskPixels[p+3]>.9 && std::max({diskPixels[p],diskPixels[p+1],diskPixels[p+2]})>1){const auto q=p/4*3;diskDisplay.push_back(std::max({pure[q],pure[q+1],pure[q+2]}));}
        }
        if(!diskDisplay.empty()){
            std::sort(diskDisplay.begin(),diskDisplay.end());const int upperMid=diskDisplay[diskDisplay.size()*3/4];
            std::cout<<"Gargantua Pure Exposure disk upper quartile="<<upperMid<<" / 255\n";
            if(upperMid<145){std::cerr<<"Pure Exposure hid the broad emitting disk\n";fail();}
        }
        KeyCallback(window,1200,0,GLFW_PRESS,0);blackHole.SetRayBudget(160);
        adaptiveBlackHoleQuality=true;blackHoleResolution=.5f;sceneCacheValid=false;
        capture("gargantua-hdr-travel-preview");
        if(renderedBlackHoleScale!=.5f)fail();
        glfwSetTime(lastBlackHoleCameraChange+.4);capture("gargantua-hdr-paused-native-clouds");
        if(renderedBlackHoleScale!=1.f){std::cerr<<"Paused accretion clouds did not refine to native resolution\n";fail();}
        glfwSetTime(glfwGetTime()+.4);float dt=.016f;Update(dt);
        if(!idleFrame){std::cerr<<"Refined black hole failed to reuse its paused frame\n";fail();}
        adaptiveBlackHoleQuality=false;blackHoleResolution=1;
        // Resolve a small disk natively, and verify camera adaptation after
        // turning away without resetting exposure history or simulation time.
        for(double distance:{650.0,5000.0,Astronomy::auPerLightYear}){
            const auto axis=glm::normalize(glm::dvec3(.2,.25,1));
            cameraPos=axis*distance;cameraFront=glm::vec3(-axis);cameraUp={0,1,0};
            fieldOfView=distance>20000?1.f:45.f;blackHole.SetRayBudget(160);
            KeyCallback(window,1200,0,GLFW_PRESS,0);sceneCacheValid=false;
            const std::string label="gargantua-distant-"+std::to_string(int(distance));
            const auto hdr=capture((label+"-hdr").c_str());
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,optics.SceneInputTexture());GLint w,h;
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
            std::vector<float> raw(static_cast<size_t>(w)*h*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,raw.data());
            const auto percentile=[&](const auto& display){
                std::vector<int> signals;
                if(display.size()==static_cast<size_t>(w)*h*3)for(size_t p=0;p<raw.size();p+=4)
                    if(raw[p+3]>2.9f && std::max({raw[p],raw[p+1],raw[p+2]})>.5f){const auto q=p/4*3;signals.push_back(std::max({display[q],display[q+1],display[q+2]}));}
                if(signals.empty())return 0;std::sort(signals.begin(),signals.end());return signals[signals.size()*3/4];
            };
            const int hdrSignal=percentile(hdr);
            KeyCallback(window,1201,0,GLFW_PRESS,0);optics.SetAutoExposure(true);sceneCacheValid=false;
            const auto automatic=capture((label+"-auto").c_str());const int autoSignal=percentile(automatic);
            const auto exposure=[&](){glActiveTexture(GL_TEXTURE2);std::array<float,4> state{};glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,state.data());glActiveTexture(GL_TEXTURE0);return state;};
            const auto facing=exposure();
            cameraFront=glm::vec3(axis);sceneCacheValid=false;
            const auto firstAway=capture((label+"-away-first").c_str());float dt=.25f;
            for(int frame=0;frame<24;++frame)Update(dt);
            const auto adaptedAway=capture((label+"-away-adapted").c_str());const auto away=exposure();
            double firstLight=0,adaptedLight=0;for(auto c:firstAway)firstLight+=c;for(auto c:adaptedAway)adaptedLight+=c;
            std::cout<<label<<": HDR P75="<<hdrSignal<<" Auto P75="<<autoSignal<<"; EV facing="<<facing[0]<<" away="<<away[0]<<" target="<<away[1]<<"\n";
            if(hdrSignal<140 || autoSignal<200 || away[0]<facing[0]+.5f || std::abs(away[0]-away[1])>.1f
                || adaptedLight<=firstLight){std::cerr<<"Distant disk grading or look-away adaptation failed\n";fail();}
        }
        KeyCallback(window,1200,0,GLFW_PRESS,0);
    };
    if(galaxyOnly){checkGalaxy();checkSagittarius();checkGargantua();return success;}
    if(weatherOnly){checkOceanWeather();checkWeather();checkNatureJourney();checkGargantua();return success;}
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
    checkOceanWeather();
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
    if(hdrDiskOn==hdrDiskOff){std::cerr<<"HDR disk detail response made no contribution\n";fail();}
    KeyCallback(window,1201,0,GLFW_PRESS,0);optics.SetAutoExposure(true);
    KeyCallback(window,1205,0,GLFW_PRESS,0);const auto pureDiskOff=capture("black-hole-auto-unassisted");
    KeyCallback(window,1204,0,GLFW_PRESS,0);const auto pureDiskOn=capture("black-hole-auto-visible");
    std::cout<<"Black hole display sums HDR "<<displayBrightness(hdrDiskOff)<<" -> "<<displayBrightness(hdrDiskOn)
        <<"; Auto "<<displayBrightness(pureDiskOff)<<" -> "<<displayBrightness(pureDiskOn)<<"\n";
    // Compare faint background directly. A brighter Auto disk may legitimately
    // make the total image sum exceed HDR without changing its background.
    int backgroundW,backgroundH,logicalW,logicalH;glfwGetFramebufferSize(window,&backgroundW,&backgroundH);
    glfwGetWindowSize(window,&logicalW,&logicalH);backgroundW-=scenePanel.Width()*backgroundW/logicalW;
    size_t cornerLight=0;int backgroundChange=0;
    for(int corner=0;corner<4;++corner)for(int y=4;y<20;++y)for(int x=4;x<20;++x){
        const int px=corner%2?backgroundW-1-x:x,py=corner/2?backgroundH-1-y:y;
        const auto p=(static_cast<size_t>(py)*backgroundW+px)*3;
        for(int c=0;c<3;++c){cornerLight+=hdrDiskOn[p+c];backgroundChange=std::max(backgroundChange,std::abs(int(hdrDiskOn[p+c])-int(hdrDiskOff[p+c])));}
    }
    if(!cornerLight || backgroundChange>1){std::cerr<<"HDR disk response changed faint background light\n";fail();}
    KeyCallback(window,1200,0,GLFW_PRESS,0);lensFlareStrength=.45f;
    cameraPos={0,0,650};cameraFront={0,0,-1};cameraUp={0,1,0};fieldOfView=4;
    capture("gargantua-visible-from-mann-distance");
    ResetView();
    KeyCallback(window, 701, 0, GLFW_PRESS, 0);
    capture("miller-orbit");
    ToggleLanding();
    fieldOfView = 80;
    capture("miller-surface");
    // Grazing, near-ground view: both the bright lensed disk and its reflected
    // direction fit on screen. SSR must affect the ocean, never the sky.
    const auto reflectionCenter=BlackHole::PlanetPosition(simulationTime);
    const auto radialOut=glm::normalize(reflectionCenter);
    const auto sideways=glm::normalize(glm::cross(radialOut,glm::dvec3(0,1,0)));
    const auto surfaceUp=glm::normalize(sideways-radialOut*.045);
    cameraPos=reflectionCenter+surfaceUp*(blackHole.PlanetSurfaceRadius(glm::vec3(surfaceUp),simulationTime,0)+2.0/Astronomy::metresPerAu);
    cameraFront=glm::normalize(glm::vec3(-radialOut-surfaceUp*.10));cameraUp=glm::vec3(surfaceUp);
    landed=false;followPlanet=followSelected=followSpacecraft=false;selectedTarget=-1;
    fieldOfView=90;visionProfile=0;lensFlareStrength=0;
    blackHole.SetOceanReflectionsEnabled(false);sceneCacheValid=false;
    const auto oceanUnreflected=capture("miller-water-reflections-off");
    blackHole.SetOceanReflectionsEnabled(true);sceneCacheValid=false;
    const auto oceanReflected=capture("miller-water-reflections-on");
    size_t reflectionPixels=0,replacedSpecular=0;
    for(size_t p=0;p<oceanReflected.size();p+=3)if(std::abs(int(oceanReflected[p])-int(oceanUnreflected[p]))
        +std::abs(int(oceanReflected[p+1])-int(oceanUnreflected[p+1]))+std::abs(int(oceanReflected[p+2])-int(oceanUnreflected[p+2]))>3)++reflectionPixels;
    std::cout<<"Miller reflection pixels="<<reflectionPixels<<"\n";
    if(reflectionPixels<20)fail();
    for(size_t p=0;p<oceanReflected.size();p+=3){
        const int before=int(oceanUnreflected[p])+oceanUnreflected[p+1]+oceanUnreflected[p+2];
        const int after=int(oceanReflected[p])+oceanReflected[p+1]+oceanReflected[p+2];
        if(after+20<before)++replacedSpecular;
    }
    std::cout<<"Miller fallback specular pixels replaced="<<replacedSpecular<<"\n";
    if(replacedSpecular<20)fail();
    // Air-only comparisons below keep the reflection contribution fixed.
    blackHole.SetOceanReflectionsEnabled(false);
    // Foreground air must only affect rays intersecting its own shell, even
    // when the accretion disk is directly behind the planet. Disable camera
    // optics here so this tests atmospheric/lensing composition in scene RGB.
    followPlanet=false;followSpacecraft=false;followSelected=false;
    trajectories=false;selectedTarget=-1;lensFlareStrength=0;visionProfile=0;exposureEv=0;
    const auto millerCenter=BlackHole::PlanetPosition(simulationTime);
    const auto millerOutward=glm::normalize(millerCenter);
    const auto millerUp=glm::normalize(glm::cross(millerOutward,glm::dvec3(0,1,0)));
    const auto millerSide=glm::normalize(glm::cross(millerOutward,millerUp));
    fieldOfView=65;
    for(double radii:{3.0,1.029,1.031,1.000001}){
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
        // Include the finite auroral layer and four pixels of linear reconstruction.
        const double auroraTop=std::min(350000.0/Astronomy::metresPerAu,Astronomy::millerRadiusAu*.09);
        const double shellRadius=std::max(Astronomy::millerRadiusAu*1.025,Astronomy::millerRadiusAu+auroraTop)+glm::length(origin)*tangent*8/height;
        size_t leaked=0,affected=0,nonfinite=0;
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){
            const auto direction=glm::normalize(glm::dvec3(cameraFront)+right*((2*(x+.5)/width-1)*tangent*aspect)+up*((2*(y+.5)/height-1)*tangent));
            const double along=glm::dot(origin,direction);
            const bool intersects=glm::length(origin)<shellRadius || (along<0 && glm::dot(origin,origin)-along*along<shellRadius*shellRadius);
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
    blackHole.SetOceanReflectionsEnabled(true);
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
    // Looking away from a star before adaptation may correctly be almost
    // black now that distant points no longer have a brightness floor.
    const auto darkFirst=capture("pure-auto-dark-first",false,{},true);
    float adaptationStep=.1f;
    for(int frame=0;frame<80;++frame)Update(adaptationStep);
    const auto darkAdapted=capture("pure-auto-dark-adapted");
    unsigned long long darkBefore=0,darkAfter=0;
    for(size_t i=0;i<darkFirst.size();++i){darkBefore+=darkFirst[i];darkAfter+=darkAdapted[i];}
    if(darkAfter<=darkBefore){std::cerr<<"Exposure did not brighten after turning away from the star\n";fail();}
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
    // Crossing the asteroid belt and the sphere/point LOD boundary must never
    // transfer ownership to a distant catalogue highlight.
    StartSolarSystem();trajectories=true;fieldOfView=45;ApplyVisionProfile(2);lensFlareStrength=1;
    for(double distance:{2.5,5.0,50.0,starSystem.DetailRadiusAu()*.99,starSystem.DetailRadiusAu()*1.01,10000.0}){
        starSystem.camera={0,0,distance};cameraFront={0,0,-1};cameraUp={0,1,0};sceneCacheValid=false;
        capture(("solar-system-flare-"+std::to_string(static_cast<int>(distance))+"-au").c_str());
        const auto sources=readSources();int count=0;
        for(int i=0;i<8;++i)if(sources[i*8+2]>0){++count;if(glm::length(glm::vec2(sources[i*8]-.5f,sources[i*8+1]-.5f))>1e-5f)fail();}
        std::cout<<"Solar flare at "<<distance<<" AU: "<<count<<" local optical sources\n";
        if(count!=1)fail();
    }
    cameraFront=glm::normalize(glm::vec3(.2,.05,-1));sceneCacheValid=false;capture("solar-off-axis-owned-flare");
    {const auto sources=readSources();int count=0;for(int i=0;i<8;++i)if(sources[i*8+2]>0)++count;if(count!=1 || std::abs(sources[0]-.5f)<.03f)fail();}
    cameraFront=glm::normalize(glm::vec3(2,0,-1));sceneCacheValid=false;capture("solar-offscreen-no-distant-flare");
    {const auto sources=readSources();for(int i=0;i<8;++i)if(sources[i*8+2]>0)fail();}
    StartSolarSystem();const auto nextHost=blackHole.ResolveTarget(10,simulationTime);
    starSystem.camera=nextHost.position+glm::dvec3(0,0,50);cameraFront={0,0,-1};cameraUp={0,1,0};sceneCacheValid=false;
    capture("nearest-system-flare-independent-of-selection");
    {const auto sources=readSources();int count=0;for(int i=0;i<8;++i)if(sources[i*8+2]>0){++count;if(glm::length(glm::vec2(sources[i*8]-.5f,sources[i*8+1]-.5f))>1e-5f)fail();}if(count!=1)fail();}
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
    // Independent synthetic scene: a bright emitter, a dim distant background
    // and finite non-emissive geometry must receive three distinct treatments.
    const auto exposureScene=compileShaderProgram(R"(#version 330 core
out vec2 uv;void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));uv=p;gl_Position=vec4(p*2.0-1.0,0,1);})",
R"(#version 330 core
in vec2 uv;out vec4 color;uniform float brightness;
void main(){color=vec4(.01,.01,.01,0);gl_FragDepth=.999999;
if(uv.x<.30 && uv.y>.35 && uv.y<.65){color=vec4(.2,.16,.1,0);gl_FragDepth=.5;}
if(length(uv-vec2(.75,.75))<.08){color=vec4(vec3(brightness),uv.x<.75?1.0:0.0);gl_FragDepth=.999;}})");
    unsigned int exposureVao;glGenVertexArrays(1,&exposureVao);
    std::array<std::array<unsigned char,3>,2> planetSignals{};
    for(int bright=0;bright<2;++bright){
        sourceProbe.SetAutoExposure(true);sourceProbe.SetStellarHint({.75f,.75f,.08f});
        glViewport(0,0,128,128);sourceProbe.BeginFrame();glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glEnable(GL_DEPTH_TEST);
        glUseProgram(exposureScene);glUniform1f(glGetUniformLocation(exposureScene,"brightness"),bright?32000.f:8000.f);
        glBindVertexArray(exposureVao);glDrawArrays(GL_TRIANGLES,0,3);
        sourceProbe.Render(0,45,0,false,0,false,0);
        unsigned char sky[3]={},emitter[3]={};glReadBuffer(GL_BACK);
        glReadPixels(10,10,1,1,GL_RGB,GL_UNSIGNED_BYTE,sky);
        glReadPixels(20,64,1,1,GL_RGB,GL_UNSIGNED_BYTE,planetSignals[bright].data());
        glReadPixels(96,96,1,1,GL_RGB,GL_UNSIGNED_BYTE,emitter);
        std::cout<<"Auto exposure probe: sky="<<int(sky[0])<<" planet="<<int(planetSignals[bright][0])<<" emitter="<<int(emitter[0])<<"\n";
        if(sky[0]>3 || planetSignals[bright][0]<80 || emitter[0]<130 || emitter[0]>245)fail();
    }
    if(planetSignals[0]!=planetSignals[1]){std::cerr<<"Bright emitter hid local planet geometry\n";fail();}
    sourceProbe.Render(0,45,0,true,0,true,0);
    unsigned char exposedSurface[3]={},plasmaCoveredSurface[3]={};
    glReadPixels(91,96,1,1,GL_RGB,GL_UNSIGNED_BYTE,exposedSurface);
    glReadPixels(100,96,1,1,GL_RGB,GL_UNSIGNED_BYTE,plasmaCoveredSurface);
    std::cout<<"HDR photosphere with optical mask cleared: "<<int(exposedSurface[0])<<" -> "<<int(plasmaCoveredSurface[0])<<"\n";
    if(std::abs(int(exposedSurface[0])-int(plasmaCoveredSurface[0]))>5)fail();
    glDeleteVertexArrays(1,&exposureVao);glDeleteProgram(exposureScene);glReadBuffer(readBuffer);
    // Two local stars in the same old extraction tile must retain two centres,
    // despite a much brighter unrelated background emitter elsewhere.
    std::array<glm::vec4,8> binaryHints{};
    binaryHints[0]={.15f,.25f,.01f,1};binaryHints[1]={.23f,.25f,.01f,1};
    for(int mode=0;mode<3;++mode){
        sourceProbe.SetSystemSources(binaryHints,mode==2?0:2);
        glViewport(0,0,128,128);sourceProbe.BeginFrame();glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        for(int i=0;i<2;++i){glScissor(int(binaryHints[i].x*128),31,2,2);glClearColor(i==0?8.f:4.f,2,1,mode==1 && i==0?0.f:1.f);glClear(GL_COLOR_BUFFER_BIT);}
        glScissor(98,98,8,8);glClearColor(60,40,20,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
        sourceProbe.Render(2,45,0,true,1,false,0);
        const auto sources=readSources();int count=0;
        for(int i=0;i<8;++i)if(sources[i*8+2]>0){++count;if(i>1 || glm::length(glm::vec2(sources[i*8],sources[i*8+1])-glm::vec2(binaryHints[i]))>1e-5f)fail();}
        std::cout<<"Scoped binary mode "<<mode<<": "<<count<<" centres\n";
        if(count!=(mode==0?2:mode==1?1:0))fail();
    }
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
        && black[0]==8 && unresolved[0]>resolved[0]+4 && probeError==GL_NO_ERROR;
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
    KeyCallback(window,1200,0,GLFW_PRESS,0);
    const auto tonDefault=capture("ton-618-volume-default");
#ifdef _WIN32
    KeyCallback(window,GLFW_KEY_F5,0,GLFW_PRESS,0);
    const auto tonEditor=static_cast<HWND>(objectEditor.NativeHandle());
    if(!IsWindowEnabled(GetDlgItem(tonEditor,ObjectEditor::Thickness)))fail();
    SetDlgItemTextA(tonEditor,ObjectEditor::Thickness,"2.0");SetDlgItemTextA(tonEditor,ObjectEditor::Temperature,"8500");
    SendMessageW(tonEditor,WM_COMMAND,MAKEWPARAM(ObjectEditor::ApplyDisk,BN_CLICKED),0);
    if(blackHole.DiskAppearance(GalacticCores::tonId)!=glm::vec2(2,8500))fail();
    SetDlgItemTextA(tonEditor,ObjectEditor::Temperature,"nan");
    SendMessageW(tonEditor,WM_COMMAND,MAKEWPARAM(ObjectEditor::ApplyDisk,BN_CLICKED),0);
    if(blackHole.DiskAppearance(GalacticCores::tonId)!=glm::vec2(2,8500))fail();
    SetDlgItemTextA(tonEditor,ObjectEditor::Temperature,"8500");
    SendMessageW(tonEditor,WM_COMMAND,MAKEWPARAM(ObjectEditor::ApplyDisk,BN_CLICKED),0);
    RefreshObjectEditor();captureUi("ui-ton-disk-editor",tonEditor);
    KeyCallback(window,GLFW_KEY_F5,0,GLFW_PRESS,0);
#else
    blackHole.SetDiskAppearance(GalacticCores::tonId,2.f,8500.f);
#endif
    sceneCacheValid=false;
    const auto tonThick=capture("ton-618-volume-thick-cool");
    if(tonDefault==tonThick)fail();
    blackHole.SetDiskAppearance(GalacticCores::tonId,1.f,12500.f);
    for(size_t i=0;i<NearbyGalaxies::Catalog.size();++i){
        const char* aliases[]={"NGC224","M33","LMC","SMC","Haog's Object","NGC221"};
        const auto& galaxy=NearbyGalaxies::Catalog[i];const auto found=navigationCatalog.Find(aliases[i],simulationTime);
        const auto entry=std::find_if(found.begin(),found.end(),[&](const auto& e){return e.kind==NavigationEntry::Kind::Galaxy && e.system==NearbyGalaxies::firstId+static_cast<int>(i);});
        if(entry==found.end()){fail();continue;}
        if(std::abs(glm::length(entry->anchorAu)/Astronomy::auPerLightYear-galaxy.distanceLy)>.001)fail();
        StartSolarSystem();starSystem.camera={0,0,2};cameraFront=glm::vec3(galaxy.Direction());cameraUp=glm::vec3(galaxy.Frame()[0]);
        visionProfile=0;lensFlareStrength=0;fieldOfView=static_cast<float>(galaxy.majorArcmin/60.0*3);sceneCacheValid=false;
        capture(("nearby-galaxy-"+std::to_string(i)+"-from-sun").c_str(),false,{},galaxy.morphology==4);
        if(galaxy.morphology==4){
            const float baseLimit=galaxyLimitingMagnitude;SetGalaxyMagnitudeLimit(18);sceneCacheValid=false;
            capture("hoags-object-distant-sensitive-camera");SetGalaxyMagnitudeLimit(baseLimit);
        }
        GoToNavigation(*entry);sceneCacheValid=false;capture(("nearby-galaxy-"+std::to_string(i)+"-approach").c_str());
        if(galaxy.morphology==4){
            capture("hoags-object-hubble-ring");glBindTexture(GL_TEXTURE_2D,optics.SceneInputTexture());GLint rw,rh;
            glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&rw);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&rh);
            std::vector<float> ringPixels(static_cast<size_t>(rw)*rh*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,ringPixels.data());
            const double ry=rh/(2*std::tan(glm::radians(static_cast<double>(fieldOfView))*.5)*3.5),rx=ry*galaxy.minorArcmin/galaxy.majorArcmin;
            double coreRed=0,coreBlue=0,ringRed=0,ringBlue=0,gap=0;size_t coreN=0,ringN=0,gapN=0;
            for(int y=0;y<rh;++y)for(int x=0;x<rw;++x){const double r=std::hypot((x+.5-rw*.5)/rx,(y+.5-rh*.5)/ry);const auto p=(static_cast<size_t>(y)*rw+x)*4;
                if(r<.12){coreRed+=ringPixels[p];coreBlue+=ringPixels[p+2];++coreN;}
                if(r>.69 && r<.84){ringRed+=ringPixels[p];ringBlue+=ringPixels[p+2];++ringN;}
                if(r>.43 && r<.50){gap+=ringPixels[p]+ringPixels[p+1]+ringPixels[p+2];++gapN;}
            }
            const double ringMean=(ringRed+ringBlue)/std::max<size_t>(1,ringN),gapMean=gap/std::max<size_t>(1,gapN);
            std::cout<<"Hoag ring: core R/B="<<coreRed<<"/"<<coreBlue<<" ring B/R="<<ringBlue<<"/"<<ringRed<<" gap="<<gapMean<<" ring="<<ringMean<<"\n";
            if(!coreN || !ringN || coreRed<coreBlue*1.2 || ringBlue<ringRed*1.1 || gapMean>ringMean*.3){std::cerr<<"Hoag lost its yellow nucleus, detached gap or blue ring\n";fail();}
        }
        const auto submitted=blackHole.LastSubmittedStars();
        KeyCallback(window,1201,0,GLFW_PRESS,0);optics.SetAutoExposure(true);sceneCacheValid=false;
        capture(("nearby-galaxy-"+std::to_string(i)+"-auto").c_str());
        if(blackHole.LastSubmittedStars()!=submitted){std::cerr<<"Galaxy exposure altered star population\n";fail();}
        KeyCallback(window,1200,0,GLFW_PRESS,0);
    }
    StartSolarSystem();trajectories=false;fieldOfView=55;visionProfile=0;lensFlareStrength=0;
    KeyCallback(window,1200,0,GLFW_PRESS,0);SetMagnitudeLimit(7);
    const auto stellarPixels=[&](){
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,optics.SceneInputTexture());GLint w,h;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
        std::vector<float> rgba(static_cast<size_t>(w)*h*4);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,rgba.data());
        size_t count=0;for(size_t p=0;p<rgba.size();p+=4)
            if(rgba[p+3]>.9f && std::max({rgba[p],rgba[p+1],rgba[p+2]})>.02f)++count;
        return count;
    };
    for(int galaxy:{0,4,9,Universe::MainStreamGalaxies}){
        StartSolarSystem();
        const auto group=Universe::GalaxyAt(galaxy);
        const auto position=galaxy==0?MilkyWayModel::ToWorldAu({14.8,0,0}):group.centerAu+group.rotation*glm::dvec3(group.axesAu.x*.55,0,0);
        starSystem.camera=position-systemOriginAu;cameraFront=glm::normalize(glm::vec3(group.rotation*glm::dvec3(-1,0,.2)));cameraUp=glm::vec3(group.rotation[1]);sceneCacheValid=false;
        capture(("seeded-galaxy-"+std::to_string(galaxy)+"-interior").c_str(),false,{},true);
        const auto localStars=galaxy==0?0:stellarPixels();
        const auto lowCount=blackHole.ResidentStreamStars();
        if(!lowCount || !blackHole.ResidentStarCells()){std::cerr<<"Galaxy interior has no streamed stars\n";fail();}
        SetMagnitudeLimit(13);sceneCacheValid=false;capture(("seeded-galaxy-"+std::to_string(galaxy)+"-deep-camera").c_str(),false,{},true);
        if(galaxy!=0){
            // The diffuse interior can illuminate every display pixel. Count
            // actual stellar cores using their emitter class, excluding the
            // galaxy volume, rather than mistaking the bright band for stars.
            const auto deepStars=stellarPixels();
            std::cout<<"Camera sensitivity: "<<localStars<<" -> "<<deepStars<<" stellar core pixels\n";
            if(deepStars<=localStars)fail();
        }
        std::cout<<"Seeded galaxy "<<galaxy<<": stars "<<lowCount<<" -> "<<blackHole.ResidentStreamStars()<<"; cells="<<blackHole.ResidentStarCells()<<"\n";
        if(blackHole.ResidentStreamStars()<lowCount)fail();
        SetMagnitudeLimit(7);
    }
    StartSolarSystem();starSystem.camera=MilkyWayModel::ToWorldAu({18,2,0});cameraFront=glm::normalize(glm::vec3(-starSystem.camera));cameraUp=glm::vec3(MilkyWayModel::Orientation()[1]);fieldOfView=55;sceneCacheValid=false;
    capture("milky-way-no-solar-star-sphere");
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
    fieldOfView=CameraZoom::MinimumFieldOfView(45);SetMagnitudeLimit(7);capture("zoom-20x-magnitude-13-51");
    std::cout<<"20x star submission: "<<blackHole.LastSubmittedStars()<<" / "<<Universe::StarCount<<" catalogue points; effective magnitude="<<blackHole.MagnitudeLimit()<<"\n";
    if(std::abs(blackHole.MagnitudeLimit()-13.50515f)>.001 || blackHole.LastSubmittedStars()>=Universe::StarCount)fail();
    fieldOfView=75;SetMagnitudeLimit(10.2f);
    cameraFront=glm::normalize(glm::vec3(MilkyWayModel::Orientation()[1]));capture("milky-way-galactic-pole");
    if(blackHole.MilkyWayCacheBuilds()>beforeSky+1){std::cerr<<"Sky rebaked on rotation or magnitude changes\n";fail();}
    SetMagnitudeLimit(7);
    checkWormhole();
    checkLensing();
    checkGalaxy();
    checkWeather();
    checkNatureJourney();
    checkSagittarius();
    checkGargantua();
    std::cout<<"Render checks "<<(success?"PASSED":"FAILED")<<": "<<captures<<" scene captures; "<<uiCaptures<<" UI captures; "<<failedLines.size()<<" assertion locations failed\n";
    return success;
}
