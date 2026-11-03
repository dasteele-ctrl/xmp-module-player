import type { VizMode } from "../types";
import { TAU, bands, bin, offscreen } from "./util";

export const spectrumModes: VizMode[] = [
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
      let peaks = v.state.peaks as Float32Array | undefined;
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
