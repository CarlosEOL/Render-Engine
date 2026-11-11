#include "Astronomy.h"
#include "StarSystem.h"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <random>

// Sources and accuracy boundaries are documented in docs/SIMULATION_CHANGES.md.
// J2000 elements are held fixed; this is a Keplerian model, not a live ephemeris.
void StarSystem::LoadSolarSystem()
{
    solarSystem = true;
    planets.clear();
    asteroids.clear();
    star = {0, "Sun / Solar System", "G2 V main-sequence star", 1, 1, 5772, 1, 4.57};
    struct Data
    {
        const char *name;
        double a, e, i, longitude, perihelion, node, radiusKm, massEarth, day, temperature;
        bool gas, atmosphere;
        glm::vec3 color;
    };
    const Data data[] = {
        {"Mercury",
         .38709927,
         .20563593,
         7.00497902,
         252.25032350,
         77.45779628,
         48.33076593,
         2439.4,
         .055274,
         58.6462,
         440,
         false,
         false,
         {.40, .35, .30}},
        {"Venus",
         .72333566,
         .00677672,
         3.39467605,
         181.97909950,
         131.60246718,
         76.67984255,
         6051.8,
         .815,
         -243.018,
         737,
         false,
         true,
         {.80, .61, .32}},
        {"Earth",
         1.00000261,
         .01671123,
         -.00001531,
         100.46457166,
         102.93768193,
         0,
         6371.0084,
         1,
         .99726968,
         288,
         false,
         true,
         {.19, .48, .25}},
        {"Mars",
         1.52371034,
         .09339410,
         1.84969142,
         -4.55343205,
         -23.94362959,
         49.55953891,
         3389.5,
         .10745,
         1.02595676,
         210,
         false,
         true,
         {.66, .26, .12}},
        {"Jupiter",
         5.202887,
         .04838624,
         1.30439695,
         34.39644051,
         14.72847983,
         100.47390909,
         69911,
         317.83,
         .41354,
         165,
         true,
         true,
         {.67, .49, .33}},
        {"Saturn",
         9.53667594,
         .05386179,
         2.48599187,
         49.95424423,
         92.59887831,
         113.66242448,
         58232,
         95.16,
         .44401,
         134,
         true,
         true,
         {.75, .65, .40}},
        {"Uranus",
         19.18916464,
         .04725744,
         .77263783,
         313.23810451,
         170.95427630,
         74.01692503,
         25362,
         14.536,
         -.71833,
         76,
         true,
         true,
         {.32, .68, .73}},
        {"Neptune",
         30.06992276,
         .00859048,
         1.77004347,
         -55.12002969,
         44.96476227,
         131.78422574,
         24622,
         17.147,
         .67125,
         72,
         true,
         true,
         {.12, .28, .74}},
        {"Pluto",
         39.482,
         .2488,
         17.14,
         238.9,
         224.1,
         110.3,
         1188.3,
         .00218,
         -6.3872,
         44,
         false,
         false,
         {.58, .43, .33}},
        {"Ceres", 2.7675, .0758, 10.59, 95, 73.6, 80.3, 469.7, .000157, .37809, 167, false, false, {.30, .29, .27}},
        {"Vesta", 2.361, .0887, 7.14, 32, 151, 103.8, 262.7, .0000432, .2226, 163, false, false, {.48, .43, .38}},
        {"Eris", 67.78, .44, 44.04, 35, 151.6, 35.9, 1163, .00278, 1.079, 30, false, false, {.77, .76, .73}},
        {"Halley (representative orbit)",
         17.834,
         .96714,
         162.26,
         120,
         169.9,
         58.42,
         5.5,
         3.7e-11,
         2.2,
         100,
         false,
         false,
         {.18, .17, .16}}};
    for (int index = 0; index < static_cast<int>(std::size(data)); ++index)
    {
        const auto &d = data[index];
        Planet body{};
        body.name = d.name;
        body.classification = index < 4     ? "Terrestrial planet"
                              : index < 6   ? "Gas giant"
                              : index < 8   ? "Ice giant"
                              : index == 10 ? "Asteroid"
                              : index == 12 ? "Comet nucleus"
                                            : "Dwarf planet";
        body.orbit = {d.a,
                      d.e,
                      glm::radians(d.i),
                      glm::radians(d.node),
                      glm::radians(d.longitude - d.perihelion),
                      glm::radians(d.perihelion - d.node)};
        body.radiusEarth = d.radiusKm / 6371.0;
        body.massEarth = d.massEarth;
        body.temperature = d.temperature;
        body.daySeconds = d.day * Astronomy::secondsPerDay;
        body.tilt = index == 2 ? .4091f : index == 6 ? 1.706f : .05f;
        body.seed = 100 + index * 719;
        body.seaLevel = index == 2 ? .03f : -1;
        body.gas = d.gas;
        body.atmosphere = d.atmosphere;
        body.color = d.color;
        planets.push_back(body);
    }
    const auto moon = [&](const char *name, int parent, double radiusKm, double massEarth, double axisKm,
                          double eccentricity, double periodDays, double phase) {
        Planet body{};
        body.name = name;
        body.classification = "Natural satellite (approximate two-body orbit)";
        body.orbit = {axisKm * 1000 / Astronomy::metresPerAu, eccentricity, .04, phase, phase};
        body.radiusEarth = radiusKm / 6371.0;
        body.massEarth = massEarth;
        body.daySeconds = periodDays * Astronomy::secondsPerDay;
        body.temperature = 100;
        body.seed = static_cast<unsigned int>(planets.size() * 617);
        body.seaLevel = -1;
        body.gas = false;
        body.atmosphere = false;
        body.color = {.47, .44, .39};
        body.parent = parent;
        planets.push_back(body);
    };
    moon("Moon", 2, 1737.4, .0123, 384400, .0549, 27.3217, 0);
    moon("Io", 4, 1821.6, .01495, 421700, .0041, 1.769, .3);
    moon("Europa", 4, 1560.8, .00804, 671100, .0094, 3.551, 1);
    moon("Ganymede", 4, 2634.1, .0248, 1070400, .0013, 7.155, 2);
    moon("Callisto", 4, 2410.3, .018, 1882700, .0074, 16.689, 3);
    moon("Titan", 5, 2574.7, .0225, 1221870, .0288, 15.945, 4);
    moon("Triton", 7, 1353.4, .00358, 354759, .000016, 5.877, 5);
    planets.back().orbit.inclination = glm::radians(156.8);
    // Representative populations, deliberately not named or advertised as surveyed asteroids.
    std::mt19937_64 random(0x501a2);
    const auto rand = [&](double low, double high) {
        return std::uniform_real_distribution<double>(low, high)(random);
    };
    for (int index = 0; index < 4096; ++index)
    {
        const bool kuiper = index >= 3072;
        asteroids.push_back({rand(kuiper ? 32 : 2.1, kuiper ? 48 : 3.3), rand(.01, kuiper ? .18 : .14), rand(-.18, .18),
                             rand(0, glm::two_pi<double>()), rand(0, glm::two_pi<double>())});
    }
    selected = 2;
    landed = following = false;
    lastTime = 0;
    camera = glm::dvec3(0, 35, 65);
    speedAu = 5;
}
