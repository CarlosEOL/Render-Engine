#pragma once
#include <glm/glm.hpp>
#include <string>
namespace GalacticCores {
inline constexpr int firstId=1000000,count=5,tonId=firstId+4;
inline bool IsCore(int id){return id>=firstId && id<firstId+count;}
struct Core {int id;std::string name,classification;glm::dvec3 position;double massSolar,redshift,emission,temperature;bool quasar;bool hotFlow=false;double spin=.8;double RadiusAu() const;};
Core Get(int id);
std::string Information(int id);
double TonComovingDistanceAu();
glm::dmat3 HotFlowFrame();
double FlowPeriodSeconds(const Core& core,double radiusInSchwarzschildUnits=3);
}
