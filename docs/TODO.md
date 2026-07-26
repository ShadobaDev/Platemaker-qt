# Platemaker GUI — TODO

Roadmap grouped by **which semver position a change forces**, not by priority or by the order
work happens in.

**How to read this**
- Version numbers are provisional, non-binding hints — a *kind of bump*, not a schedule.
- A section is the **minimum bump the change forces**. The GUI is post-1.0, so plain semver: a
  new backward-compatible **feature** bumps the **MINOR**; a **fix, cosmetic change or internal
  refactor** that does not change behaviour bumps the **PATCH**; only something that **strands the
  user** — e.g. a workspace format an older build cannot open — bumps the **MAJOR**.
- **Lib gate.** The GUI's contract is to its user, but the *timing* of some items depends on a
  libplatemaker version. The bucket states the user-facing impact; the item body says which lib
  version unblocks it.
- **Cascade.** Whichever section releases first takes its slot; the rest re-derive from the new
  baseline.

Baseline: **1.1.0 released, 1.2.0 in progress** (`CMakeLists.txt`). 1.2.0 adds the About dialog's
runtime component report (libplatemaker / libvips / nlohmann/json versions + licences, sourced from
the lib), so per the cascade the pending patch bucket re-derives to 1.2.1 and the next minor to 1.3.0.

---

## PATCH — next: 1.2.1

Bug fixes, cosmetics and internal cleanups — no new capability, no change to an existing workflow.

- [ ] **Input tile ▲/▼ reorder buttons do nothing** — clicking a tile's up or down arrow has
  no effect; only drag-and-drop reordering works. Not a wiring problem: the buttons emit
  correctly (`widgets/imagetile/imagetile.cpp:35-40` → `moveUpRequested`/`moveDownRequested`),
  the Project slots are connected (`widgets/project/input.cpp:46-47`), and `moveByOrder()`
  (`input.cpp`) swaps the `order` fields correctly.

  **Cause: the buttons never receive the click.** `imagetile.cpp:28-33` sets
  `Qt::WA_TransparentForMouseEvents` on a list of widgets so mouse presses on the display area
  fall through to the `QListWidget` viewport and can start a drag. That list includes
  **`ui->widget`**, and per the `.ui` `ui->widget` is the *container that holds both move
  buttons* (`widget → horizontalLayout_2 → verticalLayoutMoveButtons → pushButtonMoveUp` /
  `pushButtonMoveDown`). The attribute disables mouse delivery to the widget **and its
  children**, so the buttons inside it are dead — directly contradicting the code comment on
  `imagetile.cpp:27` ("The buttons stay interactive"). Drag-and-drop works precisely *because*
  the fall-through it relies on is what kills the buttons.

  **Fix:** exclude `ui->widget` from the transparent-for-mouse set — make only the true display
  widgets fall through (`ui->frame`, `ui->imageLabel`, `ui->textBrowser`), or restructure the
  `.ui` so the buttons are not descendants of a mouse-transparent container. Verify the drag
  still starts from the thumbnail/text area after the change.

  Documented as current behaviour in the wiki (`Manual-Projects` presents drag-and-drop as the
  way to reorder); revisit that page once fixed.

- [ ] **Window icon is loaded from a relative path** — `main.cpp` calls
  `setWindowIcon(QIcon("icons/icon-red.ico"))`, resolved against the *working directory*, so
  it silently yields a null icon whenever the app is not started from the install folder.
  The title-bar icon still appears because Windows takes that from the executable's own
  resource (`app.rc`), which hides the failure — it surfaced only as an empty band above the
  About text, where the icon was supposed to be.

  Fix: load it from the Qt resource system (`:/icons/…`), which the app already has via
  `app/resources.qrc` — same mechanism as the menu icons, and independent of the working
  directory.

