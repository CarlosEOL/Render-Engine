# OpenGL to DirectX 12 migration record

Updated November 12, 2026. **The primary DX12 executable now runs the complete supported application scene set**, including Planetorium and Quantum Simulation. The original isolated Kerr preview remains a separate optional project. This is a native DX12 renderer: no OpenGL context, OpenGL driver rendering, CPU frame transfer or interop presentation copy is used.

## Branches and builds

| Branch | Main project | Output |
| --- | --- | --- |
| `build/dx12` | `Render-Engine.DX12.vcxproj` | `build/dx12/Debug/bin/Render-Engine-DX12.exe` or `Release/bin` |
| `build/opengl` | `Render-Engine.vcxproj` | `build/current/bin/Render-Engine.exe` for the documented isolated build |

`main` remains unchanged. The DX12 branch retains the original project as a reference; its primary executable renders through DX12. The OpenGL branch has no DX12 SDK/runtime dependency.

Run `Build-DX12.cmd Debug` or `Build-DX12.cmd Release` from a Visual Studio 2022 installation with C++ and the Windows SDK. The script builds the pinned GLSL translator and acquires the official DLSS dependency in sibling `External_Lib`. Existing dependency checkouts with different revisions are reported rather than reset. Galaxy assets, translator/Assimp runtime DLLs, and the DLSS redistributable are copied beside the executable. Run from any working directory.

For an SDK-free native fallback after building the shader translator, pass `/p:EnableDLSS=false` to MSBuild. This omits NGX headers/libraries; the UI safely retains native rendering. The historical `Render-Engine.DX12.Preview.vcxproj` is separately buildable and is not the full application. Its output is under `build/dx12/Preview`.

## Supported scene parity

The DX12 application reuses the same scene controls and simulation code as OpenGL: startup laboratories, the scaled Solar System, generated star systems, 100 m terrain patches, atmospheres, Saturn rings, stellar surfaces/prominences, galaxy images, GPU Kerr lensing, Endurance/Rangers and interiors, photon illustration, browser/selection/G travel, following/rotation/landing/free fall, transient notifications, object editor, HDR/Pure exposure, calibrated lens profiles and glare, and all seven Quantum example families. OBJ and FBX import also use the same application path.

New shared physics, catalogue, selection and interface changes are described in [Kerr observer](KERR_OBSERVER_2026-11-12.md), [star generation and picking](STAR_GENERATION_AND_SELECTION_2026-11-12.md), [interface polish](UI_POLISH_2026-11-12.md), and the earlier [atmosphere/Moon correction](ATMOSPHERE_AND_MOON_2026-11-12.md). [DLSS](DLSS_2026-11-12.md) is an optional, experimental native DX12 addition.

## Every migration change and why

| Files or component | Change and reason |
| --- | --- |
| `Render-Engine.DX12.vcxproj` | Imports the existing application's complete source list, sets the DX12 build definition, and links native DX12/DXGI/compiler libraries instead of OpenGL. Separate output/intermediate folders prevent one backend overwriting the other. Deploys assets and runtime libraries. |
| `Render-Engine.DX12.Preview.vcxproj` | Separates the old reduced Kerr milestone from the full application. Preserves its reproducible smoke checks without presenting it as feature parity. |
| `Build-DX12.cmd`, `tools/BuildShaderTools.cmd` | Reproducible Debug/Release entry point and pinned dependency bootstrap, limited to four compiler workers. Avoids relying on unrecorded local compiler installations. |
| `src/dx12/ShaderTranslator.h/.cpp`, `tools/shader-translator/CMakeLists.txt` | Translate the shared GLSL stages through glslang/SPIR-V/SPIRV-Cross into native HLSL and DXBC. Both backends execute the same maintained scene/shader equations. The translator DLL has a C allocation/free interface so Release compiler libraries do not exchange C++ runtime/STL ownership with a Debug application. |
| `src/dx12/Dx12SceneBridge.h/.cpp`: scene commands | Implements the GL-style command subset used by this project as D3D12 resource uploads, descriptor bindings, PSOs and draw commands. GLAD function slots dispatch to this backend without loading OpenGL. This preserves the existing scene graph and application behavior rather than maintaining duplicate simulation/render implementations. It is not a general OpenGL implementation. |
| Bridge: shader linkage and constants | Reflects uniform/sampler/input layouts, transposes GLSL matrix storage for generated HLSL, aligns stage signature register ordering, and supplies unused interpolants as required. Fixes native stage-link mismatches and coordinate differences. |
| Bridge: points | Expands point sprites with a native geometry shader and correct PointCoord. Matches the reference GPU's pixel-center and lower-tie size rounding; protects unresolved stars and Quantum particle density from disappearing. |
| Bridge: textures/depth | Native default-heap 2D/cube textures, D32 depth, uploads, per-subresource transitions, render targets, depth copies and GPU mip generation. Preserves source alpha eligibility for photospheres versus excluded plasma/cabin light. |
| Bridge: coordinates | Maintains the application's bottom-up internal image convention and flips only the final GPU display blit. Existing atmosphere sampling, lens maps and readback checks remain consistent. |
| Bridge: memory/lifetime | Two persistent 16 MiB upload rings (32 MiB total), with transient oversized/overflow allocations retained to the frame fence. Large imports do not require a permanently huge ring. RTV/DSV pools recycle only after retirement, including resize/deletion; per-frame transient shader descriptors are also fenced. |
| Bridge: presentation/resize | Native swap-chain presentation and target resizing, including client-size changes caused by menu/panel changes before the first frame. Uploads are retired before target replacement. Prevents blank first Quantum frames and stale descriptors. |
| `src/dx12/Dx12Renderer.h/.cpp` | Shares the native device/queue/swap chain/fences with the bridge. Full application initialization skips all unused preview render targets, exposure textures and HLSL preview pipelines. Normal Release avoids validation overhead; render-check explicitly requests the debug layer in both configurations. Debug remains validated when available. |
| `src/dx12/Dx12Main.cpp` | Keeps the optional preview using the revised device initialization and enables validation for its smoke mode. |
| `src/WindowManager.h/.cpp` | Uses a GLFW window with no graphics context in DX12 builds; native presentation and backend title identification. Reuses input, UI, double camera origins, state transitions and clocks. Conditional hooks leave OpenGL builds independent. |
| `src/main.cpp`, `src/RenderChecks.cpp` | Shared startup, opt-in checks, exception reporting, native validation and explicit graphics/backend shutdown. Tests the real full application rather than only a synthetic preview. `--warp` explicitly requests software rendering; hardware fallback is not silently substituted. |
| `src/ObjectLoader.cpp` | Uses the Assimp C API with automatic scene release, avoiding a C++ runtime/STL ABI mismatch with the available debug Assimp DLL in Release builds. Fixes the old vertex-count/face-count indexing error, traverses all node meshes, preserves hierarchy transforms, uses inverse-transpose normals and computes imported bounds. |
| `src/SaturnRings.cpp` | Renames the shader variable `filter` to `detailFilter`, preserving its math while making it valid for the standards-based shader compiler. |
| `src/dx12/Dx12Dlss.h/.cpp` and bridge/optics integration | Real optional NGX Super Resolution, HDR inputs, projection jitter, fenced history/recreation, hardware checks and native fallback. The exact scope and conservative reset policy are documented separately; Frame Generation/Ray Reconstruction are not included. |
| `tests/KerrObserverChecks.cpp`, `tests/kerr/CMakeLists.txt`, `tests/RunKerrObserverChecks.cmd` | Graphics-free massive-observer checks of the shared Kerr equations. |
| `src/RenderChecks.cpp`, `tests/SimulationChecks.cpp` | Import tests, seven Quantum families, atmosphere/depth masks, neutral Moon, stellar emission, selection/travel, cache/inactive systems, horizon continuity, odd resizes, real DLSS evaluations and native UI captures. Generator preflight, nonblocking assertion logs and failure locations fix the confusing automated-check behavior. |
| `third_party/licenses`, README and migration records | Preserve compiler/SDK notices, pin provenance, actual captures, repeatable commands and limitations. Remove accidentally tracked compiler logs/recipes and ignore regenerated copies. |

