# Changelog

## [Unreleased]

Requires libplatemaker **0.7.0**.

### Added

- **Strip editor: the chapter as one continuous strip, lettered in place.** *View strip* on a project's
  Output tab, its card on the Workflow map, or **View → Strip editor** (**Ctrl+E**; *View → Strip
  editors* lists every project) opens the chapter's input pages in a tab beside the Workspace, which can
  float or be split out. Scroll, zoom (fit width, 100%, Ctrl+wheel) and toggle guides where the output
  will be cut. It works before the first render and follows input and profile edits live. A long
  chapter, or one drawn as a single strip up to 65,500 px, opens at once. What it shows is what the
  render produces.
- **Balloons, captions and text.** The Bubble and Text tools draw an object where you drag one out.
  Any object can be selected, moved and resized under any tool, from eight grips: **Shift** keeps the
  proportions, **Ctrl** resizes from the centre. The tool's options, under the tool rail, hold only
  what to draw: a preset, the shape, a tail and the colours (for the Text tool, the font, its colour and
  its outline). The right-hand panel shows the selected object's *Shape*, *Fill*, *Line*, *Texture*,
  *Effects*, *Text* and tails, only the sections that apply.
- **Shapes:** a box with a corner radius (speech, caption), an ellipse, a polygon (shout, diamond,
  caption plate, banner), a cloud with lobes (thought) and a curve (scroll), any of them with a zigzag
  **edge**. Double-click a balloon, or pick *Edit shape*, to edit it on the strip: round its corners,
  move, add and delete points, bend a curve's handles and set their point type, drag the text area's
  paddings. A picked point shows the handles it lacks, and *Add handles* gives them to it. A right-click
  on a point opens that point's menu. The balloon's frame follows its points both ways. *Convert to
  curve* makes any balloon an editable curve.
- **Lines and textures:** solid, **Flash**, **Brush** and **Drybrush** lines, and a **Rough** texture
  that makes the outline wander while a fill's pattern stays put (*Roughen the fill too* wobbles it as
  well). *Re-roll* deals a line's strokes, a texture's wobble or an edge's teeth again.
- **Lettering:** lines break only where you break them. Line spacing; side and top padding, negative
  to let the words out of the balloon; an outline round the letters; a halftone colour; a shadow or a
  glow. *Fit to text* sizes the balloon to the lettering, *Fix padding* keeps the size and lowers the
  paddings, and lettering that does not fit is outlined in red.
- **Tails:** any number per balloon, each with its own tip, width and bend, leaving from any edge; a
  thought tail is a chain of circles. A tail tip stays on its speaker when the balloon is resized.
- **Patterns and shadows:** fill, line and shadow can be halftone dots, lines or a grid in a second
  colour, and a fill's pattern can fade out, set by dragging on the strip. A drop shadow under the whole
  balloon has a spread, an offset and a softness. Colours may be transparent.
- **Presets:** a named look, for balloons or for text, shared by every strip editor. Pick one in the
  tool's options for the next object, or apply one to existing objects, one or many, from the panel or
  *Apply preset ▸*, after a question; *Reset to default* takes it back. Save one from an object (*Save as
  preset…*). **Manage presets** lists them, draws and edits them, and lists the objects made from each;
  every object shows the preset it was made from (*Shout-based*). Built-in ones, and a JSON file carries
  them between machines (*Tools → Import / Export bubble presets…*).
- **Several objects at once:** select them with Ctrl or Shift, or drag a box round them with the Select
  tool; then recolour, restyle, move or delete them together. Values they do not share read *Mixed*.
  Tails can be selected and moved with their balloon, and several tails share their width, bend and
  style.
- **Object list:** the strip's objects front to back, each with a drawing of itself and chips for an
  unanchored object or a blend mode. Drag to reorder, tick to mute, *Duplicate* (Ctrl+D) and *Delete*
  from the context menu. The strip and its pages sit at the bottom.
- **More on an object's context menu:** *Blend ▸* (Normal, Multiply, Screen, Overlay, Darken, Lighten;
  also in the properties panel), *Bring forward / Send back*, *Fill with primary colour*, *Outline with
  secondary colour*, *Reset to default*, *Save as preset…* and *Convert to ▸ Text / Balloon*.
- **Colour pair, eyedropper and colour tool.** A primary and a secondary colour under the tools (**X**
  swaps them, **D** resets to black and white). The eyedropper takes a colour as it appears on the
  strip. The colour tool paints the lettering, the outline or the fill, whichever you click.
