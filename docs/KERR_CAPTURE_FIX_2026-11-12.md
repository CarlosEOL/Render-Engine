# Kerr shadow and false-image fix — November 12, 2026

The repeated miniature images inside Gargantua were a rendering defect already present in earlier saved captures. The brighter Milky Way made it easier to see. Earlier render checks verified finite output, source changes and capture creation, but did not verify the shadow's capture/escape geometry; those checks were insufficient to catch this defect.

## Changes and reasons

| File | Change | Reason |
| --- | --- | --- |
| `src/BlackHole.cpp` | Track unresolved, captured, disk-hit and escaped rays separately. Only genuinely escaped rays sample the background. Nonfinite integration states are rejected. | The old path treated exhausting the ray budget as reaching infinity, displaying background light through the capture region and generating false nested images. |
| `src/KerrSpacetime.h/.cpp`, `src/BlackHole.cpp` | Add the innermost prograde spherical-photon-orbit radius. Incoming exterior rays may terminate just inside that bound, below all exterior scattering turning points, instead of integrating nearly divergent past-directed Kerr–Schild momenta all the way to the horizon. | The old test required a ray to cross below the horizon; past-directed canonical momenta stalled and accumulated large numerical errors first. This is an exterior photon-termination optimization, not a change to the event horizon, observer position, clocks or timelike equations. |
| `src/BlackHole.cpp` | Preserve actual-horizon handling near/interior observers and where the existing scene depth indicates a nearby finite emitter. | Avoid applying the empty-inner-region shortcut to the camera's plunge or nearby spacecraft/geometry. Finite-object lensing remains the existing screen-depth approximation. |
| Same renderer | Replace the plasma plane's tiny approach steps with an analytic Gaussian-slab average per ray segment; use proportional weak-field steps and bounded strong-field RK steps. Initialize the first segment tangent from the Kerr derivative. | Plane-distance steps consumed the budget without resolving a ray's fate. Keep plasma contribution while spending steps on geometry, and keep the first curved segment consistent with its Kerr initial momentum. |
| Same renderer | Reserve up to 192 extra steps only for spinning rays that exhaust their base budget inside the disk's inner edge. The hard maximum remains 384. | Near-extremal spin needs additional affine integration; ordinary escaped rays do not pay the reserve cost. Some unresolved rays near the critical curve at lower quality remain conservatively dark, rather than displaying a fake sky. |
| Same renderer | Split the expanded shader into bounded string pieces. | Keep compilation within MSVC's per-literal size limit. |
| Same renderer | Locate disk-plane intersections on a cubic Hermite interpolation of each RK segment with 12 bisections. | A straight chord produced discontinuous crossing radii and jagged disk edges as neighbouring pixels took different step counts. |
| Same renderer | Fade the outer background deflection into the original background before the existing weak-field cutoff. Replace high-frequency radial stripes with sheared multi-scale turbulent emission. | Remove the hard circular transition and the record-like concentric texture. Emission texture is artistic; null-ray propagation and source classification remain separate. |
| `src/BlackHole.h`, `src/BlackHole.cpp` | Add a test-only callable ray-status visualization: red capture, green disk, blue escape, magenta unresolved. | Make incorrect sky/capture decisions directly measurable. Normal startup leaves the diagnostic off; there is no per-frame CPU readback. |
| `tests/KerrCaptureReference.h`, `src/RenderChecks.cpp` | Compare sampled GPU outcomes with an independent double-precision Carter radial-potential calculation. Test spins +0.8, +0.98 and -0.8 at budgets 160 and 384; check the central shadow and exclude a narrow critical-potential ambiguity band. | Verify actual capture geometry instead of accepting any nonblack picture. The reference minimizes a radial quartic rather than copying RK integration or the GPU stop condition. |
| `src/main.cpp`, `src/WindowManager.h`, `src/RenderChecks.cpp` | Add `--lensing-check DIR`; include its 13 scene views in the full render suite. | Permit focused diagnosis and retain the new checks in broad regressions. |
| `tests/KerrObserverChecks.cpp` | Check the photon bound lies between the horizon and disk ISCO and reduces to 3M for zero spin; retain clock, null-energy and continuous horizon-crossing checks. | Protect the physical observer from a rendering-only correction. |
| `Render-Engine.vcxproj` | Copy the existing Assimp runtime DLL alongside a fresh OpenGL build. | A clean isolated output folder could not start because its required runtime DLL was missing. |
| README and screenshots | Show a corrected engine capture and link this change log. | Make the repair reviewable and record why the earlier passing smoke tests were insufficient. |

