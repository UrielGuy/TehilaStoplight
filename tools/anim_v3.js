// Version 3 animation decoder for the browser (mirror of src/AnimV3.cpp and
// tools/anim_v3.py; must stay bit-exact with them).  ES2017, no dependencies.
// decodeV3(arrayBuffer) -> {w, h, frames: [Uint8Array(w*h)...], delays: [ms...], total}
// Pixel bytes: bits 0-5 = 0bRRGGBB, bit 6 = silhouette light.
function decodeV3(buf) {
  var d = new Uint8Array(buf);
  if (d.length < 20 || d[0] !== 83 || d[1] !== 65 || d[2] !== 78 || d[3] !== 3) throw new Error("not a v3 animation");
  var u16 = function (o) { return d[o] | (d[o + 1] << 8); };
  var u32 = function (o) { return (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)) >>> 0; };
  var w = u16(4), h = u16(6), n = u16(8), K = d[12], ctxset = d[13], flags = d[14], nkeys = u16(16), STATES = u16(18);
  var OFF_S = 16, OFF_R = 25, OFF_G = OFF_R + 768, OFF_B = OFF_G + 3072, OFF_L = OFF_B + 3072;
  if (STATES !== OFF_L + 512 || w === 0 || w > 128 || h === 0 || n === 0 || K === 0 || ctxset > 1) throw new Error("bad v3 header");
  var masked = (flags & 2) !== 0;   // only the circle (diameter 132) of each 128x128 tile is coded, the rest is black
  if ((flags & ~3) || (masked && (w !== 128 || h % 128))) throw new Error("bad v3 flags");
  var spanS = new Uint8Array(128), spanE = new Uint8Array(128);
  for (i = 0; i < 128; i++) {
    var dy = 2 * i - 127, r2 = 132 * 132 - dy * dy, xs = 0, xe = 128;
    if (r2 < 0) xs = xe = 0;
    while (xs < xe && (2 * xs - 127) * (2 * xs - 127) > r2) xs++;
    while (xe > xs && (2 * (xe - 1) - 127) * (2 * (xe - 1) - 127) > r2) xe--;
    spanS[i] = xs; spanE[i] = xe;
  }
  var delays = [], lens = [], total = 0, i;
  for (i = 0; i < n; i++) {
    var ms = Math.max(20, u16(20 + 6 * i));
    delays.push(ms); total += ms; lens.push(u32(22 + 6 * i));
  }
  var priorOff = 20 + 6 * n + 4 * nkeys;
  var prior = d.subarray(priorOff, priorOff + STATES);
  var off = u32(20 + 6 * n);   // first keyframe = first frame
  var st = new Uint16Array(STATES);
  var npx = w * h, frames = [], prev = null;
  var tf = new Uint8Array(npx), sf = new Uint8Array(npx);
  // range decoder state (all kept as unsigned 32-bit numbers)
  var pos = 0, end = 0, range = 0, code = 0;
  var byte = function () { return pos < end ? d[pos++] : 0; };
  var bit = function (si) {
    var s = st[si], p = s >> 4, cnt = s & 15, sh = cnt < 5 ? cnt + 1 : 5;
    var bound = ((range >>> 12) * p) >>> 0, b;
    if (code < bound) { range = bound; b = 0; p += (4080 - p) >> sh; }
    else { code = (code - bound) >>> 0; range = (range - bound) >>> 0; b = 1; p -= (p - 16) >> sh; }
    st[si] = (p << 4) | (cnt < 15 ? cnt + 1 : 15);
    while (range < 16777216) { range = (range << 8) >>> 0; code = ((code << 8) | byte()) >>> 0; }
    return b;
  };
  for (var f = 0; f < n; f++) {
    pos = off; end = off + lens[f]; off = end;
    var temporal = (byte() & 1) !== 0;
    if (temporal && (prev === null || f % K === 0)) throw new Error("bad temporal frame " + f);
    range = 0xFFFFFFFF; code = 0;
    for (i = 0; i < 5; i++) code = ((code << 8) | byte()) >>> 0;
    for (i = 0; i < STATES; i++) st[i] = (prior[i] << 8) | 3;
    var cur = new Uint8Array(npx);
    for (var y = 0; y < h; y++) {
      var row = y * w, up1 = row - w, up4 = row - 4 * w, x0 = 0, x1 = w;
      if (masked) {   // hidden pixels: black (cur is zero-filled), tf 0, sf 2, no decisions
        x0 = spanS[y & 127]; x1 = spanE[y & 127];
        tf.fill(0, row, row + w); sf.fill(2, row, row + w);
      }
      for (var x = x0; x < x1; x++) {
        var o = row + x;
        var l1 = x >= 1 ? cur[o - 1] : 0, u1 = y >= 1 ? cur[up1 + x] : 0;
        var l4 = x >= 4 ? cur[o - 4] : 0, u4 = y >= 4 ? cur[up4 + x] : 0;
        var tl = x ? tf[o - 1] : 0, tu = y ? tf[o - w] : 0;
        var sl = x ? sf[o - 1] : 0, su = y ? sf[o - w] : 0;
        var t = 0;
        if (temporal) {
          t = prev[o];
          var th = bit(tl * 8 + tu * 4 + (l1 === t ? 2 : 0) + (u1 === t ? 1 : 0));
          tf[o] = th;
          if (th) { cur[o] = t; sf[o] = 2; continue; }
        } else tf[o] = 0;
        if (l1 === u1 && l1 === l4 && l1 === u4) {
          var shit = bit(OFF_S + sl * 3 + su);
          sf[o] = shit;
          if (shit) { cur[o] = l1; continue; }
        } else sf[o] = 2;
        var n2, n3;
        if (ctxset === 0) { n2 = l4; n3 = u4; }
        else { n2 = (x >= 1 && y >= 1) ? cur[up1 + x - 1] : 0; n3 = (x + 1 < w && y >= 1) ? cur[up1 + x + 1] : 0; }
        var colour = 0, prevCh = 0;
        for (var ch = 0; ch < 3; ch++) {
          var shf = 4 - 2 * ch;
          var c = (((l1 >> shf) & 3) << 6) | (((u1 >> shf) & 3) << 4) | (((n2 >> shf) & 3) << 2) | ((n3 >> shf) & 3);
          var base = ch === 0 ? OFF_R + c * 3 : (ch === 1 ? OFF_G : OFF_B) + ((c << 2) | prevCh) * 3;
          var hi = bit(base), lo = bit(base + 1 + hi);
          prevCh = hi * 2 + lo;
          colour = (colour << 2) | prevCh;
        }
        var light = bit(OFF_L + ((l1 >> 6) & 1) * 256 + ((u1 >> 6) & 1) * 128 + (temporal ? (t >> 6) & 1 : 0) * 64 + colour);
        cur[o] = (light << 6) | colour;
      }
    }
    frames.push(cur);
    prev = cur;
  }
  return { w: w, h: h, frames: frames, delays: delays, total: total };
}
