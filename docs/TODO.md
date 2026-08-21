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

Baseline: **1.4.1 released (2026-08-16), 1.4.2 in progress** (`CMakeLists.txt`). 1.4.0 (2026-08-07)
added the *New from this…* project action, render-log persistence, the render summary and the restyled
progress bar. 1.4.1 is a patch — the DLL search-path hardening, the drag-and-drop fix, `Platemaker.exe`
version metadata, and a bump to **libplatemaker 0.4.1** (metadata-only) so the bundled DLL also carries
identity metadata. The next patch bucket is **1.4.2**; the next feature bucket re-derives to **1.5.0**.

---

## PATCH — next: 1.4.2

Bug fixes, cosmetics and internal cleanups — no new capability, no change to an existing workflow.

- [ ] **Forced dark scheme does not take effect on Windows 10.** Reported from a Win10 test
  (`temp/win10/image (3).png`, `(4)`, `(5)`): the **main window renders light** — menu bar, docks,
  toolbox, tabs, plain controls — while the **hardcoded-dark dialogs stay dark** (About, Output
  Profiles: `image (1).png`, `(2).png`). The result is an inconsistent light-shell / dark-dialog mix
  — a partial return of the very bug the 1.3.0 fix closed, now only on Windows 10.

  **PARTIAL — Win10 clash fixed now via a Fusion-dark fallback** (branch `fix/win10-dark-fusion-fallback`,
  `app/main.cpp`): where the native style cannot render dark (Windows 10), the app installs the
  palette-driven **Fusion** style so the whole shell + dialogs render a consistent dark; Windows 11 keeps
  its native windows11 style unchanged. This resolves the light-shell / dark-dialog clash without touching
  the hardcoded stylesheets. The **theme-agnostic refactor** described below (drop the force, strip the
  hardcoded chrome, offer follow-OS / a Light·Dark·System toggle) remains the larger future effort and is
  left open. Trade-off accepted: on Win10 the native windowsvista look is replaced by Fusion-dark (the OS
  has no native dark to keep).

  **Cause:** the 1.3.0 fix calls `QStyleHints::setColorScheme(Qt::ColorScheme::Dark)` at startup. On
  Windows 11 (windows11 style) that forces the shell dark; on Windows 10 it does **not** take —
  Win10's platform theme does not honour the forced scheme. But the deeper cause is that the app was
  **designed dark by hardcoding**: every dialog `.ui` bakes in dark backgrounds and light text
  (≈83 `color:` + ≈63 `background-color` across 9 `.ui` files), which is *why* the shell had to be
  forced dark to match. When the force fails (Win10), the hardcoded-dark dialogs clash with the
  light native shell.

  **Chosen direction — stop fighting the platform theme; go theme-agnostic** (rather than shipping a
  custom skin/palette, which is rejected). Let Qt / the OS drive light vs. dark for all chrome, and
  hardcode only the handful of *semantic* colours that must be specific. This also folds in the
  deferred "flat/colorless on Linux vs Windows" item (MINOR): following the native theme everywhere
  is the consistency fix.

  1. **Remove the colour force.** Drop `setColorScheme(Dark)` and the global dark stylesheet in
     `app/main.cpp` (the `#2d2d2d / #1e1e1e / #e0e0e0` block). (Re-check whether the Qt 6.8 minimum is
     still needed once `setColorScheme` is gone.)
  2. **Strip hardcoded chrome from the `.ui` files** — the dark `background-color` / `color:` /
     `styleSheet` on backgrounds, panels, labels and plain controls across `mainwindow.ui`,
     `aboutdialog.ui`, `canvasprofiledialog.ui`, `imagetile.ui`, `licencedialog.ui`,
     `managecanvasprofilesdialog.ui`, `manageoutputprofilesdialog.ui`, `outputprofiledialog.ui`,
     `templatesdialog.ui`. Let them inherit the platform palette so they are light under a light OS
     and dark under a dark OS, consistently.
  3. **Strip the same from `.cpp`** — `canvasprofiledialog.cpp` (`#141414 / #888888 / #555555`),
     the progress-bar trough greys in `render.cpp` (`#2b2b2b / #555555 / #888888 / #dddddd` — keep
     only the error state), the accent `#7ac8f5` in `output.cpp` / the manage dialogs.
  4. **Keep the semantic colours, made legible on *both* themes.** For each retained colour, apply one
     of the two allowed strategies:
     - **theme-conditional** — pick the shade from the active `QStyleHints::colorScheme()`, and update
       on `colorSchemeChanged`; or
     - **dual-theme-safe** — one shade with adequate contrast on both a white and a dark background
       (e.g. darken over-light text; keep the status colours saturated enough to read on white *and*
       on dark grey).

     The set to keep: the **input/output tile status bar** colours (`imagetile.cpp:122-135` —
     green/cyan/orange/red/amber/violet/rose/grey; these are the state signal), and the **red
     error/halt** on the progress bar and the Render→Stop button (`render.cpp #b41414`,
     `output.cpp`). The invalid-field border (`#e06060`) is arguably semantic too — keep or map to the
     palette's error role.

  5. **Test under both OS themes** on Windows 10 and 11 (light and dark), plus Linux: no
     light-on-light or dark-on-dark, and the tile status colours must read on whatever background the
     theme paints. We have no Win10 CI, so this is a manual pass.

  Minimum bar: the app is **readable and internally consistent** under whatever theme the OS gives —
  no forced scheme, no light-shell/dark-dialog clash.

