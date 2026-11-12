# Star systems and direct selection — November 12, 2026

Click a visible catalogue star with mouse capture released to select it. Its information appears in the right panel and a green marker identifies the destination. **G** visits the star's generated system. Selecting alone does not move or aim the camera. This now works while viewing the Solar System or another generated system, as well as in the Gargantua scene.

## Every change and reason

| Files | Change and reason |
| --- | --- |
| `src/UniverseCatalog.h/.cpp` | Added local-disk, thin-disk, logarithmic spiral-arm, bulge and halo populations. Disk radii follow an exponential surface-density distribution and heights decline exponentially. Bulge/halo components occupy a bounded spheroid. This replaces a nearly uniform local volume plus repeated spiral points. |
| Same catalogue | Preserve 1,024 nearby survey representatives and fill the rest of the Milky Way sample with galactic populations. Widen the galaxy's bounding envelope to include a stellar halo; keep the disk itself thin. Every generated position remains inside its galaxy. |
| `src/StarSystem.cpp`: host properties | Couple illustrative main-sequence populations to galactic environment: old low-mass stars in bulges/halos and a young, hotter component in arms. Stellar ages stay below the assumed main-sequence lifetime. Spectral subtype now follows temperature rather than catalogue-index modulo ten. |
| Same file: planets | Choose compact rocky, broad, giant-rich and mixed architectures before generating bodies. Planet classes depend on a luminosity-scaled frost line and migration probabilities; first/last bodies no longer have forced types. Correlate mass and radius, set atmospheric retention from approximate escape speed and temperature, and synchronize close-in rotators. These are bounded illustrative distributions, not a calibrated planet-formation solver. |
| Same file: orbit spacing | Reject radial orbit overlap and enforce an eight-mutual-Hill-radius spacing approximation for primary planets. This improves initial separation but does not establish long-term N-body stability. |
| Same file: moons | Generate rocky/icy moons after primaries so hierarchy caching remains valid. Keep them outside a conservative Roche margin and within 22% of the pericentre Hill radius. Give them deterministic surfaces, masses, sizes and synchronized rotation. |
| `src/BlackHole.h/.cpp` | Add catalogue-only picking over cached double-precision positions. A click no longer regenerates 7,000 stellar descriptions or evaluates inactive planet orbits. Add a selection marker for an external star while inside another system. Raise the existing artistic navigation brightness floor so unresolved stars are easier to see. |
| `src/StarSystem.h/.cpp` | Picking returns whether a local body was hit. It preserves the existing selection when empty sky is clicked and reports a true hit to the caller. Local planets/star take priority over background stars. Hide the old host marker while an external system is selected. |
| `src/WindowManager.h/.cpp` | Share the screen-coordinate selection routine with the actual mouse handler and checks. Convert a six-pixel click radius through FOV, retain lens-coordinate mapping, query the galaxy catalogue on a local miss, and preserve select-then-G behavior. Render the external marker with a camera-relative projection. |
| `tests/SimulationChecks.cpp` | Survey 1,024 systems for deterministic properties, varied primary ordering, valid finite bodies, noncrossing primary orbits and bounded moon orbits. Existing galaxy containment, surface precision, clocks and vehicle checks remain. |
| `src/RenderChecks.cpp` | Test click-coordinate selection from the Solar System, unchanged camera on selection, G travel, and identical planet seeds on revisit. Add the generator survey before GPU captures, so an unusual seed is not missed by a handful of showcase views. |

## Assertion fix

The first survey exposed a new moon-size range error: `Random(0.10, min(0.72, parentRadius * 0.27))` reverses its bounds for small rocky parents. Moon minimum size now scales below its maximum. `Random` validates all ranges and throws a descriptive error before calling the C++ distribution. The automated CPU checks catch exceptions and write CRT assertions to stderr instead of opening blocking debug dialogs. Render-check prints failure source locations, checks capture readback/file writes, and ends with an explicit PASSED/FAILED summary.

The corrected survey passed: 259 primary layouts, 406 giant-first systems and 3,769 moons in the final 1,024-seed run. Re-run the checks after generation changes; population changes can change these counts.

## Scale and limitations

This remains a **7,000-star representative catalogue across four bounded galaxies**, not SpaceEngine's hundreds of billions of on-demand octree stars. Distant points are uploaded once, rendered on the GPU, and have no CPU orbital simulation. Camera proximity activates only the visited system; returning to an ID regenerates its seeded planets. Generation changes intentionally alter some old procedural locations and systems; this is generation revision 2, world seed 618.

Click picking uses projected geometric positions with the selected camera lens mapping. It does not yet invert strongly bent Kerr secondary images or diffraction/flare ghosts. Opaque current-system bodies take priority. Remnants, binary-star formation, chemical evolution and full N-body system evolution are not modeled by this update.

The general design follows SpaceEngine's public descriptions of [seeded galaxy populations](https://spaceengine.org/news/blog100201/) and its [universe-generation diversity update](https://spaceengine.org/news/blog251118). No private SpaceEngine code, models or assets were copied.
