"""Compile the production vanilla capture with a deterministic reflection boundary."""
from pathlib import Path
import sys

source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/party/party_roster.cpp').read_text()
start = source.index('auto RuntimePartyMemberService::capture_vanilla_party(')
end = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
Path(sys.argv[1]).write_text(source[start:end].replace('RuntimePartyMemberService::', ''))
