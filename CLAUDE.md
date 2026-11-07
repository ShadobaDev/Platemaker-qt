# Platemaker (Qt GUI) — notes for coding agents

A Qt 6 desktop application over `libplatemaker`, which is a separate repository
(ShadobaDev/PlateMaker). The GUI holds no domain logic: every persistent fact is in the library's
`Workspace` model.

How to contribute — what a change must include, commit messages, `.ui` files — is in the wiki page
`docs/wiki/Development-Contributing.md`. Read it before proposing a commit message.

## Always
- A domain rule belongs in the library; the GUI owns only where files live and the UI.
- Nothing blocks the UI thread. A worker never touches a widget; it reports back by queued signal.
- New widgets take their colours from the palette: no colour stylesheet.
- Never edit a `.ui` that is open in Qt Designer, and never the generated `ui_*.h`.

## Where to look

| When you… | Read (`docs/wiki/`) |
|---|---|
| need the overall layout, threads, signals, UI style, interaction rules | `Development-Architecture.md` |
| work in the main window or the project view | `Development-Main-Window.md` |
| change a user workflow (open, render, templates…) | `Development-Workflows.md` |
| touch the strip editor | `Development-Strip-Editor.md`, then `Development-Strip-Editor-Code-Map.md` |
| add a tool, a balloon shape or a kind of object | `docs/SPECIFICATION.md` § Extending the strip editor |
| touch what the workspace folder holds | `Development-Workspace-Ownership.md` |
| build, or link a new library version | `Development-Building.md` |
| write the CHANGELOG or release | `Development-Releasing.md` |
| need what a library call does | the library's wiki (ShadobaDev/PlateMaker) |

`docs/wiki/` is a git submodule: the GitHub wiki. Changes there are committed in that repository, then
the new pin is committed here.

## Build and test

The GUI is built in Qt Creator against an installed `libplatemaker` (`LIBPLATEMAKER_DIR`, see
`Development-Building.md`). A library API change needs a re-install of the library first.
`tests/gui-unit-tests/` (configure with `PLATEMAKER_GUI_BUILD_TESTS=ON`) covers the property model;
everything with a window is verified by running the application.
