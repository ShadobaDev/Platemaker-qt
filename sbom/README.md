# SBOM snapshot for GitHub's Dependency graph

`sbom.spdx.json` is a **committed snapshot** of the app's Software Bill of Materials (SPDX 2.3), used
only to feed GitHub's **Dependency graph** (Insights → Dependency graph) and Dependabot alerts.

GitHub cannot read dependencies from CMake (`find_package` for Qt / libplatemaker, the prebuilt libvips
zip, FetchContent), so the graph would otherwise be empty. The
[`.github/workflows/dependency-submission.yml`](../.github/workflows/dependency-submission.yml)
workflow submits this file through the Dependency Submission API on every push that touches `sbom/`.

This is the superset SBOM: it lists **Qt, libplatemaker, libvips and nlohmann/json**.

## When to regenerate

Regenerate this file whenever a dependency version changes — a pinned one (libvips, nlohmann/json), the
libplatemaker pin (`LIBPLATEMAKER_VERSION`), the app's own version, or the **Qt** version (which
reflects the build kit, so it changes when you build with a different Qt). The canonical copy is
produced by the build, e.g.:

```
build/Desktop_Qt_6_11_1_MinGW_64_bit-Release/credits/sbom.spdx.json
```

Copy that over `sbom/sbom.spdx.json` and commit.

> Note: enabling the graph needs *Dependency graph* turned on in the repo settings — on by default
> for public repositories. CVE alerts additionally need *Dependabot alerts* enabled in
> Settings → Code security.
