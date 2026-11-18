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


- [x] **Strip editor: *Convert to ▸ Balloon* clips the lettering.** Convert now fits the box to the
  lettering in its new kind, both ways, and moves the object so the words stay where they were — the
  balloon appears under them (`ObjectController::settleBox()`).

- [ ] **Strip editor: *Fit to strip width* leaves the picture hanging off the strip.** It calls
  `scaleSelectedPicture()`, which writes only `wFrac`, so the top-left corner stays where it was. Fix:
  the action also sets `xFrac = 0` in the same history step — a small controller method of its own,
  because `scaleSelectedPicture()` is also the scale spin box's live path and must keep the position.

- [x] **Strip editor: artwork's corner grips are not the same size on screen.** Fixed by moving the
  selection chrome out of the objects: the box, the grips and the tail handles are now one item above
  every object (`objects/selectionchrome.*`), drawn in scene units at a fixed screen size, so an item's
  own scale no longer reaches them. The same move keeps a selected object's grips and tail handles
  visible and grabbable under objects stacked above it.

- [x] **Strip editor: a single selected tail offers the whole balloon's menu entries.** `selectTail()`
  stores the balloon's uid itself as selected, so `anyObject` is true and *Fill*, *Outline*, *Blend* and
  *Apply from tool options ▸* act on the whole balloon. The comment in `ObjectMenu::updateEntries()` says
  "a tail can only be deleted". Decide which is meant; if the comment, `anyObject` must not count a
  single tail's balloon.

- [x] **Each workflow card's *Edit* arms its tool.** The Colour correction and Text & bubbles cards share
  one `openEditor` (`widgets/project/project.cpp`) and `viewStripRequested` carries only the project
  index, so *Edit* on the CC card opens the strip editor on whatever tool was armed last. Fix:
  `viewStripRequested(projectIndex, toolId)`; MainWindow calls `editor->setTool(toolId)` after opening
  the dock. The CC card passes `"grade"` and selects the strip; the bubbles card passes `"bubble"`.

- [x] **Strip editor: after *Convert to curve*, some nodes have no Bézier handles.** A straight side
  converts to nodes with no handle on that side, and a picked node offered only the handles it had.
  Fixed: a picked node offers the ones it lacks, a third of the way to its neighbour, and *Add handles*
  in its menu grows them on a straight side. A right-click on a node now picks it, so that menu (and
  *Delete point*) is the node's, not the balloon's.

- [x] **Strip editor: TOOL OPTIONS draws *Corner radius* and *Edge* over the first shape tile.**
  `ShapeEditor` puts its parameter rows on a widget of their own, `parameters()`, for the panel to lay
  out. OBJECT STATE does (`objectstate.cpp`); `BubbleToolOptions` does not, so the widget sits unlaid
  at the editor's top-left corner, on the tiles. Fixed: TOOL OPTIONS lays it out under the tiles, and
  the Speech bubble's row map leaves it out.

- [x] **Strip editor: TOOL OPTIONS shows too much.** The Bubble tool's options repeated nearly the whole
  OBJECT STATE panel. Done: each tool names the rows its options show (`Tool::optionRows`), the preset
  picked is what the next object is made from, and presets are saved from objects and managed in their
  own dialog.

- [ ] **ImageTile** rework to be more eye-appealing

- [x] **Grey out the Auto-sort rules group until it works** — the `groupBoxAutosort` fields
  (`lineEditInputNameRegex` / `Prepended` / `Appended`, `pushButtonAutosortApply`) were fully
  interactive but wired to nothing, inviting input that does nothing. Done: the group is now disabled
  and hidden, and its fields show a "Coming soon" placeholder (`project.cpp`, constructor; drop the
  `setVisible(false)` line to preview it greyed out). A stopgap — remove it
  when the Auto-sort feature lands (see the MINOR item).

---

## MINOR — next: 1.5.0

New, backward-compatible features. Several are gated on a lib version, noted in the item body.
(The `[x]` items below shipped/ready to ship; the open ones re-derive to the next MINOR.)

- [x] **Show the effect of a canvas profile edit right away.** *Done 2026-11-09 (requires
  libplatemaker 0.7.0).* "Out of sync" is a query (`detectFileStaleness()`), not a stored status, so
  every open project's tiles repaint on `workspaceChanged`. Saving or deleting a canvas profile asks
  first, listing every project it reaches. The profile managers commit each action as its own undo step.

- [x] **Reopen a workspace the way it was left.** *Done 2026-11-09.* The open project docks and strip
  editors and `saveState()` are kept per workspace file in QSettings (a per-workspace key, so the
  app-wide objection no longer holds), for the recent workspaces only; the main window's geometry too.