- **Artwork:** import a picture, place it with the Artwork tool or drop it from a file manager, resize
  it by percentage, to its original size or to the strip's width, and letter it. An SVG stays sharp at
  any zoom. An SVG that Platemaker wrote comes back as an editable balloon.
- **Objects belong to their page.** An object is anchored to the page it is drawn on and scales with
  the output width. One whose page leaves the project is kept, unanchored, and *Re-anchor to ▸* puts
  it on a page again. Removing a page that has objects asks whether to keep them. **Replace file…** on
  an input tile swaps in a newer scan of a page and keeps its objects.
- **Bubbles render without their font.** A bubble is an SVG in the workspace's `overlays` folder with
  its text as outlines; the font is needed only to change the text. A font that is missing is reported
  (*Show them* selects the bubbles) and never silently replaced.
- **Every edit can be undone,** on the project's own history, which the project panel and the strip
  editor share and which outlives their windows. **Undo takes you to what it changed:** the window comes
  to the front and the object is selected and scrolled into view; in the project panel, the tab and the
  box that changed are outlined. Undoing a profile, template or project-name change opens its list
  on that row.
- **Colour correction:** the Grade tool's *Brightness & contrast* and *Saturation*, applied live and in
  the render. A page can be excluded from it. The workflow card's **−** removes the grade.
- **The status bar says what will not render as meant:** objects with no page under them, and
  outdated templates. Click to go to them. A detached strip editor shows the same chips along its bottom
  edge. Render asks before it leaves such objects out (*Show them*, *Delete them*, *Render anyway*);
  refreshing every project (F6) skips that chapter and says why.
- **A workspace reopens the way you left it:** the open project panels and strip editors, docked,
  tabbed or floating, and the main window's size and place.
- **A canvas-profile change asks before it puts pages out of sync.** Saving or deleting a profile lists
  every project it reaches, including projects with no window open, and what their pages will render
  with.
- **Templates are layered PSDs by default:** a canvas of the profile's size and resolution with the
  template on its own layer, which a drawing app opens without resizing — so no more stray
  template-coloured edge on the left and bottom of a page. *Canvas → Templates…* gains **Template
  settings** (PSD or PNG, an optional empty layer) and **Regenerate all**. A profile keeps one template
  per format, each with its own chip and status, and *Outdated* says why.
- **Canvas profiles may share a name.** Lists show what sets such profiles apart (*2000×8080*) and mark
  an identical one *Duplicate*; their templates no longer overwrite each other.
- **Import and export canvas and output profiles** between workspaces, bundle files and a personal
  profile library (*Canvas Profiles* / *Output* → *Import*, *Export*). An import adds copies and can be
  undone.
- **The profile editors show what you changed.** A small note under each field you changed gives the
  saved value (*saved: 300*), and **Revert** puts every field back, after asking.
- **Fonts can travel with a workspace:** font files in a `fonts` folder beside the workspace file are
  used while it is open, without installing them. *Tools → Fonts…* lists them and the fonts your
  bubbles miss, adds a font to the workspace, and installs one for your user account.
- **Export package… / Open package…** (*File*) carry a whole workspace, with its pages, bubbles,
  artwork, templates and fonts, in one `.platemaker.zip`.
- **Portable ZIP:** the release also ships `Platemaker-<ver>-portable.zip`, to unzip and run.

### Changed

- **Profiles have a resolution.** A canvas profile's *Resolution (dpi)*, 300 by default, is recorded
  in its template, which said 25.4 DPI. An output profile's, 72 by default, is recorded in every slice.
  The pixels do not change, but every project rendered before reads as outdated once.
- **"Out of sync" shows as soon as a setting changes,** in every open project, and a tile's tooltip
  says what changed since the render.
- **Files are checked in the background,** all at once, when a project opens, before a render and on
  **Refresh files**; the window stays responsive. **Stop** also ends a render during the check.
- **Opening a project no longer resizes the window.**
- **Right-clicking the project list with no workspace open offers *New workspace*.** It did nothing.
- **The menu bar is in the usual order:** *File, Edit, View, Canvas, Output, Tools, Render, Help*.
  Every entry and shortcut is still there. *File* gains *Exit*.