- [x] **Output tiles flash green (Done) at the start of a config-change re-render** — change the
  output profile, hit *Refresh files*, outputs correctly show amber *Out of sync*; click *Render*,
  the *"Settings changed"* dialog appears, and every output tile turns green (Done) the instant the
  render *starts* — before a single slice is regenerated (the worker isn't even running yet; it's
  parked on the modal dialog). Cosmetic, but it reads as "already finished" mid-run.

  **Confirmed cause: the render-start refresh drops the config-stale overlay.** `onRefreshFiles()`
  (`widgets/project/output.cpp`) did *two* steps — `project.sanitize()` (disk + canvas check),
  **then** the output-profile staleness overlay (`outputsConfigStale()` flips still-`Done` outputs
  to `Desynchronized`) — before `populate()`. `startRender()` (`mainwindow/render.cpp:151-152`) did
  only the first: `sanitize()` then `populate()`, *before* the confirm dialog at line 218.
  `sanitize()` judges outputs against disk alone (old files still exist and hash-match → `Done`);
  `populate()` → `refreshOutputTiles()` read model status directly and never reapplied the overlay,
  so they painted green. The overlay lived *only* in `onRefreshFiles`.

  **Not a regression.** The overlay *and* the whole config-change dialog were introduced together in
  commit `80b5233` ("detect stale outputs"); it added the overlay to `onRefreshFiles` but not to
  `startRender`. So the asymmetry has existed since stale-detection first shipped. Before `80b5233`
  there was no out-of-sync state at all (outputs were always green after Refresh) — nothing to flash
  away, which is why it felt correct — and it is invisible on a first-ever render (no existing
  outputs to flash). Both explain the "I'm sure this used to work" impression.

  **GUI-only — no lib dependency.** Reuses `outputProfileSignature()` / `detectCanvasConfigChange()`
  (already in the lib) on the main thread, before the worker starts. See the cross-reference on
  *"Live input tile status during a render"* below: the two share **no** lib change.

  **Fixed:** folded the `Done`→`Desynchronized` overlay into `refreshOutputTiles()` (the one path
  every `populate()` goes through), and dropped the now-duplicate block from `onRefreshFiles()`. So
  every repaint — load, render-start, finish, Refresh — reflects staleness uniformly. Safe because
  `onRenderFinished()` updates `project.outputSignature` *before* repopulating, so freshly-rendered
  outputs are no longer stale and stay `Done`.

- [ ] menuPlatemaker in many collapsable combolists there are positions that have duplicated and misaligned shortcut hints

- [ ] **Segfault** was detcted but not written down how - to be investigated.

- [x] **Show the `(preset)` marker wherever a profile name is shown, not just in the manage
  dialog** — done as part of the lib 0.3.0 preset redesign (presets are code-defined, never
  persisted, resolved from the catalogue). The Output tab combo (`refreshOutputProfileCombo()`,
  `widgets/project/output.cpp`) now lists the user's own profiles **and** the presets, appending
  `   (preset)` and colouring preset rows preset-blue; selection is by id so a project can target
  either. Provenance is tested with `outputPresetDefById(id) != nullptr` (the old
  `isOutputProfilePresetId()` was removed with the prefix-as-type scheme). Presets are read-only:
  the Manage dialog greys out Edit/Delete on preset rows, and inline format editing is disabled
  when a preset is selected. `MainWindow` merges presets into the Manage dialog and writes back
  only user profiles; a new workspace no longer seeds presets into `outputProfiles`.

- [ ] **Stale comment: templates no longer draw slice guides** —
  `widgets/templatesdialog/templatesdialog.cpp` still says *"The output profile only supplies
  cosmetic slice-guide lines — use the workspace default; it is not part of the template's
  tracked identity"*, and `generateTemplate()` still takes an `OutputProfile` and falls back to
  `ws.outputProfiles.front()`. But the lib compiles the feature out:
  `template_generator.cpp` has `#define GUIDLINES_ENABLED 0` and an explicit
  `(void)outputProfile;`. So the parameter is dead weight and the comment describes code that
  does not run. Either drop the parameter (a lib API change — see the lib TODO) or re-enable the
  guides; until then, fix the comment so it stops describing a removed feature.

- [ ] **Slice tiles should have hidden edit panel** because this are neither to be deleted nor reordered by the platemaker

- [ ] **Pre-flight sanitize off the UI thread** — `project.sanitize()` currently
  hashes inputs on the main thread before launching; move to the worker for very
  large projects to avoid a brief UI pause.

