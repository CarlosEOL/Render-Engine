# Milky Way, navigation and new browsers — November 12, 2026

The Milky Way is now an observation-constrained structural illustration, with a Galactic plane oriented relative to the engine's ecliptic axes. It is not an external photograph of our galaxy, a complete Gaia catalogue, or a copy of SpaceEngine's implementation.

## Every changed component and why

| Files | Change | Reason |
| --- | --- | --- |
| `src/MilkyWayModel.h/.cpp` | Shared Galactic frame, disk, young arm populations, Orion spur, bar, bulge, halo, warp and dust density functions. | Replace an arbitrary local galaxy orientation with a documented physical coordinate frame and recognizable large-scale structure. CPU sampling and GPU sky use the same structural assumptions. |
| `src/MilkyWaySky.h/.cpp` | Cached GPU integration of stellar emission and wavelength-dependent dust extinction. Bounded sky texture, 96 galaxy samples, no rebake for rotation. Rebuild after meaningful translation, at most four times per second. | Avoid tracing the galaxy volume every displayed frame. CPU work is coordinate transforms and cache decisions, rather than stellar orbit integration. |
| `src/UniverseCatalog.h/.cpp` | Increase the representative catalogue from 7,000 to 16,000, adding 9,000 Milky Way representatives. Use isotropic local sampling and the new bar/arm/disk distribution. Build an immutable nearest-star k-d tree alongside the existing local spatial hash. Share generated display names. | Improve Galactic coverage and replace a repeated full-catalogue nearest-star scan with a spatial query. The sparse catalogue is still not a full stellar census. |
| `src/NavigationCatalog.h/.cpp` | Stable object keys, parent keys, named root systems, on-demand generated planets/moons, 4,096 individually named representative small bodies, mass-weighted satellite barycentres, galaxies, cores and nebulae. Separate anchor and local coordinates in distance calculations. | Make exact object names searchable without continuously simulating every system or flattening metre-scale offsets into cosmological coordinates. |
| `src/NavigationPanel.h/.cpp` | F3 name finder with Object/Parent/Type; Shift+F3 distance-sorted nearby-system search with a user radius; F1 icon overview with Back, Open satellites and Go to. Validate finite positive radii. Limit broad result lists to 1,000 rows and galaxy overviews to 500 entries. | Provide distinct navigation tasks while preserving click-to-select followed by explicit travel. Double-click in the overview opens a hierarchy; it does not silently move the camera. Full names remain available through the finder. |
| `src/InterstellarWorlds.h/.cpp` | Share the authored companion-star orbital elements between the renderer and navigation metadata. | Keep search distances and parent views consistent with the moving companions. |
| `src/FlightUnits.h` | Shared gradual wheel stepping and labels in m/s, km/s, c, AU/s and ly/s, using double precision. | Remove inconsistent units and float truncation between the title, toast and different scene modes. |
| `src/WindowManager.h/.cpp` | Wire the browsers, navigation actions, nearby reference selection, dormant-system frame rebasing, core/nebula travel and new key assignments. Near a planet, navigation distances use that planet's local position. During interstellar flight, use a nearby star and rebase with hysteresis. | Maintain local precision, stable selection and free flight while changing spatial reference. Distant planetary orbit evaluations remain disabled. |
| `src/ControlsPanel.h/.cpp` | Separate F9 controls window; move shortcut text out of star, black-hole and quantum information. | Keep astronomical information readable. The information panel now uses I because the newer request reserves F3 for the finder. |
| `src/StarSystem.h/.cpp`, `src/SolarSystem.cpp` | Add core-only system metadata and camera framing; expose the existing small-body orbital elements for explicit queries. | Reuse local coordinates and navigation without attaching random planets or stellar surface shaders to a black hole. |
| `src/ScenePanel.h/.cpp` | Compact magnitude readout and three controls above the clock. | Keep the active limiting magnitude visible without adding keybind text to object information. |
| `Render-Engine.vcxproj`, `Render-Engine.DX12.vcxproj` | Register shared files and copy new PNG assets beside both executables. | Keep OpenGL and DX12 behavior and asset lookup consistent, including launches outside the repository directory. |
| `tests/SimulationChecks.cpp`, `tests/RunSimulationChecks.cmd` | Coordinate round trips, nearest-tree versus exhaustive search, local precision at huge anchors, findable unvisited planets/small bodies, parent/Back relationships, Jupiter's four modeled moons, radius validation and flight-unit checks. | Test behavior rather than just successful compilation. |
| `src/RenderChecks.cpp` | Native finder/nearby/satellite screenshots; ensure selection does not move the camera; new core, cloud, eclipse, nebula and magnitude captures. Update former F3 information checks to I. | Exercise the actual new controls and rendered output, including the revised exposure policy. |

