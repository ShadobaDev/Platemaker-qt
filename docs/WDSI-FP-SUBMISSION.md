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

## Submission log

Which release hashes have been reported to Microsoft and cleared. Every new release is a new hash — add a
row each time you submit, and update the status when Microsoft's verdict lands.

| Version | File | Detection reported | Submitted | Status |
|---|---|---|---|---|
| 1.4.3 | `Platemaker-1.4.3-Setup.exe` (`0af852f…6a7`) | `Trojan:Win32/Wacatac.C!ml` | 2026-08-23 | **Cleared by Microsoft** (2026-08-25) |

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

**Confirmed for 1.4.3** (`0af852fec2a720cc2ccffdc408a653c11941f43c1156a5984eecb6b66ef786a7`,
checked 2026-08-23): Microsoft reports **`Trojan:Win32/Wacatac.C!ml`** on the installer.
VirusTotal permalink for that hash — paste this into the justification:
<https://www.virustotal.com/gui/file/0af852fec2a720cc2ccffdc408a653c11941f43c1156a5984eecb6b66ef786a7>

The other two engines flagging this file are DeepInstinct and SecureAge. (VirusTotal's web
UI labels the latter **SecureAge**, while the API returns it under the key **APEX** —
SecureAge's engine is named APEX. Same engine, two labels; don't mistake it for a fourth
detection.) Only the **Microsoft** verdict matters for a WDSI submission — ignore the rest.

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
Platemaker is a free, open-source (GPL-3.0) desktop application for comic and webtoon
artists. I am its developer and maintainer. It contains no malicious code, and none was
placed in it, knowingly or otherwise.

The build is verifiable end to end: this binary is produced by a public GitHub Actions
workflow and carries an actions/attest-build-provenance attestation tying it to the exact
public source commit it was built from.
- Source:     https://github.com/ShadobaDev/Platemaker-qt
- Release:    https://github.com/ShadobaDev/Platemaker-qt/releases/tag/<ver>
- VirusTotal: <permalink>

The file is an Inno Setup installer wrapping a C++ application built with MSVC 2022 and its
dependency DLLs. The installer's payload is compressed, which accounts for the file's high
entropy; there is no packer and no obfuscation, and VirusTotal reports none. All three of
VirusTotal's sandboxes report no malicious behaviour, and Zenbox returns an explicit CLEAN
verdict: no network traffic, no persistence, no services created.

Please re-evaluate and clear this hash.
```

> **Submitting `Platemaker.exe` instead of the installer?** Replace the first sentence of
> that last paragraph with: *"The file is a C++ application built with MSVC 2022."* — and
> drop the clause about compressed payload and entropy, which applies only to the installer.

## The other two engines — same file, separate submissions

Microsoft is the one that matters most to users, but it is not the only engine flagging the
installer. Both others accept false-positive reports directly, and neither needs a rebuild —
the same hash and the same justification text work:

| Engine | Where to report |
|---|---|
| **Deep Instinct** | `vt-fps-requests@deepinstinct.com` — email, specifically for VirusTotal false positives |
| **SecureAge APEX** | <https://www.secureage.com/contact-us> (use the bottom option), or the form at <https://www.secureaplus.com/features/antivirus/report-false-positive/> |

For both, send the **same justification text** as above, with the detection name changed to
what that engine reports (Deep Instinct: `MALICIOUS`; SecureAge: `Malicious` — neither uses a
family name), plus the SHA-256 and the VirusTotal permalink. For the email, a subject line of
`False positive — Platemaker-<ver>-Setup.exe — SHA-256 <hash>` is enough.

Same caveat as Microsoft: clearance is **per hash**, so every release needs the round again.
That is precisely the recurring cost a code-signing certificate removes
([code signing policy](CODE-SIGNING-POLICY.md)).

## After submitting
- Turnaround ~24–72 h; verdict lands on the dashboard/email.
- Clears **only the submitted hash(es)** → resubmit for every release (this is on the
  release checklist in [`GITHUB-RELEASE.md`](GITHUB-RELEASE.md) §4).
- Including the VirusTotal permalink + the provenance link makes the analyst's call easy.
