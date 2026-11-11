#include "QuantumStates.h"
#include "StarSystem.h"
#include "Astronomy.h"
#include <cmath>
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
    std::cout<<Examples().size()<<" quantum presets checked; solar scale, hierarchy and generation checks passed.\n";
}
