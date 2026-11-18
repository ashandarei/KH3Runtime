"""Compile the production freefall entry with deterministic engine boundaries."""
from pathlib import Path
import sys

player = Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/player'


def extract(source, signature):
    start = source.index(f'{signature}(')
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


floating = (player / 'movement/character_floating.cpp').read_text()
camera = (player / 'characters/character_camera.cpp').read_text()
functions = [extract(floating, signature) for signature in (
    'void notify_floating', 'void begin_tornado_phase', 'void begin_tornado',
    'void begin_vacuum', 'void end_vacuum', 'bool restore_camera_view',
)]
restore = extract(camera, 'bool restore_active_view')
Path(sys.argv[1]).write_text('namespace character_camera {\n' + restore + '\n}\n' + '\n'.join(functions))
