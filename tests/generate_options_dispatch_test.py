"""Compile the production viewport dispatch with deterministic input owners."""
from pathlib import Path
import sys

source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/ui/options_menu/viewport.cpp').read_text()
start = source.index('auto RuntimeOptionsMenuService::Impl::dispatch_key(')
end = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
Path(sys.argv[1]).write_text(source[start:end])
