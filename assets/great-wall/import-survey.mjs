import { readFileSync, writeFileSync } from "node:fs";
import { basename, dirname, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { gunzipSync } from "node:zlib";

const [, , input, output] = process.argv;
if (!input || !output) throw new Error("Usage: node import-survey.mjs <greatwall.pmtiles> <routes.bin>");
// Install pmtiles, @mapbox/vector-tile and pbf beside the source archive.
const modules = resolve(dirname(input), "node_modules");
const load = (name) => import(pathToFileURL(resolve(modules, name)).href);
const { PMTiles, tileIdToZxy } = await load("pmtiles/dist/esm/index.js");
const { VectorTile } = await load("@mapbox/vector-tile/index.js");
const { PbfReader } = await load("pbf/index.js");
const file = readFileSync(input);
const archive = new PMTiles({
    getKey: () => basename(input),
    getBytes: async (offset, length) => ({
        data: file.buffer.slice(file.byteOffset + offset, file.byteOffset + offset + length),
    }),
});
const header = await archive.getHeader();
if (header.internalCompression !== 2 || header.tileCompression !== 2)
    throw new Error("Expected gzip-compressed survey vector tiles.");

// PMTiles v3 encodes each directory column separately using unsigned varints.
function* tileEntries(offset, length) {
    const bytes = gunzipSync(file.subarray(offset, offset + length));
    let cursor = 0;
    function read() {
        let value = 0, factor = 1;
        for (; cursor < bytes.length; factor *= 128) {
            const byte = bytes[cursor++];
            value += (byte & 127) * factor;
            if (byte < 128) return value;
        }
        throw new Error("Truncated tile directory.");
    }
    const count = read(), ids = new Array(count);
    for (let i = 0, id = 0; i < count; ++i) ids[i] = id += read();
    const runs = ids.map(read);
    const sizes = ids.map(read);
    let previousEnd = 0;
    for (let i = 0; i < count; ++i) {
        const relative = read();
        const start = relative ? relative - 1 : previousEnd;
        previousEnd = start + sizes[i];
        if (runs[i] === 0) yield* tileEntries(header.leafDirectoryOffset + start, sizes[i]);
        else yield { id: ids[i], run: runs[i], start: header.tileDataOffset + start, size: sizes[i] };
    }
}

// Remove each vector tile's overlap buffer before stitching the archive.
function clip(a, b, extent) {
    if ((a.x === extent && b.x === extent) || (a.y === extent && b.y === extent)) return;
    const dx = b.x - a.x, dy = b.y - a.y;
    let begin = 0, end = 1;
    for (const [origin, delta] of [[a.x, dx], [a.y, dy]]) {
        if (!delta) {
            if (origin < 0 || origin > extent) return;
            continue;
        }
        const t0 = -origin / delta, t1 = (extent - origin) / delta;
        begin = Math.max(begin, Math.min(t0, t1));
        end = Math.min(end, Math.max(t0, t1));
    }
    if (end > begin) return [[a.x + dx * begin, a.y + dy * begin], [a.x + dx * end, a.y + dy * end]];
}

const cellSize = 12500;
const cells = new Map(), codes = new Map();
const distances = Array(6).fill(0);
const regions = new Set();
const towers = new Set();
const waypoints = {
    Mutianyu: [116.5644, 40.4314],
    Jiayuguan: [98.21501, 39.80116],
    Badaling: [116.01419, 40.35957],
    Shanhaiguan: [119.79711, 40.00153],
    Jin: [114.20, 43.10],
};
const closest = {};
function cellAt(lon, lat) {
    const x = Math.floor(lon / cellSize) * cellSize;
    const y = Math.floor(lat / cellSize) * cellSize;
    const key = x + "," + y;
    if (!cells.has(key)) cells.set(key, { x, y, segments: new Map() });
    return cells.get(key);
}

function addTower(details, [lon, lat]) {
    if (details.preservation === "5" || towers.has(details.code)) return;
    const cell = cellAt(lon, lat);
    const x = lon - cell.x, y = lat - cell.y;
    const masonry = details.material === "2" || details.material === "3";
    const preserved = details.preservation === "1" || details.preservation === "2";
    const style = masonry && preserved && details.gwtype !== "353201" ? 6 : 7;
    cell.segments.set("tower:" + x + "," + y, [x, y, x, y, style]);
    if (details.code) towers.add(details.code);
    if (details.province) regions.add(details.province);
}

function add(layer, details, a, b) {
    const [lon, lat] = a, [nextLon, nextLat] = b;
    if (lon === nextLon && lat === nextLat) return;
    const metresPerMicroDegree = .11119508;
    const longitudeScale = Math.cos((lat + nextLat) * Math.PI / 360e6);
    const distance = Math.hypot((lon - nextLon) * longitudeScale, lat - nextLat) * metresPerMicroDegree;
    if (distance <= .12) return;
    const type = details.gwtype;
    // The survey distinguishes masonry, earthen walls, artificial trenches,
    // lost traces and natural barriers that need no extra mesh over the terrain.
    let style = 0;
    if (layer === "trench" || type === "382107") style = 3;
    else if (["382103", "382104", "382105", "382106"].includes(type)) style = 5;
    else if (details.preservation === "5") style = 4;
    else if (details.material === "1" || details.material === "5") style = 2;
    else if (details.preservation === "1" || details.preservation === "2") style = 0;
    else style = 1;
    const cell = cellAt((lon + nextLon) / 2, (lat + nextLat) / 2);
    const {x, y} = cell;
    const reversed = lon > nextLon || (lon === nextLon && lat > nextLat);
    const offsets = reversed ? [nextLon-x,nextLat-y,lon-x,lat-y] : [lon-x,lat-y,nextLon-x,nextLat-y];
    const geometry = offsets.join(",");
    if (cell.segments.has(geometry)) return;
    cell.segments.set(geometry, [...offsets, style]);
    distances[style] += distance;
    if (details.code) codes.set(details.code, style);
    if (details.province) regions.add(details.province);
    for (const [name, [nearLon, nearLat]] of Object.entries(waypoints)) {
        if (name === "Jin" ? details.dynasty_code !== "15" || style !== 3 : style >= 3) continue;
        const error = Math.hypot((lon - nearLon * 1e6) * longitudeScale, lat - nearLat * 1e6);
        if (!closest[name] || error < closest[name].error) closest[name] = { lon: lon / 1e6, lat: lat / 1e6, error, style };
    }
}

const zoom = header.maxZoom;
const firstAtZoom = (4 ** zoom - 1) / 3;
for (const { id, run, start, size } of tileEntries(header.rootDirectoryOffset, header.rootDirectoryLength)) {
    if (id + run <= firstAtZoom) continue;
    const tile = new VectorTile(new PbfReader(gunzipSync(file.subarray(start, start + size))));
    for (let address = Math.max(id, firstAtZoom); address < id + run; ++address) {
        const [z, x, y] = tileIdToZxy(address);
        const grid = 2 ** z;
        for (const name of ["wall", "trench", "building"]) {
            const layer = tile.layers[name];
            if (!layer) continue;
            const coordinate = ([a,b]) => [
                Math.round(((x + a / layer.extent) / grid * 360 - 180) * 1e6),
                Math.round(Math.atan(Math.sinh(Math.PI * (1 - 2 * (y + b / layer.extent) / grid))) * 180e6 / Math.PI),
            ];
            for (let i = 0; i < layer.length; ++i) {
                const feature = layer.feature(i);
                if (name === "building" && feature.type === 1 &&
                    ["352101", "352104", "353201"].includes(feature.properties.gwtype)) {
                    for (const line of feature.loadGeometry()) for (const p of line)
                        if (p.x >= 0 && p.y >= 0 && p.x < layer.extent && p.y < layer.extent)
                            addTower(feature.properties, coordinate([p.x,p.y]));
                }
                if (feature.type !== 2) continue;
                for (const line of feature.loadGeometry()) for (let j = 1; j < line.length; ++j) {
                    const pair = clip(line[j - 1], line[j], layer.extent);
                    if (pair) add(name, feature.properties, coordinate(pair[0]), coordinate(pair[1]));
                }
            }
        }
    }
}

const tiles = Array.from(cells.values()).sort((a, b) => a.y - b.y || a.x - b.x);
const count = tiles.reduce((sum, tile) => sum + tile.segments.size, 0);
if (!count || !tiles.length) throw new Error("No survey routes found.");
const packed = Buffer.alloc(12 + tiles.length * 16 + count * 10);
packed.write("GW01");
packed.writeUInt32LE(tiles.length, 4);
packed.writeUInt32LE(count, 8);
let index = 0, cursor = 12 + tiles.length * 16;
for (const [i, tile] of tiles.entries()) {
    const header = 12 + i * 16;
    packed.writeInt32LE(tile.y, header);
    packed.writeInt32LE(tile.x, header + 4);
    packed.writeUInt32LE(index, header + 8);
    packed.writeUInt32LE(tile.segments.size, header + 12);
    for (const [x1, y1, x2, y2, style] of tile.segments.values()) {
        for (const [i, value] of [x1, y1, x2, y2].entries()) packed.writeInt16LE(value, cursor + i * 2);
        packed[cursor + 8] = style;
        cursor += 10;
        ++index;
    }
}
writeFileSync(output, packed);
console.log(JSON.stringify({
    tiles: tiles.length, segmentsAndTowers: count, routeRecords: codes.size,
    towerRecords: towers.size, regions: [...regions].sort(),
    routeKilometres: distances.map((metres) => Math.round(metres) / 1000), closest,
}, null, 2));
