# Saturn Kerr-like wormhole — November 12, 2026

Later cleanup removed the superseded Debug binaries and earlier capture folders. Both current Release builds include this feature; see [the latest build and validation record](ZOOM_AND_MILLION_SYSTEMS_2026-11-12.md). Earlier logs are retained in `build/history-logs`.

## Finding it

Open Planetorium, press **F3**, search **Saturn Kerr Wormhole** (or just **wormhole**), click to select, then **Go to** or **G** to approach. Selection alone leaves the camera in place. **F1** on Saturn shows Titan and the wormhole; Back from the wormhole returns to Saturn. **Shift+F** follows its orbit. **Space** pauses/resumes the same simulation clock as the planets. **Shift+F2** shows the object's classification and parameters.

The authored orbit has a 3,000,000 km semimajor axis, eccentricity 0.04 and inclination 14 degrees to the engine's ecliptic. Its period is computed from Saturn's existing mass, approximately 61 days. The throat's coordinate radius is 1,000 km; signed spin a/M is +0.8 and deformation lambda is 0.35. These are original model choices, not measured or film-specified orbital elements.

The film establishes a wormhole **near Saturn** without specifying these orbital elements. Its dialogue about a probe orbiting the wormhole does not establish the wormhole's own orbit. The [Nolan screenplay](https://8flix.com/assets/screenplays/i/tt0816692/Interstellar-2014-%5B11-24-2014%5D-screenplay.pdf) and the visual-effects team's [Visualizing Interstellar's Wormhole](https://arxiv.org/abs/1502.03809) describe that setting and a different wormhole model. Here, the Saturn-bound orbit implements the user's explicit request.

## Every change and its reason

| File | Change | Reason |
| --- | --- | --- |
| `src/WormholePhysics.h/.cpp` | Separate the orbital preset, optical metric normalization and GPU Hamiltonian equations from drawing code. | Keep the helper equations reusable and make the theoretical assumptions reviewable. |
| Same equations | Use a rotating Damour–Solodukhin throat and integrate backward null rays with RK4 on the GPU. Classify both exteriors and unresolved rays separately. | A rotating wormhole should show light arriving through a throat, rather than inherit a black hole's horizon capture mask. Exhausted rays must not fabricate escaped light. |
| Same equations | Integrate angular motion using a unit direction and canonical angular-momentum vector instead of polar angles. | The initial angular-coordinate version produced thin numerical spikes along the spin axis. The revised equations avoid that coordinate singularity. |
| `src/KerrWormhole.h/.cpp` | Add an independently managed fullscreen optical pass with scene/depth snapshots, frame dragging, smooth local matching, and a cached-sky view of the opposite exterior. | Give the throat a lensed spherical appearance without a terrain mesh, solid surface or artificial glowing accretion disk. |
| Same renderer | Preserve foreground depth; use homogeneous ray directions without dividing by far-plane w. | Saturn and other foreground bodies must hide the wormhole. Near/far ratios can make far-plane w round to zero for close cameras. |
| Same renderer | Cull offscreen/subpixel mouths, reuse color/depth textures, cap integration at 320 steps, and compute projected size from full matrix rows. | Avoid expensive work away from Saturn and keep visibility independent of camera roll or looking along the spin axis. |
| `src/SolarSystem.cpp` | Append the mouth as a zero-dynamical-mass child of Saturn. Reuse analytic Kepler positions and existing position caches. | It moves relative to Saturn when simulation time advances, follows Saturn around the Sun, and does not introduce a massive extra body or a new CPU N-body simulation. Existing object indices remain stable. |
| `src/StarSystem.h/.cpp` | Tag wormholes explicitly, initialize/release their renderer, skip planetary terrain and point-marker rendering, reject landing, and render after opaque bodies, rings and atmospheres but before the selection reticle. | Prevent a theoretical throat from acquiring planet textures or a landable surface; preserve selection and normal scene ordering. |
| Same camera code | Frame the mouth at 12 throat radii on Go to and keep free flight outside the throat's coordinate boundary. | The lensed sphere needs more framing space than a solid planet. This implementation is an exterior optical view, not camera transport between universes. |
| Same rotation/clock code | Treat the axisymmetric throat as stationary in orientation, including rotation-follow and its carried observer. | Kerr-like frame dragging is not the rotation of a material planetary surface; an arbitrary day length must not rotate the camera or add surface tangential velocity. |
| Same information code | Show throat radius, spin, deformation, Saturn-relative orbit and calculated period, with a theoretical classification and explicit optical-only scope. | Avoid presenting a temperature, Earth mass or natural-planet identity for the wormhole. |
| `src/BlackHole.h`, `src/WindowManager.cpp` | Share the existing cached sky cubemap with the Solar System's wormhole renderer. | Reuse stars/galaxy imagery without a second star catalogue or per-frame CPU star simulation. |
| `src/NavigationCatalog.h/.cpp` | Carry the wormhole tag through the existing searchable object hierarchy. | Make the object findable by name and browsable as a Saturn child; preserve click-to-select and explicit travel. The internal generic body entry remains compatible with current selection/follow handling. |
| `src/NavigationPanel.cpp` | Give the throat a distinct wire-sphere icon and theoretical-throat caption. | Distinguish it from planets and black holes in the overview. |
| `src/main.cpp`, `src/WindowManager.h`, `src/RenderChecks.cpp` | Add `--wormhole-check DIR` and include the same checks in `--render-check`. Check both ray exits, polar views, deterministic pause, foreground Saturn, distant Earth, orbit closure, selection, Go to, follow, landing rejection and name/parent lookup. Capture the finder and Saturn overview. | Test behavior and visible failure modes rather than merely checking that an image was produced. |
| `tests/SimulationChecks.cpp`, `tests/RunSimulationChecks.cmd` | Extend CPU catalogue/orbit tests for the extra object and compile the two new modules. | Keep the standalone test target in sync, including the updated catalogue count and parent relationship. |
| `Render-Engine.vcxproj` | Register both new modules and headers in the shared project imported by DX12. | Build the same feature from the existing shared scene sources. |
| README and `assets/screenshots/saturn-kerr-wormhole.png` | Add access instructions, this log and an actual engine capture. | Document every change and make the resulting appearance reviewable. |

## Optical equations and scope

The local geometry follows the rotating Damour–Solodukhin metric described in [Ertola Urtubey, Pérez and Romero, section 2](https://arxiv.org/html/2411.13474v1#S2). It retains Kerr's t/phi/angular metric terms while replacing the radial Delta with `DeltaHat = r² - 2 M (1+lambda²) r + a²`.

In units of the throat radius, choose `M = 1 / [(1+lambda²) + sqrt((1+lambda²)² - spin²)]` and `a = spin*M`, so the outer root of DeltaHat is 1. The regular coordinate `r = 1 + ell²/4` covers the two exteriors and gives `g^(ell,ell) = (r-a²)/Sigma`, where `Sigma = r²+a² n_z²`. This ell coordinate is regular at the throat; it is not asserted to be proper radial distance.

With a unit angular direction n, its canonical angular momentum L, radial momentum p_ell and conserved p_t, the shader evolves the Hamiltonian `H = N/(2 Sigma)`:

```
Delta = r² - 2 M r + a²
C = (r²+a²) p_t + a L_z
N = (r-a²) p_ell² + |L|² + 2 a p_t L_z
    + a² p_t² (1-n_z²) - C²/Delta
```

Rays start from a local past-directed ZAMO tetrad and terminate on an exterior sphere at radius at least 60 throat units. The exterior tangent, including transverse motion, determines the sampled sky direction. There is no black-hole event-horizon termination. Diagnostic green denotes the other exterior, blue the original exterior, and magenta an unresolved ray; normal unresolved pixels remain dark. Near-critical rays can exhaust the finite step budget.

The throat is a **local optical test geometry with a prescribed orbit**. Its geometric M is not inserted as a physical Solar System mass. This is not a self-consistent solution for Saturn plus a massive rotating wormhole, nor an exact recreation of Interstellar. Existing observer clocks continue to use the host-system model; wormhole clock transport and camera traversal are not implemented. The opposite exterior currently uses an authored rotation of the cached sky, not a simulated destination system. Finite scene objects use screen/depth reprojection and a smooth outer match, rather than full volumetric relativistic transport. No accretion plasma or gravitational backreaction is added.

## Verification

Native captures and logs are kept under `build/wormhole-full-checks`, `build/wormhole-release-checks`, `build/wormhole-full-render.log` and `build/wormhole-release-render.log`. Duplicate raw PPMs are removed after their PNG counterparts are verified. Earlier temporary probe captures are removed; the final images and logs remain.

- DX12 Debug full suite: **163 scene captures + 13 UI captures**, zero failed assertion locations and **zero DX12 validation errors**. Existing black-hole capture-reference, atmosphere, exposure, spacecraft, navigation and galaxy checks passed.
- Final DX12 Release, launched from `build/dx12/Release/bin`: **10 focused scene captures + 12 UI captures**, zero failed locations and **zero DX12 validation errors**. This includes the final stationary-orientation/rotation-follow guard, as well as selection, Go to, follow, orbit closure, hierarchy and landing rejection.
- Computed Saturn-relative period: **61.3562 days**. Both null-ray exits are present. The Release diagnostic sampled 235,907 opposite-exterior pixels, 783,496 original-exterior pixels and 3,381 unresolved pixels (about 0.16% of the frame). The polar view also passed; near-critical unresolved samples remain a finite-budget limitation.
- Foreground Saturn's tested central pixels were unchanged with the effect enabled. The tested distant Earth image was identical with the wormhole enabled/disabled. Repeated paused exterior images were identical.
- Both normal DX12 Debug and Release launchers were rebuilt. The README image is an actual final Release capture. No branch switch or publication was performed for this feature.
- The standalone CPU test sources compiled, but their executable launcher returned Windows “Access is denied” and no runnable executable remained; this is **not** reported as a passed standalone run. The orbit, hierarchy, selection, Go to, follow and landing assertions also executed successfully inside the native rendering test executable. No security setting was changed.

These checks validate the exercised views and behavior, not every null trajectory or a physically self-consistent wormhole/Saturn spacetime.
