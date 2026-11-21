# Modulez Player 1.8.2

On the **Radio** tab, play a station and choose **Record WAV…**. **Stop Recording** saves a 44.1 kHz stereo WAV while playback continues. Stopping radio, switching stations, or quitting also finishes the file. Reconnection gaps are omitted.

Select one or more tunes in the playlist and choose **Export selected…**. The same command is available by right-clicking a selected row. Ctrl+A selects every visible row, including when a search is active.

Exports and radio recordings start in **Modulez Exports** inside your Music folder. Choosing another writable destination remembers it for all three audio save dialogs.

Choose a destination folder and one output format for the batch.

| Format | Encoding |
| --- | --- |
| FLAC | Lossless stereo, 44.1 kHz |
| WAV | 16-bit PCM stereo, 44.1 kHz |
| Opus | 128 kbps target, 48 kHz |
| Ogg Vorbis | 160 kbps target, 44.1 kHz |
| M4A (AAC) | 256 kbps target, 44.1 kHz |

FLAC is selected the first time. The export dialog remembers the last format used. Each local playlist entry produces its own file. Choose **Open Folder** in the results dialog to view the finished files in your file manager. This also works after a single-track WAV export. Files with the same output name receive a numbered suffix.

Export uses independent decoders and writes audio as it renders. The batch uses the MIDI SoundFont selected when export starts. SID files use their default subtune. The dialog sets a shared duration limit for SID and UADE tunes, initially 180 seconds. UADE tunes can end earlier. The Player tab's existing **Export WAV** command still exports the current SID subtune and offers the original single-track settings.

The output includes a track title and artist when the decoder supplies them. A filename supplies the fallback title. Cancelling keeps finished tracks and discards the unfinished file. If a track fails, the batch continues and reports the failure at the end.
