"""Compile the original movie surface classes using the local JPEXS compiler.

Only our map overlay's original class scaffold is used as compiler input.
No game artwork, scripts or movie files are inputs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

import build_movie_surface as surface
from abc_format import ABC, u30


def source_hashes():
    return {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
            for name in ('KH02Surface.as', 'KH02Frame.as')}


def compile_classes(java, ffdec, work_dir):
    work_dir.mkdir(parents=True, exist_ok=True)
    source = surface.swf.TEMPLATE.read_bytes()
    scripts = b''
    for kind, _, payload, end in surface.swf.read_movie_tags(source):
        if kind != 82:
            continue
        start = source.index(0, payload + 4) + 1
        data = source[start:end]
        count, finish, values = ABC(data).pools['strings']
        strings = u30(count.value)
        for value in values[1:]:
            value = value.replace('APMapChestIcons', 'KH02Surface').replace('APMapChestIcon', 'KH02Frame').encode()
            strings += u30(len(value)) + value
        data = data[:count.start] + strings + data[finish:]
        scripts += surface.swf.tag(82, source[payload:start] + data)
    body = surface.swf.rect(0, 38400, 0, 21600) + struct.pack('<HH', 30 * 256, 1)
    body += surface.swf.tag(69, struct.pack('<I', 8))
    body += surface.swf.tag(36, struct.pack('<HBHH', 1, 5, 1, 1) + zlib.compress(bytes(4)))
    body += scripts + surface.swf.tag(76, struct.pack('<HH', 2, 0) + b'KH02Surface\0'
                                    + struct.pack('<H', 1) + b'KH02Frame\0')
    body += surface.swf.tag(1) + surface.swf.tag(0)
    scaffold = work_dir / 'classes-input.swf'
    compiled = work_dir / 'classes-compiled.swf'
    scaffold.write_bytes(b'FWS\x0a' + struct.pack('<I', len(body) + 8) + body)
    replacements = [value for name in source_hashes()
                    for value in (Path(name).stem, str(Path(__file__).with_name(name).resolve()))]
    subprocess.run([str(java.resolve()), '-jar', str(ffdec.resolve()), '-replace',
                    str(scaffold), str(compiled), *replacements], check=True)
    movie = compiled.read_bytes()
    classes = [movie[movie.index(0, payload + 4) + 1:end]
               for kind, _, payload, end in surface.swf.read_movie_tags(movie) if kind == 82]
    if len(classes) != 1 or set(ABC(classes[0]).classes) != {'KH02Surface', 'KH02Frame'}:
        raise ValueError('Compiler output must contain exactly our two surface classes')
    surface.CLASSES.write_bytes(classes[0])
    surface.CLASSES.with_suffix('.json').write_text(json.dumps(source_hashes(), indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java', type=Path, required=True)
    parser.add_argument('--ffdec', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, default=surface.ROOT / 'build/kh02-movie-surface')
    args = parser.parse_args()
    compile_classes(args.java, args.ffdec, args.work_dir)
