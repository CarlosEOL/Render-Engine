#pragma once
#include "../src/KerrSpacetime.h"
#include <algorithm>
#include <cmath>

// Independent capture oracle: find minima of Carter's radial quartic, rather
// than integrating the ray or copying the GPU stopping rule. Exterior inward
// rays scatter if the radial potential has a forbidden interval above r+.
namespace KerrCaptureReference {
struct Result { bool captured; double margin; };
inline Result Evaluate(const KerrSpacetime::Ray& ray,double rs,double spin){
    const double a=.5*rs*spin,r=KerrSpacetime::Radius(ray.position,rs,spin);
    const double c=ray.position.z/r,s=std::sqrt(std::max(1e-20,1-c*c));
    const double ptheta=(ray.position.x*ray.momentum.x+ray.position.y*ray.momentum.y)*c/s-ray.momentum.z*r*s;
    const double energy=ray.timeMomentum,l=-(ray.position.x*ray.momentum.y-ray.position.y*ray.momentum.x);
    const double q=ptheta*ptheta+c*c*(l*l/(s*s)-a*a*energy*energy);
    const double e2=energy*energy,C=a*a*e2-l*l-q,D=q+(l-a*energy)*(l-a*energy);
    const auto potential=[&](double x){return e2*x*x*x*x+C*x*x+rs*D*x-a*a*q;};
    const auto derivative=[&](double x){return 4*e2*x*x*x+2*C*x+rs*D;};
    const double horizon=KerrSpacetime::Horizon(rs,spin);
    double minimum=std::min(potential(horizon),potential(r));
    double low=std::max(horizon,std::sqrt(std::max(0.,-C/(6*e2)))),high=r;
    if(low<high && derivative(low)<0 && derivative(high)>0){
        for(int i=0;i<64;++i){const double mid=(low+high)*.5;if(derivative(mid)<0)low=mid;else high=mid;}
        minimum=std::min(minimum,potential((low+high)*.5));
    }
    return {minimum>=0,minimum/std::max(1e-20,e2*rs*rs*rs*rs)};
}
}
