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

---

## Stage 6 — Polish

- [ ] **Auto-save** on pipeline finish (optional setting)
- [ ] **Keyboard shortcuts** — `Ctrl+S` save, `F5` or `Ctrl+R` run, `Esc` cancel
- [ ] **Drag files / folders onto Project window** — triggers `mergeFileScan()`
- [ ] **Undo / Redo** (`Ctrl+Z` / `Ctrl+Y`) — for input-list operations (add,
  clear, reorder, sort) and ideally other reversible workspace edits
- [ ] **About dialog** — version, libplatemaker version, Qt version, licence
- [ ] **Slice tiles should have hidden edit panel** because this are neither to deleted nor reorder by the platemaker
- [ ] **Process bar** change style - a solid 15px bar - light broder - empty part background color, filled part grey, error or halt - red.
- [ ] **ImageTile** rework to be more eye-appealing
- [ ] **Action log** should report a summary, how manu inputs, how many slices in what time where processed and when. Output cumulative size (MB or KB) would also be nice.
- [ ] **Segfault** was detcted but not written down how - to be investigated.
- [ ] `MainWindow::m_savedSnapshot` Maybe sha256 instead of holding full string? We do not use it for recovery anyway... or maybe we should keeep for recovery purpose?
- [ ] menuPlatemaker in many collapsable combolists there are positions that have duplicated and misaligned shortcut hints
- [ ] CMake FetchContent cannot find release, needs to be fixed
---

## Stage 7 — Skins and styles

- [ ] **App looks flat/colorless on Linux vs Windows** — no explicit style is
  set in `main.cpp`, so Qt falls back to native per-platform styling: Windows
  gets `windows11`/`windowsvista` (dark mode aware, styled GroupBox borders,
  accent colors); Linux falls back to a much plainer default. Consider
  `QApplication::setStyle("Fusion")` plus a shared custom `QPalette`/QSS so the
  look is consistent (and intentional) across platforms instead of relying on
  whatever the native style happens to provide.