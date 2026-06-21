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
- [x] **Image format + JPEG options** — `comboBoxImageFormatSelection` and the JPEG
  group (quality / subsampling / optimize / progressive) are an **inline editor of
  the project's selected `OutputProfile`** (shared, persisted; also editable in
  Manage Output Profiles). Reflects the selected profile; reveals the matching
  group; disabled when no profile is selected.
- [x] **Go to input settings** — `pushButtonJumpToInput` switches to the Input tab
- [x] **Render guards** — `pushButtonRender` blocks on missing output profile /
  output directory (full pipeline is the Stage 4 task below)

- [x] **PNG / WebP option groups** — inputs disabled pending lib
  `PngOptions`/`WebpOptions` on `OutputProfile` + `ImageIO` encoder support
  (lib TODO); then bind `groupBoxPNG` (compression, interlaced) / `groupBoxWebP`
  (quality, lossless, effort) like the JPEG group.
- [ ] **Output size estimation / limits (UI)** — show estimated avg/max slice size
  and total batch size, and warn on platform caps (Webtoon ≤ 2 MB/slice,
  ≤ 25 MB/chapter). Estimate computed by the lib (mirrored in lib TODO); GUI
  displays before render and/or reports after.

---

## Stage 4 — Pipeline (Process) ✅

- [x] **Shared pipeline (lib)** — `Core::ProcessingPipeline::run()` (build strip →
  `sliceAll` → save) with progress/log/sliceSaved callbacks + `CancellationToken`;
  CLI `cmdProcess` refactored to use it.
- [x] **Run triggers** — Project `pushButtonRender` (Render⇄Stop toggle),
  `actionRender_current_project_F5` + **F5** (renders the active project dock).
  `actionRender_all_projects_F6` deferred.
- [x] **Threaded worker** — `RenderWorker` on a `QThread`, snapshot copies (never
  touches the live workspace); queued signals to the main thread.
- [x] **Progress bar** — `progressBar` `round(done/total*100)` per saved slice,
  gray normally, **dark red `#b41414`** frozen at % on failure.
- [x] **Cancel** — `pushButtonStop` / `actionStop_Esc` / **Esc** →
  `CancellationToken::cancel()`; worker checks between slices.
- [x] **Status / log** — `textBrowserActionStatus` (`<project>: Processing/Finished/
  Failed/Require action`), `textBrowserActionLogs` (per-slice stream),
  `textBrowserProjectStatus` (status-bar messages).
- [x] **Post-run** — main thread `applyProcessingResults(...)`, refresh input +
  output tiles, `setDirty(true)`. Output tiles also update live via `sliceSaved`.

- [ ] **Pre-flight sanitize off the UI thread** — `project.sanitize()` currently
  hashes inputs on the main thread before launching; move to the worker for very
  large projects to avoid a brief UI pause.

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
- [x] **Missing button open output dir** in tabOutput. 
- [ ] **Slice tiles should have hidden edit panel** because this are neither to deleted nor reorder by the platemaker
- [x] **Image options** are not porperly save/loaded in GUI - project resets it with app restart
- [ ] **Process bar** change style - a solid 15px bar - light broder - empty part background color, filled part grey, error or halt - red.
- [ ] **ImageTile** rework to be more eye-appealing
- [x] **ImageTile** fix/recover drag'n'drop reordering, arrow-button re-ordering and delete buttons
- [ ] **Action log** should report a summary, how manu inputs, how many slices in what time where processed and when. Output cumulative size (MB or KB) would also be nice.
- [x] **Template are not re-rendered** - add checkum comparision
- [x] **Ouput** changed or missing is not detected, `Render` button does nothing even though it should render missing or modified files
  - `ProjectItem::sanitize()` now validates outputs (existence + SHA-256) → `isUpToDate()` is false when an output is deleted/edited, so the Render guard proceeds.
  - **Partial re-render**: pipeline `run()` takes an optional `onlySlices` filter; when inputs are all `Processed` but some outputs are dirty, only the affected slices are regenerated (`applyPartialResults`). CLI + GUI share the same decision.
  - Re-validated on project open, before render, and via a new **Refresh files** button on the Output tab. No `QFileSystemWatcher` (deferred); no background hasher.
- [x] **OutputProfileDialog** (Manage Output Profiles) still edits only JPEG + format (its WebP lines were stubbed/commented). So PNG/WebP options are currently editable only in the project Output tab. Extending that dialog to the new fields is a small follow-up (noted in lib TODO).
- [ ] **Segfault** was detcted but not written down how - to be investigated.