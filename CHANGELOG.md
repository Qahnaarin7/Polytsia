# Changelog

## 1.3.0

### Added
- **Import games** in the background menu finds installed Steam, Epic Games, and GOG games and adds them as tiles. Steam games get their library artwork as covers.
- **Run as administrator** and **Open file location** in the tile menu.
- **Sort by most used** in the background menu. Polytsia now counts launches for each tile.
- Optional global hotkey **Ctrl+Alt+Space** (Settings → Behavior → Startup) to show or hide the window.
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
