#include "PlanetTerrain.h"
#include "Astronomy.h"
#include "glad/glad.h"
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <glm/gtc/constants.hpp>

namespace {
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
    float seaLevel,float relief,float waves,bool oceanOnly) {
    const float wave = (std::sin(35.0f * direction.y + 4.0f * std::sin(10.0f * direction.x) + wavePhase)
        + 0.35f * std::sin(80.0f * direction.y + 8.0f * direction.x - wavePhase)) / 1.35f;
    if(oceanOnly)return waves*wave;
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
    const float height = 0.75f * continents + 0.22f * (ridges * ridges - 0.5f)
        * glm::smoothstep(-0.1f, 0.25f, continents);
    const float water = 1.0f - glm::smoothstep(seaLevel - 0.008f, seaLevel + 0.008f, height);
    return relief * glm::max(height, seaLevel) + water * waves * wave;
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
    return R"(
uniform float terrainSeed;
uniform float seaLevel;
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

float terrainHeight(vec3 direction) {
    float continents = terrainFbm(direction * 2.6);
    float ridges = 1.0 - abs(terrainNoise(direction * 11.0 + vec3(13.0)));
    return 0.75 * continents + 0.22 * (ridges * ridges - 0.5)
        * smoothstep(-0.1, 0.25, continents);
}

float oceanWave(vec3 direction, float phase) {
    return (sin(35.0 * direction.y + 4.0 * sin(10.0 * direction.x) + phase)
        + 0.35 * sin(80.0 * direction.y + 8.0 * direction.x - phase)) / 1.35;
}

vec3 planetSurface(vec3 direction, float height, float phase) {
    float water = 1.0 - smoothstep(seaLevel - 0.008, seaLevel + 0.008, height);
    float radius = 1.0 + terrainRelief * max(height, seaLevel);
    radius += water * waterWaveHeight * oceanWave(direction, phase);
    return direction * radius;
}

float surfaceElevation(vec3 direction,float height,float phase,bool oceanOnly){
    float water=oceanOnly?1.0:1.0-smoothstep(seaLevel-.008,seaLevel+.008,height);
    return (oceanOnly?0.0:terrainRelief*max(height,seaLevel))+water*waterWaveHeight*oceanWave(direction,phase);
}
)";
}

const char* PlanetTerrain::SurfaceDetailShaderFunctions(){
    return R"(
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
}
