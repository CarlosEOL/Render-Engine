# Earth geography

- Surface relief from [NOAA NCEI ETOPO 2022](https://www.ncei.noaa.gov/products/etopo-global-relief-model), DOI [10.25921/fd45-gt74](https://doi.org/10.25921/fd45-gt74).
- July surface colors from [NASA Earth Observatory Blue Marble Next Generation](https://science.nasa.gov/earth/earth-observatory/blue-marble-next-generation/base-topography/), July 2004.
- Coastline and inland water polygons from [Natural Earth 1:10m Physical Vectors](https://www.naturalearthdata.com/downloads/10m-physical-vectors/).

The orbital color and coastline atlases use a 5,400 × 2,700 equirectangular grid. Close-range terrain streams the original 60 arc-second ETOPO surface heights and ocean depths, with about 1.85 km between samples at the equator. Peak heights and trench depths reflect the grid’s sampling, and ground texture at walking scale is procedural.

To regenerate the textures, place the source files below in a directory and run `python assets/earth/prepare_maps.py <source-directory>` with NumPy, Pillow, pyshp, tifffile and imagecodecs installed.

- [ETOPO 60 arc-second surface GeoTIFF](https://www.ngdc.noaa.gov/mgg/global/relief/ETOPO2022/data/60s/60s_surface_elev_gtif/ETOPO_2022_v1_60s_N90W180_surface.tif) as `etopo60-surface.tif`
- [Blue Marble July global JPEG](https://assets.science.nasa.gov/content/dam/science/esd/eo/images/bmng/bmng-topography/july/world.topo.200407.3x21600x10800.jpg) as `blue-marble-july.jpg`
- [Land polygons](https://naturalearth.s3.amazonaws.com/10m_physical/ne_10m_land.zip) as `ne_10m_land.zip`
- [Lake polygons](https://naturalearth.s3.amazonaws.com/10m_physical/ne_10m_lakes.zip) as `ne_10m_lakes.zip`

The orbital relief texture packs signed elevation in meters into red and green with a bias of 12,000. Blue and alpha carry the ocean and inland water masks, respectively. Inland water levels are estimated from the mapped shoreline. The twelve elevation archives each contain a 15-degree latitude band of signed ETOPO heights, stored as biased 16-bit PNG tiles. Use the `--tiles-only` option to regenerate the elevation archives independently of the orbital atlases.
