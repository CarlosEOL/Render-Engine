#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "PlanetTerrain.h"
#include "Astronomy.h"
#include "PlanetWeather.h"
#include <string>
#include "glad/glad.h"
#include "stb_image.h"
#include <iostream>
#include <memory>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <glm/gtc/constants.hpp>

namespace {
    constexpr float earthReliefMetres=.003f*6371008.4f;

    struct EarthMap {
        int width=0,height=0;
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels{nullptr,stbi_image_free};

        EarthMap(){
            const auto path=PlanetTerrain::EarthAssetPath("earth-relief.png");
            stbi_set_flip_vertically_on_load(true);
            int channels=0;
            pixels.reset(stbi_load(path.string().c_str(),&width,&height,&channels,4));
            if(!pixels)std::cerr<<"Could not load Earth relief from "<<path<<"\n";
        }

        glm::vec3 Sample(const glm::vec3& n) const {
            if(!pixels)return glm::vec3(0);
            const float horizontal=std::sqrt(n.x*n.x+n.z*n.z);
            const float longitude=horizontal>1e-6f?std::atan2(-n.z,n.x):0;
            const float x=(longitude/glm::two_pi<float>()+.5f)*width-.5f;
            const float y=std::atan2(horizontal,-n.y)/glm::pi<float>()*height-.5f;
            const int left=static_cast<int>(std::floor(x)),bottom=static_cast<int>(std::floor(y));
            const auto texel=[&](int column,int row){
                const auto offset=(static_cast<size_t>(glm::clamp(row,0,height-1))*width+(column+width)%width)*4;
                const auto* p=pixels.get()+offset;
                return glm::vec3(float(p[0]*256+p[1])-12000.f,p[2]/255.f,p[3]/255.f);
            };
            return glm::mix(glm::mix(texel(left,bottom),texel(left+1,bottom),x-left),
                            glm::mix(texel(left,bottom+1),texel(left+1,bottom+1),x-left),y-bottom);
        }
    };

    const EarthMap& Geography(){
        static const EarthMap map;
        return map;
    }
    float Hash(glm::vec3 cell, unsigned int seed) {
        std::uint32_t hash = seed ^ static_cast<std::uint32_t>(static_cast<int>(cell.x)) * 374761393u
            ^ static_cast<std::uint32_t>(static_cast<int>(cell.y)) * 668265263u
            ^ static_cast<std::uint32_t>(static_cast<int>(cell.z)) * 2246822519u;
        hash = (hash ^ (hash >> 13)) * 1274126177u;
        hash ^= hash >> 16;
        return static_cast<float>(hash & 0x00ffffffu) / 16777215.0f;
    }

    float Noise(const glm::vec3& position, unsigned int seed) {
        const auto cell = glm::floor(position);
        const auto f = glm::fract(position);
        const auto b = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
        const float lower = glm::mix(
            glm::mix(Hash(cell, seed), Hash(cell + glm::vec3(1, 0, 0), seed), b.x),
            glm::mix(Hash(cell + glm::vec3(0, 1, 0), seed), Hash(cell + glm::vec3(1, 1, 0), seed), b.x), b.y);
        const float upper = glm::mix(
            glm::mix(Hash(cell + glm::vec3(0, 0, 1), seed), Hash(cell + glm::vec3(1, 0, 1), seed), b.x),
            glm::mix(Hash(cell + glm::vec3(0, 1, 1), seed), Hash(cell + glm::vec3(1, 1, 1), seed), b.x), b.y);
        return 2.0f * glm::mix(lower, upper, b.z) - 1.0f;
    }

    float EllipseInterior(glm::vec2 point,glm::vec2 centre,glm::vec2 axes) {
        return (1-glm::length((point-centre)/axes))*std::min(axes.x,axes.y);
    }

    float AntarcticCoast(const glm::vec3& n,unsigned seed) {
        // Polar projection uses the planet's +X meridian and -Z east.
        const glm::vec2 p(n.x,-n.z);
        const float east=EllipseInterior(p,{.045f,.083f},{.337f,.329f});
        const float west=EllipseInterior(p,{.016f,-.151f},{.185f,.135f});
        const glm::vec2 root(.067f,-.205f),tip(.222f,-.395f),arm=tip-root;
        const float t=glm::clamp(glm::dot(p-root,arm)/glm::dot(arm,arm),0.f,1.f);
        const float peninsula=glm::mix(.052f,.015f,t)-glm::length(p-root-arm*t);
        const float ross=EllipseInterior(p,{-.302f,-.081f},{.145f,.117f});
        const float weddell=EllipseInterior(p,{.244f,-.222f},{.113f,.100f});
        const float shore=std::min(std::max({east,west,peninsula}),-std::max(ross,weddell));
        return shore+.012f*Noise(n*48.f,seed)+.005f*Noise(n*120.f,seed);
    }

