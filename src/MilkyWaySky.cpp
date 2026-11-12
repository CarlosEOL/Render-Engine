#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "MilkyWaySky.h"
#include "AssetManager.h"
#include "MilkyWayModel.h"
#include "CelestialPhenomena.h"
#include "Astronomy.h"
#include "glad/glad.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <algorithm>
#include <cmath>
#include <filesystem>

unsigned int compileShaderProgram(const char*,const char*);
namespace {
constexpr int cacheWidth=2048,cacheHeight=1024;
constexpr const char* vertex=R"(#version 330 core
out vec2 screenPosition;
void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2));screenPosition=p*2.0-1.0;gl_Position=vec4(screenPosition,0,1);}
)";
bool Linked(unsigned program){GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);return linked!=0;}
// The same world-aligned artwork is sampled by the volume and its distant LOD.
// It describes unresolved stellar light, never optical-source eligibility.
constexpr const char* appearance=R"(
uniform sampler2D galacticIllustration;
uniform bool hasIllustration;
uniform float exteriorWeight;
vec3 illustratedLight(vec3 p,float lod){
 // Rotate the illustrated bar onto the existing model's 30-degree axis.
 vec2 planar=mat2(.965925826,.258819045,-.258819045,.965925826)*p.xz;
 vec2 uv=planar/36.0+.5;
 if(any(lessThan(uv,vec2(0))) || any(greaterThan(uv,vec2(1))))return vec3(0);
 vec3 image=lod<0.0?texture(galacticIllustration,uv).rgb:textureLod(galacticIllustration,uv,lod).rgb;
 // Convert the display-referred illustration to a linear artistic light field.
 return pow(max(image,vec3(0)),vec3(2.2));
}
float exteriorRayWeight(vec3 ray){return exteriorWeight*smoothstep(.045,.16,abs(ray.y));}
)";
}
bool MilkyWaySky::Initialize(){
    const std::string bake=std::string(R"(#version 330 core
in vec2 screenPosition;out vec4 color;
uniform vec3 cameraKpc;
uniform vec4 nebula0,nebula1,nebula2,nebula3;
uniform vec3 helixAxis;
float hash3(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float cloudNoise(vec3 p){vec3 b=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
 return mix(mix(mix(hash3(b),hash3(b+vec3(1,0,0)),f.x),mix(hash3(b+vec3(0,1,0)),hash3(b+vec3(1,1,0)),f.x),f.y),
 mix(mix(hash3(b+vec3(0,0,1)),hash3(b+vec3(1,0,1)),f.x),mix(hash3(b+vec3(0,1,1)),hash3(b+vec3(1,1,1)),f.x),f.y),f.z);}
float clouds(vec3 p){return .58*cloudNoise(p*3.7)+.28*cloudNoise(p*11.3+7.0)+.14*cloudNoise(p*32.0+19.0);}
vec3 nebulaLight(vec3 direction,vec4 field,int style){
 vec3 origin=field.xyz;if(length(origin)>5000.0)return vec3(0);float along=dot(origin,direction);
 float discriminant=along*along-dot(origin,origin)+1.0;
 if(discriminant<=0.0)return vec3(0);
 float root=sqrt(discriminant),start=max(0.0,-along-root),finish=-along+root;
 if(finish<=start)return vec3(0);
 float step=(finish-start)/40.0,passing=1.0;vec3 emission=vec3(0);
 for(int i=0;i<40;++i){
  vec3 p=origin+direction*(start+(float(i)+.5)*step);
  float radius=length(p),noise=clouds(p*5.0+float(style)*31.0);
  float shell=exp(-pow((radius-.76)/.075,2.0));
  float density=pow(max(noise-.32,0.0),1.7)*8.0*(1.0-smoothstep(.7,1.0,radius));
  vec3 color=mix(vec3(.20,.48,.9),vec3(1.0,.13,.35),smoothstep(.2,.8,noise));
  if(style==1){float z=dot(p,helixAxis),ringRadius=sqrt(max(0.0,dot(p,p)-z*z));float torus=exp(-pow((ringRadius-.66)/.12,2.0)-pow(z/.18,2.0));density=torus*(.5+noise)*3.0+shell*.025;color=mix(vec3(.15,.85,1.0),vec3(1.0,.22,.12),smoothstep(.6,.84,ringRadius));}
  if(style==2){float threads=pow(1.0-abs(sin((p.x+p.z)*31.0+noise*14.0)),3.0);density=(shell*threads*3.0+.15*density);color=mix(vec3(.12,.45,.8),vec3(1.0,.48,.12),shell);}
  if(style==3){float threads=pow(1.0-abs(sin(p.y*51.0+noise*27.0)),4.0);density=shell*threads*4.0;color=mix(vec3(.15,.9,1.0),vec3(1.0,.15,.23),noise);}
  float extinction=density*(style==0?1.2:.4),transmission=exp(-extinction*step);
  emission+=passing*color*density*step*.4;passing*=transmission;
  if(passing<.01)break;
 }
 return emission;
}
)")+MilkyWayModel::ShaderFunctions()+appearance+R"(
void main(){
 vec2 uv=screenPosition*.5+.5;
 float longitude=(uv.x-.5)*6.28318530718,latitude=(uv.y-.5)*3.14159265359;
 vec3 direction=vec3(cos(latitude)*cos(longitude),sin(latitude),cos(latitude)*sin(longitude));
 // The thin stellar/dust region is bounded before marching. A face-on view
 // therefore resolves its vertical scale instead of skipping a thin slab
 // while traversing the entire halo at kiloparsec intervals.
 vec3 inverse=1.0/(sign(direction)*max(abs(direction),vec3(1e-7))+vec3(1e-12));
 vec3 near=(-vec3(18,4,18)-cameraKpc)*inverse,far=(vec3(18,4,18)-cameraKpc)*inverse;
 vec3 low=min(near,far),high=max(near,far);
 float begin=max(0.0,max(low.x,max(low.y,low.z))),end=min(high.x,min(high.y,high.z));
 vec3 light=vec3(0),transmission=vec3(1);
 if(end>begin && exteriorRayWeight(direction)<.9999){
  float step=(end-begin)/96.0;
  for(int i=0;i<96;++i){
   vec3 p=cameraKpc+direction*(begin+(float(i)+.5)*step);
   vec4 density=mwDensities(p);
   float noise=clouds(p),clumping=mix(.12,4.0,smoothstep(.28,.76,noise));
   vec3 opticalDepth=density.w*clumping*step*vec3(1.6,2.15,3.1);
   vec3 passing=exp(-opticalDepth);
   vec3 emission=(density.x*vec3(.96,.84,.69)+density.y*vec3(.53,.70,1.0)*(.45+noise)
        +density.z*vec3(1.0,.69,.43))*.055;
   if(hasIllustration){
    float height=abs(p.y-mwWarp(p));
    float thin=.16+.012*max(0.0,length(p.xz)-8.0);
    float vertical=.78*exp(-height/thin)/(2.0*thin)+.22*exp(-height/.48)/.96;
    vec3 art=illustratedLight(p,1.5);
    float structure=.65+.7*smoothstep(.25,.8,noise);
    vec3 coarse=illustratedLight(p,4.0);
    float detail=clamp(dot(art,vec3(.2126,.7152,.0722))/max(.004,dot(coarse,vec3(.2126,.7152,.0722))),.25,2.5);
    // A face-on illustration already integrates the whole stellar column.
    // Inside the disk, use local 3D emissivity, avoiding an all-sky blue fog.
    vec3 interior=(density.x*vec3(.96,.85,.72)*.25
        +density.y*vec3(.55,.73,1.0)*.40+density.z*vec3(1.0,.76,.46)*.10)*structure*detail;
    vec3 exterior=art*vertical*.8+density.z*vec3(1.0,.79,.51)*.012;
    emission=mix(interior,exterior,exteriorWeight);
   }
   emission*=step*(1.0-exteriorRayWeight(direction));
   vec3 integral=(vec3(1)-passing)/max(opticalDepth,vec3(.0001));
   // For vanishing extinction, lim((1-exp(-tau))/tau)=1.
   integral=mix(integral,vec3(1),lessThan(opticalDepth,vec3(.0001)));
   light+=transmission*emission*integral;
   transmission*=passing;
   if(max(transmission.r,max(transmission.g,transmission.b))<.001)break;
  }
 }
 light+=nebulaLight(direction,nebula0,0)+nebulaLight(direction,nebula1,1)
     +nebulaLight(direction,nebula2,2)+nebulaLight(direction,nebula3,3);
 color=vec4(clamp(light,vec3(0),vec3(200)),0);
}
)";
    const std::string display=std::string(R"(#version 330 core
in vec2 screenPosition;out vec4 color;
uniform mat4 inverseViewProjection;
uniform mat3 worldToGalactic;
uniform sampler2D milkyWay;
uniform vec3 cameraKpc;
)")+MilkyWayModel::ShaderFunctions()+appearance+R"(
void main(){
 vec4 q=inverseViewProjection*vec4(screenPosition,1,1);
 vec3 ray=normalize(worldToGalactic*normalize(q.xyz));
 vec2 uv=vec2(atan(ray.z,ray.x)/6.28318530718+.5,asin(clamp(ray.y,-1.0,1.0))/3.14159265359+.5);
 vec3 light=texture(milkyWay,uv).rgb;
 float weight=hasIllustration?exteriorRayWeight(ray):0.0;
 if(weight>0.0){
  // Full-resolution, world-plane slices keep distant arms and individual
  // illustrated star clusters sharp instead of magnifying a sky-cache texel.
  // Near the disk they fade continuously into the integrated 3D light field.
  vec3 resolved=vec3(0);float normalization=0.0;
  for(int i=0;i<8;++i){
   float height=(float(i)-3.5)*.20;
   float along=(height-cameraKpc.y)/ray.y;
   if(along<=0.0)continue;
   vec3 p=cameraKpc+ray*along;
   float radius=length(p.xz);
   float thickness=mix(.17,.55,exp(-radius*radius/8.0));
   float layer=exp(-abs(height)/thickness)/thickness;
   resolved+=illustratedLight(p,3.5)*layer;
   normalization+=layer;
  }
  if(normalization>0.0){
   vec3 mid=cameraKpc-ray*(cameraKpc.y/ray.y);
   vec3 fine=illustratedLight(mid,-1.0)-illustratedLight(mid,3.5);
   light+=weight*max(resolved/normalization+fine,vec3(0))*.8/max(abs(ray.y),.16);
  }
 }
 color=vec4(max(light,vec3(0)),0);
}
)";
    bakeProgram=compileShaderProgram(vertex,bake.c_str());
    displayProgram=compileShaderProgram(vertex,display.c_str());
    if(!Linked(bakeProgram)||!Linked(displayProgram)){Release();return false;}
    std::filesystem::path artPath="assets/galactic/milky-way-nasa.jpg";
#ifdef _WIN32
    wchar_t executable[32768];const auto length=GetModuleFileNameW(nullptr,executable,32768);
    if(length>0 && length<32768)artPath=std::filesystem::path(executable).parent_path()/artPath;
#endif
    illustration=AssetManager::LoadTexture(artPath.string().c_str(),false);
    if(illustration){glBindTexture(GL_TEXTURE_2D,illustration);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);}
    glGenVertexArrays(1,&vao);glGenTextures(1,&texture);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,cacheWidth,cacheHeight,0,GL_RGBA,GL_FLOAT,nullptr);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    const bool complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER,0);glBindTexture(GL_TEXTURE_2D,0);
    if(!complete){Release();return false;}
    return true;
}
void MilkyWaySky::Bake(const glm::dvec3& camera) const {
    GLint target,viewport[4];glGetIntegerv(GL_FRAMEBUFFER_BINDING,&target);glGetIntegerv(GL_VIEWPORT,viewport);
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glViewport(0,0,cacheWidth,cacheHeight);
    glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glUseProgram(bakeProgram);
    const auto local=glm::vec3(MilkyWayModel::ToModelKpc(camera));
    glUniform3fv(glGetUniformLocation(bakeProgram,"cameraKpc"),1,glm::value_ptr(local));
    const float exterior=glm::smoothstep(.4f,1.5f,std::abs(local.y));
    glUniform1f(glGetUniformLocation(bakeProgram,"exteriorWeight"),illustration?exterior:0.f);
    glUniform1i(glGetUniformLocation(bakeProgram,"hasIllustration"),illustration?1:0);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,illustration);
    glUniform1i(glGetUniformLocation(bakeProgram,"galacticIllustration"),0);
    const auto frame=glm::transpose(MilkyWayModel::Orientation());
    const auto helixAxis=glm::vec3(glm::normalize(frame*CelestialPhenomena::Nebulae()[1].positionAu));
    glUniform3fv(glGetUniformLocation(bakeProgram,"helixAxis"),1,glm::value_ptr(helixAxis));
    for(size_t i=0;i<CelestialPhenomena::Nebulae().size();++i){
        const auto& nebula=CelestialPhenomena::Nebulae()[i];
        const auto origin=glm::vec3(frame*(camera-nebula.positionAu)/(nebula.radiusLy*Astronomy::auPerLightYear));
        glUniform4f(glGetUniformLocation(bakeProgram,("nebula"+std::to_string(i)).c_str()),origin.x,origin.y,origin.z,1);
    }
    glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);
    glBindFramebuffer(GL_FRAMEBUFFER,target);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    bakedCamera=camera;lastBake=glfwGetTime();valid=true;++builds;
}
void MilkyWaySky::Render(const glm::mat4& vp,const glm::dvec3& camera) const {
    if(!displayProgram)return;
    const bool depth=glIsEnabled(GL_DEPTH_TEST)==GL_TRUE,blend=glIsEnabled(GL_BLEND)==GL_TRUE;
    GLboolean depthMask;glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
    GLint srcRgb,dstRgb,srcAlpha,dstAlpha;glGetIntegerv(GL_BLEND_SRC_RGB,&srcRgb);glGetIntegerv(GL_BLEND_DST_RGB,&dstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA,&srcAlpha);glGetIntegerv(GL_BLEND_DST_ALPHA,&dstAlpha);
    double threshold=.01*MilkyWayModel::auPerKpc;
    for(const auto& nebula:CelestialPhenomena::Nebulae())if(glm::length(camera-nebula.positionAu)<nebula.radiusLy*Astronomy::auPerLightYear*5)
        threshold=std::min(threshold,nebula.radiusLy*Astronomy::auPerLightYear*.015);
    if(!valid || (glm::length(camera-bakedCamera)>threshold && glfwGetTime()-lastBake>.25))Bake(camera);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);
    glUseProgram(displayProgram);const auto inverse=glm::inverse(vp);
    const auto orientation=glm::mat3(glm::transpose(MilkyWayModel::Orientation()));
    glUniformMatrix4fv(glGetUniformLocation(displayProgram,"inverseViewProjection"),1,GL_FALSE,glm::value_ptr(inverse));
    glUniformMatrix3fv(glGetUniformLocation(displayProgram,"worldToGalactic"),1,GL_FALSE,glm::value_ptr(orientation));
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);
    glUniform1i(glGetUniformLocation(displayProgram,"milkyWay"),0);
    const auto local=glm::vec3(MilkyWayModel::ToModelKpc(camera));
    // Match the cached split exactly until the next translation refresh.
    const auto cached=glm::vec3(MilkyWayModel::ToModelKpc(bakedCamera));
    glUniform3fv(glGetUniformLocation(displayProgram,"cameraKpc"),1,glm::value_ptr(local));
    glUniform1f(glGetUniformLocation(displayProgram,"exteriorWeight"),illustration?glm::smoothstep(.4f,1.5f,std::abs(cached.y)):0.f);
    glUniform1i(glGetUniformLocation(displayProgram,"hasIllustration"),illustration?1:0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,illustration);
    glUniform1i(glGetUniformLocation(displayProgram,"galacticIllustration"),1);
    glBindVertexArray(vao);glDrawArrays(GL_TRIANGLES,0,3);glBindVertexArray(0);
    glBlendFuncSeparate(srcRgb,dstRgb,srcAlpha,dstAlpha);glDepthMask(depthMask);
    glActiveTexture(GL_TEXTURE0);
    if(!blend)glDisable(GL_BLEND);if(depth)glEnable(GL_DEPTH_TEST);
}
void MilkyWaySky::Release(){
    if(bakeProgram)glDeleteProgram(bakeProgram);if(displayProgram)glDeleteProgram(displayProgram);
    if(vao)glDeleteVertexArrays(1,&vao);if(texture)glDeleteTextures(1,&texture);if(framebuffer)glDeleteFramebuffers(1,&framebuffer);
    if(illustration)glDeleteTextures(1,&illustration);illustration=0;
    bakeProgram=displayProgram=vao=texture=framebuffer=0;valid=false;builds=0;
}
