#pragma once
#include <complex>
#include <string>
#include <vector>

namespace QuantumStates
{
enum class Family
{
    Box,
    Oscillator,
    Packet,
    Hydrogen,
    Spin,
    Bell,
    Numerical
};
struct Example
{
    std::string name;
    Family family;
    int state;
    std::string explanation;
};
const std::vector<Example> &Examples();
std::complex<double> Evaluate(const Example &example, double x, double y, double time);
std::complex<double> Hydrogen3D(int orbital, double x, double y, double z, double time);
double HydrogenRadial(int orbital, double radius);
std::vector<double> Probabilities(const Example &example, double time);
// hbar=m=1; hard walls at +/-12. Crank-Nicolson evolves a sampled wavefunction.
class Solver
{
  public:
    static constexpr int size = 1024;
    static constexpr double extent = 12;
    static constexpr double dx = 2 * extent / (size - 1);
    void Reset(int potential);
    void Advance(double duration);
    const std::vector<std::complex<double>> &Wave() const
    {
        return wave;
    }
    const std::vector<double> &Potential() const
    {
        return potential;
    }
    double Norm() const;

  private:
    std::vector<std::complex<double>> wave, rhs, diagonal, upper;
    std::vector<double> potential;
    void Step(double dt);
};
} // namespace QuantumStates
