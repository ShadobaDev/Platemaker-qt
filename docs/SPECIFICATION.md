# Platemaker GUI — Application Specification

**Status:** Active development — core widget layer done, business logic wiring in progress.  
**Last updated:** 2026-09-05  
**Audience:** Developer + AI coding assistant

> **Library reference:** The GUI is a thin shell over `libplatemaker`.  
> For domain logic (pipeline, data models, serialisation, profile matching) see  
> `../PlateMaker/docs/SPECIFICATION.md`.

---

## 1. Overview

**Platemaker GUI** is a cross-platform Qt 6 desktop application for comic artists.
It exposes all capabilities of `libplatemaker` through a visual interface:

- Managing workspaces (`.platemaker.json` files)
- Managing projects (chapters) within a workspace
- Visualising source image sets as scrollable tile grids
- Running the strip-processing pipeline with live progress
- Managing canvas profiles and generating margin-overlay templates
- Managing output profiles (format, slice size, JPEG options)
- Authoring the pipeline's two **optional** steps on a continuous strip of the chapter — a project-wide
  colour grade and text/bubble overlays (§2.5)

The application follows a strict separation of concerns:

```
Qt GUI layer  (this project)
  └── views / widgets / dialogs  — all Qt types, QWidget-derived
  └── background workers          — QtConcurrent::run() wrappers
  └── no domain logic             — domain lives in libplatemaker

libplatemaker  (shared library)
  └── WorkspaceSerializer, CanvasProfileMatcher, CanvasProfileMatcher
  └── Scaler, ScaledStrip, Slicer, MarginCropper, TemplateGenerator
  └── ThumbnailCache, ImageIO, CancellationToken
```

---

## 2. Main Window Architecture

### 2.1 Shell — `MainWindow`

Dock-based host window.  Holds:

| Area | Widget | Purpose |
|---|---|---|
| Workspace panel | Left dock | Lists projects in the open workspace, profile buttons |
| Project docks | `QDockWidget` per open project | Each holds a `Project` widget, tabbed with the workspace panel (opened via `openProjectDock()`) |
| Bottom dock | Log / progress | Pipeline output, progress bar, cancel button |
| Menu bar | File / Workspace / Project / Process / Help | All application actions |
| Status bar | — | Current workspace path + dirty indicator |

One `MainWindow` instance = one open workspace.  Switching workspace closes the
current one (with save prompt) and re-opens with the new file.

Every dock — Workspace, project, strip **and Action** — wears the same **custom title bar**, the
reusable `DockTitleBar` widget (`widgets/docktitlebar/`), installed via
`MainWindow::installDockTitleBar()` (`setTitleBarWidget`): a live title label plus **minimise**,
**maximise** and **close** buttons styled after the native window controls. `DockTitleBar` handles
**maximise ⇄ restore** (fill the screen) itself — identical for every dock — and leaves the
dock-specific actions to the owner via signals: **minimise** toggles dock ⇄ detach (docking tabs it
beside the Workspace, except the Workspace anchor and the Right-only Action column, which just re-dock),
**close** routes to `closeDock()` (Workspace / Action / strip hide — reopened via the *Show … panel*
menu items / *View strip*; a project dock is destroyed). Qt hides a dock's title bar while it is
tabified, so those buttons show only while a dock floats or is docked alone; a tabified dock is detached
by double-clicking its tab. The Action dock keeps its fixed-width grip content underneath the bar. See
§2.5 for the shared dock tab-bar handling.

### 2.2 Project View — `Project`

A dock widget, tabbed alongside the workspace panel (one dock per open project).  Represents one
`ProjectItem` — created lazily by `MainWindow::openProjectDock()`, holding a `Workspace&` reference.

- Scrollable grid of `ImageTile` widgets, one per `InputFile` in the project.
- Tiles are ordered by `InputFile::order`, which is also the sequence the render builds the strip in
  (`ProjectItem::inputsInOrder()`).
- Drag-and-drop and the ▲/▼ buttons reorder through `Infrastructure::ProjectEditor`
  (`setInputOrder` / `moveInput`), which rewrites only the `order` field — the stored input vector is
  never physically moved. A reorder marks the affected outputs out of sync via the lib's
  input-composition staleness axis (`detectInputCompositionChange`, surfaced by `sanitize()` at the
  next Refresh/Render/reopen), and marks the workspace dirty.
- A toolbar row above the grid shows: project name, input directory, link/unlink
  canvas profile button, output profile selector.
- A **Workflow** tab renders the pipeline as a read-only map of `StageCard`s
  (`widgets/stagecard/`), built from the live `ProjectItem`: Inputs → Margin crop → **Colour
  correction** → Resize → Slice → **Text & bubbles** → Output. The two optional stages draw as smaller,
  greyed cards with a green **+** on hover; activating one opens the strip editor (§2.5). It is an
  honest map of a **fixed, non-reorderable** pipeline — deliberately not a node graph, because the
  library's steps are typed and ordered by design, and a draggable canvas would imply a freedom that
  does not exist.

### 2.3 Image Tile — `ImageTile`

Single card in the project grid.  Shows:

- Thumbnail (loaded asynchronously via `ThumbnailCache + QtConcurrent`)
- Filename (short)
- Processing status badge with a colour-coded left border, keyed on `FileStatus` plus, for a Processed
  input, whether a canvas profile was applied:
  Pending (grey) / Processed / Done (green) / Modified (orange) / Missing (red) /
  Desynchronized "Out of sync" (amber) / **Skipped (violet)** — the render did not include this page
  (missing / load error) / **"Processed (no canvas profile)" (cyan)** — the page was rendered
  implicitly, without a matching canvas profile (no margins). The cyan state is not a `FileStatus`: it
  is derived from `InputFile::canvasProfileId` being empty on a Processed input, so it persists across
  reopen
- Contribution indicator (which output slices this file feeds into)

`ImageTile::setStatus(FileStatus, bool renderedWithoutProfile)` repaints only the badge + border (no
thumbnail reload), so the tile can be updated live during a render (see §4.4) without re-running the
async thumbnail load. The `renderedWithoutProfile` flag drives the cyan "Processed (no canvas profile)"
state.

### 2.4 Profile Dialogs

| Dialog | Library type | Trigger |
|---|---|---|
| `CanvasProfileDialog` | `CanvasProfile` | Add / Edit in ManageCanvasProfilesDialog |
| `OutputProfileDialog` | `OutputProfile` | Add / Edit in ManageOutputProfilesDialog |
| `ManageCanvasProfilesDialog` | `Workspace::canvasProfiles()` | "Canvas Profiles…" action |
| `ManageOutputProfilesDialog` | `Workspace::outputProfiles()` | "Output Profiles…" action |

`ManageCanvasProfilesDialog` also emits `generateTemplatesRequested(QList<CanvasProfile>)`
when the user requests template PNG generation.

The dialogs edit **copies**; the workspace is mutated only on accept, and only through the lib's
`Infrastructure::WorkspaceEditor` (the palettes are private in the model — see the lib spec §7.5). The
GUI does not mint ids, deduplicate, preserve `templateInfo`, or strip presets itself; the editor does.

### 2.5 Strip Editor — `StripEdit::Editor`

A per-project **floating dock** (`widgets/stripeditor/`, a `.ui`-defined `QWidget` inside a
`QDockWidget`), opened from the Output tab's *View strip* button or from a Workflow-map card, via
`MainWindow::openStripEditorDock()`. It is the authoring surface for the pipeline's two optional steps —
colour correction and text/bubble overlays — and it shows the chapter as one continuous strip.

**The strip is built from the project's INPUT pages, not from its rendered output.** It stacks each
input put through the library's page domain (EXIF-upright → canvas-profile margin crop → scale to the
output's target width) via `ProcessingPipeline::layoutPagesFromHeaders()` / `decodePageToRgba()`. Three
consequences, and they are the whole reason for the design:

- **It works before the first render.** A grade or a bubble has to be authored before it is baked, and
  on the old output-slice feed there was nothing to look at until a render existed.
