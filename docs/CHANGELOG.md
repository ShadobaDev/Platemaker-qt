# Changelog

## [1.3.0] — 2026-08-02

Requires **libplatemaker 0.3.1** — this release uses the lib's new `ProjectEditor` /
`WorkspaceEditor` snapshot/restore for undo/redo.

### Added

- **Drag files or a folder onto a project's Input list to add them.** Dropping images (or a folder of
  images) onto the Input-tab tile list adds them the same way *Add files* / *Add from directory* do —
  de-duplicated, appended in order, existing statuses preserved — as a single undoable step. A dropped
  folder is scanned like *Add from directory* (top level, image files only: PNG/JPEG/WebP/TIFF) and
  becomes the project's remembered input directory; non-image files are ignored. Reordering tiles by
  dragging within the list still works.
- **Undo / Redo across the app** (`Ctrl+Z` / `Ctrl+Y`), via a new **Edit** menu. Reversible now:
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