    float EarthPolarHeight(const glm::vec3& n,unsigned seed,float height,float seaLevel) {
        if(n.y<-.80f){
            const float coast=AntarcticCoast(n,seed);
            const float plateau=glm::smoothstep(.006f,.18f,coast);
            const float relief=glm::clamp(coast*2.2f,-.18f,.025f)+plateau*(.13f+.045f*Noise(n*25.f,seed));
            return glm::mix(height,seaLevel+relief,glm::smoothstep(.80f,.865f,-n.y));
        }
        const float basin=glm::smoothstep(.925f,.975f,n.y+.034f*n.z);
        return glm::mix(height,seaLevel-.13f,basin);
    }
}

std::filesystem::path PlanetTerrain::EarthAssetPath(const char* filename){
    const auto relative=std::filesystem::path("assets/earth")/filename;
#ifdef _WIN32
    wchar_t executable[32768];
    const auto length=GetModuleFileNameW(nullptr,executable,32768);
    if(length>0 && length<32768){
        const auto bundled=std::filesystem::path(executable).parent_path()/relative;
        if(std::filesystem::is_regular_file(bundled))return bundled;
    }
#endif
    return relative;
}

bool PlanetTerrain::HasEarthMap(){return bool(Geography().pixels);}

glm::vec3 PlanetTerrain::EarthGeography(const glm::vec3& direction){return Geography().Sample(direction);}

unsigned PlanetTerrain::LoadEarthReliefTexture(){
    const auto& map=Geography();
    if(!map.pixels)return 0;
    unsigned texture=0;
    glGenTextures(1,&texture);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,map.width,map.height,0,GL_RGBA,GL_UNSIGNED_BYTE,map.pixels.get());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    return texture;
}

float PlanetTerrain::ArcticWinter(double seconds) {
    const double year=365.2422*Astronomy::secondsPerDay;
    return static_cast<float>(.5+.5*std::cos(glm::two_pi<double>()*std::remainder(seconds/year-.20,1.0)));
}

float PlanetTerrain::WaterCoverage(const glm::vec3& n,unsigned seed,float height,float seaLevel,bool earth,float winter) {
    const auto map=earth && HasEarthMap()?EarthGeography(n):glm::vec3(0);
    const float water=earth && HasEarthMap()?glm::smoothstep(.35f,.65f,std::max(map.y,map.z)):
        1-glm::smoothstep(seaLevel-.008f,seaLevel+.008f,height);
    if(!earth || n.y<=.90f)return water;
    const float edge=glm::mix(.977f,.944f,winter)+.026f*n.x-.016f*n.z+.007f*Noise(n*58.f,seed);
    return water*(1-glm::smoothstep(edge-.010f,edge+.009f,n.y));
}

std::vector<glm::vec3> PlanetTerrain::SpherePatch(const glm::vec3& direction,int segments) {
    const auto reference=std::abs(direction.y)<.9f?glm::vec3(0,1,0):glm::vec3(1,0,0);
    const auto right=glm::normalize(glm::cross(reference,direction)),up=glm::cross(direction,right);
    const auto point=[&](int x,int y) {
        return glm::normalize(direction+right*(.025f*(2.0f*x/segments-1))+up*(.025f*(2.0f*y/segments-1)));
    };
    std::vector<glm::vec3> vertices;vertices.reserve(segments*segments*6);
    for(int y=0;y<segments;++y)for(int x=0;x<segments;++x)
        vertices.insert(vertices.end(),{point(x,y),point(x+1,y),point(x+1,y+1),point(x,y),point(x+1,y+1),point(x,y+1)});
    return vertices;
}

