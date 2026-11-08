# Modulez Player 1.7.1 — Release Notes

## Maintenance Release

Version 1.7.1 fixes a GTK warning introduced during
the theme integration improvements in version 1.7.0.

### Fixes

- Removed an invalid GtkCssProvider signal connection.
- Corrected HVSC waveform redraw handling.
- Ensured the HVSC visualization updates when appearance
  settings change.
- Corrected a misplaced redraw call in the sample browser.

### Features

All features introduced in Modulez Player 1.7.0
remain available.

### Compatibility

Tested on Linux Mint 22.3 XFCE.

Older Linux distributions may require newer GLIBC
or other system components.
