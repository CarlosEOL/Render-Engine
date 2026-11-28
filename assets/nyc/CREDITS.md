# New York City data

- Building footprints, roof heights, and ground elevations from [NYC Open Data BUILDING](https://data.cityofnewyork.us/City-Government/BUILDING/5zhs-2jue/about_data) and its [official metadata](https://github.com/CityOfNewYork/nyc-geo-metadata/blob/main/Metadata/Metadata_BuildingFootprints.md).
- Streets from [NYC Street Centerline](https://data.cityofnewyork.us/City-Government/NYC-Street-Centerline-CSCL-/inkn-q76z/about_data).
- Land and maritime extents from [NYC Borough Boundaries](https://data.cityofnewyork.us/City-Government/Borough-Boundaries/gthc-hcne) and [Borough Boundaries with water areas](https://data.cityofnewyork.us/City-Government/Borough-Boundaries/wh2p-dxnf).
- Terrain from [New York State ITS Bare Earth DEM](https://elevation.its.ny.gov/arcgis/rest/services/Latest_DEM/ImageServer).

The November 2026 import contains 1,082,928 building records. Footprints are extruded with their published roof heights where available. Facades, windows, and uniform roadway widths are generated for the scene. Coarse building volumes are rendered beyond 100 metres. The city surface overlay uses a 3,072 by 3,072 elevation raster and the published shoreline.