- [~] **Infinite strip and lookup system** — *viewer DONE (GUI); lookup + preview-render deferred.*
  See the full strip as one continuous image during work, instead of per-slice tiles. Shipped: a
  per-project **floating dock** (`widgets/stripeditor/`, `.ui` + `.cpp`/`.hpp`, opened from the Output tab's
  *View strip*), a window onto the **lib-rendered output slices** reassembled — WYSIWYG, the viewer never
  re-derives pixels. It carries a **custom title bar** (native min/max on a dock misbehave; a floating
  dock otherwise shows only close) whose buttons **dock it tabbed beside Workspace** (minimise), **fill
  the screen** (maximise ⇄ restore) or close it, while the bar still drives Qt's drag-to-dock so the dock
  stays dockable. The shared dock tab bar's close/double-click resolve the dock by title
  (`dockForTabBarTab`), so a tabified strip closes/floats correctly. `QGraphicsView`/`Scene` with **one** item drawing every slice (avoids the 1px inter-item seam),
  and **antialiasing off on the strip draw** so adjacent slices tile without edge-coverage bleed (the
  root cause of the hairline "frames" between pages). Memory-bounded (proxy + async decode + prefetch):
  header-only layout (`QImageReader::size`), a blurry **proxy** from the render-warmed `ThumbnailCache`
  (instant, no scroll gaps), and a **sharp** native decode of visible + prefetch slices on `QtConcurrent`
  into an LRU cache — off-screen slices evicted, so RAM tracks the viewport, not chapter length.
  Native-width default (shrink-to-fit only, never enlarged), fit-width / 100% / Ctrl+wheel zoom, optional
  slice-seam guides. See the wiki's [Strip editor shell](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Strip-Editor-Shell).
  - **Feed changed (2026-09-04): the strip is built from the project's INPUT pages, not its committed
    output.** The original choice (committed output + a *Render & view* button, outputs being cheap and
    regenerable) did not survive contact with the authoring work it was meant to host: there is nothing
    to look at before the first render, a grade previewed on output is applied on top of the one the
    render already baked in, and an output slice can straddle two pages so neither a per-page exclusion
    nor a page-anchored bubble can be honoured at display time. It now feeds through the library's
    page-domain API (`layoutPagesFromHeaders` / `decodePageToRgba`, lib 0.6.0), so the strip exists before
    any render and rendering does not change what it shows. The seam guides moved with it: they now mark
    where the output *will be* cut, every `sliceHeight` down the strip.
  - *Deferred (not built):* the **lookup** half — click a strip position → which input page / output
    slice (the per-slice Y offsets + `OutputFile::sourceMap` foundation is already in place); a
    **preview-render-to-temp** feed to preview *uncommitted* edits (belongs with colour / text below);
    **display-resolution decode** (`QImageReader::setScaledSize`) so RAM also shrinks when zoomed out;
    and a dedicated side tool-panel + async-placeholder polish for very tall chapters.

- [x] **Project-wide colour correction — DONE (lib 0.6.0 + GUI).** Shipped: `Models::ColourCorrection`
  on the project (tone curves, brightness / contrast / saturation, per-page exclusions by input uid),
  applied per input page **before scale** by `Core::ColourCorrector`, folded into staleness via
  `processingConfigSignature()`. The GUI's Grade tool (`widgets/stripeditor/panels/gradepanel.*`) edits it live against the
  strip using `ColourCorrector::applyToRgba()` — the same engine the render uses, so the preview is not
  an approximation — and writes through `commitEdit` for undo.
  - *The ICC half was investigated and dropped.* An `iccToSRGB` toggle shipped first, then measurement
    killed it: the pages carry **no embedded ICC profile** (Procreate exports none), so the transform is
    a pixel-exact no-op — `vips icc_transform … srgb --embedded` then `vips subtract` gives
    `max abs difference: 0,000000` — and with the grade off the margin path normalises on load anyway.
    The checkbox governed a corner of a decision it did not own, so it was removed rather than kept as
    reassurance. Colour space is settled at *export* (Procreate), not here.
  - *Still open:* the **curve editor** (the model and the render already do curves; nothing draws them),
    the **per-page exclusion UI** (the model and render honour `excludedInputUids`; the artifact list is
    the intended home), and preselecting the Grade tool when entering from the Workflow map.

