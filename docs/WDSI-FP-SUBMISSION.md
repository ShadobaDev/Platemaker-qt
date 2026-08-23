# Clearing a Defender false positive (Microsoft WDSI)

The Windows installer and `Platemaker.exe` are **unsigned**, which periodically trips
Microsoft Defender's machine-learning heuristic (`Trojan:Win32/Wacatac.*!ml`) — a false
positive on a low-prevalence, unsigned binary, not a real detection. This page is the
procedure to report it to Microsoft.

**What a WDSI submission does — and doesn't.** It clears the *specific file hashes you
submit*, usually within ~24–72 h. It is **per-hash and temporary**: every new release
produces new hashes and must be resubmitted. It also only sways the **Microsoft** engine —
other ML resellers (DeepInstinct, SecureAge) flag unsigned binaries regardless. The only
durable fix is a real code-signing certificate from a trusted CA (see
[TODO → *Free code signing via SignPath.io OSS* / the Certum note](TODO.md) and
[`temp/code-signing-options.md`](../temp/code-signing-options.md)). WDSI is the bridge
until then.

## Which files to submit
Submit the **exact artifacts users download from the GitHub Release** — clearance is tied
to the file hash, so a locally-built copy (different hash) won't help real users.

1. **Primary:** `Platemaker-<ver>-Setup.exe`, downloaded fresh from the release page
   (`https://github.com/ShadobaDev/Platemaker-qt/releases/tag/<ver>`).
2. **If the bare exe still flags:** `Platemaker.exe` as installed *from that release
   installer* (install it, then take `…\Platemaker\bin\Platemaker.exe`). Do **not** submit
   your local Qt Creator build — wrong hash.

Submit each flagged file as a **separate** submission.

## Before you submit — confirm the current detection name
Detection names drift. Read the exact current one from either:
- Windows Defender **Protection history** (names e.g. `Trojan:Win32/Wacatac.B!ml`), or
- a fresh VirusTotal scan of that exact file (the Microsoft engine's verdict string).

Seen historically: installer → `Wacatac.B!ml`; bare exe → `Wacatac.C!ml`. Use whatever the
file shows now.

## Steps
1. Go to **https://www.microsoft.com/en-us/wdsi/filesubmission**
2. **Sign in with a Microsoft account** (gives a tracking dashboard + status emails).
3. Submitter type: **Software developer** (routes to the developer FP process).
4. **Upload the file** (installer ~30 MB — within limits).
5. "What do you believe this file is?" → **Incorrectly detected (false positive)**.
6. Detection name: paste the exact string confirmed above.
7. Paste the justification below into the details field.
8. Submit; repeat for the second file if needed. Watch the dashboard for the verdict.

## Justification text (paste into the details field)
Replace `<ver>` and the VirusTotal permalink.

```
This file is the official release installer of Platemaker, a free, open-source desktop
application for comic/webtoon artists (joins and slices artwork into upload-ready panels).
I am the developer and maintainer.

I believe the detection (Trojan:Win32/Wacatac.*!ml) is a machine-learning false positive
on an unsigned, low-prevalence binary. The application contains no malicious functionality:
it runs fully offline, performs no network communication (the Qt Network/TLS plugins are
deliberately excluded from the build), collects no user data, and only reads/writes local
image and project files chosen by the user.

Provenance and verifiability:
- Source code (GPL-3.0), fully public: https://github.com/ShadobaDev/Platemaker-qt
- This exact artifact is produced by a public GitHub Actions release pipeline with a
  build-provenance attestation (actions/attest-build-provenance) and published SHA-256
  checksums.
- Release page: https://github.com/ShadobaDev/Platemaker-qt/releases/tag/<ver>
- VirusTotal report for this file: <paste the VirusTotal permalink for THIS hash>

The binary is built with MSVC 2022, is not packed, and uses no obfuscation. Please
re-evaluate and whitelist this hash. Thank you.
```

## After submitting
- Turnaround ~24–72 h; verdict lands on the dashboard/email.
- Clears **only the submitted hash(es)** → resubmit for every release (this is on the
  release checklist in [`GITHUB-RELEASE.md`](GITHUB-RELEASE.md) §4).
- Including the VirusTotal permalink + the provenance link makes the analyst's call easy.
