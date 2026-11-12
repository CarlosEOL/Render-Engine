# 20x zoom and two million systems — November 12, 2026

**Shift + right-mouse drag** now reaches **20x** on every camera/lens profile. **Middle click** restores 1x. The central zoom notification still expires after one second and now includes the effective magnitude limit. The magnitude buttons, Ctrl+wheel and Ctrl+[ / Ctrl+] adjust the manual base limit.

Zoom adds `5 log10(max(1, zoom))` to that base, capped at magnitude 30. A base of 7.00 becomes **13.51 at 20x**, then returns to 7.00 when zoom resets. The panel displays effective and base values when boosted. This is an intentional camera visibility aid, not a claim that changing focal length increases a real telescope's aperture. Planet ground detail continues to use distance/projected-size LOD; magnitude controls unresolved star visibility.

## Procedural systems

The catalogue contains **2,000,000 deterministic stellar locations** inside the four existing galaxy bounds. The original 16,000 stars retain their names, locations, seeds and IDs. Additional names run from **Procedural star 1** through **Procedural star 1984000**. Their IDs start at 3,000,000, avoiding the existing black-hole and nebula namespaces. The same star ID recreates the same stellar properties, planets and terrain seeds.

Click a visible star, then **G**, to visit its system. **F3** accepts procedural star names and the full names of their generated planets. **Shift+F3** finds the nearest systems within the requested radius, showing at most 1,000 results. F4's older native list remains a bounded legacy list; it does not allocate two million UI rows. Detailed planets are generated when visited or explicitly queried. Inactive systems do not advance planetary orbits on the CPU.

This is a finite cached point catalogue, not an infinite streaming universe. Static locations and scalar stellar properties are generated once at startup. The main persistent arrays take approximately 48 MB for double-precision positions, 32 MB for spatial-tree nodes, 8 MB for draw-order indices and 8 MB for luminosities, plus batch/UI/graphics overhead. GPU point attributes use about 64 MB. Detailed meshes are not allocated for every system. Removing the duplicate position array and per-star hash-map cells avoids additional memory growth.

The spatial tree orders points into 256-star batches. Per-view CPU work tests batch bounds and the brightest possible apparent magnitude; it does not integrate two million star positions. The GPU applies the individual magnitude cutoff before rasterization. Visible ranges are combined/coalesced to at most **64 procedural draws per view**, plus the Sun. Some rejected points in merged gaps still reach the vertex shader, where magnitude filtering and normal clipping remain active. This tradeoff bounds driver work and avoids exhausting DX12 descriptors when refreshing six sky-cubemap faces.

Picking prunes batches before comparing individual candidates. Nearest/nearby searches use the same spatial tree. Magnitude and camera movement do not change seeds or relocate systems. Direct stars respond while zooming; cached lensing skies refresh when the zoom drag ends, avoiding six-face rebakes on every mouse event. The high/low position attributes preserve the existing camera-relative precision scheme.

## Every change and its reason

| File | Change | Reason |
| --- | --- | --- |
| `src/CameraZoom.h` | Centralize 20x magnification, tangent-based minimum FOV and magnitude gain. | Give every lens the same maximum magnification instead of unrelated degree limits. |
| `src/WindowManager.h/.cpp` | Apply the zoom bound; retain manual magnitude; synchronize effective visibility, picking, toast/title/panel; decode large star IDs for navigation anchors. | Reveal fainter objects while zooming, restore the base on reset, and navigate the expanded catalogue correctly. |
| `src/ScenePanel.h/.cpp` | Show a compact effective/base magnitude label. | Explain the boost without overlapping the magnitude buttons. |
| `src/UniverseCatalog.h/.cpp` | Expand to two million entries; add stable, collision-free ID conversions and fast deterministic properties/positions; reuse the spatial tree for nearby queries and GPU ordering; remove hash cells. | Avoid millions of small allocations, complete planetary systems or UI strings. Preserve the original catalogue. |
| `src/StarSystem.cpp` | Describe new stars from the same scalar properties used for their point rendering. | A visited point must become the same stellar type, temperature and luminosity, with reproducible planets. |
| `src/BlackHole.h/.cpp` | Remove duplicate positions; upload spatially ordered points; cull/merge batches; cap draw submissions; prune click candidates; defer sky refresh while dragging; preserve camera diagnostics across cubemap renders. | Bound memory, CPU/driver work and descriptor use while retaining GPU magnitude filtering and selection. The first dense-view run exposed descriptor exhaustion; capped submissions fixed it. |
| `src/NavigationCatalog.cpp` | Resolve new systems and planets on demand by name; use the tree for nearest results; retain bounded legacy metadata. | Make millions of destinations usable without eagerly constructing all systems. |
| `src/RenderChecks.cpp` | Check all seven lens profiles, 20x/reset/base preservation, magnitude gain, IDs, galaxy bounds, reproducibility, nearest/click/name lookup and an actual 20x frame. | Verify application behavior and retain existing atmosphere, exposure, vehicle, wormhole and galaxy regressions. |
| `tests/SimulationChecks.cpp` | Update large-catalogue IDs and boundary/repeatability checks; add zoom/magnitude checks. | Keep the standalone tests compatible and avoid regenerating a galaxy for every boundary assertion. |
| `Render-Engine.vcxproj` | Register the zoom header and provide missing OpenGL x64 Release headers, libraries and C++20 configuration. | The Release target could not find GLM/GLAD/TinyObjLoader. Both requested optimized backends must build from the shared sources; DX12 keeps its own link overrides. |
| README | Update controls, catalogue size, limitations and launcher paths. | Keep instructions consistent with the delivered executables. |
| Historical Kerr/wormhole logs | Add a cleanup pointer without rewriting the earlier results. | Their superseded binary/capture folders were removed at the user's request; current launchers and small archived logs remain. |

## Builds and validation

Optimized 64-bit launchers:

- OpenGL: `build/current/bin/Render-Engine.exe`
- DX12: `build/dx12/Release/bin/Render-Engine-DX12.exe`

Both include the Saturn wormhole. Compiling the pure OpenGL project does not enable DX12 in that executable or switch the source branch.

- Final DX12 Release: **164 scene captures + 13 UI captures**, zero failed assertion locations, **zero DX12 validation errors**.
- Final pure OpenGL Release: **155 scene captures + 13 UI captures**, zero failed assertion locations.
- Both ran from their actual launch folders. Checks covered two-million-entry catalogue size, sampled galaxy bounds, seed/planet repeatability, large-ID round trips, nearest lookup, click selection and procedural name lookup. All seven camera profiles reached 20x; reset restored 1x and magnitude 7 after a zoomed limit of 13.505. Existing renderer, atmosphere, exposure, vehicle, wormhole, DLSS (DX12) and galaxy checks passed.
- Final logs: `build/latest-dx12-render.log`, `build/latest-opengl-render.log`, and their `latest-*-build.log` counterparts. PNG evidence remains in `build/latest-dx12-checks` and `build/latest-opengl-checks`.
- Removed **2.25 GiB** of superseded builds and duplicate raw captures after validation. Only the two current Release application executables remain under `build`; their current incremental caches, assets/DLLs and latest PNG checks are preserved. Small historical logs were moved to `build/history-logs`. Source, committed screenshots and the sibling `External_Lib` were preserved. No running application was stopped and no branch switch/publication was performed.

The historical standalone CPU-test launcher previously returned Windows “Access is denied”; it is not claimed as a passed run here. The native engine checks execute the new zoom, catalogue, nearest/click lookup, deterministic-system and navigation assertions directly.
