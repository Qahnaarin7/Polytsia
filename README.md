# Polytsia

Polytsia ("shelf") is a portable launcher for game and app shortcuts on Windows 11 x64, build 22621 or later. Customize tiles with icons or covers, choose from 20 light and dark palettes, and back up your library to a single file.

[Download the latest release](https://github.com/Qahnaarin7/Polytsia/releases/latest)

Polytsia uses the Windows display language at startup. The interface is available in Ukrainian, English, and Russian, with English as the fallback.

## Getting started

1. Save **Polytsia.exe** to a folder of your choice and run it. You can pin it to the taskbar for quick access.
2. Right-click an empty area and choose **Add shortcuts…**, or drag in `.lnk`, `.url`, or `.exe` files from File Explorer. Polytsia keeps its own copies of shortcuts and creates new shortcuts for executable files. Keep the original program in its folder so its shortcut can launch it.
3. Click a tile or press **Enter** to open it. Use **Search** in the right-click menu or press **Ctrl+F** to open the search field. Close it with **Esc**. The same menu gives you access to **Settings**. Use **Tab** and the arrow keys to move focus. Drag tiles or use their context menus to reorder them.
4. Press **F2** on a selected tile or choose **Name and artwork…** in its context menu. You can rename the tile, choose a PNG or ICO file, extract an icon from an EXE, preview the result, or restore the default icon. **Change launch target…** points the tile to another LNK, URL, or EXE file.
5. A tile's context menu also has **Run as administrator** and **Open file location** for `.lnk` shortcuts. **Sort by most used** in the background menu orders tiles by how often you open them. Launches are counted starting with version 1.3.0.
6. Choose **Add cover…** to use a PNG or JPG. Drag the image within the frame and zoom with the mouse wheel or the **−** and **+** buttons. Select **Apply**, then **Save** in the shortcut editor. To switch back to the tile’s icon, select **Remove** next to the cover and save. Covers appear in the **Full tile** style.

## Settings

### Themes

There are ten light and ten dark palettes. **Match Windows** applies the corresponding palette when Windows switches between light and dark mode. Hover over a palette or use **Tab** to focus it and preview it on your first three tiles. Click the palette or press **Enter** to save your choice.

**Ambient brightness** controls the background lighting and colored tile glow from 0% to 150%. Desktop Acrylic is available on Windows 11 24H2 and later when transparency effects are enabled.

### Tiles

Choose the tile style, size, spacing, label behavior, and colored hover glow. **Centered** places the icon above the label. **Compact** places it to the side. In **Full tile** mode, the artwork spans the tile’s width, with transparent logos given some padding and a lightly tinted background.

When animations are enabled, the glow expands and pulses as you hover over a tile. The arrows at the edge of the list show which directions you can scroll. You can also scroll with the mouse wheel.

### Behavior

| Setting | Options | Default |
| --- | --- | --- |
| After opening a shortcut | Exit, To tray, Keep open | Exit |
| When minimized | To tray, To taskbar | To tray |
| When closing the window | Exit, To tray | Exit |
| Start with Windows in the tray | On or off | Off |
| Pause animations on battery | On or off on devices with a built-in battery | Off |

**Pause animations on battery** pauses the ambient motion and transitions while the device is on battery power. When you plug it back in, Polytsia follows its own animation settings and those of Windows.

Click the tray icon or run `Polytsia.exe` again to bring the window back. The tray icon’s context menu includes **Exit**. The taskbar button behaves like other Windows taskbar buttons. If tray space is limited, Windows may move the icon to the hidden icons menu.

The logo in the top corner uses the active palette’s accent color. Drag the space between the logo and the window buttons to move the window. Double-click that area to maximize it or restore its previous size.

If you enable startup, Polytsia opens in the tray when you sign in to your Windows account. If you move or rename `Polytsia.exe`, the startup entry is updated the next time you run it.

Choose a **Show window shortcut** (Ctrl+Alt+P, Ctrl+Alt+G, or Ctrl+Alt+F12) to bring Polytsia up from anywhere. Press it again to hide the window. If another app already uses the combination, Polytsia tells you and turns the shortcut off.

### Backup

Polytsia stores the library and settings in `%LOCALAPPDATA%\Polytsia`. It remembers the window size and whether the window was maximized. Startup is configured for the current Windows user. The library is empty on first launch.

Polytsia writes errors to `log.txt` in the same folder. **Open data folder** on the **Backup** tab opens it.

Use the **Backup** tab to save or restore a `.polytsia` file containing the tile order, shortcuts, available icons, covers, and settings. Restoring a backup replaces the current library after confirmation. Shortcuts will open on another PC if their targets are available there. Use **Change launch target…** to update a path if needed.

## Building from source

You’ll need MSVC Build Tools with the x64 C++ components and the Windows SDK 10.0.22621.0 or later. Run the following from the source directory.

~~~powershell
.\build.ps1
~~~

The executable is written to `build\Polytsia.exe`. Polytsia is built with C++ and uses Win32, Direct2D, DirectWrite, Windows Imaging Component, and Windows SQLite.

The version number lives in `version.h`. GitHub Actions builds every push and pull request. Pushing a tag such as `v1.3.0` that matches `version.h` publishes a release with the executable and its SHA-256. See [CHANGELOG.md](CHANGELOG.md) for changes and `packaging/winget` for the winget manifest template.

## License

[MIT](LICENSE)
