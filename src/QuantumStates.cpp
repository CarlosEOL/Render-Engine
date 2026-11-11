#include "QuantumStates.h"
#include <algorithm>
#include <cmath>

namespace
{
using Complex = std::complex<double>;
const Complex imaginary(0, 1);
constexpr double pi = 3.14159265358979323846;
Complex Phase(double phase)
{
    return std::polar(1.0, phase);
}
double Oscillator(int n, double x)
{
    double a = std::pow(pi, -0.25) * std::exp(-x * x / 2);
    if (n == 0)
        return a;
    double b = std::sqrt(2.0) * x * a;
    for (int k = 1; k < n; ++k)
    {
        const double next = std::sqrt(2.0 / (k + 1)) * x * b - std::sqrt(static_cast<double>(k) / (k + 1)) * a;
        a = b;
        b = next;
    }
    return b;
}
Complex Packet(double x, double t, double center, double momentum, double sigma)
{
    const Complex spread(1, t / (2 * sigma * sigma));
    const double delta = x - center - momentum * t;
    return std::pow(2 * pi * sigma * sigma, -.25) / std::sqrt(spread) *
           std::exp(-delta * delta / (4 * sigma * sigma * spread)) *
           Phase(momentum * (x - center) - momentum * momentum * t / 2);
}
} // namespace

const std::vector<QuantumStates::Example> &QuantumStates::Examples()
{
    static const std::vector<Example> examples = [] {
        std::vector<Example> result;
        for (int n = 1; n <= 6; ++n)
            result.push_back({"Infinite well | n=" + std::to_string(n), Family::Box, n,
                              "Exact stationary state in an infinite well, -5 < x < 5. Nodes increase with n. "
                              "Probability stays fixed while complex phase evolves. E_n = n^2 pi^2 / 200."});
        result.push_back({"Infinite well | 1 + 2 superposition", Family::Box, 7,
                          "Equal coherent superposition of n=1 and n=2. Relative energy phase changes the probability "
                          "density. This is unitary time evolution, without measurement."});
        result.push_back(
            {"Infinite well | 1 + 3 superposition", Family::Box, 8,
             "Equal coherent superposition of n=1 and n=3; interference beats at their energy difference."});
        for (int n = 0; n <= 6; ++n)
            result.push_back({"Harmonic oscillator | n=" + std::to_string(n), Family::Oscillator, n,
                              "Normalized Hermite-Gaussian eigenstate. hbar = m = omega = 1. E_n = n + 1/2. Density is "
                              "stationary; real and imaginary amplitudes rotate."});
        result.push_back({"Oscillator | coherent state", Family::Oscillator, 7,
                          "Exact coherent state with alpha=2. A minimum-uncertainty Gaussian oscillates without "
                          "spreading. Its center follows the classical oscillator."});
        result.push_back(
            {"Oscillator | squeezed vacuum", Family::Oscillator, 8,
             "Exact squeezed vacuum, squeeze parameter r=0.8. Position and momentum widths exchange periodically. The "
             "uncertainty product varies; the covariance satisfies the pure Gaussian bound."});
        result.push_back({"Oscillator | 0 + 1 superposition", Family::Oscillator, 9,
                          "Equal coherent superposition of the ground and first excited oscillator states. Relative "
                          "phase moves probability between the two sides."});
        result.push_back({"Free particle | Gaussian packet", Family::Packet, 0,
                          "Exact free Schrodinger Gaussian: initial sigma=0.6, center=-3, momentum=2. It moves and "
                          "spreads. The finite display does not wrap; use R to restart."});
        result.push_back({"Free particle | counter-propagating packets", Family::Packet, 1,
                          "Coherent sum of two exact free Gaussians initially separated by 6 units. Interference "
                          "appears while they overlap. Packets continue through each other."});
        const char *orbitals[] = {"1s", "2s", "2p_x", "2p_z", "3d_z2", "3d_xz", "3d_x2-y2"};
        for (int n = 0; n < 7; ++n)
            result.push_back({std::string("Hydrogen | ") + orbitals[n], Family::Hydrogen, n,
                              "Analytic hydrogen orbital in atomic units (Bohr radii, Hartree energies). This is a "
                              "central x-z slice of a 3D probability density, not a particle trajectory or a volume "
                              "projection. Hue indicates wavefunction phase."});
        const char *spins[] = {"|0> +Z", "|1> -Z", "|+> +X", "|-> -X", "|+i> +Y", "|0> Rabi oscillation"};
        for (int n = 0; n < 6; ++n)
            result.push_back(
                {std::string("Spin 1/2 | ") + spins[n], Family::Spin, n,
                 "A pure qubit on the Bloch sphere. The vector is (2 Re(a*b), 2 Im(a*b), |a|^2-|b|^2). Bars show Z "
                 "measurement probabilities, not repeated measurement outcomes. Rabi state uses H = sigma_x/2."});
        result.push_back(
            {"Two qubits | Bell Phi+", Family::Bell, 0,
             "Entangled (|00>+|11>)/sqrt(2). Joint Z probabilities are 1/2,0,0,1/2. Each individual qubit is maximally "
             "mixed. Bars describe joint probabilities; they are not two independent Bloch vectors."});
        result.push_back({"Two qubits | Bell Psi-", Family::Bell, 1,
                          "Singlet (|01>-|10>)/sqrt(2). Joint Z probabilities are 0,1/2,1/2,0. The relative minus sign "
                          "matters for other measurement bases."});
        result.push_back({"Numerical | barrier tunneling", Family::Numerical, 0,
                          "1D time-dependent Schrodinger solver, hbar=m=1. A Gaussian (k=2, sigma=0.7) meets V=3 on "
                          "-0.4<x<0.4. Crank-Nicolson integrates the complex wavefunction. Reflection and transmission "
                          "emerge from the equation. Hard walls at +/-12 eventually reflect outgoing waves."});
        result.push_back(
            {"Numerical | step scattering", Family::Numerical, 1,
             "The same unitary finite-difference solver with V=1.5 for x>0. Momentum and reflected/transmitted "
             "probabilities change at the step. Boundaries are reflecting, not open."});
        result.push_back({"Numerical | double-well packet", Family::Numerical, 2,
                          "A localized Gaussian evolves in V=0.08*(x^2-4)^2. This initial packet is a superposition, "
                          "so it both oscillates and tunnels. The grid uses hard walls and 1024 samples; spatial/time "
                          "discretization limits accuracy."});
        return result;
    }();
    return examples;
}

