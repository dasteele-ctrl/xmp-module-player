import type { AudioFrame } from "./types";

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
export interface AnalysisState {
  smoothLevel: number;
  bassAvg: number;
  beatEnv: number;
}

export function newAnalysisState(): AnalysisState {
  return { smoothLevel: 0, bassAvg: 0, beatEnv: 0 };
}

/**
 * Turn one analyser snapshot into an AudioFrame. `fft` is byte frequency data
 * (length = fftSize/2), `wave` is byte time-domain data (length = fftSize),
 * both as the Web Audio AnalyserNode produces them.
 */
export function analyzeFrame(
  fft: Uint8Array,
  wave: Uint8Array,
  sampleRate: number,
  t: number,
  dt: number,
  st: AnalysisState,
): AudioFrame {
  const hzPerBin = sampleRate / 2 / fft.length;
  const band = (lo: number, hi: number) => {
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