- **Freer docking.** Panels dock side by side, split or tabbed. The **Action** panel keeps its own
  column, folds into a narrow one (**`>|`**), and its log marks each line as an error, a warning or
  information.
- **Panel title bars** have working minimise (dock or detach), maximise and close buttons.
- **One workspace per folder.** *New* and *Save As* refuse a folder that already holds a workspace and
  offer a subfolder. Opening a folder that holds several asks which one to keep.
- **A workspace is open in one place at a time.** One left open on another computer can be taken over.
- **Unused files are tidied away:** bubbles, artwork and templates the workspace no longer uses go to
  the Recycle Bin when it is opened. Files you put there yourself are never touched.
- **Save As takes the workspace with it:** its bubbles, artwork, templates and fonts are copied to the
  new folder. Templates used to go missing. A workspace folder can also be moved or zipped.

### Fixed

- **Changes in *Manage profiles* are kept.** Closing a profile manager with X or Esc dropped every
  change. Each New, Save, Duplicate and Delete is now saved at once, and Ctrl+Z / Ctrl+Y undo and redo
  it there.
- **F5 renders the project in front.** After opening another workspace or removing a project, it could
  render another one.
- **A profile edited in one project's panel shows in every open project.**
- **Both arrows of the number fields in the profile dialogs work on Windows 11.** Only the down arrow
  did.

## [1.4.3] — 23.08.2026