- **Rendering does not change the view.** Edits are previewed against the *input*, so the render bakes
  exactly what was on screen. On the old feed the render baked the grade into the output and the preview
  then graded it a second time.
- **The unit of work is the page**, which is the unit the grade's per-page exclusions and an overlay's
  page anchor both address. An output slice can straddle two pages, so on that feed neither could be
  honoured at display time at all.

Output *slices* are deliberately absent: they are a publishing artifact. What the author still needs is
the slice **grid**, which the seam guides draw at every `sliceHeight` down the strip — the line a bubble
must not be split by.

- **Placement.** A `QDockWidget` allowed Left/Top/Bottom and **never** tab-combined with the Action right
  column; it defaults to floating and wears the shared custom title bar (§2.1). It opens sized to the
  **strip width + 100px each side** (× 80% of the screen height), centred, and is tracked in
  `m_openStripDocks` (keyed by a `projectIndex` property), reindexed/closed with its project.
  `MainWindow::refreshStripEditor()` re-feeds it on every `Project::projectModified`.
- **Shared dock tab bar.** The Workspace, project and strip docks can occupy one tab group, so their tab
  bar's close / double-click are resolved by `dockForTabBarTab()` (tab → dock **by window title**) rather
  than a raw index into any one list. `wireDockTabBars()` (re)applies this whenever a dock joins a group.

#### 2.5.1 Editor shell

`editor.ui` lays out `[toolbar]` over `[toolColumn | graphicsView | rightPanel]`, with the two side
columns each split vertically:

```
toolColumn = QSplitter(V): [ tool tiles | TOOL OPTIONS ]      what the NEXT object will be
rightPanel = QSplitter(V): [ OBJECT PROPERTIES | object stack ] what THIS object is
```

**Each side answers one question, and only that one** — and they are two classes, not one class in two
places. `ToolOptionsPanel` is the tool's options: what the *next* object will be, editing nothing and
emitting nothing. `ObjectStatePanel` is the selection's properties. A single class answering both would mean two
things depending on state the artist cannot see, which is what makes a preset picker above shared
controls ambiguous.

Both own a **`PropertyGroupSet`** — the shape, skin, style and text editors, plus the only two rules about
applying them: shape is written before tails (a tails editor reads it), and the style seed is topped up
afterwards, since it belongs to no group. Written once, because both are the kind of rule that drifts
when written twice. **Tails are not in the set**, because the two panels edit different things: the next
balloon has one tail to set up (`TailsEditor`), an existing balloon has a list whose members are objects
of their own (`TailListEditor`, `TailEditor`). Each panel hands its own to `collect()`, which keeps the
order.

**`PresetStore`** is a model, not a panel's field. The tool's options pick a preset for the next
object; an object's context menu applies one to what is selected (*Apply preset ▸*); a dialog will
eventually import and export them. A model reachable only by going through a widget cannot be reached, so
`Editor` owns the store and hands a reference to each. It raises no dialogs: reading and writing a pack
report what happened and leave the telling to whoever asked. `PresetStore::applied()` is the one place
that knows what a preset does *not* carry — the lettering, the box, the tails and the style seed all
survive being restyled.

`ObjectStatePanel` shows **one `CollapsibleSection` per group**, in a fixed order, and only the sections
the selected object's kind has. Qt has no collapsible container — a checkable `QGroupBox` puts a checkbox
in the title that means *enabled* — so `widgets/collapsiblesection/` is a disclosure arrow over a content
widget. Which sections are open is a working preference and lives in `QSettings`. With nothing selected
the panel shows one line saying so and no sections at all.

The **grade** lives in tool options rather than on the right: its subject is the project, so it is the
tool's own configuration and not any object's property.

**The right column is never hidden and never disabled**, under any tool. Hiding it lets the canvas grow
into the space, which makes the strip jump sideways on every tool change; disabling it costs the artist
the only place the strip's contents can be seen and reordered. A tool changes what is in the column's
*tool-facing* half, never whether the column is there.

Splitter positions are remembered in `QSettings` — a working preference that follows the artist rather
than the comic.

- **Tool rail** (left) — square checkable `QToolButton`s in an exclusive `QButtonGroup`, laid out by
  `FlowLayout` so they reflow to the rail's width (a flow layout cannot be expressed in a `.ui`).
  Tools: **Select** (default — a drag on the bare strip rubber-bands what it covers), **Pan** (a drag
  scrolls), **Grade**, **Bubble**, **Text**, **Caption box**, **Colour**, **Eyedropper**. `dragMode` is a
  single property, which is why Select and Pan are two rows rather than one tool: the rubber band and the
  hand both want the left button on the bare strip. Both only act on a press no item took, so dragging an
  object moves it under either. **The middle button scrolls under every tool** — implemented here rather
  than by the view, because Qt's hand-drag is the left button's. A tool decides what a *placement* creates; selecting, moving and resizing what is
  already there is available under every one of them (§2.5.4).
- **The rail is built from a table, and a tool is a record.** `StripEdit::tools()` holds one `Tool` per
  rail entry — id, icon, tooltip, `ToolKind` (`Select`, `Create`, `Grade`), the shape a `Create` tool
  places, and which options page it shows — and the rail, the options stack, the drag mode and the cursor
  are all read off it. **Adding a tool is a row there and nothing else.** A tool is stateless, so it needs
  no object and no class per button; the record gains a hook the day a tool needs behaviour of its own.
  - A `Create` tool that fixes its shape (Text places none, Caption places a caption) says so in its row,
    and `ToolOptionsPanel::prototype()` applies it over the artist's pick without disturbing it — which
    is why switching Text → Bubble brings back the shape chosen before. It replaced two gates that each
    existed to say *this tool makes a shapeless object*, one in the panel and one in `ObjectController`,
    and the controller now knows nothing about which tool is armed.
  - A tool that places one shape carries **no icon file**: its button is drawn by the rasteriser that
    draws that shape, so it cannot misrepresent what pressing it gives you.
- **The pointer is decided in one place.** A cursor is a function of *(active tool, what is under the
  pointer)*, evaluated by `StripEdit::cursorFor()`; nothing else writes the viewport cursor. The tool row
  carries the data half — `cursor` over the bare strip, `cursorOnObject` over an object — so *Pan* shows a
  hand and a move cross, the authoring tools a crosshair, and a new tool still costs one row.
  - **The two resize corners and a tail tip are the canvas's**, whatever the tool: every object is movable
    and resizable under every one of them. Because the cursor promises that, the colour tool lets a press
    on a grip through to the canvas rather than painting.
  - **The view's drag mode still writes a cursor of its own** (`ScrollHandDrag`, under Pan). Rather than
    taking it away — its documented rule, *"only affects mouse clicks that are not handled by any item"*,
    is worth keeping — `cursorFor()` answers the same open hand in that state, so the two agree instead of
    overwriting each other. `setTool()` therefore sets the drag mode **first** and decides the cursor
    after; reversing those two lines brings the flicker back.
  - It is re-decided on hover (only while no button is held), when the pointer enters the canvas, after a
    release, on a tool change, after a zoom and after a feed — the last two because they move the scene
    under a pointer that has not moved. **The viewport has mouse tracking on** for exactly this reason: a
    widget hears about the mouse only while a button is held otherwise, which would leave the cursor
    following the last *click* rather than the pointer. The re-decision after a **release** is queued:
    under `ScrollHandDrag` the view restores an open hand on every left release, even one an item took,
    and its handler runs after the filter.
- **The colour pair is furniture** — `ColourPair`, under the tiles in the tool column, where it stays
  whichever tool is active. It is drawn the way every drawing application draws one: two **overlapping**
  swatches, primary in front, with *swap* and *reset to black and white* beside them, inside a panel of
  its own. Two swatches side by side in a rail read as two more tool tiles; the overlap and the frame say
  *one control, and not a tool*. They are real buttons rather than painted regions, so the theme's hover
  and focus states and keyboard reach come for free, and the swatches show a chequer under a colour that
  is not opaque. It is not on the tool-options page, because that page swaps with the tool and
  furniture that vanishes is not furniture. The tools that use it hold a *reference* to it rather than a
  colour of their own, which is what keeps them stateless. It is independent of the colours in ③ —
  setting a balloon's fill does not touch the pair, and changing the pair does not touch any object — and
  it persists in `QSettings`, because it is a habit of the artist rather than a property of one comic.
