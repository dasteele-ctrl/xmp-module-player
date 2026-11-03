import type { VizMode, VizPaint } from "../types";
import { TAU, rng } from "./util";

interface P {
  x: number;
  y: number;
  vx: number;
  vy: number;
  life: number;
  hue: number;
  size: number;
}

function pool(v: VizPaint, key: string, n: number, init: (i: number) => P): P[] {
  let arr = v.state[key] as P[] | undefined;
  if (!arr || arr.length !== n) {
    arr = Array.from({ length: n }, (_, i) => init(i));
    v.state[key] = arr;
  }
  return arr;
}

export const particleModes: VizMode[] = [
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
      let cursor = (v.state.cursor as number) ?? 0;
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
