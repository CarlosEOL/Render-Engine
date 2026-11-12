#pragma once
#include "Astronomy.h"
#include <algorithm>
#include <cmath>
namespace StellarPhotometry {
// Bolometric estimate from the synthetic stellar luminosity; extinction and
// passband-dependent bolometric corrections are not measured in this catalogue.
inline double ApparentMagnitude(double luminositySolar,double distanceAu){
    const double pc=std::max(distanceAu/206264.80624709636,1e-12);
    return 4.74-2.5*std::log10(std::max(luminositySolar,1e-12))+5*std::log10(pc/10);
}
inline bool Visible(double luminosity,double distance,double limit){return ApparentMagnitude(luminosity,distance)<limit+.2;}
}
