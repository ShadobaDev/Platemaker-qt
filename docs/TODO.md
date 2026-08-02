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

Baseline: **1.3.0 released, 1.3.1 in progress** (`CMakeLists.txt`). 1.3.0 added a `Ctrl+R` alternate
for the render shortcut, app-wide undo/redo, and drag-and-drop input adding (requires libplatemaker
0.3.1). Per the cascade, the pending patch bucket re-derives to 1.3.1.

---

## PATCH — next: 1.3.1

Bug fixes, cosmetics and internal cleanups — no new capability, no change to an existing workflow.

- [ ] **Window icon is loaded from a relative path** — `main.cpp` calls
  `setWindowIcon(QIcon("icons/icon-red.ico"))`, resolved against the *working directory*, so
  it silently yields a null icon whenever the app is not started from the install folder.
  The title-bar icon still appears because Windows takes that from the executable's own
  resource (`app.rc`), which hides the failure — it surfaced only as an empty band above the
  About text, where the icon was supposed to be.

  Fix: load it from the Qt resource system (`:/icons/…`), which the app already has via
  `app/resources.qrc` — same mechanism as the menu icons, and independent of the working
  directory.

- [ ] **Segfault** was detcted but not written down how - to be investigated.

- [ ] **Stale comment: templates no longer draw slice guides** —
  `widgets/templatesdialog/templatesdialog.cpp` still says *"The output profile only supplies
  cosmetic slice-guide lines — use the workspace default; it is not part of the template's
  tracked identity"*, and `generateTemplate()` still takes an `OutputProfile` and falls back to
  `ws.outputProfiles.front()`. But the lib compiles the feature out:
  `template_generator.cpp` has `#define GUIDLINES_ENABLED 0` and an explicit
  `(void)outputProfile;`. So the parameter is dead weight and the comment describes code that
  does not run. Either drop the parameter (a lib API change — see the lib TODO) or re-enable the
  guides; until then, fix the comment so it stops describing a removed feature.

- [ ] **Pre-flight sanitize off the UI thread** — `project.sanitize()` currently
  hashes inputs on the main thread before launching; move to the worker for very
  large projects to avoid a brief UI pause.

- [ ] `MainWindow::m_savedSnapshot` Maybe sha256 instead of holding full string? We do not use it for recovery anyway... or maybe we should keeep for recovery purpose?

- [ ] **Process bar** change style - a solid 15px bar - light broder - empty part background color, filled part grey, error or halt - red.

- [ ] **ImageTile** rework to be more eye-appealing

- [x] **Grey out the Auto-sort rules group until it works** — the `groupBoxAutosort` fields
  (`lineEditInputNameRegex` / `Prepended` / `Appended`, `pushButtonAutosortApply`) were fully
  interactive but wired to nothing, inviting input that does nothing. Done: the group is now disabled
  and its fields show a "Coming soon" placeholder (`project.cpp`, constructor). A stopgap — remove it
  when the Auto-sort feature lands (see the MINOR item).

- [ ] **Light mode** is broken. It is mixed with hardcoded dark greys color, and light grey font on light background is unreadable. Either provide support for light mode, or hardcode drak mode. 
---

## MINOR — next: 1.4.0

New, backward-compatible features. Several are gated on a lib version, noted in the item body.
(The `[x]` items below shipped in 1.3.0; the open ones re-derive to the next MINOR.)

- [x] **Drag files / folders onto the Project window** — Done. Dropping images (or a folder) onto the
  Input-tab tile list adds them via the same path as *Add files* / *Add from directory*
  (`addInputPaths()` → `mergeFileScan()`), as one undo step. Implemented as an event filter on the
  list viewport (`project.cpp` `eventFilter`/`addDroppedUrls`), so external file drops are handled
  while the list's InternalMove reorder drag still works. A dropped folder is scanned like
  *Add from directory* (non-recursive, image extensions) and remembered as the project's
  `inputDirectory`. A new capability (not a change to an existing workflow), hence MINOR.

- [x] **Undo / Redo** (`Ctrl+Z` / `Ctrl+Y`) — Done, app-wide via an **Edit** menu. A `QUndoGroup` with
  one stack per open project + one workspace stack; `Ctrl+Z`/`Ctrl+Y` route to the front tab. Snapshot
  commands built on the lib's `ProjectEditor::snapshot/restore` and `WorkspaceEditor::snapshotMeta/
  restoreMeta` (component-scoped, so light in RAM; depth 10). Covers input edits, canvas links, output
  profile/dir, and workspace-level profile CRUD / project rename / templates. Add/remove project is
  deliberately **not** undoable; render/refresh are never recorded. Requires libplatemaker 0.3.1.
  (Out of scope: undoable project add/remove; restoring links a profile-delete cascaded away.)

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

- [ ] **Auto-save** on pipeline finish (optional setting)

- [ ] **Action log** should report a summary, how manu inputs, how many slices in what time where processed and when. Output cumulative size (MB or KB) would also be nice.

- [ ] **App looks flat/colorless on Linux vs Windows** — no explicit style is
  set in `main.cpp`, so Qt falls back to native per-platform styling: Windows
  gets `windows11`/`windowsvista` (dark mode aware, styled GroupBox borders,
  accent colors); Linux falls back to a much plainer default. Consider
  `QApplication::setStyle("Fusion")` plus a shared custom `QPalette`/QSS so the
  look is consistent (and intentional) across platforms instead of relying on
  whatever the native style happens to provide.

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

- [ ] **Recent-workspaces behaviour is unverified** — `Open recent workspace…` exists, but the
  cap on the number of remembered entries, and what happens when a remembered workspace has
  been moved or deleted (dropped silently vs. an error), have not been established. Test and
  document; the wiki currently says "to be tested".

---
