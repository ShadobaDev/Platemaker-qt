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

Baseline: **1.4.2 released (2026-08-22), 1.4.3 in progress** (`CMakeLists.txt`). 1.4.3 is a patch — the
Windows toolchain moved MinGW → MSVC (to dodge the Defender/MinGW false positive), the installer was
slimmed (app-local VC runtime, unused Qt plugins dropped), Qt bumped to 6.11.2, and the lib pin to
**0.5.1**. 1.4.0 (2026-08-07)
added the *New from this…* project action, render-log persistence, the render summary and the restyled
progress bar. 1.4.1 is a patch — the DLL search-path hardening, the drag-and-drop fix, `Platemaker.exe`
version metadata, and a bump to **libplatemaker 0.4.1** (metadata-only) so the bundled DLL also carries
identity metadata. The next patch bucket is **1.4.3**; the next feature bucket re-derives to **1.5.0**.

---

## PATCH — next: 1.4.4

Bug fixes, cosmetics and internal cleanups — no new capability, no change to an existing workflow.

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
(The `[x]` items below shipped/ready to ship; the open ones re-derive to the next MINOR.)

- [ ] **Infinite strip and lookup system**
  The problem is that during work user may want to see how does the full strip look like
  Two ideas:
  1. Built-in webtoon-like viewer. Problem are margins and general rendereing - the files will have to be either rendered on fly or stored in temporary location with option to save
  2. Render infinite long strip either via designated output profile or separate button/menu option [Process]

  Both options could be implemented. 

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
  - **Scope idea:** apply project-wide but with optional per-page **exclusions** (e.g. everywhere except the first
    and last page), which touches several GUI components (input tiles, a settings panel, the render
    path).
  - GUI has to visualize color correction in Infinite strip and lookup system.
    Two ideas:
    1. Persistenly modify input files - goes against rule not to modify raw user input.
    2. Add do lib an additional step during render to overlay color correction. 
  
- [ ] **Text and Text bubble creator**
  1. Persistenly modify input files - goes against rule not to modify raw user input.
  2. Add to lib an additional step to overlay text and text bubbles during render.

- [x] **Improve docking**
  It should be possible to dock multiple dock-views next to each other vertically as well as horizontally.
  Action dock shall have static default width - that can be only changed by moving splitter. 

- [x] **Import / export input and output profiles** — *DONE (lib + GUI); requires libplatemaker 0.5.2.*
  Carry canvas/output profiles between workspaces. Lib side: a portable **profile bundle**
  (`.platemaker.profiles.json`) via `Infrastructure::ProfileBundleSerializer` + `WorkspaceEditor::importProfiles`
  (fresh ids, template cleared, presets skipped — additive, so a workspace stays self-contained). GUI side:
  - **Import / Export** live as native **submenus** under *Canvas Profiles* and *Output* (populated on
    `aboutToShow`). Import **sources**: another `.platemaker.json` workspace, a `.platemaker.profiles.json`
    bundle, the GUI-managed **user library** (a bundle at a fixed `AppData` path,
    `user.platemaker.profiles.json`), plus **recent workspaces** and **recent bundles**.
  - New reusable `widgets/profilepickerdialog/` — a type-agnostic cherry-pick list with a grouped
    read-only **inspection panel** (mirrors the editor: canvas size / margins / colour swatches) and
    coloured **badges** (`margins`, `already in library`, painted by a custom item delegate).
  - Export writes a bundle **file** or **adds to the library** (upsert-by-name — no duplicates); the picker
    marks profiles already in the library and confirms before overwriting.
  - Import commits through `WorkspaceEditor::importProfiles` (undoable), so the workspace stays
    self-contained. The library is **only ever an import source / export target** — no per-profile
    "global" flag; it never mutates a workspace on its own.
  - *Deferred (not built):* a "read-only inspect in the editor dialog" (the inline panel covers it) and a
    dedicated "Manage Profile Library" curation dialog (the library is populated by Export, consumed by
    Import; per-entry edit/delete can come later).

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

