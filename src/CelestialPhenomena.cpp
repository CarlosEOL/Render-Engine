#include "CelestialPhenomena.h"
#include "Astronomy.h"
#include "MilkyWayModel.h"
#include <cmath>
#include <sstream>
#include <stdexcept>
namespace {
glm::dvec3 Sky(double raHours,double decDegrees,double distanceLy){
    const double ra=glm::radians(raHours*15),dec=glm::radians(decDegrees);
    return MilkyWayModel::EquatorialToEngine({std::cos(dec)*std::cos(ra),std::cos(dec)*std::sin(ra),std::sin(dec)})*(distanceLy*Astronomy::auPerLightYear);
}
}
const std::array<CelestialPhenomena::Nebula,4>& CelestialPhenomena::Nebulae(){
    // Approximate catalogue centres/distances; shapes and emission are original
    // illustrative 3D fields, not a reconstruction of telescope image data.
    static const std::array<Nebula,4> objects={{
        {firstId,"Orion Nebula (M42)","Emission / star-forming nebula",Sky(5+35.0/60+17.3/3600,-5-23.0/60-28.0/3600,1350),12,0,{1.f,.20f,.42f}},
        {firstId+1,"Helix Nebula (NGC 7293)","Planetary nebula / stellar ejecta",Sky(22+29.0/60+38.5454/3600,-20-50.0/60-13.7472/3600,650),1.5,1,{.25f,.85f,1.f}},
        {firstId+2,"Crab Nebula (M1)","Supernova remnant / pulsar wind nebula",Sky(5+34.0/60+31.94/3600,22+52.2/3600,6500),3,2,{1.f,.45f,.18f}},
        {firstId+3,"Veil Nebula (Cygnus Loop)","Filamentary supernova shock shell",Sky(20+51.0/60,30+40.0/60,2100),55,3,{.20f,.8f,.90f}}
    }};return objects;
}
bool CelestialPhenomena::IsNebula(int id){return id>=firstId && id<firstId+static_cast<int>(Nebulae().size());}
const CelestialPhenomena::Nebula& CelestialPhenomena::Get(int id){if(!IsNebula(id))throw std::invalid_argument("Unknown nebula");return Nebulae()[id-firstId];}
std::string CelestialPhenomena::Information(int id){
    const auto& object=Get(id);std::ostringstream out;
    out<<object.name<<"\n"<<object.classification<<"\nParent: Milky Way\n\nApproximate solar distance: "<<glm::length(object.positionAu)/Astronomy::auPerLightYear<<" ly\nIllustrated diameter: "<<object.radiusLy*2<<" ly\n\nOriginal seeded volume with gas, dust and emission-inspired color.\nCatalogue centre and approximate distance follow public astronomy sources. Internal shape, density and color are illustrative.\n";
    return out.str();
}
