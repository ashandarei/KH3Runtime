"""Compile actual chest dispatch/maintenance methods with engine and IO faked."""
from pathlib import Path
import re
import sys

treasure = Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/treasure'
source = (treasure / 'music_chest/music_chest_service.cpp').read_text(encoding='utf-8')
pause_source = (treasure / 'music_chest/music_chest_pause.cpp').read_text(encoding='utf-8')
constants_source = (treasure / 'music_chest/music_chest.h').read_text(encoding='utf-8')
constants_source += source[:source.index('    RuntimeMusicChestService::Impl::Impl')]

def method(name):
    pattern = r'\b(?:void|bool)\s+(?:RuntimeMusicChestService::Impl::)?' + re.escape(name) + r'\s*\('
    implementation = pause_source if name in ('pause_active', 'update_pause') else source
    match = re.search(pattern, implementation)
    if match is None:
        raise RuntimeError(f'Missing maintained music-chest implementation: {name}')
    start = match.start()
    end = implementation.index('{', start) + 1
    depth = 1
    while depth:
        depth += (implementation[end] == '{') - (implementation[end] == '}')
        end += 1
    extracted = implementation[start:end].replace('RuntimeMusicChestService::Impl::', '')
    if name == 'update_pause':
        # Substitute only the engine-owned audio endpoint; exercise the actual
        # pause lifecycle body with the same controllable IO used by this test.
        extracted = 'template<class AudioEndpoint>\n' + extracted.replace('RuntimeMoMAudio&', 'AudioEndpoint&')
    return extracted

out = Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
extracted = '\n'.join(method(name) for name in (
    'NotifyUObjectDeleted', 'watch_gates', 'pause_active', 'update_pause', 'tick', 'maintain'))
constants = ''.join(
    f'static constexpr auto {key} = {value};\n'
    for key, value in re.findall(r'^\s*(?:inline\s+)?constexpr auto (\w+) = ([^;\n]+);', constants_source, re.M)
    if re.search(r'\b' + key + r'\b', extracted)
)
out.write_text(constants + extracted, encoding='utf-8')
