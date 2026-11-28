# Great Wall survey routes

`routes.bin` is adapted from the [Great Wall Archive survey map](https://greatwallarchive.com/map), which compiles China's 2012 national Great Wall resource survey. The archive publishes its [GIS data and methodology](https://greatwallarchive.com/sources) under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

Render-Engine uses the public [vector tile archive](https://shanqiao.app/map/greatwall.pmtiles). The importer extracts the highest-resolution wall and trench lines and located tower records, crops the overlapping tile buffers, removes identical edges, classifies the features, and packs the coordinates in geographic tiles. The imported lines cover approximately 21,153 km. That is the length of the mapped centerlines, measured on a sphere. The national survey's 21,196.18 km is a separate headline measurement.

Render-Engine generates illustrative meshes on Earth's terrain for masonry, weathered walls, earthworks, trenches, and lost traces. Mountains, rivers, lakes, and other natural barriers are represented by the terrain itself. Routes remain discontinuous where the survey has gaps.
