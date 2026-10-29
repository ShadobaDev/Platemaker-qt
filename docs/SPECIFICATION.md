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
| a new balloon outline | **a shape** | an enum value and one table entry; parameters add five more files |
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

A value of `ShapeProperties::Kind`; move, resize, tails, lettering, undo, presets, the tiles and the
row glyphs come free. Copy **Thought** (`thoughtPath()`, `thoughtArea()`).

1. `widgets/objectrecord/propertygroups.hpp`: append to `Kind` (**append only**, persisted by name) and
   move `k_lastKind`.
2. `widgets/objectrecord/shapes.cpp`: its named constants, `<name>Path()` and `<name>Area()` (the largest
   rectangle the outline contains), and **one entry in `k_shapes`**, in the enum's order: the persisted
   name (never changes), the title, whether a placed one gets a tail, and the two functions.
3. `widgets/objectrecord/objectrecord.cpp`: its place in `shapeOrder()`, the picker's order.
4. *Optional:* a built-in preset in `presetstore.cpp`.

**Caught by** `tests/gui-unit-tests/test_property_groups.cpp`: `Conversion.EveryShapeAndStyleHasATableEntry`
(no entry, or one out of order), `Conversion.ThePickerOffersEverySilhouetteAndNoKind` (missing from
`shapeOrder()`), `PropertyGroupPersistence.EveryShapeAndStyleRoundTrips` (missing name), and
`Golden.EveryShapeAndStyleIsWrittenAsBefore`, which fails until the new shape's SVG is accepted with
`PLATEMAKER_UPDATE_GOLDEN=1` and its diff reviewed.

**Not caught by any test:** the outline must be **star-shaped around its centre** (a tail leaves the
balloon along a ray from the centre); every constant is named; noise uses `styleSeed` and nothing else
(`topUpStyleSeed()` mints it; a *Re-roll* entry on the object menu is planned, not built).

**With parameters of its own** (no shape has any yet): each parameter is a member of
`ShapeProperties` and **one line in its `fields()` list** — the key it is saved under and the member
(`widgets/objectrecord/fields.hpp`). That one line gives it equality (which gates the file rewrite), a
JSON key and a `pm:` attribute, with the struct's default as the fallback when a file lacks it. Then
controls in `shapeeditor.cpp` (`bind()` emits nothing, a change emits `changed()`), `loadedRecord()` and
`expectUntouched()` in the test, and the test's mirror struct, whose `static_assert` stops the build
until the member is placed. `Fields.EveryFieldIsComparedAndSaved` then checks the new field on its own.
A value of a type no group holds yet needs an overload in each of the three walks (`Fields::same()`,
`toJsonValue()`/`fromJsonValue()`, `attrValue()`/`fromAttr()`).

### A line style

A value of `StyleProperties::Kind` (append only, move `k_lastKind`) and **one entry in `k_styles`**
(`widgets/objectrecord/styles.cpp`), in the enum's order: the persisted name, the title the Style panel
lists, and its recipe. A recipe is an SVG noise filter (`NoiseRecipe`): the filter in `recordsvg.cpp`
and the margin it needs (`styleMargin()`) both read it, and a style with one previews through the
library (`hasFilter()`) and has an amount to scale. Caught by the same table, round-trip and golden
tests as a shape.

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
