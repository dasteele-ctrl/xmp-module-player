import type { VizMode } from "../types";
import { TAU } from "./util";

export const waveformModes: VizMode[] = [
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
      let hist = v.state.hist as Float32Array[] | undefined;
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