- [x] **Slim the MSVC installer — drop the bundled `vc_redist.x64.exe` (≈25 MB), ship the VC runtime
  app-local.** The MSVC installer is ~52 MB vs MinGW's ~27 MB, and a file-list diff of the two (built
  from the same commit on `ci/compare-mingw-msvc`) pins the whole gap on **one file**: windeployqt bundles
  the full **Visual C++ Redistributable installer** `vc_redist.x64.exe` (via its default `--compiler-runtime`),
  plus `dxcompiler.dll` + `dxil.dll`. MinGW instead ships three small runtime DLLs (`libgcc_s_seh-1`,
  `libstdc++-6`, `libwinpthread-1`, ≈3 MB total).

  **This is very likely also why Microsoft's `Wacatac.B!ml` came back on the MSVC *installer* while the
  MSVC lib/exe are clean** (VT, 1.4.3, same commit): MinGW installer 2/70 — DeepInstinct + SecureAge, **no
  Microsoft**; MSVC installer 2/68 — DeepInstinct + **Microsoft `Trojan:Win32/Wacatac.B!ml`**; standalone
  MSVC `platemaker.dll` **0/71 clean**. So the app binaries are fine on MSVC — the FP is installer-level and
  tracks the embedded `vc_redist.x64.exe`: an installer that carries *another* full installer-exe is a
  dropper-like shape ML models weight. Dropping it should slim the installer to ≈30 MB **and** remove that
  ML trigger.

  **How:** stop windeployqt bundling the redist and ship the runtime DLLs next to `Platemaker.exe` instead
  (Microsoft-supported app-local VC redist):
  - Pass `--no-compiler-runtime` to windeployqt (via `qt_generate_deploy_app_script`'s `DEPLOY_TOOL_OPTIONS`,
    Qt 6.7+ — verify against the Qt 6.11 docs), and optionally `--no-opengl-sw` is *not* wanted (both
    toolchains ship it and it is harmless), but do consider excluding `dxcompiler.dll`/`dxil.dll` — the
    DirectXShaderCompiler is for Qt Quick / RHI-D3D, and Platemaker is Qt Widgets.
  - `install(FILES …)` the three VC runtime DLLs (`vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`)
    from the VS redist dir (`$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT/`) into `bin/`, guarded by
    `if(MSVC)` — the mirror of the existing `if(MINGW)` MSYS2-runtime block.
  - **Verify** by re-scanning the slimmed MSVC installer on VirusTotal: size ≈ MinGW's, and (the real test)
    whether the Microsoft Wacatac verdict clears.

  Caveat: every verdict here is an ML/heuristic FP on an **unsigned** installer — DeepInstinct flags *both*
  toolchains regardless. Slimming removes one strong ML trigger, it is not a guarantee; the only complete
  fix for installer-level FPs remains **code signing** (see the "don't pay" decision above).

- [x] **Portable ZIP distribution** — a no-install `Platemaker-<ver>-portable.zip` alongside the
  installer: single top folder with `Platemaker.exe` at its root (flat layout, `qt.conf` → `Prefix = .`),
  built by a new `portable` CMake target that repackages the existing `install/` staging (no second
  deploy). *Done:* [`cmake/make_portable.cmake`](../cmake/make_portable.cmake) + `portable` target,
  [`scripts/make_portable.ps1`](../scripts/make_portable.ps1), shipped + attested in
  [`release.yml`](../.github/workflows/release.yml).

- [ ] **Free code signing via SignPath.io OSS** — reconsider the "don't pay" stance: SignPath's Open
  Source program grants a **free real Authenticode cert** (OV-class), which is the one option that actually
  clears the Defender `Wacatac!ml` FP. Sigstore/GPG don't (Windows ignores them for `.exe`). Full
  evaluation + next research step in [`temp/code-signing-options.md`](../temp/code-signing-options.md).

- [ ] **Submit to winget (`winget-pkgs`)** — free community channel giving users a trusted
  `winget install Platemaker` path; the manifest validates the installer's SHA-256. Cleaner than a raw
  `.exe` download (doesn't remove SmartScreen on direct download, but the winget flow is smoother).
  Consider a Scoop bucket too for the dev audience.

- [x] **VirusTotal report** — `Platemaker-1.3.0-Setup.exe` scanned **0 / 68 clean**
  ([report](https://www.virustotal.com/gui/file/6d1b95c6dc68d94c9d7a8b4ea7a7c41f2135538d3ea4ab1bade091551cae7602)),
  linked from the README. Per-build (tied to the file hash), so rescan each release; could later be
  automated in the release CI via the VirusTotal API.



---
