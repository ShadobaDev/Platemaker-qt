# Platemaker GUI — Application Specification

**Status:** Active development — core widget layer done, business logic wiring in progress.  
**Last updated:** 2026-06-14  
**Audience:** Developer + AI coding assistant

> **Library reference:** The GUI is a thin shell over `libplatemaker`.  
> For domain logic (pipeline, data models, serialisation, profile matching) see  
> `../PlateMaker/docs/SPECIFICATION.md`.

---

## 1. Overview

**Platemaker GUI** is a cross-platform Qt 6 desktop application for comic artists.
It exposes all capabilities of `libplatemaker` through a visual interface:

- Managing workspaces (`.platemaker.json` files)
- Managing projects (chapters) within a workspace
- Visualising source image sets as scrollable tile grids
- Running the strip-processing pipeline with live progress
- Managing canvas profiles and generating margin-overlay templates
- Managing output profiles (format, slice size, JPEG options)

The application follows a strict separation of concerns:

```
Qt GUI layer  (this project)
  └── views / widgets / dialogs  — all Qt types, QWidget-derived
  └── background workers          — QtConcurrent::run() wrappers
  └── no domain logic             — domain lives in libplatemaker

libplatemaker  (shared library)
  └── WorkspaceSerializer, CanvasProfileMatcher, CanvasProfileMatcher
  └── Scaler, ScaledStrip, Slicer, MarginCropper, TemplateGenerator
  └── ThumbnailCache, ImageIO, CancellationToken
```

---

## 2. Main Window Architecture

### 2.1 Shell — `MainWindow`

MDI-style host window.  Holds:

| Area | Widget | Purpose |
|---|---|---|
| Central MDI area | `QMdiArea` | Displays one `Project` sub-window per open chapter |
| Left dock | Workspace panel | Lists projects in the open workspace, profile buttons |
| Bottom dock | Log / progress | Pipeline output, progress bar, cancel button |
| Menu bar | File / Workspace / Project / Process / Help | All application actions |
| Status bar | — | Current workspace path + dirty indicator |

One `MainWindow` instance = one open workspace.  Switching workspace closes the
current one (with save prompt) and re-opens with the new file.

### 2.2 Project View — `Project`

Sub-window inside the MDI area.  Represents one `ProjectItem`.

- Scrollable grid of `ImageTile` widgets, one per `InputFile` in the project.
- Tiles are ordered by `InputFile::order`.
- Drag-and-drop reordering changes `order` and marks `ProjectItem::stripDirty`.
- A toolbar row above the grid shows: project name, input directory, link/unlink
  canvas profile button, output profile selector.

### 2.3 Image Tile — `ImageTile`

Single card in the project grid.  Shows:

- Thumbnail (loaded asynchronously via `ThumbnailCache + QtConcurrent`)
- Filename (short)
- Processing status badge (Pending / Processed / Modified / Missing)
- Contribution indicator (which output slices this file feeds into)

### 2.4 Profile Dialogs

| Dialog | Library type | Trigger |
|---|---|---|
| `CanvasProfileDialog` | `CanvasProfile` | Add / Edit in ManageCanvasProfilesDialog |
| `OutputProfileDialog` | `OutputProfile` | Add / Edit in ManageOutputProfilesDialog |
| `ManageCanvasProfilesDialog` | `Workspace::canvasProfiles` | "Canvas Profiles…" action |
| `ManageOutputProfilesDialog` | `Workspace::outputProfiles` | "Output Profiles…" action |

`ManageCanvasProfilesDialog` also emits `generateTemplatesRequested(QList<CanvasProfile>)`
when the user requests template PNG generation.

---

## 3. Application State

All persistent state is stored in `Platemaker::Models::Workspace` — the GUI
never has its own parallel data model.

```
MainWindow
  └── m_workspace : Workspace          // loaded from .platemaker.json
  └── m_workspacePath : QString        // current file path
  └── m_dirty : bool                   // unsaved changes

Project (one per open ProjectItem)
  └── m_projectIndex : int             // index into m_workspace.projectItems
```

**Dirty tracking rules:**
- Any profile edit → `m_dirty = true` → asterisk in title bar
- Any image tile reorder → `ProjectItem::stripDirty = true` + `m_dirty = true`
- Successful pipeline run → `m_dirty = true` (hashes updated)
- File → Save → `WorkspaceSerializer::save()` → `m_dirty = false`

---

## 4. Key Workflows

### 4.1 Open Workspace