## Equations and limits

The minimum spherical-photon radius is `r_ph,min = Rs * [1 + cos((2/3) acos(-abs(spin)))]`. The shader's empty-region stop is 0.99 of this radius when the observer is sufficiently outside it, never below the actual horizon. The independent oracle uses `R(r) = E² r⁴ + (a²E² - Lz² - Q) r² + Rs [Q + (Lz - aE)²] r - a²Q` and finds radial-potential minima above the horizon.

[Edward Teo's primary paper and orbit discussion](https://phyweb.physics.nus.edu.sg/~phyteoe/kerr/) give the Kerr spherical-photon region and equatorial limits. The termination shortcut assumes no unresolved emitting material inside the stopping region; nearby finite geometry uses the existing depth guard. The camera's interior view is not globally blacked out. Existing finite-body light-delay, screen-depth lensing, three-band colour and plasma texture approximations remain.

## Build environment recovery

The shared `External_Lib/include` folder was missing when compilation began. Headers were restored without switching the user's branch or replacing existing library binaries: GLM 1.0.1, GLFW 3.4, GLAD 0.1.36 (OpenGL 4.6 compatibility, no extensions), Assimp 5.4.0 matching the installed binary's C API/version/float ABI, TinyObjLoader 1.0.6 matching the stream API, and upstream stb image headers. Versions and SHA-256 values are recorded in `External_Lib/RESTORED_HEADERS_2026-11-12.md`. Temporary header-generation tools and ray probes stay in ignored build folders. An isolated checkout was used while the active workspace was switched between main and DX12.

## Validation

- Final native DX12 Debug: **153 scene captures + 11 UI captures**, no failed assertion locations, **zero DX12 validation errors**.
- Final pure OpenGL Debug: **144 scene captures + 11 UI captures**, no failed assertions.
- Final packaged DX12 Release, launched from its own directory: **13 lensing scenes + 10 UI captures**, no failed assertions and **zero DX12 validation errors**.
- The independent radial-potential comparison reported **zero mismatches among resolved sampled rays** at spins +0.8, +0.98 and -0.8 for both 160 and 384 base steps, away from the excluded critical-potential band. The high-quality runs had no unresolved sampled rays. At normal quality a few critical-region samples may remain conservatively dark; they are never filled with a fabricated escaped sky.
- Observer tests passed at spins 0, +0.8, +0.98 and -0.8, including continuous horizon crossing, finite clocks, unit received photon energy, and null/mass-shell checks. The largest measured mass-shell residual was below 6e-13.
- Atmosphere, exposure, lens-source classification, vehicle/interior, DLSS and prior navigation checks passed in the full suites. Existing compiler size-conversion/CRT warnings remain.

The stable build folder is `build/kerr-capture-fix`. Final logs are `logs/dx12-render.log`, `logs/opengl-render.log`, `logs/release-lensing.log` and `logs/kerr-observer-checks.log`. PNGs preserve the actual engine output; duplicate PPMs are removed only after checking their PNG counterparts. The corrected screenshot in README comes from the final native run.

These comparisons test the exercised cameras and sampled rays. They are not a claim of exact astronomical imaging or complete validation of every near-critical trajectory. Plasma/outflow emission and finite-object transport retain the existing approximations; inner emission omitted by an empty-region stop is a remaining source-model limitation.
