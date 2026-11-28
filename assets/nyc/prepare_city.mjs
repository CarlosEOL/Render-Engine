import { createReadStream, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { createInterface } from 'node:readline';
import { basename, join } from 'node:path';
import { deflateSync, inflateSync, crc32 } from 'node:zlib';

const source = process.argv[2];
const destination = process.argv[3];
if (!source || !destination) throw new Error('Usage: node prepare_city.mjs <source folder> <asset folder>');

const radius = 6371000;
const degrees = Math.PI / 180;
const origin = [-74.006, 40.7128];
const originSin = Math.sin(origin[1] * degrees);
const originCos = Math.cos(origin[1] * degrees);
const bounds = [-74.30, 40.455, -73.65, 40.955];
const tileSize = 256;
const tiles = new Map();
const boroughs = [0, 0, 0, 0, 0];
const feet = .3048;
const clamp = (value, min, max) => Math.max(min, Math.min(max, value));

function local([longitude, latitude]) {
    const phi = latitude * degrees;
    const delta = (longitude - origin[0]) * degrees;
    const horizontal = radius * Math.cos(phi);
    return [horizontal * Math.sin(delta), horizontal * Math.cos(delta) * originSin - radius * Math.sin(phi) * originCos];
}

function tileAt(x, z) {
    const column = Math.floor(x / tileSize), row = Math.floor(z / tileSize);
    const key = `${column}/${row}`;
    if (!tiles.has(key)) tiles.set(key, { column, row, buildings: [], roads: [] });
    return tiles.get(key);
}

function parseTiff(filename) {
    const file = readFileSync(filename);
    const little = file.toString('ascii', 0, 2) === 'II';
    const u16 = (offset) => little ? file.readUInt16LE(offset) : file.readUInt16BE(offset);
    const u32 = (offset) => little ? file.readUInt32LE(offset) : file.readUInt32BE(offset);
    if (u16(2) !== 42) throw new Error('Expected a classic elevation TIFF');
    const directory = u32(4);
    const tags = new Map();
    for (let index = 0; index < u16(directory); ++index) {
        const entry = directory + 2 + index * 12;
        const type = u16(entry + 2), count = u32(entry + 4);
        if (type !== 3 && type !== 4) continue;
        const bytes = type === 3 ? 2 : 4;
        const start = count * bytes <= 4 ? entry + 8 : u32(entry + 8);
        tags.set(u16(entry), Array.from({ length: count },
            (_, i) => bytes === 2 ? u16(start + bytes * i) : u32(start + bytes * i)));
    }
    const tag = (number, fallback) => tags.get(number)?.[0] ?? fallback;
    const width = tag(256), height = tag(257), compression = tag(259, 1);
    const bits = tag(258), sampleFormat = tag(339, 1), predictor = tag(317, 1);
    if (tag(277, 1) !== 1 || tag(274, 1) !== 1 || predictor !== 1 || ![1, 8, 32946].includes(compression)) {
        throw new Error(`Unsupported elevation encoding ${compression}/${predictor}`);
    }
    const values = new Float32Array(width * height);
    function readBlock(offset, count, x, y, stride, lines) {
        if (count === 0) return;
        const chunk = file.subarray(offset, offset + count);
        const pixels = compression === 1 ? chunk : inflateSync(chunk);
        for (let row = 0; row < lines && y + row < height; ++row) {
            for (let col = 0; col < stride && x + col < width; ++col) {
                const byte = (row * stride + col) * bits / 8;
                const value = bits === 32 && sampleFormat === 3
                    ? little ? pixels.readFloatLE(byte) : pixels.readFloatBE(byte)
                    : bits === 16 && sampleFormat === 2
                        ? little ? pixels.readInt16LE(byte) : pixels.readInt16BE(byte)
                        : NaN;
                values[(y + row) * width + x + col] = value;
            }
        }
    }
    const offsets = tags.get(324) ?? tags.get(273);
    const counts = tags.get(325) ?? tags.get(279);
    const stride = tag(322, width);
    const lines = tag(323, tag(278, height));
    const across = Math.ceil(width / stride);
    offsets.forEach((offset, i) => readBlock(offset, counts[i], (i % across) * stride, Math.floor(i / across) * lines, stride, lines));
    return { width, height, values };
}

const dem = parseTiff(join(source, 'dem.tif'));
const pixelAt = ([longitude, latitude]) => [
    (longitude - bounds[0]) / (bounds[2] - bounds[0]) * dem.width,
    (bounds[3] - latitude) / (bounds[3] - bounds[1]) * dem.height,
];
const elevation = (point) => {
    const [x, y] = pixelAt(point).map(Math.floor);
    if (x < 0 || x >= dem.width || y < 0 || y >= dem.height) return 0;
    const height = dem.values[y * dem.width + x];
    return Number.isFinite(height) && height > -100 && height < 300 ? height : 0;
};

function rasterize(filename) {
    const output = new Uint8Array(dem.width * dem.height);
    for (const feature of JSON.parse(readFileSync(filename, 'utf8')).features) {
        const polygons = feature.geometry.type === 'Polygon' ? [feature.geometry.coordinates] : feature.geometry.coordinates;
        for (const polygon of polygons) {
            const scanlines = new Map();
            for (const ring of polygon) {
                const pixels = ring.map(pixelAt);
                for (let i = 1; i < pixels.length; ++i) {
                    let [x0, y0] = pixels[i - 1], [x1, y1] = pixels[i];
                    if (y0 > y1) [x0, y0, x1, y1] = [x1, y1, x0, y0];
                    const last = Math.min(dem.height, Math.ceil(y1 - .5));
                    for (let y = Math.max(0, Math.ceil(y0 - .5)); y < last; ++y) {
                        const row = scanlines.get(y) ?? [];
                        row.push(x0 + (x1 - x0) * (y + .5 - y0) / (y1 - y0));
                        scanlines.set(y, row);
                    }
                }
            }
            for (const [line, intersections] of scanlines) {
                intersections.sort((a, b) => a - b);
                for (let i = 0; i + 1 < intersections.length; i += 2) {
                    const left = clamp(Math.ceil(intersections[i] - .5), 0, dem.width);
                    const right = clamp(Math.ceil(intersections[i + 1] - .5), 0, dem.width);
                    output.fill(255, line * dem.width + left, line * dem.width + right);
                }
            }
        }
    }
    return output;
}

function writeRelief() {
    const land = rasterize(join(source, 'land.geojson'));
    const extent = rasterize(join(source, 'extent.geojson'));
    const rowBytes = 1 + dem.width * 4;
    const raw = Buffer.alloc(rowBytes * dem.height);
    let valid = 0;
    for (let y = 0; y < dem.height; ++y) for (let x = 0; x < dem.width; ++x) {
        const cell = y * dem.width + x;
        const offset = y * rowBytes + x * 4 + 1;
        const height = dem.values[cell];
        const terrain = Number.isFinite(height) && height > -100 && height < 300 ? height : 0;
        // Land heights are stored to a quarter metre. Existing bathymetry
        // supplies sea depths while the borough outline sets the shoreline.
        raw.writeUInt16BE(Math.round(clamp(terrain, -100, 300) * 4) + 12000, offset);
        raw[offset + 2] = 255 - land[cell];
        raw[offset + 3] = extent[cell];
        if (extent[cell]) ++valid;
    }
    if (valid < 500000) throw new Error('Missing city boundary coverage');
    function chunk(name, data) {
        const type = Buffer.from(name);
        const header = Buffer.alloc(8);
        header.writeUInt32BE(data.length);
        type.copy(header, 4);
        const crc = Buffer.alloc(4);
        crc.writeUInt32BE(crc32(data, crc32(type)));
        return Buffer.concat([header, data, crc]);
    }
    const header = Buffer.alloc(13);
    header.writeUInt32BE(dem.width);header.writeUInt32BE(dem.height, 4);
    header[8] = 8;header[9] = 6;
    writeFileSync(join(destination, 'relief.png'), Buffer.concat([
        Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', header),
        chunk('IDAT', deflateSync(raw, { level: 8 })), chunk('IEND', Buffer.alloc(0)),
    ]));
}

const positions = (list) => list.trim().split(/\s*,\s*/).map((point) => point.trim().split(/\s+/).map(Number));

function shells(wkt) {
    const multi = wkt.startsWith('MULTIPOLYGON');
    if (!multi && !wkt.startsWith('POLYGON')) return [];
    const ringDepth = multi ? 3 : 2;
    const result = [];
    let nesting = 0, begin = -1, first = false;
    for (let i = 0; i < wkt.length; ++i) {
        if (wkt[i] === '(') {
            ++nesting;
            if (nesting === ringDepth - 1) first = true;
            if (nesting === ringDepth && first) begin = i + 1;
        }
        if (wkt[i] === ')') {
            if (nesting === ringDepth && first) {
                result.push(positions(wkt.slice(begin, i)));
                first = false;
            }
            --nesting;
        }
    }
    return result;
}

async function rows(filename, handle) {
    const lines = createInterface({ input: createReadStream(filename), crlfDelay: Infinity });
    let header = true;
    for await (const line of lines) {
        if (header) { header = false; continue; }
        const values = line.match(/"[^"]*"|[^,]+|(?<=,)(?=,|$)/g)?.map((token) => token.replace(/^"|"$/g, ''));
        if (values?.length && values[0]) handle(values);
    }
}

function addBuilding([wkt, roof, ground, bin]) {
    const key = Number(bin) || 0;
    let count = 0;
    for (const ring of shells(wkt)) {
        if (ring.length < 4) continue;
        const points = ring.slice(0, -1).map(local);
        const center = points.reduce((sum, p) => [sum[0] + p[0], sum[1] + p[1]], [0, 0]).map((v) => v / points.length);
        const tile = tileAt(...center);
        const quantized = [];
        for (const [x, z] of points) {
            const q = [Math.round((x - tile.column * tileSize) * 8), Math.round((z - tile.row * tileSize) * 8)];
            if (!q.every((v) => v >= -32768 && v <= 32767)) continue;
            const previous = quantized.at(-1);
            if (!previous || previous[0] !== q[0] || previous[1] !== q[1]) quantized.push(q);
        }
        if (quantized.length < 3 || quantized.length > 2048) continue;
        const groundMetres = ground !== '' && Number.isFinite(Number(ground)) ? Number(ground) * feet : elevation(ring[0]);
        const height = Number(roof) > 0 ? Number(roof) * feet : 5.5;
        const building = Buffer.allocUnsafe(10 + quantized.length * 4);
        building.writeUInt32LE(key, 0);
        building.writeInt16LE(Math.round(clamp(groundMetres, -30, 300) * 10), 4);
        building.writeUInt16LE(Math.round(clamp(height, 1, 640) * 100), 6);
        building.writeUInt16LE(quantized.length, 8);
        quantized.forEach(([x, z], i) => { building.writeInt16LE(x, 10 + 4 * i); building.writeInt16LE(z, 12 + 4 * i); });
        tile.buildings.push(building);
        ++count;
    }
    const borough = Math.floor(key / 1000000) - 1;
    if (borough >= 0 && borough < boroughs.length) boroughs[borough] += count;
}

function addStreet([wkt]) {
    for (const line of wkt.matchAll(/\(([^()]+)\)/g)) {
        const path = positions(line[1]);
        for (let i = 1; i < path.length; ++i) {
            const a = local(path[i - 1]), b = local(path[i]);
            const length = Math.hypot(b[0] - a[0], b[1] - a[1]);
            if (length < .4) continue;
            const splits = Math.ceil(length / 38);
            for (let part = 0; part < splits; ++part) {
                const u = part / splits, v = (part + 1) / splits;
                const interpolate = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t];
                const start = interpolate(a, b, u), end = interpolate(a, b, v);
                const tile = tileAt(...interpolate(start, end, .5));
                const road = Buffer.allocUnsafe(12);
                const [column, row] = [tile.column * tileSize, tile.row * tileSize];
                [start[0] - column, start[1] - row, end[0] - column, end[1] - row].forEach((value, k) =>
                    road.writeInt16LE(Math.round(value * 8), k * 2));
                [u, v].forEach((t, k) => road.writeInt16LE(Math.round(elevation(interpolate(path[i - 1], path[i], t)) * 4), 8 + k * 2));
                tile.roads.push(road);
            }
        }
    }
}

