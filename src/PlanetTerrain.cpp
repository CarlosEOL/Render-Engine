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
#include <array>
#include <algorithm>
#include <fstream>
#include <optional>
#include <glm/gtc/constants.hpp>

namespace {
    constexpr float earthReliefMetres=.003f*6371008.4f;
    constexpr int elevationTileSize=900,elevationWidth=21600,elevationHeight=10800;

    glm::vec2 EarthCoordinates(const glm::vec3& n,int width,int height){
        const float horizontal=std::sqrt(n.x*n.x+n.z*n.z);
        const float longitude=horizontal>1e-6f?std::atan2(-n.z,n.x):0;
        return glm::vec2((longitude/glm::two_pi<float>()+.5f)*width,
            std::atan2(horizontal,-n.y)/glm::pi<float>()*height)-.5f;
    }

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
            const auto coordinate=EarthCoordinates(n,width,height);
            const float x=coordinate.x,y=coordinate.y;
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

    struct ElevationTile {
        int key=-1;
        unsigned age=0;
        std::unique_ptr<stbi_us,decltype(&stbi_image_free)> pixels{nullptr,stbi_image_free};

        void Load(int column,int band){
            key=band*24+column;
            pixels.reset();
            const std::string file=std::string("elevation-")+(band<10?"0":"")+std::to_string(band)+".bin";
            std::ifstream data(PlanetTerrain::EarthAssetPath(file.c_str()),std::ios::binary|std::ios::ate);
            if(!data)return;
            const auto end=static_cast<std::streamoff>(data.tellg());
            data.seekg(column*4);
            std::array<unsigned char,8> index{};
            if(!data.read(reinterpret_cast<char*>(index.data()),index.size()))return;
            const auto offset=[&](int i){
                return static_cast<std::uint32_t>(index[i])|(static_cast<std::uint32_t>(index[i+1])<<8)
                    |(static_cast<std::uint32_t>(index[i+2])<<16)|(static_cast<std::uint32_t>(index[i+3])<<24);
            };
            const std::uint32_t start=offset(0),finish=offset(4);
            if(start<100 || finish<=start || finish>end || finish-start>4*elevationTileSize*elevationTileSize)return;
            std::vector<unsigned char> png(finish-start);
            data.seekg(start);
            if(!data.read(reinterpret_cast<char*>(png.data()),png.size()))return;
            int width=0,height=0,channels=0;
            stbi_set_flip_vertically_on_load(true);
            pixels.reset(stbi_load_16_from_memory(png.data(),static_cast<int>(png.size()),&width,&height,&channels,1));
            if(width!=elevationTileSize || height!=elevationTileSize || channels!=1)pixels.reset();
        }
    };

    std::optional<int> NativeElevation(int x,int y){
        x=(x+elevationWidth)%elevationWidth;
        y=glm::clamp(y,0,elevationHeight-1);
        const int column=x/elevationTileSize,band=y/elevationTileSize,key=band*24+column;
        static thread_local std::array<ElevationTile,8> tiles;
        static thread_local unsigned age=0;
        auto tile=std::find_if(tiles.begin(),tiles.end(),[&](const auto& entry){return entry.key==key;});
        if(tile==tiles.end()){
            tile=std::min_element(tiles.begin(),tiles.end(),[](const auto& a,const auto& b){return a.age<b.age;});
            tile->Load(column,band);
        }
        tile->age=++age;
        if(!tile->pixels)return std::nullopt;
        return static_cast<int>(tile->pixels.get()[(y%elevationTileSize)*elevationTileSize+x%elevationTileSize])-12000;
    }

