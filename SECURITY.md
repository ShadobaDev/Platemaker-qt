# Security Policy

## Supported versions

Platemaker is a small project maintained by one person. Only the **latest released
version** receives fixes; there are no long-term support branches.

| Version | Supported |
|---|---|
| Latest release | ✅ |
| Anything older | ❌ — please update first |

## Reporting a vulnerability

**Please report security issues privately, not as a public issue.**

Use GitHub's private reporting form:
**<https://github.com/ShadobaDev/Platemaker-qt/security/advisories/new>**

Only the maintainer can see reports submitted this way, and nothing becomes public
until an advisory is published.

Please include, as far as you can:

- what the issue is and how to reproduce it,
- the Platemaker version (*Help → About*) and your Windows version,
- the SHA-256 hash of the file involved, if the report concerns a binary,
- anything you already know about impact.

**What to expect.** This is a hobby project, so please allow a few days for a first
response. You will get an acknowledgement, a discussion of the issue, and credit in
the advisory and release notes unless you prefer to stay anonymous.

## What is in scope

Platemaker is an **offline desktop application**. It reads and writes local image and
project files chosen by the user, performs no network communication, collects no data,
and ships without the Qt networking and TLS plugins. That rules out whole classes of
vulnerability, but the following are very much in scope:

- memory-safety issues reachable by opening a **malicious or malformed image file**
  (the image pipeline is native C++ on top of libvips),
- anything allowing code execution when opening a crafted **project/workspace file**,
- DLL hijacking or search-order attacks against the installed application,
- tampering with, or spoofing of, the released installer.

Vulnerabilities in bundled third-party libraries (Qt, libvips and its dependencies) are
best reported to those projects directly, but tell us as well so the bundled version can
be updated.

## Verifying a download

Every release publishes SHA-256 checksums and a GitHub build-provenance attestation. How
to verify a download — and how code signing is handled — is documented in the
[code signing policy](docs/CODE-SIGNING-POLICY.md).

**Note on antivirus warnings:** unsigned binaries from small projects are routinely
flagged by machine-learning heuristics without anything actually being detected. If your
antivirus flags an official download, that is a false positive rather than a security
issue — see the code signing policy, and feel free to raise it as a normal
[public issue](https://github.com/ShadobaDev/Platemaker-qt/issues).
