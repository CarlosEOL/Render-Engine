#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "glad/glad.h"
#include <GLFW/glfw3.h>
#include "GalaxyBackground.h"
#include "AssetManager.h"
#include "Astronomy.h"
#include "UniverseCatalog.h"
#include "MilkyWayModel.h"
#include "NearbyGalaxies.h"
#include <filesystem>
#include <vector>
#include <random>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <string>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

unsigned int compileShaderProgram(const char*, const char*);

const std::array<GalaxyBackground::Group, 3>& GalaxyBackground::Groups() {
    static const std::array<Group,3> groups=[](){
        std::array<Group,3> result;
        for(int i=0;i<3;++i){const auto galaxy=Universe::GalaxyAt(i+1);result[i]={galaxy.centerAu,galaxy.tilt,static_cast<float>(galaxy.axesAu.x)};}
        return result;
    }();
    return groups;
}

bool GalaxyBackground::Initialize() {
    std::filesystem::path assetPath = "assets/galaxy.png";
#ifdef _WIN32
    wchar_t executable[32768];
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    if (length > 0 && length < 32768)
        assetPath = std::filesystem::path(executable).parent_path() / assetPath;
#endif
    texture = AssetManager::LoadTexture(assetPath.string().c_str(), true);
    const auto variantsPath=assetPath.parent_path()/"galaxy-variants.png";
    variants=AssetManager::LoadTexture(variantsPath.string().c_str(),true);
    hoagTexture=AssetManager::LoadTexture((assetPath.parent_path()/"galaxy-hoag-hubble.jpg").string().c_str(),true);
    if (!texture || !hoagTexture)
        return false;
    // The sprites are now distant, so use the generated mip chain to avoid
    // shimmering and losing the galaxy's brightness to point sampling.
    for(auto id:{texture,variants,hoagTexture})if(id){glBindTexture(GL_TEXTURE_2D,id);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);}
    glBindTexture(GL_TEXTURE_2D, 0);
    const char* vertex = R"(
#version 330 core
layout(location=0) in vec3 centerHigh;
layout(location=1) in vec3 centerLow;
layout(location=2) in vec3 normal;
layout(location=3) in vec3 majorAxis;
layout(location=4) in vec4 properties; // radius, intrinsic thickness, atlas variant, absolute magnitude
layout(location=5) in float galaxyId;
uniform mat4 viewProjection;
uniform vec3 cameraHigh,cameraLow;
uniform float magnitudeLimit;
uniform float fictionalFieldVisibility;
uniform float galaxyMagnitudeLimit;
uniform int catalogueVertexCount;
uniform vec2 closeIds,closeWeights;
out vec2 uv;
out float visibility;
out float morphology;
out float surfaceBrightness;
void main() {
    const vec2 corners[6] = vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(-1,1));
    vec2 corner = corners[gl_VertexID%6];
    uv = corner * 0.5 + 0.5;
    vec3 relative=(centerHigh-cameraHigh)+(centerLow-cameraLow);
    float distance=max(length(relative),1.0);
    float apparent=properties.w+5.0*log(max(distance/206264.806247/10.0,1e-12))/log(10.0);
    visibility=1.0-smoothstep(galaxyMagnitudeLimit-.3,galaxyMagnitudeLimit+.3,apparent);
    if(abs(galaxyId-closeIds.x)<.1)visibility*=1.0-closeWeights.x;
    if(abs(galaxyId-closeIds.y)<.1)visibility*=1.0-closeWeights.y;
    if(gl_VertexID>=catalogueVertexCount)visibility*=fictionalFieldVisibility;
    float area=max(1.0,3.14159265*pow(properties.x/distance*206264.806,2.0));
    float mu=apparent+2.5*log(area)/log(10.0);
    surfaceBrightness=clamp(1.2*pow(10.0,-.4*(mu-21.0)),.008,3.0);
    morphology=properties.z;
    if(visibility<=0.0){gl_Position=vec4(2,2,2,1);return;}
    vec3 direction=relative/distance;
    vec3 projectedAxis=majorAxis-direction*dot(majorAxis,direction);
    if(length(projectedAxis)<.01)projectedAxis=cross(normal,direction);
    vec3 right=normalize(projectedAxis),up=normalize(cross(direction,right));
    float cosine=dot(normal,direction);
    if(cosine<0.0)uv.y=1.0-uv.y;
    float ratio=sqrt(properties.y*properties.y+(1.0-properties.y*properties.y)*cosine*cosine);
    // Finite-thickness projected galaxies remain visible when edge-on. Their
    // centers stay in world space; these are not camera-attached sky stickers.
    gl_Position = viewProjection * vec4(relative+properties.x*(corner.x*right+corner.y*up*ratio),1);
    gl_Position.z=gl_Position.w*.999999;
}
)";
    // Both representations use the same porous, feathered emission map.
    const std::string emission=R"(