- [x] **Camera photos (EXIF-rotated) render wrong — lib-side fix.** Same Win10 test: three phone
  photos, the EXIF-90° one landed in its own slice with a black band instead of flowing into the
  continuous strip. Root cause was in libplatemaker (matching reads raw header dims; the scaler does
  not auto-rotate). **DONE — fixed in lib 0.5.0:** the black band via `ScaledStrip::buildSlice()`
  switching to `vips_join` (with a `built.height == sliceH` post-condition), and the orientation via
  `vips_autorot` on load in both pipelines plus `headerGeometry()` reporting display dimensions so
  matching agrees with the rendered pixels. The GUI's only action was pinning the lib: `CMakeLists.txt`
  now sets `LIBPLATEMAKER_VERSION "0.5.0"` (a hard floor for the render output contract). Verified
  against the `temp/win10/` photos — the EXIF-90° page now flows into the strip, upright, no band.

- [ ] **Camera photos (EXIF-rotated) wrong input thumbnail orientation**
EXIF-90° input has thumbanil rendered as EXIF-0°. See photo temp\wrong_input_thumbs.png. The third image should be vertical, but the thumbenail is horizontal.

- [x] **Duplicate output profile** requires to click edit on freshly duplicated profile and then save. Leaving Ouput profiles dialog without this step won't retain the duplicate. Proposition is that Duplicate button shall automatically open Output Profile edit dialog of the duplicated profile. **DONE:** the retention half was already fixed by the track-by-id / `WorkspaceEditor` refactor (a duplicate carries a fresh user id and is persisted by `replaceOutputProfiles`, which drops only preset-id entries) — this item predated that change. The proposition is now implemented: `onDuplicateClicked` seeds the copy (name + " (copy)", fresh id) and opens `OutputProfileDialog` on it immediately; **atomic** — cancelling the editor abandons the copy, only an accepted edit inserts it. 

