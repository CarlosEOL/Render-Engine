# Milky Way appearance upgrade — November 12, 2026

The previous rendering was a smooth analytic glow. The revised rendering uses the unlabelled 5,600-pixel NASA/JPL PIA10748 artist concept to supply structured arms, stellar associations, dust lanes and emission-region colours, with a separate treatment for observers inside the disk.

## Changes and reasons

| File | Change | Why |
| --- | --- | --- |
| `assets/galactic/milky-way-nasa.jpg`, `CREDITS.md` | Store NASA/JPL's original PIA10748 JPEG and provenance/hash. | Supply the requested NASA-level exterior detail with explicit attribution. No generated replacement or edited copy of the artwork is used. |
| `src/MilkyWaySky.cpp` | Map the image into the existing Galactic coordinate frame, rotate its illustrated bar toward the model bar, linearize its display colours, and integrate its structure with the 3D stellar/dust field. | Replace the uniform brown blur while preserving camera-relative position and free flight. The image spans an illustrative 36 kpc square; placement is approximate. |
| Same renderer | Increase the cached sky from 1,024 x 512 to 2,048 x 1,024. Use eight projected low-frequency layers plus a sharp mid-plane detail residual outside the disk. Fade between this representation and the volume across 0.4–1.5 kpc of camera height. Nearly edge-on rays retain the volume. | Resolve distant spiral detail at viewport resolution without magnifying cached sky texels. Keep broad depth while avoiding repeated, radially smeared copies of small clusters. |
| Same renderer | Use local analytic stellar emissivity inside the disk, modulated by the artwork's fine structure, rather than extruding the illustration's integrated brightness everywhere. Increase dust-clump contrast. | Prevent a bright blue fog covering the entire sky and retain a darker sky above the Galactic dust band. |
| Same renderer, `src/MilkyWaySky.h` | Own/release the texture once; retain translation-based cache refresh and bypass volume work where the exterior representation supplies the full ray. | Keep rotation free of volume rebakes and avoid any new CPU stellar simulation. Refresh is limited to four times a second and requires movement beyond the existing 10 pc threshold, with existing nearer-nebula overrides. |
| Both project files | Copy the JPEG and its credits alongside each executable. | Make DX12/OpenGL launches independent of the working directory and preserve attribution with distributed assets. |
| `src/WindowManager.cpp` | Show the NASA/JPL appearance credit when the Milky Way is selected. | Keep the source visible in the application, alongside its bundled credits. |
| `src/main.cpp`, `src/WindowManager.h`, `src/RenderChecks.cpp` | Add `--galaxy-check DIR` and include the seven-view galaxy checks in the full render suite. Check arm/core colour separation, darker off-plane interior, and no rebake on rotation. | Catch the original featureless glow, an overbright interior, camera-dependent cache work, and graphics validation failures. Capture face-on, inclined, edge-on and disk-transition views. |
| README and `assets/screenshots/milky-way-*.png` | Record actual engine captures and the source credit. | Make the new appearance reviewable without presenting NASA's original image as an engine screenshot. |

## Performance and accuracy

The cache occupies 16 MiB as RGBA16F. The 5,600-square artwork plus mipmaps costs approximately 160 MiB on a four-byte-per-texel backend, with temporary image decoding/upload memory during initialization. It is loaded once. This trades some GPU memory for visual detail; it does not introduce a CPU simulation of the illustrated stars. Navigable catalogue objects, their seeds, coordinate frames and gravitational calculations are unchanged. Fine points painted in the illustration represent unresolved aggregate light and are not individually selectable catalogue stars. The diffuse galaxy always writes zero flare-source eligibility.

The reconstruction is an artistic light field. The NASA artwork is a 2008 structural artist concept, not an external photograph or a current measured 3D density map. Vertical reconstruction, extinction clumping, display brightness and colour are approximate. This does not claim exact star positions, calibrated photometry, or exact recovery of NASA's appearance from every viewpoint. Existing nebula shapes are not changed by this Milky Way work.

## Source and credit

**Courtesy NASA/JPL-Caltech.** PIA10748, *Our Milky Way Gets a Makeover (Artist Concept)*, June 3, 2008. NASA's companion resource credits the artwork to NASA/JPL-Caltech/R. Hurt (SSC/Caltech).

- [NASA/JPL source and description](https://www.jpl.nasa.gov/images/pia10748-our-milky-way-gets-a-makeover-artist-concept/)
- [NASA companion Milky Way resource](https://science.nasa.gov/resource/the-milky-way-galaxy/)
- [JPL image-use policy](https://www.jpl.nasa.gov/jpl-image-use-policy/)

## Validation

- Native DX12 Debug full suite: **140 scene captures + 11 UI captures**, zero failed assertion locations and **zero DX12 validation errors**.
- Pure OpenGL Debug full suite: **131 scene captures + 11 UI captures**, zero failed assertion locations. Its process exit code was independently confirmed as zero; the orchestration shell was closed after the graphics process exited successfully.
- Native DX12 Release focused `--galaxy-check`: **7 scene captures + 10 UI captures**, zero failed assertions and **zero DX12 validation errors**.
- The seven galaxy viewpoints include face-on, a rotated face-on view, inclined, edge-on, the Solar neighbourhood and both ends of the exterior/volume transition. The cache did not rebuild on rotation.
- Native face-on image: 402,984 blue-arm pixels and 11,135 warm-core pixels at the tested viewport. The off-plane interior row averaged 28.31/255; the check rejects the initial bright blue fog. These are regression measurements, not calibrated astronomical brightness.
- Existing atmosphere tests retained zero changed pixels outside Miller's allowed atmospheric shell and no nonfinite radiance. Lens/exposure checks passed, with unchanged glare-on/off metering; native Sun-pan target span remained 0.0253 EV.

Logs/captures: `build/dx12/milky-way-final-checks*`, `build/current/milky-way-final-checks*`, and `build/dx12/Release/milky-way-release-checks*`. The standard OpenGL launch folder and both DX12 executables include the image and its credits. Raw PPMs are removed only after confirming their PNG counterparts exist.

The existing size-conversion and CRT-library warnings remain. No measured improvement in GPU temperature is claimed.
