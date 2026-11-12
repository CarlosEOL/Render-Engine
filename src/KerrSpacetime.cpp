#include "KerrSpacetime.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace {
struct Metric {
  double r, h;
  glm::dvec3 l, gradR, gradH;
};
Metric Evaluate(const glm::dvec3 &x, double rs, double spin) {
  const double a = .5 * rs * spin, a2 = a * a,
               r = KerrSpacetime::Radius(x, rs, spin);
  const double r2 = r * r, b = r2 + a2, d = r2 * r2 + a2 * x.z * x.z;
  const auto gradR =
      (r * x + glm::dvec3(0, 0, a2 * x.z / r)) / (2 * r2 - glm::dot(x, x) + a2);
  const double h = .5 * rs * r * r2 / d;
  return {r,
          h,
          {(r * x.x + a * x.y) / b, (r * x.y - a * x.x) / b, x.z / r},
          gradR,
          h * ((3 / r - 4 * r * r2 / d) * gradR -
               glm::dvec3(0, 0, 2 * a2 * x.z / d))};
}
std::pair<glm::dvec3, glm::dvec3> Derivative(const KerrSpacetime::Ray &ray,
                                             double rs, double spin) {
  const auto m = Evaluate(ray.position, rs, spin);
  const auto &x = ray.position;
  const auto &p = ray.momentum;
  const double a = .5 * rs * spin, b = m.r * m.r + a * a,
               u = x.x * p.x + x.y * p.y, v = x.y * p.x - x.x * p.y;
  const double coefficient =
      (u * b - 2 * m.r * (m.r * u + a * v)) / (b * b) - x.z * p.z / (m.r * m.r);
  const auto gradLp =
      coefficient * m.gradR + glm::dvec3((m.r * p.x - a * p.y) / b,
                                         (m.r * p.y + a * p.x) / b, p.z / m.r);
  const double s = glm::dot(m.l, p) - ray.timeMomentum;
  return {p - 2 * m.h * s * m.l, m.gradH * (s * s) + gradLp * (2 * m.h * s)};
}
} // namespace