std::complex<double> QuantumStates::Evaluate(const Example &e, double x, double y, double t)
{
    if (e.family == Family::Box)
    {
        if (std::abs(x) >= 5)
            return 0;
        const auto eigen = [&](int n) {
            return std::sqrt(.2) * std::sin(n * pi * (x + 5) / 10) * Phase(-n * n * pi * pi * t / 200);
        };
        if (e.state <= 6)
            return eigen(e.state);
        return (eigen(1) + eigen(e.state == 7 ? 2 : 3)) / std::sqrt(2.0);
    }
    if (e.family == Family::Oscillator)
    {
        if (e.state <= 6)
            return Oscillator(e.state, x) * Phase(-(e.state + .5) * t);
        if (e.state == 9)
            return (Oscillator(0, x) * Phase(-.5 * t) + Oscillator(1, x) * Phase(-1.5 * t)) / std::sqrt(2.0);
        if (e.state == 7)
        {
            const double q = 2 * std::sqrt(2.0) * std::cos(t), p = -2 * std::sqrt(2.0) * std::sin(t);
            return std::pow(pi, -.25) * std::exp(-(x - q) * (x - q) / 2) * Phase(p * (x - q / 2) - t / 2);
        }
        const Complex u = std::cosh(.8) + std::sinh(.8) * Phase(-2 * t);
        const Complex v = std::cosh(.8) - std::sinh(.8) * Phase(-2 * t);
        return std::pow(pi, -.25) / std::sqrt(v) * std::exp(-u * x * x / (2.0 * v)) * Phase(-t / 2);
    }
    if (e.family == Family::Packet)
    {
        if (e.state == 0)
            return Packet(x, t, -3, 2, .6);
        // Exact normalization includes the overlap of the two initial Gaussians.
        const double overlap = std::exp(-36 / (8 * .6 * .6) - 2 * .6 * .6 * 1.5 * 1.5);
        return (Packet(x, t, -3, 1.5, .6) + Packet(x, t, 3, -1.5, .6)) / std::sqrt(2 * (1 + overlap));
    }
    if (e.family == Family::Hydrogen)
    {
        const double r = std::sqrt(x * x + y * y);
        if (e.state == 0)
            return std::exp(-r) / std::sqrt(pi) * Phase(t / 2);
        if (e.state == 1)
            return (2 - r) * std::exp(-r / 2) / (4 * std::sqrt(2 * pi)) * Phase(t / 8);
        if (e.state == 2 || e.state == 3)
            return (e.state == 2 ? x : y) * std::exp(-r / 2) / (4 * std::sqrt(2 * pi)) * Phase(t / 8);
        const double common = std::exp(-r / 3) / (81 * std::sqrt(6 * pi));
        if (e.state == 4)
            return common * (2 * y * y - x * x) * Phase(t / 18);
        if (e.state == 5)
            return common * std::sqrt(12.0) * x * y * Phase(t / 18);
        // The hidden third coordinate is zero in this x-z slice.
        return common * std::sqrt(3.0) * x * x * Phase(t / 18);
    }
    return 0;
}

