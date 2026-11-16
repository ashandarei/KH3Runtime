"""Compile the production player resolvers with deterministic engine boundaries."""
from pathlib import Path
import sys
source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/player/context/current_player.cpp').read_text()
body = source[source.index('    namespace\n'):source.rfind('}')]
body = body.replace('RC::Unreal::FWeakObjectPtr', 'Weak')
Path(sys.argv[1]).write_text(body)

root = Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/party'
methods = []
for file, signature in [('party_reconciliation.cpp', 'auto RuntimePartyMemberService::observe_goa_map_ready('), ('party_world_context.cpp', 'auto RuntimePartyMemberService::observe_sora_context_ready(')]:
    source = (root/file).read_text()
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    methods.append(source[start:end].replace('RuntimePartyMemberService::', ''))
Path(sys.argv[1]).with_name('party_entry_under_test.h').write_text('\n'.join(methods))

source = (root / '../story/save_points/goa_autosave_service.cpp').read_text()
methods = []
for signature in ['auto is_goa_01_context_active(', 'auto queue_after_manual_save(',
                  'auto process_pending(', 'auto create_after_manual_save(']:
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    methods.append(source[start:end])
Path(sys.argv[1]).with_name('goa_autosave_under_test.h').write_text('\n'.join(methods))

# Exercise the command handler's actual write gate with the production resolver.
source = (root / '../story/save_points/save_menu_command_helpers.cpp').read_text()
start = source.index('        if (deps.runtime_state.grant_state().special_battle_mode_active())')
end = source.index('        if (is_world_map_progress_command(request.command) || is_set_game_flag_command(request.command))', start)
Path(sys.argv[1]).with_name('recovery_player_gate_under_test.h').write_text(source[start:end])
