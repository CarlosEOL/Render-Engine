"""Pack the source datasets described in CREDITS.md into runtime textures."""

import argparse
import io
from pathlib import Path
from zipfile import ZipFile

import numpy as np
from PIL import Image, ImageDraw
import shapefile
import tifffile


WIDTH, HEIGHT = 5400, 2700


def rasterize(archive_path, elevation=None):
    land_mask = elevation is None
    empty = 0 if land_mask else -32768
    mask = Image.new("L" if land_mask else "I", (WIDTH, HEIGHT), empty)
    draw = ImageDraw.Draw(mask)
    with ZipFile(archive_path) as archive:
        filename = next(name for name in archive.namelist() if name.endswith(".shp"))
        with shapefile.Reader(shp=io.BytesIO(archive.read(filename))) as reader:
            for shape in reader.iterShapes():
                geometry = shape.__geo_interface__
                coordinates = geometry["coordinates"]
                polygons = [coordinates] if geometry["type"] == "Polygon" else coordinates
                for polygon in polygons:
                    rings = [
                        [((longitude + 180) * WIDTH / 360 - 0.5,
                          (90 - latitude) * HEIGHT / 180 - 0.5)
                         for longitude, latitude in ring]
                        for ring in polygon
                    ]
                    if not rings or len(rings[0]) < 3:
                        continue
                    fill = 255
                    if not land_mask:
                        x, y = np.rint(np.asarray(rings[0])).astype(int).T
                        shoreline = elevation[y.clip(0, HEIGHT - 1), x % WIDTH]
                        # ETOPO can include lake-bed relief. Use the upper
                        # middle shoreline elevation as the water surface.
                        fill = round(float(np.percentile(shoreline, 60)))
                    for index, ring in enumerate(rings):
                        draw.polygon(ring, fill=fill if index == 0 else empty)
    return np.asarray(mask)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sources", type=Path)
    source = parser.parse_args().sources
    output = Path(__file__).parent

    dem = tifffile.imread(source / "etopo60-surface.tif")
    if dem.shape != (HEIGHT * 4, WIDTH * 4) or not np.isfinite(dem).all():
        raise ValueError("Expected the global 60 arc-second ETOPO surface raster.")
    # Average each 4x4 block onto the aligned render grid.
    elevation = dem.reshape(HEIGHT, 4, WIDTH, 4).mean(axis=(1, 3))
    meters = np.rint(elevation).astype(np.int32)
    if meters.min() < -12000 or meters.max() > 12000:
        raise ValueError("Elevation outside the texture encoding range.")

    land = rasterize(source / "ne_10m_land.zip")
    lake_surface = rasterize(source / "ne_10m_lakes.zip", elevation)
    inland_water = lake_surface != -32768
    meters[inland_water] = lake_surface[inland_water]
    lakes = inland_water.astype(np.uint8) * 255
    ocean = np.where(inland_water, 0, 255 - land)
    packed = meters + 12000
    relief = np.stack([packed >> 8, packed & 255, ocean, lakes], axis=-1)
    Image.fromarray(relief.astype(np.uint8), "RGBA").save(output / "earth-relief.png")

    Image.MAX_IMAGE_PIXELS = 250_000_000
    with Image.open(source / "blue-marble-july.jpg") as source_image:
        source_image.resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS).save(
            output / "earth-color.jpg", quality=93, subsampling=0, optimize=True
        )


if __name__ == "__main__":
    main()
