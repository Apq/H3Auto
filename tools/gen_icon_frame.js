// Regenerate HA_icon_frame.pcx: a 1px gold border around a cyan-keyed
// transparent interior, sized to wrap the full 58x64 TwCrPort portrait.
// Output format matches the other HA_*.pcx panel assets: version 5,
// encoding 1 (RLE), 8 bits/plane, 3 planes (24-bit RGB).
const fs = require('fs');
const path = require('path');

const OUT = path.join(__dirname, '..', 'img', 'HA_icon_frame.pcx');
const W = 60;   // outer width  (inner hole 58)
const H = 66;   // outer height (inner hole 64)
const GOLD = [168, 141, 68];
const CYAN = [0, 255, 255]; // transparency key

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

const bytesPerLine = (W + 1) & ~1;
const header = Buffer.alloc(128, 0);
header[0] = 0x0a;
header[1] = 5;
header[2] = 1;
header[3] = 8;
header.writeUInt16LE(W - 1, 8);
header.writeUInt16LE(H - 1, 10);
header.writeUInt16LE(72, 12);
header.writeUInt16LE(72, 14);
header[65] = 3;
header.writeUInt16LE(bytesPerLine, 66);
header.writeUInt16LE(1, 68);

const rows = [];
for (let y = 0; y < H; y++) {
  const planar = Buffer.alloc(bytesPerLine * 3, 0);
  for (let x = 0; x < W; x++) {
    const border = (x === 0 || y === 0 || x === W - 1 || y === H - 1);
    const rgb = border ? GOLD : CYAN;
    planar[x] = rgb[0];
    planar[bytesPerLine + x] = rgb[1];
    planar[bytesPerLine * 2 + x] = rgb[2];
  }
  rows.push(encodePcxRun(planar));
}

fs.writeFileSync(OUT, Buffer.concat([header, ...rows]));
console.log(`wrote ${W}x${H} -> ${OUT}`);