- [ ] **`ProjectItem::inputDirectory` is written but never read** — `onAddFromDirectory()`
  (`widgets/project/input.cpp`) stores the chosen directory in
  `projectItems[idx].inputDirectory`, but nothing in the GUI or in libplatemaker ever reads it
  back (the project keeps full absolute paths per input file instead — a leftover from Clip2l's
  flat-directory model). Either remove the field (see the matching entry in the lib TODO, since
  it lives on the lib model) or put it to use: pre-open the last-used directory in the
  **Add all files from directory** dialog. Decide, then either drop the write or wire up the
  read.

- [ ] `MainWindow::m_savedSnapshot` Maybe sha256 instead of holding full string? We do not use it for recovery anyway... or maybe we should keeep for recovery purpose?

- [ ] **Process bar** change style - a solid 15px bar - light broder - empty part background color, filled part grey, error or halt - red.

- [ ] **ImageTile** rework to be more eye-appealing

---

## MINOR — next: 1.3.0

New, backward-compatible features. Several are gated on a lib version, noted in the item body.

- [ ] **Auto-sort rules** (`groupBoxAutosort`) — pattern/regex-based ordering:
  `lineEditInputNameRegex` body token (e.g. `chap_<num>` → chap_001, chap_002…),
  `lineEditPrependedRegex` (e.g. `title_<num>` first), `lineEditAppendedRegex`
  (e.g. `end_<num>` last); `pushButtonAutosortApply` applies. Complex token/regex
  parsing — dedicated future task.

- [ ] **Output size estimation / limits (UI)** — show estimated avg/max slice size
  and total batch size, and warn on platform caps (Webtoon ≤ 2 MB/slice,
  ≤ 25 MB/chapter). Estimate computed by the lib (mirrored in lib TODO); GUI
  displays before render and/or reports after.

- [ ] **Auto-save** on pipeline finish (optional setting)

- [ ] **Keyboard shortcuts** — `Ctrl+S` save, `F5` or `Ctrl+R` run, `Esc` cancel

- [ ] **Drag files / folders onto Project window** — triggers `mergeFileScan()`

- [ ] **Undo / Redo** (`Ctrl+Z` / `Ctrl+Y`) — for input-list operations (add,
  clear, reorder, sort) and ideally other reversible workspace edits

- [ ] **Action log** should report a summary, how manu inputs, how many slices in what time where processed and when. Output cumulative size (MB or KB) would also be nice.

- [ ] **Live input tile status during a render** — output tiles now turn green as each
  slice is written, but input tiles only update when the render finishes. This cannot be
  fixed in the GUI: `ProcessingPipeline::run()` reports `ProgressFn` (per slice), `LogFn`
  and `SliceSavedFn`, but has **no per-input callback** — and inputs are all consumed in
  phase 1 (strip building) before the first slice exists, so there is nothing to hook.

  Needs a lib change: an optional callback such as `InputDoneFn(path, ok)` invoked as each
  input is appended to the strip (or skipped), re-emitted by `RenderWorker` as a signal,
  with the GUI refreshing that input's tile the same way `addOutputTile()` does. Visible
  effect: inputs go green quickly at the start, then slices stream in.

  **Decided: this rides on lib 0.3.0.** `run()` already takes 10 parameters, so rather than
  bolting on an 11th, the lib groups the callbacks into a `ProcessingCallbacks` struct
  (`onProgress` / `onLog` / `onSliceSaved` / `onInputDone`) — see the lib TODO. That is an
  API break, hence the lib minor bump, and this GUI feature lands with it.

  Caveat to expect: after a cancel, `populate()` pulls the inputs back to their model status
  (Modified / out-of-sync), because an unfinished run must not claim them as processed. So
  inputs go green during the run and amber again if it is cancelled — correct, but worth
  knowing before it looks like a bug.

  Rejected alternative: marking every input green once the first slice arrives. It would
  need no lib change but would lie about pages skipped for having no matching canvas
  profile — those are only known once the run completes.

  Not the same as the *"Output tiles flash green"* fix (PATCH): despite the superficial
  resemblance (both about tile status around a render), that one is **GUI-only** — it reapplies
  an existing main-thread staleness overlay before the worker starts and needs **no** lib change.
  This item is the only one of the pair that requires the `ProcessingCallbacks` / `onInputDone`
  lib work, so there is no near-duplicate lib change to introduce twice.

