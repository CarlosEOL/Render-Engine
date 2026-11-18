# Andromeda nearest-star lookup performance

The severe slowdown when approaching Andromeda was caused by CPU spatial searches. The simulation, navigation reference and flare ownership each request the nearest star. Outside the four original catalogue populations, the old KD-tree search had only splitting-plane tests and could walk through large portions of the two-million-star catalogue. It also searched that far catalogue before using nearby streamed stars to narrow the search radius.

## Changes

- Large KD-tree subtrees now have conservative axis-aligned bounds. A query skips a whole region when its nearest possible point cannot beat the current result. Bounds are stored for subtrees of at least 64 stars, keeping leaf storage compact.
- Resident local stars establish the initial distance bound before the far catalogue is searched. Radius/nearest-list queries use the same region rejection.
- Identical nearest-star requests reuse their result. The cache is keyed by camera position and streamed-population revision, so loading or evicting stars invalidates it.
- Repeated streaming requests at an unchanged camera position and magnitude return immediately, avoiding duplicate region enumeration and sorting within a frame.

Star positions, seeds, magnitude thresholds, resident populations, rendering resolution and visual quality are retained.

## Measured query cost

A Release-mode CPU benchmark uses the same catalogue and camera positions before and after the change. Each simulated frame makes three identical nearest-star requests; the camera then moves 0.01 ly for the next frame. Results below are averages over 90 frames, excluding catalogue generation. The benchmark ran on the same machine and returned the same nearest-star identities in each version.

| View | Before: three lookups/frame | After: three lookups/frame | Streamed residents |
| --- | ---: | ---: | ---: |
| Solar neighbourhood | 0.214 ms | 0.057 ms | 77,729 |
| Andromeda approach | 401.724 ms | 0.026 ms | 0 |
| Andromeda interior | 372.167 ms | 0.021 ms | 32,864 |

These timings measure the CPU lookup bottleneck, not complete rendered-frame FPS. Rendering, reconstruction and presentation still contribute to frame time. Benchmark source and text results are retained under `build/ProfileGalaxyQueries.cpp`, `build/andromeda-query-baseline.log` and `build/andromeda-query-optimized.log`.

## Correctness and regression checks

`tests/SimulationChecks.cpp` compares nearest results against independent exhaustive scans of the authored Sun, all two million catalogue stars and all resident streamed stars. It tests the Solar neighbourhood, Andromeda's approach/interior and empty intergalactic space. Radius searches are compared with independently selected and sorted results. The same checks are repeated after resident eviction to guard cache invalidation. Repeated queries must use the cache, and remote queries must stay below a 20,000-node traversal budget.

The existing deterministic-generation, planetary hierarchy, navigation, quantum and physics checks remain in place. Both ordinary Release renderer variants are staged and visually validated before replacing the current applications. Previous applications remain until launch checks pass; current incremental caches and useful validation logs are retained during cleanup.

The CPU suite passed exhaustive nearest/radius comparisons, eviction invalidation, the traversal budget and all existing simulation checks. Full Release scene checks passed **284 DX12 scene captures + 14 UI captures** and **275 OpenGL scene captures + 14 UI captures**, with zero failed assertions and zero DX12 validation errors.

All **298 DX12** and **289 OpenGL** PNG captures were verified against every raw framebuffer RGB pixel. The Andromeda approach and M32 seeded-field captures also have identical SHA-256 hashes to their existing reference PNGs, confirming those views are unchanged by the optimization.

Both promoted normal launchers passed **11 UI captures each**, with the PNGs checked against their raw pixels. Their executable hashes match the validated Release candidates. Superseded application outputs and captures were removed after launch validation; raw/duplicate cleanup recovered **3,009.5 MiB**, including 62 duplicate PNGs. Current OpenGL/DX12 applications, incremental caches, profiling utility and useful text logs are retained. Retention records use the `build/andromeda-performance-*` prefix.
