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

## Stage 2 — Image Tile Grid ✅

- [x] **Populate tile grid from `ProjectItem::getInputImages()`**
- [x] **Thumbnail loading (async)** — `QtConcurrent::run` + `QFutureWatcher`, fits within 160×120 with `KeepAspectRatio`
- [x] **Status badge** — colored left border: gray=Pending, green=Processed/Done, orange=Modified, red=Missing
- [x] **Drag-and-drop reorder** — `InternalMove` on `listImageTile`, `rowsMoved` → updates `InputFile::order`

---

## Stage 3 — Profile Management (wiring dialogs to workspace)

- [x] **Canvas Profiles — full CRUD** — `actionManage_profiles` → `ManageCanvasProfilesDialog`, wired in `MainWindow`
- [x] **Output Profiles — full CRUD** — `actionManage_output_profiles` → `ManageOutputProfilesDialog`, wired in `MainWindow`

- [x] **Link canvas profile to project** — lib ready (`canvasProfileIds` in `ProjectItem`)
  - `pushButtonAssignCanvasProfiles` → `QInputDialog::getItem` → `addCanvasProfile()` with conflict guard
  - `listWidgetCanvasProfiles` shows assigned profiles; double-click edits via `CanvasProfileDialog`; right-click removes assignment
  - Empty `canvasProfileIds` shows "(all workspace profiles)" placeholder

- [x] **Select output profile for project** — lib ready (`outputProfileId` in `ProjectItem`)
  - `comboBoxOutputProfile` populated from workspace output profiles; "(workspace default)" as first item
  - `currentIndexChanged` → `ProjectItem::outputProfileId`, emit `projectModified`

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
