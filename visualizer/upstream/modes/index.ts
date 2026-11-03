import type { VizMode } from "../types";
import { spectrumModes } from "./spectrum";
import { waveformModes } from "./waveform";
import { particleModes } from "./particles";
import { geometryModes } from "./geometry";
import { fieldModes } from "./field";

/** All built-in visualizers, grouped by family, stable order. */
export const MODES: VizMode[] = [
  ...spectrumModes,
  ...waveformModes,
  ...particleModes,
  ...geometryModes,
  ...fieldModes,
];

export const DEFAULT_MODE = MODES.find((m) => m.id === "radial-bars") ?? MODES[0];

export function modeById(id: string | undefined | null): VizMode {
  return MODES.find((m) => m.id === id) ?? DEFAULT_MODE;
}

export const FAMILIES = [
  { id: "spectrum", name: "Spectrum" },
  { id: "waveform", name: "Waveform" },
  { id: "particles", name: "Particles" },
  { id: "geometry", name: "Geometry" },
  { id: "field", name: "Fields" },
] as const;