float PlanetTerrain::SurfaceElevation(const glm::vec3& direction,unsigned int seed,float wavePhase,
    float seaLevel,float relief,float waves,bool oceanOnly,const PlanetWeather::State* weather,bool earth,float arcticWinter) {
    const float wave = (std::sin(35.0f * direction.y + 4.0f * std::sin(10.0f * direction.x) + wavePhase)
        + 0.35f * std::sin(80.0f * direction.y + 8.0f * direction.x - wavePhase)) / 1.35f;
    const float waveElevation=weather && weather->parameters.w>0
        ?static_cast<float>(PlanetWeather::WaveHeight(*weather,glm::dvec3(direction)*static_cast<double>(weather->parameters.w),direction)/weather->parameters.w):waves*wave;
    if(oceanOnly)return waveElevation;
    if(earth && HasEarthMap()){
        const auto map=EarthGeography(direction);
        const float height=std::max(0.f,map.x)*(1-map.y)/earthReliefMetres;
        return relief*height+WaterCoverage(direction,seed,height,seaLevel,true,arcticWinter)*waveElevation;
    }
    auto position = direction * 2.6f;
    float continents = 0.0f;
    float amplitude = 0.5f;
    for (int octave = 0; octave < 5; ++octave) {
        continents += amplitude * Noise(position, seed);
        position = position * 2.03f + glm::vec3(7.1f, 3.7f, 1.9f);
        amplitude *= 0.5f;
    }
    continents /= 0.96875f;
    const float ridges = 1.0f - std::abs(Noise(direction * 11.0f + glm::vec3(13.0f), seed));
    float height = 0.75f * continents + 0.22f * (ridges * ridges - 0.5f)
        * glm::smoothstep(-0.1f, 0.25f, continents);
    if(earth)height=EarthPolarHeight(direction,seed,height,seaLevel);
    const float water=WaterCoverage(direction,seed,height,seaLevel,earth,arcticWinter);
    return relief * glm::max(height, seaLevel) + water * waveElevation;
}

float PlanetTerrain::SurfaceRadius(const glm::vec3& direction,unsigned int seed,float phase,
    float seaLevel,float relief,float waves) {
    return 1+SurfaceElevation(direction,seed,phase,seaLevel,relief,waves);
}

std::vector<glm::vec3> PlanetTerrain::SurfaceLodVertices(double radiusMetres) {
    if(!std::isfinite(radiusMetres) || radiusMetres<1000)throw std::invalid_argument("Invalid terrain radius");
    struct Ring{double radius;int segments;};
    std::vector<Ring> rings={{0,1}};
    for(int i=1;i<=40;++i)rings.push_back({i*2.5,256});
    const double outer=radiusMetres*std::tan(.020)*1.03;
    double start=100;
    int level=0;
    while(start<outer){
        const double finish=std::min(outer,start*2);
        const int segments=std::max(16,128>>std::min(level,3));
        for(int i=1;i<=8;++i)rings.push_back({start+(finish-start)*i/8,segments});
        start=finish;++level;
    }
    std::vector<glm::vec3> vertices;vertices.reserve(100000);
    const auto point=[](const Ring& ring,int i){
        const double angle=glm::two_pi<double>()*static_cast<double>(i%ring.segments)/ring.segments;
        return glm::vec3(ring.radius*std::cos(angle),ring.radius*std::sin(angle),0);
    };
    const auto triangle=[&](const glm::vec3& a,const glm::vec3& b,const glm::vec3& c){vertices.insert(vertices.end(),{a,b,c});};
    for(size_t ring=1;ring<rings.size();++ring){
        const auto& a=rings[ring-1];const auto& b=rings[ring];
        if(a.radius==0){for(int j=0;j<b.segments;++j)triangle(glm::vec3(0),point(b,j),point(b,j+1));continue;}
        int i=0,j=0;
        while(i<a.segments || j<b.segments){
            const int nextA=(i+1)*b.segments,nextB=(j+1)*a.segments;
            if(nextA==nextB){
                triangle(point(a,i),point(b,j),point(a,i+1));
                triangle(point(a,i+1),point(b,j),point(b,j+1));++i;++j;
            }else if(nextA<nextB){triangle(point(a,i),point(b,j),point(a,i+1));++i;}
            else{triangle(point(a,i),point(b,j),point(b,j+1));++j;}
        }
    }
    return vertices;
}

