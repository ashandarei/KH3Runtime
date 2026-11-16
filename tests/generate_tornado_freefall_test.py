"""Compile the production freefall entry with deterministic engine boundaries."""
from pathlib import Path
import sys

source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/player/movement/character_floating.cpp').read_text()
functions = []
for signature in ('void notify_floating', 'void begin_tornado_phase', 'void begin_tornado',
                  'void begin_vacuum', 'void end_vacuum', 'bool restore_camera_view'):
    start = source.index(f'{signature}(')
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    functions.append(source[start:end])
Path(sys.argv[1]).write_text('\n'.join(functions))