Built with **MSVC 2022** instead of MinGW (requires libplatemaker **0.5.1**'s MSVC package). The switch
responds to a Microsoft Defender ML false positive (`Wacatac.B!ml`) that flags MinGW-compiled, unsigned
binaries; the MSVC build of the same sources is clean at the binary level. Also bumps Qt to 6.11.2 and
slims the installer.

### Changed

- **Windows binaries are now built with MSVC 2022** (previously MinGW). Requires libplatemaker **0.5.1**
  (its MSVC dev package). MinGW and MSVC are ABI-incompatible, so this is a hard toolchain switch — the
  lib's package config enforces a matching toolchain at configure time.
- **Leaner installer.** The MSVC C++ runtime ships as small app-local DLLs (`vcruntime140`/`msvcp140`)
  instead of bundling the ~25 MB `vc_redist.x64.exe`, and unused Qt plugins (network / TLS / touch) are
  excluded from deployment — dropping the whole networking stack the app never used.
- **Qt updated to 6.11.2.**

### Added

- **The About dialog shows the build compiler and platform** ("Built with … for …"), read from the loaded
  libplatemaker at runtime.

### Notes

- The unsigned installer/executable may still be flagged by some antivirus ML engines — a known false
  positive for unsigned apps, not an infection. Verify against the published VirusTotal report and the
  build-provenance attestation on the release. A signed build is the durable fix (tracked in the TODO).

## [1.4.2] — 22.08.2026

Built against **libplatemaker 0.5.0**, adopting the lib's new render output contract (lib SPECIFICATION
§7.0). Requires libplatemaker 0.5.0 (the GUI's `run()` call now passes the cache dir).

### Fixed

- **Re-renders no longer fail with *unable to open for write*.** The render now passes the workspace's
  `.platemaker-cache` dir to the pipeline, which pre-warms each slice's output thumbnail from the in-RAM
  pixels *before* signalling the tile — so the GUI never re-reads a slice the render is still writing. This
  closes the Windows read/write race that intermittently aborted a re-render (most visibly on the *Webtoon*
  profile) and the stale/black-band output previews. A slice that genuinely cannot be published (its file
  held open by another program) now surfaces cleanly instead of a cryptic vips error.
- **The Project Status panel shows a short status, not the whole error dump.** A failed render now reads
  *Render failed — see the action log* (consistent with *Render finished.*); the full multi-line error
  stays in the action log.
- **The forced dark scheme now takes effect on Windows 10.** Windows 10's native style cannot render its
  controls dark, so the forced dark scheme previously left the shell (menu bar, docks, tabs, plain
  controls) light while the hardcoded-dark dialogs stayed dark — an unreadable mix. Where the native style
  cannot go dark, the app now falls back to the palette-driven **Fusion** style, so the whole window and its
  dialogs render a consistent dark. Windows 11 is unchanged (it keeps its native windows11 look). A fuller
  theme-agnostic option (follow the OS, or a Light/Dark/System toggle) remains future work.
- **Creating a canvas profile no longer turns a whole project amber with an alarming "state cannot be
  confirmed" prompt.** libplatemaker 0.5.0 records each page's dimensions and re-matches canvas profiles
  per page, so a profile that matches no page in a project leaves it untouched. When a canvas change *does*
  affect pages, the reopen prompt is now titled *Canvas profiles changed* and lists each project with the
  **exact page count** it affects (`• Chapter 01 — 2 page(s)`) instead of a generic warning. A project
  last rendered by an older version (no recorded page sizes) shows *needs one re-render to confirm* and
  becomes precise after its next render.
- **Detaching a panel and docking it back no longer wrecks the window layout.** Floating the Workspace or
  Action panel out and snapping it back left the layout broken — one panel filled the whole window and the
  other vanished behind it, recoverable only by dragging the splitter by hand. On re-dock the app now
  rebuilds the Workspace │ Action split and restores its previous proportion, so the panels return to
  where they were.
- **Rotated camera photos now preview upright in the input list.** A photo carrying an EXIF 90° tag showed
  a sideways (landscape) input thumbnail. libplatemaker 0.5.0 already generates the preview upright, but a
  thumbnail cached by an earlier build kept being re-served because the cache only compared file dates —
  and a camera photo is always older than its cached thumbnail. The lib now versions each cached thumbnail,
  so the stale sideways previews are discarded and regenerated the correct way up, with no need to clear the
  `.platemaker-cache` by hand.

## [1.4.1] — 2026-08-16

Built against **libplatemaker 0.4.1** (up from 0.4.0). 0.4.1 is metadata-only — no API change — so
this is purely a bundled-runtime bump on top of the fixes below.

### Security

- **Restricted the DLL search path (Windows).** `Platemaker.exe` now calls `SetDefaultDllDirectories`
  at startup to drop the current working directory and `PATH` from the default DLL search, leaving only
  the application directory (where the whole bundled DLL graph lives) and System32. This closes the
  classic DLL search-order hijacking / planting vector for an unsigned, DLL-heavy app. Defence-in-depth,
  layered under — not a substitute for — code signing; it does not affect SmartScreen. (Blocking global
  hook *injection* was considered but deferred over the IME/accessibility risk — see
  `docs/SPECIFICATION.md` §9.)

### Added

- **Windows version metadata on `Platemaker.exe`.** The executable now carries a `VERSIONINFO` resource
  (product name, version from the project version, company, description, copyright), so Explorer's
  *Details* tab and tools like Process Explorer show proper identity instead of blanks — and the file
  reads as less "anonymous" to users and heuristics. Generated from `PROJECT_VERSION` via
  `app/version.rc.in`; `app.rc` still supplies the icon.

- **The bundled `libplatemaker.dll` now carries identity metadata too.** Bumping to libplatemaker 0.4.1
  means the shipped DLL (and the standalone CLI) embed a `VERSIONINFO` resource plus a portable `@(#)`
  version marker, so Process Explorer / Explorer *Details* show identity for the bundled runtime, not
  only for `Platemaker.exe`. No behaviour change — 0.4.1 is metadata-only.

### Fixed

- **Drag-and-drop of images now works over the whole project panel, not just the input list.** Dropping
  files/folders was only accepted over the input tile list (an event filter on its viewport), so a drop
  on any other part of the panel (empty space, labels, buttons, other tabs) was rejected. The `Project`
  widget now accepts external file drops itself, so an image dropped **anywhere** on the panel is added
  via the same path as *Add files* / *Add from directory*; the list keeps its own filter so InternalMove
  reordering is unaffected, and a drop reaches exactly one handler (no double-add). (Text fields that
  natively accept a URL-as-text drop remain the one exception.)

## [1.4.0] — 2026-08-07

Last released version: **1.3.0**. This supersedes the never-released 1.3.1 (its work is folded in
here); adding the *New from this…* project action below makes the accumulated changes a MINOR.

Requires **libplatemaker 0.4.0** — the lib's processing error channel is now typed (a breaking change),
so this release adapts to it.

### Added

- **"New from this…" project action.** The workspace context menu can seed a new project from an
  existing one — copying its **input files** and **profile links** (canvas + output) but **not** its
  outputs, output directory, or render state, so the copy starts fresh (inputs *Pending*) and renders
  into its own folder. Built for the multi-publisher workflow: siblings over the same pages that differ
  only in the Output profile. The new project gets a fresh workspace-unique uid (via libplatemaker
  0.4.0's `WorkspaceEditor::duplicateProject()`), so nothing collides with the source.

- **Render summary in the action log.** A successful render now ends with a short summary — slice count
  / input count / elapsed time, the heaviest slice (name + size), and the total output size (e.g.
  *"Output: 40 slice(s) — from 12 input(s) in 3.2 s"*, *"The heaviest slice: output_017.png (612 KB)"*,
  *"Output size: 18.4 MB"*); a batch adds each project's summary plus the whole-sweep time on the
  *Batch finished* line. It is part of the saved log too.
- **Action-log right-click menu.** *Copy* / *Copy all* / *Select all*, *Open output folder* (the last
  render's output dir) and *Open log folder* (`.platemaker-cache/logs`, to grab a saved log after a
  crash), plus *Save log as…* / *Clear log*. The never-usable *Copy Link Location* entry is gone.
- **Restyled progress bar.** The Action-panel progress bar is now a slim 15 px bar with a light border
  and a dark trough (the empty part), a grey fill, turning red when a render fails or is halted.
- **Render logs are persisted and manageable.** Each render's action-log transcript is now auto-saved to
  `<workspace>/.platemaker-cache/logs/render-<timestamp>.log` when the run finishes (a batch is one
  file); the **last 10 runs** are kept, so a failing render's log survives the next one — handy for
  reviewing what happened or attaching to a bug report. The log also gained a **right-click menu**:
  *Save log as…* (export the current text anywhere) and *Clear log*, alongside the usual Copy / Select
  All. No new buttons.

- **"Unverified" input state.** An input the render produced output for but whose content could not be
  hashed afterwards (locked file / denied permission / offline drive) now shows a distinct rose tile
  labelled *Unverified* (libplatemaker's new `FileStatus::Error`) instead of being silently reprocessed
  on every subsequent render. After such a render the project reports **Require action** and the action
  log lists each unverified file; the tile recovers once the file can be read again.
- **Applying render results is guarded.** The model update in `onRenderFinished` runs on the GUI thread,
  where an escaping exception would take down the app; it is now caught, logged to the action log, and
  shown as a **Failed** status with the diagnostic — useful to attach to a bug report. (The lib's
  `run()` already guards the render itself.) `main.cpp` also installs `std::set_terminate` to log the
  in-flight exception (`qCritical`) on the `terminate` paths (uncaught exception / `noexcept` violation /
  pure-virtual call) instead of aborting silently. Hardware faults such as a segfault are not C++
  exceptions and are still out of scope here (see `docs/TODO.md`).
- **Full third-party notices reach the app.** libplatemaker 0.4.0 now ships a complete
  `THIRD-PARTY-NOTICES.txt` + licence texts + a 32-package SBOM for the bundled libvips DLL graph; the
  product `credits/` carries them into the installer, and the **About dialog links to the full notices**
  (the bundled runtime components — glib, libpng, libimagequant, … — beyond the five headline rows).
  The product-SBOM merge was fixed to **preserve** the lib's bundled-dependency relationships (it
  previously overwrote them, orphaning the packages). Note libvips as bundled is effectively GPL-3.0
  because it is built with **libimagequant (GPL-3.0)** — compatible with Platemaker's own GPL-3.0.

### Changed

- **About dialog header.** The small app icon inside the *About* tab is replaced by the wide product
  banner (`:/icons/banner`) shown **above** the tabs, scaled to span the full tab-area width. The dialog
  now has a fixed width so the banner and the tab bodies share one stable measure.

- **Adapted to libplatemaker 0.4.0's typed errors.** A fatal render error is read from
  `outcome.error->message` (the old free-text `outcome.errorMessage` is gone), and the value returned by
  `ProjectItem::applyProcessingResults()` is now surfaced (see below). No visible behaviour change for a
  normal render.

### Fixed

- **Window/taskbar icon no longer depends on the working directory.** `main.cpp` loaded the app icon
  from a relative filesystem path (`icons/icon-red.ico`), which resolved against the working directory
  and silently yielded a null icon whenever the app was launched from outside the install folder — the
  About dialog exposed it as an empty band above the text. It now loads from the compiled Qt resource
  (`:/icons/app`, added to `app/resources.qrc`), independent of the working directory.

## [1.3.0] — 2026-08-03

Requires **libplatemaker 0.3.1** — this release uses the lib's new `ProjectEditor` /
`WorkspaceEditor` snapshot/restore for undo/redo.

### Added

- **Drag files or a folder onto a project's Input list to add them.** Dropping images (or a folder of
  images) onto the Input-tab tile list adds them the same way *Add files* / *Add from directory* do —
  de-duplicated, appended in order, existing statuses preserved — as a single undoable step. A dropped
  folder is scanned like *Add from directory* (top level, image files only: PNG/JPEG/WebP/TIFF) and
  becomes the project's remembered input directory; non-image files are ignored. Reordering tiles by
  dragging within the list still works.
- **Undo / Redo across the app** (`Ctrl+Z` / `Ctrl+Y`), via the **Undo** / **Redo** items in the
  Workspace menu. Reversible now:
  - **Project edits** — add files / add-from-directory / remove / clear / reorder (drag or ▲/▼) / sort
    inputs, link/unlink a canvas profile, change the output profile, set/clear the output directory.
  - **Workspace edits** — rename a project; create / edit / delete canvas and output profiles; generate
    or delete templates.

  Each open project has its own undo history and there is one for the workspace; a `QUndoGroup` routes
  `Ctrl+Z`/`Ctrl+Y` to **whichever tab is in front** (a project dock, or the Workspace panel). Each step
  is a compact snapshot from the lib (a single project, or the workspace's profiles + names — never the
  whole workspace), so history stays light even with many projects open; depth is 10 per timeline and a
  no-op edit (e.g. re-sorting sorted inputs) records nothing. Undo restores the edited scope only —
  output staleness is recomputed by the next Refresh/render, as for a plain reorder. **Adding or removing
  a project is not undoable** (by design), and render/refresh are never recorded.
- **`Ctrl+R` also renders the current project** — an alternate for `F5` (the "run" convention in many
  editors); `F5` stays the primary key shown in the menu, and `F6` (render all) / `Esc` (stop) are
  unchanged. The rest of the app's keyboard shortcuts (`Ctrl+S`/`Ctrl+Shift+S` save, `Ctrl+O` open,
  `Ctrl+N` new) already shipped in earlier releases.

