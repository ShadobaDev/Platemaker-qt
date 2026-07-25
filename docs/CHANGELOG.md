# Changelog

## [1.2.0] — Unreleased

Requires **libplatemaker 0.2.2** — the About dialog now reads component versions and licences from
the lib.

### Added

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

- **The GUI no longer asserts versions or licences about code it does not own.** Compile-time
  licence definitions are kept only for Platemaker itself and Qt; libplatemaker and its dependencies
  are sourced from the lib, so they cannot silently go stale when a dependency is swapped or
  relicensed

### Fixed

_none_

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
