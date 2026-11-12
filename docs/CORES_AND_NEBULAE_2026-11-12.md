# Galactic cores, TON 618 and nebulae — November 12, 2026

## Every change and why

| Files | Change and reason |
| --- | --- |
| `src/GalacticCore.h/.cpp` | Named Sagittarius A*, three generated galaxy-centre black holes and TON 618. Derive Schwarzschild radii from mass. Put the generated cores at their actual seeded galaxy centres and Sagittarius A* at the adopted Galactic centre. |
| Same core module | TON 618 uses its SIMBAD sky direction, redshift 2.219 and an adopted uncertain virial mass of 4.07e10 solar masses. Integrate a flat Planck-2018 LCDM radial comoving distance once and cache it. Distinguish that distance from lookback time and luminosity distance. |
| `src/BlackHole.h/.cpp` | Render a core-only background and Kerr-lensed accretion field using normalized gravitational units, while keeping the camera's physical AU offsets separate. Scale simulation time with mass. Rebuild the off-screen sky cube at a new core's world origin; reuse its allocation and invalidate it when magnitude visibility changes. Never attach Miller/Mann/Endurance geometry to a real galactic core. |
| Same renderer | Add an explicitly artistic TON 618 quasar preset: a larger luminous disk and polar outflow inspired by the supplied image. Retain spin-dependent lensing. Assumed spin, disk temperature/orientation, brightness and outflow structure are not measurements of TON 618. |
| `src/CelestialPhenomena.h/.cpp` | Add named Orion, Helix, Crab and Veil objects with approximate public catalogue centres, distances, sizes and distinct phenomenon types. Keep the model metadata separate from the sky shader. |
| `src/MilkyWaySky.cpp` | Integrate original coherent emission-cloud, shell and filament volumes into the bounded sky cache. Use camera-relative normalized nebula coordinates before converting to float, and shorten translation invalidation thresholds nearby. | 
| `src/NavigationCatalog.cpp`, `src/WindowManager.cpp` | Finder entries and explicit Go to for nebulae, named cores and galaxy overviews. Free flight continues within/around the nebular volume. Information distinguishes measured context from illustrated structure. |

## Model boundaries and sources

Sagittarius A* is quiescent: its faint optical accretion glow in this renderer is an illustrative visibility aid, not an observed optical photograph. Its adopted mass/distance follow [GRAVITY](https://arxiv.org/abs/2112.07478). The three other galaxy/core systems are fictional seeded examples.

TON 618's direction follows [SIMBAD](https://simbad.cds.unistra.fr/simbad/sim-basic?Ident=TON+618). The adopted mass is one published virial estimate; estimates in the literature differ substantially, so it must not be labelled an exact measurement. The source's measured redshift is distinct from a travel distance. Cosmological conversion adopts [Planck 2018](https://arxiv.org/abs/1807.06209): H0=67.4 km/s/Mpc, Omega_m=0.315 and flat Omega_Lambda=0.685. The engine remains a static spatial visualization; it does not propagate the entire expanding universe or render a time-evolving past light cone during a warp.

Core free flight stays outside the reference radius, using an approximate static clock display. The existing explicit timelike Kerr free-fall observer remains the authored Gargantua feature; the new core navigation preset is not a newly validated full interior observer implementation.

Nebular shapes are original illustrative fields, not reconstructions of NASA telescope datasets or measured hydrodynamic expansion. Gas emission colors are enhanced for visibility. The cache integrates self-attenuation but does not solve full inter-object radiative transfer or correctly extinguish every individual foreground/background point star. Very small distant nebulae are limited by the sky-cache resolution; travelling closer reveals spatial structure. No per-frame CPU gas/stellar orbit simulation is introduced.

| Object | Adopted approximate distance / radius | Primary context |
| --- | --- | --- |
| Orion / M42 | 1,350 ly / 12 ly | [Herschel mission overview](https://herscheltelescope.org.uk/results/orion-nebula/); published distance estimates vary. |
| Helix / NGC 7293 | 650 ly / 1.5 ly | [NASA Caldwell 63](https://science.nasa.gov/mission/hubble/science/explore-the-night-sky/hubble-caldwell-catalog/caldwell-63/), [SIMBAD centre](https://simbad.cds.unistra.fr/simbad/sim-id?Ident=NGC7293). |
| Crab / M1 | 6,500 ly / 3 ly | [NASA Messier 1](https://science.nasa.gov/mission/hubble/science/explore-the-night-sky/hubble-messier-catalog/messier-1/). |
| Veil / Cygnus Loop | 2,100 ly / 55 ly | [NASA observing planner](https://science.nasa.gov/wp-content/uploads/2023/10/2022_NASA_Science_Planner_508_0-1.pdf), [Cambridge remnant catalogue](https://www.mrao.cam.ac.uk/surveys/snrs/snrs.G74.0-8.5.html). Extent is an illustrative envelope. |
