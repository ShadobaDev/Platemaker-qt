# Extending the strip editor

How to add something to the strip editor (the canvas where balloons, lettering and artwork are placed
over a chapter's pages): which files to touch, in which order, and which tests will tell you what you
missed.

This page is the **mechanics**. The reasons behind them are in the code's own comments and in the
wiki's [Strip editor](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Strip-Editor)
pages, which also define the vocabulary used here (object, property group, role, tool, and the
panels ③ properties, ④ tool options and ⑤ object list). Building and submitting are covered at the
end.

---

## First: which of three things are you adding?

The editor can be extended along three axes, and they cost very different amounts. The first instinct
is usually "a new tool". That is rarely the right one.

| You want… | You are adding… | Cost |
|---|---|---|
| a new button on the rail that does what an existing kind of tool does | **a tool row** | one table row, if it reuses a tool kind and an options page |
| a new balloon outline, with or without settings of its own | **a shape** | about five files; parameters add about five more |
| something on the strip that is neither a balloon nor a picture | **a kind of object** | the expensive one: the last took 19 files |

**A tool is a row of data, not a class.** It has a name, an icon, what a press does to the canvas
(`ToolKind`) and which options page it shows. A tool holds no state, so a tool cannot be "a scribble
balloon". What a placement creates is decided by the object it places and the shape that object
wears.

A rule follows from this: **a rail row is a kind of object, not a value of one of its properties.**
Caption used to be a rail tool and was removed for that reason; a caption box is one of the
silhouettes picked in ④. A new outline belongs in the shape tiles, and a one-click way to place a
configured one is a **preset**, not a rail button.

Everything below lives in `widgets/stripeditor/` (the editor) and `widgets/artifact/` (the record a
balloon is, how it is drawn, and how it is saved). **The library needs no change for any of the
three.** It composites the SVG or picture the GUI writes, and knows nothing about shapes, tools or
lettering.

---

## Recipe 1: a tool row

| Step | File | What |
|---|---|---|
| 1 | `widgets/stripeditor/toolregistry.cpp` | One row in `tools()`, in rail order: `id`, icon path, name and one-sentence hint (both `QT_TRANSLATE_NOOP`), `ToolKind`, the fixed shape for a `Create` tool (usually `std::nullopt`), the options page key, and two cursors. |
| 2 | `icons/tools/<id>.svg` + `app/resources.qrc` | The rail glyph. |
| 3 | `docs/SPECIFICATION.md` §2.5 | The rail's list of tools. |

That is all, **as long as the row reuses an existing `ToolKind` and an existing page key** (`""` for
none, `"grade"`, `"artifact"`, `"artwork"`). Beyond that:

- **A new options page** is a panel in `widgets/stripeditor/panels/`, constructed and registered in
  `Editor`'s constructor (`editor.cpp`, the `pageIndex` map) under the key your row names. A row
  naming a key nobody registered fails a `Q_ASSERT` at start-up.
- **A new `ToolKind`** means teaching `Editor::setTool()` its drag mode and cursor, and
  `Editor::eventFilter()` what a press does. If it acts on objects, `ObjectController` needs it too.
  This is no longer "a row".
- **A row with a fixed `shape`** hides ④'s Shape, Skin, Style and Tails box, because the tool has
  already decided. That is right for Text (no balloon) and wrong for anything that should still be
  styled.

---

## Recipe 2: a shape

A shape is a value of `ShapeProperties::Kind`. Everything else comes free:
- move, resize, tails, lettering, undo, presets, the object list;
- the SVG the render composites, the shape tiles and the list glyphs, all drawn from the one outline
  you write.

The closest existing shape to copy is **Thought** (`thoughtPath()`).

| Step | File | What |
|---|---|---|
| 1 | `widgets/artifact/artifact.hpp` | Append to `ShapeProperties::Kind` (**append only**: values are persisted by name). **Move `ShapeProperties::k_lastKind`** to the new last value. |
| 2 | `widgets/artifact/artifact.cpp` | `shapeName()`: the persisted name, never to change. `shapeTitle()`: the translated one. `shapeOrder()`: where the picker offers it. |
| 3 | `widgets/artifact/artifactpainter.cpp` | Named constants in the block at the top, `<name>Path(const QRectF&)` beside the others, and a case in `artifactSilhouette()`, in `textSafeArea()` (where the lettering may go inside your outline) and in `artifactLabel()`. |
| 4 | `widgets/stripeditor/properties/shapeeditor.cpp` | A case in `shapeSpeaks()`: does a placed one get a tail? |
| 5 | `widgets/stripeditor/presetstore.cpp` | *Optional:* a built-in preset that places it in one click. |
| 6 | `docs/SPECIFICATION.md` §2.5 | The shape list and its count. |

**What fails if you miss a step** (`tests/gui-unit-tests/test_property_groups.cpp`):
- *Missing from `shapeOrder()`:* `Conversion.ThePickerOffersEverySilhouetteAndNoKind` fails on the
  count.
- *Missing name:* `PropertyGroupPersistence.EveryShapeAndStyleRoundTrips` fails.

A `switch` with no `default` covers every kind, but MSVC does not warn about a missing case by
default and MinGW does. Build with both, or grep for `case Artifact::Shape::`.

**Rules no test can check:**
- **The outline must be star-shaped around its centre.** A tail finds where it leaves the balloon by
  casting a ray from the centre to the tip (`tailPath()`). An outline that folds back across that ray
  attaches tails in odd places.
- **Name every constant.** The painter keeps them in one block at the top, with a comment each.
- **Noise uses `styleSeed`, never a seed of its own.** A seed that belongs to no property group is
  what stops a preset from giving a whole chapter one repeated wobble. A noisy outline therefore:
  - reads `Artifact::styleSeed`;
  - makes `topUpStyleSeed()` mint a seed for it too;
  - offers a **Re-roll** action on the object menu (one undo step) to draw a new one.

### …with parameters of its own

No shape has parameters yet. The first one brings the [shape registry](TODO.md) with it, so the
paragraphs below describe what that first one has to touch, not an existing pattern.

| Step | File | What |
|---|---|---|
| 1 | `artifact.hpp` | The fields in `ShapeProperties`, and in its `operator==`. **Equality gates the file rewrite:** a record that compares equal is not re-saved, so a parameter left out of `==` keeps the old outline in the export. |
| 2 | `artifact.cpp` | `artifactToJson()` / `artifactFromJson()`: flat keys, read with the struct's default as fallback, so older files still load. |
| 3 | `artifactsvg.cpp` | Write a `pm:` attribute beside `pm:shape` and read it back. The `<path d>` is generic and needs nothing. |
| 4 | `properties/shapeeditor.cpp` | The controls, shown for your kind. Follow `StyleEditor`: `bind()` emits nothing, a change emits `edited()` only (the panel turns a burst of them into one undo step), and the `showSpin`/`restoreSpin` helpers render a multi-selection that disagrees as *Mixed*. |
| 5 | `artifactpainter.cpp` | Your path function takes the parameters. |
| 6 | the tests | `loadedArtifact()` sets the new fields off their defaults, `expectUntouched()` checks them, and each group's mirror struct at the top of the file lists them. |

**What fails if you miss a step:** each property group has a mirror struct and a `static_assert`
on its size. A field added to a group and not to the test stops the build, with the list of places
it has to go. Every write already goes through the whole group (`ShapeProperties::applyTo()`, a
whole-group copy for *Apply ▸ Shape*), so a parameter reaches placements and multi-selections with no
extra code.

---

## Recipe 3: a kind of object

Balloons and imported pictures are the two kinds today: `BubbleObject` and `AssetObject`, both
`Object`s. **One record, `Artifact`, serves every kind on purpose:** `artwork` is a field, not a
type. A second record keyed by the same uid would be a second channel carrying the same object, and
the two would drift. A kind is therefore a new `Object` subclass plus whatever in the record tells it
apart.

The worked example is the commit that added the Artwork tool (`268a562`, 19 files, +821 lines). Read
it before starting. In outline:

| Where | What |
|---|---|
| `widgets/stripeditor/<kind>object.{hpp,cpp}` | The `Object` subclass: what it draws and how big that is. Selection, move, corner resize, mute, delete and stacking live in `Object` and come free. |
| `Object::Kind` (`object.hpp`) | A value for the few places that must ask. |
| `ObjectController::syncItems()` | **The one place a subclass is chosen** for a record. |
| `objectcontroller.cpp`, `panels/objectstatepanel.cpp` | Every `isParametric()` / `isArtwork()` / `hasSilhouette()` decision: which menu entries, groups and roles apply. `carriesGroup()` (`properties/propertygroup.hpp`) is the single answer to which property groups a record carries. |
| `panels/` + `editor.cpp` | Its tool-options page, and a tool row pointing at it (Recipe 1). |
| `widgets/artifact/artifactsvg.cpp` | How it is written to the file the library composites. |
| `widgets/project/project.cpp` | Where the project creates and stores it (undo, the overlays folder). |

`ObjectController`'s header opens with a map of its sections; start there.

---

## A quality bar: could it be a separate DLL?

The editor does not load plugins, and will not: keeping a binary interface stable across versions is
not worth it here. The question still makes a good test of whether a change is well contained:
*could your change be compiled on its own without touching anything outside its files?*

- **A shape** comes close: an outline, a text area and a table row. Once the
  [shape registry](TODO.md) lands it will pass.
- **A tool row** is data, so the question does not apply.
- **A kind of object** does not pass. It answers in `ObjectController`, the state panel and the
  project. That is the honest cost of one shared record, and the reason it is the expensive axis.

---

## Build, test, submit

- **Build:** [README](../README.md#building), Qt 6 with Qt Creator and the libplatemaker dev
  package.
- **Tests:** configure with `PLATEMAKER_GUI_BUILD_TESTS=ON` and run `platemaker-gui-tests`. They
  cover the property model (ownership, persistence, presets) and need no window. Everything with a
  window is checked by hand: there is no CI.
- **Before significant work, open an issue.** The project is not looking for collaborators, but
  that is not a rule against contributions. Asking first saves work that might not fit. The
  wiki's [Contributing](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Contributing)
  page covers branches, commit messages, spelling and the CLA.
