# Platemaker GUI — TODO

Priority order: each section depends on the previous.  Complete in order.

---

## Stage 1 — Workspace & Project Lifecycle (core wiring) ✅

These are the foundation — nothing else is usable without them.

- [x] **Open Workspace** (`File → Open Workspace`)
- [x] **New Workspace** (`File → New Workspace`)
- [x] **Save Workspace** (`File → Save`, `Ctrl+S`)
- [x] **Close + unsaved-changes prompt**
- [x] **Open Project** (double-click in workspace panel)
- [x] **New Project** (`Projects → New Project`)

---

## Stage 2 — Image Tile Grid

Requires Stage 1 (project must be open with `InputFile` list populated).

- [ ] **Populate tile grid from `ProjectItem::getInputImages()`**
  - Create one `ImageTile` per `InputFile`, ordered by `InputFile::order`
  - Pass `filePath` and `status` to each tile

- [ ] **Thumbnail loading (async)**
  - On `ImageTile` first paint: `QtConcurrent::run([path]{ ThumbnailCache::getOrGenerate(path); })`
  - On worker finish: `QMetaObject::invokeMethod` → set pixmap, `update()`
  - Show placeholder rect while loading; show error icon on failure

- [ ] **Status badge**
  - Reflect `FileStatus` enum visually: colour dot or label overlay
  - Update tiles after `project.sanitize()` is called (before pipeline run)

- [ ] **Drag-and-drop reorder**
  - Enable `QAbstractItemView`-style DnD or custom mouse handling in `Project`
  - After drop: update `InputFile::order` for all affected tiles, set `stripDirty = true`

---

## Stage 3 — Profile Management (wiring dialogs to workspace)

Dialogs exist; they need to read/write `m_workspace`.

- [ ] **Canvas Profiles — full CRUD**
  - `Workspace → Canvas Profiles…` opens `ManageCanvasProfilesDialog`
  - Pass `m_workspace.canvasProfiles` by reference (or copy + commit on OK)
  - **Add**: call `CanvasProfileDialog`, append to list, generate `id = "cp-" + timestamp`
  - **Edit**: call `CanvasProfileDialog`, update in-place
  - **Delete**: warn if profile is linked to any project (`canvasProfileIds` check), then remove
  - On OK: `m_dirty = true`

- [ ] **Output Profiles — full CRUD**
  - Same pattern as canvas profiles
  - **Delete**: warn if profile is the `outputProfileId` of any project

- [ ] **Link canvas profile to project**
  - Button in `Project` toolbar: "Assign Canvas Profile"
  - Show list of `m_workspace.canvasProfiles` not yet linked
  - Call `addCanvasProfileToProject()` (libplatemaker) — display conflict error if returned
  - Show linked profiles in project toolbar as chips/tags

- [ ] **Select output profile for project**
  - Drop-down in `Project` toolbar, populated from `m_workspace.outputProfiles`
  - Sets `ProjectItem::outputProfileId`, marks `stripDirty = true`

---

## Stage 4 — Pipeline (Process)

Requires Stages 1–3 (workspace loaded, project open, profiles linked).

- [ ] **Run button wired**
  - `Process → Run` (menu) + toolbar button
  - Guard: if no project active → disable
  - Call `project.sanitize()` to refresh file statuses
  - If `project.isUpToDate()` → inform user via status bar, skip

- [ ] **Background worker**
  - `QtConcurrent::run()` with the full pipeline (see SPECIFICATION.md §4.4)
  - Emit `progressChanged(int sliceIndex, int totalSlices)` signal to main thread
  - Emit `processingFinished(bool cancelled, int skippedCount)` on completion

- [ ] **Progress bar**
  - Bottom dock: `QProgressBar` 0–100%
  - Update on `progressChanged` signal (convert slice index to percent)
  - Hide or reset on idle

- [ ] **Cancel button**
  - Enabled only while pipeline is running
  - Calls `m_cancellationToken.cancel()`
  - Worker checks token between slices

- [ ] **Post-run refresh**
  - Call `project.applyProcessingResults(...)` in worker before emitting `processingFinished`
  - On `processingFinished`: refresh tile status badges, `m_dirty = true`, auto-save

---

## Stage 5 — Template Generation

- [ ] **Wire `generateTemplatesRequested` signal**
  - `ManageCanvasProfilesDialog` emits this; `MainWindow` receives it
  - `QFileDialog` → choose output directory
  - `QtConcurrent::run()` → `TemplateGenerator::generate()` per profile
  - Show result: open directory in file manager (`QDesktopServices::openUrl`)

---

## Stage 6 — Polish

- [ ] **Workspace panel** — tree or list widget with project names, status icons
- [ ] **Recent files** (`QSettings` — recent workspace paths in `File` menu)
- [ ] **Auto-save** on pipeline finish (optional setting)
- [ ] **Keyboard shortcuts** — `Ctrl+S` save, `F5` or `Ctrl+R` run, `Esc` cancel
- [ ] **Drag files / folders onto Project window** — triggers `mergeFileScan()`
- [ ] **About dialog** — version, libplatemaker version, Qt version, licence
