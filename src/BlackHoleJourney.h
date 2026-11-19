#pragma once
#include "Astronomy.h"
#include <algorithm>
#include <cmath>

// Navigation state, separate from the physical Kerr observer. The Gargantua
// destination is a fictional cinematic scene, not an extension of that metric.
struct BlackHoleJourney {
    static constexpr double revealDepthKm=1000;
    static constexpr double furnishedRadiusFraction=.55;
    int source=-1;
    double insideSeconds=0;
    double gridBlend=0;
    double gridApproach=0;
    double interiorBlend=0;
    bool inside=false,atLimit=false,tesseract=false;
    void Reset(){*this=BlackHoleJourney{};}
    void Advance(int id,bool belowHorizon,double seconds,double radiusAu,double horizonAu){
        if(source!=id || !belowHorizon){Reset();source=id;}
        if(!belowHorizon)return;
        inside=true;
        if(std::isfinite(seconds))insideSeconds+=std::clamp(seconds,0.0,.25);
        if(id!=0)return;
        const double depthKm=(horizonAu-radiusAu)*(Astronomy::metresPerAu/1000.0);
        const double inward=1-radiusAu/horizonAu;
        const auto smooth=[](double fraction){
            const double t=std::clamp(fraction,0.0,1.0);
            return t*t*(3-2*t);
        };
        gridBlend=smooth((depthKm-revealDepthKm)/5000.0);
        gridApproach=smooth(inward/(1-furnishedRadiusFraction));
        interiorBlend=gridBlend*smooth((inward-.29)/(1-furnishedRadiusFraction-.29));
        tesseract=gridBlend>0;
    }
};
