"""Compile production swim mutation/restoration and event methods against fake objects."""
from pathlib import Path
import sys

source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/party/party_swim_combat.cpp').read_text()


def extract(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end].replace('RuntimePartyMemberService::', '')


Path(sys.argv[1]).write_text('\n'.join(extract(s) for s in (
    'auto inherited_action_prefix(', 'auto owns_action_asset(',
    'template <typename State> bool remember_actions(',
    'void adapt_states(', 'void adapt(', 'template <typename State> void restore_state(')))
Path(sys.argv[2]).write_text('\n'.join(extract(s) for s in (
    'auto RuntimePartyMemberService::observe_friend_state(',
    'auto RuntimePartyMemberService::restore_party_swim_combat(')))
source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/party/party_ai.cpp').read_text()
Path(sys.argv[3]).write_text(extract('auto RuntimePartyMemberService::update_swimming_party_ai('))
source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/party/party_helpers.cpp').read_text()
Path(sys.argv[4]).write_text(extract('auto friend_underwater('))
