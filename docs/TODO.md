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

Baseline: **1.3.0 released, 1.4.0 in progress** (`CMakeLists.txt`). 1.3.0 added a `Ctrl+R` alternate
for the render shortcut, app-wide undo/redo, and drag-and-drop input adding (requires libplatemaker
0.3.1). **1.3.1 was never released** — its work is folded into 1.4.0, which a new feature (*New from
this…*) turns into a MINOR. Per the cascade, 1.4.0 (MINOR) takes the next slot, so the pending patch
bucket re-derives to 1.4.1.

---

## PATCH — next: 1.4.1

Bug fixes, cosmetics and internal cleanups — no new capability, no change to an existing workflow.

- [x] **Window icon is loaded from a relative path** — `main.cpp` calls
  `setWindowIcon(QIcon("icons/icon-red.ico"))`, resolved against the *working directory*, so
  it silently yields a null icon whenever the app is not started from the install folder.
  The title-bar icon still appears because Windows takes that from the executable's own
  resource (`app.rc`), which hides the failure — it surfaced only as an empty band above the
  About text, where the icon was supposed to be.

  Fix: load it from the Qt resource system (`:/icons/…`), which the app already has via
  `app/resources.qrc` — same mechanism as the menu icons, and independent of the working
  directory.

- [x] **Duplicate project** option in context menu in workspace. This will cover multi-publisher situation. For example when user wants to have different projects per publisher of the same chapter. The use-case is that one project is fully done, and the dupliacted ones will only have Output profile changed.

  Done (shipped in **1.4.0**, so a MINOR not a patch): implemented as **"New from this…"** — a *naive*
  seed, not a render clone. It copies the source's input files + profile links (canvas + output) but
  drops the output directory, the output list and all render state, so the copy starts fresh (inputs
  *Pending*) and renders into its own folder. Backed by `WorkspaceEditor::duplicateProject()` in
  libplatemaker 0.4.0, which mints the fresh workspace-unique project uid.

- [x] **Persist last render log.** The GUI render log is in-memory only (cleared on exit). Optionally persist the last run's log (and the slice/skip summary) next to the workspace so a user can review what the previous render did.

  Done (1.4.0): each run's log is auto-saved to `<workspace>/.platemaker-cache/logs/render-<timestamp>.log`
  at render/batch end (`persistRenderLog()`), keeping the newest 10 (`k_maxRenderLogs`) so a failing
  run's log isn't overwritten by the next. Plus a right-click menu on the log — *Save log as…* and
  *Clear log* — instead of a toolbar button.

- [x] **Stale comment: templates no longer draw slice guides** — comments cleaned up (the
  `until-then` option). `templatesdialog.cpp`, the lib `template_generator.{hpp,cpp}` docstrings and
  the CLI `template` help/comment no longer describe the border + slice-guide lines as if they run:
  they are documented as **compiled out behind `GUIDELINES_ENABLED`** (renamed from the misspelled
  `GUIDLINES_ENABLED`, now `#undef`-ed after use), and `outputProfile` is documented as reserved/unused
  while guides are off. The parameter is **kept** on purpose (dropping it is a breaking lib API change
  and would remove the easy path to re-enable the guides). Still open, later: *drop the parameter* or
  *re-enable the guides* — a deliberate feature/API decision, not a cleanup.

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

- [x] **Process bar** change style - a solid 15px bar - light broder - empty part background color, filled part grey, error or halt - red. Done (1.4.0): full `QProgressBar` QSS in `setProgressValue()` (15px min/max height, `#555` border, `#2b2b2b` trough, `#888` chunk / `#b41414` on error), applied at construction for the idle look.

- [ ] **ImageTile** rework to be more eye-appealing

- [x] **Grey out the Auto-sort rules group until it works** — the `groupBoxAutosort` fields
  (`lineEditInputNameRegex` / `Prepended` / `Appended`, `pushButtonAutosortApply`) were fully
  interactive but wired to nothing, inviting input that does nothing. Done: the group is now disabled
  and its fields show a "Coming soon" placeholder (`project.cpp`, constructor). A stopgap — remove it
  when the Auto-sort feature lands (see the MINOR item).

- [x] **Light mode** was broken (hardcoded dark greys mixed with an OS-light background → light-grey
  font on light background, unreadable). Resolved by **forcing the dark colour scheme**: `main.cpp`
  calls `QStyleHints::setColorScheme(Qt::ColorScheme::Dark)` at startup, so the native style renders
  dark regardless of the OS setting while keeping the platform look (windows11 on Windows). This bumped
  the Qt minimum to **6.8** (where `setColorScheme` was added). Shipped in 1.3.0.

---

## MINOR — next: 1.4.0

New, backward-compatible features. Several are gated on a lib version, noted in the item body.
(The `[x]` items below shipped in 1.3.0; the open ones re-derive to the next MINOR.)

- [x] **Drag files / folders onto the Project window** — Done. Dropping images (or a folder) onto the
  Input-tab tile list adds them via the same path as *Add files* / *Add from directory*
  (`addInputPaths()` → `mergeFileScan()`), as one undo step. Implemented as an event filter on the
  list viewport (`project.cpp` `eventFilter`/`addDroppedUrls`), so external file drops are handled
  while the list's InternalMove reorder drag still works. A dropped folder is scanned like
  *Add from directory* (non-recursive, image extensions) and remembered as the project's
  `inputDirectory`. A new capability (not a change to an existing workflow), hence MINOR.

