# Modulez Remote preview

This branch starts at tag `v1.8.8`. The Android app sends playback commands over HTTPS to the desktop player. Decoding and audio output run on the Linux computer.

## Try the preview

1. On Linux Mint, make the preview AppImage executable and launch it. Add tracks or load an M3U playlist.
2. Choose **Pair remote**, select the Dell’s home-network IPv4 address, and click **Start pairing**.
3. Install the test APK on Android 7.0 or later. Connect to the same home network, open **Modulez Remote**, and enter the address and code displayed on the Dell.
4. Tap a playlist row to play it. The phone shows the current track and format, transport controls, and the Dell player’s volume. It refreshes while the app is open. For radio, the transport uses the player’s existing stop/start behavior.

The listener starts on the address you select, on TCP port 48788. Pairing codes are single-use and expire after five minutes or five unsuccessful guesses. The first part checks the certificate shown by the Dell. The phone stores the full certificate fingerprint and a session token after pairing. Click the remote button again on the Dell to stop listening and revoke the token. After restarting the player, tap **Forget Dell** on the phone and pair again. If the connection times out, check that the router allows devices to reach each other and that Mint’s firewall allows the phone to reach TCP 48788.

## Build

Build the Linux player with the repository’s normal `make` command and its v1.8.8 development dependencies. To package it with the matching release runtime, run `remote/package-linux.sh /path/to/Modulez-Player-v1.8.8-x86_64.AppImage` on a compatible x86_64 Mint build host with squashfs-tools, binutils, Python 3 and glib-networking installed. The preview uses its own GApplication ID and player configuration directory. Its AppImage omits release update metadata.

With JDK 17 or later, Android SDK Platform 35, and Android Build Tools 35.0.0, set `ANDROID_HOME` and run `remote/android/build.sh`. The resulting test APK and local test keystore appear under `remote/android/build/`. Reuse the same test keystore when installing updates over an earlier APK from your own build.

### Preview 2

On Android, **Radio stations** opens the Dell preview’s built-in retro stations, favorites, custom stations and currently loaded World Radio results. Tap a station to start it on the Dell. The launcher uses the Modulez Player artwork.

Enter **Fullscreen Vis** to see a track caption in the lower-left corner. It appears again on a track change, stays for four seconds and fades out. It uses the displayed title as a fallback, local audio title and artist tags when available, and SID author metadata. Radio streams can supply live titles.

### Preview 3

Tap **World Radio** beside **Stations** on the paired phone. The Dell queries Radio Browser and returns a page of popular stations immediately on opening the directory. Use the dropdown to search by station name, genre/tag, or two-letter country code. Previous and Next browse pages of up to 100 stations. Tap a result to start playback on the Dell. The desktop World Radio tab shares the current search and results.
