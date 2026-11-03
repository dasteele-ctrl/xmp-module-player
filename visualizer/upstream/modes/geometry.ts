import type { VizMode } from "../types";
import { TAU, bands } from "./util";

export const geometryModes: VizMode[] = [
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
      let rings = v.state.rings as number[] | undefined;
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
      interface Shard {
        a: number;
        d: number;
        vd: number;
        rot: number;
        vr: number;
        size: number;
        life: number;
        hue: number;
      }
      let shards = v.state.shards as Shard[] | undefined;
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
