# Platemaker GUI — Application Specification

**Status:** Active development — core widget layer done, business logic wiring in progress.  
**Last updated:** 2026-08-16  
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

Dock-based host window.  Holds:

| Area | Widget | Purpose |
|---|---|---|
| Workspace panel | Left dock | Lists projects in the open workspace, profile buttons |
| Project docks | `QDockWidget` per open project | Each holds a `Project` widget, tabbed with the workspace panel (opened via `openProjectDock()`) |
| Bottom dock | Log / progress | Pipeline output, progress bar, cancel button |
| Menu bar | File / Workspace / Project / Process / Help | All application actions |
| Status bar | — | Current workspace path + dirty indicator |

One `MainWindow` instance = one open workspace.  Switching workspace closes the
current one (with save prompt) and re-opens with the new file.

### 2.2 Project View — `Project`

A dock widget, tabbed alongside the workspace panel (one dock per open project).  Represents one
`ProjectItem` — created lazily by `MainWindow::openProjectDock()`, holding a `Workspace&` reference.

- Scrollable grid of `ImageTile` widgets, one per `InputFile` in the project.
- Tiles are ordered by `InputFile::order`, which is also the sequence the render builds the strip in
  (`ProjectItem::inputsInOrder()`).
- Drag-and-drop and the ▲/▼ buttons reorder through `Infrastructure::ProjectEditor`
  (`setInputOrder` / `moveInput`), which rewrites only the `order` field — the stored input vector is
  never physically moved. A reorder marks the affected outputs out of sync via the lib's
  input-composition staleness axis (`detectInputCompositionChange`, surfaced by `sanitize()` at the
  next Refresh/Render/reopen), and marks the workspace dirty.
- A toolbar row above the grid shows: project name, input directory, link/unlink
  canvas profile button, output profile selector.

### 2.3 Image Tile — `ImageTile`

Single card in the project grid.  Shows:

- Thumbnail (loaded asynchronously via `ThumbnailCache + QtConcurrent`)
- Filename (short)
- Processing status badge with a colour-coded left border, keyed on `FileStatus` plus, for a Processed
  input, whether a canvas profile was applied:
  Pending (grey) / Processed / Done (green) / Modified (orange) / Missing (red) /
  Desynchronized "Out of sync" (amber) / **Skipped (violet)** — the render did not include this page
  (missing / load error) / **"Processed (no canvas profile)" (cyan)** — the page was rendered
  implicitly, without a matching canvas profile (no margins). The cyan state is not a `FileStatus`: it
  is derived from `InputFile::canvasProfileId` being empty on a Processed input, so it persists across
  reopen
- Contribution indicator (which output slices this file feeds into)

`ImageTile::setStatus(FileStatus, bool renderedWithoutProfile)` repaints only the badge + border (no
thumbnail reload), so the tile can be updated live during a render (see §4.4) without re-running the
async thumbnail load. The `renderedWithoutProfile` flag drives the cyan "Processed (no canvas profile)"
state.

### 2.4 Profile Dialogs

| Dialog | Library type | Trigger |
|---|---|---|
| `CanvasProfileDialog` | `CanvasProfile` | Add / Edit in ManageCanvasProfilesDialog |
| `OutputProfileDialog` | `OutputProfile` | Add / Edit in ManageOutputProfilesDialog |
| `ManageCanvasProfilesDialog` | `Workspace::canvasProfiles()` | "Canvas Profiles…" action |
| `ManageOutputProfilesDialog` | `Workspace::outputProfiles()` | "Output Profiles…" action |

`ManageCanvasProfilesDialog` also emits `generateTemplatesRequested(QList<CanvasProfile>)`
when the user requests template PNG generation.

The dialogs edit **copies**; the workspace is mutated only on accept, and only through the lib's
`Infrastructure::WorkspaceEditor` (the palettes are private in the model — see the lib spec §7.5). The
GUI does not mint ids, deduplicate, preserve `templateInfo`, or strip presets itself; the editor does.

---

## 3. Application State

All persistent state is stored in `Platemaker::Models::Workspace` — the GUI
never has its own parallel data model.  `MainWindow` owns the single live `m_workspace` (the whole
model is loaded at open); each `Project` widget holds a **reference** to it and is a live view over
`m_workspace.projectItems[m_projectIndex]`, not a copy.  Project widgets are created lazily, one per
open dock.