- [ ] **Drop direct `m_workspace` mutations once the lib exposes `WorkspaceEditor`** (lands
  with lib 0.3.0 — see the *WorkspaceEditor* entry in the lib TODO for the rationale and the
  facade shape). The GUI currently reaches into the workspace struct and re-establishes the
  library's invariants by hand — minting profile ids, deduping, preserving `templateInfo` —
  so an edit made in-session is not validated the way a loaded file is. Route these through
  the facade instead.

  Call-site map to rewrite:
  - **`mainwindow/profiles.cpp`** — wholesale `canvasProfiles.assign()` + id minting at
    `115`/`120-122`, output twin at `230`/`235-237`, new-profile minting `157`/`266`, edit
    fallback `323`, and the `templateInfo` snapshot/reattach at `125-128` →
    `replaceCanvasProfiles()` / `replaceOutputProfiles()`.
  - **`widgets/project/input.cpp:112`** — raw `std::remove` on `canvasProfileIds` →
    `removeCanvasProfileFromProject()` (mirrors the existing `addCanvasProfile()`).
  - **`widgets/project/output.cpp:80`** — unchecked `outputProfileId =` →
    `setProjectOutputProfile()` (validates the id exists).

  Reads and navigation (`projectItems[idx]`, iterating for display) stay as they are — the
  facade covers invariant-bearing edits, not every vector access.

- [ ] **Bump the lib pin when 0.3.0 lands** — `find_package` is now pinned via
  `LIBPLATEMAKER_VERSION` (currently `0.2.1`), which also builds the FetchContent URL, so the
  required and downloaded versions cannot drift. Moving to 0.3.0 is a one-line change to that
  variable.

  Caveat: the pin does not fully hold until the lib switches its config-version file from
  `SameMajorVersion` to `SameMinorVersion` (tracked in the lib TODO). With major `0`, the
  current setting treats every `0.y` as compatible, so a `0.2.1` pin also accepts `0.3.0`
  and `0.4.0` — it rejects anything *older*, which is the case that bites in practice, but
  not a newer incompatible one.

- [ ] **App looks flat/colorless on Linux vs Windows** — no explicit style is
  set in `main.cpp`, so Qt falls back to native per-platform styling: Windows
  gets `windows11`/`windowsvista` (dark mode aware, styled GroupBox borders,
  accent colors); Linux falls back to a much plainer default. Consider
  `QApplication::setStyle("Fusion")` plus a shared custom `QPalette`/QSS so the
  look is consistent (and intentional) across platforms instead of relying on
  whatever the native style happens to provide.

---

## MAJOR — next: 2.0.0

Nothing pending. Reserved for a change that strands the user — e.g. a workspace format an older
version cannot open. The format has stayed additive and reads both ways, so no such change is on
the horizon.

---

## To establish / test — no release impact

Investigations, testing and manual/wiki work that ships no code change on their own.

- [ ] **Decide the recommended output format and quality** — the manual currently lists PNG /
  JPEG / WebP neutrally because there is no considered recommendation to give. Users need one
  ("use X unless Y"). Needs a comparison on real pages: file size and visible quality for flat
  line art versus painted work, at a few JPEG quality values, against the platform's per-chapter
  size cap. The first published chapter used JPEG purely to fit 20 MB per chapter, which is a
  constraint rather than a considered choice. Feeds `Manual-Output-Profiles`.

- [ ] **Recent-workspaces behaviour is unverified** — `Open recent workspace…` exists, but the
  cap on the number of remembered entries, and what happens when a remembered workspace has
  been moved or deleted (dropped silently vs. an error), have not been established. Test and
  document; the wiki currently says "to be tested".

---

## Done — shipped in 1.2.0

- [x] **About dialog reports libplatemaker + its dependencies from the lib** — closed together with
  *Show the libvips version in About* and *Move the licence values out of the GUI*. The lib gained
  `Infrastructure::buildInfo()` (its own version + SPDX licence, read at runtime from the loaded DLL)
  and `Infrastructure::linkedComponents()` (libvips at its runtime `vips_version()`, nlohmann/json,
  each with its SPDX licence) in **libplatemaker 0.2.2**. `aboutdialog.cpp` now builds the component
  table from those, and `CMakeLists.txt` keeps compile-def licences only for Platemaker itself and Qt
  — the GUI no longer asserts versions or licences about code it does not own, and libvips finally
  shows a version. Lib pin bumped to `0.2.2`.

