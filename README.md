# KH3 Runtime

Standalone C++ mod for Kingdom Hearts III, including KH0.2 integration. Builds
`KH3APProbeCpp/main.dll` independently of the APWorld, client and hosting projects.

## Prerequisites

- Windows x64, Visual Studio 2022 with MSVC 14.38, and a Windows SDK.
- CMake 3.22 or newer, Ninja, Git, Python 3.11 or newer, and Rust/Cargo.
- Access to the UE4SS fork pinned in `CMakeLists.txt`.
- A licensed UEPseudo Git checkout containing the submodule commit pinned by UE4SS.
- Matching licensed UE4.17 `Engine/Source/Runtime/Core` headers for the file adapter.

Pillow and Java/JPEXS are needed only for UI resource authoring.

## Set up

Copy `.env.example` to `.env` and set:

```dotenv
KH3_DIR="path/to/your/EGS/KH3/runtime"
KH3AP_UE417_CORE_ROOT="path/to/licensed/Engine/Source/Runtime/Core"
KH3AP_UEPSEUDO_SOURCE="path/to/licensed/UEPseudo"
```

`KH3_DIR` is needed only for installation. Environment variables override `.env`;
`KH3_ENV_FILE` selects an alternate file. Keep local settings and dependencies out
of Git. Fetch the pinned UE4SS sources and check the toolchain:

```powershell
.\scripts\setup.ps1
.\scripts\build.ps1 -CheckSetup
```

`KH3AP_UEPSEUDO_SOURCE` must point to a Git checkout outside `vendor/ue4ss-goa`.
Setup initializes the Unreal submodule from that checkout at the pinned commit;
uncommitted source edits are not copied. `-UePseudoSource` overrides the setting
for setup and release builds. Setup fails if the required commit is unavailable.

UEPseudo requires the applicable Epic Games source license and repository access.
See the upstream [build instructions](https://docs.ue4ss.com/#build-instructions)
and [licensing terms](https://docs.ue4ss.com/dev/contributing.html#uepseudo-code-licensing).

## Build and test

Run from this repository root:

```powershell
.\scripts\build.ps1                     # Player DLL
.\scripts\build.ps1 -EnableDiagnostics  # Local diagnostics and automation
.\scripts\test.ps1                      # Native service and policy suites
```

Outputs are `build/KH3APProbeCpp/main.dll` and
`build-diagnostics/KH3APProbeCpp/main.dll`; use `-BuildDir` to override. Each build
validates dependencies and records DLL identity and build mode in adjacent metadata.
Player builds exclude diagnostics and agent automation.

For focused tests, use `-Suite services` or `-Suite policies`. Policy tests need
no game installation or licensed headers. Client integration tests use
`KH3_PIPE_TEST_HOST` set to the absolute path of the service suite's
`build-tests/services/pipe_transport_host.exe` (adjust for a custom build directory).

## Optional local installation

Close KH3 before updating its loaded DLL:

```powershell
.\scripts\build.ps1 -Install
```

This installs to `KH3_DIR` and verifies hashes. The game and compatible loader/mod
assets must already be installed. Add `-SyncUe4ss` only to intentionally update
the loader. Installation does not launch KH3.

## Package the DLL

Release builds require a clean working tree and a committed source revision:

```powershell
.\scripts\release.ps1 -SourceRef HEAD -Channel Beta
.\scripts\release.ps1 -SourceRef HEAD -Channel Stable -PublishMode Draft
```

Each run clones the selected commit into a new directory under
`artifacts/releases/`, prepares the pinned dependencies, runs catalog checks and
native tests, and builds a fresh player DLL. The selected revision must include
`scripts/release_record.py`. `VERSION.txt` supplies the version and default tag.
`-ReleaseTag` accepts the same version with an optional `v` prefix and, for beta
releases, a `-beta.N` suffix.

`-ValidateDiagnostics` adds a diagnostic compilation check; the package contains
the player build. `-CoreHeadersRoot`, `-UePseudoSource`, `-VsDevCmd`, and
`-VcVarsVer` select build inputs. `-OutputRoot` selects the output directory.

The archive includes `release-provenance.json`, also emitted alongside the ZIP.
It records source commit/tree, dependency revisions, compiler/linker/tool hashes
and versions, Windows SDK version, a fingerprint of external Core headers,
build options and completed checks. The header fingerprint contains an aggregate
hash and file count. The run directory retains the checkout, build output and
log. The record describes one build; byte-for-byte reproducibility and live-game
behavior are not verified by the release script.

The default `-PublishMode None` saves local artifacts. `Draft` creates a GitHub
draft release; `Publish` publishes a release.
Both require GitHub CLI authentication and the source commit already pushed to
the origin repository. `-Channel Beta` marks the release as a prerelease;
`Stable` marks it latest. Optional `-NotesFile` adds release notes. Existing
releases are never overwritten and existing tags must identify the selected
commit. Publication uploads
the ZIP and provenance record; the ZIP checksum is also printed in its notes.

To package an existing player build:

```powershell
.\scripts\package.ps1
```

Creates a player archive in `dist/` with the DLL, metadata, manifest, licenses and
a separate SHA-256 checksum. Packaging rejects diagnostic builds, stale metadata
and unreviewed dependencies. Game assets, engine headers, UE4SS binaries and local
settings are excluded. `-ProvenancePath` attaches a release record after verifying
that it matches the source revision, DLL and build metadata.

Before a public release, run the contribution checks and record live-game
validation separately. For self-hosted CI, protect the `native-build` environment
and review the workflow ref before approval.

## Project layout

| Path | Contents |
| --- | --- |
| `KH3APProbeCpp/` | Native code and embedded resources |
| `shared/`, `include/` | Protocol helpers and metadata headers |
| `data/`, `authoring/` | Catalogs, native profiles and UI sources |
| `scripts/`, `cmake/` | Build, test, generation and packaging tools |
| `tests/` | Native service and policy tests |

After editing catalogs, run `python scripts/generate_catalogs.py`; `--check`
verifies generated output. Builds check local inputs without importing APWorld.

## Further reading

- [Contributing](CONTRIBUTING.md)
- [Source guide](KH3APProbeCpp/kh3ap/README.md) · [Data contract](docs/DATA_CONTRACT.md)
- [KH0.2 runtime](docs/kh02-runtime.md) · [KH0.2 asset loading](docs/kh02-asset-loading.md)
- [UI authoring](docs/MOVIE_SOURCES.md) · [Character profiles](docs/CHARACTER_RUNTIME.md)

## License

First-party code, tests and tools use [MIT](LICENSE). See
[third-party notices](THIRD_PARTY_NOTICES.md) and `licenses/` for dependencies.
Unreal Engine headers and game content remain external and are not covered by MIT.
