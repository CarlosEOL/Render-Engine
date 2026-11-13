#include "GalacticCore.h"
#include "Astronomy.h"
#include "MilkyWayModel.h"
#include "UniverseCatalog.h"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
double GalacticCores::Core::RadiusAu() const{return Astronomy::gargantuaHorizonAu*massSolar/1e8;}
glm::dmat3 GalacticCores::HotFlowFrame(){
    return glm::dmat3(glm::rotate(glm::dmat4(1),glm::radians(28.0),glm::dvec3(0,0,1))
        *glm::rotate(glm::dmat4(1),glm::radians(58.0),glm::dvec3(1,0,0)));
}
double GalacticCores::FlowPeriodSeconds(const Core& core,double radius){
    return glm::two_pi<double>()*(core.RadiusAu()*.5/Astronomy::lightSpeedAu)*(std::pow(2*radius,1.5)+core.spin);
}
double GalacticCores::TonComovingDistanceAu(){
    static const double distance=[](){
    // Flat Planck-2018 LCDM radial comoving distance; not lookback distance.
    constexpr int steps=2048;constexpr double z=2.219,dz=z/steps;
    double sum=0;for(int i=0;i<=steps;++i){double q=1+i*dz;sum+=(i==0 || i==steps?1:i%2?4:2)/std::sqrt(.315*q*q*q+.685);}
    return sum*dz/3*299792.458/67.4*1e6*MilkyWayModel::auPerParsec;
    }();return distance;
}
GalacticCores::Core GalacticCores::Get(int id){
    if(!IsCore(id))throw std::invalid_argument("Unknown galactic core");
    const int index=id-firstId;
    if(index==0)return {id,"Sagittarius A*","Supermassive black hole / Milky Way nucleus",MilkyWayModel::CenterAu(),MilkyWayModel::centralMassSolar,0,.8,1e9,false,true};
    if(index<4)return {id,"Galaxy "+std::to_string(index)+" central black hole","Procedural supermassive black hole",Universe::GalaxyAt(index).centerAu,index==1?1.4e8:index==2?3.2e7:8.5e8,0,.6,6500,false};
    const double ra=glm::radians(187.1040248856),dec=glm::radians(31.4771191632);
    const glm::dvec3 equatorial(std::cos(dec)*std::cos(ra),std::cos(dec)*std::sin(ra),std::sin(dec));
    return {id,"TON 618","Ultramassive black hole / luminous quasar",MilkyWayModel::EquatorialToEngine(equatorial)*TonComovingDistanceAu(),4.07e10,2.219,18,12500,true};
}
std::string GalacticCores::Information(int id){
    const auto core=Get(id);std::ostringstream out;
    out<<core.name<<"\n"<<core.classification<<"\n\nMass  "<<std::setprecision(5)<<core.massSolar<<" solar\nSchwarzschild radius  "<<core.RadiusAu()<<" AU\n";
    if(core.quasar)out<<"Redshift  "<<core.redshift<<"\nRadial comoving distance  "<<TonComovingDistanceAu()/Astronomy::auPerLightYear/1e9<<" billion ly\nFlat LCDM: H0=67.4, Omega_m=0.315\nVirial mass estimates are uncertain; publications differ.\n\nLocal quasar view: artistic disk and polar outflow, inspired by the supplied reference.\nSpin, disk orientation and outflow appearance are assumed, not resolved observations.\n";
    else out<<(id==firstId?"Observed Galactic-centre mass and distance; idealized Galactic midplane.\nHot, optically thin, geometrically thick flow\nBroad diffuse outflow; low optical efficiency\nPlasma temperature model: ~1 billion K\nOptical colours and outflow shape are illustrative.\n":"Galaxy and core parameters are seeded illustrative models.\n");
    if(core.hotFlow)out<<"Model rotation period at 3 Schwarzschild radii: "<<std::setprecision(3)<<FlowPeriodSeconds(core)/60<<" minutes\n";
    out<<"\nGPU Kerr lensing; assumed dimensionless spin 0.8\nAge: not constrained by this model\n";
    return out.str();
}
