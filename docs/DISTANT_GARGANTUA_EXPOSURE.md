# Distant Gargantua and exposure recovery

HDR now grades disk emission at native scene texels before building the filtered images used for glow. Its independent disk exposure no longer comes from the whole camera exposure. The original scene remains available to the meter and validation readbacks. This removes the dark disk inside an uncorrected bright halo when the object covers only a few pixels; the same corrected emission supplies both the resolved disk and its glow.

The meter samples a bounded 64 by 64 region around the projected disk at native texels, instead of requiring eight bright whole-screen mip samples. Its median sets Auto Exposure's bright disk response; its upper quartile supplies HDR's independent detail exposure. Disk metering follows the visible object even when a different nearby star system owns optical flares.

Dark sky now participates in the scene mean. A single distant bright point cannot impose the brightest-pixel exposure limit, and the photosphere calibration requires a resolved stellar disk. Turning away therefore permits the camera exposure to recover, including while simulation time is paused. Exposure history is retained through the turn and approaches the new target over real elapsed time.

The old 20,000 AU switch to background-only rendering is removed. Distant lens rays receive enough work for their weak-field approach and escape paths, rather than becoming dark when the ordinary near-camera budget runs out.

Regression captures cover 650 AU, 5,000 AU and a zoomed view from one light-year. They check bright Auto Exposure disk pixels, detailed HDR disk pixels and exposure recovery after turning away without resetting history. The meter also distinguishes an isolated bright point from a resolved bright source. Background preservation is checked directly in faint image regions; total image brightness is not used as a proxy for background brightness.
The final Release checks passed 334 OpenGL scene captures and 343 DX12 scene captures, with 14 interface captures each and zero assertion failures. DX12 reported zero scene validation errors. At the three distant poses, the disk's displayed upper quartile was 221–230 in HDR and 255 in Auto Exposure (8-bit RGB); look-away exposure reached its target without a history reset.

Both current launchers passed 11 additional interface/launch captures each. All 348 OpenGL and 357 DX12 full-check PNGs, plus launch captures, were checked against every raw RGB pixel before removing superseded builds and captures. Both renderer variants, current object caches and useful validation logs were retained.