uniform sampler2D galaxy;
uniform sampler2D galaxyVariants;
uniform sampler2D hoag;
vec3 columnLight(vec2 uv,int variant,float lod) {
    if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))))return vec3(0);
    vec2 coordinate=uv;
    if(variant>0 && variant<3)coordinate.y=uv.y*.5+(variant==1?.5:0.0);
    vec2 atlasSize=vec2(textureSize(galaxyVariants,0));
    float atlasLod=lod<0.0?clamp(log2(max(max(length(dFdx(coordinate)*atlasSize),length(dFdy(coordinate)*atlasSize)),1.0)),0.0,5.0):clamp(lod,0.0,5.0);
    vec4 image = variant==0 || variant==3?(lod<0.0?texture(galaxy,coordinate):textureLod(galaxy,coordinate,lod)):textureLod(galaxyVariants,coordinate,atlasLod);
    vec2 p=uv*2.0-1.0;
    float radius=length(p);
    if(variant==4){
        // Sample the unchanged NASA image around its actual nucleus. Remove
        // photographic sky and unrelated objects from the detached ring gap.
        vec2 observed=vec2(.527,.505)+p*vec2(.335,.3422);
        image=lod<0.0?texture(hoag,observed):textureLod(hoag,observed,lod);
        image.rgb=max(image.rgb-vec3(.0025),vec3(0));
        float core=1.0-smoothstep(.18,.42,radius);
        float ring=smoothstep(.52,.65,radius)*(1.0-smoothstep(.88,1.0,radius));
        image.a=max(core,ring)*smoothstep(.0005,.008,max(image.r,max(image.g,image.b)));
    }
    if(variant==5)image=vec4(vec3(1.0,.82,.59)*exp(-4.5*radius),1.0);
    if(variant==3){
        // Broken, asymmetric knots rather than a miniature grand-design spiral.
        float knots=.6+.4*sin(p.x*23.0+sin(p.y*15.0))*sin(p.y*19.0-p.x*7.0);
        image.rgb=mix(image.rgb,vec3(.20,.31,.43)*image.a,.65)*knots;
        image.a*=(1.0-smoothstep(.15,1.0,length(p*vec2(.8,1.1))))*knots;
    }
    // Keep atlas filtering inside one image instead of leaking the adjacent
    // galaxy through the seam at coarse mip levels.
    float border=smoothstep(0.0,.06,min(min(uv.x,uv.y),min(1.0-uv.x,1.0-uv.y)));
    // The RGBA asset now has porous arms. Keep low-alpha wisps instead of
    // thresholding them away, and dissolve the baked circular perimeter over
    // a broad band rather than merely trimming the corners of the quad.
    float feather=variant==4?1.0:1.0-smoothstep(.35,1.0,radius);
    float density=pow(image.a,1.15)*feather*border;
    return image.rgb*density;
}
)";
    std::string farFragment=std::string("#version 330 core\n")+emission;
    farFragment+=R"(
