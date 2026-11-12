# Galaxy texture provenance

These are AI-generated illustrative textures, not measured images of named galaxies. The renderer overlays them with synthetic star distributions; they are not survey data.

## Original spiral

File: `galaxy.png`. Mode: new image generation, transparent background.

Prompt:

> Use case: scientific-educational. Asset type: square transparent galaxy texture for a real-time space simulator. Create one photorealistic astronomical face-on spiral galaxy viewed exactly perpendicular to its disk, centered, with its entire disk fitting inside 90 percent of the square. Three main loosely wound spiral arms, each winding roughly one full turn from a compact luminous cream-gold central bulge to the outer rim. Detailed irregular dark brown dust lanes, diffuse pale blue-white stellar arms, subtle pink nebula knots, countless unresolved stars creating soft continuous light rather than distinct dotted lines. Natural Hubble/JWST-inspired visible-light astrophotography, restrained realistic colors, deep dynamic range, bright core but visible dust detail. Isolated on genuine transparent background with smoothly fading transparent outer halo and transparent outside corners. No surrounding sky stars, no neighboring galaxies, no perspective tilt, no lens flare, no text, no labels, no diagram, no watermark.

## Engine screenshots

`screenshots/` contains original framebuffer captures, converted losslessly from PPM to PNG with `tests/convert_render_checks.py`. The Earth low-Sun, Miller ocean, Mann ice, Edmunds desert, Saturn rings, Endurance interior and Ranger interior captures come from this repository's OpenGL `--render-check` harness. `dx12-native-kerr.png` comes from the separate native DX12 preview's `--smoke` harness, captured on the RTX 4080 Laptop GPU. These are actual engine images, not AI-generated artwork or movie frames, and show the implementation's current limits. The original OpenGL gallery remains a reference for the ongoing DX12 port.

## Additional variations

File: `galaxy-variants.png`, 1024 x 1536 RGBA. Mode: new image generation, transparent background. The top cell is a barred spiral; the bottom cell is an elliptical galaxy. OpenGL's vertical image flip is accounted for when choosing the atlas cell. Cells use a 4:3 image aspect correction in world space.

Prompt:

> Create a transparent astronomical texture atlas, tall 2:3 portrait image, divided into two equal horizontal cells with no visible divider. In the TOP half, centered at x=50%, y=25%, make one isolated photorealistic face-on barred spiral galaxy, its entire circular disk comfortably within the top half with generous transparent margins. Strong cream-gold central bar, two irregular dusty blue-white spiral arms, scattered faint pink nebulae, natural astrophotography, soft transparent edges. In the BOTTOM half centered x=50%, y=75%, make one isolated round giant elliptical galaxy, very different from the spiral: compact ivory-gold luminous core and diffuse smooth warm cream stellar halo, subtle shells, no spiral arms. Each galaxy must fit completely within its own half, no overlap. Genuine transparent background everywhere outside the two isolated galaxies and between them, no sky backdrop, no stars outside the galaxies, no labels, no lettering, no grid, no frame. These are two bitmap assets for a realistic space simulator, not a diagram. Fine resolved stellar texture, restrained natural colors, smooth alpha falloff.


`assets/screenshots/ui-laboratory-menu.png` and `dx12-full-gargantua.png` are actual captures of the November 12 full native DX12 application. The menu uses original vector decoration and native control painting; no SpaceEngine assets are used. Existing Earth/Interstellar gallery captures are retained.