```
File → Open Workspace
  → QFileDialog (*.platemaker.json)
  → WorkspaceSerializer::load(path)
  → populate workspace panel (project list)
  → restore open projects from last session (if desired)
```

### 4.2 Create Workspace

```
File → New Workspace
  → QFileDialog (choose save location)
  → construct default Workspace (one default OutputProfile "Webtoon Standard")
  → WorkspaceSerializer::save(path)
  → open workspace panel
```

### 4.3 Open / Create Project

```
Workspace panel → double-click project  OR  Workspace → New Project
  → Project sub-window opens in MDI area
  → mergeFileScan() populates tile grid from input directory
  → thumbnails load asynchronously in background
```

### 4.4 Run Pipeline

```
Process → Run  (or toolbar button)
  → project.sanitize()
  → if project.isUpToDate() → inform user, skip
  → else:
      worker = QtConcurrent::run([&]() {
          CanvasProfileMatcher matcher(workspace.canvasProfiles, project.canvasProfileIds)
          for each InputFile:
              result = matcher.resolve(w, h)
              // scale + strip.append()
          slices = strip.sliceAll(...)
          for each slice: imageIO.save(...)  + emit progress
          project.applyProcessingResults(records, outDir, timestamp)
      })
      show progress bar + Cancel button
      on finish: refresh tile statuses, WorkspaceSerializer::save()
```

### 4.5 Cancel Pipeline

```
Cancel button
  → cancellationToken.cancel()
  → worker checks token between slices and exits early
  → progress bar resets, partial output kept
```

### 4.6 Generate Templates

```
ManageCanvasProfilesDialog → "Generate Templates" button
  → emits generateTemplatesRequested(selectedProfiles)
  → MainWindow receives signal
  → QFileDialog (choose output directory)
  → for each profile:
        TemplateGenerator::generate(profile, activeOutputProfile, outPath)
  → open output directory in file manager
```

### 4.7 Manage Canvas Profiles (CRUD)

```
Workspace → Canvas Profiles…
  → ManageCanvasProfilesDialog opens
  → shows workspace.canvasProfiles list
  → Add    → CanvasProfileDialog → appends to workspace.canvasProfiles
  → Edit   → CanvasProfileDialog → updates entry in-place
  → Delete → removes from list (check: warn if profile is linked to any project)
  → OK     → save workspace
```

**Conflict guard** (SPECIFICATION.md §7.5.2):  
When linking a canvas profile to a project (`project add-profile` equivalent in GUI),
the GUI must call `addCanvasProfileToProject()` from libplatemaker and display an
error if `AddCanvasProfileResult::Status::Conflict` is returned, highlighting the
conflicting profile in the list.

---

## 5. Thumbnail Loading Policy

- Thumbnails are **never** loaded on the main thread.
- Each `ImageTile` requests its thumbnail via `QtConcurrent::run()` on first paint.
- `ThumbnailCache::getOrGenerate(filePath)` is the only call made in the worker.
- On completion the worker emits a signal back to the tile; the tile calls `update()`.
- A placeholder grey rect is shown while loading.
- Failed thumbnails show an error icon; the tile remains interactive.

---

## 6. Background Thread Model

| Operation | Mechanism | Thread safety notes |
|---|---|---|
| Thumbnail loading | `QtConcurrent::run()` per tile | `ThumbnailCache` is thread-safe |
| Pipeline run | Single `QFuture` via `QtConcurrent::run()` | `CancellationToken` is atomic |
| Template generation | `QtConcurrent::run()` per profile | `TemplateGenerator` is stateless |
| All UI updates | `QMetaObject::invokeMethod()` or signal/slot | Never touch widgets from worker |

---

## 7. Cross-Platform Notes

- Tested on Windows 10/11 (MSVC + MSYS2 MinGW) and Ubuntu 22.04.
- `libplatemaker.dll` and libvips runtime DLLs are copied next to the executable
  by the CMake post-build step — no manual PATH setup needed.
- Linux: RPATH embedded in the installed binary; no `LD_LIBRARY_PATH` needed after install.
- macOS: not tested yet.

---

## 8. UI Style Conventions

- **No custom QSS** unless strictly necessary — rely on the platform native style.
- Dialog buttons: standard `QDialogButtonBox` with Ok / Cancel.
- Destructive actions (delete profile, remove project): require a `QMessageBox::question` confirmation.
- Errors from libplatemaker (pipeline, serialiser): shown as `QMessageBox::critical`.
- Progress: `QProgressBar` in the bottom dock, range 0–100 (percent of output slices written).
