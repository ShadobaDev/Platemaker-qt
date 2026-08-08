# Cutting a GUI release through GitHub Actions

How to release the **Platemaker GUI** via the CI [`Release`](../.github/workflows/release.yml) workflow,
instead of creating a Release from the GitHub form.

**The model:** the workflow triggers on a **pushed tag** (`*.*.*`, a bare version like `1.4.0`). You push
the tag from git; the single **`installer`** job (Windows only — the installer is Inno Setup) builds
`Platemaker-<version>-Setup.exe` + `SHA256SUMS.txt`, attaches a **build-provenance attestation**, and — on
a tag — **creates the GitHub Release, uploads those two files, and scans the installer on VirusTotal**. Do
**not** touch the GitHub Release form; pushing the tag is the whole trigger.

A manual **"Run workflow"** (`workflow_dispatch`) builds + attests but **does not** publish a Release (the
publish and VirusTotal steps are gated on `refs/tags/*`), so it's a safe dry run — and it still uploads the
installer as a run artifact you can download.

---

## 0. Pre-flight

- **Version:** [`CMakeLists.txt`](../CMakeLists.txt) `project(Platemaker VERSION x.y.z …)` **matches the
  tag** you're about to push (the installer filename comes from it).
- **Lib dependency (the GUI-specific gotcha):** the pinned `LIBPLATEMAKER_VERSION` in `CMakeLists.txt`
  must have a **published lib Release** exposing `platemaker-dev-<that version>-windows-mingw-release.zip`
  on [PlateMaker/releases](https://github.com/ShadobaDev/PlateMaker/releases) — CMake downloads it at
  configure time, so a missing/mismatched asset makes the build **404**. Release the lib **first**.
  *(1.4.0 pins lib 0.4.0, which is published — good.)*
- **Changelog:** `docs/CHANGELOG.md` has the entry for this version.
- Everything committed and pushed to `main`.
- One-time: the **`VT_API_KEY`** repo secret is set on **this** repo (separate from the lib), or the
  VirusTotal step self-skips.

## 1. Dry run — validate the build without releasing (recommended)

1. GitHub → **Actions** → **Release** → **Run workflow** → branch `main` → **Run workflow**.
2. Wait for the **`installer`** job (windows-latest) to go green. The Publish-Release and VirusTotal steps
   are skipped — it's not a tag. That's expected.
3. Optional: download the run's **`installer`** artifact and check the `.exe` runs / installs.
4. Red? Fix, `git push`, re-run. No tags to clean up (none pushed yet).

Why bother if a failed tag build won't release anyway: a dry run avoids leaving a **tag pointing at a
broken commit** that you'd then have to delete and re-push. (Especially useful here, where the Qt install
and the lib download are the usual failure points — see Troubleshooting.)

## 2. Publish — push the tag

Once the dry run is green (or you're confident):

```bash
git checkout main
git pull
git tag 1.4.0                 # bare version, no "v" — matches *.*.*
git push origin 1.4.0
```

This runs the full job: build the installer → provenance attestation → **create Release `1.4.0`** with
`Platemaker-1.4.0-Setup.exe` + `Platemaker-1.4.0-SHA256SUMS.txt`, then VirusTotal scans the installer and
appends the report link to the release body.

## 3. Verify

- **Actions** → the run is green.
- **Releases** → `1.4.0` exists with:
  - `Platemaker-1.4.0-Setup.exe`
  - `Platemaker-1.4.0-SHA256SUMS.txt`
  - a VirusTotal link in the body.
- Provenance check: `gh attestation verify Platemaker-1.4.0-Setup.exe --repo ShadobaDev/Platemaker-qt`.
- **Add release notes:** the workflow leaves the body empty apart from the VT link. Edit the Release and
  paste the highlights from `docs/CHANGELOG.md`; a one-line provenance/verify pointer (see the README's
  *Installing & verifying your download*) is a nice touch for non-technical users.

## 4. Post-release housekeeping

- Update the README's **Latest verified build** block (`README.md`) with this release's filename, its
  **SHA-256** (from `SHA256SUMS.txt`), and the VirusTotal link — those are per-release facts.
- (Optional) list it on itch.io / announce it.

## Rollback — if you pushed a tag on a bad build

```bash
git tag -d 1.4.0                      # delete locally
git push origin :refs/tags/1.4.0      # delete on the remote
# fix, commit, push, then re-tag and push again
```

If a Release was already created for that tag, delete it on the Releases page too before re-pushing.

## Troubleshooting

- **Jobs hang on "waiting for a runner to come online" for many minutes** — almost always a GitHub-side
  runner-queue delay or an Actions incident, not the workflow. Check
  [githubstatus.com](https://www.githubstatus.com/); if Actions is degraded, **wait** (retrying doesn't
  summon runners) and hold off on the real tag until it's green. Public repos have free, unlimited Actions
  minutes, so it's not a quota issue.
- **Configure fails downloading the lib (`platemaker-dev-…zip` 404 / FATAL_ERROR)** — the pinned
  `LIBPLATEMAKER_VERSION` has no matching **published** lib Release asset. Publish the lib release first,
  or fix the pin (see Pre-flight).
- **Qt install fails / `aqt` errors** — the workflow drives **aqtinstall from git master** because Qt
  6.11's repo layout isn't handled by aqt's latest release (issue #1007); if it breaks, check that and the
  Qt MinGW tool id (`tools_mingw1310` for Qt 6.11.1 — `aqt list-tool windows desktop tools_mingw`). Revert
  to `install-qt-action` once a fixed aqt (> 3.3.0) ships.
- **Config-package guard FATAL_ERRORs on the lib** — an ABI mismatch between the Qt-MinGW toolchain and
  the downloaded lib archive (both should be MinGW/GCC; this is what works locally). Confirm the lib asset
  is the **MinGW** dev zip.
