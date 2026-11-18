# Planet weather, auroras and accretion appearance

Cloud coverage now follows broad circulation instead of a uniform spherical noise threshold. The map combines equatorial convection, drier subtropics, broken midlatitude fronts, scattered banks and smaller cloud erosion. Three circulation bands advect independently in the rotating planet frame. The orbital shell and surface volume sample the same map, including storms, so descent does not swap weather patterns.

Up to three seeded cyclones occupy warm ocean basins, away from the equator and poles. Their envelopes grow and decay over several simulated days, with clear eyes, irregular eyewalls, spiral rainbands and hemisphere-dependent rotation. Land and unsuitable temperatures suppress these tropical systems. These are bounded visual weather systems, not a meteorological forecast or a fluid simulation. Their basis is [NOAA's tropical-cyclone guidance](https://www.noaa.gov/jetstream/tropical/tropical-cyclone-introduction).

## Wind and water

The same wind field controls ocean displacement, material normals and screen-space reflection distortion. Four wave bands use metre coordinates and gravity-wave dispersion; increased wind raises wave energy, while a steepness bound prevents unbounded crests. Wind is strongest near a cyclone's eyewall and lower in its eye. Derivative filtering suppresses unresolved ripples at distance. Integer wave vectors make the 4096-metre coordinate period continuous, and double-precision time is reduced separately for each wave phase. Paused weather stays deterministic.

Surface reflections continue to sample the finished atmosphere, clouds and celestial scene, replacing the analytic bright stripe where a valid reflection exists. The wind response is an illustrative spectral approximation: fetch, wave age, breaking-wave spray and a full ocean simulation remain outside this model. Real wave development also depends on [wind duration and fetch](https://www.noaa.gov/jetstream/ocean/waves).

## Auroras

Metal-core, active-magnetic-field and atmosphere flags control eligibility. Earth and the authored magnetic Solar giants are eligible; an airless Mercury and inactive Venus are excluded. Procedural dynamo activity uses explicit size, age and rotation assumptions. The magnetic fields of Miller, Mann and Edmund are authored choices for these fictional planets.

Both magnetic poles have animated, folded auroral ovals. Green lower emission, faint red upper emission and a purple lower edge are integrated through a finite upper-atmosphere layer and attenuated by foreground air. Night-side visibility is strongest. Aurora light has no stellar-flare eligibility and is included in the atmosphere image used by ocean reflections. The illustrative oxygen/nitrogen colours and polar geometry follow [NASA's aurora overview](https://science.nasa.gov/sun/auroras/) and [NOAA's aurora explanation](https://www.spaceweather.gov/phenomena/aurora); the shader is not a particle-transport model or an atmospheric-composition solver.

## Foreground cloud attenuation

Cloud extinction is integrated through the entire bounded interval. The old one-percent early termination left a different residual at each final step; Gargantua remained bright enough to reveal that residual as horizontal bands. Cloud intervals now use 24–128 samples, targeting 650-metre spacing, with interpolated coarse weather and finer density erosion. A GPU regression checks that thick cloud can extinguish the bright disk well beyond the former opacity floor. Dense overcast can hide the disk; this is separate from the HDR display response.

## Gargantua and exposure

Auto Exposure meters the visible body of the disk at native texels, uses one camera exposure and allows the brightest disk regions to clip. HDR applies the optional black-hole detail response to explicitly tagged disk emission before constructing glow mips, using an independent disk exposure while retaining the faint sky. The disk treatment previously applied in Auto Exposure now belongs to HDR. The tag follows foreground cloud attenuation and avoids changing ordinary stars, planets or an estimated circle around the object.

Accretion turbulence is filtered against the angular pixel footprint and integration interval. Gargantua's thin cloud layer uses four subsegment samples with analytic vertical weights instead of one midpoint. The camera retains its configured travel resolution, then refines a paused black-hole view to native resolution after 0.25 seconds and reuses the finished frame. An explicitly selected DLSS mode keeps its own reconstruction policy.

Miller's daylight, sunset and ocean-reflection views, plus Mann's icy surface/orbital views, are captured in both HDR and Pure Exposure. The authored distances and atmosphere remain illustrative; these views are not exact reconstructions of SpaceEngine or the film.

## Panurge

The former fifth generic companion is now named Panurge and classified as a neutron star, using a 12 km model radius, 1.4 solar masses and the supplied reference's 8569 K appearance temperature. It retains the existing outer companion orbit (250 AU, eccentricity 0.13). Miller's whole orbit lies inside Panurge's 217.5 AU periapsis. Positions, names and radii agree between navigation and rendering. These are explicit fictional-system choices; the central Kerr solver remains the strong-field lensing model, rather than a new multi-body relativistic solver for Panurge.

## Checks

CPU checks cover deterministic phases, wind-dependent wave energy, calm water, coordinate wrapping, cyclone location/eye wind, aurora eligibility and compact-companion orbital ordering. GPU checks compare the shared wave field against CPU heights, exercise storm appearance and pause stability, count night-side auroral emission, compare exposure modes and verify paused native-resolution refinement and frame reuse. 
The ordinary Release applications passed 334 OpenGL scene captures and 343 DX12 scene captures, with 14 interface captures each and zero assertion failures. DX12 reported zero scene validation errors. All 348 OpenGL and 357 DX12 PNGs were compared against every RGB pixel of their raw captures. The wave-field comparison differed by at most 0.0000058 metres; more than 270,000 ocean pixels demonstrated cloud reflection. The finite Miller atmosphere/aurora checks found zero changed pixels outside their analytic bounds. The added cloud regression extinguished more than 167,000 bright disk pixels beyond the former one-percent floor; face and oblique Endurance views are included in each full run.

Both applications were promoted to the current launcher paths and passed 11 further launch/interface captures each, also validated against their PNGs. Superseded application outputs, duplicate captures and validated raw images were removed; both renderer variants, incremental object caches and useful text logs were retained.