- **The colour tool** (`ToolKind::Apply`) spends the pair on what a press lands on: **left** with the
  primary, **Shift+left** with the secondary. `X` swaps the pair and `D` resets it to black and white,
  both scoped to the canvas so typing an *x* into a balloon stays typing an *x*.
  - **What changes is what is under the pointer** — the lettering, the outline band, or the fill —
    answered by `artifactPartAt()` off the same two paths the scene draws (`artifactSilhouette()`,
    `artifactTextOutline()`), topmost first. A few screen pixels of slack make a hairline outline and a
    thin letter hittable, converted through the zoom so they do not swallow the fill when magnified. A
    press inside a balloon's box but outside its silhouette paints nothing; imported artwork takes
    nothing. One undo step per press, named after what it painted.
  - **The tool therefore has no "what am I painting" setting**: the picture is the setting, and the two
    axes — which colour, which property — stay separate, which the first version did not manage.
  - **The right button stays the context menu's.** Only the eyedropper takes it, and only because
    sampling two colours is what a pair is for.
  - **The press selects what it painted**, so ③ shows the change that just happened rather than leaving
    an undo step describing something invisible.
  - Its **other face needed no code**: editing the selection's colours is `SkinEditor` in ③, which was
    already there because ③ follows the selection and never the tool. A tool's "editor face" is a tool
    naming a property group some panel already shows.
- **The eyedropper** (`ToolKind::Sample`) takes what is **drawn** at the press — one composited pixel of
  the scene: the page through its grade, with every balloon, caption and imported asset over it, each
  with its own blend mode. Sampling the page pixmap alone was defensible and still wrong: clicking a
  balloon gave the paper behind it, which is not what the artist sees. **Left fills the primary half,
  right the secondary** (Ctrl+left does the same as right), and the right button opens no context menu
  while the tool is active, because it belongs to the tool.
  - **Chrome is not part of the picture.** Selection boxes, grips, tail handles and the seam guides are
    hidden for that one repaint — `Object::setChromeVisible()` — so sampling a *selected* balloon gives
    its fill rather than the highlight colour.
  - A page still showing its blurry proxy is **not** sampled: the page is requested instead, because a
    stand-in would answer with an average of the colours around the point rather than the colour at it.
  - The press is consumed, so sampling never changes the selection.
- **Tool options** (bottom-left, under the rail) — a `QStackedWidget`, one page per *page* rather than
  per tool: `GradePanel` for Grade, `ToolOptionsPanel` for every tool that authors a `TextArtifact`
  (Bubble, Text, Caption), because they author the same object (§2.5.4) and two copies of those controls
  would drift. A tool with no options gets an empty page.
- **Artifact list** (right-bottom) — `artifactList`, the overlays as a **stack**: row 0 is the front-most
  object, and a row covers every row below it wherever they overlap. The library's composite order is the
  opposite (it draws `stripOverlays` in vector order, so the last element is on top), so the list is that
  vector **reversed** — done in `refreshList()` / `commitListOrder()` alone, because a layers panel that
  reads bottom-up is a surprise in every tool that has one, and the reversal belongs at the view rather
  than in a model the render shares.

#### 2.5.2 Rendering (seam-free) and memory

`QGraphicsView` + `QGraphicsScene`. The scene's coordinates **are** strip coordinates, 1:1 — which is
what lets an overlay's scene position be its library placement plus its anchor page's top, with no
mapping layer.

- **One item for the pages.** `StripItem` draws every page as its own image in one pass. One
  `QGraphicsPixmapItem` per page would leave a 1px hairline at each join — `QGraphicsView` clips and
  rounds each item's edge independently — but the actual cause is **edge antialiasing**: with AA on,
  each `drawPixmap` coverage-antialiases the destination rect's edges at fractional zoom, so the boundary
  row is only partly covered and the background hairlines through. `StripItem::paint` **disables
  `QPainter::Antialiasing`** (keeping `SmoothPixmapTransform`), so adjacent pages tile with hard edges.
  Overlays are separate items above it (§2.5.4) — they are sparse, so they cost no seam.
- **Layout without pixels.** `layoutPagesFromHeaders()` reads each page's header and decodes nothing; pages the
  render would skip (missing / unreadable) are dropped here exactly as the render drops them, or every
  page below would sit at the wrong strip offset.
- **Two tiers, both off the UI thread** (§6):
  - **Proxy** — the input page's thumbnail from the lib `ThumbnailCache` the Input tab already warms
    (reused, not reinvented), drawn instantly so a page is never blank.
  - **Sharp** — `decodePageToRgba()` on `QtConcurrent` for pages in view plus a one-page prefetch margin,
    into a byte-capped LRU `QCache`. Off-screen pages are evicted, so RAM tracks the viewport, not the
    chapter. A generation counter drops async results from a superseded rebuild.
- **Zoom.** 100% default, plus fit-width / 100% / − / + and Ctrl+wheel; the default re-settles on resize
  until the user takes control.

#### 2.5.3 Colour correction (Grade tool)

`GradePanel` edits `Models::ColourCorrection` — brightness / contrast / saturation, with curves and the
per-page exclusion UI still to come. It emits `changed()` continuously (live preview) and `committed()`
debounced (persist + one undo step), which `MainWindow` routes to `Project::applyColourCorrection()`.

The preview grades the **resident, ungraded page pixels** in place with
`Core::ColourCorrector::applyToRgba()` — the same engine the render uses, so the preview is not an
approximation. Because the colour step never influences how a page is *read*, no grade edit can
invalidate a built page: re-grading what is resident is always enough, and a page fetched once stays a
valid baseline for every grade tried on it. Excluded pages are skipped, matching the render.

#### 2.5.4 Text & bubbles (Bubble / Text tools)

- **One object, two tools.** A bubble is a `TextArtifact` (`widgets/textartifact/`): shape, box, tails,
  text, font, colours, line style. The Text tool is the same object with `shape == None`. Ten shapes;
  each brings the rectangle its text may occupy (`textSafeArea()`), which is the half that takes the
  thought — a path is a few lines, knowing where words fit inside it is what stops them crossing a
  stroke.
- **Tails are a list.** Each is a tip (in balloon coordinates, and free to fall *outside* the balloon), a
  base width and a bend. The base is found by casting a ray from the balloon's centre and binary-searching
  for where it crosses the silhouette with `QPainterPath::contains()` — shape-agnostic, so every shape
  grew a working tail for free and a tail may leave any edge. `artifactBounds()` therefore computes what
  the artifact actually covers; `box` is the balloon alone.
- **Each tail is an object of its own.** It shares nothing with its siblings: its width and bend are its
  own, and *Add tail* only seeds a new tail's starting values from the last one. It is selected in the
  object tree or by pressing its handle on the strip, which leaves the bubble selected there — its handles
  exist only on a selected bubble — but hides its box and corner grips, which stop resizing while a tail is
  the subject; the selected tail's handle is drawn hollow. ③ then shows only **Tail** (width, bend) and
  *Delete tail*, which removes that tail and selects its bubble. A press anywhere else on the bubble selects
  the bubble again.
  - **A tail is addressed by position**, `ObjectController::selectedTail()`, not by an id: undo restores
    whole states and holds no tail by number. A feed that changes how many tails the bubble has cannot say
    which one went, so the selection moves up to the bubble.