Compiler revisions: glslang `ff6ad3c77a2cc2fdea329b4a1d5425c82bc56a1f`; SPIRV-Cross `57e4efcc84112abcd426dc5ec248b5339bee04f2`. SDK revisions and their independent licence are in the DLSS record. See [third-party notices](../third_party/README.md).

## Checks

Run the executable with `--render-check <output-directory>` for opt-in scene/UI captures and a nonzero exit on failure. Tests use GPU readback; normal rendering does not. `tests\RunSimulationChecks.cmd` exercises 1,024 generated systems, solar scale/hierarchy, clocks, terrain and vehicle dynamics without a graphics context. `tests\RunKerrObserverChecks.cmd` checks the central observer without graphics dependencies.

Validation is recorded on an NVIDIA GeForce RTX 4080 Laptop GPU. The full native Debug run passed 113 scene captures and six normal/compact UI captures, including all four actual DLSS modes and zero DX12 validation errors. The generator survey and standalone Kerr observer also passed. Latest normal/compact UI, Release and OpenGL validation results are recorded in the interface update and build logs; a successful compile alone is not visual validation.

## Remaining engineering and model limits

- DLSS resets history on scene changes until complete moving-object and bent-ray motion vectors exist. Stable views accumulate; moving views use current-frame reconstruction. This is explicitly experimental.
- HDR means linear scene storage plus tone mapping to SDR, not HDR10 monitor output. No Frame Generation, Ray Reconstruction, HDR10 or automatic device-loss recovery is claimed.
- Planet/vehicle/camera physical approximations remain documented independently of the graphics API. A native backend does not make a mixed Newtonian/Schwarzschild/Kerr simulation fully relativistic.
- General shader translation/PSO creation occurs at startup or first use; an on-disk shader cache remains future work. The native command bridge supports the application's used API subset.
- Correctness captures are not GPU performance or temperature benchmarks. No speed/thermal claim is made without equal-scene measurements.

The [historical preview milestone](DX12_PREVIEW_HISTORY.md) is retained for provenance. Primary integration references: [Microsoft D3D12 presentation](https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-hello-world-samples-win32/), [resource binding](https://learn.microsoft.com/en-us/windows/win32/direct3d12/resource-binding-flow-of-control), [native pipelines](https://learn.microsoft.com/windows/win32/direct3d12/pipelines-and-shaders-with-directx-12), [Khronos glslang](https://github.com/KhronosGroup/glslang), and [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross).

### Release rebuild correction

Two incremental Release links in this environment reported LNK1103 with the project-wide C7 (`/Z7`) symbol override. DX12 now restricts C7 symbols to Debug, uses standard PDB (`/Zi`) symbols for Release, and requests full link-time generation rather than reusing the incremental LTCG symbol cache. After removing stale Release objects once, both a clean build and a second rebuild of `RenderChecks.cpp` passed, followed by 113 scene/six UI captures and zero native validation errors. Existing static-library runtime warnings remain; these are not reported as zero compiler warnings. [Microsoft symbol formats](https://learn.microsoft.com/en-us/cpp/build/how-to-debug-a-release-build?view=msvc-170), [link-time generation](https://learn.microsoft.com/en-us/cpp/build/reference/ltcg-link-time-code-generation?view=msvc-170).
