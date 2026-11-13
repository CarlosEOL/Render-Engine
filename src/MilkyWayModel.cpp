#include "MilkyWayModel.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace MilkyWayModel {
glm::dvec3 EquatorialToEngine(const glm::dvec3& q) {
    const auto ecliptic=glm::dmat3(glm::rotate(glm::dmat4(1),glm::radians(-23.439291111),glm::dvec3(1,0,0)))*q;
    return {ecliptic.x,ecliptic.z,-ecliptic.y};
}
glm::dmat3 Orientation() {
    // Rows of the ICRS -> Galactic matrix published in ESA Gaia documentation
    // are the Galactic basis vectors expressed in ICRS.
    const auto centre=EquatorialToEngine({-.0548755604162154,-.8734370902348850,-.4838350155487132});
    const auto north=EquatorialToEngine({-.8676661490190047,-.1980763734312015,.4559837761750669});
    const auto east=EquatorialToEngine({.4941094278755837,-.4448296299600112,.7469822444972189});
    return {-centre,north,east};
}
glm::dvec3 CenterAu(){return Orientation()*glm::dvec3(-solarRadiusKpc*auPerKpc,0,0);}
glm::dvec3 ToModelKpc(const glm::dvec3& world){return glm::transpose(Orientation())*(world-CenterAu())/auPerKpc;}
glm::dvec3 ToWorldAu(const glm::dvec3& local){return CenterAu()+Orientation()*(local*auPerKpc);}
double WarpKpc(const glm::dvec3& p){
    const double r=std::hypot(p.x,p.z);
    return .025*std::pow(std::max(0.0,r-8),1.6)*std::sin(std::atan2(p.z,p.x)-.35);
}
double ExteriorWeight(const glm::dvec3& p){
    const auto fade=[](double low,double high,double x){const double t=std::clamp((x-low)/(high-low),0.0,1.0);return t*t*t*(t*(6*t-15)+10);};
    const double height=fade(.20,2.60,std::abs(p.y-WarpKpc(p)));
    const double edge=fade(12.5,20.0,std::hypot(p.x,p.z));
    return 1-(1-height)*(1-edge);
}
double ArmStrength(const glm::dvec3& p){
    const double r=std::max(.05,std::hypot(p.x,p.z)),theta=std::atan2(p.z,p.x),pitch=glm::radians(12.8);
    double arms=0;
    for(int i=0;i<4;++i){
        const double axis=glm::radians(barAngleDegrees)+i*glm::half_pi<double>()+std::log(r/4.3)/std::tan(pitch);
        const double angle=std::atan2(std::sin(theta-axis),std::cos(theta-axis));
        const double distance=r*angle*std::sin(pitch);
        arms=std::max(arms,(i%2==0?1.0:.70)*std::exp(-.5*distance*distance/(.28*.28)));
    }
    // Orion/Local spur near the solar circle, between the principal arms.
    const double spurRadius=solarRadiusKpc*std::exp(theta*std::tan(pitch));
    const double spur=std::exp(-.5*std::pow((r-spurRadius)/.16,2)-std::pow(theta/.55,4));
    const double window=glm::smoothstep(2.4,3.6,r)*(1-glm::smoothstep(14.5,16.0,r));
    return std::max(arms,spur*.85)*window;
}
glm::dvec4 Densities(const glm::dvec3& p){
    const double r=std::hypot(p.x,p.z),z=std::abs(p.y-WarpKpc(p)),arms=ArmStrength(p);
    const double edge=1-glm::smoothstep(14.5,16.0,r);
    const double scale=.28+.015*std::pow(std::max(0.0,r-8),1.5);
    const double disk=std::exp(-r/diskScaleKpc-z/scale)*(1-std::exp(-r/.8))*edge;
    const double thick=.10*std::exp(-r/3.4-z/.9)*edge;
    const double halo=.004/std::pow(1+glm::length(p)/2.5,3.5);
    const double young=std::exp(-r/3.5-z/.085)*(.10+1.8*arms)*glm::smoothstep(1.8,3.5,r)*edge;
    const double a=glm::radians(barAngleDegrees),u=p.x*std::cos(a)+p.z*std::sin(a),v=-p.x*std::sin(a)+p.z*std::cos(a);
    const double box=std::pow(std::abs(u)/barHalfLengthKpc,4)+std::pow(std::abs(v)/1.25,4)+std::pow(std::abs(p.y)/.55,4);
    const double bar=1.15*std::exp(-2*box);
    const double bulge=1.7*std::exp(-std::sqrt(std::pow(u/1.25,2)+std::pow(v/.65,2)+std::pow(p.y/.65,2)));
    const double dust=std::exp(-r/4.2-z/.095)*(.18+2.8*arms)*glm::smoothstep(1.4,3.0,r)*edge;
    return {disk+thick+halo,young,bar+bulge,dust};
}
const char* ShaderFunctions(){return R"(
float mwWarp(vec3 p){float r=length(p.xz);return .025*pow(max(0.0,r-8.0),1.6)*sin(atan(p.z,p.x)-.35);}
float mwArms(vec3 p){
 float r=max(.05,length(p.xz)),theta=atan(p.z,p.x),pitch=.223402144255;
 float arms=0.0;
 for(int i=0;i<4;++i){float axis=.523598775598+float(i)*1.570796326795+log(r/4.3)/tan(pitch);
 float delta=atan(sin(theta-axis),cos(theta-axis));float d=r*delta*sin(pitch);
 arms=max(arms,(i%2==0?1.0:.70)*exp(-.5*d*d/(.28*.28)));}
 float spurRadius=8.277*exp(theta*tan(pitch));
 float radial=(r-spurRadius)/.16,angular=theta/.55;
 float spur=exp(-.5*radial*radial-angular*angular*angular*angular);
 return max(arms,spur*.85)*smoothstep(2.4,3.6,r)*(1.0-smoothstep(14.5,16.0,r));
}
vec4 mwDensities(vec3 p){
 float r=length(p.xz),z=abs(p.y-mwWarp(p)),arms=mwArms(p),edge=1.0-smoothstep(14.5,16.0,r);
 float h=.28+.015*pow(max(0.0,r-8.0),1.5);
 float disk=exp(-r/2.6-z/h)*(1.0-exp(-r/.8))*edge;
 float thick=.10*exp(-r/3.4-z/.9)*edge;
 float halo=.004/pow(1.0+length(p)/2.5,3.5);
 float young=exp(-r/3.5-z/.085)*(.10+1.8*arms)*smoothstep(1.8,3.5,r)*edge;
 float u=p.x*.866025403784+p.z*.5,v=-p.x*.5+p.z*.866025403784;
 float box=pow(abs(u)/5.0,4.0)+pow(abs(v)/1.25,4.0)+pow(abs(p.y)/.55,4.0);
 float bar=1.15*exp(-2.0*box);
 vec3 bulgePosition=vec3(u/1.25,v/.65,p.y/.65);
 float bulge=1.7*exp(-length(bulgePosition));
 float dust=exp(-r/4.2-z/.095)*(.18+2.8*arms)*smoothstep(1.4,3.0,r)*edge;
 return vec4(disk+thick+halo,young,bar+bulge,dust);
}
)";}
}
