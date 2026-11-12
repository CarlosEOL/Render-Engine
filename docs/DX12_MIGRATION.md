# OpenGL → DirectX 12 migration record

Status: **not migrated**. Updated November 11, 2026.

The running application still creates an OpenGL 3.3 core context, uses GLAD/OpenGL rendering commands and GLSL, links `opengl32.lib`, and presents with `glfwSwapBuffers`. No D3D12 device, DXGI swap chain, HLSL pipeline, DLSS integration or DX12 executable has been implemented. The current executable is `build/current/bin/Render-Engine.exe`.

The agreed order is to refine and verify the simulation/interiors first, then migrate graphics. This document records preparation actually completed and the concrete work remaining. It must not be read as a completed DX12 implementation report.

## Branch separation

| Branch | Purpose | Renderer status |
| --- | --- | --- |
| `build/opengl` | Preserved, tested OpenGL renderer and simulation checkpoint | OpenGL 3.3, including GPU Kerr lensing and current interiors |
| `build/dx12` | Separate starting point for the future DX12 port, branched from the same checkpoint | Initially the same OpenGL baseline; no DX12 implementation yet |

`main` is left at its previous commit. No branch is pushed remotely. Switch branches only with a clean working tree; the OpenGL output currently remains under `build/current`, while future DX12 products should use a separate output directory.

## Completed preparation

| Work completed | Files | Why it helps the migration |
| --- | --- | --- |
| Kept deterministic galaxy/system generation outside the graphics API | `src/UniverseCatalog.h/.cpp`, generation in `src/StarSystem.cpp` | The world seed, catalogue IDs, AU positions, galaxy boundaries and generated planet identities can remain unchanged across backends. |
| Added independent Interstellar-world and flight equations | `src/InterstellarWorlds.h/.cpp`, `src/OrbitalMechanics.h/.cpp` | Schwarzschild integration, proper-time fuel, orbital parameters and separation conditions can be checked without a graphics device. These modules still expose some GLSL helper strings; extracting those strings is remaining work. |
| Added Kerr ray equations and an independent CPU reference | `src/KerrSpacetime.h/.cpp`, GPU integration in `src/BlackHole.cpp` | The HLSL port can be checked against the same metric, null Hamiltonian, spin/horizon/ISCO definitions and reference rays. This is light propagation; vehicle integration remains Schwarzschild. |
| Concentrated new rendering features in their own modules | `src/SaturnRings.h/.cpp`, `src/VehicleInterior.h/.cpp`, `src/ExposureMeter.h/.cpp` | Rings, interiors and metering each have initialization, render and release lifecycles. They are still OpenGL implementations, but their responsibilities are identifiable for a pass-by-pass port. |
| Established bounded GPU data and camera-relative coordinates | Catalogue buffers, `PlanetTerrain`, `BlackHole`, `StarSystem` | Static distant-star data, high/low star coordinates and local terrain offsets define the precision and memory behavior the DX12 implementation must preserve. |
| Added a CPU regression suite and hidden GPU captures | `tests/SimulationChecks.cpp`, `src/RenderChecks.cpp` | Clock rates, energy conservation, deterministic revisits, zero inactive orbit evaluations, optical-source masking, exposure independence and cabin orientation provide comparison criteria. |
| Consolidated current build products | `build/current`, `.gitignore` | Old experimental outputs were removed. A single validated OpenGL output serves as the reference, with generated files excluded from version control. |
| Documented actual captures and model limits | `README.md`, `docs/SIMULATION_CHANGES.md` | The migration can compare the existing rendered appearance and avoid accidentally changing physical assumptions. |

These changes were implemented to satisfy the simulation requests. They are useful preparation, rather than a graphics API replacement.

## OpenGL-dependent code inventory