- ~~**Project-wide colour correction.**~~ — *superseded by the entry above; kept for the reasoning.* Comic/webtoon art is drawn on iPad in **Display P3** (wide
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
  
- [x] **Text and Text bubble creator — DONE (lib 0.6.0 + GUI).** Option 2 was taken: the library gained
  a strip-domain step that composites consumer-rasterised RGBA bitmaps onto each output slice
  (`Core::StripOverlayCompositor`), so raw input files are never touched. Storage, the rasterising
  contract and the anchoring rule are documented in the wiki's **[Strip editor objects](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Strip-Editor-Objects)** and
  **[Workspace folder ownership](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Workspace-Ownership)**.
  - **Placement is page-anchored** (`StripOverlay::anchorInputUid` + an offset from that page's top,
    resolved by `Models::resolveOverlayAnchors()`), not an absolute strip-Y. An absolute placement drifts
    onto different artwork the moment anything above it changes height — inserting a page is the everyday
    case — and drifts silently. Pinned by `test_overlay_anchoring.cpp` and `test_overlays.py`, each with a
    deliberate absolute-placement control.
  - **A bubble is a GUI object, a bitmap is what the lib sees.** `Artifact` (shape / box / tail /
    text / font / colours) is the overlay's content, saved in the workspace with it; the SVG in `overlays/`
    is derived from it, and is what gets composited. That is what keeps a bubble re-editable instead of
    flattened.
  - *Still open:* hand-drawn custom shapes, rich text, per-artifact blend modes in the UI (the model and
    render already carry them), re-rasterising every bubble when the output target width changes, and
    **SVG as the stored form** — see the note under "To establish / test".

- ~~**Text and Text bubble creator**~~ — *superseded by the entry above; kept for the reasoning.*
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
  Verdict from the cost/benefit analysis: a minidump /
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

- [ ] **Adopt an imported drawing as a balloon** — a hand-drawn silhouette (someone's own SVG) becomes a
  real parametric object: recolourable, styleable, and able to grow tails. Today an import is artwork
  because it carries no *recipe* — the `<pm:content>` a Platemaker-written SVG has — and a foreign
  file's paths cannot be guessed into `shape.kind`. What is already in place is most of it: tails aim at
  an arbitrary outline (`tailPath()` takes a `QPainterPath` and ray-casts, so it does not care where the
  outline came from), the style filter and fill/stroke work on any path, and the lettering is drawn over
  whatever is underneath. What is missing is **a place in the composition for a custom silhouette**
  (`ShapeProperties` holds an enum; it would need an "imported path" variant) and **a dialog where the
  artist marks which path is the silhouette and where the tails attach** — i.e. writes the recipe for
  somebody else's drawing. The honest limit: one closed silhouette, not a drawing of two hundred paths,
  or "fill" and "outline" stop meaning anything.

- [ ] **The Artwork tool's label.** The code calls an imported picture a *picture* (`PictureObject`,
  `PictureToolOptions`); the rail, its menu entries (*Import artwork…*) and its undo steps still say *Artwork*.
  Whether the artist's word should be *Picture* too is a question for the interface, not the code: a picture
  may be a sprite, a sound effect, a logo or a screen grab, which *artwork* only sometimes describes.

- [x] **A workspace owns its folder — and cleans up the files nobody uses.** *Done 2026-09-26.* Nothing ever deletes from
  `overlays/` (measured 2026-09-24: **46 files, 2 referenced**), and nothing guarantees that the
  folder belongs to one workspace, or that only one process writes to it. Two `*.platemaker.json` in
  one folder share `overlays/` and `templates/`, and a second instance opening the same workspace
  could sweep the first one's unsaved files. Cubase and Ableton both hit exactly this, and both
  answer with *one project per folder*. So the guarantees come first and the cleanup last:
  1. ~~**One workspace per folder.**~~ **Done (2026-09-25).** New and Save As refuse a folder that
     holds another workspace and offer a subfolder. A folder that already holds two does not open until
     a wizard resolves it (the ones not kept go to the Recycle Bin).
  2. ~~**A workspace lock.**~~ **Done (2026-09-25).** A stale lock on this machine goes by itself, a
     live one here is reported, and one from another machine (synced drive) can be **taken over**; the
     instance that lost it refuses its next write and says why. Two files, because a held `QLockFile`
     cannot be written or deleted by anyone else (measured, also on `G:`): the process lock in local
     application data, and a plain `.platemaker.lock` marker in the folder that a synced drive can carry.
  3. ~~**Relative paths** *(lib)*.~~ **Done (2026-09-26, lib 0.6.0).** An overlay path inside the folder
     is also stored relative (`assetPathRelative`) and preferred on load, so the folder can be moved or
     zipped; the absolute path is kept, so older builds still read the file. A rootless path is read
     against the folder for overlays, inputs and source maps; `outputDirectory` stays as written (a CLI
     `--output` is relative to the working directory).
  4. ~~**Save As collects**~~ **Done (2026-09-26).** Overlays, the pictures behind lettered pictures,
     and templates are copied into the new folder, all or nothing; the same collect runs on undo so a
     history never points back into the old folder. This also fixed Save As breaking templates.
  5. ~~**Sweep at open, to the Recycle Bin**~~ **Done (2026-09-26).** Only with one workspace in the
     folder and the lock held, and only files with our own names (`ovl-*`, `art-*`, `templates/*`).
     Reported in the Action log and the status bar, with *Show Recycle Bin* in the log's menu — not as
     an advisory, which the registry reserves for standing conditions. Measured: on the Google Drive `G:`
     the files land in the ordinary Windows Recycle Bin. At open, not at close: after *Discard*, memory
     is not what is on disk. *Delete template* stopped deleting the file, which makes it undoable again.

- [x] **Bug: a missing font silently re-letters bubbles.** *Done 2026-09-27.* A bubble's text is baked into its SVG as
  outlines through `QFont`. When the family is not installed, every re-emit bakes it in a fallback,
  including **every overlay undo**, because `rewriteOverlayAssets()` rewrites *every* bubble, not
  just the ones the step touched. Fix: rewrite only what a step changed; keep the intended family in
  the composition (already true); mark a fallback bake with `pm:fontFallback`; **heal at open** when the
  font is back (re-bake, and mark the workspace modified); and warn at open and on entering the strip
  editor instead of on every edit.

- [x] **Menu bar in the standard categories.** *Done 2026-09-28*, with one `PresetStore` for the application
  (MainWindow owns it, every strip editor gets it; import and export of packs are in *Tools*). Today *Workspace* holds file commands, Undo/Redo and panel
  toggles; *Templates* is separate from the canvas profiles it belongs to; *Process* and *About* are
  not the standard names; and the new commands below have no home. Proposed: File · Edit · View ·
  Canvas · Output · Tools · Render · Help, single-word names, flyouts one level deep (Microsoft's
  menu guidelines). Every action keeps its shortcut. Open: bubble-preset import/export in *Tools*
  needs one application-wide `PresetStore`, since today each strip editor owns its own.

- [x] **Workspace fonts.** A `fonts/` folder in the workspace, activated for the session at open
  (`QFontDatabase::addApplicationFont`, no install, no admin), which is InDesign's *Document Fonts*
  model. *Tools → Fonts…* groups them, with *Add font…* and *Install* (a per-user install,
  Windows 10 1803+). Files dropped in by hand are picked up at the next open.
  - ~~Measure precedence~~ **Done (2026-09-30).** The `fonts/` copy wins over an installed one of the
    same family, which is listed once; a per-user install is seen by a new process only.
  - ~~Activation and Save As~~ **Done (2026-09-30).**
  - ~~*Tools → Fonts…*, *Add font…*~~ **Done (2026-09-30).**
  - ~~*Install*~~ **Done (2026-09-30).** Offering it when a package brings fonts is part of
    *Open package…* below.

- [ ] **A clickable workspace file (`<name>.platemaker`) — idea, to decide.** Today Platemaker has to be
  started before a workspace can be opened, and `*.platemaker.json` cannot be associated with it: Windows
  associates by the last extension, which is `.json`. The idea is **not** to replace the workspace file, but to
  add a small GUI-owned file beside it that the operating system opens with Platemaker, and that holds what
  belongs to the GUI rather than to the library's model — a layout version for the folder (a place to
  migrate from), the window's state, the active profiles, the open docks.
  - **Precedents:** VS Code's `.code-workspace` (a JSON file you open, naming folders and settings, beside the
    code's own files); Sublime Text's `.sublime-project` (shared) and `.sublime-workspace` (per-user state);
    Visual Studio's `.sln` above its projects, with per-user state in `.vs/`; Siemens TIA Portal's `.apNN`, the
    file you double-click in a project folder whose data lives in subfolders beside it; Ableton Live's
    `Ableton Project Info/` folder of metadata next to the `.als` set.
  - **To settle:** which file the one-workspace-per-folder rule counts, and what opening the `.json` directly
    still means; that Save As, the sweep and the package carry it (a package mirrors the folder, so it would
    travel as is); the installer's file association; what happens when the two disagree.
  - The package itself could get its own extension and association the same way (Ableton's `.alp` model) —
    smaller, and independent of the rest.

- [x] **Line styles and balloon shapes** — *Done 2026-11-02.*
  Built: shapes as families (Box, Ellipse, Polygon, Curve,
  Cloud) with templates, edited on the strip; line × texture (Clean, Flash, Brush, Drybrush × None,
  Rough); the zigzag edge; the panel in drawing order (Shape · Fill · Line · Texture · Effects · Text);
  Paint with halftone patterns on the fill, the line and the shadow; a shadow with spread; a halftone
  shadow that fades, and a fill whose halftone fades by a cross that moves, either way; a thought tail
  (Circles). What is left is the next item.

- [x] **Line styles, balloon shapes and bubble outlines** — *Done 2026-11-11.* Built: the Dashed,
  Multi-line, Sketch (short pen strokes, or long loose ones with ink blots), Dry ink and Explosion lines;
  the wave, scallop and spike edges; a Polygon's bow, even or not; *Regular polygon / star…*; the Hexagon
  and Octagon tiles; several nodes picked and moved together in edit mode, left by a double click; a
  filter's and a balloon's saved numbers kept to four places; a double click in OBJECT STACK bringing the
  object into view. Hatching and glow were there already: the Lines and Grid patterns, and a centred soft
  shadow. What is left is the next item.

- [ ] **Line styles and balloon shapes.**
  - **Drip:** ink running down from the bottom edge. Every edge goes in from the outline, which keeps the
    balloon in its box; a drip goes out, so it is a look of its own (gravity, uneven lengths, round ends)
    rather than another edge.
  - **Fine-tuning:** the tile set and its order, defaults by eye, the UX of edit mode.
  - **The dry brush of the references** differs from ours by a rough applied to some strokes at random,
    not to the whole outline.
  - **Bow on a Box:** a speech balloon with concave sides, the Box drawn as a Polygon only while it bows,
    so its exact text area stays for every other one.
  - *Regular polygon / star…* is in the Shape section only; the object's menu could offer it too.
  - The flash's direction could be chosen from a shape's convex and concave vertices instead of a
    per-family value — an idea, not planned.
  - The dry brush: a tail's root that turns less than a corner (`k_cornerTurn`, 35°) is followed
    round, and an offset turned round a tight concave bend may fold; left for when it shows. Its hidden-
    part run-on (`k_partRunOn`) and corner thresholds are constants, not rows.
  - The thought tail: the next balloon's tail (`NextTailEditor`) has no look of its own; a chain's
    ellipses take their length along the tail from its length, so a fixed aspect would need a row of its
    own and would loosen the tip from the last one.

- [ ] **Joined balloons.** Two balloons of one speaker, read in turn, joined into one outline by a neck
  between them, as a manga reference does. A balloon is a graph: bubbles and the terminals of their
  tails are nodes, tails and necks its edges; the bubbles form a chain, every bubble and tail of it wears
  one style, each bubble keeps its own place, shape and words, and the whole graph is one SVG. Joining two
  existing balloons, and splitting one by removing a neck or a middle bubble, are part of it. The object
  model holds it: a balloon is a list of bodies (`Balloon::bodies`), words sit on a body (`Words::seat`) and a
  tail leaves one (its root); everything but the neck and the joining reads the first body today.
  - **To consider: a fading fill per bubble.** A chain wears one fill, and its fade is laid across the whole
    chain (`fillBox()`), so with a fade one way the first bubble can come out nearly plain and the next fully
    toned. The alternative is the same fade in every bubble, each over its own box. A mark of a bridge would take
    the fade of the bubble nearest it (as a Flash stroke takes the nearest core, `coreFor()`): cheap, but a fade
    one way then shows a seam halfway along the bridge, where the two bubbles' tones meet; blending them along the
    bridge has none, at a cost out of proportion to how rarely a chain is faded.

- [ ] **The chapter's text at once: a script out and back in.** Every words of a project in reading order
  (down the strip; in a joined balloon along its chain; in a body, its words in order), written to a plain
  text file, one paragraph per words, and read back into the same words in that order, as one undo step. It
  serves a translator's round trip, and a script poured into balloons already placed. A larger form is a list
  of every words of the chapter, edited in place (Clip Studio's Story Editor). To decide: what a run becomes
  when its text is replaced; a file whose count does not match (refuse, or report the difference); whether
  text objects and lettered pictures are included.

- [ ] **Lettering: letter spacing, kerning and the rest.**
  - **Letter spacing** (tracking), for the words and for a run: one field in `TextProperties::fields()`,
    applied in `layOutText()` and measured by `textBlockSize()`, so *Fit to text* follows it.
  - **Kerning** from the font's metrics, on or off, and a **horizontal scale** of the glyphs.
  - **To decide:**
    - Wrapping at the frame. Today the lines are the author's (`QTextOption::NoWrap`), which is what
      keeps fitting pure geometry; a one-shot *Re-break to width* would keep that.
    - Vertical text and readings (ruby).
    - A warp along a curve.

- [ ] **Undo depth — low priority.** Each project's history keeps 20 steps. Raise it, or make it a setting,
  after measuring what a step costs in memory (a step holds compositions).

- [ ] **Tools and object kinds added from outside the editor (plugins) — idea, to decide after joined
  balloons.** The editor reads tools (`tools()`), shapes and edges (`ShapeInfo`, `EdgeInfo`) and property
  fields (`fields()`) from tables, and draws every object as a `Render::Item` list. That is a registry's shape.
  Every table is still keyed by a closed enum, though (`ToolKind`, `ShapeProperties::Kind`, `PropertyGroup`,
  `Row`). `Composition` holds exactly a balloon, a picture and words, and the editor is one executable that
  exports nothing. The library and the CLI need no change: they composite the finished SVG and keep the
  content JSON unread. So an object a plugin made renders without the plugin; only editing it needs the plugin.
  What it takes, in the order it would be built (all but the last are needed for any third kind, even a
  built-in one):
  - **Keep what is not understood.** A composition gains extension elements, `{id, kind, data}`, saved,
    compared and undone as they are. An object of an unknown kind is drawn from its saved SVG, as a picture
    is. Without this, saving a workspace opened without the plugin loses the plugin's objects.
  - **An element kind interface**, with the balloon, the picture and the words as its first implementations.
    It covers render items, bounds, handles and their drag, the hit test, the OBJECT STACK label and glyph, and
    the groups the kind carries. `carriesGroup()`, `Object::Kind` and the one place items are made then ask
    it. The built-in kinds keep their direct reads of `balloon` and `picture`.
  - **A tool behaviour hook.** A `Tool` row gets an optional handler for press, move and release, called by
    `CanvasInput` before its own switch, and its options page comes from a factory.
  - **A generic property panel** built from a group's `fields()`, with one row per field. A kind that needs
    more brings its own widget.
  - **Shapes by id.** `ShapeProperties::Kind` becomes a key, and an added shape keeps its parameters in its
    own section. `Golden.EveryShapeAndStyleIsWrittenAsBefore` must stay byte-identical.
  - **Only for DLLs:**
    - What was done: `widgets/composition/` and the extension API in a shared library, plugins loaded with
      `QPluginLoader` from the application's own `plugins/` folder, and an interface id with a version.
    - What it costs, for as long as plugins are supported:
      - Every plugin must be built with the application's MSVC and Qt, so a Qt bump breaks them all.
      - A plugin must be GPL-compatible to be distributed (the FSF's reading of the GPL).
      - Code from others runs in the signed process.
    - A workspace package never carries a plugin: that would be an executable inside a document.

  The alternative to DLLs is data: shapes read from a file (a parameterised SVG path) or a script run by
  `QJSEngine`. That needs the first and the shape steps and no binary interface, but it cannot add a tool
  with its own interaction. Leaning: open the editor inside (all but the DLL step) when a third kind is
  wanted, and DLLs only for a concrete author outside the project.

- [x] **Tail options** — *Done 2026-11-13.* A Plain tail ends Sharp, Round or Cut (*Tip*, *Tip width*), and
  can break into a sharp zigzag (*Breaks*, *Jog*, *First turn*), checked under every line and the Rough
  texture. Left: a Sketch stroke can shoot off a tight round end.

- [x] **Text area and lettering effects** — *Done 2026-11-03.* Built:
  - **Two paddings**, sides and top. The words start centred and grow both ways with each line; once
    they reach the top padding they grow downwards, out of the balloon, never cut at the bottom (the
    sides and the top still clip). *Fix padding* lowers each padding in its own direction.
  - **The text area on the strip**: in shape editing a balloon with words shows it, with arrows on the
    left, right and top edges that drag the paddings.
  - **What the letters wear**, each off by default and in the Text section: their colour as a Paint (a
    halftone too), an **outline** outside the glyphs (a Paint and a width), and a **shadow**, the
    balloon's own, cast by the letters with their outline; solid at no offset it is a second outline,
    soft a glow.
  - A shadow is never a union of its caster and its growth: drawn as the body and a band in one layer or
    group (a union of a line of lettering took 1.5 s).
  - **Left:** letter spacing and a warp along a curve are in *Lettering: letter spacing, kerning and the
    rest*, above (a style per word came as runs, below). A lettered picture's
    soft shadow shows hard on the strip (the picture is drawn by its own item, not through the library)
    and blurred in the output.

- [x] **Words as elements: several per balloon, a frame each, runs of other lettering** — *Done 2026-11-17.*
  A balloon or a picture holds several words, each its own frame (four edges, automatic or set) and lettering,
  picked on its own (its letters, its frame's handle, its row in OBJECT STACK) with an OBJECT STATE of its own.
  *Fit to text* fits the body to every words' block where it lies. A stretch of the text can override the
  family, size, bold, italic and fill (runs), edited in the Text section's text box as in a word processor.
  Left:
  - A *Mixed* spin box steps from the least and the greatest value only for the text size (`MixedSpinBox`);
    every other spin box still starts from its minimum. Each would need the spread of its values at binding.
  - Seen once and not reproduced: after typing in a second words, its whole text took a font picked earlier.
    If it comes back, the text box's caret and selection handling (`TextEditor::rowChanged()`,
    `showSelection()`) is where to log.

- [x] **Shape registry** — built 2026-10-29, ahead of the first parameter: one table,
  `k_shapes` in `widgets/composition/shapes.cpp`, one entry per shape (name, title, tail, outline, text
  area), in one file rather than one file per shape. The parameters came after it, as field lists.
  What it replaced: an enum value plus a case in seven places (`shapeName`, `shapeTitle`, `shapeOrder`,
  `shapeFromName`, `Painter::silhouette`, `Painter::textSafeArea`, `Painter::label`). Moved byte for byte, checked by `Golden.EveryShapeAndStyleIsWrittenAsBefore`.
  - **Seeds:** a noisy outline reads the composition's `seed` (an `outline` that needs it then takes it), never a seed of its
    own, so a preset cannot copy a wobble. The shape that needs it brings a **Re-roll** action (one undo
    step) on the object menu.
  - Optionally in the same step: a tool row names its options page through a factory, so `Editor`'s
    constructor stops listing the panels by hand.
  - **Later idea, not planned:** shapes loaded from a file (a parameterised SVG path, or a script) are
    the same table with rows read at run time. The library needs nothing, since it composites the
    finished SVG. A package would have to carry such a file, as it carries `fonts/`. Plugins, DLLs
    included: *Tools and object kinds added from outside the editor*, above.
  - **Not planned: a folder per object kind.** `stripeditor/kinds/<kind>/` behind an `ObjectKind` interface
    (item, prototype, options page, label, glyph, conversion rules) would turn the controller's kind checks
    into virtual calls. With two kinds that is one interface for two implementations; revisit when a third
    kind arrives (a masked picture, a panel frame). It must keep one `Composition` for every kind. The
    element kind interface in the plugins item above is that interface.

- [x] **Export / open a workspace package.** One zip with the workspace, `overlays/`, `templates/`,
  `inputs/` and `fonts/` (everything in `fonts/` plus the installed fonts the bubbles name; the
  platform's default font is not packed). Every path is
  relative, uids and hashes are kept, and there are no outputs and no output directory: a package
  carries what reproduces the outputs 1:1, not the outputs. A `package.json` manifest records versions
  and missing pages; missing pages do not stop the export. The lib plans and writes (libarchive,
  which already ships inside the libvips package; only the header has to be fetched), the GUI adds
  what only it knows (fonts, the pictures behind lettered pictures), and the CLI gets
  `workspace export`. *Open package…* unpacks into a new folder named after the package.
  Needs: relative paths, and workspace fonts.
  - ~~Lib: plan, write, manifest, CLI `workspace export`~~ **Done (2026-09-30).** A CLI package renders
    byte-identically from the unpacked copy with the original pages moved away.
  - ~~*Export package…*~~ **Done (2026-09-30).** Font files are found on Windows only (DirectWrite);
    fontconfig on Linux is still to do.
  - ~~*Open package…*~~ **Done (2026-10-01).**

- [x] **Namespace hygiene for the `pm:` recipe** — three small things, together, before overlay files
  start travelling between people (which the import work makes routine):
  - ~~**`platemaker.dev` is not registered.**~~ **Done (2026-09-23).** The URI is now
    `https://github.com/ShadobaDev/Platemaker-qt/ns/artifact/1` — a name the project controls, and one
    that no longer claims the composition is about bubbles. No back-compatibility: the feature is unreleased,
    so the only overlay files carrying the old namespace were the author's own and were re-made by hand.
    A file written before the change imports as ordinary artwork, which is what it now is to us.
  - ~~**We write `pm:v="2"` and never read it.**~~ **Removed (2026-09-24).** A version marker nobody
    checks is decoration, and before the first release there is no older reader for it to warn. The
    first compatible revision that needs telling apart adds a version attribute; its absence then means
    today's format, so dropping it now costs that future nothing.
  - ~~**The two version numbers need a stated rule.**~~ **Stated (2026-09-23), simplified (2026-09-24)**
    to one number, in `artifactsvg.hpp` and in the specification: the URI's `/1` changes only on a
    break, where an old reader *should* fail to recognise the file at all.

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

- [x] **Store bubbles as SVG instead of PNG — the library already accepts it, measured. DONE.**
  Shipped: the SVG carries the artwork *and* the editor parameters in a `pm:` namespace, so it replaced
  the PNG **and** the authoring sidecar. See the CHANGELOG entry and the wiki's [Workspace folder ownership](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Workspace-Ownership).
  The overlay compositor opens `assetPath` with `vips_image_new_from_file()`, so it takes **any format libvips can
  read**, SVG included (via librsvg). Verified end-to-end: pointing a `StripOverlay` at
  `fixtures/overlays/bubble-speech.svg` and rendering through the CLI composited the balloon on the right
  page with its alpha intact — **no library change at all**. The size it rasterises at is the SVG's own
  `width`/`height` at 72 dpi, which is exactly the strip-scale pixel box the GUI already authors in.

  Why it is interesting: an SVG overlay is *resolution-independent* (a target-width change would
  re-rasterise rather than resample), it is a real interchange format (draw a bubble anywhere, drop it
  in), and SVG **filters** would give bubble styling the GUI cannot reach with `QPainter` alone —
  a dried-marker edge, a rough ink texture, a paper grain — declaratively, without a filter engine in
  either the library or the GUI. That reframes the authoring model as a *small SVG editor*: shape
  handles, a movable tail anchor, per-shape filters, rather than the "box you drag" it is today.

  What it would cost, honestly:
  - **librsvg is an optional libvips module.** It is present in this build; a libvips without it would
    fail to load the overlay (logged and skipped, not fatal — but the bubble silently vanishes). Shipping
    SVG as *the* storage form makes a soft dependency into a hard one, so it needs a startup probe and a
    PNG fallback.
  - libvips marks `svgload` **untrusted** (librsvg parses external XML). A consumer that enables
    `vips_block_untrusted_set()` would refuse it.
  - The GUI would still need to rasterise for the *preview*, since `QPainter` draws the scene — so
    `QSvgRenderer` becomes the preview path and Qt's SVG feature coverage (notably filters) becomes the
    limit on WYSIWYG, not librsvg's.
  - The sidecar's job would shrink but not vanish: an SVG is a *shape*, not an editing model. Which
    control point is the tail, which rect is the text-safe area, which font was requested — that is
    still ours to record.

  **Convention for hand-drawn shapes** (the same one a "custom shape" slot would need, so settle it
  once): SVG, transparent background, nominal width ~1000 px (it gets scaled, so the number only fixes
  the stroke-to-size ratio); the **tail as its own `<path id="tail">`** so it can be hidden, mirrored or
  rotated independently; and the **text-safe area as `<rect id="safe" fill="none">`** — the box text may
  wrap inside. Without that last one the GUI has to guess an inset, and it guesses wrong on anything
  non-rectangular. Strokes as real strokes (not outlined paths) if they should stay re-colourable.

  **Why the built-in shapes should stay parametric either way:** a bubble resizes to its text, and a
  hand-drawn outline stretched non-uniformly distorts its own stroke (a 3px line becomes 3px on one axis
  and 9px on the other). Drawn art belongs in the *custom* slot, used at its natural aspect ratio.


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
  Source program grants a **free real Authenticode cert** (OV-class), which is the one option that
  actually clears the Defender `Wacatac!ml` FP.

  | option | fixes the Wacatac FP? | what it is actually for |
  |---|---|---|
  | **Sigstore** (cosign/gitsign) | **No** | Artifact/commit provenance. Not an Authenticode signature — Windows, Defender and SmartScreen ignore it for `.exe`. We already get the equivalent from the build-provenance attestation in `release.yml`. |
  | **SignPath.io OSS** | **Yes — the one** | Free, real Authenticode cert + signing pipeline for verified OSS. A genuine publisher identity is what lifts a binary out of the anonymous-unsigned ML dice-roll. |
  | **GnuPG / GPG** | **No** | Detached sigs for tags/tarballs (pairs with our SHA256SUMS). Web-of-trust; Windows ignores it for execution. |

  Caveats: approval needs their OSS verification (public repo + OSI licence — GPL-3.0 qualifies), and it
  is not instant. The free certs are **OV, not EV** — that kills "unknown publisher" and is exactly what
  stops the `Wacatac!ml` FP, but SmartScreen *download* reputation still accrues over time (only a paid
  EV cert is instant). Signing runs on their infrastructure via a GitHub Actions step.

  **Paid fallback if approval stalls:** Azure Trusted Signing ≈ $10/mo (Microsoft-run Authenticode,
  short-lived certs, especially effective vs Defender — but historically wants a 3+ year org history,
  which can block an individual dev); OV ≈ $200–400/yr, EV ≈ $300–700/yr. This does not overturn the
  standing "don't pay" decision — it records that SignPath OSS is a *free* route to a real signature,
  which the earlier "only a paid cert removes the warning" framing missed.

  **Next research step:** confirm SignPath's exact OSS eligibility criteria and how their Actions signing
  step slots into `release.yml` — sign the installer **and** the portable exe before the VirusTotal scan.

- [ ] **Submit to winget (`winget-pkgs`)** — free community channel giving users a trusted
  `winget install Platemaker` path; the manifest validates the installer's SHA-256. Cleaner than a raw
  `.exe` download (doesn't remove SmartScreen on direct download, but the winget flow is smoother).
  Consider a Scoop bucket too for the dev audience.

- [x] **VirusTotal report** — `Platemaker-1.3.0-Setup.exe` scanned **0 / 68 clean**
  ([report](https://www.virustotal.com/gui/file/6d1b95c6dc68d94c9d7a8b4ea7a7c41f2135538d3ea4ab1bade091551cae7602)),
  linked from the README. Per-build (tied to the file hash), so rescan each release; could later be
  automated in the release CI via the VirusTotal API.



---
