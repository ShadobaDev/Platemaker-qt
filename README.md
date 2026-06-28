# Platemaker GUI

Qt 6 desktop frontend for [libplatemaker](https://github.com/ShadobaDev/PlateMaker) — a comic artist tool for pre- and post-processing Webtoon-format artwork.

> **Library documentation:** See the `PlateMaker` repository for the domain specification,
> data models, CLI reference, and pipeline details.

---

## Screenshots

_Main window — workspace panel + open project_

<!-- TODO: add screenshot -->
![Main window placeholder](docs/screenshots/main-window.png)

_Canvas profile management dialog_

<!-- TODO: add screenshot -->
![Canvas profiles dialog placeholder](docs/screenshots/canvas-profiles-dialog.png)

_Processing in progress — progress bar + tile status badges_

<!-- TODO: add screenshot -->
![Processing placeholder](docs/screenshots/processing.png)

---

## Requirements

| Tool | Version | Notes |
|---|---|---|
| Qt | 6.5+ | Widgets module required |
| CMake | 3.21+ | Presets format v6 |
| MSVC 2022 or MinGW (MSYS2) | — | Windows |
| GCC / Clang | — | Linux |
| libplatemaker | 0.1.x | See **Linking libplatemaker** below |

---

## Building

The project is built and run from **Qt Creator**.  Open `CMakeLists.txt` as a
Qt Creator project and configure the kit (MSVC 2022 or MinGW 64-bit).

```bash
cmake -B .\build\Desktop_Qt_6_11_1_MinGW_64_bit-Debug\ -S .      
cmake --build .\build\Desktop_Qt_6_11_1_MinGW_64_bit-Debug\ --target installer
```
### Linking libplatemaker

`CMakeLists.txt` resolves `libplatemaker` in three steps, in order:

1. **`PLATEMAKER_DIR` cache variable** (preferred during development)
2. System `find_package` / `CMAKE_PREFIX_PATH`
3. Automatic download from GitHub Releases (v0.1.1)

**Setting `PLATEMAKER_DIR` in Qt Creator:**

1. Open **Projects** (left sidebar) → select your kit → **CMake** tab
2. Click **Add** → **String**
3. Name: `PLATEMAKER_DIR`  
   Value: path to the dev package, e.g.
   ```
   D:/Users/Shadoba/Dev/PlateMaker/install/windows-msvc
   ```
4. Click **Apply Configuration Changes** → **Build**

The value is cached in `build/.../CMakeCache.txt` and survives reconfigures.

**Building the dev package from source:**

```powershell
# In the PlateMaker repository
cmake --preset windows-msvc
cmake --build --preset windows-msvc
cmake --install build/windows-msvc --config Release
# → install/windows-msvc/  (use this path as PLATEMAKER_DIR)
```

### Windows: runtime DLLs

The CMake post-build step automatically copies `platemaker.dll` and all libvips
runtime DLLs next to the executable.  No manual PATH configuration is needed to
run from Qt Creator or the build directory.

---

## Development Workflow

### Branching

```
main          — stable, matches latest release tag
dev           — active development (default target for feature branches)
feature/<name>
fix/<name>
```

### Modifying UI files

UI forms (`.ui`) are edited in **Qt Designer** launched from within Qt Creator.  
Do **not** edit `.ui` XML files by hand — Qt Creator regenerates the `ui_*.h`
headers from them at build time and hand edits will be overwritten.

When adding a new widget to a dialog or window:
1. Open the `.ui` file in Qt Designer inside Qt Creator.
2. Drag and drop the widget.
3. Set the object name and properties.
4. Build — Qt will regenerate the header.
5. Reference the new widget via `ui->objectName` in the `.cpp`.

### Keeping in sync with libplatemaker

When libplatemaker changes its public API (new model fields, renamed methods):

1. Update `PLATEMAKER_DIR` to point to the freshly installed dev package.
2. Click **Apply Configuration Changes** in Qt Creator.
3. Rebuild — the compiler will surface any API breaks immediately.

---

## Project Structure

```
Platemaker/
├── CMakeLists.txt
├── main.cpp
├── mainwindow.h / .cpp / .ui      — application shell (MDI + docks)
├── project.h / .cpp / .ui         — per-chapter image tile grid
├── imagetile.h / .cpp / .ui       — single source image card
├── projectitem.h / .cpp           — thin model adapter (minimal)
├── canvasprofiledialog.h / .cpp / .ui
├── outputprofiledialog.h / .cpp / .ui
├── managecanvasprofilesdialog.h / .cpp / .ui
├── manageoutputprofilesdialog.h / .cpp / .ui
└── docs/
    ├── SPECIFICATION.md           — GUI feature specification
    └── TODO.md                    — implementation checklist
```

---

## Licence

LGPL 3.0 — same as `libplatemaker`.  Qt is used under LGPL 3.0.