```
MainWindow
  └── m_workspace : Workspace          // loaded from .platemaker.json; the source of truth
  └── m_workspacePath : QString        // current file path
  └── m_dirty : bool                   // unsaved changes

Project (one per open project dock)
  └── m_workspace : Workspace&         // reference to MainWindow's workspace
  └── m_projectIndex : int             // index into m_workspace.projectItems
```

**Mutation goes through the library.** The workspace's profile palettes and the projects' profile-link
fields are private in the model; the GUI edits them only through `Infrastructure::WorkspaceEditor`
(`replaceCanvasProfiles` / `replaceOutputProfiles`, `add`/`removeCanvasProfileToProject`,
`setProjectOutputProfile`, `setCanvasProfileTemplateInfo`).  It reads through the const accessors
(`canvasProfiles()`, `outputProfileId()`, …).

**Keeping open views in sync.** A workspace-level profile edit (Manage/New/Edit) emits
`MainWindow::workspaceProfilesChanged`, connected in `openProjectDock()` to
`Project::refreshProfileViews()` on every open dock — so the output-profile combo and assigned-canvas
list of all open projects update at once, without a manual refresh.

**Dirty tracking rules:**
- Any profile edit → `m_dirty = true` → asterisk in title bar
- Any image tile reorder → `ProjectEditor::setInputOrder`/`moveInput` (rewrites `order`) + `m_dirty = true`
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
  → Project dock opens (tabbed with the workspace panel), or is raised if already open
  → mergeFileScan() populates tile grid from input directory
  → thumbnails load asynchronously in background
```

### 4.4 Run Pipeline

The pipeline runs on a `RenderWorker` moved to its own `QThread`; it holds **copies** of the inputs,
profiles and output dir, so it never touches the live workspace.  The lib reports progress through a
`Core::ProcessingCallbacks` struct (plain `std::function`s called synchronously on the worker thread);
each callback lambda does one cheap thing — `emit` a Qt signal — which is delivered to the main thread
via a queued connection, so the *reaction* (repainting tiles) happens on the main thread while the
render is never blocked on the UI.

```
Process → Run  (or the project's Render button)
  → startRender(projectIndex):
      project.sanitize(workspace.canvasProfiles())   // refresh statuses (disk + config)
      if up-to-date and no config change → inform user, skip
      confirm if outputs are stale (format/size/canvas changed since last render)
      worker = new RenderWorker(copies…);  worker.moveToThread(thread)
      connect worker → MainWindow:  progress, log,
                                    sliceSaved(index,…)  → setOutputTile(index,…)   // live, positional
                                    inputStatus(path,…)  → setInputTileStatus(path,…) // live, phase 1
      ProcessingPipeline::run(inputs, outProfile, canvasProfiles, canvasProfileIds,
                              outDir, cancel, callbacks, onlySlices?, cacheDir)  // static; worker thread
      show progress bar + Stop button
  → onRenderFinished():
      project.applyProcessingResults(records, appliedProfiles, outcome.skippedPages,
                                     workspace.canvasProfiles(), outDir, timestamp)
        // skipped pages are recorded as FileStatus::Skipped, not Processed
      delete orphaned outputs the new config no longer produces
      populate()  + WorkspaceSerializer::save()
```

During phase 1 (strip building) each input's tile turns green as it is appended, cyan **Processed
(no canvas profile)** when it is rendered without a matching profile, or violet **Skipped** when it is
left out (missing / load error); then output tiles stream in per slice.  See §2.3.

**Render output contract (consumer side — lib SPECIFICATION §7.0).** `startRender` passes the workspace's
`.platemaker-cache` dir to `run()`, so the pipeline warms each slice's thumbnail from its **in-RAM** pixels
*before* `sliceSaved` fires. The output tile's `getOrGenerate()` is then a **cache hit** that never
re-reads a slice the render is still writing — this closes a Windows read/write race that used to abort
re-renders with *unable to open for write*. Rules the GUI must keep: treat `sliceSaved(path)` as the only
"ready" signal (never read an output before it or during a run that rewrites it); a locked output surfaces
as `ProcessingErrorCode::OutputLocked` and is shown as a short *Render failed — see the action log* status
(the lib does not retry — that policy is ours). Requires **libplatemaker 0.5.0**.

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
  → ManageCanvasProfilesDialog opens on a COPY of workspace.canvasProfiles()
  → Add / Edit / Delete happen on that copy inside the dialog
  → OK  → WorkspaceEditor(m_workspace).replaceCanvasProfiles(copy)
            // mints ids for new profiles, dedups, carries templateInfo by id
          setDirty(true);  emit workspaceProfilesChanged()   // open projects refresh live
```

The output-profile equivalent is the same through `replaceOutputProfiles()` (which additionally strips
any preset — presets are code-defined and never persisted).  Single-profile edits (edit-active,
double-click) use a copy → mutate → replace of the whole palette, since the palette is private and
cannot be mutated in place.

**Conflict guard** (lib SPECIFICATION.md §7.5.2):  
Linking a canvas profile to a project goes through
`WorkspaceEditor::addCanvasProfileToProject()`, which returns `false` when the profile's canvas W×H
collides with one already linked; the GUI shows an error and does not link it.  The project's
`canvasProfileIds` are private in the model, so a raw bypass of this guard is not possible.

---

## 5. Thumbnail Loading Policy

- Thumbnails are **never** loaded on the main thread.
- Each `ImageTile` requests its thumbnail via `QtConcurrent::run()` on first paint.
- `ThumbnailCache::getOrGenerate(filePath)` is the only call made in the worker.
- On completion the worker emits a signal back to the tile; the tile calls `update()`.
- A placeholder grey rect is shown while loading.
- Failed thumbnails show an error icon; the tile remains interactive.
- **During a render, output thumbnails are pre-warmed by the library, not read from the output file.**
  The pipeline writes each slice's thumbnail into the same cache from the in-RAM slice before `sliceSaved`
  (§4.4), so the output tile's `getOrGenerate()` is a cache hit. File-reading generation (`getOrGenerate`
  opening the source) is therefore only for **input** tiles and **at-rest** output tiles (reopening a
  workspace) — never for an output while a render is writing it, which is what avoids the read/write race.

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

