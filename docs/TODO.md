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
  slice-seam guides. See SPECIFICATION §2.5.
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
  contract and the anchoring rule are documented in **SPECIFICATION §2.5.4**.
  - **Placement is page-anchored** (`StripOverlay::anchorInputUid` + an offset from that page's top,
    resolved by `Models::resolveOverlayAnchors()`), not an absolute strip-Y. An absolute placement drifts
    onto different artwork the moment anything above it changes height — inserting a page is the everyday
    case — and drifts silently. Pinned by `test_overlay_anchoring.cpp` and `test_overlays.py`, each with a
    deliberate absolute-placement control.
  - **A bubble is a GUI object, a bitmap is what the lib sees.** `Artifact` (shape / box / tail /
    text / font / colours) lives inside the overlay's own SVG in `overlays/`, in a `pm:` namespace every
    renderer ignores; the same file is what gets composited. That is what keeps a bubble re-editable
    instead of flattened.
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
  because it carries no *recipe* — the `pm:` attributes a Platemaker-written SVG has — and a foreign
  file's paths cannot be guessed into `shape.kind`. What is already in place is most of it: tails aim at
  an arbitrary outline (`tailPath()` takes a `QPainterPath` and ray-casts, so it does not care where the
  outline came from), the style filter and fill/stroke work on any path, and the lettering is drawn over
  whatever is underneath. What is missing is **a place in the record for a custom silhouette**
  (`ShapeProperties` holds an enum; it would need an "imported path" variant) and **a dialog where the
  artist marks which path is the silhouette and where the tails attach** — i.e. writes the recipe for
  somebody else's drawing. The honest limit: one closed silhouette, not a drawing of two hundred paths,
  or "fill" and "outline" stop meaning anything.

- [ ] **Names that describe what things were, not what they are** — a sweep, deliberately deferred so it
  lands as one mechanical diff rather than as noise inside feature work. Nothing here changes behaviour.
  - ~~**`TextArtifact` → `Artifact`.**~~ **Done (2026-09-23).** It was never the text: it is the whole
    authoring record of an overlay — shape, skin, style, tails, the imported picture, *and* a
    `TextProperties text` field, which is the only part the old name described. Twenty sibling symbols
    already said *artifact*; only the struct still said *Text*, and a reader's first question was *why
    does a "text artifact" have tails and a picture?* 396 occurrences in 49 files, crossing into
    `widgets/project/project.hpp` but not into the library. `widgets/textartifact/textartifact.{h,cpp}`
    became `widgets/artifact/artifact.{hpp,cpp}`, which also makes the directory one thing: `artifact`,
    `artifactsvg`, `artifactpainter`. No `using` alias — the feature is unreleased, so nothing had to
    keep the old spelling alive, and an alias would have kept two names for ever.
  - **`Artifact::artwork` is not necessarily art.** It holds whatever picture the artist placed — a
    sprite, a sound effect, a logo, a screen grab. *Artwork* reads as "someone's drawing", which is
    only sometimes true. Something like `picture`, `image` or `source` says what the field is without
    claiming what it is *for*. (The same word is worth auditing in `AssetObject`, `loadArtwork()`,
    `ArtworkOptionsPanel` and the **Artwork** tool — the rail's label is a separate question from the
    code's, since the artist's word for it may legitimately differ.)
  - Expect more of these once the two above are pulled: the sweep is the point, not the individual
    rename.

