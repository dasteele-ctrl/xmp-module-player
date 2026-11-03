import type { VizMode } from "../types";
import { TAU, bin, makeScratchCanvas, noise2, offscreen, rng } from "./util";

export const fieldModes: VizMode[] = [
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
      let lut = v.state.lut as Uint8ClampedArray | undefined;
      if (!lut || v.state.lutPalette !== v.p.id) {
        lut = new Uint8ClampedArray(256 * 3);
        const probe = makeScratchCanvas(256, 1);
        const px = probe.getContext("2d")!;
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
      interface G {
        x: number;
        y: number;
        hue: number;
      }
      let grains = v.state.grains as G[] | undefined;
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
      let drops = v.state.drops as Float32Array | undefined;
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
      interface Ring {
        x: number;
        y: number;
        r: number;
        life: number;
        hue: number;
      }
      let rings = v.state.pr as Ring[] | undefined;
      if (!rings) {
        rings = [];
        v.state.pr = rings;
      }
      const wasBeat = (v.state.wasBeat as boolean) ?? false;
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
