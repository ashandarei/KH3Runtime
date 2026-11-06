"""Compile production bootstrap orchestration and UI state steps with engine calls faked."""
from pathlib import Path
import re
import sys

root = Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/story'
service = (root / 'bootstrap/world_bootstrap_service.cpp').read_text()
ui = (root / 'bootstrap/proof_ui_lifecycle.cpp').read_text()

def method(source, name, qualified=False):
    prefix = r'ProofUiLifecycle::' if qualified else ''
    start = re.search(r'(?m)^ +auto ' + prefix + name + r'\(', source).start()
    cursor = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == '{') - (source[cursor] == '}')
        cursor += 1
    return source[start:cursor].replace('ProofUiLifecycle::', '')

out = Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text('\n'.join(method(service, name) for name in (
    'reset', 'begin_world', 'perform_bootstrap', 'world_ready', 'tick')))
out.with_name('proof_ui_state_under_test.h').write_text('\n'.join(method(ui, name) for name in (
    'retire_proof_ui', 'NotifyUObjectDeleted', 'refresh_viewport', 'ensure_widget', 'process_events')))
out.with_name('proof_ui_facade_under_test.h').write_text('\n'.join(method(ui, name, True) for name in (
    'reset', 'request_viewport_check', 'process_events', 'refresh_viewport', 'ensure_widget', 'added')))
