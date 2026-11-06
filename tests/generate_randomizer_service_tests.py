"""Compile engine-bound service logic against a deterministic engine boundary.

Only reflection, object discovery, audio output and hook registration are faked.
Production method bodies and state declarations are copied verbatim (apart from
the test class name), so regressions exercise the orchestration, not a duplicate
implementation. The normal probe build separately validates the real UE4SS ABI.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1] / "KH3APProbeCpp" / "kh3ap" / "features"
OUT = Path(sys.argv[1]).resolve()
OUT.mkdir(parents=True, exist_ok=True)


def block(text: str, marker: str) -> str:
    start = text.index(marker)
    # Ignore braces inside comments and quoted strings while finding the body.
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda match: " " * len(match[0]), text)
    pos = masked.index("{", start) + 1
    depth = 1
    while depth:
        depth += (masked[pos] == "{") - (masked[pos] == "}")
        pos += 1
    return text[start:pos]


def constant(text: str, name: str) -> str:
    declaration = re.search(r'constexpr[^;\n]*\b' + name + r'\b[^;]*;', text)
    assert declaration, name
    return declaration[0] + "\n"


impl = (ROOT / "music/shuffle/bgm_shuffle_service_impl.h").read_text(encoding="utf-8")
declarations = impl[impl.index("namespace kh3ap::bgm_shuffle_detail"):]
declarations = declarations.replace("RuntimeBgmShuffleService", "TestBgmShuffleService")
(OUT / "music_service_under_test.h").write_text(
    '#pragma once\n#include "randomizer_runtime_stubs.h"\n#include "kh3ap/features/music/now_playing/now_playing_title.h"\n'
    'namespace kh3ap { struct TestBgmShuffleService { struct Impl; }; }\n' + declarations)

service = (ROOT / "music/shuffle/bgm_shuffle_service.cpp").read_text(encoding="utf-8")
details = "\n".join(block(service, "auto " + name + "(") for name in
                    ["lower", "world_code_of", "persistent_level_package", "load_asset_name_for_level"])
methods = "\n".join(block(service, "auto RuntimeBgmShuffleService::Impl::" + name + "(") for name in [
    "tick", "tick_steps", "at_home", "active", "refresh_table", "replacement_for",
    "intercept_requested_track", "note_slot_restart",
    "remember_slot_rewrite",
    "refresh_world_context", "report_waiting", "bgm_manager",
    "original_value", "rewrite", "swap_slot", "apply_on_level_change", "swap_playing_originals", "swap_group_tracks",
])
source = '#include "music_service_under_test.h"\n'
source += ("namespace kh3ap::bgm_shuffle_detail {\n" +
           constant(service, "music_object_readiness_retry") + details + "\n}\n")
source += "namespace kh3ap {\n" + methods + "\n}\n"
debug = (ROOT / "music/shuffle/bgm_shuffle_debug.cpp").read_text(encoding="utf-8")
source += "namespace kh3ap {\n" + "\n".join(
    block(debug, "auto RuntimeBgmShuffleService::Impl::" + name + "(")
    for name in ["log_music_changes", "report_slots"]) + "\n}\n"
for name in ["shuffle/bgm_shuffle_custom.cpp", "shuffle/bgm_shuffle_forms.cpp"]:
    text = (ROOT / "music" / name).read_text(encoding="utf-8")
    source += text[text.index("namespace kh3ap"):]
source = source.replace("RuntimeBgmShuffleService", "TestBgmShuffleService")
(OUT / "music_service_under_test.cpp").write_text(source)

# Lifecycle discovery is compiled directly in music_object_registry_test.

gummi = (ROOT / "colors/color_gummi.cpp").read_text(encoding="utf-8")
source = '#pragma once\n#include "randomizer_runtime_stubs.h"\nnamespace kh3ap {\n'
source += 'using namespace RC; using namespace RC::Unreal;\n'
source += 'inline constexpr bool s_debug_logging = false;\n'
for name in ["color_key", "palette_use_parameter", "palette_index_parameter", "custom_color_parameter",
             "slots_per_tick", "settle_time", "gummi_color_discovery_retry"]:
    source += constant(gummi, name)
for marker in ["auto palette_index_color(", "struct Original", "enum class ShipStatus", "struct Ship\n"]:
    source += block(gummi, marker) + ";\n"
begin = gummi.index("struct RuntimeColorGummi::Impl")
members = gummi[gummi.index("{", begin) + 1:gummi.index("        auto reset_level()", begin)]
source += 'struct TestColorGummi {\n' + members
source += '''
        UObject* editor_ship() { return fake_runtime::editing; }
'''
for name in ["reset_level", "refresh_objects", "track", "settle", "paint_slot", "tick", "restore_all", "repaint_all"]:
    source += block(gummi, "auto " + name + "(") + "\n"
source += '};\n}\n'
# Keep named production constants used by the extracted bodies. This also
# follows constants introduced by readability refactors without duplicating
# their numeric values in the test boundary.
additional_constants = ''.join(
    constant(gummi, name)
    for name in re.findall(r'\bconstexpr auto (\w+)\s*=', gummi)
    if re.search(r'\b' + name + r'\b', source)
    and not re.search(r'\bconstexpr[^;\n]*\b' + name + r'\b', source)
)
source = source.replace('namespace kh3ap {\n', 'namespace kh3ap {\n' + additional_constants, 1)
(OUT / "gummi_service_under_test.h").write_text(source, encoding='utf-8')
