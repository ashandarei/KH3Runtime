"""Exercise production map-ready publication with fake engine/bridge boundaries."""
from pathlib import Path
import re
import sys

source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/player/context/player_context_readiness.cpp').read_text(encoding="utf-8")
start = re.search(r'auto\s+RuntimePlayerContextService::Impl::write_map_ready_marker\(', source).start()
end = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
Path(sys.argv[1]).write_text(source[start:end].replace("RuntimePlayerContextService::Impl::", ""), encoding="utf-8")

source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/player/controller_abilities/refresh.cpp').read_text(encoding="utf-8")
start = re.search(r'auto\s+RuntimeControllerAbilityService::Impl::request_refresh\(', source).start()
end = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
Path(sys.argv[1]).with_name('ability_ready_under_test.h').write_text(
    source[start:end].replace("RuntimeControllerAbilityService::Impl::", ""), encoding="utf-8"
)
