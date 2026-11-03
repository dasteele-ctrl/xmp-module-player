import type { VizPalette } from "./types";

/**
 * Trace palettes. "phosphor" is the brand ultramarine and the default;
 * the rest are classic instrument phosphor colors plus one full-spectrum
 * palette for modes that want hue travel.
 */

function mono(id: string, name: string, hue: number, sat: number): VizPalette {
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

export const PALETTES: VizPalette[] = [
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

export const DEFAULT_PALETTE = PALETTES[0];

export function paletteById(id: string): VizPalette {
  return PALETTES.find((p) => p.id === id) ?? DEFAULT_PALETTE;
}

/* ------------------------------------------------------------------ */
/* Custom palettes: user-defined color stops, persisted in the browser. */
/* ------------------------------------------------------------------ */

export interface CustomPaletteDef {
  id: string;
  name: string;
  /** 1-3 hex stops; color(pos) interpolates across them. */
  stops: string[];
}

function hexToRgb(hex: string): [number, number, number] {
  const c = hex.replace("#", "");
  const n = parseInt(
    c.length === 3 ? c.split("").map((x) => x + x).join("") : c,
    16,
  );
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

/** Build a drawable palette from a stored definition. */
export function customToPalette(def: CustomPaletteDef): VizPalette {
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
export function loadCustomPalettes(): CustomPaletteDef[] {
  try {
    const raw = localStorage.getItem(CUSTOM_KEY);
    const parsed = raw ? JSON.parse(raw) : [];
    return Array.isArray(parsed)
      ? parsed.filter(
          (d): d is CustomPaletteDef =>
            typeof d?.id === "string" &&
            typeof d?.name === "string" &&
            Array.isArray(d?.stops),
        )
      : [];
  } catch {
    return [];
  }
}

export function saveCustomPalettes(defs: CustomPaletteDef[]) {
  try {
    localStorage.setItem(CUSTOM_KEY, JSON.stringify(defs));
  } catch {
    // Storage full or blocked: palettes stay session-only.
  }
}