in vec2 uv;
in float visibility;
in float morphology;
in float surfaceBrightness;
out vec4 color;
void main(){
    vec3 light=columnLight(uv,int(morphology+.5),-1.0)*surfaceBrightness*visibility;
    if(max(light.r,max(light.g,light.b))<.00001)discard;
    color=vec4(light,.25);
}
)";
    program = compileShaderProgram(vertex, farFragment.c_str());
    const char* screenVertex=R"(
#version 330 core
out vec2 screen;
void main(){
    screen=vec2((gl_VertexID<<1)&2,gl_VertexID&2)*2.0-1.0;
    gl_Position=vec4(screen,0,1);
}
)";
    const std::string bake=std::string("#version 330 core\n")+emission+R"(
in vec2 screen;
uniform vec3 localCamera;
uniform int variant,marchSteps;
out vec4 color;
void main(){
    vec2 angle=screen*vec2(3.14159265,1.57079633);
    vec3 ray=vec3(cos(angle.y)*cos(angle.x),sin(angle.y),cos(angle.y)*sin(angle.x));
    vec3 extent=vec3(1,variant==5?.7:.45,1);
    vec3 safeRay=sign(ray+vec3(1e-12))*max(abs(ray),vec3(1e-6));
    vec3 a=(-extent-localCamera)/safeRay,b=(extent-localCamera)/safeRay;
    vec3 nearHit=min(a,b),farHit=max(a,b);
    float start=max(0.0,max(nearHit.x,max(nearHit.y,nearHit.z)));
    float end=min(farHit.x,min(farHit.y,farHit.z));
    if(end<=start){color=vec4(0);return;}
    // Bound the march to the physical volume and sample its finite thickness;
    // never project a second disk behind the observer.
    float stepSize=(end-start)/float(marchSteps);
    vec3 light=vec3(0);
    for(int i=0;i<128;++i){
        if(i>=marchSteps)break;
        vec3 p=localCamera+ray*(start+(float(i)+.5)*stepSize);
        float r=length(p.xz);
        if(r>=1.0)continue;
        vec3 column=columnLight(vec2(p.x,-p.z)*.5+.5,variant,2.0);
        float height=variant==5?.20:(.035+.02*r);
        float vertical=exp(-abs(p.y)/height)/(2.0*height);
        // A separate round stellar bulge gives edge-on and interior views
        // finite thickness rather than another camera-facing image.
        float core=exp(-r*r/.035);
        vec3 bulge=variant==4?vec3(0):vec3(.50,.38,.25)*exp(-length(p/vec3(.16,.10,.16)))/.20;
        light+=(column*vertical*(1.0-.45*core)+bulge)*stepSize;
    }
    color=vec4(light,0);
}
)";
    const char* display=R"(