writeRelief();
const full = statSync(join(source, 'buildings.csv'), { throwIfNoEntry: false });
const input = full?.size > 100000000 ? ['buildings.csv'] :
    readdirSync(source).filter((name) => /^buildings-\d+\.csv$/.test(name)).sort();
if (input.length === 0) input.push('buildings.csv');
for (const filename of input) {
    await rows(join(source, filename), addBuilding);
    process.stdout.write(`${basename(filename)}: ${boroughs.join(', ')}\n`);
}
if (boroughs.some((count) => count < 20000)) throw new Error('Building input does not cover all five boroughs');
await rows(join(source, 'streets.csv'), addStreet);

const header = Buffer.alloc(12 + tiles.size * 20);
header.write('NYC1');
header.writeUInt32LE(tiles.size, 4);
header.writeUInt32LE(tileSize, 8);
let entry = 12, offset = header.length;
const blocks = [];
for (const tile of tiles.values()) {
    const compressed = deflateSync(Buffer.concat([...tile.buildings, ...tile.roads]), { level: 6 });
    header.writeInt16LE(tile.column, entry);
    header.writeInt16LE(tile.row, entry + 2);
    header.writeUInt32LE(tile.buildings.length, entry + 4);
    header.writeUInt32LE(tile.roads.length, entry + 8);
    header.writeUInt32LE(offset, entry + 12);
    header.writeUInt32LE(compressed.length, entry + 16);
    blocks.push(compressed);
    offset += compressed.length;
    entry += 20;
}
writeFileSync(join(destination, 'city.bin'), Buffer.concat([header, ...blocks]));
console.log(`Wrote ${tiles.size} tiles, ${boroughs.reduce((a, b) => a + b)} mapped footprints and ${offset} archived bytes.`);
