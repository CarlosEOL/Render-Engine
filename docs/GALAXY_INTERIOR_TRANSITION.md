# Continuous approaches to external galaxies

The old external-galaxy image multiplied its visibility by a distance fade between 2.5 and 1.25 galaxy radii. Local seeded stars could load, but no diffuse galaxy light replaced the image. Andromeda therefore vanished on approach, particularly when looking toward its nucleus.

`GalaxyBackground` now blends the distant image into a world-aligned, finite light volume between 3.25 and 2.25 radii. Inside that range the volume remains visible, including from within the disk. Camera-relative double-precision coordinates preserve the position of distant hosts. A forward-only ray/box intersection prevents the galaxy from being projected onto the opposite hemisphere.

The volume uses the existing porous emission maps, disk thickness and a round stellar bulge. The same feathering and colours are shared with the distant image. Backside image orientation now agrees with the galaxy's fixed spatial frame. Emission adds to the background and retains the galaxy display class; separately generated stars keep their existing seeds, density, magnitude limits and selectable identities. This is an illustrative stellar-light volume, not a measured three-dimensional reconstruction or a new dust radiative-transfer simulation.

Two nearby hosts can retain independent caches, allowing Andromeda and M32 to coexist during the companion approach. The same transition applies to the other existing external-galaxy appearances. Remote wormhole sky rendering does not replace or invalidate local volume caches.

## Performance and stopping

Only nearby hosts need a volume. Each cache stores an equirectangular sky, so rotation and zoom reuse it rather than ray-marching the full application viewport. Fast translation uses 512 × 256 pixels and 40 integration steps. A stationary camera refines to 1024 × 512 pixels and 96 steps. Small translations refresh after a position threshold and crossfade over 0.18 seconds; large teleports replace the cache directly. The previous light map remains available during a fade. Both active caches together use at most 16 MiB for their four half-float light maps.

Paused-frame reuse waits for the terminal fade, stationary refinement and any translation queued during a fade. The previous CPU nearest-star performance improvement is retained.

## Validation

The render suite isolates diffuse light from procedural stars to test eight approach distances, including the former disappearance range and an interior position. It checks both transition boundaries, forward-only visibility, rotation reuse, simultaneous M31/M32 volumes, moving preview quality, stationary refinement, pending translation and return to idle. The focused galaxy suite explicitly selects filled rendering so its outcome does not depend on the model viewer's initial wireframe setting.

Both ordinary Release renderer variants passed the full scene suite: **298 OpenGL scene captures + 14 UI captures**, and **307 DX12 scene captures + 14 UI captures**, with zero failed assertions and zero DX12 validation errors. All **312 OpenGL** and **321 DX12** PNGs were compared against every raw framebuffer RGB pixel. The existing CPU simulation suite also passed.

At the two blend boundaries, the average display RGB difference across a 0.0002-radius camera step stayed below 0.002 on both renderers. The previously empty close range retained diffuse light throughout all eight approach poses. Stellar-core classification checks confirm that increasing camera sensitivity still reveals additional stars inside the volume.

Both current launchers passed another **11 UI captures each** after promotion; those PNGs were also checked against their raw pixels. Superseded application outputs and obsolete diagnostic captures were then removed. Raw/duplicate capture cleanup recovered approximately **4.5 GiB**, including 68 duplicate PNGs. Current incremental compiler caches and useful text validation logs remain. Promotion hashes, retention manifests and cleanup records use the `build/galaxy-interior-*` prefix.
