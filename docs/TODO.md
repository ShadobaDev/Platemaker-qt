# Platemaker GUI — TODO

Priority order: each section depends on the previous.  Complete in order.

---

## Stage 4 — prep: Input-tab controls ✅

- [ ] **Auto-sort rules** (`groupBoxAutosort`) — pattern/regex-based ordering:
  `lineEditInputNameRegex` body token (e.g. `chap_<num>` → chap_001, chap_002…),
  `lineEditPrependedRegex` (e.g. `title_<num>` first), `lineEditAppendedRegex`
  (e.g. `end_<num>` last); `pushButtonAutosortApply` applies. Complex token/regex
  parsing — dedicated future task.

---

## Stage 4 — prep: Output-tab controls ✅

- [ ] **Output size estimation / limits (UI)** — show estimated avg/max slice size
  and total batch size, and warn on platform caps (Webtoon ≤ 2 MB/slice,
  ≤ 25 MB/chapter). Estimate computed by the lib (mirrored in lib TODO); GUI
  displays before render and/or reports after.

---

## Stage 4 — Pipeline (Process) ✅

- [ ] **Pre-flight sanitize off the UI thread** — `project.sanitize()` currently
  hashes inputs on the main thread before launching; move to the worker for very
  large projects to avoid a brief UI pause.

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

- [ ] **Bump the lib pin when 0.3.0 lands** — `find_package` is now pinned via
  `LIBPLATEMAKER_VERSION` (currently `0.2.0`), which also builds the FetchContent URL, so the
  required and downloaded versions cannot drift. Moving to 0.3.0 is a one-line change to that
  variable.

  Caveat: the pin does not fully hold until the lib switches its config-version file from
  `SameMajorVersion` to `SameMinorVersion` (tracked in the lib TODO). With major `0`, the
  current setting treats every `0.y` as compatible, so a `0.2.0` pin also accepts `0.3.0`
  and `0.4.0` — it rejects anything *older*, which is the case that bites in practice, but
  not a newer incompatible one.

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

---

## Stage 6 — Polish

- [ ] **Auto-save** on pipeline finish (optional setting)
- [ ] **Keyboard shortcuts** — `Ctrl+S` save, `F5` or `Ctrl+R` run, `Esc` cancel
- [ ] **Drag files / folders onto Project window** — triggers `mergeFileScan()`
- [ ] **Undo / Redo** (`Ctrl+Z` / `Ctrl+Y`) — for input-list operations (add,
  clear, reorder, sort) and ideally other reversible workspace edits
- [x] **About dialog** — version, libplatemaker version, Qt version, licence
- [ ] **Show the libvips version in About** — the dialog now names libvips and its licence
  (LGPL-2.1-or-later), but not its version, because the GUI cannot obtain it: the lib links
  `VIPS::vips-cpp` as **PRIVATE** (deliberately — `VipsImage` never appears in the public API),
  so nothing vips-related reaches the GUI. The CLI can print it only because it links vips
  directly.

  Needs the lib to expose it, two options:
  - *Build-time*: add `libvips_version` to the generated `platemaker/version.hpp` from CMake
    (`pkg_check_modules` already sets `VIPS_VERSION`). Zero runtime cost, but it reports what
    the lib was **built** against — needs a fallback for the MSVC/FetchContent branch, which
    has no pkg-config.
  - *Runtime*: a small accessor in the lib (e.g. next to `ImageIO`) returning
    `vips_version(0/1/2)`. Reports the DLL actually loaded, which is the more honest answer
    for a bundled app where the shipped DLL can be swapped.

  Either is **additive** (no API break), so it can ride along with any release; the runtime
  accessor is the better answer if the version is meant to help diagnose a user's install.

  **Superseded by the lib-side plan:** the lib TODO now proposes `linkedComponents()`,
  returning name + version + licence for everything it links. That closes this item and the
  one below in one go — the About table would be built from what the lib reports instead of
  from values this repo asserts about someone else's code.

- [ ] **Move the licence values out of the GUI once `linkedComponents()` exists** — they are
  currently `set()` in `CMakeLists.txt` and injected as `PLATEMAKER_*_LICENCE` defines. That
  is a real improvement over hardcoding them in the dialog (one build-file edit, no code
  hunt), but the libplatemaker and libvips rows still describe code this repo does not own,
  so they can silently go stale. Keep the CMake defines only for Platemaker itself and Qt;
  take the rest from the lib.
- [ ] **Window icon is loaded from a relative path** — `main.cpp` calls
  `setWindowIcon(QIcon("icons/icon-red.ico"))`, resolved against the *working directory*, so
  it silently yields a null icon whenever the app is not started from the install folder.
  The title-bar icon still appears because Windows takes that from the executable's own
  resource (`app.rc`), which hides the failure — it surfaced only as an empty band above the
  About text, where the icon was supposed to be.

  Fix: load it from the Qt resource system (`:/icons/…`), which the app already has via
  `app/resources.qrc` — same mechanism as the menu icons, and independent of the working
  directory.

- [ ] **Slice tiles should have hidden edit panel** because this are neither to be deleted nor reordered by the platemaker
- [ ] **Process bar** change style - a solid 15px bar - light broder - empty part background color, filled part grey, error or halt - red.
- [ ] **ImageTile** rework to be more eye-appealing
- [ ] **Action log** should report a summary, how manu inputs, how many slices in what time where processed and when. Output cumulative size (MB or KB) would also be nice.
- [ ] **Segfault** was detcted but not written down how - to be investigated.
- [ ] `MainWindow::m_savedSnapshot` Maybe sha256 instead of holding full string? We do not use it for recovery anyway... or maybe we should keeep for recovery purpose?
- [ ] menuPlatemaker in many collapsable combolists there are positions that have duplicated and misaligned shortcut hints
- [x] CMake FetchContent cannot find release, needs to be fixed
---

## Stage 7 — Skins and styles

- [ ] **App looks flat/colorless on Linux vs Windows** — no explicit style is
  set in `main.cpp`, so Qt falls back to native per-platform styling: Windows
  gets `windows11`/`windowsvista` (dark mode aware, styled GroupBox borders,
  accent colors); Linux falls back to a much plainer default. Consider
  `QApplication::setStyle("Fusion")` plus a shared custom `QPalette`/QSS so the
  look is consistent (and intentional) across platforms instead of relying on
  whatever the native style happens to provide.