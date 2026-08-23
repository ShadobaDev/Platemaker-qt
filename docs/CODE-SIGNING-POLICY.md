# Code signing policy

Platemaker is a free, open-source desktop application for comic and webtoon artists. It is
developed in the open, built by a public CI pipeline, and published on GitHub Releases.
This page describes who can change the code, how release binaries are produced, and how
they are (or will be) signed — so that anyone can judge whether a file claiming to be
Platemaker really is.

---

## Project and repositories

| | |
|---|---|
| **Application** | Platemaker (Windows desktop, Qt Widgets) |
| **Application repository** | <https://github.com/ShadobaDev/Platemaker-qt> |
| **Library repository** | <https://github.com/ShadobaDev/PlateMaker> (`libplatemaker`, bundled in the installer) |
| **Downloads** | GitHub Releases of the repositories above — no other official channel |
| **Licence** | Application GPL-3.0; `libplatemaker` LGPL-3.0; `platemaker-cli` GPL-3.0 |

Platemaker is licensed **exclusively under open-source licences**. There is no commercial,
proprietary or dual-licensed edition of any component. (The repositories' `CLA.md` reserves
the *option* to relicense in future; it does not create such an edition, and none exists.)

---

## Signed artifacts

Once signing is active, these are the artifacts covered by this policy:

| Artifact | Produced by |
|---|---|
| `Platemaker-<version>-Setup.exe` (Inno Setup installer) | `Platemaker-qt` release workflow |
| `Platemaker.exe` (the application, inside the installer) | `Platemaker-qt` release workflow |
| `platemaker.dll` (`libplatemaker`, bundled) | `PlateMaker` release workflow |
| `platemaker-cli.exe` (standalone CLI) | `PlateMaker` release workflow |

Third-party runtime libraries redistributed inside the installer (Qt, libvips and its
dependencies, the Microsoft Visual C++ runtime) keep whatever signatures their own
publishers applied; this project does not re-sign them.

---

## Team roles

Platemaker is maintained by a single developer. All three code-signing roles are therefore
held by the same person, which is stated explicitly rather than left implied:

| Role | Who | Responsibility |
|---|---|---|
| **Author** (committer) | Bartłomiej Mucha ([@ShadobaDev](https://github.com/ShadobaDev)) | Trusted to modify the source code directly, without additional review. |
| **Reviewer** | Bartłomiej Mucha | Reviews every change proposed by anyone who is not a committer, before it is merged. External contributions arrive only as pull requests and are never merged unreviewed. |
| **Approver** | Bartłomiej Mucha | Approves each individual signing request. Signing is never automatic or unattended. |

Contributions from non-committers are additionally governed by the
[Contributor License Agreement](../CLA.md) and the
[Code of Conduct](../CODE_OF_CONDUCT.md).

---

## How release binaries are built

Release artifacts are **never** built or uploaded from a developer workstation. Every
published binary is produced by a public, source-controlled GitHub Actions workflow:

- **Trigger:** pushing a bare version tag (e.g. `1.4.3`) runs
  [`.github/workflows/release.yml`](../.github/workflows/release.yml). The publishing steps
  are gated on the tag, so a manual run validates the build without releasing anything.
- **Determinism:** the build is fully determined by configuration under source control —
  `CMakeLists.txt`, `Platemaker.iss` and the workflow file. There are no manually supplied
  build parameters and no post-build hand edits.
- **Toolchain:** MSVC 2022 on a `windows-2022` runner, Qt from `aqtinstall`, installer built
  by Inno Setup. The bundled `libplatemaker` is downloaded as a versioned, checksummed
  release asset from the library repository.
- **Provenance:** each release attaches a
  [build-provenance attestation](https://github.com/ShadobaDev/Platemaker-qt/attestations)
  (`actions/attest-build-provenance`), cryptographically tying the artifact to this
  repository, commit and workflow run.
- **Checksums:** `Platemaker-<version>-SHA256SUMS.txt` is published alongside each installer.
- **Malware scan:** release assets are submitted to VirusTotal from the same workflow.

Signing, once active, happens **inside this pipeline** — after the build, before publication —
so that the file users download is exactly the file that was signed.

---

## Verifying a release

**Check the checksum** (works today, signed or not). Compare against the published
`SHA256SUMS.txt`:

```powershell
Get-FileHash .\Platemaker-1.4.3-Setup.exe -Algorithm SHA256
```

**Check the build provenance** (works today) using the GitHub CLI:

```powershell
gh attestation verify .\Platemaker-1.4.3-Setup.exe --repo ShadobaDev/Platemaker-qt
```

**Check the signature.** Platemaker's binaries are **not yet code-signed** — right-click the
file → *Properties* → *Digital Signatures*, or:

```powershell
Get-AuthenticodeSignature .\Platemaker-1.4.3-Setup.exe
```

reports `NotSigned` today. That is expected and is not a sign of tampering: use the checksum
and attestation checks above instead, which verify the same thing by a different route. A
code-signing certificate is being pursued; when it is in place this page and the release
notes will say so, and the command above will report `Valid`.

### A note on antivirus warnings

Unsigned, newly published binaries from a small project are routinely flagged by
machine-learning antivirus heuristics (for example `Trojan:Win32/Wacatac.*!ml`) purely
because they are unsigned and rarely seen — not because anything was detected in them.
Independent dynamic analysis of the 1.4.3 installer by VirusTotal's sandboxes returned a
clean verdict, with no network activity, no persistence mechanisms and no unexpected file or
registry changes. Obtaining a real code-signing certificate is the durable fix, which is why
this policy exists.

---

## Privacy statement

**This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it.**

Platemaker runs entirely offline. It reads and writes only the local image and project files
the user chooses. It contains no telemetry, no analytics, no update check and no crash
reporting, and the Qt networking and TLS plugins are deliberately excluded from the build, so
the networking stack is not even shipped.

---

## Reporting a suspicious binary

**If your antivirus flagged an official download**, that is almost certainly the false positive
described above. Please open a normal public issue —
[github.com/ShadobaDev/Platemaker-qt/issues](https://github.com/ShadobaDev/Platemaker-qt/issues) —
so other users can see the answer too.

**If you believe a binary is genuinely malicious or tampered with**, please report it
*privately* instead, so it can be investigated before it is publicised:
[report a vulnerability](https://github.com/ShadobaDev/Platemaker-qt/security/advisories/new).

Either way, please include:

- the SHA-256 hash of the file,
- where you obtained it,
- the antivirus product and the exact detection name, if any.

Downloads from anywhere other than the GitHub Releases pages listed above are not official
and are not covered by this policy.