## Controls

| Key | Action |
| --- | --- |
| F1 | System overview; open a planet to browse its modeled satellites. Back ascends to its parent. |
| Shift+F1 | Legacy wireframe toggle. |
| F3 | Object finder by name. |
| Shift+F3 | Nearby systems within a supplied radius in light-years. |
| I | Object information. |
| F4 / Shift+F4 | Existing all-system / current-system side browsers remain available. |
| F9 | Separate controls panel. |
| G / Go to | Travel to the selected object. |
| Wheel | Flight speed, 15% per notch. |
| Ctrl+Wheel / Ctrl+[ / Ctrl+] | Change stellar magnitude limit by 0.20. |

The overview shows the **modeled parent hierarchy**, not every body exerting a nonzero gravitational force. Satellite barycentres are mass-weighted display/navigation references; they do not turn the existing Keplerian solver into a mutually interacting N-body solver. Thumbnails are lightweight illustrative icons. The redundant Select button was removed; a single item click selects without moving the camera.

The name finder matches case-insensitively and ignores punctuation. Full generated planet/moon names expand their parent system on demand. A generic query such as “moon” lists authored matches; it does not instantiate every unvisited system to build a global wildcard index. Small bodies use stable names `Solar asteroid 0001` through `Solar asteroid 4096`; these are representative seeded bodies, not identifications of real catalogued asteroids. Saturn's visual ring particles are not separate catalogue objects.

## Structural parameters and sources

| Parameter | Adopted value and boundary |
| --- | --- |
| Solar Galactocentric radius | 8.277 kpc; Sun idealized in the Galactic midplane. |
| Sagittarius A* mass | 4.30 million solar masses. |
| Stellar bar half-length / angle | 5 kpc / 30 degrees. |
| Thin-disk radial scale | 2.6 kpc. |
| Displayed disk / halo envelope | 16 kpc / 50 kpc radii; convenient illustrative bounds, not precise physical edges. |
| Spiral arms | Four logarithmic loci, two stronger stellar arms, weaker other arms and a local spur. Pitch 12.8 degrees, widths and contrast are illustrative choices. |
| Warp, scale heights, clumping, colors | Explicit approximate rendering/population prescriptions; not fitted dust tomography or calibrated surface photometry. |

The coordinate basis uses the [ESA Gaia ICRS-to-Galactic matrix](https://gea.esac.esa.int/archive/documentation/GDR1/Data_processing/chap_cu3ast/sec_cu3ast_intro.html). The real Milky Way is represented from within; the [ESA Gaia map](https://www.esa.int/media/milky-way-map-by-gaia-labelled) is a useful structural guide, not an actual outside photograph. Adopted central distance/mass follow [GRAVITY's Galactic-centre analysis](https://arxiv.org/abs/2112.07478) and [ESO's explanation](https://www.hq.eso.org/public/news/eso2119/). Bar dimensions follow [Wegg, Gerhard and Portail](https://arxiv.org/abs/1504.01401). The [disk scale-length study](https://academic.oup.com/mnras/article/509/2/2339/6380515) motivates the radial scale; [NASA's spiral-structure illustration](https://science.nasa.gov/resource/the-milky-way-galaxy/) explains the main stellar arms and local spur.

### Catalogue compatibility

This is spatial catalogue version 3. The same seed and ID reproduce the same result **within this version**. The new Galactic frame and local sampling move some version-2 star positions; planetary seeds and external-galaxy IDs retain their earlier scheme. This is not a promise of identical coordinates across generator algorithm upgrades. Unobserved synthetic entries must not be presented as measured real stars.