---

## 9. Windows Security Hardening

Platemaker is an **unsigned** desktop app that ships a large bundled DLL closure (Qt, `platemaker.dll`,
the whole libvips graph, the MinGW runtime), all copied next to the executable (§7). While debugging
drag-and-drop we noticed third-party **global hooks** injecting themselves into our own process (LG
OnScreen Control's `ScreenSplitterHook64X.dll`; RGB software) — benign, but it exposed that the process
took no injection/hijacking precautions. This section records the security posture and, importantly,
what we deliberately do **not** do and why.

### 9.1 What we do — restrict the DLL search path

`app/main.cpp` calls `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
LOAD_LIBRARY_SEARCH_SYSTEM32)` as the **first statement in `main()`** — before `QApplication`, before
any post-`main` dynamic DLL load. This drops the current working directory and `PATH` from the default
`LoadLibrary` search, closing the classic **DLL search-order hijack / planting** vector (drop a
malicious `zlib1`/`libpng` on `PATH` or beside a data file and have our graph pick it up).

- **Why it's safe here:** the entire dependency closure is co-located in the application directory, which
  `LOAD_LIBRARY_SEARCH_APPLICATION_DIR` still covers, and the exe's static imports are resolved *before*
  `main()` runs — so startup linkage is unaffected. Only *post-`main`* dynamic loads are narrowed (Qt
  plugins, loaded by absolute path with their deps in the app dir; libvips operation DLLs at render
  time). Verified by launching + running a full render under both the Qt Creator run environment (which
  injects the Qt bin via `PATH`) and the installed build.
- **Robustness:** the API is resolved dynamically via `GetProcAddress`, so it degrades to a no-op on any
  pre-Windows-8 host instead of failing to load, and sidesteps MinGW header/`_WIN32_WINNT` quirks. It
  logs a one-line startup notice (`DLL hardening: …`) so the code path is verifiable in DebugView / the
  Qt Creator Application Output.

### 9.2 What we defer — blocking extension-point injection

The natural companion is `SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, …)`, which would
block exactly the hook injection we observed, plus `AppInit_DLLs` and legacy IME DLLs. **We deliberately
do not ship it.** It also disables **legacy IMM32 IMEs** and some **accessibility / assistive tools**.
Broken IME would hurt non-Latin text entry for Korean / Japanese / Chinese authors — a core webtoon
audience — and we have no way to validate that without a CJK IME test rig (modern TSF IMEs are usually
unaffected; legacy ones are not). The benign, low real-world risk of hook injection (the attacker already
needs hook-registration privileges on the machine — not a remote vector) does not justify that
regression. **Revisit** once code signing lands or an IME test rig exists; the deferred snippet + caveat
live in `docs/TODO.md`.

### 9.3 What this does *not* address

Neither mitigation affects **SmartScreen / AV reputation** — for an unsigned app that is per-file-hash and
resets each release. **Code signing** is the real integrity/reputation fix; the search-path restriction is
defence-in-depth layered under it, not a substitute.
