const fs = require('fs');
const path = require('path');
const b = fs.readFileSync(path.join(__dirname, '..', 'img', 'HA_icon_frame.pcx'));
const xmax = b.readUInt16LE(8), ymax = b.readUInt16LE(10);
const bpl = b.readUInt16LE(66), planes = b[65];
const W = xmax + 1, H = ymax + 1;
let p = 128;
const rows = [];
for (let y = 0; y < H; y++) {
  const total = bpl * planes;
  const line = Buffer.alloc(total);
  let i = 0;
  while (i < total) {
    let c = b[p++];
    if ((c & 0xc0) === 0xc0) {
      const run = c & 0x3f;
      const v = b[p++];
      for (let n = 0; n < run && i < total; n++) line[i++] = v;
    } else {
      line[i++] = c;
    }
  }
  rows.push(line);
}
function px(x, y) {
  const l = rows[y];
  return [l[x], l[bpl + x], l[bpl * 2 + x]];
}
console.log('W H', W, H);
// classify: cyan transparent (0,255,255) vs opaque border
function tag(rgb){ return (rgb[0]===0&&rgb[1]===255&&rgb[2]===255)?'.':'#'; }
// print full grid as . / #
for (let y = 0; y < H; y++) {
  let s = '';
  for (let x = 0; x < W; x++) s += tag(px(x, y));
  console.log(s);
}
// print sample border colors
console.log('corner00', px(0,0));
console.log('edgeTop mid', px(30,0), px(30,1), px(30,2));
console.log('left col', px(0,10), px(1,10), px(2,10));
