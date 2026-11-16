"""Exercise the production party event receiver and helm transitions."""
from pathlib import Path
import sys

source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/party/party_world_context.cpp').read_text()
methods = []
for name in ('observe_state_write', 'observe_sora_state'):
    start = source.index(f'auto RuntimePartyMemberService::{name}(')
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    methods.append(source[start:end].replace('RuntimePartyMemberService::', ''))
Path(sys.argv[1]).write_text('\n'.join(methods))