- [x] **Harden against DLL injection / hijacking (Windows) — search-path half done in 1.4.1.** Prompted
  by finding third-party global hooks injected into our own process during the drag-and-drop debugging
  (LG OnScreen Control's `ScreenSplitterHook`; ASUS/ENE RGB software) — plus general defence-in-depth for
  an unsigned app. Two Windows-only mitigations were considered; **one shipped, one is deferred** (full
  rationale in `docs/SPECIFICATION.md` §9):
  - **DONE (1.4.1) — restrict the DLL search path** to the app dir + System32 (removes the current dir
    and `PATH` — the classic DLL-planting vector; relevant because we bundle a large DLL graph). Called as
    the first statement in `main()`, guarded by `#ifdef _WIN32`, resolved dynamically so it no-ops on
    pre-Win8. Verified: launches + full render clean under Qt Creator and the installed build.
    ```c
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    ```
  - **DEFERRED — block legacy extension points** (`ProcessExtensionPointDisablePolicy`). This is the half
    that would actually stop the hook injection we observed (plus `AppInit_DLLs` and legacy IME DLLs), but
    it also breaks **legacy IMM32 IMEs** and some **accessibility / assistive tools**. Broken IME would
    hurt non-Latin input (Korean / Japanese / Chinese authors — a core webtoon audience), and we have no
    CJK IME test rig to validate it (modern TSF IMEs are usually unaffected; legacy ones are not). The
    benign, low real-world risk of hook injection does not justify that regression. **Revisit once code
    signing lands or an IME test rig exists.** Deferred snippet kept for the record:
    ```c
    PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY p = {0};
    p.DisableExtensionPoints = 1;
    SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &p, sizeof(p));
    ```
  - Neither is a substitute for code signing (the real integrity / reputation fix); the shipped half is
    defence-in-depth against search-order hijacking and does **not** affect SmartScreen.

- [~] **Pre-flight sanitize off the UI thread — won't do (by design).** `project.sanitize()` hashes
  every input + output on the main thread before launching, which can briefly pause the UI on a very
  large project. Decision after review: **leave it on the UI thread.**
  - The pre-flight hash is small next to the render that immediately follows and re-reads every input
    anyway; the user has already committed to a wait by pressing Render.
  - `sanitize()` **mutates** the live, UI-thread-owned model (the render worker runs on *value copies*,
    not the live model), so off-threading it means sanitize-on-a-copy + merge-back — real complexity and
    race surface for a pause this small.
  - Reacting to *config* changes does **not** need this: `sanitize()` really does two things — a cheap
    in-memory config-staleness pass (`detectCanvasConfigChange` / `detectInputCompositionChange`) and the
    expensive disk hashing. Config edits don't change file bytes, so they never need the hash. The
    output-profile axis is already surfaced live and cheaply via `outputsConfigStale()` on every
    `populate()`; the canvas-profile / reorder axes are caught at the next Refresh/render (only a minor
    tile-freshness lag — never a wrong render, since pre-flight re-derives full-vs-partial).
  - **Deferred option** if that tile-freshness lag ever matters: extract the cheap in-memory axes out of
    `sanitize()` into a `refreshConfigStaleness()` and call *only that* on config-change signals — no
    hashing, no threads. Not worth doing now.

- [~] **`MainWindow::m_savedSnapshot` — keep as-is (by design).** Original question: store a sha256
  instead of the full serialized string, and is it even useful. After analysis: **keep the full string.**
  - It is the **authoritative** unsaved-changes baseline: `isWorkspaceModified()` re-serializes the
    workspace and compares against it, and `maybeSave()` uses that — deliberately independent of the
    eager `m_dirty` flag so a forgotten `setDirty()` can never silently drop changes.
  - **Not made redundant by undo/redo.** The `QUndoStack`s revert edits, but not every change goes
    through undo (a render updates hashes via `setDirty(true)` but is never recorded; add/remove project
    is not undoable), so `QUndoStack::isClean()` would miss real modifications. Serialize-and-compare
    catches every serialized change — strictly more robust.
  - **sha256 rejected:** `isWorkspaceModified()` already re-serializes the whole workspace each call
    (that is the cost); a hash would not cut it, only shrink the stored baseline from ~KB to 32 bytes
    (negligible), and would foreclose a cheap in-memory **"Revert to last saved"** the full string
    enables (deferred idea, not built).
  - Separate minor smell noted for later: the title-bar `*` (driven by `m_dirty`) can be a false
    positive after undoing back to the saved state; harmless because the save prompt uses the
    authoritative check.


- [ ] **ImageTile** rework to be more eye-appealing

- [x] **Grey out the Auto-sort rules group until it works** — the `groupBoxAutosort` fields
  (`lineEditInputNameRegex` / `Prepended` / `Appended`, `pushButtonAutosortApply`) were fully
  interactive but wired to nothing, inviting input that does nothing. Done: the group is now disabled
  and its fields show a "Coming soon" placeholder (`project.cpp`, constructor). A stopgap — remove it
  when the Auto-sort feature lands (see the MINOR item).

---

## MINOR — next: 1.5.0

New, backward-compatible features. Several are gated on a lib version, noted in the item body.
(The `[x]` items below shipped in 1.3.0 / 1.4.0; the open ones re-derive to the next MINOR.)

- [x] **Drag files / folders onto the Project window** — Done. Dropping images (or a folder) onto the
  Input-tab tile list adds them via the same path as *Add files* / *Add from directory*
  (`addInputPaths()` → `mergeFileScan()`), as one undo step. Implemented as an event filter on the
  list viewport (`project.cpp` `eventFilter`/`addDroppedUrls`), so external file drops are handled
  while the list's InternalMove reorder drag still works. A dropped folder is scanned like
  *Add from directory* (non-recursive, image extensions) and remembered as the project's
  `inputDirectory`. A new capability (not a change to an existing workflow), hence MINOR.


- [ ] **Auto-sort rules** (`groupBoxAutosort`) — pattern/regex-based ordering:
  `lineEditInputNameRegex` body token (e.g. `chap_<num>` → chap_001, chap_002…),
  `lineEditPrependedRegex` (e.g. `title_<num>` first), `lineEditAppendedRegex`
  (e.g. `end_<num>` last); `pushButtonAutosortApply` applies. Complex token/regex
  parsing — dedicated future task. When implemented, **re-enable the group and drop the
  "Coming soon" stopgap** in `project.cpp` (see the completed PATCH item).

- [ ] **Output size estimation / limits (UI)** — show estimated avg/max slice size
  and total batch size, and warn on platform caps (Webtoon ≤ 2 MB/slice,
  ≤ 25 MB/chapter). Estimate computed by the lib (mirrored in lib TODO); GUI
  displays before render and/or reports after.

- [ ] **Project-wide colour correction.** Comic/webtoon art is drawn on iPad in **Display P3** (wide
  gamut); most webtoon platforms and screens are **sRGB**, so even Procreate's "sRGB IEC61966-2.1" export
  doesn't fully fix how colours land (gamut mapping / a perceived shift). Artists want the *whole chapter*
  graded consistently, not tweaked page-by-page. Idea: a **project-level colour tool** that uniformly
  adjusts every input page of a project at render time (non-destructive — source files untouched).
  - Open design questions: proper **ICC colour management** (P3→sRGB via embedded/assumed profiles)
    versus a simple **user-driven curves/levels** control (per-channel RGB curves, saturation,
    brightness/contrast) versus both; how the settings are stored (a project setting, like the canvas /
    output profile) and previewed before a full render.
  - The pixel work belongs in the lib (libvips has `vips_icc_transform` plus curve/LUT ops) — mirror in
    the lib TODO.
  - **Scope idea:** apply project-wide but with per-page **exclusions** (e.g. everywhere except the first
    and last page), which touches several GUI components (input tiles, a settings panel, the render
    path).

- [ ] **OS-level crash handler for hard faults (segfault / SEH) — DEFERRED, likely not worth it yet.**
  Verdict from the cost/benefit analysis (`PlateMaker/temp/crash-handling-options.md`, §0): a minidump /
  Breakpad apparatus is a **bazooka** for a simple app with a small user base. It only pays off for
  crashes on **users' machines we cannot reproduce** — which we don't yet have evidence of; a crash we
  observe ourselves (see the segfault in the PATCH bucket) is reproducible, so the **Qt Creator debugger
  on the Debug build gives a full trace for free**. The symbol worry is also smaller than it seemed: we'd
  archive only **libplatemaker's** small `-g` debug info (never Qt's GB-scale symbols, never libvips'
  which don't exist), and that covers the boundary frame in *our* code — the only frame triage usually
  needs; Qt/vips show as `module+offset+version`, which is enough. **Revisit only if unreproducible field
  crashes appear**; then do the *minimal* form (Windows `SetUnhandledExceptionFilter` → text breadcrumb +
  optional `MiniDumpNormal`), not Breakpad. Full menu + pitfalls in the linked note.

- [ ] **Auto-save** on pipeline finish (optional setting)

- [ ] **App looks flat/colorless on Linux vs Windows** — no explicit style is set in `main.cpp`, so Qt
  falls back to native per-platform styling: Windows gets `windows11`/`windowsvista` (dark-mode aware,
  styled GroupBox borders, accent colors); Linux falls back to a much plainer default. (We deliberately
  keep the native windows11 look on Windows — see the light-mode fix — rather than force Fusion, so this
  cross-platform-consistency item is still open.) If uniformity matters, consider
  `QApplication::setStyle("Fusion")` plus a shared custom `QPalette`/QSS, accepting that it trades the
  native windows11 look for consistency.

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


## Add dependency manifest — done (SBOM submission)

- [x] **Repository defines its dependencies for GitHub's Dependency graph.** GitHub can't read our CMake
  dependencies (find_package for Qt/libplatemaker, the prebuilt libvips zip, FetchContent), and it does
  **not** ingest an SBOM merely committed to the repo (the *Export SBOM* button only exports). So instead
  of a fake `package.json`, we feed the graph through the **Dependency Submission API**: a committed SPDX
  snapshot at [`sbom/sbom.spdx.json`](../sbom/sbom.spdx.json) (the superset: Qt, libplatemaker, libvips,
  nlohmann/json) is submitted by
  [`.github/workflows/dependency-submission.yml`](../.github/workflows/dependency-submission.yml) on every
  push touching `sbom/`. Regenerate the snapshot from the build's `credits/sbom.spdx.json` when a version
  changes (see `sbom/README.md`). CVE alerts additionally need *Dependabot alerts* enabled in
  Settings → Code security; [`.github/dependabot.yml`](../.github/dependabot.yml) separately keeps the
  GitHub Actions current (Dependabot version-updates has no C++ ecosystem for Qt/libvips).

---

## Distribution & installer trust (no paid signing) — no release impact

Windows SmartScreen warns on our installer because it is **unsigned**. Only a paid Authenticode
certificate removes that warning — OV ≈ 200-400 USD/yr (still needs SmartScreen reputation to build up),
EV ≈ 300-700 USD/yr (instant reputation), or Azure Trusted Signing ≈ 10 USD/mo (cheapest legit,
needs a 3-yr-old org or individual verification). A self-signed cert does **not** help — Windows
doesn't trust it. **Decision: don't pay.** The items below instead give users a *verifiable* integrity
and provenance trail; they do **not** remove the SmartScreen warning (set that expectation in docs).

- [ ] **Submit to winget (`winget-pkgs`)** — free community channel giving users a trusted
  `winget install Platemaker` path; the manifest validates the installer's SHA-256. Cleaner than a raw
  `.exe` download (doesn't remove SmartScreen on direct download, but the winget flow is smoother).
  Consider a Scoop bucket too for the dev audience.

- [x] **VirusTotal report** — `Platemaker-1.3.0-Setup.exe` scanned **0 / 68 clean**
  ([report](https://www.virustotal.com/gui/file/6d1b95c6dc68d94c9d7a8b4ea7a7c41f2135538d3ea4ab1bade091551cae7602)),
  linked from the README. Per-build (tied to the file hash), so rescan each release; could later be
  automated in the release CI via the VirusTotal API.



---
