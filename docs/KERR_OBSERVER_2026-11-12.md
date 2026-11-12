# Continuous black-hole observer — November 12, 2026

The **H / Gravity** camera now follows a timelike geodesic in the same Cartesian Kerr–Schild metric as the GPU photon tracer. Crossing the outer event horizon neither blanks the screen nor artificially increases a lens-strength slider. Exterior light can still reach an infalling observer.

## Changes and reasons

| Files | Change and reason |
| --- | --- |
| `src/KerrSpacetime.h/.cpp` | Added a massive observer with covariant momentum and proper/coordinate clocks. RK4 advances Hamilton's equations, `dx^mu/dtau = g^{mu nu} p_nu`, `dp_mu/dtau = -0.5 partial_mu(g^{alpha beta}) p_alpha p_beta`, with mass shell `g^{mu nu}p_mu p_nu = -1`. Kerr–Schild coordinates remain regular at the outer horizon. |
| Same metric helpers | Construct a stationary observer where possible and a timelike slice-normal observer in the ergosphere; transform physical local velocity and launch past-directed null rays using the observer's local Lorentz frame. Prevents applying a stationary-observer redshift inside a region where stationary observers cannot exist. |
| `src/WindowManager.h/.cpp` | H initializes and advances this observer with adaptive bounded work, retaining queued proper time. Local time follows the observer's proper time; playing world time receives the corresponding regular coordinate-time increment. Pausing the world still permits an H plunge, matching the existing control behavior. Home resets the observer. |
| `src/BlackHole.h/.cpp` | Physical observers use Kerr photon integration even at zero spin (the Schwarzschild limit). Removed the exterior capture band at 1.015 horizon radii; capture now uses the actual horizon, and interior past rays may reach exterior emitters. Removed legacy impact/away-ray culls for a physical observer. |
| Same shader | Observer-frame rays have unit received frequency. Distant stationary-source shift is `g = 1 / p_t` for the past-directed convention; specific intensity uses the invariant `I_nu / nu^3`. Avoids counting the observer's Doppler shift twice. Disk/plasma also use cubic specific-intensity beaming in physical-observer mode. |
| Same shader | Trace to an exit beyond the luminous disk, stop only at the modeled deep-interior boundary, and bound radiance to finite half-float storage. This is a storage bound, not a blackout at the outer horizon. |
| `tests/KerrObserverChecks.cpp`, `tests/kerr/CMakeLists.txt`, `tests/RunKerrObserverChecks.cmd` | A graphics-independent test crosses the outer horizon at spins 0, 0.8, 0.98 and -0.8; verifies the massive/null constraints, conserved energy, finite clocks, timelike velocity and received frequency. An interior outward past ray must connect to exterior space. |
| `src/RenderChecks.cpp` | Captures either side of the horizon and checks finite, nonempty received light and continuity, plus the actual H-control plunge. |

## Accuracy boundaries

This models a test observer and photon propagation in a fixed central Kerr spacetime. It does not solve the coupled spacetime, tidal damage, a camera instrument's survival, or the unstable inner horizon. The plunge stops at the larger of 0.03 Schwarzschild radii and 1.05 inner-horizon radii. No wormhole or continuation through a singularity is claimed.

Planets still use Keplerian test orbits. Endurance/Ranger trajectories and received finite-body history retain their documented Schwarzschild/direct-path approximations. Distant-sky spectra and radiance normalization are illustrative, and the camera model does not make every emitter a full covariant radiative-transfer solution. No finite plunge reveals an infinite future of the universe.

## Verification

The standalone observer check passed, with maximum mass-shell residual approximately `5.8e-13`. GPU horizon captures passed in OpenGL and native DX12; light remains visible just inside the outer horizon. Run `tests\RunKerrObserverChecks.cmd` and either application's `--render-check <directory>` for reproducible checks.

## Sources

- [GRay2: Cartesian Kerr–Schild ray integration](https://arxiv.org/abs/1706.07062).
- [JILA: the view near and inside a black hole](https://jila.colorado.edu/~ajsh/courses/insidebh/schw.html).
- [NASA black-hole questions](https://science.nasa.gov/apod/frequently-asked-questions-faqs-on-black-holes/).
