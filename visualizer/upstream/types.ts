/**
 * Core types for the WaveScope visualizer engine.
 *
 * The engine is deliberately dependency-free: an AudioFrame is produced once
 * per animation frame (by the local AudioEngine or by a SyncReceiver in a
 * secondary window) and handed to the active mode's draw() function.
 */

/** One analyzed slice of audio, produced every animation frame. */
export interface AudioFrame {
  /** Frequency-domain magnitudes, 0-255, length fftSize/2 (1024). */
  fft: Uint8Array;
  /** Time-domain samples, 0-255 centered at 128, length 2048. */
  wave: Uint8Array;
  /** Overall RMS level, 0-1, smoothed. */
  level: number;
  /** Band energies, 0-1: bass (<250Hz), mid (250Hz-4kHz), treble (>4kHz). */
  bass: number;
  mid: number;
  treble: number;
  /** Beat envelope: jumps toward 1 on detected onsets, decays exponentially. */
  beat: number;
  /** Seconds since the engine started. */
  t: number;
  /** Seconds since the previous frame (clamped to 0.1). */
  dt: number;
}

/** A color system a mode draws with. `pos` is 0-1 along the palette. */
export interface VizPalette {
  id: string;
  name: string;
  /** Canvas clear color. */
  bg: string;
  /** Color at a palette position with optional alpha. */
  color: (pos: number, alpha?: number) => string;
}

/** Everything a mode receives per frame. */
export interface VizPaint {
  ctx: CanvasRenderingContext2D;
  w: number;
  h: number;
  f: AudioFrame;
  p: VizPalette;
  /** Per-instance scratch space that persists across frames. */
  state: Record<string, unknown>;
  /** Pointer position in canvas pixels, or null when idle/untracked. */
  pointer: { x: number; y: number } | null;
}

export type VizFamily =
  | "spectrum"
  | "waveform"
  | "particles"
  | "geometry"
  | "field";

export interface VizMode {
  /** Stable id used in URLs (?mode=radial-bars) and the docs. */
  id: string;
  name: string;
  family: VizFamily;
  /** One-line description shown in the mode picker and the manual. */
  description: string;
  /**
   * Trail persistence: alpha of the background wash painted before drawing.
   * 1 fully clears each frame; lower values leave phosphor-style trails.
   */
  fade?: number;
  /**
   * True for modes with sudden beat-driven bursts or bright pops. Calm mode
   * (the photosensitivity toggle) excludes these from the picker and shuffle.
   */
  flash?: boolean;
  draw: (v: VizPaint) => void;
}

export type SourceKind = "demo" | "mic" | "system" | "file";

/**
 * Anything exposing a live Web Audio graph tap. The console's AudioEngine
 * and a companion window's PcmLink both satisfy this shape, which is what
 * lets MilkDrop (butterchurn) render in every window.
 */
export interface AudioGraphSource {
  readonly audioContext: AudioContext | null;
  readonly analyserNode: AnalyserNode | null;
}

export interface SourceInfo {
  kind: SourceKind;
  /** Human label, e.g. the selected file name. */
  label: string;
}

/** Rendering resolution targets (canvas backing-store height in pixels). */
export const RESOLUTIONS = [
  { id: "auto", name: "Auto (native)", height: 0 },
  { id: "1080", name: "Full HD 1920x1080", height: 1080 },
  { id: "1440", name: "QHD 2560x1440", height: 1440 },
  { id: "2160", name: "4K 3840x2160", height: 2160 },
  { id: "4320", name: "8K 7680x4320", height: 4320 },
] as const;

export type ResolutionId = (typeof RESOLUTIONS)[number]["id"];
