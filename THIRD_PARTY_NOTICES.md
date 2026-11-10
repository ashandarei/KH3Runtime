# Third-party notices

First-party DLL code and tools are MIT licensed; see `LICENSE`. Third-party
code, adapted portions, and separately licensed technology retain their own terms.

## UE4SS and its dependencies

The reviewed UE4SS source revision is pinned in `CMakeLists.txt`. Its MIT license
is preserved in `licenses/LICENSE-UE4SS.txt`. The existing consolidated dependency
notices are preserved in `licenses/UE4SS-THIRD-PARTY-NOTICES.txt`, including the
upstream attributions for dependency components used by the runtime toolchain.
That notice collection also describes the separate loader release; this project's
release archive does not bundle UE4SS, Microsoft DirectX/XInput, or loader fonts.

UE4SS's Unreal/UEPseudo dependency is subject to Epic Games' licensing terms.
The MIT licenses of UE4SS's main repository and this project's first-party code
do not apply to UEPseudo. UEPseudo source is excluded from the release archive.
See https://docs.ue4ss.com/dev/contributing.html#uepseudo-code-licensing.

## JSON and audio

JSON for Modern C++ and stb are pinned and verified by the CMake dependency
modules. Their notices are preserved in `licenses/LICENSE-nlohmann-json.txt`
and `licenses/LICENSE-stb.txt`. The latter preserves stb's offered license choices.

## Adapted KH3 randomizer behavior and data

Portions of the encounter catalog and deterministic shuffle behavior are adapted
from WaterKH/KH3RandomizerClient. The original Apache-2.0 terms and credits remain
in `licenses/LICENSE-KH3RandomizerClient.txt` and
`licenses/UPSTREAM-KH3RandomizerClient-CREDITS.txt`. The project's MIT license
does not remove those notices or replace the applicable upstream terms.

## Unreal Engine and game content

Licensed UE4.17 Core headers are supplied by the developer through
`KH3AP_UE417_CORE_ROOT`. No engine header tree, game executable, original game
package, movie media, or Microsoft runtime DLL is included in this repository
or the DLL release archive. Authored helper movies and patch descriptions use
original game content only from the installed game at runtime where required.
Unreal Engine technology remains subject to Epic's license terms:
https://www.unrealengine.com/eula/unreal
These terms also apply to engine code compiled from header templates into the DLL.

The optional UI authoring workflow invokes a separately installed Java/JPEXS
compiler. That compiler is not bundled or linked into the runtime DLL.
