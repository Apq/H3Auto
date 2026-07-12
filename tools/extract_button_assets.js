const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const projectRoot = path.resolve(__dirname, '..');
const imageDir = path.join(projectRoot, 'img');
const lodPath = process.argv[2]
  || 'D:/Heroes3/Heroes3_2026.05.01/Data/H3sprite.lod';

function extractLodEntry(entryName) {
  const lod = fs.readFileSync(lodPath);
  const count = lod.readUInt32LE(8);
  for (let i = 0; i < count; i++) {
    const entry = 0x5c + i * 32;
    const name = lod.subarray(entry, entry + 16).toString('ascii').replace(/\0.*$/, '');
    if (name.toLowerCase() !== entryName.toLowerCase()) continue;

    const offset = lod.readUInt32LE(entry + 16);
    const size = lod.readUInt32LE(entry + 20);
    const packedSize = lod.readUInt32LE(entry + 28);
    const data = lod.subarray(offset, offset + (packedSize || size));
    return packedSize ? zlib.inflateSync(data) : Buffer.from(data);
  }
  throw new Error(`LOD entry not found: ${entryName}`);
}

function decodeDef(def) {
  const palette = Array.from({ length: 256 }, (_, index) => [
    def[16 + index * 3],
    def[17 + index * 3],
    def[18 + index * 3],
  ]);
  const group = 16 + 768;
  const count = def.readUInt32LE(group + 4);
  const names = group + 16;
  const offsets = names + count * 13;
  const frames = [];

  for (let frameIndex = 0; frameIndex < count; frameIndex++) {
    const frameOffset = def.readUInt32LE(offsets + frameIndex * 4);
    const compression = def.readUInt32LE(frameOffset + 4);
    const fullWidth = def.readUInt32LE(frameOffset + 8);
    const fullHeight = def.readUInt32LE(frameOffset + 12);
    const width = def.readUInt32LE(frameOffset + 16);
    const height = def.readUInt32LE(frameOffset + 20);
    const left = def.readInt32LE(frameOffset + 24);
    const top = def.readInt32LE(frameOffset + 28);
    if (compression !== 1) throw new Error(`Unsupported DEF compression: ${compression}`);

    const indices = Buffer.alloc(fullWidth * fullHeight, 0);
    const frameData = frameOffset + 32;
    for (let y = 0; y < height; y++) {
      let cursor = frameData + def.readUInt32LE(frameData + y * 4);
      let x = 0;
      while (x < width) {
        const code = def[cursor++];
        const run = def[cursor++] + 1;
        if (code === 0xff) {
          for (let n = 0; n < run && x < width; n++, x++)
            indices[(top + y) * fullWidth + left + x] = def[cursor++];
        } else {
          for (let n = 0; n < run && x < width; n++, x++)
            indices[(top + y) * fullWidth + left + x] = code;
        }
      }
    }
    frames.push({ width: fullWidth, height: fullHeight, indices, palette });
  }
  return frames;
}

function encodePcxRun(bytes) {
  const output = [];
  for (let i = 0; i < bytes.length;) {
    const value = bytes[i];
    let run = 1;
    while (i + run < bytes.length && bytes[i + run] === value && run < 63) run++;
    if (run > 1 || (value & 0xc0) === 0xc0) output.push(0xc0 | run, value);
    else output.push(value);
    i += run;
  }
  return Buffer.from(output);
}

function writePcx(frame, outputName) {
  const { width, height, indices, palette } = frame;
  const bytesPerLine = (width + 1) & ~1;
  const header = Buffer.alloc(128, 0);
  header[0] = 0x0a;
  header[1] = 5;
  header[2] = 1;
  header[3] = 8;
  header.writeUInt16LE(width - 1, 8);
  header.writeUInt16LE(height - 1, 10);
  header.writeUInt16LE(72, 12);
  header.writeUInt16LE(72, 14);
  header[65] = 3;
  header.writeUInt16LE(bytesPerLine, 66);
  header.writeUInt16LE(1, 68);

  const rows = [];
  for (let y = 0; y < height; y++) {
    const planar = Buffer.alloc(bytesPerLine * 3, 0);
    for (let x = 0; x < width; x++) {
      const index = indices[y * width + x];
      const rgb = index < 8 ? [0, 255, 255] : palette[index];
      planar[x] = rgb[0];
      planar[bytesPerLine + x] = rgb[1];
      planar[bytesPerLine * 2 + x] = rgb[2];
    }
    rows.push(encodePcxRun(planar));
  }
  fs.writeFileSync(path.join(imageDir, outputName), Buffer.concat([header, ...rows]));
}

for (const [entry, prefix] of [['iOKAY.def', 'HA_ok'], ['iCANCEL.def', 'HA_cancel']]) {
  const frames = decodeDef(extractLodEntry(entry));
  writePcx(frames[0], `${prefix}_normal.pcx`);
  writePcx(frames[1], `${prefix}_pressed.pcx`);
  console.log(`${entry}: wrote normal and pressed ${frames[0].width}x${frames[0].height} frames`);
}