std::vector<double> QuantumStates::Probabilities(const Example &e, double t)
{
    if (e.family == Family::Bell)
        return e.state == 0 ? std::vector<double>{.5, 0, 0, .5} : std::vector<double>{0, .5, .5, 0};
    if (e.family != Family::Spin)
        return {};
    if (e.state == 0)
        return {1, 0};
    if (e.state == 1)
        return {0, 1};
    if (e.state == 5)
        return {std::pow(std::cos(t / 2), 2), std::pow(std::sin(t / 2), 2)};
    return {.5, .5};
}

void QuantumStates::Solver::Reset(int kind)
{
    wave.resize(size);
    potential.resize(size);
    rhs.resize(size);
    diagonal.resize(size);
    upper.resize(size);
    for (int i = 0; i < size; ++i)
    {
        const double x = -extent + i * dx;
        potential[i] = kind == 0   ? (std::abs(x) < .4 ? 3 : 0)
                       : kind == 1 ? (x > 0 ? 1.5 : 0)
                                   : .08 * std::pow(x * x - 4, 2);
        wave[i] = Packet(x, 0, kind == 2 ? -2 : -5, kind == 2 ? 0 : 2, .7);
    }
    wave.front() = wave.back() = 0;
    const double norm = std::sqrt(Norm());
    for (auto &amplitude : wave)
        amplitude /= norm;
}
double QuantumStates::Solver::Norm() const
{
    double sum = 0;
    for (const auto &amplitude : wave)
        sum += std::norm(amplitude) * dx;
    return sum;
}
void QuantumStates::Solver::Advance(double duration)
{
    if (wave.empty() || !std::isfinite(duration) || duration <= 0)
        return;
    const int steps = static_cast<int>(std::ceil(duration / .002));
    const double dt = duration / steps;
    for (int step = 0; step < steps; ++step)
        Step(dt);
}
void QuantumStates::Solver::Step(double dt)
{
    const Complex off = -imaginary * dt / (4 * dx * dx);
    for (int i = 1; i < size - 1; ++i)
    {
        const Complex d = imaginary * dt * .5 * (1 / (dx * dx) + potential[i]);
        rhs[i] = (1.0 - d) * wave[i] - off * (wave[i - 1] + wave[i + 1]);
        diagonal[i] = 1.0 + d;
    }
    for (int i = 1; i < size - 1; ++i)
    {
        if (i > 1)
        {
            const Complex multiplier = off / diagonal[i - 1];
            diagonal[i] -= multiplier * off;
            rhs[i] -= multiplier * rhs[i - 1];
        }
    }
    wave[size - 2] = rhs[size - 2] / diagonal[size - 2];
    for (int i = size - 3; i >= 1; --i)
        wave[i] = (rhs[i] - off * wave[i + 1]) / diagonal[i];
}
