"""Compile the production viewport dispatch with deterministic input owners."""
from pathlib import Path
import sys

source = (Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/ui/options_menu/viewport.cpp').read_text()
def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


Path(sys.argv[1]).write_text('\n'.join((
    method('auto RuntimeOptionsMenuService::Impl::dispatch_key('),
    method('auto xinput_title_chord('),
    method('void RuntimeOptionsMenuService::Impl::check_controller_title_shortcut('),
)))
