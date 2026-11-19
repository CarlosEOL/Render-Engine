#pragma once
#include "PlanetTerrain.h"
#include "glad/glad.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <deque>
#include <cmath>
#include <cstdint>

// Seeded visual weather, shared by clouds, ocean geometry and reflections.
// It represents circulation/storms, not a numerical atmospheric forecast.
namespace PlanetWeather {
struct Profile {
    unsigned seed=0;
    float radiusMetres=6371000,seaLevel=.035f,temperature=288,wind=8;
    bool oceanOnly=false,enabled=true;
    bool metalCore=false,magneticField=false,atmosphere=false;
};
struct State {
    glm::vec4 parameters{}; // ambient wind m/s, humidity, seed offset, radius m
    glm::vec3 advection{};
    std::array<glm::vec4,3> storms{}; // unit centre, current maximum wind m/s
    std::array<glm::vec4,3> shapes{}; // angular radius, hemisphere, phase, life
    std::array<glm::vec4,4> spectrum{}; // periodic wave vector (rad/m), amplitude
    glm::vec4 phases{};
    glm::vec4 ripplePhases{};
    float foamPhase=0;
    glm::vec4 auroraAxis{},auroraMotion{};
};
inline float Unit(unsigned value){value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;value^=value>>16;return (value&0xffffffu)/16777215.f;}
inline constexpr glm::vec4 rippleFrequencies{8,21,55,144};
inline bool Ocean(const Profile& p,const glm::vec3& n){
    return p.oceanOnly || PlanetTerrain::SurfaceElevation(n,p.seed,0,p.seaLevel,1,0)<=p.seaLevel+1e-6f;
}
inline std::array<glm::vec3,3> Centres(const Profile& p){
    struct Entry{unsigned seed;float sea;bool ocean;std::array<glm::vec3,3> centres;};
    static std::deque<Entry> cache;
    for(const auto& entry:cache)if(entry.seed==p.seed && entry.sea==p.seaLevel && entry.ocean==p.oceanOnly)return entry.centres;
    std::array<glm::vec3,3> centres{};
    for(unsigned i=0;i<3;++i)for(unsigned attempt=0;attempt<48;++attempt){
        const unsigned h=p.seed+i*1337+attempt*7907;
        const float latitude=glm::radians(12.f+20*Unit(h+31))*(i==1?-1.f:1.f),longitude=glm::two_pi<float>()*Unit(h+79);
        const glm::vec3 n{std::cos(latitude)*std::cos(longitude),std::sin(latitude),std::cos(latitude)*std::sin(longitude)};
        if(!Ocean(p,n))continue;
        bool separate=true;for(unsigned j=0;j<i;++j)if(glm::dot(n,centres[j])>.92f)separate=false;
        if(separate){centres[i]=n;break;}
    }
    if(cache.size()==32)cache.pop_front();cache.push_back({p.seed,p.seaLevel,p.oceanOnly,centres});return centres;
}
inline State Make(const Profile& p,double seconds,float windScale=1,float stormScale=1){
    State s;
    if(!std::isfinite(seconds))seconds=0;
    if(p.metalCore && p.magneticField && p.atmosphere){
        const float tilt=.12f+.12f*Unit(p.seed+907),longitude=Unit(p.seed+1009)*glm::two_pi<float>();
        s.auroraAxis={std::sin(tilt)*std::cos(longitude),std::cos(tilt),std::sin(tilt)*std::sin(longitude),1};
        s.auroraMotion={static_cast<float>(std::remainder(seconds/240.,glm::two_pi<double>())),
            static_cast<float>(std::remainder(seconds/1100.,glm::two_pi<double>())),Unit(p.seed+797)*37,p.radiusMetres};
    }
    if(!p.enabled)return s;
    windScale=std::isfinite(windScale)?glm::clamp(windScale,0.f,4.f):1.f;
    stormScale=std::isfinite(stormScale)?glm::clamp(stormScale,0.f,2.f):1.f;
    const float wind=glm::clamp(p.wind*windScale,0.f,60.f);
    const float humidity=p.oceanOnly?.62f:p.seaLevel>-.2f?.58f:.28f;
    s.parameters={wind,humidity,Unit(p.seed)*127,p.radiusMetres};
    for(int i=0;i<3;++i)s.advection[i]=static_cast<float>(std::remainder(seconds*wind/p.radiusMetres*(i==0?-.6:i==1?1.1:-.35),glm::two_pi<double>()));
    if(p.temperature>=273 && p.temperature<=320 && stormScale>0){
        const auto centres=Centres(p);
        for(int i=0;i<3;++i){
            auto centre=centres[i];if(glm::length(centre)<.5f)continue;
            const double period=(6+3*Unit(p.seed+i+19))*86400.;
            const float age=static_cast<float>(glm::fract(seconds/period+.20+.50*Unit(p.seed+i+43)));
            const float life=glm::smoothstep(0.f,.18f,age)*(1-glm::smoothstep(.70f,1.f,age));
            const double drift=.018*std::sin(seconds/(86400.*2.5)+i);
            centre=glm::vec3(centre.x*std::cos(drift)-centre.z*std::sin(drift),centre.y,centre.x*std::sin(drift)+centre.z*std::cos(drift));
            const float ocean=Ocean(p,centre)?1.f:0.f;
            s.storms[i]={centre,(42+20*Unit(p.seed+i+71))*life*ocean*stormScale*windScale};
            s.shapes[i]={.055f+.035f*Unit(p.seed+i+101),centre.y>0?1.f:-1.f,
                static_cast<float>(std::remainder(seconds/(1800.+1200*Unit(p.seed+i+181)),glm::two_pi<double>())),life*ocean*stormScale};
        }
    }
    const float heading=glm::two_pi<float>()*Unit(p.seed+401);
    const float wavelength[4]={256,100,32,10},height[4]={.44f,.19f,.075f,.022f};
    for(int i=0;i<4;++i){
        const float angle=heading+(i%2?-.35f:.22f)*i;
        const float count=4096/wavelength[i];
        // A three-dimensional directional spectrum avoids every swell collapsing
        // into parallel stripes at equatorial surface locations.
        const float elevation=(i%2?-.65f:.45f)+.3f*Unit(p.seed+421+i);
        const auto direction=glm::vec3(std::cos(angle)*std::cos(elevation),std::sin(elevation),std::sin(angle)*std::cos(elevation));
        const auto k=glm::round(direction*count)*(glm::two_pi<float>()/4096.f);
        s.spectrum[i]={k,height[i]};
        const double omega=std::sqrt(9.81*glm::length(k));
        const double phase=-seconds*omega+i*1.618;
        const auto wrap=[](double angle){return static_cast<float>(std::remainder(angle,glm::two_pi<double>()));};
        s.phases[i]=wrap(phase);
        // Scale the continuous phase first. Scaling a wrapped angle by a
        // fractional factor makes its sine or cosine jump at each base cycle.
        s.ripplePhases[i]=wrap(phase*std::sqrt(static_cast<double>(rippleFrequencies[i])));
        if(i==0)s.foamPhase=wrap(phase*.3);
    }
    return s;
}
inline Profile Miller(){return {731,6371000,.035f,288,14,true,true,true,true,true};}
inline float StormWind(const State& s,int index,const glm::vec3& n){
    const auto& storm=s.storms[index];
    if(storm.w<=0)return 0;
    const float r=std::acos(glm::clamp(glm::dot(n,glm::vec3(storm)),-1.f,1.f))/s.shapes[index].x;
    return storm.w*glm::smoothstep(.05f,.20f,r)*std::exp(-r*r*3.f);
}
inline float WindSpeed(const State& s,const glm::vec3& direction){
    if(s.parameters.w<=0)return 0;
    const auto n=glm::normalize(direction);float wind=s.parameters.x*(.72f+.28f*std::cos(n.y*6));
    for(int i=0;i<3;++i)wind+=StormWind(s,i,n);
    return std::min(wind,85.f);
}
// Planet-fixed wind in m/s, tangent to the surface. The prevailing flow
// follows the primary swell, with circulation from each local storm.
inline glm::vec3 SurfaceWind(const State& s,const glm::vec3& n){
    if(s.parameters.w<=0 || s.parameters.x<=0)return glm::vec3(0);
    const auto prevailing=glm::vec3(s.spectrum[0]);
    const auto tangent=prevailing-n*glm::dot(n,prevailing);
    const float ambient=s.parameters.x*(.72f+.28f*std::cos(n.y*6));
    auto wind=tangent*(ambient/std::max(glm::length(tangent),.002f));
    for(int i=0;i<3;++i){
        const float speed=StormWind(s,i,n);
        if(speed<=0)continue;
        const auto around=glm::cross(glm::vec3(s.storms[i]),n);
        wind+=around*(speed*s.shapes[i].y/std::max(glm::length(around),1e-6f));
    }
    return wind*(85.f/std::max(glm::length(wind),85.f));
}
inline double WaveHeight(const State& s,const glm::dvec3& metres,const glm::vec3& n){
    const float wind=WindSpeed(s,n);double height=0;
    const auto p=glm::mod(metres,glm::dvec3(4096));
    for(int i=0;i<4;++i){const auto k=glm::vec3(s.spectrum[i]);const double length=glm::length(k);if(length==0)continue;
        const double amplitude=std::min(s.spectrum[i].w*std::pow(wind/8.,1.65),.20/length);
        const double phase=glm::dot(p,glm::dvec3(k))+s.phases[i],steepness=std::min(.20,.5*length*amplitude);
        height+=amplitude*(std::sin(phase)-steepness*std::cos(2*phase));
    }return height;
}
inline void Upload(unsigned program,const State& s){
    glUniform4fv(glGetUniformLocation(program,"weatherParameters"),1,glm::value_ptr(s.parameters));
    glUniform3fv(glGetUniformLocation(program,"weatherAdvection"),1,glm::value_ptr(s.advection));
    glUniform4fv(glGetUniformLocation(program,"weatherStorms"),3,glm::value_ptr(s.storms[0]));
    glUniform4fv(glGetUniformLocation(program,"weatherShapes"),3,glm::value_ptr(s.shapes[0]));
    glUniform4fv(glGetUniformLocation(program,"weatherSpectrum"),4,glm::value_ptr(s.spectrum[0]));
    glUniform4fv(glGetUniformLocation(program,"weatherPhases"),1,glm::value_ptr(s.phases));
    glUniform4fv(glGetUniformLocation(program,"weatherRipplePhases"),1,glm::value_ptr(s.ripplePhases));
    glUniform4fv(glGetUniformLocation(program,"weatherRippleFrequencies"),1,glm::value_ptr(rippleFrequencies));
    glUniform1f(glGetUniformLocation(program,"weatherFoamPhase"),s.foamPhase);
    glUniform4fv(glGetUniformLocation(program,"auroraAxis"),1,glm::value_ptr(s.auroraAxis));
    glUniform4fv(glGetUniformLocation(program,"auroraMotion"),1,glm::value_ptr(s.auroraMotion));
}
inline const char* ShaderFunctions(){return R"(
uniform vec4 weatherParameters,weatherPhases;
uniform vec4 auroraAxis,auroraMotion;
uniform vec3 weatherAdvection;
uniform vec4 weatherStorms[3],weatherShapes[3],weatherSpectrum[4];
float weatherWind(vec3 n){
    float wind=weatherParameters.x*(.72+.28*cos(n.y*6.0));
    for(int i=0;i<3;++i){
        if(weatherStorms[i].w<=0.0)continue;
        float r=acos(clamp(dot(n,weatherStorms[i].xyz),-1.0,1.0))/weatherShapes[i].x;
        wind+=weatherStorms[i].w*smoothstep(.05,.20,r)*exp(-r*r*3.0);
    }return min(wind,85.0);
}
float weatherWaveAmplitude(int i,float wind){
    return min(weatherSpectrum[i].w*pow(max(wind,0.0)/8.0,1.65),.20/max(length(weatherSpectrum[i].xyz),1e-6));
}
float weatherWaveHeight(vec3 metres,vec3 n){
    vec3 p=mod(metres,4096.0);float height=0.0,wind=weatherWind(n);
    for(int i=0;i<4;++i){
        float amplitude=weatherWaveAmplitude(i,wind),phase=dot(p,weatherSpectrum[i].xyz)+weatherPhases[i];
        float steepness=min(.20,.5*length(weatherSpectrum[i].xyz)*amplitude);
        height+=amplitude*(sin(phase)-steepness*cos(2.0*phase));
    }
    return height;
}
)";}
inline const char* SurfaceShaderFunctions(){return R"(
uniform vec4 weatherRipplePhases,weatherRippleFrequencies;
uniform float weatherFoamPhase;
vec3 weatherWaveGradient(vec3 metres,vec3 n,bool filtering){
    vec3 gradient=vec3(0),p=mod(metres,4096.0);float wind=weatherWind(n);
    for(int i=0;i<4;++i){
        vec3 k=weatherSpectrum[i].xyz;if(length(k)<1e-6)continue;float a=dot(p,k)+weatherPhases[i];
        float visible=filtering?1.0-smoothstep(.7,3.0,fwidth(dot(metres,k))):1.0;
        float amplitude=weatherWaveAmplitude(i,wind),steepness=min(.20,.5*length(k)*amplitude);
        gradient+=amplitude*k*(cos(a)+2.0*steepness*sin(2.0*a))*visible;
        // Fine capillary/wind detail remains in the material, not coarse mesh
        // displacement. Integer frequency multiples retain coordinate wrapping.
        vec3 rippleK=k*weatherRippleFrequencies[i];float ripple=dot(p,rippleK)+weatherRipplePhases[i];
        float rippleVisible=filtering?1.0-smoothstep(.7,3.0,fwidth(dot(metres,rippleK))):1.0;
        gradient+=normalize(rippleK)*(.025+.025*float(i))*smoothstep(0.0,14.0,wind)*cos(ripple)*rippleVisible;
    }
    return gradient-n*dot(gradient,n);
}
float weatherOceanFoam(vec3 metres,vec3 n){
    vec3 p=mod(metres,4096.0);float wind=weatherWind(n),compression=0.0;
    for(int i=0;i<4;++i){
        vec3 k=weatherSpectrum[i].xyz;float a=dot(p,k)+weatherPhases[i];
        float visible=1.0-smoothstep(.8,3.0,fwidth(dot(metres,k)));
        compression+=weatherWaveAmplitude(i,wind)*length(k)*pow(max(sin(a),0.0),3.0)*visible;
    }
    // Whole cycles per spatial tile keep the breakup continuous at its edges.
    const float tileFrequency=6.283185307179586/4096.0;
    float breakup=.65+.35*sin(dot(p,vec3(476,189,346)*tileFrequency)+weatherFoamPhase)
        *sin(dot(p,vec3(-202,398,150)*tileFrequency));
    return smoothstep(.09,.34,compression)*smoothstep(7.0,22.0,wind)*breakup;
}
)";}
}
