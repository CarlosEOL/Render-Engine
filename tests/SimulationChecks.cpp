#include "QuantumStates.h"
#include "StarSystem.h"
#include "Astronomy.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

// The catalogue is exercised without creating an OpenGL context.
unsigned int compileShaderProgram(const char*,const char*) { return 0; }
void Require(bool result,const char* message) {
    if (!result) throw std::runtime_error(message);
}
int main() {
    using namespace QuantumStates;
    for (const auto& example:Examples()) {
        if (example.family==Family::Box || example.family==Family::Oscillator || example.family==Family::Packet) {
            for (double time:{0.0,.7,4.0}) {
                double norm=0;
                constexpr double dx=.004;
                for (int i=0;i<40000;++i) {
                    const double x=-80+(i+.5)*dx;
                    norm+=std::norm(Evaluate(example,x,0,time))*dx;
                }
                if (std::abs(norm-1)>1e-5) std::cerr<<example.name<<" norm="<<norm<<"\n";
                Require(std::abs(norm-1)<1e-5,"Analytic wavefunction normalization failed");
            }
        }
        if (example.family==Family::Spin || example.family==Family::Bell) {
            for (double time:{0.0,.7,4.0}) {
                double norm=0;
                for (double p:Probabilities(example,time)) { Require(p>=0 && p<=1,"Invalid probability"); norm+=p; }
                Require(std::abs(norm-1)<1e-12,"Qubit probabilities not normalized");
            }
        }
    }
    for (int potential=0;potential<3;++potential) {
        Solver solver; solver.Reset(potential);
        Require(std::abs(solver.Norm()-1)<1e-12,"Initial numerical norm failed");
        solver.Advance(5);
        Require(std::abs(solver.Norm()-1)<1e-10,"Crank-Nicolson lost probability");
        if (potential==0) {
            double left=0,right=0;
            for (int i=0;i<Solver::size;++i) {
                const double x=-Solver::extent+i*Solver::dx;
                if (x<-.5) left+=std::norm(solver.Wave()[i])*Solver::dx;
                if (x>.5) right+=std::norm(solver.Wave()[i])*Solver::dx;
            }
            Require(left>.1 && right>.01,"Barrier did not produce reflected and transmitted waves");
            std::cout<<"Barrier R="<<left<<" T="<<right<<" norm="<<solver.Norm()<<"\n";
        }
    }
    StarSystem solar; solar.LoadSolarSystem();
    Require(solar.Planets().size()==20 && solar.AsteroidCount()==4096,"Solar system catalogue incomplete");
    const auto& earth=solar.Planets()[2];
    const double year=6.283185307179586/OrbitalMechanics::MeanMotion(Astronomy::solarGravityAu,earth.orbit.semiMajorAxis);
    Require(year/86400>365 && year/86400<366,"Earth year not physical");
    Require(std::abs(earth.radiusEarth*Astronomy::earthRadiusAu*Astronomy::metresPerAu-6371008.4)<.01,"Earth radius scale incorrect");
    Require(glm::length(solar.Position(2,0)-solar.Position(2,year))<1e-9,"Earth orbit not closed");
    const double moonDistance=glm::length(solar.Position(13,0)-solar.Position(2,0));
    Require(moonDistance*Astronomy::metresPerAu>350e6 && moonDistance*Astronomy::metresPerAu<410e6,"Moon did not orbit Earth at physical distance");
    StarSystem a,b,c; a.Generate(64,"A"); b.Generate(64,"A"); c.Generate(65,"B");
    Require(a.Planets().size()==b.Planets().size(),"Generation not deterministic");
    Require(a.Planets()[0].seed==b.Planets()[0].seed && a.Planets()[0].seed!=c.Planets()[0].seed,"Planet seeds not deterministic and distinct");
    for (int i=0;i<static_cast<int>(a.Planets().size());++i)
        Require(glm::length(a.Position(i,12345)-b.Position(i,12345))<1e-12,"Generated positions not deterministic");
    {
        StarSystem follow; follow.LoadSolarSystem();
        glm::vec3 front(0,0,-1),up(0,1,0);
        follow.SelectPlanet(2); follow.ToggleFollow(0);
        follow.Update(3600,front,up);
        const auto before=follow.camera;
        follow.Pick(glm::vec3(glm::normalize(follow.Position(3,3600)-follow.camera)),3600);
        Require(follow.Selected()==3,"Pick did not select the planet it was aimed at");
        follow.Update(7200,front,up);
        Require(glm::length(follow.camera-before)<1e-12,"Changing the selection while following moved the camera");
    }
    {
        StarSystem land; land.LoadSolarSystem();
        glm::vec3 front(0,0,-1),up(0,1,0);
        land.SelectPlanet(2); land.GoToSelected(0,front,up); land.ToggleLanding(0,front,up);
        Require(land.IsLanded(),"Could not land on Earth");
        const auto& body=land.Planets()[2];
        // The planet is drawn with a float rotation; a landed camera must stay fixed in that frame.
        const auto bodyDirection=[&](double t) {
            auto spin=glm::rotate(glm::mat4(1),body.tilt,glm::vec3(1,0,0));
            spin=glm::rotate(spin,static_cast<float>(std::remainder(t/body.daySeconds,1.0)*6.283185307179586),glm::vec3(0,1,0));
            return glm::normalize(glm::transpose(glm::dmat3(glm::mat3(spin)))*(land.camera-land.Position(2,t)));
        };
        const auto start=bodyDirection(0);
        double drift=0;
        for (int frame=1;frame<=6000;++frame) {
            const double t=frame*0.5;
            land.Update(t,front,up);
            drift=std::max(drift,glm::length(bodyDirection(t)-start));
        }
        std::cout<<"landed drift "<<drift*6371e3<<" m"<<std::endl;
        Require(drift*6371e3<1,"Landed camera drifted along the surface");
    }
    {
        StarSystem pole; pole.LoadSolarSystem();
        glm::vec3 front(0,0,-1),up(0,1,0);
        pole.SelectPlanet(2);
        pole.camera=pole.Position(2,0)+glm::dvec3(0,1e-3,0);
        pole.GoToSelected(0,front,up);
        Require(glm::length(glm::cross(front,up))>.1f,"GoToSelected left the view direction parallel to up");
        StarSystem core; core.LoadSolarSystem();
        core.SelectPlanet(2);
        core.camera=core.Position(2,0);
        core.ToggleLanding(0,front,up);
        Require(std::isfinite(core.camera.x+core.camera.y+core.camera.z),"Landing with the camera at the planet centre produced NaN");
    }
    {
        // Recover the eccentric anomaly from the orbit position and compare it with a bisection run to double precision.
        constexpr double pi=3.141592653589793;
        const auto reference=[&](double mean,double e) {
            double lower=-pi,upper=pi;
            for (int i=0;i<200;++i) {
                const double middle=.5*(lower+upper);
                if (middle-e*std::sin(middle)<mean) lower=middle; else upper=middle;
            }
            return .5*(lower+upper);
        };
        std::vector<double> means={-pi,pi,0,1e-12,-1e-12,1e-9,1e-6,-1e-6,1e-3,pi-1e-9,-pi+1e-9};
        for (int i=0;i<=720;++i) means.push_back(-pi+i*(2*pi/720)+.001);
        double worst=0;
        for (double e:{0.0,.1,.5,.9,.967,.99,.999}) {
            for (double mean:means) {
                if (mean>pi || mean<-pi) continue;
                const auto p=OrbitalMechanics::PositionAtTimeDouble({1,e,0,0,mean,0},1.0,0.0);
                const double anomaly=std::atan2(p.y/std::sqrt(1-e*e),p.x+e);
                const double error=std::abs(std::remainder(anomaly-reference(mean,e),2*pi));
                const double residual=std::abs(std::remainder(anomaly-e*std::sin(anomaly)-mean,2*pi));
                worst=std::max(worst,std::max(error,residual));
                if (error>=1e-12 || residual>=1e-12) std::cerr<<"e="<<e<<" M="<<mean<<" error="<<error<<" residual="<<residual<<"\n";
                Require(error<1e-12 && residual<1e-12,"Kepler solver disagrees with bisection");
            }
        }
        std::cout<<"Kepler solver worst error "<<worst<<" rad"<<std::endl;
    }
    std::cout<<Examples().size()<<" quantum presets checked; solar scale, hierarchy and generation checks passed.\n";
}
