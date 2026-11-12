#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#endif
#include "WindowManager.h"
#include "glad/glad.h"
#include <filesystem>
#include <fstream>
#include <iostream>

// Optional hidden-window GPU smoke checks; normal application startup never runs this.
bool WindowManager::RunRenderChecks(const std::string &directory)
{
    if (!blackHoleReady || !starSystemReady || !quantumReady || !opticsReady)
        return false;
    std::filesystem::create_directories(directory);
    glfwSwapInterval(0);
    // Exercise the real startup transition, including the menu's OpenGL clear-color state.
    float menuFrame=.016f;
    Update(menuFrame);
    bool success = true;
    const auto capture = [&](const char *name,bool keepToast=false) {
        if(!keepToast)speedToastUntil = 0;
        float dt = .016f;
        Update(dt);
        glFinish();
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
            success = false;
        }
        int width, height, logicalWidth, logicalHeight;
        glfwGetFramebufferSize(window, &width, &height);
        glfwGetWindowSize(window, &logicalWidth, &logicalHeight);
        width -= scenePanel.Width() * width / logicalWidth;
        std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
        glReadBuffer(GL_FRONT);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        std::ofstream image(std::filesystem::path(directory) / (std::string(name) + ".ppm"), std::ios::binary);
        image << "P6\n" << width << " " << height << "\n255\n";
        for (int y = height - 1; y >= 0; --y)
            image.write(reinterpret_cast<const char *>(pixels.data() + static_cast<size_t>(y) * width * 3),
                        static_cast<std::streamsize>(width) * 3);
        size_t lit = 0;
        for (size_t i = 0; i < pixels.size(); i += 3)
            if (pixels[i] > 20 || pixels[i + 1] > 20 || pixels[i + 2] > 20)
                ++lit;
        std::cout << name << ": " << width << "x" << height << ", lit=" << lit << ", GL=" << error << "\n";
        if (lit < 10)
            success = false;
        return pixels;
    };
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
    }
    StartSolarSystem();
    KeyCallback(window, GLFW_KEY_F4, 0, GLFW_PRESS, 0);
    if (scenePanel.IsCurrentSystemBrowser())
        success = false;
    SelectCatalogue(20);
    KeyCallback(window, GLFW_KEY_F4, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    if (!scenePanel.IsCurrentSystemBrowser() || pendingSystemTarget != activeSystemTarget)
        success = false;
    KeyCallback(window, GLFW_KEY_F4, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    KeyCallback(window, GLFW_KEY_F3, 0, GLFW_PRESS, 0);
    if (scenePanel.Width() != 0)
        success = false;
    KeyCallback(window, GLFW_KEY_F3, 0, GLFW_PRESS, 0);
    KeyCallback(window, GLFW_KEY_F4, 0, GLFW_PRESS, GLFW_MOD_SHIFT);
    capture("solar-overview");
    starSystem.SelectPlanet(2);
    starSystem.GoToSelected(simulationTime, cameraFront, cameraUp);
    capture("solar-earth");
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
    if(cameraPos!=beforeSelectionPosition || cameraFront!=beforeSelectionFront){std::cerr<<"Single selection moved camera\n";success=false;}
    SelectCatalogue(0);
    const auto original = capture("gargantua-lensing");
    const auto cached = capture("gargantua-cached");
    if (!idleFrame || original != cached)
    {
        std::cerr << "Cached frame mismatch\n";
        success = false;
    }
    KeyCallback(window,1330,0,GLFW_PRESS,0);
    const auto nonSpinning=capture("gargantua-spin-zero");
    KeyCallback(window,1332,0,GLFW_PRESS,0);
    const auto spinning=capture("gargantua-kerr-spin-08");
    if(nonSpinning==spinning){std::cerr<<"Kerr spin did not alter light propagation\n";success=false;}
    const auto cameraBeforeEditor=cameraPos;const auto timeBeforeEditor=simulationTime;
    KeyCallback(window,GLFW_KEY_F5,0,GLFW_PRESS,0);
    if(!objectEditor.Visible() || !objectEditor.Editable())success=false;
#ifdef _WIN32
    const auto editor=static_cast<HWND>(objectEditor.NativeHandle());
    if(!editor)return false;
    SetDlgItemTextA(editor,ObjectEditor::SpinValue,"0.650000");
    SendMessageW(editor,WM_COMMAND,MAKEWPARAM(ObjectEditor::Apply,BN_CLICKED),0);
    if(blackHole.Spin()!=.65 || sceneCacheValid)success=false;
    const auto editedSpin=capture("object-editor-spin-065");
    if(editedSpin==spinning)success=false;
    for(const char* invalid:{"1.0","nan","infinity","0.4junk",""}){
        SetDlgItemTextA(editor,ObjectEditor::SpinValue,invalid);
        SendMessageW(editor,WM_COMMAND,MAKEWPARAM(ObjectEditor::Apply,BN_CLICKED),0);
        if(blackHole.Spin()!=.65)success=false;
    }
    SendDlgItemMessageW(editor,ObjectEditor::SpinSlider,TBM_SETPOS,TRUE,-5000);
    SendMessageW(editor,WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(GetDlgItem(editor,ObjectEditor::SpinSlider)));
    if(blackHole.Spin()!=-.5)success=false;
    capture("object-editor-reverse-spin");
    KeyCallback(window,1331,0,GLFW_PRESS,0);
    char editorValue[40];GetDlgItemTextA(editor,ObjectEditor::SpinValue,editorValue,40);
    if(std::string(editorValue)!="0.500000")success=false;
    SelectCatalogue(10);
    if(objectEditor.Editable() || IsWindowEnabled(GetDlgItem(editor,ObjectEditor::SpinSlider)))success=false;
    SendMessageW(editor,WM_COMMAND,MAKEWPARAM(ObjectEditor::Reset,BN_CLICKED),0);
    if(blackHole.Spin()!=.5)success=false;
    SelectCatalogue(0);
    SendMessageW(editor,WM_COMMAND,MAKEWPARAM(ObjectEditor::Reset,BN_CLICKED),0);
    if(blackHole.Spin()!=.8)success=false;
#endif
    KeyCallback(window,GLFW_KEY_F5,0,GLFW_PRESS,0);
    if(objectEditor.Visible() || cameraPos!=cameraBeforeEditor || simulationTime!=timeBeforeEditor)success=false;
    const auto displayBrightness=[](const auto& pixels){double sum=0;for(auto value:pixels)sum+=value;return sum;};
    lensFlareStrength=0;exposureEv=0;KeyCallback(window,1200,0,GLFW_PRESS,0);
    KeyCallback(window,1205,0,GLFW_PRESS,0);const auto hdrDiskOff=capture("black-hole-hdr-unassisted");
    KeyCallback(window,1204,0,GLFW_PRESS,0);const auto hdrDiskOn=capture("black-hole-hdr-visible");
    if(displayBrightness(hdrDiskOn)<=displayBrightness(hdrDiskOff))success=false;
    KeyCallback(window,1201,0,GLFW_PRESS,0);optics.SetAutoExposure(true);
    KeyCallback(window,1205,0,GLFW_PRESS,0);const auto pureDiskOff=capture("black-hole-auto-unassisted");
    KeyCallback(window,1204,0,GLFW_PRESS,0);const auto pureDiskOn=capture("black-hole-auto-visible");
    std::cout<<"Black hole display sums HDR "<<displayBrightness(hdrDiskOff)<<" -> "<<displayBrightness(hdrDiskOn)
        <<"; Auto "<<displayBrightness(pureDiskOff)<<" -> "<<displayBrightness(pureDiskOn)<<"\n";
    if(displayBrightness(pureDiskOn)<=displayBrightness(pureDiskOff))success=false;
    KeyCallback(window,1200,0,GLFW_PRESS,0);lensFlareStrength=.45f;
    cameraPos={0,0,650};cameraFront={0,0,-1};cameraUp={0,1,0};fieldOfView=4;
    capture("gargantua-visible-from-mann-distance");
    ResetView();
    KeyCallback(window, 701, 0, GLFW_PRESS, 0);
    capture("miller-orbit");
    ToggleLanding();
    fieldOfView = 80;
    capture("miller-surface");
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
        success=false;
    }
    KeyCallback(window,1320,0,GLFW_PRESS,0);
    const auto disabled=capture("solar-prominences-disabled");
    size_t exteriorPlasma=0;
    for(size_t i=0;i<active.size();i+=3)
        if(disabled[i]<5 && disabled[i+1]<5 && disabled[i+2]<5
            && (active[i]>20 || active[i+1]>20 || active[i+2]>20))++exteriorPlasma;
    std::cout<<"Exterior plasma pixels: "<<exteriorPlasma<<"\n";
    if(exteriorPlasma<100)success=false;
    KeyCallback(window,1321,0,GLFW_PRESS,0);
    simulationTime+=180;
    if(active==capture("solar-prominences-evolved"))success=false;
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
    if(natural==warm)success=false;
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
        if(changed<100 || darkened>0)success=false;
        for(const auto& other:profileDifferences)if(difference==other)success=false;
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
            std::cerr<<"Occluded Sun produced lens flare\n";success=false;
        }
    // Reference-like framing: bright source above the optical center.
    starSystem.camera={0,0,.8};
    cameraFront=glm::normalize(glm::vec3(0,-.15,-1));cameraUp={0,1,0};
    ApplyVisionProfile(2);fieldOfView=45;
    optics.SetReferenceGlare(false);
    const auto referenceOff=capture("reference-glare-off");
    optics.SetReferenceGlare(true);
    const auto referenceOn=capture("reference-glare-on");
    if(referenceOff==referenceOn)success=false;
    // Camera adaptation continues independently of paused scene dynamics.
    KeyCallback(window,1201,0,GLFW_PRESS,0);
    KeyCallback(window,1202,0,GLFW_PRESS,0);
    exposureEv=0;
    const auto autoStar=capture("pure-auto-star");
    exposureEv=2;
    if(autoStar==capture("pure-auto-positive-bias"))success=false;
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
    if(!idleFrame || brightFirst==brightAdapted)success=false;
    KeyCallback(window,1203,0,GLFW_PRESS,0);
    if(autoStar==capture("pure-manual-star"))success=false;
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
        if(int(glareFrame[i])+1<int(cleanFrame[i])){success=false;std::cerr<<"Glare darkened the scene\n";break;}
    optics.SetAutoExposure(true);visionProfile=3;
    capture("exposure-independent-nikon");
    const auto nikonState=meterState();
    if(glareOffState[0]!=glareOnState[0] || glareOffState[1]!=glareOnState[1]
        || glareOffState[0]!=nikonState[0]){
        std::cerr<<"Optical glare affected metered exposure\n";success=false;
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
    if(maxPanEv-minPanEv>.2f)success=false;
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
                if(glm::length(offset*glm::vec2(aspect,1))<hostRadius*1.9f+.004f)success=false;
            }
        }
        std::cout<<"Stellar activity "<<activity<<" optical sources="<<count<<"; host centers="<<hostCenters<<"\n";
        if(hostCenters!=1)success=false;
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
        if((eligible==0 && count!=0) || (eligible==1 && count==0) || glGetError()!=GL_NO_ERROR)success=false;
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
    if(brightnessSignals[1]<=brightnessSignals[0])success=false;
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
    if(starBefore!=starAfter){std::cerr<<"Planet state contaminated stellar rendering\n";success=false;}
    size_t brightStellarPixels=0;
    for(size_t i=0;i<starAfter.size();i+=3)if(starAfter[i]>160)++brightStellarPixels;
    if(brightStellarPixels<1000){std::cerr<<"Cool star lacks emissive radiance\n";success=false;}
    StartSolarSystem();
    const auto galaxyTarget=blackHole.ResolveTarget(1610,simulationTime);
    const glm::dvec3 arrivalOffset(0,0,galaxyTarget.radius*5.0);
    starSystem.camera=galaxyTarget.position+arrivalOffset;
    cameraFront={0,0,-1};cameraUp={0,1,0};
    const auto arrivalTime=simulationTime;
    const auto firstGalaxyImage=capture("camera-proximity-galaxy-star");
    if(activeSystemTarget!=1610 || starSystem.Host().id!=1610)success=false;
    const auto firstPlanetSeed=starSystem.Planets()[0].seed;
    starSystem.camera={0,0,starSystem.DetailRadiusAu()*100};
    const auto beforeFarSolves=starSystem.PositionEvaluations();
    simulationPlaying=true;
    capture("distant-star-gpu-only-tier");
    simulationPlaying=false;
    const auto farSolves=starSystem.PositionEvaluations()-beforeFarSolves;
    std::cout<<"Inactive-system orbital evaluations="<<farSolves<<"\n";
    if(farSolves!=0)success=false;
    SelectCatalogue(-2);EnterSelectedSystem(false);
    simulationTime=arrivalTime;
    starSystem.camera=galaxyTarget.position+arrivalOffset;
    cameraFront={0,0,-1};cameraUp={0,1,0};
    const auto revisitedGalaxyImage=capture("camera-proximity-same-star-revisit");
    if(activeSystemTarget!=1610 || starSystem.Planets()[0].seed!=firstPlanetSeed)success=false;
    if(firstGalaxyImage!=revisitedGalaxyImage){std::cerr<<"Galaxy revisit changed the same seeded view\n";success=false;}
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
    KeyCallback(window,702,0,GLFW_PRESS,0);
    capture("mann-spacecraft-departure");
    simulationTime=111*Astronomy::secondsPerDay;
    capture("gargantua-spacecraft-periapsis");
    cameraPos=blackHole.SpacecraftPosition()+glm::normalize(blackHole.SpacecraftPosition())*(500/Astronomy::metresPerAu);
    lastSpacecraftPosition=blackHole.SpacecraftPosition();cameraUp={0,1,0};
    cameraFront=glm::normalize(glm::vec3(blackHole.ObservedSpacecraftPosition(cameraPos,simulationTime)-cameraPos));
    const auto nearKerrShip=capture("endurance-close-kerr");
    KeyCallback(window,1330,0,GLFW_PRESS,0);
    const auto nearStaticShip=capture("endurance-close-schwarzschild");
    GLint inspectionViewport[4];glGetIntegerv(GL_VIEWPORT,inspectionViewport);
    int solidShipPixels=0;
    for(int y=inspectionViewport[3]/2-100;y<inspectionViewport[3]/2+100;++y)
        for(int x=inspectionViewport[2]/2-100;x<inspectionViewport[2]/2+100;++x){
            const size_t i=(static_cast<size_t>(y)*inspectionViewport[2]+x)*3;
            if(i+2>=nearKerrShip.size())continue;
            const int r=nearKerrShip[i],g=nearKerrShip[i+1],b=nearKerrShip[i+2];
            if(r>60 && std::abs(r-g)<15 && std::abs(g-b)<15){
                ++solidShipPixels;
                for(int c=0;c<3;++c)if(std::abs(int(nearKerrShip[i+c])-int(nearStaticShip[i+c]))>2)success=false;
            }
        }
    std::cout<<"Foreground ship pixels stable across spin: "<<solidShipPixels<<"\n";
    if(solidShipPixels<100)success=false;
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,optics.SceneTexture());
    GLint hdrWidth,hdrHeight;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&hdrWidth);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&hdrHeight);
    std::vector<float> shipHdr(static_cast<size_t>(hdrWidth)*hdrHeight*4);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,shipHdr.data());glBindTexture(GL_TEXTURE_2D,0);
    for(float value:shipHdr)if(!std::isfinite(value)){std::cerr<<"Nonfinite ship HDR radiance\n";success=false;break;}
    KeyCallback(window,1332,0,GLFW_PRESS,0);
    simulationTime=180*Astronomy::secondsPerDay;
    capture("gargantua-spacecraft-escape");
    if(blackHole.SpacecraftEnergy()<=0 || !blackHole.RangersDetached())success=false;
    SetVehicleView(1);capture("endurance-interior");
    const auto cabinUp=cameraUp;
    KeyCallback(window,GLFW_KEY_R,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    KeyCallback(window,GLFW_KEY_F,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    if(followSelected || followSpacecraft || cameraUp!=cabinUp)success=false;
    KeyCallback(window,GLFW_KEY_L,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    capture("endurance-interior-looking-at-planet");
    if(glm::dot(cameraUp,glm::vec3(lastCabinRotation[1]))<.99999f)success=false;
    SetVehicleView(2);capture("ranger-1-detached-interior");
    SetVehicleView(3);capture("ranger-2-detached-interior");
    SetVehicleView(0);
    StartSolarSystem();starSystem.SelectPlanet(2);
    KeyCallback(window,GLFW_KEY_F,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    if(!starSystem.IsFollowing())success=false;
    capture("compact-position-follow-notification",true);
    fieldOfView=22.5;
    const auto beforeZoom=capture("zoom-readout-base");
    ShowZoomToast(true);
    if(speedToastUntil-glfwGetTime()>1.01 || speedToastUntil-glfwGetTime()<.8)success=false;
    const auto duringZoom=capture("compact-zoom-readout",true);
    speedToastUntil=glfwGetTime()-1;
    const auto expiredZoom=capture("zoom-readout-expired",true);
    if(beforeZoom==duringZoom || beforeZoom!=expiredZoom){std::cerr<<"Zoom toast did not appear/disappear independently\n";success=false;}
    fieldOfView=45;
    KeyCallback(window,GLFW_KEY_R,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    if(!starSystem.IsRotationFollowing())success=false;
    KeyCallback(window,GLFW_KEY_L,0,GLFW_PRESS,GLFW_MOD_SHIFT);
    if(glm::dot(cameraFront,glm::normalize(glm::vec3(starSystem.Position(2,simulationTime)-starSystem.camera)))<.99f)success=false;
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
        && black[0]==8 && unresolved[0]>resolved[0]+5 && probeError==GL_NO_ERROR;
    std::cout<<"Exposure meter EV: "<<initial[0]<<" -> "<<darkening[0]
             <<" -> "<<settled[0]<<"; brightening "<<brightening[0]
             <<"; black "<<black[0]<<"; valid="<<meterValid<<"\n";
    success=success && meterValid;
    probe.Release();glDeleteTextures(1,&meterInput);
    return success;
}
