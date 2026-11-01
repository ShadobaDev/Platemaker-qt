# Platemaker GUI — specification (outline)

This page is the outline. The detail lives in the repository's **wiki**:
**https://github.com/ShadobaDev/Platemaker-qt/wiki/Development**, checked out here as a git submodule
at [`docs/wiki/`](wiki/).

```
git clone --recurse-submodules https://github.com/ShadobaDev/Platemaker-qt.git
# or, in an existing clone:
git submodule update --init docs/wiki
```

The submodule pins one wiki commit. After editing the wiki, push it, then commit the new pin here
(`git add docs/wiki`) so the two stay in step.

The domain itself (the pipeline, the data models, serialisation, profile matching) is the library's,
specified in [libplatemaker's wiki](https://github.com/ShadobaDev/PlateMaker/wiki).

## What the GUI is

A Qt 6 desktop application that puts everything `libplatemaker` does behind a visual interface:
workspaces and their projects (chapters), source pages as tile grids, the strip pipeline with live
progress, canvas and output profiles, margin templates, and the two **optional** pipeline steps
authored on a continuous strip of the chapter: a colour grade, and text, balloons and artwork.

**It holds no domain logic.** Every persistent fact is in the library's `Workspace` model, which
`MainWindow` owns and every widget views; nothing in the GUI keeps a parallel copy. Work that takes
time runs off the UI thread.

## The parts, and where each is specified

| Part | Wiki page |
|---|---|
| Code organisation, layers, cross-platform notes, UI style | [Architecture](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Architecture) |
| The main window, the project view, image tiles, profile dialogs, application state, thumbnails, threads | [Main window](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Main-Window) |
| Opening, creating, rendering, cancelling, templates, canvas profiles, step by step | [Workflows](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Workflows) |
| What the workspace folder holds, who writes it, fonts, packages, the folder lock | [Workspace folder ownership](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Workspace-Ownership) |
| Status-bar advisories and badges | [Advisories](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Advisories) |
| Replacing a page, unanchored objects, the render gate | [Inputs and anchoring](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Anchoring) |
| The DLL search path, and what is deferred | [Windows hardening](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Windows-Hardening) |
| **Strip editor:** vocabulary, layers, regions | [Tool framework](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Strip-Editor) |
| — which folder holds which region, what may include what | [Code map](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Strip-Editor-Code-Map) |
| — the dock, the rail, the columns, the cursor, the colour pair, rendering, the grade | [Shell](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Strip-Editor-Shell) |
| — bubbles, tails, placement, presets, imported artwork | [Objects](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Strip-Editor-Objects) |
| — property groups and the write contract | [Property groups](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Strip-Editor-Properties) |
| — OBJECT STATE, TOOL OPTIONS, the OBJECT STACK, selection, the object menu | [Panels](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Strip-Editor-Panels) |
| — one history per project | [History and undo](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Strip-Editor-History) |

---

## Extending the strip editor

Which files to touch, and which test tells you what you missed. Paths are under
`widgets/stripeditor/` unless they start with `widgets/objectrecord/` (the record a balloon is, how it
is drawn and saved). **The library needs no change for any of the three**: it composites the SVG or
picture the GUI writes.

| You want… | You are adding… | Cost |
|---|---|---|
| a rail button doing what an existing kind of tool does | **a tool row** | one table row |
| a new ready-made outline of an existing kind | **a template** | one table row |
| a new way of describing an outline | **a shape** (a kind) | an enum value, one table entry and a template; parameters add four more files |
| something on the strip that is neither a balloon nor a picture | **a kind of object** | the last took 19 files |

**A tool is a row of data and holds no state**, so a rail row is a kind of object, never a value of one
of its properties. A new outline belongs in the shape tiles; a one-click way to place a configured one is
a **preset**. (Caption was a rail tool and was removed for exactly this.)

### A tool row

1. `toolrail/toolregistry.cpp`: one row in `tools()`, in rail order — id, icon, name and hint
   (`QT_TRANSLATE_NOOP`), `ToolKind`, a fixed shape or `std::nullopt`, the options page key (`""`,
   `"grade"`, `"bubble"`, `"artwork"`), two cursors.
2. `icons/tools/<id>.svg` and `app/resources.qrc`.

Beyond that it is no longer a row: a **new options page** goes in `tooloptions/` and is registered in
`editor.cpp` (an unregistered key fails a `Q_ASSERT` at start-up); a **new `ToolKind`** reaches
`canvas/canvasinput.cpp` (what a press does) and `toolrail/cursors.cpp`.

### A shape

A **kind** is a way of describing an outline: a value of `ShapeProperties::Kind`, read with the
parameters in `ShapeProperties`. A **template** is a ready-made outline, a value of that group with a
title and whether it speaks; the picker's tiles are the templates (`shapeTemplates()` in `shapes.cpp`), so
the speech balloon and the caption box are two templates of one kind, the Box. **A new outline that an
existing kind can describe is one template row**, and nothing else.

A new kind gets move, resize, tails, lettering, undo, presets, the tiles and the row glyphs for free. Copy
**Ellipse** (`ellipsePath()`, `inscribedInEllipse()`) for one with no parameter, **Box** (`boxPath()`,
`boxArea()`) or **Cloud** (`cloudPath()`, `cloudArea()`) for one with parameters, or **Polygon** (`polygonPath()`, `polygonArea()`) and **Curve** (`curvePath()`,
`curveArea()`) for one with no formula for its text area: `largestInscribedRect()` finds it from the
outline.

1. `widgets/objectrecord/propertygroups.hpp`: add to `Kind` (persisted by name; the strip editor is
   unreleased, so a kind may still be renamed or merged) and move `k_lastKind`.
2. `widgets/objectrecord/shapes.cpp`: its named constants, `<name>Path()` and `<name>Area()` (the largest
   rectangle the outline contains), both taking the shape group and the body, and **one entry in
   `k_shapes`**, in the enum's order: the persisted name, the title, whether an outline matching no
   template speaks, whether it reads `cornerRadius`, the two functions, and the flash's Auto stretch.
   Then **at least one template** in `shapeTemplates()`, in the picker's order.
3. `widgets/objectrecord/objectrecord.cpp`: its place in `shapeOrder()`, which is what names are read
   back from.
4. *Optional:* a built-in preset in `presetstore.cpp`.

**An edge** (a modulation any kind takes, PLAN-L M11) is a value of `ShapeProperties::Edge` (move
`k_lastEdge`) and one entry in `k_edges` (`shapes.cpp`): its names and a `modulate` function from the
bare outline to the modulated one. `outlineOf()` applies it before any tail is united in, so tails stay
smooth. Build it on `pushedIn()`, which takes a depth profile along the edge (`EdgeWalk`) and keeps the
outline's corners, fading the push near a sharp one so a spike does not fold into a loop; the teeth
go **in** by at most `edgeDepth`, which is what `edgeInset()` takes off the text area. Zigzag is the
worked example. A template does not set the edge: `templateOf()` ignores it and a picked tile keeps it
(`withEdgeOf()`).

**Caught by** `tests/gui-unit-tests/test_property_groups.cpp`: `Conversion.EveryShapeAndStyleHasATableEntry`
(no entry, or one out of order), `Conversion.ThePickerOffersEverySilhouetteAndNoKind` (missing from
`shapeOrder()`), `PropertyGroupPersistence.EveryShapeAndStyleRoundTrips` (missing name), and
`Golden.EveryShapeAndStyleIsWrittenAsBefore` (one case per template), which fails until the new shape's
SVG is accepted with `PLATEMAKER_UPDATE_GOLDEN=1` and its diff reviewed. The table test also checks that
every template is a balloon, is found by `templateOf()` as itself, and that every kind has one.

**Not caught by any test:** every constant is named; noise uses `styleSeed` and nothing else
(`topUpStyleSeed()` mints it; a *Re-roll* entry on the object menu is planned, not built). Any outline
takes a tail, concave included: the tail walks from its tip towards the centre and leaves from the first
edge it meets (`Tails.AConcaveOutlineIsLeftNearestTheTip`); only a balloon whose centre lies outside it
gets none.

**With parameters of its own** (the Box's `cornerRadius` is the worked example): each parameter is a
member of `ShapeProperties` and **one line in its `fields()` list** — the key it is saved under and the
member (`widgets/objectrecord/fields.hpp`). That one line gives it equality (which gates the file
rewrite, and decides which template a shape is), a JSON key and a `pm:` attribute, with the struct's
default as the fallback when a file lacks it. Then a row in `shapeeditor.cpp`, through `FieldRows` and a
spec table, shown only for the kinds that read it; `loadedRecord()` and `expectUntouched()` in the test,
and the test's mirror struct, whose `static_assert` stops the build until the member is placed. `Fields.EveryFieldIsComparedAndSaved` then checks the new field on its own.
A value of a type no group holds yet needs an overload in each of the three walks (`Fields::same()`,
`toJsonValue()`/`fromJsonValue()`, `attrValue()`/`fromAttr()`) and a `bump()` in the test. The
`nodes` is the worked example: a list of `ShapeNode`, relative to the box, which a Polygon and a Curve
share. A node is a point, and for a Curve two handles (offsets from it) and a type; one with neither is
saved as its point alone (`x,y`), one with handles as `x,y,type,inX,inY,outX,outY`. Full precision in the
JSON, `k_nodeDecimals` in the SVG, so a template's nodes are rounded to those decimals and a placed one
reads back as itself. `FieldRows` skips a field that is not a number at compile time, so a list never
becomes a row.

### A fill

A value of `SkinProperties::Fill` (move `k_lastFill`), one entry in `k_fills` (`styles.cpp`, its names)
and its drawing in two places that must agree: the painter's brush (`Painter::fillBrush()`) and the SVG's
`<pattern>` (`fillDefs()` in `recordsvg.cpp`). Both are drawn from one description of the pattern's cell
(the screentone's is `toneCell()`), and both are laid from the box's corner (the brush from the painter's
origin, the pattern in `userSpaceOnUse`), so the preview and the output match. A pattern's own colour is
`fillSecond`; its numbers are `SkinProperties` fields with a prefix of their own (`tone…`), rows of the
*Fill & outline* panel through `FieldRows` (`k_toneRows` in `skineditor.cpp`).

### A line or a texture

A style is two independent choices: a **line** (how the edge is drawn) and a **texture** (what is done
to the balloon as it is rasterised).

A line is a value of `StyleProperties::Line` (move `k_lastLine`) and **one entry in `k_lines`**
(`widgets/objectrecord/styles.cpp`), in the enum's order: the persisted name, the title the Style panel
lists, and strokes of its own or none. Strokes are an `ink` function, from the record and its silhouette
to closed paths filled in the stroke colour, with an `inkReach` that says how far they reach past the
silhouette; a line with ink leaves the solid outline off (`hasOutline()`) and is drawn locally.

A texture is a value of `StyleProperties::Texture` (move `k_lastTexture`), **one entry in
`k_textures`** (its name, title and `reach`, how far it moves the balloon past its geometry) and its SVG
filter in `styleDefs()` (`recordsvg.cpp`), written from the texture's own fields. The filter hangs on the
balloon's group, its silhouette and its line's strokes, never on the lettering's group; a balloon with
one previews through the library (`hasFilter()`), and `styleMargin()` adds its reach to the line's.
Rough is the worked example: four `rough…` fields, the defaults the Marker look's, and the Ink look a
built-in preset (`inkRough()`).

A line that follows the outline walks it by arc length (`EdgeWalk`, `edgeWalks()` in `edgewalk.*`) and
varies what it lays there with `smoothNoise()`, seeded from `styleSeed`, so it stays put while the
balloon is edited and *Re-roll* deals it again. The brush (`brushInk()`) is the example: ribbons about
the edge, as wide as the outline (`SkinProperties::strokeWidth`, not a number of its own).

The flash is the worked example of a line. Its parameters are members of
`StyleProperties` with a `flash` prefix, one line each in its `fields()` list; the Style panel builds a
spin for every `flash…` field from that list (`FieldRows`, `properties/fieldrows.hpp`, which any panel
can use for a prefix of its group's fields), and takes its label, range and tooltip from `k_flashRows`
in `styleeditor.cpp` (a field with no row there stops a debug build; a row with `decimals` is a
decimal spin, as Rough's are in `k_roughRows`). The core stretch's *Auto* comes
from `ShapeInfo::flashStretch`, one value per shape in `shapes.cpp`. Caught by the same table,
round-trip and golden tests as a shape; strokes are also checked against the room reserved for them
(`Style.AFlashStaysInsideItsBounds`) and Auto against the shape's value (`Style.AutoStretchIsTheShapes`).

### A kind of object

One record, `ObjectRecord`, serves every kind on purpose (`artwork` is a field, not a type). A kind is a
new `Object` subclass plus what in the record tells it apart. The worked example is commit `268a562`
(the Artwork tool).

| Where | What |
|---|---|
| `objects/<kind>object.*` | the subclass: what it draws and how big; selection, move, resize, mute, delete, stacking are `Object`'s |
| `objects/object.hpp` | a `Kind` value, for the few places that must ask |
| `ObjectController::syncItems()` | **the one place a subclass is chosen** for a record |
| `objects/objectcontroller.cpp`, `objectstack/objectmenu.cpp`, `objectstate/objectstate.cpp` | every `isParametric()` / `isArtwork()` / `hasSilhouette()` decision; `carriesGroup()` (`properties/propertygroup.hpp`) says which groups a record carries |
| `tooloptions/` + `editor.cpp` | its options page, and a tool row (above) |
| `widgets/objectrecord/recordsvg.cpp` | how it is written to the file the library composites |
| `widgets/project/project.cpp` | where the project creates and stores it |

**A quality bar:** could the change be compiled on its own? A shape nearly can; a kind of object cannot,
because it answers in the controller, the panels and the project. That is the cost of one shared record.

**Tests:** configure with `PLATEMAKER_GUI_BUILD_TESTS=ON` and run `platemaker-gui-tests`; anything with
a window is checked by hand. The layer rules are checked on every build (`cmake/check_layers.cmake`).
Before significant work, open an issue; see
[Contributing](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Contributing).