- **A project has one history, and it belongs to no window.** `MainWindow` owns it, keyed by
  `ProjectItem::uid` and alive for the session; the `QUndoGroup` makes it the active stack while either
  of that project's docks — the project dock or its strip editor — is in front. One rather than one per
  window because both edit the same document in the same train of thought: with a history per window,
  Ctrl+Z reaches past the last thing done to undo the one before it, which is a stranger thing to
  explain than an undo whose effect is elsewhere.
  - **The answer to "undo did something I cannot see" is to show it, not to split the history.**
    `Project::historyStepApplied(EditScope, uids)` is emitted by the restores only — an edit is already
    on screen where it was made — and names both the dock that shows the step and the objects it moved.
    `showDockAttention()` raises and focuses that dock, outlining it for about a second and a half
    **only when it was not already the focused dock**; a flash on the window being looked at teaches
    nothing and would cost the effect its meaning.
    - **The objects are named by diffing the two overlay states**, not tracked per operation, so every
      kind of edit reports what it moved without any of them being taught to. Artifacts are compared
      alongside placements: a text edit rewrites a bubble's asset, and reading that as "changed"
      through the placement alone would rest on the hash having settled.
    - The signal is emitted **before** the state reaches the views: it arms `Editor::selectAfterFeed()`,
      and the feed that follows is what consumes the arming — the objects do not exist in the editor
      until that feed builds them. The first named object still standing is selected and scrolled into
      view in the canvas and the object stack; a step that removed everything it touched clears the
      selection instead of pointing at a ghost. `ensureVisible()` moves nothing that is already
      visible, so an undo of what is in front of you stays perfectly still.
  - **A step still reaches only into its own half of the document**, which is what keeps restores cheap
    and stops two steps treading on each other. `fullSnapshot()` carries the library's project snapshot
    and no authoring records; `applyProjectSnapshot()` lifts the overlays out and puts them back around
    the restore, because the library's snapshot carries them as part of the project. `OverlayState` —
    `stripOverlays` plus the authoring records — is the other half, typed rather than serialised, and
    the two halves of *it* are never split from each other: an overlay's record says which file renders,
    its artifact says what that file contains.
  - **Which half a step covers is decided by what the operation changes**, not by which window triggered
    it: *Clear text & bubbles* is a button on the project dock and records an overlay step.
  - **Closing a dock keeps everything.** A project dock hides rather than being destroyed, because an
    open strip editor goes on sending it edits that need its artifacts, its overlay directory and its
    history. Removing the project drops its history, and so does closing the workspace; both snapshot
    commands hold a `QPointer` so one that outlives its project does nothing rather than reaching into
    freed memory.
- **Advisories: the application says what is set up wrongly, and offers the way out.** `Advisories`
  (owned by `MainWindow`) holds every standing advisory, keyed `"<condition>/<project uid>"` — by uid
  and never by index, since removing a project shifts every index after it. An `Advisory` carries a
  level, four words for its badge, the sentence behind it, and **the action that fixes it**: *"grade not
  run"* is an annoyance, *"grade not run — turn the step on"* is help.
  - **A condition is derived, so it is re-evaluated and never remembered.** `refreshAdvisoriesFor()`
    recomputes everything this window can observe about one project and raises or withdraws to match,
    called from anything that changes a project rather than from each place that could make one true —
    the arrangement where a condition is eventually forgotten and a badge outlives its cause. Re-raising
    an unchanged advisory is silent, which is what makes calling it on every edit affordable.
  - **A thing that merely happened is not an advisory.** *"Converted 2 texts to bubbles"* can never stop
    being true, so nothing could withdraw it; those belong in the status bar's temporary message, which
    expires by itself and needs no registry.
  - **One condition today:** *N objects unanchored* (Error — an overlay whose anchor names no page, or
    names a page not in the input list; its action opens the strip editor with them picked out, and its
    resolution deletes them).
  - **An advisory carries a way to look and, separately, a way to resolve.** The action is one click on
    a chip. The resolution may destroy work, so it is only ever offered inside a question — never on a
    chip.
  - **The render gate is one rule: a render stops and asks while any Error stands for the chapter.**
    Every button is an advisory's — each one's action (which ends the render) and resolution (which
    renders afterwards) — plus *Render anyway* and *Cancel*, which is the default. A resolution returns
    `RenderGate::Again`, which re-runs `startRender()` from the top: deleting objects changed the chapter
    every earlier derivation was made from. The gate sits after "up to date" and before the settings
    question, and a batch does not ask — it skips the chapter and the summary says why.
  - **The surface is a widget, `AdvisoryBar`, and there are two hosts.** `MainWindow` keeps one in the
    status bar; the strip editor keeps one along its own bottom edge, live **only while its dock is
    floating** — a floating dock is a top-level window with no status bar, and maximised it covers the
    one behind it, so the advisories would be missing from the window where the work is done. The rule
    is the owner's: `MainWindow` connects `QDockWidget::topLevelChanged` to
    `Editor::setAdvisoriesActive()`, because a widget cannot know what other surfaces its window has.
  - **One chapter at a time** — the one whose dock was last raised, or failing that the one selected in
    the project list. Chips are ordered worst first; one whose advisory carries an action is clickable
    and says so with a pointing-hand cursor. The bar is exactly as wide as its chips, so where it sits
    is the host's to state — bottom right in both, the corner permanent status items live in. A bar with
    nothing to say takes no room at all, margins included.
- **A page is its uid, not its file, and removing one says what it strands.**
  - **Replace file…** on an input tile swaps the file and keeps the uid, so nothing anchored to the page
    unanchors. The swap is `ProjectEditor::replaceInputFile()`, the library's, because the project's
    lookup tables are keyed by path. A file that is already another page is refused — case-insensitively,
    the way adding files de-duplicates — since a rescan matches by path and would fold the two.
  - **Removing inputs goes through one `Project::removeInputs()`**, for *Clear* and *Remove* alike. When
    objects are anchored to a page being removed, the confirmation says how many and offers *Remove, keep
    them* (the default) or *Remove with the objects*; the latter is **one undo step** — a `QUndoStack`
    macro around the project step and the overlay step.
  - **Re-anchor to ▸** on the selection lists every page as `p.NN — file name`, current one checked, and
    changes only the anchor: the object keeps its offset from the page top. Pages are listed, never
    pre-chosen, because a guess could only go on position and position is what a deletion shifts.
- **Badges are one widget, shared.** `widgets/badge/` owns the rounded chip: a `Badge` is a label, a
  required fill and three colours derived from it (border, gradient, label), plus the sentence behind
  it. `paintBadge()` draws one, `layOutBadges()` a run of them, and `makeBadge()` hands one out as a
  widget — an item delegate cannot give out widgets and a status bar cannot host a delegate, so what
  they share is the description and the painter rather than the surface.
  - **`layOutBadges()` paints or measures through one code path** (a null painter measures), which is
    how `QStyledItemDelegate::helpEvent()` answers the tooltip for the chip under the cursor: a painted
    badge is not a widget, and a second copy of the layout arithmetic would drift from the first and
    put the wrong sentence on a chip.
  - **The four tones — Info, Warning, Error and neutral — are named in exactly one place**, and their
    lightness is derived against `QPalette::Base` so the hue carries the meaning and the theme carries
    the rest. The label is black or white by the fill's perceived luminance rather than a fixed dark
    grey, which was correct only while every chip stayed light.
