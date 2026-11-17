#pragma once
#include "MilkyWayModel.h"
#include "Astronomy.h"
#include <array>
#include <algorithm>
#include <cmath>

// SIMBAD J2000 centres, V magnitudes, optical major/minor axes and PA;
// rounded distance estimates. Morphology is illustrative, not a survey image.
namespace NearbyGalaxies {
inline constexpr int firstId=100;
struct Galaxy {
    const char* name;const char* aliases;
    double ra,dec,distanceLy,majorArcmin,minorArcmin,positionAngle,magnitude;
    int morphology;
    glm::dvec3 Direction()const{
        const double a=glm::radians(ra),d=glm::radians(dec);
        return MilkyWayModel::EquatorialToEngine({std::cos(d)*std::cos(a),std::cos(d)*std::sin(a),std::sin(d)});
    }
    glm::dvec3 Center()const{return Direction()*distanceLy*Astronomy::auPerLightYear;}
    double Radius()const{return distanceLy*Astronomy::auPerLightYear*std::tan(glm::radians(majorArcmin/120.0));}
    glm::dmat3 Frame()const{
        const double a=glm::radians(ra),d=glm::radians(dec),pa=glm::radians(positionAngle);
        const auto north=MilkyWayModel::EquatorialToEngine({-std::sin(d)*std::cos(a),-std::sin(d)*std::sin(a),std::cos(d)});
        const auto east=MilkyWayModel::EquatorialToEngine({-std::sin(a),std::cos(a),0});
        const auto major=north*std::cos(pa)+east*std::sin(pa),direction=Direction();
        const auto across=glm::normalize(glm::cross(direction,major));
        const double q=minorArcmin/majorArcmin,cosine=std::sqrt(std::max(0.0,(q*q-.13*.13)/(1.0-.13*.13)));
        const auto normal=direction*cosine+across*std::sqrt(1.0-cosine*cosine);
        return {major,normal,glm::normalize(glm::cross(major,normal))};
    }
};
inline const std::array<Galaxy,5> Catalog{{
    {"Andromeda (M31)","M31 NGC224 UGC454 Andromeda",10.6847083333,41.26875,2540000,199.53,70.79,35,3.44,0},
    {"Triangulum (M33)","M33 NGC598 Triangulum",23.4620690622,30.6601751120,2730000,60.26,35.48,22,5.72,0},
    {"Large Magellanic Cloud", "LMC Large Magellanic Cloud",80.8941666667,-69.7561111111,163000,322.82666,274.77,170,.4,3},
    {"Small Magellanic Cloud", "SMC Small Magellanic Cloud",13.1583333333,-72.8002777778,200000,158.11333,93.105,45,2.2,3},
    // Full 120 kly outer ring (NASA/Hubble); SIMBAD D25 is the smaller bright
    // optical extent. The name, J2000 centre and V magnitude are catalogued.
    {"Hoag's Object", "Hoag Hoags Object Haog Haogs Object PGC54559 PRCD51",229.31002860781,21.58551562657,600000000,
        .687549352,.637771,0,15.1,4}
}};
inline bool IsCatalogued(int id){return id>=firstId && id<firstId+static_cast<int>(Catalog.size());}
}