void PlanetTerrain::ConfigureSurfacePatch(unsigned int program,const glm::dvec3& direction,
    const glm::dmat3& rotation,const glm::dvec3& centerRelativeAu,double radiusMetres,float elevation) {
    const auto anchor=glm::normalize(direction);
    const auto reference=std::abs(anchor.y)<.9?glm::dvec3(0,1,0):glm::dvec3(1,0,0);
    const auto right=glm::normalize(glm::cross(reference,anchor)),up=glm::cross(anchor,right);
    const auto frame=glm::mat3(rotation*glm::dmat3(right,up,anchor));
    const auto origin=glm::vec3(centerRelativeAu+rotation*anchor*(radiusMetres*(1.0+elevation)/Astronomy::metresPerAu));
    const auto textureOrigin=glm::vec3(glm::mod(anchor*(radiusMetres*(1.0+elevation)),glm::dvec3(4096)));
    const auto v3=[&](const char* name,const glm::vec3& value){glUniform3fv(glGetUniformLocation(program,name),1,glm::value_ptr(value));};
    v3("patchCenter",glm::vec3(anchor));v3("patchRight",glm::vec3(right));v3("patchUp",glm::vec3(up));
    v3("patchRelativeOrigin",origin);v3("patchTextureOrigin",textureOrigin);
    glUniformMatrix3fv(glGetUniformLocation(program,"patchWorldFrame"),1,GL_FALSE,glm::value_ptr(frame));
    glUniform1f(glGetUniformLocation(program,"patchRadiusMetres"),static_cast<float>(radiusMetres));
    glUniform1f(glGetUniformLocation(program,"patchAnchorElevation"),elevation);
}

std::vector<glm::vec3> PlanetTerrain::SphereVertices(int latitudeSegments, int longitudeSegments) {
    const auto point = [](float latitude, float longitude) {
        const float ring = std::cos(latitude);
        return glm::vec3(ring * std::cos(longitude), ring * std::sin(longitude), std::sin(latitude));
    };
    std::vector<glm::vec3> vertices;
    vertices.reserve(latitudeSegments * longitudeSegments * 6);
    for (int row = 0; row < latitudeSegments; ++row) {
        const float lower = -glm::half_pi<float>() + glm::pi<float>() * row / latitudeSegments;
        const float upper = lower + glm::pi<float>() / latitudeSegments;
        for (int col = 0; col < longitudeSegments; ++col) {
            const float left = glm::two_pi<float>() * col / longitudeSegments;
            const float right = glm::two_pi<float>() * (col + 1) / longitudeSegments;
            const auto a = point(lower, left), b = point(lower, right);
            const auto c = point(upper, right), d = point(upper, left);
            vertices.insert(vertices.end(), {a,b,c,a,c,d});
        }
    }
    return vertices;
}

