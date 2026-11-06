"""Compile both production flag-event drains against fake flag/IO boundaries."""
from pathlib import Path
import re
import sys

root = Path(__file__).parent / '../KH3APProbeCpp/kh3ap/features/story/progression'
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for name in ['vbonus', 'event']:
    source = (root / f'{name}_tracking_service.cpp').read_text(encoding='utf-8')
    signature = re.search(r'\bvoid\s+process\s*\(\s*\)\s*\{', source)
    if signature is None:
        raise ValueError(f'{name} tracking process method not found')
    start = signature.start()
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    method = source[start:end]
    constants = ''.join(
        f'static constexpr auto {key} = {value};\n'
        for key, value in re.findall(r'^\s*constexpr auto (\w+) = ([^;\n]+);', source[:start], re.M)
        if re.search(r'\b' + key + r'\b', method)
    )
    (out / f'{name}_recovery_under_test.h').write_text(constants + method, encoding='utf-8')
