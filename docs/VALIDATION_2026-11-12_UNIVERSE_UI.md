# Universe/UI follow-up validation — November 12, 2026

## Completed checks

| Build | Result |
| --- | --- |
| Native DX12 Debug | Full suite: **133 scene captures + 11 UI captures**, no assertion failures, **zero DX12 validation errors**. |
| Native DX12 Release | Full suite: **133 scene captures + 11 UI captures**, no assertion failures, **zero DX12 validation errors**. |
| Final DX12 card layout, Debug and Release | Separate `--ui-check`: **11 captures each**, no failures or DX12 validation errors. |
| Separate pure OpenGL checkout | Full suite: **124 scene captures + 11 UI captures**, no assertion failures. Final card-layout `--ui-check`: **11 captures**, no failures. |
| CPU simulation/navigation suite | All **38 quantum presets**, Solar System scale/hierarchy, **1,024 generated-system** survey, **16,000-star** galaxy bounds/repeatability, nearest-tree comparison against exhaustive search, local-offset precision, named planet/moon/asteroid/barycentre/nebula lookup, planetary parent relationships, magnitude scaling and existing clocks/vehicle checks passed. |

The nine additional native scene captures exercise DLSS. The full native runs recorded **40 actual NGX evaluations and nine history resets**. The existing DLSS integration remains experimental: moving scenes reset history conservatively; true dynamic/bent-ray motion vectors, Frame Generation and Ray Reconstruction remain outside its present implementation.

Tests explicitly confirm that single-click name selection leaves camera coordinates and simulation time unchanged, and that no redundant Select button is present. Final overview cards show readable diameters, temperatures and satellite counts; barycentres do not inherit a body's temperature.

Miller's atmosphere on/off comparisons at three viewing distances reported **zero changed pixels outside the allowed foreground shell** and no nonfinite radiance. The eclipse comparison changed coronal light while preserving the covered lunar-centre scene radiance. Six lens profiles retained distinct additive effects. Switching glare on/off did not change metered exposure; panning with the visible Sun produced a **0.0253 EV** target range. Dormant-system orbital evaluation count remained zero outside the detail range.

## Regressions found and corrected during this work

- Adding a star-visibility varying exposed a DX12 geometry/pixel signature packing incompatibility. Compute the transition from existing source data inside the pixel shader instead, while retaining vertex-stage faint-star rejection. This removes the additional interface attribute and restored zero validation errors.
- A background sprite's orientation initially depended on the previous model transform. Seed it from the immutable star position so otherwise identical frames remain identical.
- Optical-source eligibility was incorrectly usable as compositing opacity. Fully composited atmosphere/lensing passes now replace scene RGB with blending disabled, preserving metadata independently.
- An extreme unresolved display PSF could override exposure despite a visible nearby Sun. A resolved host now meters its stable continuum; secondary optical signatures remain independent.
- Introducing the Sun into nearest-star navigation could steal the camera during the authored Gargantua fall. Navigation handoff is disabled for the physical falling observer, and the legacy authored Gargantua/Solar alternate scenes require an explicit Sun transition.
- Initial overview window sizing ignored display scaling and clipped icon labels. Use DPI-aware dimensions and explicitly painted cards with readable numerical values. Single clicks select; only Go to travels.

## Saved outputs and cleanup

Current test logs/captures are under `build/dx12/space-browser-*`, `build/dx12/Release/space-browser-*` and `build/current/space-browser-*`. Representative PNGs are copied into `assets/screenshots` and referenced from README. The existing Earth/Interstellar gallery remains intact.

Removed **16 obsolete generated capture/build directories**, freeing **6.14 GiB**, after verifying every resolved target remained below this repository's `build` directory. Current executables, dependencies, source files, current validation outputs and the temporary OpenGL checkout were preserved. No user application was terminated.

Remaining build warnings are the existing CRT-library conflict and size conversion warning. Successful regression checks are evidence for these exercised cases, not a claim of complete scientific validation or measured GPU-temperature improvement.