- **A colour grade is its own switch.** The library dropped `ColourCorrection::enabled`, so *graded*
  means *not neutral* — `Models::isNeutral()` is the one test the render, the staleness signature and
  this preview all gate on, which is what makes the strip editor WYSIWYG by construction rather than by
  two predicates happening to agree.
  - **The workflow card reads the values**, like the *Text & bubbles* card next to it reads the overlay
    count: active because there is something in it, no in-place *+* (a grade is made in the editor),
    and **−** removes what is there: every adjustment back to neutral, recorded as *Remove colour
    correction*. The page exclusions stay — they are each page's decision, and apply again once the strip
    is graded again.
  - **The Grade tool is an image editor's colour menu, applied to the selected object.** Its ④ page lists the
    adjustments — *Brightness & contrast*, *Saturation* — with the applied ones in bold, and the chosen
    one's controls below, live, settling into one undo step named *Adjust …*; *Reset* takes that
    adjustment off (*Reset …*) and leaves the others. Curves are run by the library but have no editor
    here, so they are not listed. The panel edits only the shown adjustment's fields and switches nothing.
  - **It can act on the strip, and on nothing else yet** (a page's only colour decision is its exclusion).
    With a page, an overlay or nothing selected the list and controls are disabled and the panel says
    why, with *Select the strip*. Picking the Grade tool with **nothing** selected selects the strip, as
    an image editor always has an active layer for a colour tool; a selection the artist made is left alone.
  - **`ColourAdjustment` is the one mapping** between the names an artist knows and `ColourCorrection`'s fields, used by
    both panels: which adjustments exist, whether one is applied, its values, and the grade without it.
    Neutral is read from a default-constructed `ColourCorrection` rather than restated, and a GUI test
    pins that removing one adjustment touches nothing else and that the per-adjustment rules add up to
    the library's `isNeutral()`.
  - **The undo step is named by whoever made the edit.** `Editor::colourCorrectionEdited(cc, undoText)`
    carries the name to `Project::applyColourCorrection(cc, undoText)`, which records it as given.
- **The selection is a set with a primary.** `ObjectController` holds the selected overlays in the order
  they were picked; the **last** is the primary, which is what `selectedOverlay()` returns and what every
  single-subject path still reads. The canvas gathers a set with Ctrl+click — the scene always
  multi-selected, the controller used to take the first item and drop the rest — and the tree gathers one
  with Ctrl and Shift (`ExtendedSelection`).
  - **The strip, a page and a tail stay single.** Picking one collapses the set: there is one strip, a
    page is a row of it, and a tail belongs to one balloon.
  - **③ binds the whole set** through `setArtifacts()`: the sections shown are those at least one
    selected object carries, and a control whose value differs across the selection says **Mixed** rather
    than showing the first object's. Every control kind has a way to say it — a chequered swatch, a spin
    box using the value below its minimum with Qt's `specialValueText`, a combo with no current entry and
    a *Mixed* placeholder, and a tri-state check box — so fill, outline, stroke width, line style, its
    amount, font, size, bold, alignment and the lettering's colour are all editable across a selection.
  - **Shape, the tails and the lettering are absent for a set**: giving several objects one shape is a
    conversion, adding a tail to five balloons is five objects, and five balloons do not share one line.
  - **A role is which objects a group is bound to.** The skin editor is bound to the shaped objects only,
    so a caption with no balloon neither votes *Mixed* with a fill it never uses nor receives one.
  - **Writes are property-granular for a set**: `PropertyGroupEditor::applyEditedTo()` writes only what
    the artist touched since `bind()` and leaves every other property as each object had it. One subject
    still takes the whole-group `applyTo()`. The edits come back as `changedMany()` / `committedMany()`,
    in the selection's order, and land as one history step (*Edit N objects*).
  - **The colour tool follows the same rule**: poured onto an object that is part of a selection, it
    paints every selected object in the role it has, as one step, and the selection stays.
  - **Delete acts on the whole set, as one history step** (*Delete N objects*) — one gesture, one undo.
    Duplicate, *Apply preset ▸* and *Re-anchor to ▸* are **disabled** while several are selected, for the
    same reason ③ shows nothing: they act on one object and say so.
  - A feed re-applies the set and drops whatever no longer exists, so an undo that removed two of five
    leaves three selected rather than clearing the lot.
  - **A selection may hold objects and tails at once**, because *position* is the role they share: a tail
    of one balloon and the body of another are dragged as one thing. Ctrl gathers them — on a tail's row in
    the tree, or on its handle on the strip — and the balloon a selected tail belongs to is selected with
    it, because its handles only exist while it is. ③ then shows **no** property sections: what such a
    selection has in common is where it sits, and that is edited by dragging rather than in a panel.
  - **Dragging one of a selection moves all of it** — *position* is the one property every object has.
    Each co-mover is placed at the position it held when the drag began plus the drag's delta, so a fast
    drag cannot make the formation drift; the whole move is **one** history step (*Move N objects*), and
    each object re-anchors to whatever page it lands on, exactly as a single drag does. Only a **move**
    carries the others: a resize is that object's own size.
- **The object's context menu is the widgets' own action list** (`Qt::ActionsContextMenu` on both the
  canvas and the tree, so the two cannot drift and every entry keeps its shortcut; a right-click selects
  the object it points at unless that object is already part of the selection), in three sections:
  what the object looks like — *Apply preset ▸*, **Blend ▸** — then where it sits in the stack —
  **Bring forward**, **Send back** — then what happens to it whole: *Re-anchor to ▸*, *Duplicate*,
  *Delete*, and *Import artwork…* which needs no selection at all.
  - **Blend** applies to the whole selection as one step, and the tick shows the mode only when the
    selection agrees — the same answer ③ gives by saying *Mixed*. The mode has been in the model, the
    compositor and this preview since the library shipped it; nothing could reach it until now.
  - **Bring forward / Send back** move one object one place through `stripOverlays`, which *is* the
    composite order — the list has always shown it, reversed, and a drag has always committed it. They
    stay single-object: moving several needs a rule about their order among themselves.
- **Selection is the canvas's, not a tool's.** Every object is selectable, movable and resizable under
  every tool; an unanchored one is the only exception, because it is not on the strip. A tool decides
  what a *placement* creates — armed in `Editor::eventFilter()` — so `ObjectController` knows exactly one
  thing about the active tool: whether it is Text. Anything more would put the same gate in two places
  and make the object panel depend on a cause the artist cannot see.
- **The object stack is a tree, and a way to pick existing objects rather than a structure of its own.**
  Every row points at an object the editor already has — an overlay by uid, a page by input uid, a tail
  by its bubble's uid and its index — and holds nothing else. Overlays are top level, front-most first,
  each bubble with a *Tail N* row per tail; **the strip is pinned last with its pages nested under it**,
  collapsed until opened. A bubble's text has no row: it is a property group, not an object.
  - **The strip and its pages are selectable subjects, not overlays.** `ObjectController::Subject` says
    which kind of thing is selected — none, an overlay, a tail, the strip or a page — and selecting one lets go of
    every overlay through the same path an empty click takes. ③ is a stack of two panels chosen by that
    subject: `ObjectStatePanel` for overlays, `StripStatePanel` for the strip and its pages.
  - **The strip** shows its page count, how many pages its grade skips, and the adjustments that grade
    applies — *Curves*, *Brightness & contrast*, *Saturation*, in the order the library runs them — each
    with its values, *Edit* (which opens it in the Grade tool) and *Remove* (one undo step, *Remove …*),
    the way an image editor lists the filters applied to a layer. Rows are rebuilt only when *which* adjustments apply changes;
    while a slider moves only their text is rewritten. The strip cannot be dragged, dropped on or muted:
    a strip that could be hidden would stop showing what renders.
  - **A page** shows its size in the strip and **Excluded from colour correction**, which adds or removes
    its uid in `ColourCorrection::excludedInputUids` — the one colour decision the library lets a page
    make. The toggle is made against the grade the editor last showed and goes out as one undo step,
    named *Exclude page from colour correction* or *Include page in colour correction*.
  - **What the artist opened stays open.** A row is moved by taking it out and inserting it, which makes
    the view forget whether it was expanded, so that is carried across; and rows for deleted objects are
    removed before the strip is placed, so a deletion above it does not move — and fold — the strip.
  - **Updated in place, never cleared and refilled.** Every edit returns to the editor as a feed, so a
    tree rebuilt on each feed would lose what the artist had open, selected or scrolled to at exactly the
    moment they were using it; rows are matched to overlays by uid and only what changed is touched.
  - **Drops land between rows, never onto one** — no row accepts a drop — because nesting is structural
    and not something a drag may create. A tree moves a row by taking it out and inserting it again, so
    a drag can arrive as several model signals: they restart one zero-length timer, and the commit it
    fires compares the rows with the overlays and records nothing if the order did not change. The
    take also clears the row's selection, which is ignored until that commit has re-shown it.
- **The object-state panel follows the selection, never the active tool.** Only the tool-options panel is
  told which controls a tool offers. Which groups the object panel shows comes from the selected object's
  own kind: a shapeless text object has no fill to edit and no tail to grow, so those sections are
  **absent** rather than greyed — a greyed control promises something deferred, and these are not
  deferred, they are not part of that object at all. Stable group order plus update-in-place means the
  panel moves only when the *kind* of the selection changes, which is a change worth seeing.
- **A balloon's editable state is five property groups.** A group is a named struct with exactly one
  editor responsible for it; the editor reads through `bind()` and writes through `applyTo()`, which
  touches only that group. `TextArtifact` composes them — `ShapeProperties`, `SkinProperties`,
  `StyleProperties`, `TextProperties`, `TailsProperties` — and `StripEdit::PropertyGroupEditor` is the
  contract their editors implement. One level down, **`TailProperties`** is one tail by index: its
  `applyTo()` writes that tail and no other, and an index the balloon no longer has writes nothing, so a
  deleted tail cannot come back.
  - **The base is re-read before every patch**, and each editor writes one member. An editor holding a
    copy of the whole artifact would write back stale values for properties it never touched — a canvas
    resize during an edit being the obvious way in.
  - **Two properties belong to no group**, so no editor and no preset can copy them: `box` (the
    object's, not the look's) and `styleSeed` (per balloon, set once at placement — a preset carrying
    it would give a chapter one repeated wobble).
  - **Reading the target is allowed; writing outside the group is not.** `TailListEditor::applyTo()`
    reads the shape, because a shapeless artifact has nothing to grow a tail from; `TailEditor::applyTo()`
    reads the tail's tip, so a spin box cannot undo a drag made since.
  - **The enums live with their groups** — `ShapeProperties::Kind`, `StyleProperties::Kind` — with
    `TextArtifact::Shape` and `::Style` kept as aliases, so every existing spelling still compiles and
    the persisted names are untouched.
  - The rule that makes it work: **an editor never knows which surface it is in.** A surface asks an
    editor to be compact; an editor never asks where it is, or the two surfaces drift into showing the
    same thing.
  - `tests/gui-unit-tests/` links `Qt6::Gui` and nothing else, so anything it can reach is free of
    `QWidget` by construction. Nine tests: one per group, plus the two colour groups' boundary, the
    seed, and the file format.
- **Every placed thing is a `StripEdit::Object`.** One `QGraphicsObject` per overlay, and everything an
  author does to one is the same whatever kind it is: select, move, drag a corner, mute, delete,
  reorder. That all lives on the base class, once — including the grips, the drag state machine and the
  "report only a settled drag" rule the undo stack depends on. A kind supplies only what it draws, how
  big that is, and any extra handle it offers.
  - **`BubbleObject`** — painted from the **authoring model** by `paintArtifactPaths()`, so typing
    updates the strip with no file round-trip and the preview is the render because both go through one
    function at one scale. Its tails are the object's handles.
  - **`AssetObject`** — imported artwork, drawn from its own pixmap, with no parameters to edit. It
    keeps aspect on a corner drag, because the record stores one width fraction and a distorted one is
    not expressible.
  - **What is clickable is the box, not the bounding rectangle.** `Object::shape()` is the box, plus the
    tail tips (grabbable at any time) and the corner grips while selected. Qt's default would be
    `boundingRect()`, which here is the *drawn extent* — balloon ∪ glyphs ∪ every tail tip, padded for
    grips — a rectangle largely made of empty page whenever a tail is long, and one that lets the upper
    of two overlapping bubbles swallow presses meant for the lower. A tail's shaft is left out
    deliberately: it is a thin sliver far from anything anyone aims at.
  - **The split is a correctness measure, not tidiness.** One item class switching on whether a pixmap
    had been handed to it forces every caller to re-derive the same distinction from the model
    (`m_artifacts.contains(uid)`) — and the ones that get it wrong persist a *default* bubble over
    imported artwork, or make Duplicate write a blank balloon. With two types none of that is
    expressible. Three model-side asks remain and all three are legitimate: choosing which object to
    build, naming a row before the scene has a layout, and comparing records in
    `Project::applyOverlays()`.
- **Placement is resolution-independent.** Every coordinate and the artwork's width are stored as
  fractions of the render's target width, so re-profiling a chapter moves and resizes every object
  proportionally and nothing has to remember what the numbers used to mean. `Layout::targetWidth()` is
  that unit on the GUI side — the strip's width, which *is* the target width because the page domain
  scales every page to it — and `Layout::pixels()` / the write-back in `onOverlayGeometryEdited()` are
  the only two places a placement crosses between the two forms.
- **Placement is page-anchored.** An overlay stores `anchorInputUid` plus an offset from that page's top;
  the viewer resolves it against the layout it just built, exactly as `ProcessingPipeline::run()` does.
  Dragging across a page boundary silently re-anchors. An overlay whose page is not in the strip is shown
  greyed and listed as an orphan — never re-homed, never deleted.
- **Creation is the library's.** The viewer emits `artifactCreated()`; `Project::createOverlay()` writes
  the SVG and calls `ProjectItem::addOverlay()`, which mints the uid, hashes the file and dedups identical
  content. Every other edit arrives as the complete new state on `overlaysEdited()`.
- **A preset is a bubble with nothing said in it.** `BubblePreset` (`widgets/stripeditor/panels/`) is a name
  plus a `TextArtifact` whose `text`, `box`, `tails` and `styleSeed` are meaningless — applying one
  copies the *look* over the selection and copies the content straight back, so restyling never touches
  the lettering. With nothing selected it restyles `prototype()` instead, which is what the next
  placement is built from. Serialised by taking `artifactToJson()` and removing those four keys: the
  reader's defaults fill them back in, and a styling field added to `TextArtifact` is carried without
  being enumerated anywhere. Built-ins are code and are not deletable; the artist's own live in the
  application config (`QSettings`, key `bubblePresets`) because restyling follows the artist, not the
  chapter. A *pack* is the same array in a file, marked with `platemakerBubblePresets`, imported by
  replacing same-named presets rather than accumulating them.
- **Imported artwork is the same overlay with fewer attributes.** *Import artwork…* copies a file into
  `overlays/` under its content hash and registers it with **no** authoring record; with no `pm:`
  parameters it becomes an `AssetObject` — placed, moved, re-anchored, muted, resized and rendered like
  any bubble, but not re-typable. Duplicating one goes back through the import channel rather than
  through creation, because there is no authoring record to re-emit and the library dedups the identical
  bytes onto the file already there.

##### Where a bubble lives, and when it becomes pixels

**Two** layers, each with exactly one owner. For a workspace at `D:\Comic\Chapter_002.platemaker.json`:

| layer | what it holds | where | written by |
|---|---|---|---|
| library record | uid, `assetPath`, `sha256`, `anchorInputUid`, `xFrac`/`yFrac`/`wFrac`, enabled, blend | `Chapter_002.platemaker.json` → `projectItems[].stripOverlays[]` | lib |
| the asset | resolved artwork **plus** the editor's `pm:` parameters | `D:\Comic\overlays\ovl-<sha16>.svg` | GUI |

**Two and not three**, because what a bubble *says* and what it *looks like* belong in one file. Holding
shapes, fills, strokes and glyphs in a bespoke schema beside a bitmap is inventing a worse SVG; here the
artwork every renderer can draw carries, in a private `pm:` namespace every renderer ignores, the
parameters the editor re-solves from. Losing those attributes degrades a bubble to
flat-but-still-rendering art rather than to nothing.

**Only the GUI ever rasterises; the library draws nothing.** It happens in two places through **one**
function, `paintArtifact()`:

- **Preview** — `BubbleObject::paintContent()` calls it on every scene repaint, so typing updates the strip with
  no file I/O at all.
- **The file** — `renderArtifact()` calls it into an ARGB32 `QImage` when an edit *settles* (the panel
  debounces typing by 300 ms; a drag reports on mouse release), never per keystroke.

Because the scene is the strip at 1:1, both run at the same scale over the same numbers — so the preview
is not *consistent with* the render, it is the same drawing. The shape-picker tiles are a third caller of
the same function, which is why a tile cannot show a shape that placing it does not give you.

**A bubble owns one file for its lifetime.** `writeArtifactSvg()` names a *new* overlay by the content
hash of the bytes about to be written (so identical bubbles share one file, matching the library's own
dedup), and thereafter overwrites that same path. Naming every revision by its content instead would
leave one file per settled edit — a dozen in a single lettering session. Undo does not need them:
`fullSnapshot()` carries the complete authoring record, so `Project::rewriteOverlayAssets()` re-emits the
file from the restored record. An overlay sharing a path with another (`addOverlay()` dedups identical
content at creation) forks to a fresh file rather than re-lettering its twin.

A text or styling edit therefore rewrites the asset **and** the record's `sha256`; a move or a reorder
rewrites neither, only the placement. `Project::applyOverlays()` re-emits exactly the artifacts whose
authoring record actually changed.

**A styled bubble is previewed by the library.** Marker and Ink are SVG filters, which Qt cannot draw at
all, so `BubbleObject` shows an `OverlayRaster` obtained from `StripOverlayCompositor::rasterizeSvgRgba()`
while at rest, falling back to its own paths mid-drag. It renders from the **document in hand**, not from
the file just written: on a synced drive a file read back immediately after a write may still return the
previous content, which would pin pre-edit artwork on screen for a whole session.

---

## 3. Application State

All persistent state is stored in `Platemaker::Models::Workspace` — the GUI
never has its own parallel data model.  `MainWindow` owns the single live `m_workspace` (the whole
model is loaded at open); each `Project` widget holds a **reference** to it and is a live view over
`m_workspace.projectItems[m_projectIndex]`, not a copy.  Project widgets are created lazily, one per
open dock.

```
MainWindow
  └── m_workspace : Workspace          // loaded from .platemaker.json; the source of truth
  └── m_workspacePath : QString        // current file path
  └── m_dirty : bool                   // unsaved changes
  └── m_overlayArtifacts : ArtifactStore   // parsed cache; the records live in the overlays SVGs

Project (one per open project dock)
  └── m_workspace : Workspace&         // reference to MainWindow's workspace
  └── m_projectIndex : int             // index into m_workspace.projectItems
  └── m_artifacts : ArtifactMap        // this project's slice of the store
  └── m_workspacePath : QString        // where overlays/ and the sidecar live
```

**Bubble authoring records are a cache, not a store.** What a bubble *says* — shape, text, font, colours
— is information the workspace codec has no business carrying, so it lives inside the overlay's own SVG
in the `pm:` namespace. `MainWindow::m_overlayArtifacts` holds the *parsed* form, keyed by project uid
then overlay uid, so a `populate()` does not re-read and re-parse the whole chapter; it is repopulated
from the assets by `artifactsFromOverlays()` when a workspace opens, and **nothing about it is written to
disk**. An authoring sidecar would be a second copy of what the asset already carries — one more file to
keep in step, and one more thing to lose separately from the artwork it describes.

The assets live in an `overlays/` folder beside the workspace file, which follows the storage split the
rest of the app runs on (the library is a complete, OS-path-agnostic tool; the GUI decides where things
live) and keeps a workspace self-contained: the two copy together. Deliberately **not** in
`.platemaker-cache/`, which is regenerable and safe to delete. Each `Project` holds its own slice and
pushes changes back via `Project::artifactsChanged`.

**Undo covers both halves.** `Project::fullSnapshot()` is the library's project snapshot *plus* those
authoring records, serialised together, so undoing a text edit restores what a bubble said and not merely
where it sat. Without it the record would come back pointing at the *new* bitmap, and the strip would
show text the render does not bake.

**Mutation goes through the library.** The workspace's profile palettes and the projects' profile-link
fields are private in the model; the GUI edits them only through `Infrastructure::WorkspaceEditor`
(`replaceCanvasProfiles` / `replaceOutputProfiles`, `add`/`removeCanvasProfileToProject`,
`setProjectOutputProfile`, `setCanvasProfileTemplateInfo`).  It reads through the const accessors
(`canvasProfiles()`, `outputProfileId()`, …).

**Keeping open views in sync.** A workspace-level profile edit (Manage/New/Edit) emits
`MainWindow::workspaceProfilesChanged`, connected in `openProjectDock()` to
`Project::refreshProfileViews()` on every open dock — so the output-profile combo and assigned-canvas
list of all open projects update at once, without a manual refresh.

**Dirty tracking rules:**
- Any profile edit → `m_dirty = true` → asterisk in title bar
- Any image tile reorder → `ProjectEditor::setInputOrder`/`moveInput` (rewrites `order`) + `m_dirty = true`
- Successful pipeline run → `m_dirty = true` (hashes updated)
- File → Save → `WorkspaceSerializer::save()` → `m_dirty = false`

---

## 4. Key Workflows

### 4.1 Open Workspace

```
File → Open Workspace
  → QFileDialog (*.platemaker.json)
  → WorkspaceSerializer::load(path)
  → populate workspace panel (project list)
  → restore open projects from last session (if desired)
```

### 4.2 Create Workspace

```
File → New Workspace
  → QFileDialog (choose save location)
  → construct default Workspace (one default OutputProfile "Webtoon Standard")
  → WorkspaceSerializer::save(path)
  → open workspace panel
```

### 4.3 Open / Create Project

```
Workspace panel → double-click project  OR  Workspace → New Project
  → Project dock opens (tabbed with the workspace panel), or is raised if already open
  → mergeFileScan() populates tile grid from input directory
  → thumbnails load asynchronously in background
```

### 4.4 Run Pipeline

The pipeline runs on a `RenderWorker` moved to its own `QThread`; it holds **copies** of the inputs,
profiles and output dir, so it never touches the live workspace.  The lib reports progress through a
`Core::ProcessingCallbacks` struct (plain `std::function`s called synchronously on the worker thread);
each callback lambda does one cheap thing — `emit` a Qt signal — which is delivered to the main thread
via a queued connection, so the *reaction* (repainting tiles) happens on the main thread while the
render is never blocked on the UI.

```
Process → Run  (or the project's Render button)
  → startRender(projectIndex):
      project.sanitize(workspace.canvasProfiles())   // refresh statuses (disk + config)
      if up-to-date and no config change → inform user, skip
      confirm if outputs are stale (format/size/canvas changed since last render)
      worker = new RenderWorker(copies…);  worker.moveToThread(thread)
      connect worker → MainWindow:  progress, log,
                                    sliceSaved(index,…)  → setOutputTile(index,…)   // live, positional
                                    inputStatus(path,…)  → setInputTileStatus(path,…) // live, phase 1
      ProcessingPipeline::run(inputs, outProfile, canvasProfiles, canvasProfileIds,
                              outDir, cancel, callbacks, onlySlices?, cacheDir,
                              colourCorrection, stripOverlays)  // static; worker thread
      show progress bar + Stop button
  → onRenderFinished():
      project.applyProcessingResults(records, appliedProfiles, outcome.skippedPages,
                                     workspace.canvasProfiles(), outDir, timestamp)
        // skipped pages are recorded as FileStatus::Skipped, not Processed
      delete orphaned outputs the new config no longer produces
      populate()  + WorkspaceSerializer::save()
```

During phase 1 (strip building) each input's tile turns green as it is appended, cyan **Processed
(no canvas profile)** when it is rendered without a matching profile, or violet **Skipped** when it is
left out (missing / load error); then output tiles stream in per slice.  See §2.3.

**The two optional steps ride along.** `startRender` copies the project's `colourCorrection` and
`getStripOverlays()` into the worker; both default to empty, so a project using neither renders
byte-identically to one built before they existed. They also need a staleness axis of their own: a grade
tweak or a moved bubble changes no input and no output file, so `Models::processingConfigSignature()` is
compared against the stored `ProjectItem::processingSignature` and a mismatch folds into the
"needs a full re-render" decision, exactly as `outputProfileSignature()` does for format/size changes.
The signature is stamped back after a completed render. Requires **libplatemaker 0.6.0**.

**Render output contract (consumer side — lib SPECIFICATION §7.0).** `startRender` passes the workspace's
`.platemaker-cache` dir to `run()`, so the pipeline warms each slice's thumbnail from its **in-RAM** pixels
*before* `sliceSaved` fires. The output tile's `getOrGenerate()` is then a **cache hit** that never
re-reads a slice the render is still writing — this closes a Windows read/write race that used to abort
re-renders with *unable to open for write*. Rules the GUI must keep: treat `sliceSaved(path)` as the only
"ready" signal (never read an output before it or during a run that rewrites it); a locked output surfaces
as `ProcessingErrorCode::OutputLocked` and is shown as a short *Render failed — see the action log* status
(the lib does not retry — that policy is ours). Requires **libplatemaker 0.5.0**.

### 4.5 Cancel Pipeline

```
Cancel button
  → cancellationToken.cancel()
  → worker checks token between slices and exits early
  → progress bar resets, partial output kept
```

### 4.6 Generate Templates

```
ManageCanvasProfilesDialog → "Generate Templates" button
  → emits generateTemplatesRequested(selectedProfiles)
  → MainWindow receives signal
  → QFileDialog (choose output directory)
  → for each profile:
        TemplateGenerator::generate(profile, activeOutputProfile, outPath)
  → open output directory in file manager
```

### 4.7 Manage Canvas Profiles (CRUD)

```
Workspace → Canvas Profiles…
  → ManageCanvasProfilesDialog opens on a COPY of workspace.canvasProfiles()
  → Add / Edit / Delete happen on that copy inside the dialog
  → OK  → WorkspaceEditor(m_workspace).replaceCanvasProfiles(copy)
            // mints ids for new profiles, dedups, carries templateInfo by id
          setDirty(true);  emit workspaceProfilesChanged()   // open projects refresh live
```

The output-profile equivalent is the same through `replaceOutputProfiles()` (which additionally strips
any preset — presets are code-defined and never persisted).  Single-profile edits (edit-active,
double-click) use a copy → mutate → replace of the whole palette, since the palette is private and
cannot be mutated in place.

**Conflict guard** (lib SPECIFICATION.md §7.5.2):  
Linking a canvas profile to a project goes through
`WorkspaceEditor::addCanvasProfileToProject()`, which returns `false` when the profile's canvas W×H
collides with one already linked; the GUI shows an error and does not link it.  The project's
`canvasProfileIds` are private in the model, so a raw bypass of this guard is not possible.

---

## 5. Thumbnail Loading Policy

- Thumbnails are **never** loaded on the main thread.
- Each `ImageTile` requests its thumbnail via `QtConcurrent::run()` on first paint.
- `ThumbnailCache::getOrGenerate(filePath)` is the only call made in the worker.
- On completion the worker emits a signal back to the tile; the tile calls `update()`.
- A placeholder grey rect is shown while loading.
- Failed thumbnails show an error icon; the tile remains interactive.
- **During a render, output thumbnails are pre-warmed by the library, not read from the output file.**
  The pipeline writes each slice's thumbnail into the same cache from the in-RAM slice before `sliceSaved`
  (§4.4), so the output tile's `getOrGenerate()` is a cache hit. File-reading generation (`getOrGenerate`
  opening the source) is therefore only for **input** tiles and **at-rest** output tiles (reopening a
  workspace) — never for an output while a render is writing it, which is what avoids the read/write race.