    std::optional<float> DetailedEarthElevation(const glm::vec3& direction){
        const auto position=EarthCoordinates(direction,elevationWidth,elevationHeight);
        const glm::ivec2 cell(glm::floor(position));
        const glm::vec2 f=position-glm::vec2(cell);
        const auto a=NativeElevation(cell.x,cell.y),b=NativeElevation(cell.x+1,cell.y);
        const auto c=NativeElevation(cell.x,cell.y+1),d=NativeElevation(cell.x+1,cell.y+1);
        if(!a || !b || !c || !d)return std::nullopt;
        return glm::mix(glm::mix(float(*a),float(*b),f.x),glm::mix(float(*c),float(*d),f.x),f.y);
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

    float TerrainHeight(const glm::vec3& direction,unsigned seed,float seaLevel,bool earth,bool layered) {
        auto position=direction*2.6f;
        float continents=0,amplitude=.5f;
        for(int octave=0;octave<5;++octave){
            continents+=amplitude*Noise(position,seed);
            position=position*2.03f+glm::vec3(7.1f,3.7f,1.9f);
            amplitude*=.5f;
        }
        continents/=.96875f;
        const float ridges=1-std::abs(Noise(direction*11.f+glm::vec3(13.f),seed));
        float height=.75f*continents+.22f*(ridges*ridges-.5f)*glm::smoothstep(-.1f,.25f,continents);
        if(layered){
            const auto n=direction;
            const glm::vec3 warp(Noise(n*4.4f+glm::vec3(13,35,9),seed),
                Noise(n*4.4f+glm::vec3(51,7,29),seed),Noise(n*4.4f+glm::vec3(27,18,61),seed));
            const float ranges=glm::smoothstep(-.13f,.38f,continents)*
                glm::smoothstep(-.36f,.38f,Noise(n*6.3f+glm::vec3(29,12,53),seed));
            auto ridgePoint=n*13.f+warp*1.85f+glm::vec3(7,43,18);
            float ridgeStack=0,weight=.61f;
            for(int octave=0;octave<4;++octave){
                const float crest=1-std::abs(Noise(ridgePoint,seed));
                ridgeStack+=weight*crest*crest*crest;
                ridgePoint=ridgePoint*2.07f+glm::vec3(21,3,16);
                weight*=.43f;
            }
            const float hills=.68f*Noise(n*27.f+warp*1.2f+glm::vec3(18,11,6),seed)
                +.23f*Noise(n*58.f+glm::vec3(8,17,41),seed)+.09f*Noise(n*124.f+glm::vec3(31,4,17),seed);
            const float localHills=.017f*Noise(n*320.f+glm::vec3(3,81,42),seed)
                +.0055f*Noise(n*960.f+glm::vec3(14,31,7),seed);
            height=.75f*continents+.44f*ranges*(ridgeStack-.23f)+(.047f+.055f*ranges)*hills+localHills;
        }
        return earth?EarthPolarHeight(direction,seed,height,seaLevel):height;
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

glm::vec3 PlanetTerrain::EarthGeography(const glm::vec3& direction,bool detailed){
    auto geography=Geography().Sample(direction);
    if(detailed && geography.z<.5f)geography.x=DetailedEarthElevation(direction).value_or(geography.x);
    return geography;
}

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

void PlanetTerrain::BindEarthReliefDetail(unsigned program,const glm::vec3& direction,unsigned& texture,glm::ivec2& origin){
    constexpr int window=384, stride=128;
    const glm::vec2 coords=EarthCoordinates(direction,elevationWidth,elevationHeight);
    glm::ivec2 next(glm::floor(coords/static_cast<float>(stride))*static_cast<float>(stride)-static_cast<float>(stride));
    next.y=glm::clamp(next.y,0,elevationHeight-window);
    glActiveTexture(GL_TEXTURE6);
    if(!texture || next!=origin){
        if(!texture)glGenTextures(1,&texture);
        origin=next;
        std::vector<unsigned char> rgba(window*window*4,0);
        for(int y=0;y<window;++y)for(int x=0;x<window;++x){
            const auto elevation=NativeElevation(origin.x+x,origin.y+y);
            if(!elevation)continue;
            const int encoded=*elevation+12000;
            const size_t pixel=(static_cast<size_t>(y)*window+x)*4;
            rgba[pixel]=static_cast<unsigned char>(encoded>>8);
            rgba[pixel+1]=static_cast<unsigned char>(encoded&255);
            rgba[pixel+3]=255;
        }
        glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,window,window,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }else glBindTexture(GL_TEXTURE_2D,texture);
    glUniform1i(glGetUniformLocation(program,"earthDetailMap"),6);
    glUniform2f(glGetUniformLocation(program,"earthDetailOrigin"),static_cast<float>(origin.x),static_cast<float>(origin.y));
    glUniform1i(glGetUniformLocation(program,"earthDetailed"),true);
    glActiveTexture(GL_TEXTURE0);
}

float PlanetTerrain::ArcticWinter(double seconds) {
    const double year=365.2422*Astronomy::secondsPerDay;
    return static_cast<float>(.5+.5*std::cos(glm::two_pi<double>()*std::remainder(seconds/year-.20,1.0)));
}

float PlanetTerrain::WaterCoverage(const glm::vec3& n,unsigned seed,float height,float seaLevel,bool earth,float winter) {
    const auto map=earth && HasEarthMap()?Geography().Sample(n):glm::vec3(0);
    const float water=earth && HasEarthMap()?glm::smoothstep(.35f,.65f,std::max(map.y,map.z)):
        1-glm::smoothstep(seaLevel-.008f,seaLevel+.008f,height);
    if(!earth || n.y<=.90f)return water;
    const float edge=glm::mix(.977f,.944f,winter)+.026f*n.x-.016f*n.z+.007f*Noise(n*58.f,seed);
    return water*(1-glm::smoothstep(edge-.010f,edge+.009f,n.y));
}

float PlanetTerrain::WaterDepthMetres(const glm::vec3& direction,unsigned seed,float seaLevel,double radiusMetres,
    bool earth,float winter,bool layeredTerrain) {
    if(earth && HasEarthMap()){
        const auto map=EarthGeography(direction);
        if(WaterCoverage(direction,seed,0,seaLevel,true,winter)<=.5f)return 0;
        if(map.z>.5f)return glm::mix(.5f,18.f,glm::smoothstep(.5f,1.f,map.z));
        return std::max(.25f,-map.x);
    }
    if(seaLevel<=-1)return 0;
    const float height=TerrainHeight(direction,seed,seaLevel,earth,layeredTerrain);
    if(height>=seaLevel || WaterCoverage(direction,seed,height,seaLevel,earth,winter)<=.5f)return 0;
    return static_cast<float>((seaLevel-height)*.003f*radiusMetres);
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
    float seaLevel,float relief,float waves,bool oceanOnly,const PlanetWeather::State* weather,bool earth,float arcticWinter,bool layeredTerrain) {
    const float wave = (std::sin(35.0f * direction.y + 4.0f * std::sin(10.0f * direction.x) + wavePhase)
        + 0.35f * std::sin(80.0f * direction.y + 8.0f * direction.x - wavePhase)) / 1.35f;
    const float waveElevation=weather && weather->parameters.w>0
        ?static_cast<float>(PlanetWeather::WaveHeight(*weather,glm::dvec3(direction)*static_cast<double>(weather->parameters.w),direction)/weather->parameters.w):waves*wave;
    if(oceanOnly)return waveElevation;
    if(earth && HasEarthMap()){
        const auto map=EarthGeography(direction);
        const float height=map.x*(1-glm::smoothstep(.05f,.5f,map.y))/earthReliefMetres;
        return relief*height+WaterCoverage(direction,seed,height,seaLevel,true,arcticWinter)*waveElevation;
    }
    const float height=TerrainHeight(direction,seed,seaLevel,earth,layeredTerrain);
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
        const int segments=std::max(96,128>>std::min(level,1));
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
uniform sampler2D earthDetailMap;
uniform bool earthDetailed;
uniform vec2 earthDetailOrigin;
uniform bool layeredTerrain;
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
    vec2 firstQuadrant=abs(vec2(x,y));
    float angle=atan(firstQuadrant.y/max(firstQuadrant.x,1e-20));
    // Correct the backend's initial angle with the sine of its residual.
    // Even a small error can move a sample several metres on steep terrain.
    float inverseLength=inversesqrt(max(dot(firstQuadrant,firstQuadrant),1e-20));
    angle+=(firstQuadrant.y*cos(angle)-firstQuadrant.x*sin(angle))*inverseLength;
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

float earthElevationMetres(vec4 geography){
    return dot(geography.rg,vec2(65280.0,255.0))-12000.0;
}

float earthMappedElevation(vec3 n,vec4 geography){
    float coarse=earthElevationMetres(geography);
    if(!earthDetailed || geography.a>=.5)return coarse;
    vec2 coordinate=earthMapUv(n)*vec2(21600.0,10800.0)-.5;
    coordinate.y=clamp(coordinate.y,0.0,10799.0);
    vec2 p=coordinate-earthDetailOrigin;
    p.x=mod(p.x,21600.0);
    vec2 limit=vec2(textureSize(earthDetailMap,0))-1.0;
    if(any(lessThan(p,vec2(0))) || any(greaterThan(p,limit)))return coarse;
    ivec2 cell=ivec2(p);
    ivec2 next=min(cell+1,ivec2(limit));
    vec4 a=texelFetch(earthDetailMap,cell,0),b=texelFetch(earthDetailMap,ivec2(next.x,cell.y),0);
    vec4 c=texelFetch(earthDetailMap,ivec2(cell.x,next.y),0),d=texelFetch(earthDetailMap,next,0);
    if(min(min(a.a,b.a),min(c.a,d.a))<.5)return coarse;
    vec2 f=fract(p);
    float detail=mix(mix(earthElevationMetres(a),earthElevationMetres(b),f.x),
        mix(earthElevationMetres(c),earthElevationMetres(d),f.x),f.y);
    vec2 edge=min(p,limit-p);
    // Fade the streamed window into the orbital surface. At the poles, retain
    // the last latitude row instead of introducing an artificial map border.
    if((earthDetailOrigin.y==0.0 && p.y<24.0) ||
       (earthDetailOrigin.y+limit.y>=10799.0 && limit.y-p.y<24.0))edge.y=24.0;
    return mix(coarse,detail,smoothstep(0.0,24.0,min(edge.x,edge.y)));
}

float liquidDepthMetres(vec3 direction,float height,float radiusMetres){
    if(earthMapped){
        vec4 geography=earthGeography(direction);
        // The atlas stores inland water at its surface level.
        if(geography.a>.5)return mix(.5,18.0,smoothstep(.5,1.0,geography.a));
        return max(.25,-earthMappedElevation(direction,geography));
    }
    return max(.25,(seaLevel-height)*terrainRelief*radiusMetres);
}

float terrainHeight(vec3 direction) {
    if(earthMapped){
        vec4 earth=earthGeography(direction);
        return earthMappedElevation(direction,earth)*(1.0-smoothstep(.05,.5,earth.b))/(.003*6371008.4);
    }
    float continents = terrainFbm(direction * 2.6);
    float ridges = 1.0 - abs(terrainNoise(direction * 11.0 + vec3(13.0)));
    float height=0.75 * continents + 0.22 * (ridges * ridges - 0.5)
        * smoothstep(-0.1, 0.25, continents);
    if(layeredTerrain){
        vec3 n=direction;
        vec3 warp=vec3(terrainNoise(n*4.4+vec3(13,35,9)),terrainNoise(n*4.4+vec3(51,7,29)),
            terrainNoise(n*4.4+vec3(27,18,61)));
        float ranges=smoothstep(-.13,.38,continents)*smoothstep(-.36,.38,terrainNoise(n*6.3+vec3(29,12,53)));
        vec3 ridgePoint=n*13.0+warp*1.85+vec3(7,43,18);
        float ridgeStack=0.0,weight=.61;
        for(int octave=0;octave<4;++octave){
            float crest=1.0-abs(terrainNoise(ridgePoint));
            ridgeStack+=weight*crest*crest*crest;
            ridgePoint=ridgePoint*2.07+vec3(21,3,16);
            weight*=.43;
        }
        float hills=.68*terrainNoise(n*27.0+warp*1.2+vec3(18,11,6))
            +.23*terrainNoise(n*58.0+vec3(8,17,41))+.09*terrainNoise(n*124.0+vec3(31,4,17));
        float localHills=.017*terrainNoise(n*320.0+vec3(3,81,42))+.0055*terrainNoise(n*960.0+vec3(14,31,7));
        height=.75*continents+.44*ranges*(ridgeStack-.23)+(.047+.055*ranges)*hills+localHills;
    }
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
    return direction*(1.0+terrainRelief*(earthMapped?height:max(height,seaLevel))+water*waterWaveHeight*oceanWave(direction,phase));
}

float surfaceElevation(vec3 direction,float height,float phase,bool oceanOnly){
    float water=oceanOnly?1.0:liquidCoverage(direction,height);
    return (oceanOnly?0.0:terrainRelief*(earthMapped?height:max(height,seaLevel)))+water*waterWaveHeight*oceanWave(direction,phase);
}

float seabedElevation(vec3 direction,float height,float radiusMetres){
    float elevation=terrainRelief*(earthMapped?height:max(height,seaLevel));
    if(liquidCoverage(direction,height)>.5)
        elevation-=liquidDepthMetres(direction,height,radiusMetres)/radiusMetres;
    return elevation;
}
)";
    return source.c_str();
}
const char* PlanetTerrain::SurfaceDetailShaderFunctions(){
    static const std::string source=std::string(PlanetWeather::SurfaceShaderFunctions())+R"(
uniform float groundClearanceMetres;
float groundNoise(vec3 p){
    vec3 cell=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
    float a=mix(mix(terrainHash(mod(cell,256.0)),terrainHash(mod(cell+vec3(1,0,0),256.0)),f.x),
                mix(terrainHash(mod(cell+vec3(0,1,0),256.0)),terrainHash(mod(cell+vec3(1,1,0),256.0)),f.x),f.y);
    float b=mix(mix(terrainHash(mod(cell+vec3(0,0,1),256.0)),terrainHash(mod(cell+vec3(1,0,1),256.0)),f.x),
                mix(terrainHash(mod(cell+vec3(0,1,1),256.0)),terrainHash(mod(cell+vec3(1,1,1),256.0)),f.x),f.y);
    return mix(a,b,f.z);
}
vec2 groundMicrostructure(vec3 p,float footprint,float clearance){
    float nearWeight=1.0-smoothstep(18.0,25.0,clearance);
    if(nearWeight<=0.0 || footprint>=.10)return vec2(0);
    vec3 frequency=vec3(8.0,32.0,64.0);
    vec3 octaveWeight=1.0-smoothstep(vec3(.26),vec3(.66),footprint*frequency);
    vec3 layers=vec3(groundNoise(p*frequency.x+vec3(29,7,61)),
        groundNoise(p*frequency.y+vec3(4,57,18)),groundNoise(p*frequency.z+vec3(19,3,35)))-.5;
    layers*=octaveWeight*nearWeight;
    return vec2(dot(layers,vec3(.013,.0045,.0012)),dot(layers,vec3(.29,.25,.14)));
}
void groundDetail(inout vec3 material,inout vec3 normal,vec3 delta,float water,float cameraDistance,float phase){
    if(!patchDrawing || cameraDistance>=100.0)return;
    float fade=1.0-smoothstep(80.0,100.0,cameraDistance);
    vec3 p=patchTextureOrigin+delta;
    float footprint=max(length(dFdx(p)),length(dFdy(p)));
    float fine=1.0-smoothstep(.35,.8,footprint*16.0);
    float grain=groundNoise(p*16.0),pebbles=groundNoise(p*2.0),strata=groundNoise(p*.25);
    vec2 micro=groundMicrostructure(p,footprint,groundClearanceMetres);
    float height=(pebbles*.035+strata*.10+grain*.006*fine+micro.x)*(1.0-water);
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
    float variation=.70+.32*pebbles+.18*strata+.08*(grain-.5)*fine+micro.y;
    material*=mix(1.0,variation,fade*(1.0-water));
}
)";
    return source.c_str();
}