### Changed

- **The "Auto-sort rules" panel is greyed out until the feature exists.** Its fields were fully
  editable but wired to nothing; the group is now disabled with a "Coming soon" placeholder, so the UI
  no longer invites input that does nothing. (The auto-sort feature itself is still planned.)

### Fixed

- **Unreadable UI in OS "light" mode — the app now forces its dark theme.** The interface was designed
  dark (every dialog hardcodes dark backgrounds and light text), but the window shell and the plain
  controls followed the operating system's theme, so under a light OS theme the light-grey text landed
  on light backgrounds and became unreadable. The app now requests the **dark colour scheme**
  (`QStyleHints::setColorScheme`) at startup, so the native style renders dark regardless of the OS
  setting — while keeping the platform's own look (on Windows, the windows11 style: lighter-grey rounded
  controls and the accent left-bar on the selected row). One line in `main.cpp`; no per-widget changes.
  Requires **Qt 6.8+** (the CMake minimum moved from 6.5 to 6.8).

### VIRUSTOTAL report
  https://www.virustotal.com/gui/file/6d1b95c6dc68d94c9d7a8b4ea7a7c41f2135538d3ea4ab1bade091551cae7602
## [1.2.0] — 2026-08-02

Requires **libplatemaker 0.3.0** — this release adopts the lib's new `WorkspaceEditor` (profile
editing) and `ProcessingCallbacks` (per-input / per-slice render events), and renders the new
`FileStatus::Skipped` state. (The earlier About-dialog work needed only 0.2.2; the pin moved to 0.3.0
with the profile-editing adoption.)

