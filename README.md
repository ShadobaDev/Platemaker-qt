# Platemaker GUI

Qt 6 desktop frontend for [libplatemaker](https://github.com/ShadobaDev/PlateMaker) — a comic artist tool for pre- and post-processing Webtoon-format artwork.

> **Library documentation:** the domain specification, data models, CLI reference and pipeline
> details are in [libplatemaker's wiki](https://github.com/ShadobaDev/PlateMaker/wiki).

---

## Screenshots

_Project view — input files_

![Project input view](docs/pics/Project_Input.png)

_Project view — output settings_

![Project output view](docs/pics/Project_Output.png)

_Project view — output out of sync_

![Project output out of sync](docs/pics/Project_Output_Out_Of_Sync.png)

_Project view — output out of sync, warning before render_ 

![Project output processing](docs/pics/Project_Output_Out_Of_Sync_Warning.png)

_Project view — processing in progress_

![Project output processing](docs/pics/Project_Output_Processing.png)

_Canvas profile dialog_

![Canvas profile dialog](docs/pics/Canvas%20profile.png)

_Canvas profile with margin guides_

![Canvas profile with margin](docs/pics/Canvas%20profile%20with%20margin.png)

_Template manager_

![Template manager](docs/pics/Template%20manager.png)

---

## Installing & verifying your download

The Windows installer is **not code-signed** — a paid certificate is required to remove Windows'
warning, and this is a free open-source project. SmartScreen may therefore show *"Windows protected
your PC / unknown publisher"*. That is expected for an unsigned installer, **not** a sign the file is
unsafe. To proceed: **More info → Run anyway**, or right-click the downloaded file →
**Properties → Unblock → OK** before running.

Since it isn't signed, you can verify the download yourself:

**1. Checksum (SHA-256)** — confirm the file is byte-for-byte what was published:

```powershell
Get-FileHash .\Platemaker-<version>-Setup.exe -Algorithm SHA256
```

**2. Provenance — proof GitHub itself built it from this repo.** Every release carries a GitHub
**build-provenance attestation**: GitHub vouches that the file was produced by this repository's release
workflow and records the exact source commit it was built from. This is stronger than a checksum — you
don't have to trust a hash *we* published.

- **In your browser (nothing to install):** open the
  [Attestations](https://github.com/ShadobaDev/Platemaker-qt/attestations) page. Each entry is a
  GitHub-verified build showing the file it covers (name + SHA-256), the **source commit**, and the
  **workflow** that produced it — i.e. GitHub confirming *"this binary was built here, from this commit."*
  Compare that SHA-256 with your download's (step 1): if they match, the attestation is about your file.
- **Command line ([GitHub CLI](https://cli.github.com/)) — one check for integrity + origin:**

  ```powershell
  gh attestation verify .\Platemaker-<version>-Setup.exe --repo ShadobaDev/Platemaker-qt
  ```

  A `✓ Verification succeeded!` confirms the file's digest matches and that it was built by this repo's
  `release.yml`.

**3. Malware scan** — each installer is scanned on VirusTotal. The checksum and the scan link for a
build are published on its [Releases](https://github.com/ShadobaDev/Platemaker-qt/releases) page.

---

## Requirements

| Tool | Version | Notes |
|---|---|---|
| Qt | 6.8+ | Widgets, Svg, Concurrent |
| CMake | 3.25+ | |
| MSVC 2022 or MinGW (MSYS2) | — | Windows; MSVC is the shipping toolchain |
| GCC / Clang | — | Linux |
| libplatemaker | `LIBPLATEMAKER_VERSION` in `CMakeLists.txt` | see **Linking libplatemaker** below |

---

## Building

Open `CMakeLists.txt` in **Qt Creator** and pick a kit. The post-build step copies `platemaker.dll` and
the libvips DLLs next to the executable.

- **Commands** (the installer, the portable ZIP, deploying to a prefix): [docs/CHEATSHEET.md](docs/CHEATSHEET.md)
- **Details** (a local library build, `LIBPLATEMAKER_DIR`, common failures): the wiki's
  [Building](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Building) page
- **Tests:** configure with `PLATEMAKER_GUI_BUILD_TESTS=ON` and run `platemaker-gui-tests`

### Linking libplatemaker

`CMakeLists.txt` resolves `libplatemaker` in three steps, in order:

1. **`LIBPLATEMAKER_DIR` cache variable** (preferred during development)
2. System `find_package` / `CMAKE_PREFIX_PATH`
3. Automatic download from GitHub Releases (version pinned by `LIBPLATEMAKER_VERSION`
   in `CMakeLists.txt` — the one place the required version is stated)

**Setting `LIBPLATEMAKER_DIR` in Qt Creator:**

1. Open **Projects** (left sidebar) → select your kit → **CMake** tab
2. Click **Add** → **String**
3. Name: `LIBPLATEMAKER_DIR`  
   Value: path to the dev package, e.g.
   ```
   D:/Users/Shadoba/Dev/PlateMaker/install/msvc-release
   ```
4. Click **Apply Configuration Changes** → **Build**

The value is cached in `build/.../CMakeCache.txt` and survives reconfigures.

---

## Documentation

| Where | What |
|---|---|
| [Wiki](https://github.com/ShadobaDev/Platemaker-qt/wiki/) | the user manual, comic-production guides, and the developer pages; checked out at `docs/wiki/` (`git submodule update --init docs/wiki`) |
| [docs/SPECIFICATION.md](docs/SPECIFICATION.md) | the outline: what the GUI is, where each part is specified, how to extend the strip editor |
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | release to release |
| [docs/TODO.md](docs/TODO.md) | the roadmap |

---

## Project Structure

```
Platemaker/
├── CMakeLists.txt
├── Platemaker.iss                 — Inno Setup installer script
├── app/                           — main.cpp, resources.qrc, app.rc (manifest, icon, version)
├── mainwindow/                    — the application shell, one .cpp per concern
│                                    (workspace, projects, render, profiles, templates,
│                                     fonts, package, advisories, about)
├── widgets/                       — one folder per widget, its .cpp/.hpp/.ui together
│   ├── project/                   — a chapter: inputs, outputs and its undo history
│   ├── stripeditor/               — the strip editor, one folder per screen region
│   │                                (the wiki's Code map page says which)
│   ├── objectrecord/              — what a balloon is, how it is drawn, how it is saved
│   ├── workspacefolder/           — the workspace folder: lock, fonts, layout
│   ├── renderworker/              — the background render
│   ├── advisories/, badge/, …     — shared widgets
│   └── …dialog/                   — the profile, template and about dialogs
├── tests/gui-unit-tests/          — GoogleTest, the property model (PLATEMAKER_GUI_BUILD_TESTS)
├── icons/                         — app icons; tools/ and menu/ hold the SVG glyphs
├── cmake/, scripts/               — installer and portable builds, the layer check
├── sbom/                          — the dependency manifest
└── docs/
    ├── wiki/                      — the wiki, as a git submodule
    ├── SPECIFICATION.md           — the outline, and how to extend the strip editor
    ├── CHEATSHEET.md              — build and release commands
    ├── CHANGELOG.md               — release to release
    └── TODO.md                    — the roadmap
```

---

## Licence

`Platemaker` is distributed under the **GNU General Public License v3.0** (GPL-3.0). See `LICENSE`.

Third-party components:
- **Qt 6** — LGPL 3.0 ([qt.io/licensing](https://www.qt.io/licensing/)), dynamically linked
- **libplatemaker** — LGPL 3.0-or-later, dynamically linked
- **libvips** — LGPL 2.1-or-later ([libvips.org](https://www.libvips.org/)), dynamically
  linked via libplatemaker; its own dependency DLLs ship alongside it on Windows
- **nlohmann/json** — MIT, header-only (compiled into libplatemaker, not linked separately)

Platemaker is distributed under open-source licences only — there is no commercial or
dual-licensed edition (see [CLA §7](CLA.md#7-current-licensing-status)).

---

## Download integrity & code signing

Every release publishes SHA-256 checksums and a GitHub build-provenance attestation, so you
can verify that an installer really came from this repository. How to check a download — and
how signing is handled — is documented in the **[code signing policy](docs/CODE-SIGNING-POLICY.md)**.

If your antivirus flags the installer, read that page first: unsigned binaries from small
projects are routinely flagged by ML heuristics without anything actually being detected.

---

## Contributing

Contributions are welcome — open an issue first for anything significant. To extend the strip editor, start with **[*Extending the strip editor*](docs/SPECIFICATION.md#extending-the-strip-editor)**; the wiki's [Development](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development) pages explain how the rest is put together, and [Contributing](https://github.com/ShadobaDev/Platemaker-qt/wiki/Development-Contributing) covers branches and commit messages. By opening a pull request you agree to the **[Contributor License Agreement](CLA.md)**
and the **[Code of Conduct](CODE_OF_CONDUCT.md)**.
