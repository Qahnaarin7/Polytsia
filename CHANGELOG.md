# Changelog

## 1.4.0

### Added
- Ten new palettes in calmer, bolder tones. Light: Steel, Pine, Khaki, Navy, Brick. Dark: Carbon, Forest, Volcano, Neon, Gunmetal.
- **Ambient effect** selector on the Themes tab: Drift (the previous effect), Aurora, Pulse, Follow cursor, and Still. Still uses no CPU for animation.

## 1.3.0

### Added
- **Run as administrator** and **Open file location** in the tile menu.
- **Sort by most used** in the background menu. Polytsia now counts launches for each tile and says so when there is nothing to sort yet.
- Optional global shortcut to show or hide the window: Ctrl+Alt+P, Ctrl+Alt+G, or Ctrl+Alt+F12 (Settings → Behavior → Startup).
- **Open data folder** on the Backup tab.
- Error log at `%LOCALAPPDATA%\Polytsia\log.txt`.

### Changed
- Startup now follows `Polytsia.exe` automatically when the file is moved or renamed, for example after updating to a new release.
- Behavior settings group startup options under their own heading.
- Reordering tiles updates only the affected rows instead of rewriting the whole library.

### Fixed
- Backups made by older versions are migrated on restore instead of being rejected once the library format changes.
- A library or backup from a newer version now shows a clear message instead of failing silently.
- Startup no longer fails when Polytsia is stored in a folder with a path longer than 260 characters.

## 1.2.13
- Previous release.
