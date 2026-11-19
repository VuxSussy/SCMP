// Browser state used by a few upstream scenes. The terminal canvas starts at column zero.
const SIDE = 0;
const root = { dataset: {} };

const skyStops = {
  moonlit:        [['#1a2a5e', '#0d1530', '#090f24'], .75, .10],
  'cloud-drift':  [['#4fa3e8', '#86c3f0', '#b6dbf6']],
  'honu-lagoon':  [['#35a6f0', '#1d7ad8', '#1650b2']],
  'koi-pond':     [['#4aa08c', '#2f8070', '#1f6156']],
  jellyfish:      [['#174784', '#0c2b58', '#071a3a']],
  'lantern-night':[['#070c1c', '#24305e', '#050a14']],
  fireflies:      [['#1b3522', '#0c1710', '#070c09'], .50, 1.10],
  'neon-rain':    [['#05070f', '#0b1024', '#34203f']],
  'sakura-dusk':  [['#17112b', '#2a1c44', '#8c4a5e']],
  'snow-cabin':   [['#060b18', '#101c38', '#1c2a4a']],
  'star-voyage':  [['#2a1a52', '#120d2e', '#08061a'], .85, .20],
};

const paletteCache = new Map();
const color = value => {
  if (!paletteCache.has(value)) paletteCache.set(value, parseInt(value.slice(1), 16) || 0);
  return paletteCache.get(value);
};

function interpolate(a, b, alpha) {
  const channel = shift => {
    const from = (a >>> shift) & 255, to = (b >>> shift) & 255;
    return Math.round(from + (to - from) * alpha);
  };
  return (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

function visWallpaper(name, cols, rows, t) {
  const scene = SCENES[name];
  if (!scene) return new Uint32Array();
  const data = new Uint32Array(cols * rows * 2);
  const [colors, centerX, centerY] = skyStops[name];
  const [a, b, c] = colors.map(color);
  const put = (x, y, cell, gx, gy, opacity = 1) => {
    if (!cell || x < 0 || x >= cols || y < 0 || y >= rows) return;
    const index = (y * cols + x) * 2;
    const alpha = Math.max(0, Math.min(1, (cell.a ?? 1) * opacity));
    if (alpha < .03) return;
    if (cell.bg) data[index] = interpolate(data[index], color(cell.bg), alpha);
    const ch = cell.ch ?? glyph(cell.bg ? GLYPHS : LOOSE, gx, gy, t);
    if (cell.bg || ch) {
      const fg = interpolate(data[index], cell.fg ? color(cell.fg) : data[index], cell.bg ? .8 : alpha);
      data[index + 1] = (((ch?.charCodeAt(0) || 32) & 127) << 24) | fg;
    }
  };
  for (let y = 0; y < rows; ++y) {
    for (let x = 0; x < cols; ++x) {
      const u = x / cols, v = y / rows;
      const depth = centerX === undefined ? v : Math.min(1, Math.hypot(u - centerX, v - centerY));
      const sky = depth < .5 ? interpolate(a, b, depth * 2) : interpolate(b, c, depth * 2 - 1);
      const index = (y * cols + x) * 2;
      data[index] = sky;
      data[index + 1] = (46 << 24) | interpolate(sky, 0xb4c8f0, .08);
      put(x, y, scene.cell?.(x, y, cols, rows, t), x, y);
    }
  }
  for (const sprite of scene.sprites?.(cols, rows, t) ?? []) {
    const ox = Math.round(sprite.x), oy = Math.round(sprite.y), opacity = sprite.a ?? 1;
    const width = sprite.art?.[0].length ?? sprite.w, height = sprite.art?.length ?? sprite.h;
    const key = (sprite.key ?? 0) * 997;
    if (sprite.glow) {
      const { r, a, color: glow } = sprite.glow;
      const cx = ox + width / 2, cy = oy + height / 2;
      for (let y = Math.max(0, Math.floor(cy - r)); y < Math.min(rows, cy + r); ++y) {
        for (let x = Math.max(0, Math.floor(cx - r)); x < Math.min(cols, cx + r); ++x) {
          const light = Math.max(0, 1 - Math.hypot(x - cx, y - cy) / r);
          put(x, y, { bg: glow, ch: '', a: light * light * a }, x, y);
        }
      }
    }
    if (sprite.halo && sprite.art) {
      for (const [x, y] of haloOf(sprite.art)) {
        put(ox + x, oy + y, { fg: sprite.halo, a: .3 }, key + x, y, opacity);
      }
    }
    for (let y = 0; y < height; ++y) {
      for (let x = 0; x < (sprite.art?.[y]?.length ?? width); ++x) {
        const texel = sprite.art ? null : sprite.field(x, y);
        const cell = sprite.art ? sprite.pal[sprite.art[y]?.[x]] : texel?.c;
        if (cell?.bg && sprite.shadow) {
          const shadow = typeof sprite.shadow === 'string' ? sprite.shadow : '#100c16';
          put(ox + x + 1, oy + y + 1, { bg: shadow, a: .4, ch: '' }, x, y, opacity);
        }
        put(ox + x, oy + y, cell, key + (texel?.sx ?? x), texel?.sy ?? y, opacity);
      }
    }
  }
  return data;
}