- **The Strip Viewer (§2.5) reuses these same output thumbnails as low-res proxies** — an instant blurry
  fill drawn under each slice while its full-resolution decode runs, so scrolling never shows a gap.

---

## 6. Background Thread Model

| Operation | Mechanism | Thread safety notes |
|---|---|---|
| Thumbnail loading | `QtConcurrent::run()` per tile | `ThumbnailCache` is thread-safe |
| Strip-editor page build | `QtConcurrent::run()` per page (proxy + sharp) | The sharp tier runs the library's page domain (`decodePageToRgba`); the proxy tier reads `ThumbnailCache`. Both are thread-safe; `QPixmap` is built on the GUI thread in the watcher, and a generation counter drops results from a superseded rebuild |
| Bubble rasterising | GUI thread | `paintArtifact()` on a small `QImage`; it runs on a settled edit, not per keystroke, and a bubble is a few hundred pixels — not worth a thread |
| Pipeline run | Single `QFuture` via `QtConcurrent::run()` | `CancellationToken` is atomic |
| Template generation | `QtConcurrent::run()` per profile | `TemplateGenerator` is stateless |
| All UI updates | `QMetaObject::invokeMethod()` or signal/slot | Never touch widgets from worker |

---

## 7. Cross-Platform Notes

- Tested on Windows 10/11 (MSVC + MSYS2 MinGW) and Ubuntu 22.04.
- `libplatemaker.dll` and libvips runtime DLLs are copied next to the executable
  by the CMake post-build step — no manual PATH setup needed.
