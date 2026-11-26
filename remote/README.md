# Modulez Remote 2.0

This branch starts at tag `v1.8.8`. The Android app sends playback commands over HTTPS to the desktop player. Decoding and audio output run on the Linux computer.

## Pair with Modulez Player 2.0

1. On Linux Mint, make the 2.0 AppImage executable and launch it. Add tracks or load an M3U playlist.
2. Choose **Pair remote**, select the computer’s home-network IPv4 address, and click **Start pairing**.
3. Install the 2.0 APK on Android 7.0 or later. Connect to the same home network, open **Modulez Remote**, and tap **Scan QR code**. Allow camera access and point the phone at the code. You can also enter the address and code displayed on the computer.
4. Tap a playlist row to play it. The phone shows the current track and format, transport controls, and the player’s volume. It refreshes while the app is open. For radio, the transport uses the player’s existing stop/start behavior.

The listener starts on the address you select, on TCP port 48788. Pairing codes are single-use and expire after five minutes or five unsuccessful guesses. The first part checks the certificate shown by the computer. The phone stores the full certificate fingerprint and a session token after pairing. Click the remote button again on the computer to stop listening and revoke the token. After restarting the player, tap **Forget player** on the phone and pair again. If the connection times out, check that the router allows devices to reach each other and that the computer’s firewall allows the phone to reach TCP 48788.

## Build

Build the Linux player with the repository’s normal `make` command and its v1.8.8 development dependencies. To package it with the matching release runtime, run `remote/package-linux.sh /path/to/Modulez-Player-v1.8.8-x86_64.AppImage` on a compatible x86_64 Mint build host with squashfs-tools, binutils, Python 3 and glib-networking installed. The 2.0 package retains the preview’s GApplication ID and player configuration directory. Its AppImage omits release update metadata.

With JDK 17 or later, Android SDK Platform 35, and Android Build Tools 35.0.0, set `ANDROID_HOME` and run `remote/android/build.sh`. The resulting test APK and local test keystore appear under `remote/android/build/`. Set PREVIEW_KEYSTORE to the existing signing keystore to build an APK that upgrades the previous installation. The distributed 2.0 APK uses the Preview 9 signer. Tap **About** on the pairing or Now Playing screen to see the installed version.

### Preview 2

On Android, **Radio stations** opens the desktop preview’s built-in retro stations, favorites, custom stations and currently loaded World Radio results. Tap a station to start it on the computer. The launcher uses the Modulez Player artwork.

Enter **Fullscreen Vis** to see a track caption in the lower-left corner. It appears again on a track change, stays for four seconds and fades out. It uses the displayed title as a fallback, local audio title and artist tags when available, and SID author metadata. Radio streams can supply live titles.

### Preview 3

Tap **World Radio** beside **Stations** on the paired phone. The player queries Radio Browser and returns a page of popular stations immediately on opening the directory. Use the dropdown to search by station name, genre/tag, or two-letter country code. Previous and Next browse pages of up to 100 stations. Tap a result to start playback on the computer. The desktop World Radio tab shares the current search and results.

### Preview 4

On the computer choose **Pair remote** and **Start pairing**. In the Android app tap **Scan QR code** and point the camera at the pairing dialog. The QR code carries the player’s address, full SHA-256 certificate fingerprint and the single-use code. Scanning and decoding happen on the phone. Camera permission is optional for manual pairing. To pair again after restarting the player, tap **Forget player** first.

The desktop encoder uses Project Nayuki’s QR Code generator v1.8.0 (MIT). The Android build downloads and verifies ZXing Core 3.3.3 (Apache 2.0); the scanner runs offline after installation.

### Preview 5

In the computer’s **Pair remote** dialog, use **Shared music folders** to add one or more folders. The selection persists across launches. Use **Clear sharing** there to remove all shared folders. After pairing once, **Browse music** on the phone opens those folders. Tap a track to append it to the playlist. The computer keeps playing its current track.

**Visualizer** opens the desktop Vis page. Choose a Wavescope mode, a projectM preset (including imported presets), or enter/exit fullscreen. These actions use the same connection as radio, playback and player volume. This preview keeps the Android package ID. Moving from Preview 4 to the newly signed Preview 5 requires uninstalling the previous APK and pairing once.

### Preview 6

On the phone, open **Browse music**, enter a shared folder, and tap **Add entire folder** to append its supported tracks and subfolders in sorted order. Hidden entries and symbolic links are skipped. The player scans and appends in short batches while the phone shows progress and the final count. Directory and playlist pages display up to 150 entries. Individual file selection still appends a single track.

On Android 11 and later, if Android records a crash, low-memory termination, or unresponsive process, the remote can show its previous exit reason on the next launch with a **Copy details** button.

The Android pairing screen and Now Playing header show the Modulez Player icon. The charcoal surfaces, aqua accents, rounded transport buttons and playlist panel carry the desktop player's appearance into the remote.

### Preview 7

Press and hold a playlist row and choose **Remove from playlist** to remove that occurrence. **Clear playlist** shows the total number of tracks across all pages before clearing. Both actions edit only the queue. Files stay on the computer and the current track keeps playing. If the queue changes while a menu is open, reopen the action on the refreshed list.

### Preview 8

In **Visualizer → Wavescope modes**, tap a **Color** entry to apply the desktop palette while keeping your Wavescope mode. The menu marks the current color. This works with the Preview 7 Android remote and its existing pairing.

### Preview 9

The Android volume slider has a 56 dp touch area, a 28 dp thumb and a 10 dp track. Drag left or right and release to set the player volume.

On Linux, **Pair remote** lists each IPv4 address with its interface name and type. Active physical links take priority over tunnels. Among otherwise comparable links, the lower-metric default route comes first. You can still select another address or enter one manually.
