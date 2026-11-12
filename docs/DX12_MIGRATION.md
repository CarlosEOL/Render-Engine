# OpenGL → DirectX 12 migration record

Status: **native DX12 preview implemented; full application migration in progress**. Updated November 11, 2026.

The complete application remains the OpenGL reference, `build/current/bin/Render-Engine.exe`. A separate native DX12 preview now creates a Win32 window, D3D12 device, queue, DXGI swap chain and HLSL pipelines. It does not create an OpenGL context, link OpenGL, copy OpenGL frames into DX12 or read scene pixels through the CPU during normal rendering.

The preview includes GPU Kerr rays, a thin emitting disk, HDR/Pure exposure and free flight. Planets, Endurance/Rangers, interiors, terrain, atmospheres, rings, catalogue browsing, galaxy assets, quantum scenes and lens flares are **not yet ported to the native executable**. DLSS is also not integrated. Its seeded directional sky is illustrative, rather than the navigable galaxy catalogue. This document distinguishes the native milestone from full feature parity.

## Branch separation

| Branch | Purpose | Renderer status |
| --- | --- | --- |
| `build/opengl` | Preserved complete application; checkpoint `e1fa2a0` | OpenGL 3.3, including the Endurance foreground/normal fix, disk visibility and F5 editor |
| `build/dx12` | Includes that checkpoint plus the separately buildable native preview | Native DX12 Kerr/exposure milestone; original OpenGL project remains available for comparison |

`main` is unchanged. No branch is pushed remotely. OpenGL output remains under `build/current`; DX12 outputs are separated under `build/dx12/Debug` and `build/dx12/Release`.

## Native implementation: every change and reason

| Files | Change and reason |
| --- | --- |
| `Render-Engine.DX12.vcxproj` (new) | Separate Debug/Release x64 project, native Windows/DX12 libraries, relative include paths, per-configuration output and HLSL deployment. Keeps the working OpenGL project separate while native parity is incomplete. Uses only Windows SDK libraries and existing GLM headers, without downloaded helper libraries. |
| `Build-DX12.cmd` (new) | Validates the configuration and invokes the installed Visual Studio toolchain. Provides a repeatable entry point without changing the OpenGL solution. |
| `src/dx12/Dx12Main.cpp` (new) | Native window/menu, free camera, playback/rates, exposure modes and shared F5 editor. Identifies itself as a migration preview. Its time currently drives disk structure only. Also contains the hidden `--smoke` checks so tests exercise the real executable. |
| `src/dx12/Dx12Renderer.h/.cpp` (new): lifetime | Hardware adapter selection, explicit optional WARP, debug layer when available, direct queue, two flip-discard buffers, per-buffer allocators/256-byte constant slices and fences. Upload/allocator memory is reused only after retirement. Checked resize/shutdown waits protect resource/descriptors. Unsupported hardware fails clearly rather than silently using software. |
| Renderer: resources | Half-resolution RGBA16F scene, two 1×1 RGBA32F exposure textures, RTV/SRV heaps, root signature, native PSOs and explicit transitions. Retains emission above display white and avoids read/write hazards. Halving both scene dimensions reduces ray pixels to one quarter. |
| Renderer: scheduling | Vsync interactively, cached ray scene when paused/unchanged, independent exposure/presentation, event waiting while minimized. Avoids retracing static scenes or busy rendering a minimized window. No temperature/performance improvement is claimed without measurement. |
| `src/dx12/shaders/BlackHole.hlsl` (new) | Native Kerr RK4 rays, horizon capture, ISCO disk edge, frequency shift/beaming, signed spin and proportional far steps. Shared metric source comes from `KerrSpacetime::ShaderFunctions()` with a `vec3`→`float3` alias, avoiding a second divergent metric. This thin disk does not yet include OpenGL volumetric plasma. |
| `src/dx12/shaders/Optics.hlsl` (new) | Bounded 32×18 GPU metering, emitting-disk percentile, ping-pong adaptation, manual EV, HDR/Pure display and sRGB output. Optional HDR assistance is restricted to the projected disk. Prevents a beamed hotspot from hiding the disk and keeps future camera flare downstream of metering. HDR here means scene-range tone mapping to an SDR swap chain, not HDR10 monitor output. |
| Renderer/check helpers | Explicit fenced, row-pitch-aware PPM readback; half-float finite checks; exposure checks; 1×1 GPU Kerr probes compared against CPU RK4; spin/cache/resize/editor captures; debug-message inspection. Validates actual GPU work, formats and synchronization. Readback runs only for explicit tests. |
| `src/ObjectEditor.cpp` | Guards existing `WIN32_LEAN_AND_MEAN`/`NOMINMAX` definitions so both projects share the editor without redefinition warnings; behavior is unchanged. |
| `README.md`, this document, refinement log, `assets/screenshots/dx12-native-kerr.png` | Launch instructions, controls, actual GPU capture, file-by-file reasons and explicit parity limits make the milestone reviewable. |

Build and launch from the repository:

```bat
Build-DX12.cmd Debug
build\dx12\Debug\bin\Render-Engine-DX12.exe
```

`Build-DX12.cmd Release` writes to `build/dx12/Release/bin`. Shader files are copied beside the executable and compiled at startup with the Windows shader compiler; errors appear in the console. Launching from another working directory is supported. No GLFW/GLAD/Assimp, Vulkan SDK or shader translator is needed by this project.