- [x] **Licence viewer, GitHub links, and a product SBOM** — the About licences are now clickable and
  open a `LicenceDialog` (`widgets/licencedialog/`) showing the full licence text shipped in
  `credits/licenses/`; component names link to their GitHub project (`github.com` only, validated
  before opening). The lib ships an SPDX SBOM + licence texts (`credits/`, located via
  `platemaker_CREDITS_DIR`) and `CMakeLists.txt` merges it — via CMake `string(JSON)` — with Qt and
  Platemaker into a flat product `credits/sbom.spdx.json` installed beside the executable. This is the
  machine-readable inventory the EU CRA requires and commercial integrators ask for; the licence texts
  meet the LGPL duty to distribute a copy. Full ~89-DLL closure (whole libvips graph) is the remaining
  follow-up — the SBOM extends to it with more entries.

## Done — shipped in 1.1.0

- [x] **Batch render — `actionRender_all_projects_F6`** (currently unwired;
  `mainwindow.cpp` says "F6 'all projects' deferred"). Design decided:

  - **Sequential, not parallel.** The target scenario (a shared page — e.g. the title —
    swapped in every project) makes each project a *full* re-render (changed input →
    `inputsAllProcessed()` false → empty `onlySlices`), so running N in parallel is the
    worst case: N× peak memory + libvips threadpool oversubscribe. A sequential queue
    keeps the existing single-slot render state (`m_cancelToken`, `m_renderProjectIndex`,
    `m_renderOrphan*`) untouched — none of the parallel-mode hazards arise.
  - **Rename the action's display text to "Refresh all projects   F6"** (keep the object
    name). It *skips* up-to-date projects, so "Re-render" would over-promise hours of work
    it won't do; "Refresh" describes sweeping + refreshing the stale ones.
  - **`startRender()` must return `bool`** — `true` only when `thread->start()` was called;
    every early guard (no output profile / dir / inputs, "up to date", config-change prompt
    declined, `mkpath` fail) → `return false`. Without this the queue can't tell "skipped"
    from "started" and stalls on the first skipped project. `onRenderToggle()` ignores the
    result (F5 path unchanged).
  - **New `mainwindow/renderbatch.cpp`** (matches the per-topic split): `onRefreshAllProjects()`
    (guard workspace + `m_rendering` once, build the index queue), `advanceBatch()` (a **loop**
    — `while (!queue.empty())`: pop, `if (startRender(idx)) return;` else record skipped and
    continue; empty → `finishBatch()`), `finishBatch()` (summary of rendered / skipped / failed
    to the status browsers). Loop not recursion, so runs of skipped projects don't deep-stack.
  - **Batch state on `MainWindow`**: `std::vector<int> m_batchQueue`, `int m_batchTotal`,
    `QStringList m_batchOk / m_batchSkipped / m_batchFailed`.
  - **Hook at the very end of `onRenderFinished()`** (after it resets `m_rendering` etc. —
    order matters, `startRender()` bounces off `m_rendering==true`): record this project's
    outcome; `outcome.cancelled` → clear queue + `finishBatch()`; `outcome.failed &&
    !batchShouldContinueAfterFailure(name)` → clear queue + finish; else `advanceBatch()`.
  - **Error policy** in one method `bool batchShouldContinueAfterFailure(const QString&)`
    (a Continue/Abort `QMessageBox`), so switching to "log and continue" is a one-line change.
  - **Config-change prompt stays per-project** for now; leave a seam
    (`enum class ConfigChangePolicy { AskPerProject, /* AskOnceForBatch, SkipInBatch */ }`)
    so "ask once at batch start" is later a branch, not a rewrite of `startRender()`.
  - **Stop semantics**: Esc / `pushButtonStop` cancels the whole batch (worker reports
    `cancelled` → clear queue). No change to `cancelRender()`.
  - Wire the connect + `setShortcut(Qt::Key_F6)` in `mainwindow.cpp` (drop the "deferred"
    comment). Reload/close `mainwindow.ui` in Qt Designer before building (Designer clobbers
    on-disk edits).

- [x] **About dialog** — version, libplatemaker version, Qt version, licence

- [x] CMake FetchContent cannot find release, needs to be fixed
