# Adaptive galaxy and nebula quality

The camera now uses a cheaper volumetric preview during significant translation, then refines after stopping. Rotation, a high flight-speed setting without actual movement, and small subpixel translations do not by themselves lower quality.

SpaceEngine publicly documents separate moving/stationary resolution settings for galaxies and nebulae, based on whether motion noticeably changes their perspective; its stationary path uses a skybox. See the [official user manual, “Volumetric objects resolution”](https://spaceengine.org/manual/user-manual/). That supports the user's observation; it does not establish a universal kilolight-year-per-second threshold or expose all of SpaceEngine's internal algorithms. This implementation uses our own existing radiative-transfer sky cache.

## Every change and its reason

| File / area | Change | Reason |
| --- | --- | --- |
| `src/MilkyWaySky.h/.cpp`, motion policy | Add actual world-camera translation observation with frame duration, field of view and viewport height. Estimate projected parallax against the galactic field and nearer nebulae. Enter preview above approximately 30 pixels/second; retain it above 15 pixels/second, with a 0.25-second settling delay. | Respond to noticeable motion rather than a speed dial or an arbitrary absolute navigation unit. Hysteresis avoids rapid quality changes near the boundary. The distance estimate is a conservative heuristic, not an exact per-pixel velocity field. |
| Same class, camera resets | Reset the motion sample and settling deadline on explicit sky invalidation. Do not infer motion from `Render()` calls, sky-cube faces or camera rotation. | Treat teleports as fresh views and sample the actual camera once per application update. Avoid false velocity estimates from auxiliary rendering. |
| Same class, moving cache | Render a 1024×512 cache with at most 48 galactic ray steps and 20 steps per nebula. Use a 0.10-second translation-refresh/fade interval, while retaining the existing distance threshold and completed-fade guard. | Reduce expensive integration during fast travel and refresh the cheaper preview more often than the previous 0.32-second cadence. |
| Same class, stationary cache | After settling, render at 2048×1024 with up to 192 galactic steps and 40 per nebula, even if the camera has stopped short of the normal translation threshold. Fade to the new linear-radiance cache over 0.32 seconds. | Restore angular resolution and more accurately integrate/filter grazing paths, then return to reuse. This is one higher-quality rebuild and a crossfade, not temporal Monte Carlo accumulation. |
| Same shaders, budgets | Parameterize the bounded marching loops; retain fixed maximum loop counts. The finer settled sampling also decreases the footprint used to select texture mips along grazing paths. | Increase stationary detail without changing the dust, emissivity or physical-coordinate model. Keep the work finite on both backends. |
| Same display shader, preview filtering | Apply a normalized five-tap prefilter only to preview caches, tracking the quality of both current and previous textures independently during fades. | The first coarse implementation produced aliasing rather than stable softness. Prefiltering suppresses this noise without modifying exposure or filtering separate star/planet passes. The weights sum to one. |
| Same class, texture ownership | Resize only the destination cache when its quality changes; swap the associated width and quality metadata with the textures. Keep the old cache intact until the crossfade completes. | Avoid destroying the source image during a quality transition. No third sky texture or mip chain is introduced. |
| Same class, readiness | Request a frame when cached quality differs from the desired quality, and retain the final fully blended frame requirement. Once settled, stationary/rotation-only views need no new volume integration. | Ensure paused-scene reuse does not trap the camera in its blurry preview or freeze an incomplete transition. |
| `src/BlackHole.h` | Forward camera observation and the adaptive-policy control to the sky cache. | Keep ownership in the existing renderer while letting the window's frame scheduler observe the correct world position. |
| `src/WindowManager.cpp` | Observe the post-movement, post-system-handoff world camera immediately before deciding whether to reuse a scene. | Let a stopped camera wake refinement even when the simulation is paused. Preserve a continuous position estimate across automatic system handoffs. |
| `src/RenderChecks.cpp`, reference captures | Hold adaptive quality off for authored reference poses that teleport the camera. | Keep numerical/appearance comparisons deterministic and avoid measuring teleport velocity as ordinary flight. Normal application startup keeps adaptation enabled. |
| Same checks, adaptive sequence | Add a continuous actual-translation → preview → stop → refinement → rotation sequence. Check cache dimensions/budgets, wake-up readiness, changed detail, final idle readiness, no rebake on rotation and no preview for a one-AU subpixel displacement. | Test actual rendering and scheduling, rather than only checking the policy flags. Measure second-difference image contrast in the central dust lane of the same isolated view; whole-frame values were dominated by quantization in the large faint halo, not resolved detail. |
| `Render-Engine.DX12.vcxproj` | Add an explicit optional `DX12OutputDir` override in both output property groups; leave the normal path unchanged when unset. | The project resets `OutDir` after importing the OpenGL project. The initial candidate command consequently wrote to the normal DX12 folder. This dedicated property makes candidate staging reliable. The committed DX12 baseline was reconstructed separately for fallback restoration; working source files were not reverted. |
| README / this report | Describe moving versus settled rendering, source attribution, costs and validation. | Keep the behavior and each implementation decision reviewable. |

## Cost and limits

The moving galactic pass has one quarter as many cache pixels and one quarter as many allowed integration steps as the settled pass: **1/16 of its nominal pixel-step budget**. Early ray exits, texture sampling, preview filtering and other rendering passes mean this is not a 16× frame-rate claim. The stationary budget is twice the previous fixed 96-step budget and runs when needed, rather than on every camera rotation.

Two RGBA16F sky caches occupy approximately **8–32 MiB**, depending on their current sizes. Quality switches may reallocate a destination texture. No extra CPU stellar motion, galaxy generation or gravitational integration is introduced; CPU motion observation checks the camera and four authored nebulae. Catalogue points, local stars, planets, spacecraft and UI remain at their existing quality settings. Existing DX12 DLSS behavior remains separate.

The effect is a change in rendering quality, not a simulation of camera exposure-time motion blur. Refinement restores detail supported by the artwork and model; it cannot invent unresolved astronomical structure. Very fast motion can still reveal approximation/cache latency, and the parallax thresholds are heuristics.

## Validation and build retention

The committed DX12 fallback was rebuilt in an isolated source snapshot after the initial staging override failed, and passed its focused galaxy suite with zero failed assertions and zero DX12 validation errors. It was restored to the normal launcher while corrected candidates were tested. The original OpenGL launcher remained intact until promotion. The explicit `DX12OutputDir` override was confirmed in subsequent build logs.

Both final **Release x64** candidate suites passed:

| Check | DX12 | OpenGL |
| --- | --- | --- |
| Full scene / UI captures | 234 / 13 | 225 / 13 |
| Failed assertion locations | 0 | 0 |
| Preview cache / galactic steps | 1024×512 / 48 | 1024×512 / 48 |
| Settled cache / galactic steps | 2048×1024 / 192 | 2048×1024 / 192 |
| Measured central-lane detail energy, preview → settled | 87,480 → 209,421 | 90,627 → 214,794 |
| Settled detail-energy ratio | 2.39× | 2.37× |
| Refinement wake-up, completed fade and return to idle | Passed | Passed |
| Rotation-only cache reuse / subpixel-motion quality | Passed | Passed |

These image measurements compare the same pose and exposure; they are not an astronomical fidelity or FPS measurement. The README includes the actual isolated moving/settled captures. Existing galaxy geometry, atmosphere, exposure/flare, independent Kerr capture, spacecraft/interior, wormhole, resize and generation checks passed. DX12 exercised 40 DLSS evaluations and reported **zero graphics validation errors**. The pre-existing size-conversion warning remains.

The tested executable SHA-256 hashes were checked during promotion into `build/dx12/Release/bin/` and `build/current/bin/`. Both normal launchers then passed a separate 11-capture UI/runtime-asset smoke check; DX12 again reported zero validation errors. Superseded executables, temporary baseline/candidate directories, obsolete captures and validated PPM duplicates are removed only after this promotion check. Current source/dependencies/object caches, final PNG evidence and useful text logs remain. No branch was switched or published.