double KerrSpacetime::Radius(const glm::dvec3 &x, double rs, double spin) {
  const double a = .5 * rs * spin, q = glm::dot(x, x) - a * a;
  return std::sqrt(
      std::max(1e-24, .5 * (q + std::sqrt(q * q + 4 * a * a * x.z * x.z))));
}
double KerrSpacetime::Horizon(double rs, double spin) {
  if (rs <= 0 || std::abs(spin) >= 1)
    throw std::invalid_argument("Kerr requires positive mass and |spin| < 1");
  return .5 * rs * (1 + std::sqrt(1 - spin * spin));
}
double KerrSpacetime::ProgradeIsco(double rs, double spin) {
  Horizon(rs, spin);
  const double z1 = 1 + std::cbrt(1 - spin * spin) *
                            (std::cbrt(1 + spin) + std::cbrt(1 - spin));
  const double z2 = std::sqrt(3 * spin * spin + z1 * z1);
  return .5 * rs *
         (3 + z2 -
          (spin >= 0 ? 1 : -1) * std::sqrt((3 - z1) * (3 + z1 + 2 * z2)));
}
KerrSpacetime::Ray KerrSpacetime::PastRay(const glm::dvec3 &x,
                                          const glm::dvec3 &direction,
                                          double rs, double spin) {
  const auto m = Evaluate(x, rs, spin);
  const auto n = glm::normalize(direction);
  const double q = glm::dot(n, m.l), scale = std::sqrt(1 + 2 * m.h);
  // Orthonormal Eulerian observer of the regular Kerr-Schild time slices.
  // Past-directed rays retain their signed covariant time momentum.
  return {x, n + m.l * (q * (scale - 1)), (1 + 2 * m.h * q) / scale};
}
double KerrSpacetime::Hamiltonian(const Ray &ray, double rs, double spin) {
  const auto m = Evaluate(ray.position, rs, spin);
  const double s = glm::dot(m.l, ray.momentum) - ray.timeMomentum;
  return .5 * (glm::dot(ray.momentum, ray.momentum) -
               ray.timeMomentum * ray.timeMomentum) -
         m.h * s * s;
}
KerrSpacetime::Ray KerrSpacetime::ObserverPastRay(const glm::dvec3 &x,
                                                  const glm::dvec3 &direction,
                                                  double rs, double spin,
                                                  const glm::dvec3 &beta) {
  const double squared = glm::dot(beta, beta);
  if (!std::isfinite(squared) || squared >= 1)
    throw std::invalid_argument("Observer velocity must be subluminal");
  if (squared < 1e-24)
    return PastRay(x, direction, rs, spin);
  const auto n = glm::normalize(direction);
  const double gamma = 1 / std::sqrt(1 - squared), along = glm::dot(beta, n);
  const double energy = gamma * (1 - along);
  const auto localMomentum = n + beta * ((gamma - 1) * along / squared - gamma);
  auto ray = PastRay(x, localMomentum / energy, rs, spin);
  ray.momentum *= energy;
  ray.timeMomentum *= energy;
  return ray;
}
KerrSpacetime::Observer KerrSpacetime::MakeObserver(const glm::dvec3 &x,
                                                    double rs, double spin,
                                                    const glm::dvec3 &beta) {
  Horizon(rs, spin);
  const double squared = glm::dot(beta, beta);
  if (!std::isfinite(squared) || squared >= 1)
    throw std::invalid_argument("Observer velocity must be subluminal");
  const auto m = Evaluate(x, rs, spin);
  const double scale = std::sqrt(1 + 2 * m.h),
               gamma = 1 / std::sqrt(1 - squared), q = glm::dot(m.l, beta);
  return {{x, gamma * (beta + m.l * (q * (scale - 1))),
           gamma * (-1 + 2 * m.h * q) / scale},
          0,
          0};
}
KerrSpacetime::Observer
KerrSpacetime::MakeStationaryObserver(const glm::dvec3 &x, double rs,
                                      double spin) {
  const auto m = Evaluate(x, rs, spin);
  // Coordinate-stationary observers are impossible inside the ergosphere.
  // There, start in the timelike slice-normal frame instead.
  return MakeObserver(x, rs, spin,
                      2 * m.h < .999 ? m.l * (2 * m.h) : glm::dvec3(0));
}
double KerrSpacetime::ObserverTimeComponent(const Observer &observer, double rs,
                                            double spin) {
  const auto &ray = observer.state;
  const auto m = Evaluate(ray.position, rs, spin);
  return -ray.timeMomentum +
         2 * m.h * (glm::dot(m.l, ray.momentum) - ray.timeMomentum);
}
glm::dvec3 KerrSpacetime::ObserverBeta(const Observer &observer, double rs,
                                       double spin) {
  const auto &ray = observer.state;
  const auto m = Evaluate(ray.position, rs, spin);
  const double scale = std::sqrt(1 + 2 * m.h), q = glm::dot(m.l, ray.momentum);
  const double gamma = -scale * ray.timeMomentum + 2 * m.h * q / scale;
  return (ray.momentum - m.l * (q * (1 - 1 / scale))) / gamma;
}
glm::dvec3 KerrSpacetime::ObserverCoordinateVelocity(const Observer &observer,
                                                     double rs, double spin) {
  return Derivative(observer.state, rs, spin).first /
         ObserverTimeComponent(observer, rs, spin);
}
double KerrSpacetime::ReceivedEnergy(const Ray &photon,
                                     const Observer &observer, double rs,
                                     double spin) {
  return photon.timeMomentum * ObserverTimeComponent(observer, rs, spin) +
         glm::dot(photon.momentum, Derivative(observer.state, rs, spin).first);
}
void KerrSpacetime::AdvanceObserver(Observer &observer, double rs, double spin,
                                    double seconds, double c) {
  if (seconds < 0 || c <= 0 || !std::isfinite(seconds))
    throw std::invalid_argument("Invalid observer integration step");
  const double step = seconds * c;
  const auto start = observer.state;
  const auto k1 = Derivative(start, rs, spin);
  const Ray second{start.position + k1.first * (step * .5),
                   start.momentum + k1.second * (step * .5),
                   start.timeMomentum};
  const auto k2 = Derivative(second, rs, spin);
  const Ray third{start.position + k2.first * (step * .5),
                  start.momentum + k2.second * (step * .5), start.timeMomentum};
  const auto k3 = Derivative(third, rs, spin);
  const Ray fourth{start.position + k3.first * step,
                   start.momentum + k3.second * step, start.timeMomentum};
  const auto k4 = Derivative(fourth, rs, spin);
  auto time = [&](const Ray &state) {
    return ObserverTimeComponent({state}, rs, spin);
  };
  observer.coordinateSeconds +=
      seconds *
      (time(start) + 2 * time(second) + 2 * time(third) + time(fourth)) / 6;
  observer.properSeconds += seconds;
  observer.state.position +=
      step * (k1.first + 2.0 * k2.first + 2.0 * k3.first + k4.first) / 6.0;
  observer.state.momentum +=
      step * (k1.second + 2.0 * k2.second + 2.0 * k3.second + k4.second) / 6.0;
}
void KerrSpacetime::AdvanceRay(Ray &ray, double rs, double spin, double step) {
  const auto offset = [&](const Ray &state, const auto &k, double dt) {
    return Ray{state.position + k.first * dt, state.momentum + k.second * dt,
               state.timeMomentum};
  };
  const auto k1 = Derivative(ray, rs, spin),
             k2 = Derivative(offset(ray, k1, step * .5), rs, spin);
  const auto k3 = Derivative(offset(ray, k2, step * .5), rs, spin),
             k4 = Derivative(offset(ray, k3, step), rs, spin);
  ray.position +=
      step * (k1.first + 2.0 * k2.first + 2.0 * k3.first + k4.first) / 6.0;
  ray.momentum +=
      step * (k1.second + 2.0 * k2.second + 2.0 * k3.second + k4.second) / 6.0;
}
const char *KerrSpacetime::ShaderFunctions() {
  return R"(
float kerrRadius(vec3 x,float a){
    float q=dot(x,x)-a*a;
    return sqrt(max(1e-12,.5*(q+sqrt(q*q+4.0*a*a*x.z*x.z))));
}
void kerrMetric(vec3 x,float rs,float spin,out float r,out float h,out vec3 l){
    float a=.5*rs*spin;r=kerrRadius(x,a);
    float r2=r*r,b=r2+a*a,d=r2*r2+a*a*x.z*x.z;
    h=.5*rs*r*r2/d;l=vec3((r*x.x+a*x.y)/b,(r*x.y-a*x.x)/b,x.z/r);
}
void kerrPastRay(vec3 x,vec3 n,float rs,float spin,out vec3 p,out float pt){
    float r,h;vec3 l;kerrMetric(x,rs,spin,r,h,l);
    float q=dot(n,l),scale=sqrt(1.0+2.0*h);
    p=n+l*(q*(scale-1.0));pt=(1.0+2.0*h*q)/scale;
}
void kerrObserverPastRay(vec3 x,vec3 n,float rs,float spin,vec3 beta,out vec3 p,out float pt){
    float squared=dot(beta,beta);
    if(squared<1e-12){kerrPastRay(x,n,rs,spin,p,pt);return;}
    float gamma=1.0/sqrt(max(1e-7,1.0-squared)),along=dot(beta,n);
    float energy=gamma*(1.0-along);
    vec3 localMomentum=n+beta*((gamma-1.0)*along/squared-gamma);
    kerrPastRay(x,localMomentum/energy,rs,spin,p,pt);p*=energy;pt*=energy;
}
void kerrDerivative(vec3 x,vec3 p,float pt,float rs,float spin,out vec3 dx,out vec3 dp){
    float r,h;vec3 l;kerrMetric(x,rs,spin,r,h,l);
    float a=.5*rs*spin,r2=r*r,b=r2+a*a,d=r2*r2+a*a*x.z*x.z;
    vec3 gradR=(r*x+vec3(0,0,a*a*x.z/r))/max(2.0*r2-dot(x,x)+a*a,1e-8);
    vec3 gradH=h*((3.0/r-4.0*r*r2/d)*gradR-vec3(0,0,2.0*a*a*x.z/d));
    float u=x.x*p.x+x.y*p.y,v=x.y*p.x-x.x*p.y;
    float coefficient=(u*b-2.0*r*(r*u+a*v))/(b*b)-x.z*p.z/r2;
    vec3 gradLp=coefficient*gradR+vec3((r*p.x-a*p.y)/b,(r*p.y+a*p.x)/b,p.z/r);
    float s=dot(l,p)-pt;dx=p-2.0*h*s*l;dp=gradH*s*s+2.0*h*s*gradLp;
}
float kerrDiskShift(float r,float rs,float spin,float pt,float angularMomentum,vec3 beta,vec3 direction){
    float m=.5*rs,a=m*spin,root=sqrt(m),r32=pow(r,1.5);
    float omega=root/(r32+a*root);
    float ut=(1.0+a*root/r32)/sqrt(max(1e-7,1.0-3.0*m/r+2.0*a*root/r32));
    float receiver=(1.0+dot(beta,direction))/sqrt(max(.0001,1.0-dot(beta,beta)));
    return clamp(receiver/max(.0001,ut*(pt+omega*angularMomentum)),.02,12.0);
}
)";
}