### Added

- **"Add all files from directory" re-opens the last-used folder.** The dialog now starts at the
  project's last scanned directory (falling back to the folder of an existing input, then the platform
  default), instead of always at the default location — so re-scanning a folder or adding a sibling no
  longer means re-navigating the tree. This gives the project's `inputDirectory` field a defined use on
  the GUI side (it was written on every scan but never read back); the CLI already uses it to match a
  project by its directory
- **Live input tile status during a render.** Input tiles update in real time as the strip is built
  (phase 1), instead of only when the render finishes: a page turns green (Processed) the moment it is
  appended, cyan **"Processed (no canvas profile)"** when it is rendered without a matching canvas
  profile (see below), and violet **"Skipped"** only when it is missing or fails to load. Driven by the
  lib's new `ProcessingCallbacks::onInput`, re-emitted as a `RenderWorker` signal. These states survive
  the render (the model records them), instead of silently going green
- **Cyan "Processed (no canvas profile)" for implicitly-rendered inputs.** With libplatemaker 0.3.0 a
  page whose size matches no canvas profile is no longer dropped — it is rendered without margins. Such
  a page (and every page in a workspace that has no canvas profiles at all) now shows a **cyan** tile
  reading **"Processed (no canvas profile)"** instead of a plain green "Processed", so it is obvious
  which pages went through without a profile. Derived from the input's recorded profile
  (`InputFile::canvasProfileId` empty ⇒ none), so it persists across reopen; no new `FileStatus`. Cyan
  is deliberately distinct from the amber "Out of sync" state
