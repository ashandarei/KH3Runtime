"""Compile the production swim input route against controlled menu state."""
from pathlib import Path
import sys

source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/player/movement/character_traversal.cpp').read_text()
start = source.index('bool route(int controller,')
end = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
Path(sys.argv[1]).write_text(source[start:end])