#version 330 core
in vec2 screen;
uniform mat4 inverseViewProjection;
uniform mat3 worldToGalaxy;
uniform sampler2D currentVolume,previousVolume;
uniform float cacheBlend,weight;
out vec4 color;
void main(){
    vec3 worldRay=normalize((inverseViewProjection*vec4(screen,1,1)).xyz);
    vec3 ray=normalize(worldToGalaxy*worldRay);
    vec2 uv=vec2(atan(ray.z,ray.x)/6.28318530+.5,asin(clamp(ray.y,-1.0,1.0))/3.14159265+.5);
    vec3 light=mix(texture(previousVolume,uv).rgb,texture(currentVolume,uv).rgb,cacheBlend)*weight;
    if(max(light.r,max(light.g,light.b))<.00001)discard;
    color=vec4(light,.25);
}
)";
    volumeBake=compileShaderProgram(screenVertex,bake.c_str());
    volumeDisplay=compileShaderProgram(screenVertex,display);
    int linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    GLint bakeLinked=0,displayLinked=0;
    glGetProgramiv(volumeBake,GL_LINK_STATUS,&bakeLinked);glGetProgramiv(volumeDisplay,GL_LINK_STATUS,&displayLinked);
    if (!linked || !bakeLinked || !displayLinked) {
        Release();
        return false;
    }
    struct Vertex {glm::vec3 high,low,normal,major;glm::vec4 properties;float id;};
    std::vector<Vertex> vertices;vertices.reserve((NearbyGalaxies::Catalog.size()+3+DistantGalaxyCount)*6);
    volumeGalaxies.clear();
    const auto append=[&](int id,const glm::dvec3& center,double radius,const glm::dmat3& rotation,int variant,float magnitude){
        const glm::vec3 high(center),low(center-glm::dvec3(high));
        const Vertex v{high,low,glm::vec3(rotation[1]),glm::vec3(rotation[0]),
            {static_cast<float>(radius),variant==2?.65f:.13f,static_cast<float>(variants?variant:0),magnitude},static_cast<float>(id)};
        volumeGalaxies.push_back({id,variant,center,rotation,radius,magnitude});
        for(int i=0;i<6;++i)vertices.push_back(v);
    };
    int namedId=4;
    for(const auto& galaxy:NearbyGalaxies::Catalog)
        append(namedId++,galaxy.Center(),galaxy.Radius(),galaxy.Frame(),galaxy.morphology,
            static_cast<float>(galaxy.magnitude-5*std::log10(galaxy.distanceLy/3.261563777/10)));
    for(int i=0;i<3;++i){const auto group=Universe::GalaxyAt(i+1);append(i+1,group.centerAu,group.axesAu.x,group.rotation,i,-21.4f);}
    // Fixed seeded far-field sample. Upload once: no per-frame CPU galaxy
    // generation, movement, sorting, or gravitational integration.
    for(int i=0;i<DistantGalaxyCount;++i){
        const auto group=Universe::GalaxyAt(Universe::MainStreamGalaxies+i);
        append(10+i,group.centerAu,group.axesAu.x,group.rotation,i%3,
            static_cast<float>(-20.5-5*std::log10(group.axesAu.x/(50000*Astronomy::auPerLightYear))));
    }
    vertexCount=static_cast<int>(vertices.size());
    glGenVertexArrays(1, &vao);glGenBuffers(1,&buffer);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STATIC_DRAW);
    const size_t offsets[]={offsetof(Vertex,high),offsetof(Vertex,low),offsetof(Vertex,normal),offsetof(Vertex,major),offsetof(Vertex,properties)};
    for(unsigned i=0;i<5;++i){glEnableVertexAttribArray(i);glVertexAttribPointer(i,i==4?4:3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsets[i]));}
    glEnableVertexAttribArray(5);glVertexAttribPointer(5,1,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,id)));
    glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);
    glGenFramebuffers(1,&volumeFramebuffer);
    for(auto& cache:volumeCaches){
        glGenTextures(1,&cache.texture);glGenTextures(1,&cache.previous);
        for(auto id:{cache.texture,cache.previous}){
            glBindTexture(GL_TEXTURE_2D,id);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        }
    }
    glBindTexture(GL_TEXTURE_2D,0);
    return true;
}

void GalaxyBackground::ObserveCamera(const glm::dvec3& camera,double seconds) const {
    if(motionValid && adaptive && seconds>0 && std::isfinite(seconds)){
        const double speed=glm::length(camera-observedCamera)/seconds;
        for(const auto& galaxy:volumeGalaxies)
            if(glm::length(camera-galaxy.centre)<galaxy.radius*3.25 && speed>galaxy.radius*.002){lastMotion=glfwGetTime();break;}
    }
    observedCamera=camera;motionValid=true;
}

