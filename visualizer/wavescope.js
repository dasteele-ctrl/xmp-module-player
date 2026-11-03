const wsModules = Object.create(null), wsCache = Object.create(null);
function wsRequire(id){if(wsCache[id])return wsCache[id]; const exports=wsCache[id]={}; wsModules[id](exports,wsRequire);return exports;}
wsModules["analyze"] = function(exports, require){


/**
 * The one analysis contract, shared by every producer of an AudioFrame so the
 * console (AudioEngine) and companion windows (PcmLink) can never drift apart.
 * This is the SAME recipe the native app unit-tests in `src/analysis.inl`:
 *
 *  - bands split at 250 Hz / 4 kHz, computed from the REAL sample rate (not a
 *    48 kHz assumption): mean byte / 255 over the bins in range;
 *  - level = RMS of the waveform, quarter-decimated, smoothed `+= (rms-l)*0.2`;
 *  - beat = bass energy spiking 1.35x above its own 0.04 moving average (and a
 *    0.08 floor) charges the envelope to 1; it decays `*= e^(-6*dt)`.
 *
 * Pure and stateful-by-argument: the caller owns the smoothing state so this
 * function stays a testable transform of (bytes, rate, state) -> frame.
 */

/** Per-source smoothing memory, owned by the caller across frames. */






function newAnalysisState()                {
  return { smoothLevel: 0, bassAvg: 0, beatEnv: 0 };
}

/**
 * Turn one analyser snapshot into an AudioFrame. `fft` is byte frequency data
 * (length = fftSize/2), `wave` is byte time-domain data (length = fftSize),
 * both as the Web Audio AnalyserNode produces them.
 */
function analyzeFrame(
  fft            ,
  wave            ,
  sampleRate        ,
  t        ,
  dt        ,
  st               ,
)             {
  const hzPerBin = sampleRate / 2 / fft.length;
  const band = (lo        , hi        ) => {
    const a = Math.max(0, Math.floor(lo / hzPerBin));
    const b = Math.min(fft.length - 1, Math.ceil(hi / hzPerBin));
    let sum = 0;
    for (let i = a; i <= b; i++) sum += fft[i];
    return sum / ((b - a + 1) * 255);
  };

  const bass = band(20, 250);
  const mid = band(250, 4000);
  const treble = band(4000, 16000);

  let rms = 0;
  for (let i = 0; i < wave.length; i += 4) {
    const s = (wave[i] - 128) / 128;
    rms += s * s;
  }
  rms = Math.sqrt(rms / (wave.length / 4));
  st.smoothLevel += (rms - st.smoothLevel) * 0.2;

  // Onset detection: bass energy spiking above its own moving average.
  st.bassAvg += (bass - st.bassAvg) * 0.04;
  if (bass > st.bassAvg * 1.35 && bass > 0.08) st.beatEnv = 1;
  st.beatEnv *= Math.exp(-dt * 6);

  return {
    fft,
    wave,
    level: st.smoothLevel,
    bass,
    mid,
    treble,
    beat: st.beatEnv,
    t,
    dt,
  };
}


Object.assign(exports,{newAnalysisState,analyzeFrame});
};
wsModules["modes/field"] = function(exports, require){

const { TAU, bin, makeScratchCanvas, noise2, offscreen, rng } = require("modes/util");

const fieldModes            = [
  {
    id: "plasma",
    name: "Plasma",
    family: "field",
    description: "A slow interference plasma that breathes with the level.",
    draw: (v) => {
      const { c, x } = offscreen(v, "pl", 160, 90);
      const img = x.createImageData(c.width, c.height);
      const t = v.f.t * (0.4 + v.f.level * 1.6);
      const boost = 0.5 + v.f.level * 1.6;
      // Palette lookup table once per frame (256 fillStyle parses are cheap,
      // but a LUT keeps the inner loop pure math).
      let lut = v.state.lut                                 ;
      if (!lut || v.state.lutPalette !== v.p.id) {
        lut = new Uint8ClampedArray(256 * 3);
        const probe = makeScratchCanvas(256, 1);
        const px = probe.getContext("2d") ;
        for (let i = 0; i < 256; i++) {
          px.fillStyle = v.p.color(i / 255, 1);
          px.fillRect(i, 0, 1, 1);
        }
        const d = px.getImageData(0, 0, 256, 1).data;
        for (let i = 0; i < 256; i++) {
          lut[i * 3] = d[i * 4];
          lut[i * 3 + 1] = d[i * 4 + 1];
          lut[i * 3 + 2] = d[i * 4 + 2];
        }
        v.state.lut = lut;
        v.state.lutPalette = v.p.id;
      }
      for (let j = 0; j < c.height; j++) {
        for (let i = 0; i < c.width; i++) {
          const u = i / c.width;
          const q = j / c.height;
          const s =
            Math.sin(u * 7 + t) +
            Math.sin((u + q) * 5 - t * 1.3) +
            Math.sin(Math.hypot(u - 0.5, q - 0.5) * 14 - t * 2);
          const m = Math.max(0, Math.min(1, (s / 3 + 0.5) * boost));
          const k = (j * c.width + i) * 4;
          const li = Math.floor(m * 255) * 3;
          img.data[k] = lut[li];
          img.data[k + 1] = lut[li + 1];
          img.data[k + 2] = lut[li + 2];
          img.data[k + 3] = 255;
        }
      }
      x.putImageData(img, 0, 0);
      v.ctx.imageSmoothingEnabled = true;
      v.ctx.drawImage(c, 0, 0, v.w, v.h);
    },
  },
  {
    id: "interference",
    name: "Interference",
    family: "field",
    description: "Ripple sources tuned to bass, mid, and treble collide.",
    fade: 0.5,
    draw: (v) => {
      const sources = [
        { px: 0.3, py: 0.4, e: v.f.bass },
        { px: 0.7, py: 0.35, e: v.f.mid },
        { px: 0.5, py: 0.7, e: v.f.treble },
      ];
      v.ctx.lineWidth = Math.max(1.5, v.h / 500);
      for (let s = 0; s < sources.length; s++) {
        const src = sources[s];
        const cx = src.px * v.w;
        const cy = src.py * v.h;
        const count = 9;
        for (let i = 0; i < count; i++) {
          const phase = (v.f.t * (0.3 + src.e * 1.2) + i / count) % 1;
          const r = phase * Math.min(v.w, v.h) * 0.6;
          v.ctx.strokeStyle = v.p.color(s / 2.2, (1 - phase) * (0.15 + src.e));
          v.ctx.beginPath();
          v.ctx.arc(cx, cy, r, 0, TAU);
          v.ctx.stroke();
        }
      }
    },
  },
  {
    id: "flow-field",
    name: "Flow field",
    family: "field",
    description: "A thousand grains drifting through wind made of noise.",
    fade: 0.055,
    draw: (v) => {
      const r = rng(41);





      let grains = v.state.grains                   ;
      if (!grains) {
        grains = Array.from({ length: 900 }, () => ({
          x: r(),
          y: r(),
          hue: r(),
        }));
        v.state.grains = grains;
      }
      const scale = 3.2;
      const speed = (0.04 + v.f.level * 0.35 + v.f.beat * 0.1) * v.f.dt * 60;
      for (const g of grains) {
        const a =
          noise2(g.x * scale, g.y * scale + v.f.t * 0.12) * TAU * 2 +
          v.f.mid * 3;
        g.x += (Math.cos(a) * speed) / 100;
        g.y += (Math.sin(a) * speed) / 100;
        if (g.x < 0 || g.x > 1 || g.y < 0 || g.y > 1) {
          g.x = Math.random();
          g.y = Math.random();
        }
        v.ctx.fillStyle = v.p.color(g.hue, 0.55);
        const size = Math.max(1, v.h / 720);
        v.ctx.fillRect(g.x * v.w, g.y * v.h, size, size);
      }
    },
  },
  {
    id: "aurora",
    name: "Aurora",
    family: "field",
    description: "Translucent curtains of light bending with the mids.",
    draw: (v) => {
      const layers = 5;
      for (let l = 0; l < layers; l++) {
        const q = l / (layers - 1);
        const yBase = v.h * (0.25 + q * 0.4);
        const amp = v.h * (0.06 + v.f.mid * 0.22) * (1 - q * 0.4);
        v.ctx.beginPath();
        v.ctx.moveTo(0, v.h);
        for (let i = 0; i <= 90; i++) {
          const u = i / 90;
          const y =
            yBase +
            Math.sin(u * 5 + v.f.t * (0.5 + l * 0.17)) * amp +
            Math.sin(u * 13 - v.f.t * 0.9) * amp * 0.35 +
            bin(v.f.fft, u) * -v.h * 0.12;
          v.ctx.lineTo(u * v.w, y);
        }
        v.ctx.lineTo(v.w, v.h);
        v.ctx.closePath();
        v.ctx.fillStyle = v.p.color(0.25 + q * 0.6, 0.16 + v.f.level * 0.2);
        v.ctx.fill();
      }
    },
  },
  {
    id: "rain",
    name: "Signal rain",
    family: "field",
    description: "Columns of glyph-like dashes falling at band speed.",
    fade: 0.22,
    draw: (v) => {
      const cols = Math.max(24, Math.min(72, Math.floor(v.w / 26)));
      let drops = v.state.drops                            ;
      if (!drops || drops.length !== cols) {
        drops = new Float32Array(cols);
        for (let i = 0; i < cols; i++) drops[i] = Math.random();
        v.state.drops = drops;
      }
      const cw = v.w / cols;
      const dash = Math.max(6, v.h / 60);
      for (let i = 0; i < cols; i++) {
        const e = bin(v.f.fft, i / cols);
        drops[i] += v.f.dt * (0.08 + e * 1.4);
        if (drops[i] > 1.2) drops[i] = -0.2;
        const y = drops[i] * v.h;
        for (let k = 0; k < 7; k++) {
          const yy = y - k * dash * 1.6;
          if (yy < -dash || yy > v.h) continue;
          v.ctx.fillStyle = v.p.color(e, (1 - k / 7) * (0.3 + e * 0.7));
          v.ctx.fillRect(i * cw + cw * 0.35, yy, cw * 0.3, dash);
        }
      }
    },
  },
  {
    id: "pulse-rings",
    flash: true,
    name: "Pulse rings",
    family: "field",
    description: "Every onset drops a stone in the pond.",
    fade: 0.09,
    draw: (v) => {







      let rings = v.state.pr                      ;
      if (!rings) {
        rings = [];
        v.state.pr = rings;
      }
      const wasBeat = (v.state.wasBeat           ) ?? false;
      if (v.f.beat > 0.7 && !wasBeat && rings.length < 24) {
        rings.push({
          x: v.w * (0.2 + Math.random() * 0.6),
          y: v.h * (0.2 + Math.random() * 0.6),
          r: 0,
          life: 1,
          hue: Math.random(),
        });
      }
      v.state.wasBeat = v.f.beat > 0.7;
      const grow = Math.min(v.w, v.h) * 0.55;
      for (let i = rings.length - 1; i >= 0; i--) {
        const ring = rings[i];
        ring.life -= v.f.dt * 0.5;
        ring.r += grow * v.f.dt;
        if (ring.life <= 0) {
          rings.splice(i, 1);
          continue;
        }
        v.ctx.lineWidth = Math.max(2, v.h / 260) * ring.life;
        v.ctx.strokeStyle = v.p.color(ring.hue, ring.life * 0.9);
        v.ctx.beginPath();
        v.ctx.arc(ring.x, ring.y, ring.r, 0, TAU);
        v.ctx.stroke();
      }
      // A quiet baseline so silence still shows a living surface.
      const cx = v.w / 2;
      const cy = v.h / 2;
      v.ctx.strokeStyle = v.p.color(0.5, 0.2 + v.f.level * 0.5);
      v.ctx.lineWidth = Math.max(1.5, v.h / 400);
      v.ctx.beginPath();
      v.ctx.arc(cx, cy, Math.min(v.w, v.h) * (0.05 + v.f.bass * 0.12), 0, TAU);
      v.ctx.stroke();
    },
  },
  {
    id: "sunburst",
    name: "Sunburst",
    family: "field",
    description: "Rays sweeping from the center, widened by the bass.",
    fade: 0.16,
    draw: (v) => {
      const rays = 48;
      const cx = v.w / 2;
      const cy = v.h / 2;
      const R = Math.hypot(v.w, v.h) * 0.6;
      for (let i = 0; i < rays; i++) {
        const q = i / rays;
        const e = bin(v.f.fft, q);
        const a = q * TAU + v.f.t * 0.1;
        const width = (TAU / rays) * (0.15 + v.f.bass * 0.5 + e * 0.3);
        v.ctx.fillStyle = v.p.color(q, 0.12 + e * 0.55);
        v.ctx.beginPath();
        v.ctx.moveTo(cx, cy);
        v.ctx.arc(cx, cy, R * (0.3 + e * 0.7), a - width / 2, a + width / 2);
        v.ctx.closePath();
        v.ctx.fill();
      }
    },
  },
];


Object.assign(exports,{fieldModes});
};
wsModules["modes/geometry"] = function(exports, require){

const { TAU, bands } = require("modes/util");

const geometryModes            = [
  {
    id: "polygon-morph",
    name: "Polygon morph",
    family: "geometry",
    description: "Nested polygons that grow sides as the mids open up.",
    fade: 0.22,
    draw: (v) => {
      const cx = v.w / 2;
      const cy = v.h / 2;
      const R = Math.min(v.w, v.h) * 0.4;
      const sides = 3 + Math.round(v.f.mid * 6);
      for (let ring = 6; ring >= 1; ring--) {
        const rr = R * (ring / 6) * (1 + v.f.beat * 0.12);
        const rot = v.f.t * 0.25 * (ring % 2 ? 1 : -1);
        v.ctx.strokeStyle = v.p.color(ring / 6, 0.75);
        v.ctx.lineWidth = Math.max(1.5, R / 110);
        v.ctx.beginPath();
        for (let i = 0; i <= sides; i++) {
          const a = (i / sides) * TAU + rot;
          const x = cx + Math.cos(a) * rr;
          const y = cy + Math.sin(a) * rr;
          i === 0 ? v.ctx.moveTo(x, y) : v.ctx.lineTo(x, y);
        }
        v.ctx.stroke();
      }
    },
  },
  {
    id: "tunnel",
    flash: true,
    name: "Tunnel",
    family: "geometry",
    description: "Concentric rings rushing outward, kicked by the drums.",
    fade: 0.25,
    draw: (v) => {
      let rings = v.state.rings                        ;
      if (!rings) {
        rings = [0.1, 0.3, 0.5, 0.7, 0.9];
        v.state.rings = rings;
      }
      const cx = v.w / 2;
      const cy = v.h / 2;
      const R = Math.hypot(v.w, v.h) / 2;
      const speed = 0.12 + v.f.level * 0.7 + v.f.beat * 0.4;
      v.ctx.lineWidth = Math.max(2, v.h / 300);
      for (let i = 0; i < rings.length; i++) {
        rings[i] += v.f.dt * speed;
        if (rings[i] > 1) rings[i] -= 1;
        const e = rings[i] ** 2.2;
        v.ctx.strokeStyle = v.p.color(1 - rings[i], Math.min(1, e * 3));
        v.ctx.beginPath();
        const squash = 0.75 + v.f.bass * 0.2;
        v.ctx.ellipse(cx, cy, e * R, e * R * squash, v.f.t * 0.1, 0, TAU);
        v.ctx.stroke();
      }
    },
  },
  {
    id: "kaleido",
    name: "Kaleidoscope",
    family: "geometry",
    description: "One waveform arm mirrored into a turning mandala.",
    fade: 0.14,
    draw: (v) => {
      const seg = 10;
      const cx = v.w / 2;
      const cy = v.h / 2;
      const R = Math.min(v.w, v.h) * 0.46;
      const w = v.f.wave;
      v.ctx.lineWidth = Math.max(1.5, R / 200);
      for (let s = 0; s < seg; s++) {
        v.ctx.save();
        v.ctx.translate(cx, cy);
        v.ctx.rotate((s / seg) * TAU + v.f.t * 0.12);
        if (s % 2) v.ctx.scale(1, -1);
        v.ctx.strokeStyle = v.p.color(s / seg, 0.8);
        v.ctx.beginPath();
        for (let i = 0; i < 64; i++) {
          const q = i / 63;
          const sample = (w[Math.floor(q * (w.length - 1))] - 128) / 128;
          const x = q * R;
          const y = sample * R * 0.3 * Math.sin(q * Math.PI);
          i === 0 ? v.ctx.moveTo(x, y) : v.ctx.lineTo(x, y);
        }
        v.ctx.stroke();
        v.ctx.restore();
      }
    },
  },
  {
    id: "spiro",
    name: "Spirograph",
    family: "geometry",
    description: "Hypotrochoid curves whose gears are tuned by the music.",
    fade: 0.06,
    draw: (v) => {
      const cx = v.w / 2;
      const cy = v.h / 2;
      const R = Math.min(v.w, v.h) * 0.38;
      const k = 0.25 + v.f.mid * 0.5;
      const l = 0.5 + v.f.treble * 0.45;
      const t0 = v.f.t * (0.8 + v.f.level * 2);
      v.ctx.strokeStyle = v.p.color((v.f.t * 0.06) % 1, 0.85);
      v.ctx.lineWidth = Math.max(1.5, R / 220);
      v.ctx.beginPath();
      for (let i = 0; i <= 140; i++) {
        const t = t0 + i * 0.045;
        const x =
          cx + R * ((1 - k) * Math.cos(t) + l * k * Math.cos(((1 - k) / k) * t));
        const y =
          cy + R * ((1 - k) * Math.sin(t) - l * k * Math.sin(((1 - k) / k) * t));
        i === 0 ? v.ctx.moveTo(x, y) : v.ctx.lineTo(x, y);
      }
      v.ctx.stroke();
    },
  },
  {
    id: "shards",
    flash: true,
    name: "Shards",
    family: "geometry",
    description: "Glass shards blasted outward on every kick drum.",
    fade: 0.13,
    draw: (v) => {










      let shards = v.state.shards                       ;
      if (!shards) {
        shards = [];
        v.state.shards = shards;
      }
      if (v.f.beat > 0.7 && shards.length < 140) {
        for (let k = 0; k < 16; k++) {
          shards.push({
            a: Math.random() * TAU,
            d: Math.min(v.w, v.h) * 0.05,
            vd: (0.25 + Math.random() * 0.5) * Math.min(v.w, v.h),
            rot: Math.random() * TAU,
            vr: (Math.random() - 0.5) * 6,
            size: (0.02 + Math.random() * 0.045) * Math.min(v.w, v.h),
            life: 1,
            hue: Math.random(),
          });
        }
      }
      const cx = v.w / 2;
      const cy = v.h / 2;
      for (let i = shards.length - 1; i >= 0; i--) {
        const s = shards[i];
        s.life -= v.f.dt * 0.7;
        if (s.life <= 0) {
          shards.splice(i, 1);
          continue;
        }
        s.d += s.vd * v.f.dt;
        s.rot += s.vr * v.f.dt;
        const x = cx + Math.cos(s.a) * s.d;
        const y = cy + Math.sin(s.a) * s.d;
        v.ctx.save();
        v.ctx.translate(x, y);
        v.ctx.rotate(s.rot);
        v.ctx.strokeStyle = v.p.color(s.hue, s.life);
        v.ctx.lineWidth = Math.max(1.5, v.h / 500);
        v.ctx.beginPath();
        v.ctx.moveTo(-s.size / 2, s.size / 2);
        v.ctx.lineTo(0, -s.size / 2);
        v.ctx.lineTo(s.size / 2, s.size / 2);
        v.ctx.closePath();
        v.ctx.stroke();
        v.ctx.restore();
      }
      // Idle hint so the mode never renders empty before the first beat.
      v.ctx.fillStyle = v.p.color(0.5, 0.25 + v.f.level);
      v.ctx.beginPath();
      v.ctx.arc(cx, cy, Math.min(v.w, v.h) * 0.012 * (1 + v.f.bass * 2), 0, TAU);
      v.ctx.fill();
    },
  },
  {
    id: "lattice",
    name: "Lattice",
    family: "geometry",
    description: "A rotating grid of struts that stretch with the spectrum.",
    fade: 0.3,
    draw: (v) => {
      const b = bands(v, 12);
      const cx = v.w / 2;
      const cy = v.h / 2;
      const R = Math.min(v.w, v.h) * 0.5;
      const rows = 12;
      v.ctx.save();
      v.ctx.translate(cx, cy);
      v.ctx.rotate(v.f.t * 0.08);
      v.ctx.lineWidth = Math.max(1.2, R / 260);
      for (let i = 0; i < rows; i++) {
        const q = i / (rows - 1);
        const y = (q - 0.5) * R * 1.6;
        const stretch = 0.5 + b[i] * 0.9;
        v.ctx.strokeStyle = v.p.color(q, 0.55 + b[i] * 0.45);
        v.ctx.beginPath();
        v.ctx.moveTo(-R * stretch, y);
        v.ctx.lineTo(R * stretch, y);
        v.ctx.stroke();
        v.ctx.beginPath();
        v.ctx.moveTo(y, -R * stretch);
        v.ctx.lineTo(y, R * stretch);
        v.ctx.stroke();
      }
      v.ctx.restore();
    },
  },
];


Object.assign(exports,{geometryModes});
};
wsModules["modes/index"] = function(exports, require){

const { spectrumModes } = require("modes/spectrum");
const { waveformModes } = require("modes/waveform");
const { particleModes } = require("modes/particles");
const { geometryModes } = require("modes/geometry");
const { fieldModes } = require("modes/field");

/** All built-in visualizers, grouped by family, stable order. */
const MODES            = [
  ...spectrumModes,
  ...waveformModes,
  ...particleModes,
  ...geometryModes,
  ...fieldModes,
];

const DEFAULT_MODE = MODES.find((m) => m.id === "radial-bars") ?? MODES[0];

function modeById(id                           )          {
  return MODES.find((m) => m.id === id) ?? DEFAULT_MODE;
}

const FAMILIES = [
  { id: "spectrum", name: "Spectrum" },
  { id: "waveform", name: "Waveform" },
  { id: "particles", name: "Particles" },
  { id: "geometry", name: "Geometry" },
  { id: "field", name: "Fields" },
]         ;


Object.assign(exports,{MODES,DEFAULT_MODE,modeById,FAMILIES});
};
wsModules["modes/particles"] = function(exports, require){

const { TAU, rng } = require("modes/util");











function pool(v          , key        , n        , init                  )      {
  let arr = v.state[key]                   ;
  if (!arr || arr.length !== n) {
    arr = Array.from({ length: n }, (_, i) => init(i));
    v.state[key] = arr;
  }
  return arr;
}

const particleModes            = [
  {
    id: "nebula",
    name: "Nebula",
    family: "particles",
    description: "A cloud of motes orbiting the center, swelling with the bass.",
    fade: 0.12,
    draw: (v) => {
      const r = rng(7);
      const n = 420;
      const ps = pool(v, "nb", n, (i) => ({
        x: r() * TAU,
        y: 0.25 + r() * 0.75,
        vx: 0.2 + r() * 0.8,
        vy: 0,
        life: 0,
        hue: i / n,
        size: 1 + r() * 2.2,
      }));
      const cx = v.w / 2;
      const cy = v.h / 2;
      const R = Math.min(v.w, v.h) * 0.44;
      for (const p of ps) {
        p.x += v.f.dt * p.vx * (0.25 + v.f.mid * 2.2);
        const swell = 1 + v.f.bass * 0.5 + v.f.beat * 0.25;
        const rr = p.y * R * swell;
        const x = cx + Math.cos(p.x) * rr;
        const y = cy + Math.sin(p.x) * rr * 0.72;
        v.ctx.fillStyle = v.p.color(p.hue, 0.35 + v.f.level);
        v.ctx.beginPath();
        v.ctx.arc(x, y, p.size * (v.h / 900) * (1 + v.f.beat), 0, TAU);
        v.ctx.fill();
      }
    },
  },
  {
    id: "fountain",
    flash: true,
    name: "Fountain",
    family: "particles",
    description: "Sparks launched on every beat, falling back under gravity.",
    fade: 0.16,
    draw: (v) => {
      const r = rng(11);
      const ps = pool(v, "ft", 500, () => ({
        x: 0,
        y: 0,
        vx: 0,
        vy: 0,
        life: 0,
        hue: 0,
        size: 1 + r() * 2,
      }));
      let cursor = (v.state.cursor          ) ?? 0;
      const burst = Math.floor(v.f.beat > 0.6 ? 26 : v.f.level * 6);
      for (let k = 0; k < burst; k++) {
        const p = ps[cursor % ps.length];
        cursor++;
        p.x = v.w / 2 + (Math.random() - 0.5) * v.w * 0.08;
        p.y = v.h * 0.92;
        const a = -Math.PI / 2 + (Math.random() - 0.5) * 0.9;
        const speed = (0.55 + Math.random() * 0.6) * v.h * (0.9 + v.f.bass);
        p.vx = Math.cos(a) * speed;
        p.vy = Math.sin(a) * speed;
        p.life = 1;
        p.hue = Math.random();
      }
      v.state.cursor = cursor;
      for (const p of ps) {
        if (p.life <= 0) continue;
        p.life -= v.f.dt * 0.55;
        p.vy += v.h * 1.1 * v.f.dt;
        p.x += p.vx * v.f.dt;
        p.y += p.vy * v.f.dt;
        v.ctx.fillStyle = v.p.color(p.hue, Math.max(0, p.life));
        v.ctx.beginPath();
        v.ctx.arc(p.x, p.y, p.size * (v.h / 800), 0, TAU);
        v.ctx.fill();
      }
    },
  },
  {
    id: "starfield",
    flash: true,
    name: "Starfield",
    family: "particles",
    description: "Flying through stars: the music sets the warp speed.",
    fade: 0.3,
    draw: (v) => {
      const r = rng(23);
      const ps = pool(v, "sf", 420, () => ({
        x: (r() - 0.5) * 2,
        y: (r() - 0.5) * 2,
        vx: 0,
        vy: 0,
        life: 0.05 + r() * 0.95,
        hue: r(),
        size: 0.6 + r() * 1.6,
      }));
      const cx = v.w / 2;
      const cy = v.h / 2;
      const speed = 0.12 + v.f.level * 2.4 + v.f.beat * 0.8;
      for (const p of ps) {
        p.life -= v.f.dt * speed;
        if (p.life <= 0.02) {
          p.x = (Math.random() - 0.5) * 2;
          p.y = (Math.random() - 0.5) * 2;
          p.life = 1;
        }
        const z = p.life;
        const px = cx + (p.x / z) * cx * 0.9;
        const py = cy + (p.y / z) * cy * 0.9;
        const pz = Math.min(1.5, (1 - z) * 2);
        v.ctx.fillStyle = v.p.color(p.hue, Math.min(1, pz));
        v.ctx.beginPath();
        v.ctx.arc(px, py, p.size * pz * (v.h / 700), 0, TAU);
        v.ctx.fill();
      }
    },
  },
  {
    id: "swarm",
    name: "Swarm",
    family: "particles",
    description: "A flock chasing your cursor, agitated by the treble.",
    fade: 0.14,
    draw: (v) => {
      const r = rng(31);
      const ps = pool(v, "sw", 260, () => ({
        x: r() * v.w,
        y: r() * v.h,
        vx: 0,
        vy: 0,
        life: 0,
        hue: r(),
        size: 1 + r() * 1.8,
      }));
      const tx = v.pointer?.x ?? v.w / 2 + Math.cos(v.f.t * 0.6) * v.w * 0.22;
      const ty = v.pointer?.y ?? v.h / 2 + Math.sin(v.f.t * 0.83) * v.h * 0.22;
      const agitation = 40 + v.f.treble * 900;
      for (const p of ps) {
        p.vx += ((tx - p.x) * 1.1 + (Math.random() - 0.5) * agitation) * v.f.dt;
        p.vy += ((ty - p.y) * 1.1 + (Math.random() - 0.5) * agitation) * v.f.dt;
        p.vx *= 0.985;
        p.vy *= 0.985;
        p.x += p.vx * v.f.dt * 3;
        p.y += p.vy * v.f.dt * 3;
        v.ctx.fillStyle = v.p.color(p.hue, 0.6);
        v.ctx.beginPath();
        v.ctx.arc(p.x, p.y, p.size * (v.h / 900) * (1 + v.f.beat * 0.8), 0, TAU);
        v.ctx.fill();
      }
    },
  },
  {
    id: "orbitals",
    name: "Orbitals",
    family: "particles",
    description: "Electron shells: each ring rides its own frequency band.",
    fade: 0.2,
    draw: (v) => {
      const shells = 7;
      const cx = v.w / 2;
      const cy = v.h / 2;
      const R = Math.min(v.w, v.h) * 0.46;
      for (let s = 0; s < shells; s++) {
        const idx = Math.floor((s / shells) * v.f.fft.length * 0.5);
        const e = v.f.fft[idx] / 255;
        const r0 = R * ((s + 1) / shells);
        const count = 6 + s * 4;
        for (let i = 0; i < count; i++) {
          const a = (i / count) * TAU + v.f.t * (0.3 + s * 0.12) * (s % 2 ? -1 : 1);
          const wob = 1 + e * 0.18 * Math.sin(a * 5 + v.f.t * 3);
          v.ctx.fillStyle = v.p.color(s / shells, 0.3 + e * 0.7);
          v.ctx.beginPath();
          v.ctx.arc(
            cx + Math.cos(a) * r0 * wob,
            cy + Math.sin(a) * r0 * wob * 0.85,
            (v.h / 340) * (0.5 + e * 1.6),
            0,
            TAU,
          );
          v.ctx.fill();
        }
      }
    },
  },
  {
    id: "comets",
    name: "Comets",
    family: "particles",
    description: "A few bright comets tracing curves, faster when it's loud.",
    fade: 0.045,
    draw: (v) => {
      const n = 7;
      for (let i = 0; i < n; i++) {
        const speed = 0.35 + v.f.level * 1.8;
        const t = v.f.t * speed + (i * TAU) / n;
        const x =
          v.w / 2 + Math.sin(t * 1.3 + i) * Math.cos(t * 0.7) * v.w * 0.4;
        const y =
          v.h / 2 + Math.cos(t * 1.7 + i * 2) * Math.sin(t * 0.9) * v.h * 0.4;
        v.ctx.fillStyle = v.p.color(i / n, 0.95);
        v.ctx.beginPath();
        v.ctx.arc(x, y, (v.h / 300) * (1 + v.f.beat), 0, TAU);
        v.ctx.fill();
      }
    },
  },
];


Object.assign(exports,{particleModes});
};
wsModules["modes/spectrum"] = function(exports, require){

const { TAU, bands, bin, offscreen } = require("modes/util");

const spectrumModes            = [
  {
    id: "bars",
    name: "Bars",
    family: "spectrum",
    description: "The classic: log-spaced frequency bars with soft decay.",
    draw: (v) => {
      const n = Math.max(24, Math.min(96, Math.floor(v.w / 28)));
      const b = bands(v, n);
      const gap = v.w / n / 6;
      const bw = v.w / n - gap;
      for (let i = 0; i < n; i++) {
        const h = b[i] * v.h * 0.85;
        v.ctx.fillStyle = v.p.color(i / n, 0.95);
        v.ctx.fillRect(i * (bw + gap) + gap / 2, v.h - h, bw, h);
      }
    },
  },
  {
    id: "mirror-bars",
    name: "Mirror bars",
    family: "spectrum",
    description: "Bars unfolding from the horizontal centerline, symmetric.",
    draw: (v) => {
      const n = Math.max(32, Math.min(128, Math.floor(v.w / 20)));
      const b = bands(v, n);
      const bw = v.w / n;
      for (let i = 0; i < n; i++) {
        const h = (b[i] * v.h * 0.45) ** 1.02;
        v.ctx.fillStyle = v.p.color(b[i], 0.9);
        v.ctx.fillRect(i * bw + 1, v.h / 2 - h, bw - 2, h * 2);
      }
    },
  },
  {
    id: "radial-bars",
    name: "Radial bars",
    family: "spectrum",
    description: "The spectrum wrapped into a slowly rotating ring.",
    draw: (v) => {
      const n = 96;
      const b = bands(v, n);
      const cx = v.w / 2;
      const cy = v.h / 2;
      const r0 = Math.min(v.w, v.h) * (0.18 + v.f.bass * 0.05);
      const rot = v.f.t * 0.15;
      v.ctx.lineWidth = Math.max(2, (TAU * r0) / n / 2.4);
      for (let i = 0; i < n; i++) {
        const a = (i / n) * TAU + rot;
        const len = b[i] * Math.min(v.w, v.h) * 0.3;
        v.ctx.strokeStyle = v.p.color(i / n, 0.95);
        v.ctx.beginPath();
        v.ctx.moveTo(cx + Math.cos(a) * r0, cy + Math.sin(a) * r0);
        v.ctx.lineTo(cx + Math.cos(a) * (r0 + len), cy + Math.sin(a) * (r0 + len));
        v.ctx.stroke();
      }
    },
  },
  {
    id: "peak-ladder",
    name: "Peak ladder",
    family: "spectrum",
    description: "Bars with falling peak caps, like a rack meter bridge.",
    draw: (v) => {
      const n = 48;
      const b = bands(v, n);
      let peaks = v.state.peaks                            ;
      if (!peaks || peaks.length !== n) {
        peaks = new Float32Array(n);
        v.state.peaks = peaks;
      }
      const bw = v.w / n;
      const seg = Math.max(6, v.h / 48);
      for (let i = 0; i < n; i++) {
        peaks[i] = Math.max(peaks[i] - v.f.dt * 0.25, b[i]);
        const segs = Math.floor((b[i] * v.h * 0.85) / seg);
        for (let s = 0; s < segs; s++) {
          v.ctx.fillStyle = v.p.color(s / (v.h / seg), 0.9);
          v.ctx.fillRect(i * bw + 2, v.h - (s + 1) * seg + 1, bw - 4, seg - 2);
        }
        const py = v.h - peaks[i] * v.h * 0.85;
        v.ctx.fillStyle = v.p.color(1, 1);
        v.ctx.fillRect(i * bw + 2, py, bw - 4, Math.max(2, seg / 4));
      }
    },
  },
  {
    id: "ribbon",
    name: "Ribbon",
    family: "spectrum",
    description: "A smooth filled spectrum curve, drawn like a mountain ridge.",
    draw: (v) => {
      const n = 128;
      const b = bands(v, n, 0.5);
      for (let layer = 2; layer >= 0; layer--) {
        const scale = 0.9 - layer * 0.22;
        v.ctx.beginPath();
        v.ctx.moveTo(0, v.h);
        for (let i = 0; i < n; i++) {
          const x = (i / (n - 1)) * v.w;
          const y = v.h - b[i] * v.h * scale;
          v.ctx.lineTo(x, y);
        }
        v.ctx.lineTo(v.w, v.h);
        v.ctx.closePath();
        v.ctx.fillStyle = v.p.color(layer / 2.5, 0.35 + layer * 0.15);
        v.ctx.fill();
      }
    },
  },
  {
    id: "city",
    name: "Skyline",
    family: "spectrum",
    description: "Wide frequency blocks stacked like a night skyline.",
    draw: (v) => {
      const n = 20;
      const b = bands(v, n, 0.55);
      const bw = v.w / n;
      for (let i = 0; i < n; i++) {
        const h = b[i] * v.h * 0.8;
        const x = i * bw;
        v.ctx.fillStyle = v.p.color(i / n, 0.25);
        v.ctx.fillRect(x + 3, v.h - h, bw - 6, h);
        v.ctx.fillStyle = v.p.color(i / n, 1);
        v.ctx.fillRect(x + 3, v.h - h, bw - 6, Math.max(3, v.h * 0.006));
        // Lit windows climb with the level.
        const rows = Math.floor(h / (v.h * 0.04));
        for (let r = 0; r < rows; r += 2) {
          v.ctx.fillStyle = v.p.color(1 - i / n, 0.5);
          v.ctx.fillRect(x + bw * 0.25, v.h - h + r * v.h * 0.04 + 6, bw * 0.1, 4);
          v.ctx.fillRect(x + bw * 0.6, v.h - h + r * v.h * 0.04 + 6, bw * 0.1, 4);
        }
      }
    },
  },
  {
    id: "waterfall",
    name: "Waterfall",
    family: "spectrum",
    description: "A scrolling spectrogram: time flows down, pitch runs across.",
    draw: (v) => {
      const { c, x } = offscreen(v, "wf", 512, 288);
      // Scroll history down one row.
      x.drawImage(c, 0, 1);
      for (let i = 0; i < c.width; i++) {
        const m = bin(v.f.fft, i / c.width);
        x.fillStyle = m < 0.02 ? v.p.bg : v.p.color(m, Math.min(1, 0.15 + m));
        x.fillRect(i, 0, 1, 1);
      }
      v.ctx.imageSmoothingEnabled = true;
      v.ctx.drawImage(c, 0, 0, v.w, v.h);
    },
  },
  {
    id: "dual-arc",
    name: "Dual arc",
    family: "spectrum",
    description: "Two mirrored spectrum fans opening like a moth's wings.",
    draw: (v) => {
      const n = 64;
      const b = bands(v, n);
      const cx = v.w / 2;
      const cy = v.h * 0.55;
      const R = Math.min(v.w, v.h) * 0.42;
      v.ctx.lineWidth = Math.max(2, R / 90);
      for (let i = 0; i < n; i++) {
        const spread = (i / n) * Math.PI * 0.85 + Math.PI * 0.075;
        const len = R * (0.25 + b[i] * 0.75);
        for (const dir of [-1, 1]) {
          const a = Math.PI / 2 + dir * spread;
          v.ctx.strokeStyle = v.p.color(i / n, 0.85);
          v.ctx.beginPath();
          v.ctx.moveTo(cx, cy);
          v.ctx.lineTo(cx + Math.cos(a) * len, cy - Math.abs(Math.sin(a)) * len);
          v.ctx.stroke();
        }
      }
    },
  },
  {
    id: "dot-matrix",
    name: "Dot matrix",
    family: "spectrum",
    description: "A field of dots that light column by column with the mix.",
    draw: (v) => {
      const cols = 48;
      const rows = 24;
      const b = bands(v, cols);
      const cw = v.w / cols;
      const rh = v.h / rows;
      const r = Math.min(cw, rh) * 0.28;
      for (let i = 0; i < cols; i++) {
        const lit = Math.round(b[i] * rows);
        for (let j = 0; j < rows; j++) {
          const on = j < lit;
          v.ctx.fillStyle = on ? v.p.color(j / rows, 0.95) : v.p.color(0, 0.1);
          v.ctx.beginPath();
          v.ctx.arc(i * cw + cw / 2, v.h - (j + 0.5) * rh, on ? r : r * 0.6, 0, TAU);
          v.ctx.fill();
        }
      }
    },
  },
];


Object.assign(exports,{spectrumModes});
};
wsModules["modes/util"] = function(exports, require){


const TAU = Math.PI * 2;

const lerp = (a        , b        , t        ) => a + (b - a) * t;

/**
 * Sample the FFT at a 0-1 position on a log frequency axis (matches how
 * pitch is perceived; linear sampling wastes half the range on hiss).
 */
function bin(fft            , pos        )         {
  const idx = Math.min(
    fft.length - 1,
    Math.floor(Math.pow(fft.length - 1, Math.max(0, Math.min(1, pos)) * 0.92 + 0.08)),
  );
  return fft[idx] / 255;
}

/** n log-spaced band magnitudes, 0-1, lightly smoothed across frames. */
function bands(v          , n        , smooth = 0.35)               {
  const key = `bands${n}`;
  let prev = v.state[key]                            ;
  if (!prev || prev.length !== n) {
    prev = new Float32Array(n);
    v.state[key] = prev;
  }
  for (let i = 0; i < n; i++) {
    const target = bin(v.f.fft, i / (n - 1 || 1));
    prev[i] = lerp(prev[i], target, 1 - smooth);
  }
  return prev;
}

/**
 * Create a scratch canvas that works on the main thread AND inside a
 * dedicated worker (the v2 render pipeline draws modes off-thread).
 */
function makeScratchCanvas(w        , h        )                    {
  if (typeof document !== "undefined") {
    const c = document.createElement("canvas");
    c.width = w;
    c.height = h;
    return c;
  }
  // Worker context: OffscreenCanvas exposes the same 2D surface we use.
  return new OffscreenCanvas(w, h)                                ;
}

/** A persistent offscreen canvas for cheap low-res field effects. */
function offscreen(
  v          ,
  key        ,
  w        ,
  h        ,
)                                                        {
  let entry = v.state[key]

               ;
  if (!entry || entry.c.width !== w || entry.c.height !== h) {
    const c = makeScratchCanvas(w, h);
    entry = { c, x: c.getContext("2d")  };
    v.state[key] = entry;
  }
  return entry;
}

/** Deterministic pseudo-random stream so modes stay stable across resizes. */
function rng(seed        )               {
  let s = seed >>> 0 || 1;
  return () => {
    s ^= s << 13;
    s ^= s >>> 17;
    s ^= s << 5;
    return ((s >>> 0) % 1_000_000) / 1_000_000;
  };
}

/** Cheap 2D value noise (enough for flow fields, no dependency). */
function noise2(x        , y        )         {
  const ix = Math.floor(x);
  const iy = Math.floor(y);
  const fx = x - ix;
  const fy = y - iy;
  const h = (a        , b        ) => {
    const n = Math.sin(a * 127.1 + b * 311.7) * 43758.5453;
    return n - Math.floor(n);
  };
  const s = (t        ) => t * t * (3 - 2 * t);
  const a = lerp(h(ix, iy), h(ix + 1, iy), s(fx));
  const b = lerp(h(ix, iy + 1), h(ix + 1, iy + 1), s(fx));
  return lerp(a, b, s(fy));
}


Object.assign(exports,{TAU,lerp,bin,bands,makeScratchCanvas,offscreen,rng,noise2});
};
wsModules["modes/waveform"] = function(exports, require){

const { TAU } = require("modes/util");

const waveformModes            = [
  {
    id: "scope",
    name: "Oscilloscope",
    family: "waveform",
    description: "A single clean trace of the raw signal, phosphor style.",
    fade: 0.35,
    draw: (v) => {
      const w = v.f.wave;
      v.ctx.lineWidth = Math.max(2, v.h / 320);
      v.ctx.strokeStyle = v.p.color(0.8, 0.95);
      v.ctx.beginPath();
      for (let i = 0; i < w.length; i++) {
        const x = (i / (w.length - 1)) * v.w;
        const y = v.h / 2 + ((w[i] - 128) / 128) * v.h * 0.4;
        i === 0 ? v.ctx.moveTo(x, y) : v.ctx.lineTo(x, y);
      }
      v.ctx.stroke();
    },
  },
  {
    id: "ring-scope",
    name: "Ring scope",
    family: "waveform",
    description: "The waveform bent into a breathing circle.",
    fade: 0.3,
    draw: (v) => {
      const w = v.f.wave;
      const cx = v.w / 2;
      const cy = v.h / 2;
      const R = Math.min(v.w, v.h) * (0.28 + v.f.bass * 0.08);
      v.ctx.lineWidth = Math.max(2, R / 90);
      v.ctx.strokeStyle = v.p.color(0.75, 0.95);
      v.ctx.beginPath();
      for (let i = 0; i <= w.length; i++) {
        const s = (w[i % w.length] - 128) / 128;
        const a = (i / w.length) * TAU + v.f.t * 0.1;
        const r = R + s * R * 0.55;
        const x = cx + Math.cos(a) * r;
        const y = cy + Math.sin(a) * r;
        i === 0 ? v.ctx.moveTo(x, y) : v.ctx.lineTo(x, y);
      }
      v.ctx.stroke();
    },
  },
  {
    id: "lissajous",
    name: "Lissajous",
    family: "waveform",
    description: "The signal plotted against a delayed copy of itself.",
    fade: 0.18,
    draw: (v) => {
      const w = v.f.wave;
      const cx = v.w / 2;
      const cy = v.h / 2;
      const S = Math.min(v.w, v.h) * 0.42;
      const delay = 180;
      v.ctx.lineWidth = Math.max(1.5, S / 260);
      v.ctx.strokeStyle = v.p.color(0.85, 0.8);
      v.ctx.beginPath();
      for (let i = 0; i < w.length - delay; i += 2) {
        const x = cx + ((w[i] - 128) / 128) * S;
        const y = cy + ((w[i + delay] - 128) / 128) * S;
        i === 0 ? v.ctx.moveTo(x, y) : v.ctx.lineTo(x, y);
      }
      v.ctx.stroke();
    },
  },
  {
    id: "braid",
    name: "Braid",
    family: "waveform",
    description: "Three phase-shifted traces weaving around each other.",
    fade: 0.4,
    draw: (v) => {
      const w = v.f.wave;
      v.ctx.lineWidth = Math.max(1.5, v.h / 400);
      for (let k = 0; k < 3; k++) {
        const shift = k * 340;
        const off = (k - 1) * v.h * 0.05;
        v.ctx.strokeStyle = v.p.color(0.35 + k * 0.3, 0.85);
        v.ctx.beginPath();
        for (let i = 0; i < w.length - shift; i += 2) {
          const x = (i / (w.length - 1)) * v.w;
          const env = Math.sin((i / w.length) * Math.PI);
          const y =
            v.h / 2 + off + ((w[i + shift] - 128) / 128) * v.h * 0.35 * env;
          i === 0 ? v.ctx.moveTo(x, y) : v.ctx.lineTo(x, y);
        }
        v.ctx.stroke();
      }
    },
  },
  {
    id: "ridgeline",
    name: "Ridgeline",
    family: "waveform",
    description: "Stacked history of the wave, drawn like a famous album cover.",
    draw: (v) => {
      const ROWS = 36;
      let hist = v.state.hist                              ;
      if (!hist) {
        hist = [];
        v.state.hist = hist;
      }
      const N = 160;
      const row = new Float32Array(N);
      for (let i = 0; i < N; i++) {
        const s = (v.f.wave[Math.floor((i / N) * v.f.wave.length)] - 128) / 128;
        row[i] = s;
      }
      hist.unshift(row);
      if (hist.length > ROWS) hist.pop();

      const mx = v.w * 0.18;
      const my = v.h * 0.12;
      const rh = (v.h - my * 2) / ROWS;
      for (let r = hist.length - 1; r >= 0; r--) {
        const y0 = my + r * rh;
        const env = Math.sin(((r + 1) / ROWS) * Math.PI) * 0.4 + 0.6;
        v.ctx.beginPath();
        v.ctx.moveTo(mx, y0);
        for (let i = 0; i < N; i++) {
          const x = mx + (i / (N - 1)) * (v.w - mx * 2);
          const bell = Math.sin((i / (N - 1)) * Math.PI) ** 2;
          const y = y0 - Math.abs(hist[r][i]) * rh * 9 * bell * env;
          v.ctx.lineTo(x, y);
        }
        v.ctx.lineTo(v.w - mx, y0 + rh);
        v.ctx.lineTo(mx, y0 + rh);
        v.ctx.closePath();
        v.ctx.fillStyle = v.p.bg;
        v.ctx.fill();
        v.ctx.strokeStyle = v.p.color(1 - r / ROWS, 0.9);
        v.ctx.lineWidth = Math.max(1.2, v.h / 800);
        v.ctx.stroke();
      }
    },
  },
  {
    id: "strings",
    name: "Strings",
    family: "waveform",
    description: "Vertical strings plucked by the bands they listen to.",
    fade: 0.25,
    draw: (v) => {
      const n = 24;
      const w = v.f.wave;
      const gap = v.w / (n + 1);
      v.ctx.lineWidth = Math.max(1.5, v.w / 1200);
      for (let s = 0; s < n; s++) {
        const x0 = gap * (s + 1);
        const phase = Math.floor((s / n) * (w.length / 2));
        v.ctx.strokeStyle = v.p.color(s / n, 0.85);
        v.ctx.beginPath();
        for (let i = 0; i <= 40; i++) {
          const yt = i / 40;
          const bell = Math.sin(yt * Math.PI);
          const sample = (w[(phase + i * 24) % w.length] - 128) / 128;
          const x = x0 + sample * gap * 1.4 * bell;
          const y = yt * v.h;
          i === 0 ? v.ctx.moveTo(x, y) : v.ctx.lineTo(x, y);
        }
        v.ctx.stroke();
      }
    },
  },
];


Object.assign(exports,{waveformModes});
};
wsModules["palettes"] = function(exports, require){


/**
 * Trace palettes. "phosphor" is the brand ultramarine and the default;
 * the rest are classic instrument phosphor colors plus one full-spectrum
 * palette for modes that want hue travel.
 */

function mono(id        , name        , hue        , sat        )             {
  return {
    id,
    name,
    bg: "#0b0d0c",
    color: (pos, alpha = 1) => {
      const l = 38 + pos * 47; // 38%..85% lightness along the palette
      return `hsla(${hue}, ${sat}%, ${l}%, ${alpha})`;
    },
  };
}

const PALETTES               = [
  mono("phosphor", "Ultramarine", 232, 64),
  mono("emerald", "Green phosphor", 152, 60),
  mono("amber", "Amber CRT", 40, 72),
  {
    id: "paper",
    name: "Paper white",
    bg: "#0b0d0c",
    color: (pos, alpha = 1) => `hsla(140, 6%, ${60 + pos * 36}%, ${alpha})`,
  },
  {
    id: "spectrum",
    name: "Full spectrum",
    bg: "#0b0d0c",
    color: (pos, alpha = 1) => `hsla(${(pos * 300 + 220) % 360}, 70%, 60%, ${alpha})`,
  },
  {
    id: "magma",
    name: "Magma",
    bg: "#0b0d0c",
    color: (pos, alpha = 1) =>
      `hsla(${10 + pos * 45}, ${70 + pos * 20}%, ${30 + pos * 45}%, ${alpha})`,
  },
];

const DEFAULT_PALETTE = PALETTES[0];

function paletteById(id        )             {
  return PALETTES.find((p) => p.id === id) ?? DEFAULT_PALETTE;
}

/* ------------------------------------------------------------------ */
/* Custom palettes: user-defined color stops, persisted in the browser. */
/* ------------------------------------------------------------------ */








function hexToRgb(hex        )                           {
  const c = hex.replace("#", "");
  const n = parseInt(
    c.length === 3 ? c.split("").map((x) => x + x).join("") : c,
    16,
  );
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

/** Build a drawable palette from a stored definition. */
function customToPalette(def                  )             {
  const stops = (def.stops.length ? def.stops : ["#3346c9"]).map(hexToRgb);
  return {
    id: def.id,
    name: def.name,
    bg: "#0b0d0c",
    color: (pos, alpha = 1) => {
      const t = Math.max(0, Math.min(1, pos)) * (stops.length - 1);
      const i = Math.min(stops.length - 2, Math.floor(t));
      if (stops.length === 1 || i < 0) {
        const [r, g, b] = stops[0];
        return `rgba(${r}, ${g}, ${b}, ${alpha})`;
      }
      const f = t - i;
      const r = Math.round(stops[i][0] + (stops[i + 1][0] - stops[i][0]) * f);
      const g = Math.round(stops[i][1] + (stops[i + 1][1] - stops[i][1]) * f);
      const b = Math.round(stops[i][2] + (stops[i + 1][2] - stops[i][2]) * f);
      return `rgba(${r}, ${g}, ${b}, ${alpha})`;
    },
  };
}

const CUSTOM_KEY = "wavescope-custom-palettes";

/** Client-only: call from effects or handlers, never during render. */
function loadCustomPalettes()                     {
  try {
    const raw = localStorage.getItem(CUSTOM_KEY);
    const parsed = raw ? JSON.parse(raw) : [];
    return Array.isArray(parsed)
      ? parsed.filter(
          (d)                        =>
            typeof d?.id === "string" &&
            typeof d?.name === "string" &&
            Array.isArray(d?.stops),
        )
      : [];
  } catch {
    return [];
  }
}

function saveCustomPalettes(defs                    ) {
  try {
    localStorage.setItem(CUSTOM_KEY, JSON.stringify(defs));
  } catch {
    // Storage full or blocked: palettes stay session-only.
  }
}


Object.assign(exports,{PALETTES,DEFAULT_PALETTE,paletteById,customToPalette,loadCustomPalettes,saveCustomPalettes});
};
wsModules["types"] = function(exports, require){
/**
 * Core types for the WaveScope visualizer engine.
 *
 * The engine is deliberately dependency-free: an AudioFrame is produced once
 * per animation frame (by the local AudioEngine or by a SyncReceiver in a
 * secondary window) and handed to the active mode's draw() function.
 */

/** One analyzed slice of audio, produced every animation frame. */



















/** A color system a mode draws with. `pos` is 0-1 along the palette. */









/** Everything a mode receives per frame. */









































/**
 * Anything exposing a live Web Audio graph tap. The console's AudioEngine
 * and a companion window's PcmLink both satisfy this shape, which is what
 * lets MilkDrop (butterchurn) render in every window.
 */











/** Rendering resolution targets (canvas backing-store height in pixels). */
const RESOLUTIONS = [
  { id: "auto", name: "Auto (native)", height: 0 },
  { id: "1080", name: "Full HD 1920x1080", height: 1080 },
  { id: "1440", name: "QHD 2560x1440", height: 1440 },
  { id: "2160", name: "4K 3840x2160", height: 2160 },
  { id: "4320", name: "8K 7680x4320", height: 4320 },
]         ;




Object.assign(exports,{RESOLUTIONS});
};
