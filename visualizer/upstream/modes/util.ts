import type { VizPaint } from "../types";

export const TAU = Math.PI * 2;

export const lerp = (a: number, b: number, t: number) => a + (b - a) * t;

/**
 * Sample the FFT at a 0-1 position on a log frequency axis (matches how
 * pitch is perceived; linear sampling wastes half the range on hiss).
 */
export function bin(fft: Uint8Array, pos: number): number {
  const idx = Math.min(
    fft.length - 1,
    Math.floor(Math.pow(fft.length - 1, Math.max(0, Math.min(1, pos)) * 0.92 + 0.08)),
  );
  return fft[idx] / 255;
}

/** n log-spaced band magnitudes, 0-1, lightly smoothed across frames. */
export function bands(v: VizPaint, n: number, smooth = 0.35): Float32Array {
  const key = `bands${n}`;
  let prev = v.state[key] as Float32Array | undefined;
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
export function makeScratchCanvas(w: number, h: number): HTMLCanvasElement {
  if (typeof document !== "undefined") {
    const c = document.createElement("canvas");
    c.width = w;
    c.height = h;
    return c;
  }
  // Worker context: OffscreenCanvas exposes the same 2D surface we use.
  return new OffscreenCanvas(w, h) as unknown as HTMLCanvasElement;
}

/** A persistent offscreen canvas for cheap low-res field effects. */
export function offscreen(
  v: VizPaint,
  key: string,
  w: number,
  h: number,
): { c: HTMLCanvasElement; x: CanvasRenderingContext2D } {
  let entry = v.state[key] as
    | { c: HTMLCanvasElement; x: CanvasRenderingContext2D }
    | undefined;
  if (!entry || entry.c.width !== w || entry.c.height !== h) {
    const c = makeScratchCanvas(w, h);
    entry = { c, x: c.getContext("2d")! };
    v.state[key] = entry;
  }
  return entry;
}

/** Deterministic pseudo-random stream so modes stay stable across resizes. */
export function rng(seed: number): () => number {
  let s = seed >>> 0 || 1;
  return () => {
    s ^= s << 13;
    s ^= s >>> 17;
    s ^= s << 5;
    return ((s >>> 0) % 1_000_000) / 1_000_000;
  };
}

/** Cheap 2D value noise (enough for flow fields, no dependency). */
export function noise2(x: number, y: number): number {
  const ix = Math.floor(x);
  const iy = Math.floor(y);
  const fx = x - ix;
  const fy = y - iy;
  const h = (a: number, b: number) => {
    const n = Math.sin(a * 127.1 + b * 311.7) * 43758.5453;
    return n - Math.floor(n);
  };
  const s = (t: number) => t * t * (3 - 2 * t);
  const a = lerp(h(ix, iy), h(ix + 1, iy), s(fx));
  const b = lerp(h(ix, iy + 1), h(ix + 1, iy + 1), s(fx));
  return lerp(a, b, s(fy));
}
