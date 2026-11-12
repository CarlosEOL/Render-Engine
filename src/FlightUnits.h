#pragma once
#include "Astronomy.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

namespace FlightUnits {
inline constexpr double minimumAu=1e-15,maximumAu=1e12;
inline double Step(double speed,double notches){
    return std::clamp(speed*std::pow(1.15,std::clamp(notches,-8.0,8.0)),minimumAu,maximumAu);
}
inline std::string Label(double auPerSecond,bool physical=true){
    double value=auPerSecond;const char* unit="units/s";
    if(physical){
        const double metres=value*Astronomy::metresPerAu;
        if(metres<1000){value=metres;unit="m/s";}
        else if(metres<Astronomy::lightSpeedAu*Astronomy::metresPerAu*.1){value=metres/1000;unit="km/s";}
        else if(value<1){value/=Astronomy::lightSpeedAu;unit="c";}
        else if(value<Astronomy::auPerLightYear*.01){unit="AU/s";}
        else{value/=Astronomy::auPerLightYear;unit="ly/s";}
    }
    std::ostringstream text;text<<std::fixed<<std::setprecision(value<10?2:value<100?1:0)<<value<<' '<<unit;
    return text.str();
}
}