bool GalaxyBackground::NeedsFrame() const {
    const bool moving=adaptive && glfwGetTime()-lastMotion<.25;
    for(const auto& cache:volumeCaches)if(cache.valid){
        if((cache.blending && cache.lastBlend<1) || cache.moving!=moving)return true;
        // A translation received during a fade still needs its own refresh
        // after the terminal blend frame, even if the camera has now stopped.
        if(motionValid)for(const auto& galaxy:volumeGalaxies)if(galaxy.id==cache.id){
            const auto local=glm::transpose(galaxy.frame)*(observedCamera-galaxy.centre)/galaxy.radius;
            if(glm::length(local-cache.camera)>(moving?.0015:.00015))return true;
            break;
        }
    }
    return false;
}

void GalaxyBackground::BakeVolume(VolumeCache& cache,const VolumeGalaxy& galaxy,const glm::dvec3& local,bool moving) const {
    GLint target,viewport[4];glGetIntegerv(GL_FRAMEBUFFER_BINDING,&target);glGetIntegerv(GL_VIEWPORT,viewport);
    const bool depth=glIsEnabled(GL_DEPTH_TEST),blend=glIsEnabled(GL_BLEND),cull=glIsEnabled(GL_CULL_FACE);
    cache.blending=adaptive && cache.valid && glm::length(local-cache.camera)<.03;
    if(cache.valid){std::swap(cache.texture,cache.previous);std::swap(cache.width,cache.previousWidth);}
    const int width=moving?512:1024;
    glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,cache.texture);
    if(cache.width!=width){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,width/2,0,GL_RGBA,GL_FLOAT,nullptr);cache.width=width;}
    glBindFramebuffer(GL_FRAMEBUFFER,volumeFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,cache.texture,0);
    glViewport(0,0,width,width/2);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    glUseProgram(volumeBake);
    const auto p=glm::vec3(local);glUniform3fv(glGetUniformLocation(volumeBake,"localCamera"),1,glm::value_ptr(p));
    glUniform1i(glGetUniformLocation(volumeBake,"variant"),galaxy.morphology);
    glUniform1i(glGetUniformLocation(volumeBake,"marchSteps"),moving?40:96);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);glUniform1i(glGetUniformLocation(volumeBake,"galaxy"),0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,variants?variants:texture);glUniform1i(glGetUniformLocation(volumeBake,"galaxyVariants"),1);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,hoagTexture);glUniform1i(glGetUniformLocation(volumeBake,"hoag"),2);
    glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER,target);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    if(depth)glEnable(GL_DEPTH_TEST);if(blend)glEnable(GL_BLEND);if(cull)glEnable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    cache.camera=local;cache.baked=glfwGetTime();cache.valid=true;cache.moving=moving;cache.lastBlend=cache.blending?0.f:1.f;++volumeBuilds;
}

void GalaxyBackground::DrawVolume(VolumeCache& cache,const VolumeGalaxy& galaxy,const glm::mat4& vp,float weight,float brightness) const {
    glUseProgram(volumeDisplay);
    const auto inverse=glm::inverse(vp);const auto orientation=glm::mat3(glm::transpose(galaxy.frame));
    glUniformMatrix4fv(glGetUniformLocation(volumeDisplay,"inverseViewProjection"),1,GL_FALSE,glm::value_ptr(inverse));
    glUniformMatrix3fv(glGetUniformLocation(volumeDisplay,"worldToGalaxy"),1,GL_FALSE,glm::value_ptr(orientation));
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,cache.texture);glUniform1i(glGetUniformLocation(volumeDisplay,"currentVolume"),0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,cache.blending?cache.previous:cache.texture);glUniform1i(glGetUniformLocation(volumeDisplay,"previousVolume"),1);
    const float t=cache.blending?glm::clamp(static_cast<float>((glfwGetTime()-cache.baked)/.18),0.f,1.f):1.f;
    cache.lastBlend=t*t*(3-2*t);glUniform1f(glGetUniformLocation(volumeDisplay,"cacheBlend"),cache.lastBlend);
    glUniform1f(glGetUniformLocation(volumeDisplay,"weight"),weight*brightness);
    glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);glBindVertexArray(0);glActiveTexture(GL_TEXTURE0);
}