const char* PlanetTerrain::ShaderFunctions() {
    static const std::string source=std::string(PlanetWeather::ShaderFunctions())+R"(
uniform float terrainSeed;
uniform float seaLevel;
uniform bool earthPoles;
uniform bool earthMapped;
uniform sampler2D earthReliefMap;
uniform float arcticWinter;
uniform float terrainRelief;
uniform float waterWaveHeight;
uniform bool patchDrawing;
uniform vec3 patchCenter,patchRight,patchUp,patchRelativeOrigin,patchTextureOrigin;
uniform mat3 patchWorldFrame;
uniform float patchRadiusMetres,patchAnchorElevation;

vec3 surfacePatchDirection(vec2 metres){
    return normalize(patchCenter+(patchRight*metres.x+patchUp*metres.y)/patchRadiusMetres);
}
vec3 surfacePatchDelta(vec2 metres,float elevation){
    float r=patchRadiusMetres;
    float squared=dot(metres,metres);
    float scale=sqrt(1.0+squared/(r*r));
    vec2 tangent=metres*(1.0+elevation)/scale;
    float radial=-squared*(1.0+elevation)/(r*scale*(scale+1.0))+(elevation-patchAnchorElevation)*r;
    return patchRight*tangent.x+patchUp*tangent.y+patchCenter*radial;
}
vec3 surfacePatchRelative(vec3 delta){
    return patchRelativeOrigin+patchWorldFrame*vec3(dot(delta,patchRight),dot(delta,patchUp),dot(delta,patchCenter))/149597870700.0;
}

float terrainHash(vec3 cell) {
    uvec3 coordinate = uvec3(ivec3(cell));
    uint hash = uint(terrainSeed) ^ coordinate.x * 374761393u
        ^ coordinate.y * 668265263u ^ coordinate.z * 2246822519u;
    hash = (hash ^ (hash >> 13)) * 1274126177u;
    hash ^= hash >> 16;
    return float(hash & 0x00ffffffu) / 16777215.0;
}

float terrainNoise(vec3 position) {
    vec3 cell = floor(position);
    vec3 fraction = fract(position);
    vec3 blend = fraction * fraction * fraction * (fraction * (fraction * 6.0 - 15.0) + 10.0);
    float lower = mix(
        mix(terrainHash(cell), terrainHash(cell + vec3(1, 0, 0)), blend.x),
        mix(terrainHash(cell + vec3(0, 1, 0)), terrainHash(cell + vec3(1, 1, 0)), blend.x), blend.y);
    float upper = mix(
        mix(terrainHash(cell + vec3(0, 0, 1)), terrainHash(cell + vec3(1, 0, 1)), blend.x),
        mix(terrainHash(cell + vec3(0, 1, 1)), terrainHash(cell + vec3(1, 1, 1)), blend.x), blend.y);
    return 2.0 * mix(lower, upper, blend.z) - 1.0;
}

float terrainFbm(vec3 position) {
    float height = 0.0;
    float amplitude = 0.5;
    for (int octave = 0; octave < 5; ++octave) {
        height += amplitude * terrainNoise(position);
        position = position * 2.03 + vec3(7.1, 3.7, 1.9);
        amplitude *= 0.5;
    }
    return height / 0.96875;
}

float ellipseInterior(vec2 point,vec2 centre,vec2 axes){
    return (1.0-length((point-centre)/axes))*min(axes.x,axes.y);
}

float antarcticCoast(vec3 n){
    vec2 p=vec2(n.x,-n.z);
    float east=ellipseInterior(p,vec2(.045,.083),vec2(.337,.329));
    float west=ellipseInterior(p,vec2(.016,-.151),vec2(.185,.135));
    vec2 root=vec2(.067,-.205),arm=vec2(.222,-.395)-root;
    float t=clamp(dot(p-root,arm)/dot(arm,arm),0.0,1.0);
    float peninsula=mix(.052,.015,t)-length(p-root-arm*t);
    float ross=ellipseInterior(p,vec2(-.302,-.081),vec2(.145,.117));
    float weddell=ellipseInterior(p,vec2(.244,-.222),vec2(.113,.100));
    float shore=min(max(max(east,west),peninsula),-max(ross,weddell));
    return shore+.012*terrainNoise(n*48.0)+.005*terrainNoise(n*120.0);
}

float earthMapAtan2(float y,float x){
    // Use the single-argument arctangent with explicit quadrant correction.
    // This keeps the atlas coordinates stable at the poles and antimeridian.
    float angle=atan(abs(y)/max(abs(x),1e-20));
    angle=mix(angle,3.14159265359-angle,float(x<0.0));
    return mix(angle,-angle,float(y<0.0));
}

vec2 earthMapUv(vec3 n){
    float horizontal=length(n.xz);
    float longitude=horizontal>1e-6?earthMapAtan2(-n.z,n.x):0.0;
    return vec2(longitude/6.28318530718+.5,earthMapAtan2(horizontal,-n.y)/3.14159265359);
}

vec4 earthGeography(vec3 n){
    ivec2 size=textureSize(earthReliefMap,0);
    vec2 coordinate=earthMapUv(n)*vec2(size)-.5;
    ivec2 lower=ivec2(floor(coordinate)),upper=lower+1;
    lower.x=(lower.x+size.x)%size.x;
    upper.x=(upper.x+size.x)%size.x;
    lower.y=clamp(lower.y,0,size.y-1);
    upper.y=clamp(upper.y,0,size.y-1);
    vec2 f=fract(coordinate);
    // Interpolate in full precision. Filtering the packed bytes in UNORM can
    // round the high byte before height decoding and shift the ground.
    return mix(mix(texelFetch(earthReliefMap,lower,0),texelFetch(earthReliefMap,ivec2(upper.x,lower.y),0),f.x),
               mix(texelFetch(earthReliefMap,ivec2(lower.x,upper.y),0),texelFetch(earthReliefMap,upper,0),f.x),f.y);
}

float terrainHeight(vec3 direction) {
    if(earthMapped){
        vec4 earth=earthGeography(direction);
        float height=dot(earth.rg,vec2(65280.0,255.0))-12000.0;
        return max(0.0,height)*(1.0-earth.b)/(.003*6371008.4);
    }
    float continents = terrainFbm(direction * 2.6);
    float ridges = 1.0 - abs(terrainNoise(direction * 11.0 + vec3(13.0)));
    float height=0.75 * continents + 0.22 * (ridges * ridges - 0.5)
        * smoothstep(-0.1, 0.25, continents);
    if(!earthPoles)return height;
    if(direction.y<-.80){
        float coast=antarcticCoast(direction);
        float plateau=smoothstep(.006,.18,coast);
        float relief=clamp(coast*2.2,-.18,.025)+plateau*(.13+.045*terrainNoise(direction*25.0));
        return mix(height,seaLevel+relief,smoothstep(.80,.865,-direction.y));
    }
    return mix(height,seaLevel-.13,smoothstep(.925,.975,direction.y+.034*direction.z));
}

float surfaceWaterCoverage(vec3 n,float height){
    if(earthMapped){
        vec2 water=earthGeography(n).ba;
        return smoothstep(.35,.65,max(water.x,water.y));
    }
    return 1.0-smoothstep(seaLevel-.008,seaLevel+.008,height);
}

float liquidCoverage(vec3 n,float height){
    float water=surfaceWaterCoverage(n,height);
    if(!earthPoles || n.y<=.90)return water;
    float edge=mix(.977,.944,arcticWinter)+.026*n.x-.016*n.z+.007*terrainNoise(n*58.0);
    return water*(1.0-smoothstep(edge-.010,edge+.009,n.y));
}

float oceanWave(vec3 direction, float phase) {
    if(weatherParameters.w>0.0)return weatherWaveHeight(direction*weatherParameters.w,direction)/max(waterWaveHeight*weatherParameters.w,1e-9);
    return (sin(35.0 * direction.y + 4.0 * sin(10.0 * direction.x) + phase)
        + 0.35 * sin(80.0 * direction.y + 8.0 * direction.x - phase)) / 1.35;
}

vec3 planetSurface(vec3 direction, float height, float phase) {
    float water=liquidCoverage(direction,height);
    return direction*(1.0+terrainRelief*max(height,seaLevel)+water*waterWaveHeight*oceanWave(direction,phase));
}

float surfaceElevation(vec3 direction,float height,float phase,bool oceanOnly){
    float water=oceanOnly?1.0:liquidCoverage(direction,height);
    return (oceanOnly?0.0:terrainRelief*max(height,seaLevel))+water*waterWaveHeight*oceanWave(direction,phase);
}
)";
    return source.c_str();
}
const char* PlanetTerrain::SurfaceDetailShaderFunctions(){
    static const std::string source=std::string(PlanetWeather::SurfaceShaderFunctions())+R"(
float groundNoise(vec3 p){
    vec3 cell=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
    float a=mix(mix(terrainHash(mod(cell,256.0)),terrainHash(mod(cell+vec3(1,0,0),256.0)),f.x),
                mix(terrainHash(mod(cell+vec3(0,1,0),256.0)),terrainHash(mod(cell+vec3(1,1,0),256.0)),f.x),f.y);
    float b=mix(mix(terrainHash(mod(cell+vec3(0,0,1),256.0)),terrainHash(mod(cell+vec3(1,0,1),256.0)),f.x),
                mix(terrainHash(mod(cell+vec3(0,1,1),256.0)),terrainHash(mod(cell+vec3(1,1,1),256.0)),f.x),f.y);
    return mix(a,b,f.z);
}
void groundDetail(inout vec3 material,inout vec3 normal,vec3 delta,float water,float cameraDistance,float phase){
    if(!patchDrawing || cameraDistance>=100.0)return;
    float fade=1.0-smoothstep(80.0,100.0,cameraDistance);
    vec3 p=patchTextureOrigin+delta;
    float footprint=max(length(dFdx(p)),length(dFdy(p)));
    float fine=1.0-smoothstep(.35,.8,footprint*16.0);
    float grain=groundNoise(p*16.0),pebbles=groundNoise(p*2.0),strata=groundNoise(p*.25);
    float height=(pebbles*.035+strata*.10+grain*.006*fine)*(1.0-water);
    if(water>.5 && weatherParameters.w>0.0){
        normal=normalize(normal-weatherWaveGradient(p,normal,true)*fade);
        return;
    }
    if(water>.5)height=.025*(sin(dot(p,vec3(.7,.1,.5))*2.0+phase)+.4*sin(dot(p,vec3(-.3,.2,.8))*5.0-phase));
    vec3 dx=dFdx(delta),dy=dFdy(delta);
    vec3 r1=cross(dy,normal),r2=cross(normal,dx);
    float determinant=dot(dx,r1);
    if(abs(determinant)>1e-12){
        vec3 gradient=sign(determinant)*(dFdx(height)*r1+dFdy(height)*r2);
        normal=normalize(abs(determinant)*normal-gradient*fade);
    }
    float variation=.70+.32*pebbles+.18*strata+.08*(grain-.5)*fine;
    material*=mix(1.0,variation,fade*(1.0-water));
}
)";
    return source.c_str();
}
