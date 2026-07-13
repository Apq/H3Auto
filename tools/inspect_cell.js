const fs = require('fs');
const b = fs.readFileSync('D:/GitHub/H3/H3Auto/img/HA_cell.pcx');
const xmin = b.readUInt16LE(4), ymin = b.readUInt16LE(6);
const xmax = b.readUInt16LE(8), ymax = b.readUInt16LE(10);
const W = xmax - xmin + 1, H = ymax - ymin + 1;
const bpl = b.readUInt16LE(66), planes = b[65];
console.log('W', W, 'H', H, 'planes', planes, 'bpl', bpl, 'enc', b[2], 'bpp', b[3]);
const rawSize = bpl * planes * H;
const raw = Buffer.alloc(rawSize);
let sp = 128, op = 0;
while (op < rawSize && sp < b.length) {
  const m = b[sp++];
  if ((m & 0xc0) === 0xc0) { const c = m & 0x3f; const v = b[sp++]; for (let i = 0; i < c && op < rawSize; i++) raw[op++] = v; }
  else raw[op++] = m;
}
function px(x, y) {
  const base = y * bpl * planes;
  return [raw[base + x], raw[base + bpl + x], raw[base + bpl * 2 + x]];
}
console.log('TL', px(0,0), px(1,1), px(2,2));
console.log('TR', px(W-1,0), px(W-2,1));
console.log('BL', px(0,H-1), px(1,H-2));
console.log('top row0 x0..7:');
for (let x = 0; x < 8; x++) console.log('  ', x, px(x,0));
console.log('top row1 x0..7:');
for (let x = 0; x < 8; x++) console.log('  ', x, px(x,1));
console.log('left col y0..7 (x0,x1):');
for (let y = 0; y < 8; y++) console.log('  ', y, px(0,y), px(1,y));
console.log('interior', px(20,20));