- [ ] **A workspace owns its folder — and cleans up the files nobody uses.** Nothing ever deletes from
  `overlays/` (measured 2026-09-24: **46 files, 2 referenced**), and nothing guarantees that the
  folder belongs to one workspace, or that only one process writes to it. Two `*.platemaker.json` in
  one folder share `overlays/` and `templates/`, and a second instance opening the same workspace
  could sweep the first one's unsaved files. Cubase and Ableton both hit exactly this, and both
  answer with *one project per folder*. So the guarantees come first and the cleanup last:
  1. **One workspace per folder.** New and Save As refuse a folder that holds another workspace and
     offer a subfolder. A folder that already holds two does not open until a wizard resolves it
     (the ones not kept go to the Recycle Bin).
  2. **A workspace lock** (`QLockFile`, `.platemaker.lock` in the folder). A stale lock on this machine
     goes by itself. A live one here is reported. One from another machine (synced drive) can be
     **taken over**, and the instance that lost it refuses its next write and says why.
  3. **Relative paths** *(lib)*: an overlay path inside the folder is also stored relative, and a
     relative path is read in every path field, so the folder can be moved or zipped. Both forms are
     kept, so older builds still read the file.
  4. **Save As collects** what the workspace made: overlays, the pictures behind lettered pictures,
     and templates. It is all or nothing, and it also runs on undo so a history never points back
     into the old folder. This also fixes Save As breaking templates, whose paths are relative.
  5. **Sweep at open, to the Recycle Bin**, only with one workspace in the folder and the lock held,
     and only files with our own names (`ovl-*`, `art-*`, `templates/*`). It is reported with an
     advisory offering *Show*. Measured: on the Google Drive `G:` the files land in the ordinary
     Windows Recycle Bin. At open, not at close: after *Discard*, memory is not what is on disk.
     *Delete template* stops deleting the file, which makes it undoable again.

- [ ] **Bug: a missing font silently re-letters bubbles.** A bubble's text is baked into its SVG as
  outlines through `QFont`. When the family is not installed, every re-emit bakes it in a fallback,
  including **every overlay undo**, because `rewriteOverlayAssets()` rewrites *every* bubble, not
  just the ones the step touched. Fix: rewrite only what a step changed; keep the intended family in
  the record (already true); mark a fallback bake with `pm:fontFallback`; **heal at open** when the
  font is back (re-bake, and mark the workspace modified); and warn at open and on entering the strip
  editor instead of on every edit.

- [ ] **Menu bar in the standard categories.** Today *Workspace* holds file commands, Undo/Redo and panel
  toggles; *Templates* is separate from the canvas profiles it belongs to; *Process* and *About* are
  not the standard names; and the new commands below have no home. Proposed: File · Edit · View ·
  Canvas · Output · Tools · Render · Help, single-word names, flyouts one level deep (Microsoft's
  menu guidelines). Every action keeps its shortcut. Open: bubble-preset import/export in *Tools*
  needs one application-wide `PresetStore`, since today each strip editor owns its own.

- [ ] **Workspace fonts.** A `fonts/` folder in the workspace, activated for the session at open
  (`QFontDatabase::addApplicationFont`, no install, no admin), which is InDesign's *Document Fonts*
  model. *Tools → Fonts…* groups them, with *Add font…* and *Install for me* (a per-user install,
  Windows 10 1803+). Files dropped in by hand are picked up at the next open. To measure first: which
  copy Qt draws with when the same family is also installed system-wide in another version.

- [ ] **Export / open a workspace package.** One zip with the workspace, `overlays/`, `templates/`,
  `inputs/` and `fonts/` (everything in `fonts/` plus the system fonts the bubbles use). Every path is
  relative, uids and hashes are kept, and there are no outputs and no output directory: a package
  carries what reproduces the outputs 1:1, not the outputs. A `package.json` manifest records versions
  and missing pages; missing pages do not stop the export. The lib plans and writes (libarchive,
  which already ships inside the libvips package; only the header has to be fetched), the GUI adds
  what only it knows (fonts, the pictures behind lettered pictures), and the CLI gets
  `workspace export`. *Open package…* unpacks into a new folder named after the package.
  Needs: relative paths, and workspace fonts.

- [x] **Namespace hygiene for the `pm:` recipe** — three small things, together, before overlay files
  start travelling between people (which the import work makes routine):
  - ~~**`platemaker.dev` is not registered.**~~ **Done (2026-09-23).** The URI is now
    `https://github.com/ShadobaDev/Platemaker-qt/ns/artifact/1` — a name the project controls, and one
    that no longer claims the record is about bubbles. No back-compatibility: the feature is unreleased,
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
  the PNG **and** the authoring sidecar. See the CHANGELOG entry and SPECIFICATION 2.5.4.
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