| Current code | DX12 work remaining |
| --- | --- |
| `WindowManager.cpp`, `main.cpp`: context creation, viewport/clear, shader compilation, swap, cleanup | Introduce an explicit graphics backend; create the GLFW window with `GLFW_NO_API` for DX12, obtain its HWND, create a D3D12 device/queue and DXGI swap chain. Keep Win32 menus, input and native panels. Route frame begin/end, resize, present and teardown through the backend. |
| `AssetManager.cpp`: OpenGL texture upload and mip generation | Decode assets independently, upload through staging resources, create SRVs and generate/upload mip levels explicitly. Track texture lifetime until GPU fences complete. |
| `StarSystem.cpp`, `BlackHole.cpp`: spheres, terrain, craft, photon markers | Port GLSL to HLSL, define vertex layouts and constant buffers, create immutable PSOs, upload meshes to GPU resources and record draw/instance commands. Preserve double precision on CPU before conversion to camera-relative floats. |
| `StellarActivity.cpp`, `SaturnRings.cpp`, `GalaxyBackground.cpp` | Port instancing, source eligibility, additive/translucent blend modes, depth behavior and seeded shaders. Keep distant stars/particles GPU-only. |
| `Atmosphere.cpp`, black-hole lens pass | Replace copied GL scene/depth textures with explicit intermediate resources and transitions. Preserve foreground occlusion, logarithmic depth decoding and the optical-source mask. Initially port existing equations directly. |
| `CameraOptics.cpp`, `ExposureMeter.cpp` | Port source extraction, exposure history, downsampling/mip generation and final tone-map/lens shaders. Maintain independent scene metering and the two 1×1 history resources; do not meter camera flare. |
| `VehicleInterior.cpp`, toast overlay | Port cabin geometry and overlay blending. Composite the cabin before exposure/optics and UI after final exposure, so interface text cannot change metering. |
| `QuantumSimulation.cpp`, `QuantumParticleView.cpp` | Port particle clouds, graph/line rendering and their depth/blend rules. Preserve the upper 3D viewport and smaller lower graph. |
| `RenderChecks.cpp` | Replace GL pixel reads/error checks with fenced DX12 readback and debug-layer checks. Compare matching scene states and exposure values, allowing small cross-API rasterization differences. |

## Planned implementation sequence

1. **Native device and presentation.** Add a separate DX12 backend with debug-layer support, adapter selection, command queue, flip-model swap chain, render targets, depth target, per-frame command allocators/lists and fence values. Validate resize, minimization, focus changes and shutdown before porting scenes.
2. **Explicit resources and binding.** Add upload/default/readback allocations, RTV/DSV and shader-visible descriptor heaps, samplers, root signatures, constant-buffer alignment, resource-state tracking and deferred release after fences. Do not reuse upload memory while an earlier frame is in flight.
3. **Shaders and a simple scene.** Introduce HLSL files and a build-time compiler step. Port the model viewer and one opaque sphere first. Specify matrix layout, handedness, UV orientation, clip-depth conventions and the existing logarithmic depth formula explicitly. GLM's current OpenGL projection conventions must not be copied blindly.
4. **Astronomical geometry.** Port static stars/galaxies, active systems, 100 m terrain patches, stellar plasma, rings, Endurance/Rangers and interiors. Run CPU checks unchanged; compare deterministic camera views against the OpenGL captures.
5. **Postprocessing.** Port atmospheric scattering, black-hole lensing, mip/downsample passes, source extraction, exposure adaptation, HDR/Pure modes and camera optics. Use separate resources for any pass that reads a prior scene while writing its result.
6. **Quantum and interface parity.** Port quantum views, preserve native menus/browser behavior and verify input, upright cockpit views and toast timing. Run the entire scene/camera suite.
7. **Performance and release.** Measure CPU/GPU frame times, memory, frame pacing and paused-scene reuse on the same hardware/settings. Only change the default backend after feature parity and checks pass. Keep an explicit OpenGL fallback until then.
8. **Optional DLSS.** Add it after DX12 has reliable motion vectors, depth, jitter, exposure and temporal history. Super Resolution and Frame Generation are separate integrations. Do not substitute generated frames for physics updates or assume they reduce heat without measurement.

Microsoft documents the required [device/presentation workflow](https://learn.microsoft.com/windows/win32/direct3d12/creating-a-basic-direct3d-12-component), [resource binding](https://learn.microsoft.com/en-us/windows/win32/direct3d12/resource-binding-flow-of-control) and [pipeline state/shaders](https://learn.microsoft.com/windows/win32/direct3d12/pipelines-and-shaders-with-directx-12). NVIDIA's [DLSS programming guide](https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf) lists the integration APIs and temporal inputs. These references guide future work; their samples/SDKs have not been installed into this repository.

## Reference data that must survive

- The world seed and deterministic IDs/positions; galaxy membership and planet seeds.
- Physical AU/metre scales, double camera origins and metre-level landed precision.
- Proper-time clocks, finite fuel, Ranger detachment and conserved geodesic quantities.
- Static far-star rendering and zero inactive-system planet-orbit evaluations.
- Source alpha semantics: eligible photospheres versus excluded stellar plasma/cabin displays.
- Glare-independent exposure, Pure-mode real-time adaptation, manual EV and lens differences.
- Landed/cockpit up axes, free-flight follow controls and the small transient notifications.

## Completion criteria

A migration can be marked complete only when the DX12 backend builds, runs all supported scenes, passes CPU checks and DX12 debug/readback checks, reproduces the reference views within documented tolerance, and handles resize/device lifetime correctly. A new status entry must name actual changed DX12/HLSL files, build commands, tested hardware, checks and remaining limitations. There is no such completed entry yet.
