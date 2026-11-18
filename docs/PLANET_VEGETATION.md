# Planet vegetation

Earth is explicitly life-bearing. Procedural terrestrial primaries can receive a deterministic illustrative life flag when their existing atmosphere, water level and temperature permit it. The assignment uses the existing planet seed without consuming catalogue-generation random numbers. Gas planets, airless bodies and generated moons remain ineligible. This is a visual world-generation assumption, not evidence of observed extrasolar life.

Nearby life-bearing terrain has broadleaf, conifer and palm silhouettes, selected by local temperature and latitude. The original meshes include trunks, layered crowns or palm fronds. Scale, rotation and colour vary reproducibly. Placement uses the same terrain elevation as landing and surface rendering, rejects water, high/icy or hot terrain, dry regions and steep slopes, and changes forest/grass density with an illustrative moisture field. Roots use the actual rendered planet radius in metres.

Vegetation occupies stable cube-face spatial cells in the rotating planet frame. A local cache refreshes after roughly 64 metres of camera movement; all relevant cube faces are considered near face boundaries. Tree geometry gives way to crossed silhouette cards beyond 260 metres and fades out from 650 to 900 metres. Submission is bounded to 2,000 tree instances and uses at most six tree batches. Vegetation is skipped from orbit and on lifeless terrain.

Grassland uses crossed 2D cards rather than individual blade meshes. Six curved, tapered blades with transparent gaps are shaped procedurally in the fragment shader. Alpha cutout writes normal depth and requires no transparency sorting. Clumps receive deterministic variation, mild wind motion and distance fading from 50 to 85 metres, with a maximum 6,000 visible instances in one batch. Placement tiles share terrain slopes to avoid evaluating the full terrain field independently for every blade.

Trees and grass render before foreground atmospheric composition, so air and clouds attenuate them. They do not emit stellar flares. This local visual ecology does not implement vegetation collisions, botanical growth, biome feedback or a global stored forest simulation.
