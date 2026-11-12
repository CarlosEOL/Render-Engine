#include "WormholePhysics.h"
#include "Astronomy.h"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

double WormholePhysics::ThroatRadiusAu() {return ThroatRadiusKm*1000/Astronomy::metresPerAu;}
double WormholePhysics::MassLength() {
    const double b=1+Deformation*Deformation;
    return 1/(b+std::sqrt(b*b-Spin*Spin));
}
double WormholePhysics::SpinLength() {return Spin*MassLength();}
OrbitalMechanics::Orbit WormholePhysics::SaturnOrbit() {
    return {OrbitRadiusKm*1000/Astronomy::metresPerAu,.04,glm::radians(14.0),.65,1.1,.4};
}

const char* WormholePhysics::RayShaderFunctions() {
    return R"(
// Rotating Damour-Solodukhin metric, throat radius = 1.
// r=1+ell^2/4 covers both exteriors smoothly; g^ell,ell=(r-a^2)/Sigma.
// Integrate on the unit sphere with n and its canonical angular momentum L.
// This avoids Boyer-Lindquist polar-coordinate singularities entirely.
uniform vec2 metricParameters; // M, a
struct WhState {vec3 n;vec3 L;vec2 radial;}; // radial=(ell,p_ell)
WhState whOffset(WhState y,WhState k,float h){
    WhState v;v.n=y.n+h*k.n;v.L=y.L+h*k.L;v.radial=y.radial+h*k.radial;return v;
}
WhState whDerivative(WhState y,float pt) {
    float m=metricParameters.x,a=metricParameters.y,ell=y.radial.x,pell=y.radial.y,z=y.n.z;
    float r=1.0+.25*ell*ell,sigma=r*r+a*a*z*z,delta=r*r-2.0*m*r+a*a;
    float C=(r*r+a*a)*pt+a*y.L.z;
    float N=(r-a*a)*pell*pell+dot(y.L,y.L)+2.0*a*pt*y.L.z+a*a*pt*pt*(1.0-z*z)-C*C/delta;
    float Nr=pell*pell-4.0*r*pt*C/delta+C*C*(2.0*r-2.0*m)/(delta*delta);
    float omega=(a*pt-a*C/delta)/sigma;
    float torque=-a*a*z*(pt*pt*sigma+N)/(sigma*sigma);
    WhState v;
    v.n=cross(y.L/sigma+vec3(0,0,omega),y.n);
    v.L=cross(vec3(0,0,omega),y.L)+cross(vec3(0,0,torque),y.n);
    v.radial=vec2((r-a*a)*pell/sigma,-.25*ell*(Nr*sigma-N*2.0*r)/(sigma*sigma));
    return v;
}
// Returns 1 for the camera's exterior, -1 for the other exterior, 0 if
// unresolved. Exhausting the budget never invents an escaped sky direction.
int whTrace(vec3 camera,vec3 direction,out vec3 escaped) {
    float r=max(length(camera),1.001);
    WhState y;y.n=normalize(camera);
    float m=metricParameters.x,a=metricParameters.y,sigma=r*r+a*a*y.n.z*y.n.z;
    float delta=r*r-2.0*m*r+a*a,A=(r*r+a*a)*(r*r+a*a)-a*a*delta*(1.0-y.n.z*y.n.z);
    vec3 azimuth=cross(vec3(0,0,1),y.n);
    azimuth=dot(azimuth,azimuth)>.00000001?normalize(azimuth):vec3(1,0,0);
    y.L=sqrt(sigma)*cross(y.n,direction)+(sqrt(A/sigma)-sqrt(sigma))*dot(direction,azimuth)*cross(y.n,azimuth);
    // Past-directed local ZAMO tetrad: energy sign matters for frame dragging.
    float pt=sqrt(sigma*delta/A)-2.0*m*a*r/A*y.L.z;
    y.radial=vec2(2.0*sqrt(r-1.0),sqrt(sigma/(r-a*a))*dot(direction,y.n));
    float escapeEll=2.0*sqrt(max(60.0,r*1.5)-1.0);
    for(int step=0;step<320;++step) {
        WhState k1=whDerivative(y,pt);
        float h=min(.8,.09*(1.0+abs(y.radial.x)));
        h=min(h,.12*(1.0+abs(y.radial.x))/max(abs(k1.radial.x),.001));
        h=min(h,.07/max(length(k1.n),.001));
        WhState k2=whDerivative(whOffset(y,k1,.5*h),pt);
        WhState k3=whDerivative(whOffset(y,k2,.5*h),pt);
        WhState k4=whDerivative(whOffset(y,k3,h),pt);
        y.n=normalize(y.n+h*(k1.n+2.0*k2.n+2.0*k3.n+k4.n)/6.0);
        y.L+=h*(k1.L+2.0*k2.L+2.0*k3.L+k4.L)/6.0;
        y.radial+=h*(k1.radial+2.0*k2.radial+2.0*k3.radial+k4.radial)/6.0;
        if(any(isnan(y.n)) || any(isinf(y.n)) || any(isnan(y.radial)) || any(isinf(y.radial)))return 0;
        if(abs(y.radial.x)>escapeEll && y.radial.x*y.radial.y>0.0) {
            // At the finite outer sphere retain the transverse direction,
            // rather than treating its radial normal as the final ray.
            WhState tangent=whDerivative(y,pt);
            float rr=1.0+.25*y.radial.x*y.radial.x;
            escaped=normalize(y.n*(.5*y.radial.x*tangent.radial.x)+rr*tangent.n);
            return y.radial.x<0.0?-1:1;
        }
    }
    escaped=direction;return 0;
}
)";
}
