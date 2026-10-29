# CI and releases

[简体中文](ci.md) | [English](ci_en.md)

The workflow is defined in [build.yml](../../.github/workflows/build.yml). Packaging is implemented in [package.ps1](../package.ps1).

## Triggers

| Action | Builds and tests | Actions artifacts | GitHub Release |
| --- | --- | --- | --- |
| Push to `main` | Regular and Trace builds | Retained for 14 days | Not created |
| Pull request targeting `main` | Regular and Trace builds | Retained for 14 days | Not created |
| Manual workflow run | Regular and Trace builds | Retained for 14 days | Not created |
| Push a version tag such as `v1.0.0` | Regular and Trace builds | Retained for 14 days | Published after both builds pass |

Version tags use `vMAJOR.MINOR.PATCH`, optionally followed by a prerelease suffix such as `-rc.1` or `-beta.1`. Releases with a suffix are marked as prereleases and are not marked Latest. Invalid tags stop the build. The workflow does not create Git tags itself.

Branch builds use `ci-<run-number>-<short-commit-hash>` as the package version. Tag builds use the complete tag name.

## Builds and tests

Build jobs run on `windows-2022` using its MSVC, MASM, and Windows SDK installation. Regular and Trace configurations run on separate runners:

1. `build.ps1` compiles the x64 DLL, probe, and test window, then runs the COM and dispatch unit tests.
2. `tests/install_tests.ps1` uses a simulated helper to test installation, removal, and file protection.
3. `package.ps1` creates the release ZIP and SHA-256 checksum file.
4. `tests/package_tests.ps1` checks the archive hash, binary hashes against build outputs, directory layout, and documentation links. It then runs installation tests again from the extracted package.
5. The verified ZIP and checksum file are uploaded as Actions artifacts.

CI does not run `validate.ps1`. That script requires a Windows 10 native-interface baseline and an interactive desktop; a hosted Windows Server build environment does not replace real screenshot regression testing. Before a formal release, complete the [validation procedure](validation_en.md) on the target Windows 10 environment.

The workflow defaults to read access to repository contents. Only the tagged release job receives `contents: write`, using GitHub's built-in `GITHUB_TOKEN`. Actions dependencies are pinned to commit SHAs. When upgrading them, verify both the version comment and the pinned commit.

## Downloading and installing

Download published versions from [Releases](https://github.com/MagicalAstrogy/CodexComputerUseFix/releases). Use the regular ZIP for everyday operation and the ZIP with a `-trace` suffix for diagnostics.

Each version provides four files. For example, `v1.0.0` produces:

```text
CodexCaptureCompat-v1.0.0-windows-x64.zip
CodexCaptureCompat-v1.0.0-windows-x64.zip.sha256
CodexCaptureCompat-v1.0.0-windows-x64-trace.zip
CodexCaptureCompat-v1.0.0-windows-x64-trace.zip.sha256
```

The Actions page also provides `packages-windows-x64-release` and `packages-windows-x64-trace`. After downloading an Actions artifact, extract GitHub's outer archive first to access the project's ZIP and checksum file.

Place the ZIP and its `.sha256` file in the same directory and verify them with PowerShell:

```powershell
$archive = '.\CodexCaptureCompat-v1.0.0-windows-x64.zip'
$expected = ((Get-Content -LiteralPath "$archive.sha256" -Raw).Trim() -split '\s+', 2)[0]
$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
if ($actual -ine $expected) { throw 'SHA-256 mismatch.' }
Expand-Archive -LiteralPath $archive -DestinationPath .\CodexCaptureCompat-v1.0.0
```

The extracted package preserves the source layout. Its root contains the Chinese and English READMEs and LICENSE; `capture-compat/dist/` contains the three binaries, alongside the installation scripts, source, tests, and documentation. Enter `capture-compat` and follow the [installation guide](../README_en.md) to deploy the DLL. Extract regular and Trace packages into separate directories to avoid mixing their files.

## Publishing a version

First push the workflow and the source revision you want to release to `main`, and confirm that CI passes for that commit. Create an unused version tag on that commit and push it:

```powershell
# Example version; replace it with the version being released.
git tag -a v1.0.0 -m 'Release v1.0.0'
git push origin v1.0.0
```

The tag triggers a fresh build. The release job downloads both configurations' artifacts, verifies their SHA-256 checksums, then runs `gh release create --verify-tag --generate-notes` to create a Release with all four files. GitHub generates the release notes.

Only a tag push publishes a release. Manually running the workflow on a tag still produces only Actions artifacts. If a run fails before publication, it can be rerun from the Actions page. If the tag already has a Release, creation fails without overwriting that release or its assets. Publish updated content under a new version tag.

## Local packaging

Run these commands from the `capture-compat` directory:

```powershell
# Regular build: build, package, then verify.
.\build.ps1
$package = .\package.ps1 -Version v1.0.0
.\tests\package_tests.ps1 -ArchivePath $package.archive

# Diagnostic packages require the corresponding -Trace build.
.\build.ps1 -Trace
$tracePackage = .\package.ps1 -Version v1.0.0 -Trace
.\tests\package_tests.ps1 -ArchivePath $tracePackage.archive
```

`package.ps1` packages the existing `dist/` files without rebuilding them. `-Trace` selects the diagnostic package name and must match the most recent build option. Both configurations share `dist/`, so follow the build, package, and verify sequence separately for each configuration. Rebuilding replaces the files in that directory.

| Parameter | Default | Purpose |
| --- | --- | --- |
| `-Version` | `dev` | Version in the package name; accepts letters, digits, dots, plus signs, and hyphens, starting with a letter or digit |
| `-Trace` | Disabled | Adds the `-trace` package suffix |
| `-OutputDirectory` | `artifacts/` at the repository root | Output directory for the ZIP and checksum file |

Packaging refuses to overwrite an existing ZIP or checksum file with the same name. Use a new version or output directory. The temporary staging directory is cleaned up afterward; installation and extraction fixtures remain under `validation/`. Packages exclude intermediate build files, validation records, screenshots, Git history, and migration copies.