- Linux: RPATH embedded in the installed binary; no `LD_LIBRARY_PATH` needed after install.
- macOS: not tested yet.

---

## 8. UI Style Conventions

- **No custom QSS** unless strictly necessary — rely on the platform native style.
- Dialog buttons: standard `QDialogButtonBox` with Ok / Cancel.
- Destructive actions (delete profile, remove project): require a `QMessageBox::question` confirmation.
- Errors from libplatemaker (pipeline, serialiser): shown as `QMessageBox::critical`.
- Progress: `QProgressBar` in the bottom dock, range 0–100 (percent of output slices written).

---

## 9. Windows Security Hardening

Platemaker is an **unsigned** desktop app that ships a large bundled DLL closure (Qt, `platemaker.dll`,
the whole libvips graph, the MinGW runtime), all copied next to the executable (§7). While debugging
drag-and-drop we noticed third-party **global hooks** injecting themselves into our own process (LG
OnScreen Control's `ScreenSplitterHook64X.dll`; RGB software) — benign, but it exposed that the process
took no injection/hijacking precautions. This section records the security posture and, importantly,
what we deliberately do **not** do and why.

### 9.1 What we do — restrict the DLL search path

`app/main.cpp` calls `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
LOAD_LIBRARY_SEARCH_SYSTEM32)` as the **first statement in `main()`** — before `QApplication`, before
any post-`main` dynamic DLL load. This drops the current working directory and `PATH` from the default
`LoadLibrary` search, closing the classic **DLL search-order hijack / planting** vector (drop a
malicious `zlib1`/`libpng` on `PATH` or beside a data file and have our graph pick it up).

- **Why it's safe here:** the entire dependency closure is co-located in the application directory, which
  `LOAD_LIBRARY_SEARCH_APPLICATION_DIR` still covers, and the exe's static imports are resolved *before*
  `main()` runs — so startup linkage is unaffected. Only *post-`main`* dynamic loads are narrowed (Qt
  plugins, loaded by absolute path with their deps in the app dir; libvips operation DLLs at render
  time). Verified by launching + running a full render under both the Qt Creator run environment (which
  injects the Qt bin via `PATH`) and the installed build.
- **Robustness:** the API is resolved dynamically via `GetProcAddress`, so it degrades to a no-op on any
  pre-Windows-8 host instead of failing to load, and sidesteps MinGW header/`_WIN32_WINNT` quirks. It
  logs a one-line startup notice (`DLL hardening: …`) so the code path is verifiable in DebugView / the
  Qt Creator Application Output.

### 9.2 What we defer — blocking extension-point injection

The natural companion is `SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, …)`, which would
block exactly the hook injection we observed, plus `AppInit_DLLs` and legacy IME DLLs. **We deliberately
do not ship it.** It also disables **legacy IMM32 IMEs** and some **accessibility / assistive tools**.
Broken IME would hurt non-Latin text entry for Korean / Japanese / Chinese authors — a core webtoon
audience — and we have no way to validate that without a CJK IME test rig (modern TSF IMEs are usually
unaffected; legacy ones are not). The benign, low real-world risk of hook injection (the attacker already
needs hook-registration privileges on the machine — not a remote vector) does not justify that
regression. **Revisit** once code signing lands or an IME test rig exists; the deferred snippet + caveat
live in `docs/TODO.md`.

### 9.3 What this does *not* address

Neither mitigation affects **SmartScreen / AV reputation** — for an unsigned app that is per-file-hash and
resets each release. **Code signing** is the real integrity/reputation fix; the search-path restriction is
defence-in-depth layered under it, not a substitute.