- [x] **Undo / Redo** (`Ctrl+Z` / `Ctrl+Y`) — Done, app-wide via the Workspace-menu Undo/Redo actions
  (`actionUndo`/`actionRedo`, wired to the group in `setupUndo`). A `QUndoGroup` with
  one stack per open project + one workspace stack; `Ctrl+Z`/`Ctrl+Y` route to the front tab. Snapshot
  commands built on the lib's `ProjectEditor::snapshot/restore` and `WorkspaceEditor::snapshotMeta/
  restoreMeta` (component-scoped, so light in RAM; depth 10). Covers input edits, canvas links, output
  profile/dir, and workspace-level profile CRUD / project rename / templates. Add/remove project is
  deliberately **not** undoable; render/refresh are never recorded. Requires libplatemaker 0.3.1.
  (Out of scope: undoable project add/remove; restoring links a profile-delete cascaded away.)

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

- [x] **`std::set_terminate` in `main.cpp`** — Done (1.3.1). Logs the in-flight C++ exception via
  `qCritical` on the `terminate` paths (uncaught exception / `noexcept` violation / pure-virtual call)
  instead of a silent abort. Does **not** catch a segfault (that's not a C++ exception). The lib CLI got
  the same via `std::set_terminate` in `runCli`.

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

- [x] **Action log** should report a summary, how manu inputs, how many slices in what time where processed and when. Output cumulative size (MB or KB) would also be nice. Done (1.4.0): a successful render appends `Output: N slice(s), <size> — from M input(s) in <time>` (size via `QLocale::formattedDataSize`, time via a new per-render `QElapsedTimer`); a batch adds each project's line plus the whole-sweep time on the *Batch finished* line. Captured in the persisted render log too. ("when" is the log file's own timestamp.)

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

- [x] **Recent-workspaces behaviour — verified.** Confirmed in code (`mainwindow/workspace.cpp`):
  the list is capped at `k_maxRecentWorkspaces = 10` (oldest dropped) and de-duplicated
  case-insensitively; opening a remembered workspace that has been moved/deleted shows a warning
  (`QMessageBox::warning`, "The workspace no longer exists…") and removes it from the list rather than
  erroring. Behaviour is intentional; wiki note ("to be tested") can be updated to describe this.

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

- [x] **Generate SHA-256 checksums with the installer** — the `installer` target now writes
  `installer-output/Platemaker-<version>-SHA256SUMS.txt` (via `cmake/make_checksums.cmake`, `sha256sum`
  format) right after Inno Setup runs, so every local build produces the checksum. The name is
  versioned so releases don't overwrite each other in `installer-output/`. **Still manual:** uploading
  that file as a GitHub Release asset (will be automated by the release-CI item below). Proves the
  download wasn't tampered with, even though it stays unsigned.

- [x] **GitHub Actions release CI** — green: [`.github/workflows/release.yml`](../.github/workflows/release.yml).
  On a bare version tag it installs **Qt 6.11.1 (MinGW)**, builds `--target installer` (which also emits the
  SHA256SUMS), uploads the installer as a run artifact, attests **build provenance**
  (`actions/attest-build-provenance` — verify with `gh attestation verify <installer> --repo
  ShadobaDev/Platemaker-qt`), and on a tag publishes the Release + runs a best-effort VirusTotal scan.
  Validated via `workflow_dispatch` (provenance produced for `Platemaker-1.3.1-Setup.exe`). Notes: Qt is
  installed by driving **aqtinstall from git master** (its latest release 3.3.0 can't read Qt 6.11's new
  repo layout — issue #1007; revert to `install-qt-action` once a fixed aqt ships); a `VT_API_KEY` secret
  must be added to this repo to enable the VirusTotal step. **Remaining:** cut a real tag `1.3.1` to
  exercise the publish path end-to-end. See `../PlateMaker/temp/CI-release-github-actions.md`.

- [ ] **Submit to winget (`winget-pkgs`)** — free community channel giving users a trusted
  `winget install Platemaker` path; the manifest validates the installer's SHA-256. Cleaner than a raw
  `.exe` download (doesn't remove SmartScreen on direct download, but the winget flow is smoother).
  Consider a Scoop bucket too for the dev audience.

- [x] **VirusTotal report** — `Platemaker-1.3.0-Setup.exe` scanned **0 / 68 clean**
  ([report](https://www.virustotal.com/gui/file/6d1b95c6dc68d94c9d7a8b4ea7a7c41f2135538d3ea4ab1bade091551cae7602)),
  linked from the README. Per-build (tied to the file hash), so rescan each release; could later be
  automated in the release CI via the VirusTotal API.

- [x] **README: explain the SmartScreen warning** — README now has an *Installing & verifying your
  download* section: why the "unknown publisher" warning appears, how to proceed (Properties →
  *Unblock*, or *More info → Run anyway*), plus the current build's SHA-256 and the VirusTotal link.
  Now also documents **build-provenance verification** (the CI produces attestations): a browser path
  (the repo's Attestations page — GitHub confirming the file was built here from a specific commit, for
  non-technical users) and the `gh attestation verify … --repo ShadobaDev/Platemaker-qt` command.

---