- **Output tiles replace by position in real time.** During a re-render the output tiles are updated in
  place at their row, using the absolute slice index the lib now reports (`SliceSaved{sliceIndex,…}`).
  A format or slice-count change no longer leaves stale tiles lingering until the run finishes
- **Profile edits refresh every open project immediately.** Adding, editing or deleting a canvas or
  output profile now updates the output-profile combo and the assigned-canvas list of *all* open
  project docks at once (via a `MainWindow::workspaceProfilesChanged` signal → `Project::refreshProfileViews`),
  without needing to click "Refresh files"
- **About dialog shows the libvips version** and reports every linked component from the lib
  itself. libplatemaker, libvips and nlohmann/json now display the version and SPDX licence the
  library reports at runtime (`buildInfo()` / `linkedComponents()`), with libvips at the version of
  the DLL actually loaded
- **Clickable licences and project links in About.** Each component's licence opens a viewer with
  the full licence text (shipped in `credits/licenses/`), and each component name links to its
  GitHub project. Only `github.com` links are ever opened
- **Software Bill of Materials (`credits/sbom.spdx.json`).** The installer ships a flat SPDX 2.3 SBOM
  for the whole product — Platemaker, Qt and libplatemaker with its bundled dependencies (versions,
  SPDX licences, `pkg:github/...` purls) — merged from the lib's own SBOM. This is the machine-readable
  inventory required by the EU Cyber Resilience Act and commonly requested by commercial users; the
  licence texts beside it satisfy the LGPL requirement to distribute a copy of the licence

### Changed