Preview controls: right-drag to look, WASD to move, R/F up/down, Q/E roll, wheel ×1.12 speed per notch (fractional input retained), Shift+wheel FOV, Home resets the camera. Space plays/pauses; `<`/`>` changes time rate by a factor of ten. F5 edits signed spin; F7 cycles HDR/Pure Auto/Pure Manual; `+`/`-` changes manual EV by 0.25. The View menu toggles disk visibility assistance. The camera is exterior-only and clamps just outside the horizon; navigation is not a physical massive observer. Local clocks, landing/follow/infall/cockpit behavior remain in OpenGL pending their ports.

## Native verification

```bat
build\dx12\Debug\bin\Render-Engine-DX12.exe --smoke build\dx12\checks
```

Tested in **Debug and Release** on **NVIDIA GeForce RTX 4080 Laptop GPU**, with the **DX12 debug layer enabled**. Nine captures pass in each configuration: Kerr HDR, identical cached frame, zero spin, reverse spin, Pure Auto, +1 EV, odd-size resize, restored size and editor spin. A 1×1 resize also passes. HDR components/exposure are finite; paused rendering adds no ray-scene draw. Four HLSL Kerr probes match the double-precision CPU reference with position errors of 3.2×10⁻⁷–8.0×10⁻⁷ AU for the chosen ray/step. This checks implementation agreement, not physical accuracy of the entire scene. Debug validation reports **zero errors**. Release was launched from its executable directory to check shader lookup independently of the repository working directory. Each capture directory contains `report.txt`.

`--warp` explicitly selects Microsoft's software adapter for optional compatibility checks; it was not used for the hardware result above. These are correctness checks, not a performance benchmark. The current half-resolution pass uses ordinary bilinear reconstruction; no denoiser or frame generation is claimed.

The OpenGL checkpoint passed 81 GPU captures with zero GL errors. Its earlier CPU suite passed; a fresh full-suite rerun in this follow-up was blocked by Windows reporting “Access is denied” before its executable was available. The new in-process CPU/GPU Kerr comparisons pass independently. An unchanged CPU-suite result is not presented as a new pass.

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

## Full-parity roadmap (remaining beyond the preview)

1. **Native device and presentation.** Implemented in the separate preview. Full-window scene/UI parity and device-loss recovery remain. Add the native depth target when geometric scene passes arrive.
2. **Explicit resources and binding.** Core heaps/signature/PSOs, upload/readback and fences are implemented. Add mesh/texture uploads, depth resources, asset lifetime and material/source eligibility for the complete scene.
3. **Shaders and a simple scene.** Introduce HLSL files and a build-time compiler step. Port the model viewer and one opaque sphere first. Specify matrix layout, handedness, UV orientation, clip-depth conventions and the existing logarithmic depth formula explicitly. GLM's current OpenGL projection conventions must not be copied blindly.
4. **Astronomical geometry.** Port static stars/galaxies, active systems, 100 m terrain patches, stellar plasma, rings, Endurance/Rangers and interiors. Run CPU checks unchanged; compare deterministic camera views against the OpenGL captures.
5. **Postprocessing.** Port atmospheric scattering, black-hole lensing, mip/downsample passes, source extraction, exposure adaptation, HDR/Pure modes and camera optics. Use separate resources for any pass that reads a prior scene while writing its result.
6. **Quantum and interface parity.** Port quantum views, preserve native menus/browser behavior and verify input, upright cockpit views and toast timing. Run the entire scene/camera suite.
7. **Performance and release.** Measure CPU/GPU frame times, memory, frame pacing and paused-scene reuse on the same hardware/settings. Only change the default backend after feature parity and checks pass. Keep an explicit OpenGL fallback until then.
8. **Optional DLSS.** Add it after DX12 has reliable motion vectors, depth, jitter, exposure and temporal history. Super Resolution and Frame Generation are separate integrations. Do not substitute generated frames for physics updates or assume they reduce heat without measurement.

Microsoft documents the required [device/presentation workflow](https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-hello-world-samples-win32/), [resource binding](https://learn.microsoft.com/en-us/windows/win32/direct3d12/resource-binding-flow-of-control) and [pipeline state/shaders](https://learn.microsoft.com/windows/win32/direct3d12/pipelines-and-shaders-with-directx-12). NVIDIA's [DLSS programming guide](https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf) lists future temporal inputs. Their sample implementations/SDKs have not been copied into this repository.

## Reference data that must survive

- The world seed and deterministic IDs/positions; galaxy membership and planet seeds.
- Physical AU/metre scales, double camera origins and metre-level landed precision.
- Proper-time clocks, finite fuel, Ranger detachment and conserved geodesic quantities.
- Static far-star rendering and zero inactive-system planet-orbit evaluations.
- Source alpha semantics: eligible photospheres versus excluded stellar plasma/cabin displays.
- Glare-independent exposure, Pure-mode real-time adaptation, manual EV and lens differences.
- Landed/cockpit up axes, free-flight follow controls and the small transient notifications.

## Completion criteria

A migration can be marked complete only when DX12 runs all supported scenes, passes CPU/debug/readback checks, reproduces reference views within documented tolerance, and handles device lifetime correctly. The native preview passes its limited scope above; it does not yet satisfy full completion criteria.
