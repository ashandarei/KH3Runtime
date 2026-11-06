"""Compile the original music overlay using the existing original SWF scaffold."""
from pathlib import Path
import argparse
import hashlib
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'map_overlay'))
from build_chest_icon import rect, tag
sys.path.insert(0, str(HERE.parent / 'shop_ui'))
from build_runtime_patch import compile_helper

ROOT = HERE.parents[2] / 'KH3APProbeCpp'
MOVIE = ROOT / 'resources/now_playing_movie.swf'
HEADER = ROOT / 'kh3ap/features/music/now_playing/now_playing_movie_data.h'
SOURCE = HERE / 'APNowPlaying.as'


def hashes():
    return hashlib.sha256(SOURCE.read_text(encoding='utf-8').encode()).hexdigest(), hashlib.sha256(MOVIE.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--java', type=Path)
    parser.add_argument('--ffdec', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.check:
        source_hash, movie_hash = hashes()
        header = HEADER.read_text()
        if source_hash not in header or movie_hash not in header:
            raise ValueError('Music movie is stale; run build_movie.py --java ... --ffdec ...')
        return
    if not args.java or not args.ffdec:
        parser.error('--java and --ffdec are required to compile')
    work = HERE.parents[2] / 'tmp/kh3-now-playing'
    work.mkdir(parents=True, exist_ok=True)
    body = rect(0,1920*20,0,1080*20) + struct.pack('<HH',60*256,1)
    body += tag(69,struct.pack('<I',8))
    # Dynamic KH3 text requires the game's font library, as stock HUD movies
    # import it. This is only a runtime reference; no game font is embedded.
    body += tag(71,b'../91_font/TresFont.swf\0\x01\x00\x00\x00')
    # Compile exactly one original class. Do not carry the marker scaffold's
    # unused second class into another independently loaded movie.
    body += compile_helper(args.java.resolve(),args.ffdec.resolve(),work,'APNowPlaying',SOURCE)
    body += tag(76,struct.pack('<HH',1,0)+b'kh3ap.APNowPlaying\0')
    body += tag(1) + tag(0)
    MOVIE.write_bytes(b'FWS\x15' + struct.pack('<I',len(body)+8) + body)
    source_hash, movie_hash = hashes()
    HEADER.write_text('#pragma once\n#include <cstdint>\n'
                      f'// Source SHA256 (LF): {source_hash}\n// Movie SHA256: {movie_hash}\n'
                      f'namespace kh3ap {{ inline constexpr uint32_t now_playing_movie_size = {MOVIE.stat().st_size}; }}\n')


if __name__ == '__main__':
    main()