- **Reordering inputs goes through the lib's `ProjectEditor`, and now actually affects the render.**
  Drag-and-drop and the ▲/▼ buttons call `Infrastructure::ProjectEditor::setInputOrder` / `moveInput`
  instead of writing `InputFile::order` by hand; the render feeds `ProjectItem::inputsInOrder()` to the
  pipeline so the strip is built in the reordered sequence. A reorder now marks the affected outputs
  **Out of sync** (via the lib's new input-composition staleness axis) — live at Refresh/Render and,
  because it is persisted, after a save→reopen — instead of being silently ignored. Requires
  libplatemaker 0.3.0.
- **New projects are created through `WorkspaceEditor::addProject`.** The GUI no longer mints the
  project uid itself (it used `makeUniqueId` directly) — identifier generation is the library's job.
- **All workspace-profile editing goes through the lib's `WorkspaceEditor`.** The GUI no longer mutates
  the workspace's profile vectors directly or re-implements the library's invariants (minting profile
  ids, deduplicating, preserving `templateInfo`, stripping presets, the project-link dimension guard).
  Manage/New/Edit for canvas and output profiles, profile↔project links, and per-project output-profile
  selection now call `WorkspaceEditor`, so an in-session edit is validated the same way a loaded file is.
  Template generation/deletion uses the lib's `setCanvasProfileTemplateInfo`
- **The GUI no longer asserts versions or licences about code it does not own.** Compile-time
  licence definitions are kept only for Platemaker itself and Qt; libplatemaker and its dependencies
  are sourced from the lib, so they cannot silently go stale when a dependency is swapped or
  relicensed

### Fixed

- **Output tiles no longer show the ▲/▼ reorder buttons.** Output order is derived from the render and
  cannot be changed; the buttons did nothing there (their move signals are wired only for input tiles)
  and implied outputs could be reordered. They are now hidden on output tiles (the space is reclaimed).
- **Input tile ▲/▼ reorder buttons work again.** The move-up/-down arrows on an input tile did
  nothing (only drag-and-drop reordered). The tile marked its display areas
  `WA_TransparentForMouseEvents` so clicks fall through to start a drag, but the set included the
  buttons' ancestor container — and Qt hit-testing skips a transparent widget's whole subtree, so the
  buttons never received the click. Now only the leaf display widgets are transparent; the buttons are
  clickable and drag still starts from anywhere on the tile (the plain container widgets ignore the
  press and it propagates to the list)
- **A project with skipped pages no longer re-renders on every open, and skipped tiles survive a
  reopen.** Reopening a project whose outputs were all Done used to always trigger a render, and the
  violet "Skipped" input tiles reverted to green/grey — because the lib's `sanitize()` recomputed input
  status from disk and did not know "skipped". Fixed in libplatemaker 0.3.0 (`sanitize()` now keeps
  `Skipped` sticky for unchanged files and treats it as a settled, terminal state); requires the updated
  `libplatemaker.dll`
- **"Refresh files" no longer resets input statuses.** It now refreshes only the *output* files against
  disk (and the output-profile staleness overlay); input statuses are owned by the last render, so a
  page the render marked **Skipped** (or Processed) survives a refresh. Previously, changing the output
  profile and clicking "Refresh files" reverted inputs to their pre-render state, because the shared
  `sanitize()` re-derived input status from disk (which has no notion of "skipped")

## [1.1.0] — 2026-07-20

Requires **libplatemaker 0.2.1** — the minimum is now enforced at configure time instead of
surfacing as compile errors.

### Added

- **Workspace repair notice** — when two profiles are found sharing an internal
  identifier, one is given a new one and a dialog explains what changed and why some
  projects may now need a refresh
- **Output profile presets are marked and read-only** — "Webtoon Standard" is labelled
  *(preset)* in Manage output profiles, with Edit and Delete disabled. A preset is shared
  by every workspace, so it has to mean the same thing everywhere; Duplicate makes an
  ordinary copy you can change freely

### Changed

_none_

### Fixed

- **Projects under a path with non-ASCII characters now work** (fixed in libplatemaker
  0.2.1). Two symptoms, one cause: inputs stayed amber after a successful render, so every
  render redid all the work and overwrote the output; and a workspace could be saved but not
  reopened — which looked like a Google Drive restriction, because Drive creates a localised
  folder name such as the Polish "Mój dysk"
- **A canvas profile could not be assigned to a project** — profiles created together in
  one pass of the manage dialog were given the same internal identifier, which made all
  but the first count as "already assigned" and disappear from the assign list. Identifiers
  are now random and checked for uniqueness, and existing workspaces are repaired on open
- **New workspaces no longer define "Webtoon Standard" themselves** — the profile was
  described here *and* in the CLI, matching only by coincidence. Both now take it from
  libplatemaker, so a workspace created in either place is identical

## [1.0.1] — 2026-07-17

Requires **libplatemaker 0.2.0** — the minimum is now enforced at configure time instead of
surfacing as compile errors.

### Added

- **Batch render — "Refresh all projects" (F6)** — sweeps the workspace one project at a
  time, skipping the ones already up to date, and reports a summary of rendered / skipped /
  failed. Config changes are confirmed once for the whole sweep, not per project
- **Out-of-sync warning on workspace open** — explains, in one dialog, why tiles are amber
  and offers to refresh straight away
- **About dialog** — component table listing version and license for Platemaker,
  libplatemaker, Qt, libvips and nlohmann/json

### Changed

- Output tiles update **live** as each slice is written, instead of only after the render
  finished
- Render log records each project's outcome (finished / cancelled / failed with the reason),
  so a batch leaves a readable trail rather than transient status text

### Fixed

- **Cancelling a render no longer discards the slices it already wrote** — they were on disk
  but the project did not record them, so the next render redid the work
- **Canvas profile edits are now visible** — pages whose profile changed are marked amber
  ("out of sync") instead of silently reporting as up to date
- **Output tiles no longer duplicate during a re-render** — each slice refreshed its own tile
  instead of appending a second one
- **`FetchContent` could not find the release** — the download succeeded; the failure was in
  the path handling afterwards, which aborted the configure as if the URL were wrong
- Empty band above the About text when the application icon fails to load
- `clazy` warning: range-loop over a non-const Qt container could detach it

## [1.0.0] — initial release
