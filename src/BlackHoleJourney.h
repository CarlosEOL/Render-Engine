#pragma once
#include <algorithm>
#include <cmath>

// Navigation state, separate from the physical Kerr observer. The Gargantua
// destination is a fictional cinematic scene, not an extension of that metric.
struct BlackHoleJourney {
    int source=-1;
    double insideSeconds=0;
    double interiorBlend=0;
    bool inside=false,atLimit=false,tesseract=false;
    void Reset(){*this=BlackHoleJourney{};}
    void Advance(int id,bool belowHorizon,double seconds,double horizonFraction=1){
        if(source!=id || !belowHorizon){Reset();source=id;}
        if(!belowHorizon)return;
        inside=true;
        if(std::isfinite(seconds))insideSeconds+=std::clamp(seconds,0.0,.25);
        const double depth=std::clamp((.92-horizonFraction)/.37,0.0,1.0);
        interiorBlend=id==0?depth*depth*(3-2*depth):0;
        tesseract=interiorBlend>0;
    }
};
