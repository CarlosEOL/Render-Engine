# Render-Engine
3D render engine with GLFW, glad and glm. It has three laboratories: a 1:1 scale Solar System, the Gargantua black-hole system, and a quantum state viewer. Drop an OBJ or FBX file on the window to view a model.

## Building

The project builds with Visual Studio 2022 (v143 toolset, x64 only). It expects third-party headers and libraries in one folder, `..\External_Lib` next to the repository by default:

- `include\` holds `glad\glad.h`, `KHR\khrplatform.h`, `GLFW\`, `glm\`, `assimp\`, `stb_image.h` and `tiny_obj_loader.h` (tinyobjloader 1.0.6 or later).
- `lib\` holds `glfw3.lib`, `assimp-vc143-mtd.lib` for Debug and `assimp-vc143-mt.lib` for Release.
- `bin\` holds any DLLs the libraries need. The build copies them next to the exe.

Use another folder with `msbuild Render-Engine.vcxproj /p:Configuration=Release /p:Platform=x64 /p:ExternalLibDir=C:\path\to\libs`. vcpkg can supply everything: `vcpkg install glfw3 glm assimp tinyobjloader stb glad --triplet x64-windows`. Copy its `include` folder, rename `glfw3dll.lib` to `glfw3.lib`, and put the DLLs from its `bin` and `debug\bin` folders in `bin\`.

`tests\RunSimulationChecks.cmd` builds and runs the physics and catalogue checks. Set `EXTERNAL_LIB` when the libraries are not in `..\External_Lib`.

`Render-Engine.exe --benchmark` opens each scene with playback running and v-sync off, then prints the scene start time and frame-time statistics. It also saves the last frame of each scene as `benchmark-<scene>.ppm`.

## Controls

The window opens on the main menu. **Planetorium** leads to the Solar System and the Gargantua system, and **Quantum Simulation** leads to the state list. **F10** or **Main menu** returns to it from any scene.

The side panel shows the playback state, both clocks, flight speed, follow mode, spacecraft status and the current selection at the top. Below that are a searchable star and object browser, the bodies of the selected system and a description. **F3** hides or shows it. After you click a list, the keyboard returns to the 3D view, so the flight keys keep working.

Every command is also in the menu bar. **Scene**, **Time**, **Camera**, **Spacecraft** and **View** show check marks for the options that are on and grey out commands that do nothing in the current scene.

Shared controls:

- **Space** plays and pauses. Scenes start paused.
- **< / >** (comma / period) change the time rate. Gargantua offers 1×, 60× and 3600×. Star systems go up to one year per second.
- **WASD** fly, **Q / E** move down and up, and **Left Shift** gives a 5× boost. **[ / ]** halve or double flight speed.
- **Right mouse button** toggles mouse look. **Esc** releases the cursor.
- **Mouse wheel** zooms between 15° and 90° field of view.
- **Left-click** selects the body or star under the cursor. **G** or **Go to selection** goes to it. In the Gargantua view, G on a star enters that star's system.
- **F** follows the selection. **L** lands on or lifts off the nearest surface. **F4** or **Home** shows the overview.
- **F2** toggles orbit paths and **F6** toggles the photon view. **F7** switches between HDR tone mapping and pure exposure, and **- / =** step exposure. **View > Lens** applies calibrated camera lens profiles.

Gargantua only:

- **Scene > Miller flyby** follows the ocean planet. **Scene > Spacecraft view** follows the ring spacecraft.
- **B** fires the spacecraft's prograde booster and **R** restores its initial orbit and fuel.
- **H** toggles free fall toward the black hole. The fall stops at 1.015 horizon radii.

Quantum view:

- **Space** plays and pauses, **< / >** change the rate, and **R** restarts the selected state.

Model viewer:

- Drop an `.obj` or `.fbx` file on the window. **F1** switches between wireframe and shaded rendering. **Scene > Gargantua black hole** switches back.

The **World** clock is playback time. The **Local** clock applies approximate gravitational and velocity time dilation at the camera. **Craft light age** is how far into the spacecraft's past the received image looks.

`WindowManager::GetSimulationTime()` returns elapsed playback time in seconds as a `double`.

## Black-hole scene

The scene contains a Gargantua-inspired black hole, a procedural accretion disk, five orbiting stars, three terrain planets including a Miller-inspired world, a ring-shaped spacecraft, and a background star field. Each stellar and planetary orbit has its own semi-major axis, eccentricity, inclination, and phase. Smaller orbits have shorter periods, and bodies move faster near periapsis according to Keplerian motion. These paths are analytic solutions of the central-gravity equations. [NASA's explanation of orbital motion](https://science.nasa.gov/solar-system/orbits-and-keplers-laws/)

The GPU integrates Schwarzschild photon paths to render the disk's bent secondary images and the capture shadow. It evaluates gravitational and orbital Doppler frequency shifts at disk intersections, including the bent ray's emission direction. Shifted blackbody temperatures control emitted colors and brightness. Disk material approaching the observer becomes brighter and bluer, while receding material becomes dimmer and redder. [Schwarzschild photon acceleration equations](https://academic.oup.com/mnras/article/444/3/2470/1063426), [NASA's disk visualization](https://svs.gsfc.nasa.gov/14619/)

### Model assumptions

- The spacecraft advances double-precision position and velocity through gravity and prograde thrust with adaptive kick-drift-kick substeps. Its controller starts a burn when it crosses 1.05 scene units inbound. Its fuel permits 1.2 seconds of constant-acceleration thrust. Exhaust reflects the booster state, and the trail stores sampled simulated positions. The title reports coasting, boosting, capture, or positive-energy escape. Gravity bends the trajectory, and thrust supplies the additional orbital energy.
- Spacecraft attitude guidance keeps the ring axis radial, with a constant spin rate. Its exhaust follows the opposite direction to prograde thrust. Full rigid-body torque and attitude dynamics remain future work.
- The central object uses the non-spinning Schwarzschild model. The Gargantua name describes the visual inspiration. Kerr spin and frame dragging remain future work.
- Orbital motion uses independent Newtonian test-particle ellipses around a fixed central mass. The stars and planet have illustrative sizes and temperatures.
- Scene distances and playback seconds use scaled units. The local clock records elapsed playback time. The observer clock estimates local proper time. The film's extreme time-dilation ratio remains future work.
- The camera samples the local static observer frame at its position. Flight speed controls navigation. Velocity-dependent observer aberration and Doppler effects apply to disk rays. Rasterized background stars and bodies retain approximate screen positions.
- Photon integration has a finite step budget. The orbiting bodies and background stars use rasterization, and compositing their depth against the lensed disk is approximate.
- Ocean waves, foam, and reflections use procedural visual approximations. Film-scale tidal waves and ocean fluid dynamics remain future work.
- Emission uses a three-wavelength Planck-spectrum display approximation with illustrative optical temperatures and fixed exposure.

### CPU and GPU work

The GPU handles photon integration, disk appearance, redshift, Doppler effects, spectral colors, ocean displacement, foam, lighting, and rasterization. The CPU handles eight analytic orbital positions and velocities, spacecraft force integration and fuel, the playback clock, camera movement, and draw submission. V-sync limits unnecessary rendering between display refreshes.

## Change log and reasons

- **src/BlackHole.cpp** owns scene shaders, geometry, bodies, GPU light tracing, and rendering so simulation-specific graphics stay together. It generates shared sphere geometry, spacecraft ring geometry, and a deterministic background star field to avoid external asset dependencies. A full-screen shader traces light into the disk and horizon so the disk produces bent images. The ocean shader adds rotating swells, foam, and highlights so the planet can be inspected up close. The spacecraft controller, finite fuel, adaptive stepping, bounded trajectory buffer, and engine exhaust expose the result of force integration. Reset and manual boost allow the initial experiment to be repeated or altered. Metallic shading, spokes, and modules make the ring craft recognizable at close range.
- **src/BlackHole.h** declares scene initialization, updates, rendering, cleanup, and position queries. The camera uses the queries to follow the planet or spacecraft. It stores spacecraft state, fuel, and trajectory samples between frames.
- **src/OrbitalMechanics.cpp** contains the shared equations for mean motion, Kepler's equation, orbit orientation, analytic position and velocity, particle gravity and thrust integration, gravitational frequency ratios, and the GLSL relativity functions. Keeping these equations together lets the scene call them without embedding the physics throughout its rendering code. Analytic stellar orbits avoid accumulated integration drift during accelerated playback. The spacecraft integrates forces so booster commands change its subsequent trajectory. Input checks reject invalid ellipses, observer positions, and particle inputs.
- **src/OrbitalMechanics.h** declares the orbital parameters and equation interfaces. Scene distances use scaled units and orbital angles use radians.
- **src/WindowManager.h** adds scene ownership, scene selection, planet and spacecraft following, mouse capture, flight speed, time-rate selection, title refresh timing, and graphics cleanup declarations. These values persist between GLFW callbacks and frames.
- **src/WindowManager.cpp** initializes and renders the black-hole scene, adds native clickable Windows controls and keyboard equivalents, and routes successful model drops back to the model viewer. Mouse capture becomes explicit so the controls can be clicked. Vertical movement and adjustable speed support close inspection and travel. Camera motion remains usable while paused. Home and scene switching reset the view, and the flyby follows orbital translation. The title displays the local clock, playback multiplier, and flight speed. Playback starts paused so Space visibly starts animation. Observer movement stays outside the horizon for the static-observer redshift equations. Graphics cleanup releases scene buffers and shaders while the OpenGL context is available. Windows headers load before engine headers to avoid SDK symbol conflicts. V-sync reduces redundant GPU frames.
- **src/main.cpp** calls graphics cleanup before GLFW termination so GPU resources are released with a live context.
- **Render-Engine.vcxproj** includes the new source and header files so Visual Studio builds the scene and equations.
- **README.md** documents the controls, physics assumptions, CPU/GPU responsibilities, and reasons for the changes.

The spacecraft controls in **src/WindowManager.cpp** add close following, manual boost, reset, and a live flight-status title so the physical flyby can be inspected and repeated. The spacecraft view initially places the black hole behind the craft, matching the reference's viewing arrangement.

The shared sphere mesh in **src/BlackHole.cpp** uses higher tessellation to resolve the ocean displacement during close inspection. Its spacecraft attitude basis keeps the ring visible during the close flyby, and translucent blue-white exhaust makes the engine state visible.

- **src/PlanetTerrain.cpp and src/PlanetTerrain.h** add seeded 3D value noise, five-octave fBm, and a ridge contribution for Musgrave-style terrain. Shared vertex and fragment functions keep the displaced surface and its shading consistent. Elevation controls beaches, vegetation, rock, snow, and ocean-depth colors. CPU counterparts evaluate the same surface for camera collision. Sampling normalized 3D coordinates avoids a texture seam at longitude wrapping. These are procedural terrain approximations, with finite mesh resolution and a small camera clearance above the surface. [Musgrave fractal noise](https://docs.blender.org/manual/en/3.0/render/shader_nodes/textures/musgrave.html)
- **src/Atmosphere.cpp and src/Atmosphere.h** add GPU integration through an exponential atmosphere. Wavelength-dependent extinction and a Rayleigh-like phase function produce a blue limb, atmospheric haze, and attenuation of stars from the surface. Planet shadowing changes illumination between day and night. A copied scene texture allows atmospheric transmittance to affect the rendered space background. This uses a single-scattering approximation, illustrative density, a point light at the disk center, and spherical ground clipping. Clouds, weather, multiple scattering, and precise foreground-depth compositing remain future work. [Atmospheric scattering geometry](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-16-accurate-atmospheric-scattering)
- **src/BlackHole.cpp and src/BlackHole.h** add two planets, per-planet terrain seeds, and an atmosphere on all three planets. A deterministic catalogue contains 1,600 local stars and 5,400 stars across three spiral groups. These finite, scaled star distributions have selectable positions and render as spheres nearby. Their galactic motion and mutual gravity remain unmodeled. Target queries supply current positions and names for selection and travel. Landing queries identify the nearest planet and its rotating surface.
- **src/BlackHole.cpp and src/BlackHole.h** also retain spacecraft position, velocity, and booster history. The displayed craft is sampled at an emission time whose estimated arrival matches the observation time. Historical engine state drives the visible exhaust. Navigation targets its current state. The trajectory line is a simulation guide showing its physical path. History samples become farther apart as the mission ages to limit storage growth.
- **src/OrbitalMechanics.cpp and src/OrbitalMechanics.h** add observer clock-rate estimates and direct-path light-travel delay with a first-order Shapiro term. The camera's velocity adds Doppler shifts and aberration to disk rays. Spacecraft light delay uses recorded Newtonian motion and a direct-path approximation. Strong-field multiple-image arrival times, complete relativistic body dynamics, and Kerr spin remain unimplemented. The model produces modest clock differences at these orbital radii. The film's years-to-hours ratio requires additional relativistic modeling. [Black-hole clock rates](https://www.nasa.gov/sites/default/files/atoms/files/black_hole_math.pdf)
- **src/WindowManager.cpp and src/WindowManager.h** add terrain regeneration, landing constraints, free-cam gravity, mouse picking, G travel, separate local/world clocks, and a light-age readout. Surface mouse-look uses the local up direction. Near clipping changes during landing to show nearby ground. Far clipping expands during space flight to include the generated star groups. Reset and flyby controls clear gravity and landing constraints. Pointing at a destination changes selection while the simulation continues.
- **src/BlackHole.cpp and src/BlackHole.h** add a bounded GPU point buffer for 128 photon markers and a detector marker. Packets use straight illustrative paths and playback time scaled by estimated travel duration. This debug view samples the disk and craft. The disk image itself continues to use the separate Schwarzschild ray integrator.
- **Render-Engine.vcxproj** registers the terrain and atmosphere source/header files so the normal Visual Studio build includes both features.