void GalaxyBackground::Render(const glm::mat4& viewProjection, const glm::dvec3& cameraPosition,float magnitudeLimit,bool localVolumes) const {
    if (!program || !texture)
        return;
    std::array<const VolumeGalaxy*,2> selected{};
    std::array<float,2> weights{};
    std::array<double,2> distances{1e30,1e30};
    if(localVolumes)for(const auto& galaxy:volumeGalaxies){
        const double distance=glm::length(cameraPosition-galaxy.centre)/galaxy.radius;
        const double apparent=galaxy.absoluteMagnitude+5*std::log10(std::max(1e-12,distance*galaxy.radius/206264.806247/10));
        if(distance>=3.25 || apparent>magnitudeLimit+.3)continue;
        if(distance<distances[1]){
            const int slot=distance<distances[0]?0:1;
            if(slot==0){selected[1]=selected[0];distances[1]=distances[0];}
            selected[slot]=&galaxy;distances[slot]=distance;
        }
    }
    // Preserve each galaxy's cached light when a companion becomes nearer.
    if(selected[0] && volumeCaches[1].id==selected[0]->id)std::swap(volumeCaches[0],volumeCaches[1]);
    const bool moving=adaptive && glfwGetTime()-lastMotion<.25;
    for(int i=0;i<2;++i){
        auto& cache=volumeCaches[i];
        if(!selected[i]){if(localVolumes){cache.valid=false;cache.id=-1;}continue;}
        const auto& galaxy=*selected[i];
        const auto local=glm::transpose(galaxy.frame)*(cameraPosition-galaxy.centre)/galaxy.radius;
        if(cache.id!=galaxy.id){cache.valid=false;cache.id=galaxy.id;}
        const double displacement=glm::length(local-cache.camera);
        const double threshold=moving?.0015:.00015;
        if(!cache.valid || displacement>.1 || ((displacement>threshold || cache.moving!=moving)
            && (!cache.blending || cache.lastBlend>=1.f)))BakeVolume(cache,galaxy,local,moving);
        weights[i]=static_cast<float>(1-glm::smoothstep(2.25,3.25,distances[i]));
    }
    glUseProgram(program);
    glUniform2f(glGetUniformLocation(program,"closeIds"),selected[0]?static_cast<float>(selected[0]->id):-1.f,selected[1]?static_cast<float>(selected[1]->id):-1.f);
    glUniform2f(glGetUniformLocation(program,"closeWeights"),weights[0],weights[1]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glUniform1i(glGetUniformLocation(program, "galaxy"), 0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,variants?variants:texture);
    glUniform1i(glGetUniformLocation(program,"galaxyVariants"),1);glActiveTexture(GL_TEXTURE0);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,hoagTexture);
    glUniform1i(glGetUniformLocation(program,"hoag"),2);glActiveTexture(GL_TEXTURE0);
    glUniformMatrix4fv(glGetUniformLocation(program, "viewProjection"), 1, GL_FALSE, glm::value_ptr(viewProjection));
    const glm::vec3 high(cameraPosition),low(cameraPosition-glm::dvec3(high));
    glUniform3fv(glGetUniformLocation(program,"cameraHigh"),1,glm::value_ptr(high));
    glUniform3fv(glGetUniformLocation(program,"cameraLow"),1,glm::value_ptr(low));
    glUniform1f(glGetUniformLocation(program,"magnitudeLimit"),magnitudeLimit);
    glUniform1i(glGetUniformLocation(program,"catalogueVertexCount"),static_cast<int>(NearbyGalaxies::Catalog.size()*6));
    // Only galaxy visibility responds to a large foreground galaxy here. Star
    // batches keep the user's own limiting magnitude and catalogue identities.
    double limit=magnitudeLimit;
    if(autoExposure){
        for(const auto& galaxy:NearbyGalaxies::Catalog){
            const double distance=glm::length(galaxy.Center()-cameraPosition);
            if(distance>galaxy.Radius()*1.25 && distance<galaxy.Radius()*10){
                const double apparent=galaxy.magnitude+5*std::log10(distance/(galaxy.distanceLy*Astronomy::auPerLightYear));
                limit=std::min(limit,magnitudeLimit-std::max(0.0,-apparent)*.75);
            }
        }
    }
    glUniform1f(glGetUniformLocation(program,"galaxyMagnitudeLimit"),static_cast<float>(limit));
    const double distanceKpc=glm::length(cameraPosition-MilkyWayModel::CenterAu())/MilkyWayModel::auPerKpc;
    glUniform1f(glGetUniformLocation(program,"fictionalFieldVisibility"),static_cast<float>(glm::smoothstep(40.0,100.0,distanceKpc)));
    GLboolean depthMask;glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
    const bool culling=glIsEnabled(GL_CULL_FACE)==GL_TRUE;
    GLint src,dst,srcAlpha,dstAlpha;glGetIntegerv(GL_BLEND_SRC_RGB,&src);glGetIntegerv(GL_BLEND_DST_RGB,&dst);glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);
    glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);
    const bool blending=glIsEnabled(GL_BLEND)==GL_TRUE;
    glEnable(GL_BLEND);
    // Add emitted light without covering the sky; keep a non-emitter class for
    // galaxy-only display controls. Stars render afterward and keep their mask.
    glBlendFuncSeparate(GL_ONE,GL_ONE,GL_ONE,GL_ZERO);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES,0,vertexCount);
    glBindVertexArray(0);
    const bool depth=glIsEnabled(GL_DEPTH_TEST);glDisable(GL_DEPTH_TEST);
    for(int i=0;i<2;++i)if(selected[i]){
        const auto& galaxy=*selected[i];
        const double distance=std::max(1.0,distances[i]*galaxy.radius);
        const double apparent=galaxy.absoluteMagnitude+5*std::log10(distance/206264.806247/10);
        const double area=std::max(1.0,glm::pi<double>()*std::pow(galaxy.radius/distance*206264.806,2));
        const double mu=apparent+2.5*std::log10(area);
        float brightness=static_cast<float>(glm::clamp(1.2*std::pow(10.,-.4*(mu-21)),.008,3.0));
        brightness*=static_cast<float>(1-glm::smoothstep(limit-.3,limit+.3,apparent));
        if(galaxy.id>=10)brightness*=static_cast<float>(glm::smoothstep(40.0,100.0,distanceKpc));
        DrawVolume(volumeCaches[i],galaxy,viewProjection,weights[i],brightness);
    }
    if(depth)glEnable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBlendFuncSeparate(src,dst,srcAlpha,dstAlpha);
    glDepthMask(depthMask);if(culling)glEnable(GL_CULL_FACE);
    if(!blending)glDisable(GL_BLEND);
}

void GalaxyBackground::Release() {
    for(auto& cache:volumeCaches){if(cache.texture)glDeleteTextures(1,&cache.texture);if(cache.previous)glDeleteTextures(1,&cache.previous);cache=VolumeCache{};}
    if(volumeBake)glDeleteProgram(volumeBake);if(volumeDisplay)glDeleteProgram(volumeDisplay);if(volumeFramebuffer)glDeleteFramebuffers(1,&volumeFramebuffer);
    volumeBake=volumeDisplay=volumeFramebuffer=volumeBuilds=0;volumeGalaxies.clear();motionValid=false;lastMotion=-1e30;
    if (program) glDeleteProgram(program);
    if (vao) glDeleteVertexArrays(1, &vao);
    if(buffer)glDeleteBuffers(1,&buffer);
    if (texture) glDeleteTextures(1, &texture);
    if (variants) glDeleteTextures(1,&variants);
    if(hoagTexture)glDeleteTextures(1,&hoagTexture);
    program = vao = buffer = texture = variants = hoagTexture = 0;vertexCount=0;
}
