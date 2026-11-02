# UADE fallback: first playback test

Based on XMP Module Player v1.4.2 (6e0ca22).

libxmp remains first for tracker modules. Existing dedicated MIDI and ZX
routing is preserved. After libxmp and SID reject a file, UADE is attempted
before FFmpeg. The original absolute song path is passed to libuade so
companion samples can be loaded normally. Keep companion files together
and retain their names.

UADE renders stereo signed 16-bit PCM at 44100 Hz on the existing background
decoder thread. The SDL callback consumes buffered audio and applies the
existing EQ and volume. Stop, engine changes, and exit join that thread
before freeing the UADE state. Pause retains the current buffer; Play after
Stop or end reopens the song. Notifications are drained and freed.

Seeking queues absolute song-relative positions on the decoder thread. Old
buffered PCM is discarded, rapid requests keep the latest target, and the
worker remains available at natural EOF for another seek. Seeking after Stop
reopens the renderer; seeking while paused keeps playback paused. UADE drives
its asynchronous seek during subsequent reads, so large jumps can take time.
When duration is unknown, the label shows `--:--` and the slider starts with
five minutes, expanding as playback approaches its end. This slider range is
a navigation window, not an estimated song duration.

WAV export uses an independent UADE state and the existing temporary-file
writer. It renders from the start until UADE reports song end by default.
The Save dialog offers an unchecked “Limit to” option (1–3600 seconds,
initial value 180). Enabling it stops at that upper limit or natural end,
whichever comes first. There is no separate UADE duration dialog. Cancellation
or failure leaves an existing destination unchanged. Manual subsong selection is not
implemented; UADE's normal subsong/end policy applies.

Folder scanning includes a common-format list with Amiga prefix names such
as mdat.*, mod.*, and dw.*. It excludes smpl.* sample companions. For formats
outside this initial list, choose All files in Add Files or Open. Filename inclusion is only a filter; libuade validates playback.

## Build on the development machine

UADE 3.05 development headers, libuade.pc, and the installed UADE runtime
are required, in addition to the existing XMP dependencies. The regular
`make -j2` target includes the adapter and explicitly links libbencodetools. If libuade is not in the loader cache,
run with `LD_LIBRARY_PATH=/usr/local/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}`.

The known development runtime is:

- `/usr/local/lib/uade/uadecore`
- `/usr/local/share/uade` (players, score, eagleplayer.conf, uade.conf, uaerc)

Optional `XMP_UADE_CORE` and `XMP_UADE_BASE_DIR` environment variables override
compiled-in libuade locations. AppImage bundling is a separate follow-up;
these patches do not modify the packaging script or produce a portable build.

## Verification

Adapter contract tests ran against a fake libuade API with AddressSanitizer
and UndefinedBehaviorSanitizer (leak scanning disabled because the host does
not support it). Covered failed initialization/detection/play, wrong sample
rate, PCM reads, EOF, decode errors, malformed frame lengths, notification
cleanup, and repeated open/cleanup, plus seek arguments and failure propagation.
The extracted decoder worker passed thread tests for seek after EOF, latest
request selection, stale PCM rejection, backwards seek, and joining on stop.
The actual WAV writer passed tests with a fake decoder and the real GLib
runtime: natural-end export, exact frame limits and WAV headers, early EOF, an independent live
playback state, cancellation, decode/open failure, invalid export limits, and
preservation of an existing destination. These do not validate real UADE decoding.

This environment lacks GTK/SDL/libuade development libraries, so the complete
GUI build and real-song playback have not been verified here.

On the development machine:

1. `make check-uade-syntax xmp-uade-test`
2. `./xmp-uade-test /absolute/path/to/mdat.song` (with its smpl companion)
3. `make -j2` and launch `./xmp-linux`.
4. Play a known MOD: it must retain its libxmp format label.
5. Play Turrican II via its mdat file: the format label must include UADE.
6. Check seeking forwards, backwards, while paused, and after Stop; then
   check pause/resume, stop/play, next/previous, EQ, volume, visualizers, and
   repeated switches among UADE, MOD, SID, MIDI, and ZX.
7. Export a 10-second WAV, verify its audio/duration, and cancel a longer
   export over an existing destination to verify it is preserved.
8. Check a missing companion and an invalid file fail without exiting.
9. Close while playing, then check `pgrep -af uadecore` for leaked processes.

Do not release until these runtime checks and AppImage bundling are complete.
