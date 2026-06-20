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

## Stage 4 — prep: Input-tab controls ✅

- [x] **Add from directory** — `pushButtonAddInputsFromDir`, additive + dedup
  (combine multiple folders; same file never added twice)
- [x] **Add files** — `pushButtonAddInputs`, multi-select via `QFileDialog`
- [x] **Clear** — `pushButtonInputClear`, "Are you sure?" prompt → `mergeFileScan({})`
- [x] **Sort** — `comboBoxSortingOpt` (Name / Date created / Date modified) +
  `pushSortyByApply`; rewrites `InputFile::order` (manual drag-reorder still wins)
- [x] **Go to rendering** — `pushButtonGoToOutput` switches to the Output tab

- [ ] **Auto-sort rules** (`groupBoxAutosort`) — pattern/regex-based ordering:
  `lineEditInputNameRegex` body token (e.g. `chap_<num>` → chap_001, chap_002…),
  `lineEditPrependedRegex` (e.g. `title_<num>` first), `lineEditAppendedRegex`
  (e.g. `end_<num>` last); `pushButtonAutosortApply` applies. Complex token/regex
  parsing — dedicated future task.

---

## Stage 4 — prep: Output-tab controls ✅

- [x] **Output profile** — `comboBoxOutputProfile` with "Choose output profile"
  placeholder; writes `ProjectItem::outputProfileId`
- [x] **Output directory** — `pushButtonODSelect`/`pushButtonODClear`, full path in
  `textOutputDirectory`, persisted to `ProjectItem::getOutputDirectory()`; last dir
  remembered in `QSettings` (`lastOutputDir`)
- [x] **Image format** — `comboBoxImageFormatSelection` ("Select image format" +
  PNG/JPEG/WebP) reveals the matching `groupBoxPNG`/`groupBoxJPG`/`groupBoxWebP`
  (show/hide only for now)
- [x] **Go to input settings** — `pushButtonJumpToInput` switches to the Input tab
- [x] **Render guards** — `pushButtonRender` blocks on missing output profile /
  output directory (full pipeline is the Stage 4 task below)

- [ ] **Bind format option groups to the model** — once the lib exposes
  `PngOptions`/`WebpOptions` on `OutputProfile`, wire `groupBoxPNG` (compression,
  interlaced), `groupBoxJPG` (quality, subsampling, optimize, progressive),
  `groupBoxWebP` (quality, lossless, effort) to the selected output profile.
- [ ] **Output size estimation / limits (UI)** — show estimated avg/max slice size
  and total batch size, and warn on platform caps (Webtoon ≤ 2 MB/slice,
  ≤ 25 MB/chapter). Estimate computed by the lib (mirrored in lib TODO); GUI
  displays before render and/or reports after.

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

## Stage 5 — Template Generation ✅

- [x] **TemplatesDialog** (`actionGenerate_templates` / `actionManage_templates`)
  - Operates on the live `Workspace&`; lists canvas profiles with template status
    (Not generated / Up to date / Outdated / File missing), colour-coded
  - Generate selected / generate all outdated / open folder / delete template
  - PNGs written to `<workspace-dir>/templates/<profile-name>.png`
- [x] **Quick generate from selected** — `ManageCanvasProfilesDialog` button emits
  the selected profile; `MainWindow` generates + preserves `templateInfo` by id
- [x] **Template tracking (lib)** — `CanvasProfile::templateInfo` (relative path +
  canvas-only fingerprint + timestamp); `TemplateGenerator::signature()` drives
  staleness detection; serialized additively (no schema bump)
- [x] **id correctness fixes** — `ManageCanvasProfilesDialog` edit preserves id +
  templateInfo; duplicate clears them (avoids id collision / inherited template)

---

## Stage 6 — Polish

- [x] **Menu** - `Projects` is obsolete, project managemnt should be done in workspace dock/tab.
- [x] **Workspace panel** — tree or list widget with project names, status icons, add buttons for deleting and renaming
- [x] **Recent files** — `QSettings` (IniFormat, app-config dir) holds up to 10 recent workspace paths; dynamic submenu on `actionOpen_recent_workspace`; Open/New dialogs default to the most recent workspace's folder; stale entries self-prune
- [ ] **Auto-save** on pipeline finish (optional setting)
- [ ] **Keyboard shortcuts** — `Ctrl+S` save, `F5` or `Ctrl+R` run, `Esc` cancel
- [ ] **Drag files / folders onto Project window** — triggers `mergeFileScan()`
- [ ] **Undo / Redo** (`Ctrl+Z` / `Ctrl+Y`) — for input-list operations (add,
  clear, reorder, sort) and ideally other reversible workspace edits
- [ ] **About dialog** — version, libplatemaker version, Qt version, licence
