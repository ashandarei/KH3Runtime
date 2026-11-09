"""Encode the user-supplied PNG in a standalone SWF with an original root class.

No KH3 files are inputs. The movie has a pool of transparent named sprites;
the native overlay positions and reveals them. Rebuild the checked-in header
after editing the .as sources, PNG artwork, colors or layout. Generation uses
Pillow and the same Java/JPEXS compiler as the shop and journal. --check only
validates checked-in source hashes and the embedded binary.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
PROBE_ROOT = HERE.parents[2] / 'KH3APProbeCpp'
OUTPUT = PROBE_ROOT / 'kh3ap/features/ui/map_overlay/map_chest_movie.h'
MOVIE = PROBE_ROOT / 'resources/map_chest_movie.swf'
SOURCES = ('APMapChestIcons.as', 'APMapChestIcon.as')
TEMPLATE = HERE / 'compiler-template.swf'
sys.path.insert(0, str(HERE.parent / 'shop_ui'))
from abc_format import ABC, tags as read_movie_tags

PALETTES = HERE.parents[2] / 'data/runtime-inputs.json'
LEGEND_WIDTH = 238
LEGEND_HEIGHT = 486
KEYBIND_WIDTH = 390
KEYBIND_ROW_HEIGHT = 28
KEYBIND_LABELS = ('L2 - Zoom out', 'R2 - Zoom in', 'L2 + R2 = Show/Hide locked chests')
MAP_COLORS = {
    'Classic Tone': '#808080',
    'Crystal Snow': '#59D3F9',
    'Ever After': '#FF80BF',
    'Favorite Deputy': '#A06135',
    'Grand Chef': '#B388FF',
    'Happy Gear': '#B7E534',
    "Hero's Origin": '#FF8000',
    'Hunny Spout': '#F9D338',
    'Kingdom Key': '#35D06F',
    'Nano Gear': '#E72C2C',
    'Oathkeeper': '#F3F6FF',
    'Oblivion': '#602080',
    'Shooting Star': '#2C5095',
    'Starlight': '#BFC6D0',
    'Wheel of Fate': '#297A8C',
}


def keyblade_colors():
    """Single map tints in sRGB, with the physical palette's Keyblade identities."""
    palettes = json.loads(PALETTES.read_text(encoding='utf-8'))['chests']['palettes']
    if set(palettes) != set(MAP_COLORS):
        raise ValueError('Every chest Keyblade needs exactly one map tint')
    return {name: tuple(bytes.fromhex(MAP_COLORS[name].removeprefix('#')))
            for name in sorted(palettes)}


class Bits:
    def __init__(self):
        self.bits = []

    def put(self, value, count):
        if count:
            self.bits.append(format(value & ((1 << count) - 1), f'0{count}b'))

    def data(self):
        bits = ''.join(self.bits)
        bits += '0' * (-len(bits) % 8)
        return bytes(int(bits[i:i+8], 2) for i in range(0, len(bits), 8))


def signed_size(values):
    return max(1, max(abs(v).bit_length() + 1 for v in values))


def rect(x0, x1, y0, y1):
    b = Bits()
    n = signed_size((x0, x1, y0, y1))
    b.put(n, 5)
    for v in (x0, x1, y0, y1):
        b.put(v, n)
    return b.data()


def matrix(scale=1, x=0, y=0):
    b = Bits()
    b.put(scale != 1, 1)
    if scale != 1:
        v = int(scale * 65536)
        n = signed_size((v,))
        b.put(n, 5)
        b.put(v, n)
        b.put(v, n)
    b.put(0, 1)  # no skew
    n = signed_size((x, y))
    b.put(n, 5)
    b.put(x, n)
    b.put(y, n)
    return b.data()


def tag(kind, data=b''):
    n = len(data)
    return struct.pack('<H', kind << 6 | min(n, 63)) + (struct.pack('<I', n) if n >= 63 else b'') + data


def place(character, depth, transform, name=None, hidden=False, tint=None):
    flags = 6 | (0x20 if name else 0) | (8 if hidden or tint is not None else 0)
    payload = bytes([flags]) + struct.pack('<HH', depth, character) + transform
    if hidden or tint is not None:
        b = Bits()
        b.put(0, 1)  # no additive color
        b.put(1, 1)  # multiplicative color
        b.put(10, 4)
        rgb = (256, 256, 256) if tint is None else tuple(round(c * 256 / 255) for c in tint)
        for value in (*rgb, 0 if hidden else 256):
            b.put(value, 10)
        payload += b.data()
    if name:
        payload += name.encode() + b'\0'
    return tag(26, payload)


def source_hashes():
    return {name: hashlib.sha256((HERE / name).read_text(encoding='utf-8').encode('utf-8')).hexdigest()
            for name in SOURCES}


def document_class(movie=None):
    """Read generated script/class tags; editable behavior lives in the .as files."""
    data = MOVIE.read_bytes() if movie is None else movie
    return b''.join(data[start:end] for kind, start, _, end in read_movie_tags(data)
                    if kind in {82, 76})  # DoABC, SymbolClass


def compile_document_classes(java, ffdec, work_dir):
    """Compile both authored classes using the fixed, original class scaffold.

    Only its class tags are copied to the compiler input. Every class is then
    replaced from .as; all output artwork is freshly built from the PNGs below.
    No KH3 assets or decompiled game scripts are inputs.
    """
    work_dir.mkdir(parents=True, exist_ok=True)
    body = rect(0, 20, 0, 20) + struct.pack('<HH', 60*256, 1)
    body += tag(69, struct.pack('<I', 8))
    body += tag(39, struct.pack('<HH', 3, 1) + tag(1) + tag(0))
    body += document_class(TEMPLATE.read_bytes()) + tag(1) + tag(0)
    blank, compiled = work_dir / 'classes-input.swf', work_dir / 'classes-compiled.swf'
    blank.write_bytes(b'FWS\x0a' + struct.pack('<I', len(body)+8) + body)
    replacements = [value for name in SOURCES for value in (Path(name).stem, str(HERE / name))]
    subprocess.run([str(java.resolve()), '-jar', str(ffdec.resolve()), '-replace',
                    str(blank), str(compiled), *replacements], check=True)
    data = compiled.read_bytes()
    classes = []
    for kind, _, payload, end in read_movie_tags(data):
        if kind == 82:
            abc_start = data.index(0, payload+4)+1
            classes.extend(ABC(data[abc_start:end]).classes)
    if sorted(classes) != sorted(Path(name).stem for name in SOURCES):
        raise ValueError('Compiler output must contain exactly our two map classes')
    return document_class(data)


def check_generated():
    """Catch stale .as sources or a mismatched binary before building the DLL."""
    header = OUTPUT.read_text(encoding='utf-8')
    for name, digest in source_hashes().items():
        if f'// Source SHA256 (LF) {name}: {digest}' not in header:
            raise ValueError(f'{name} changed; regenerate the map movie with --java and --ffdec')
    digest = hashlib.sha256(MOVIE.read_bytes()).hexdigest()
    if f'// Movie SHA256: {digest}' not in header:
        raise ValueError('Map movie/header mismatch; regenerate the map movie')


def icon_image(filename, grayscale=False):
    from PIL import Image
    original = Image.open(HERE / filename).convert('RGBA')
    # KH3 disables embedded bitmap decoding. Encode the supplied image as tiny
    # colored rectangles instead; the native sprite caches their rendered result.
    # 96 px exceeds the marker's displayed size, including the tested 4K viewport.
    im = original.resize((96, round(original.height * 96 / original.width)), Image.Resampling.LANCZOS)
    im = im.quantize(colors=256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE).convert('RGBA')
    if grayscale:
        # Vector fill colors only; retain the supplied art's silhouette, alpha,
        # dark outlines and shading, without multiplying its gold into the tint.
        im.putdata([(max(r, g, b),)*3 + (a,) for r, g, b, a in im.getdata()])
    return im


def icon_shape(filename, shape_id, sprite_id):
    im = icon_image(filename)
    shape = rgba_shape(im, shape_id)
    bounds = im.getbbox()
    cx, cy = (bounds[0]+bounds[2])/2, (bounds[1]+bounds[3])/2
    sprite = tag(39, struct.pack('<HH', sprite_id, 1) + place(shape_id, 1, matrix(x=-int(cx*20), y=-int(cy*20))) + tag(1) + tag(0))
    return shape + (sprite if sprite_id else b''), bounds[2]-bounds[0], (cx, cy)


def rgba_rectangles(im):
    """Shared lossless rectangle representation of the prepared map icon."""
    w, h = im.size
    pixels = list(im.getdata())
    colors = sorted({c for c in pixels if c[3]})
    # Coalesce identical horizontal runs across adjacent rows.
    rectangles, previous = [], {}
    for y in range(h):
        active, x = {}, 0
        while x < w:
            color, start = pixels[y*w+x], x
            x += 1
            while x < w and pixels[y*w+x] == color:
                x += 1
            if color[3]:
                key = start, x, color
                box = previous.pop(key, [start, y, x, y+1, color])
                box[3] = y+1
                active[key] = box
        rectangles.extend(previous.values())
        previous = active
    rectangles.extend(previous.values())
    return colors, rectangles


def rgba_shape(im, shape_id):
    """Encode authored RGBA artwork as vector fills for KH3's bitmap-free UI."""
    w, h = im.size
    colors, rectangles = rgba_rectangles(im)
    color_index = {c: i+1 for i,c in enumerate(colors)}
    shape = struct.pack('<H', shape_id) + rect(0, w*20, 0, h*20)
    shape += (bytes([len(colors)]) if len(colors) < 255 else b'\xff' + struct.pack('<H',len(colors)))
    shape += b''.join(b'\x00'+bytes(c) for c in colors) + b'\x00'
    bits = Bits()
    fill_bits = len(colors).bit_length()
    bits.put(fill_bits, 4); bits.put(0, 4)
    for x0, y0, x1, y1, color in rectangles:
        bits.put(0, 1); bits.put(0b00011, 5)  # absolute move + fill0
        n = signed_size((x0*20,y0*20))
        bits.put(n,5); bits.put(x0*20,n); bits.put(y0*20,n)
        bits.put(color_index[color],fill_bits)
        for dx,dy in (((x1-x0)*20,0),(0,(y1-y0)*20),(-(x1-x0)*20,0),(0,-(y1-y0)*20)):
            n = max(2,signed_size((dx,dy)))
            bits.put(1,1);bits.put(1,1);bits.put(n-2,4);bits.put(0,1)
            bits.put(dx == 0,1);bits.put(dy if dx == 0 else dx,n)
    bits.put(0,6)
    return tag(32,shape+bits.data())


def legend_layers(colors):
    from PIL import Image, ImageDraw, ImageFont
    # Pillow's bundled scalable font keeps authoring reproducible without game
    # font assets or a system font dependency. Render at 2x for high-DPI text.
    im = Image.new('RGBA', (LEGEND_WIDTH*2, LEGEND_HEIGHT*2))
    draw = ImageDraw.Draw(im)
    draw.rounded_rectangle((0, 0, im.width-1, im.height-1), radius=16,
                           fill=(8, 18, 29, 224), outline=(116, 131, 149, 180), width=2)
    layers = [(im, 0, 0)]

    def label(text, size):
        # Keep antialiasing in a small alpha palette, rather than blending the
        # glyphs into the panel and creating hundreds of separate RGB fills.
        mask = Image.new('L', (im.width-80, 56))
        ImageDraw.Draw(mask).text((0, 0), text, font=ImageFont.load_default(size=size), fill=255)
        mask = mask.point(lambda a: round(a/17)*17)
        ink = Image.new('RGBA', mask.size, (245, 247, 250, 255))
        ink.putalpha(mask)
        return ink

    title = Image.new('RGBA', (im.width, 70))
    title.alpha_composite(label('Chest keyblades', 42), (28, 0))
    ImageDraw.Draw(title).line((28, 58, im.width-28, 58), fill=(116, 131, 149, 150), width=2)
    layers.append((title, 0, 22))
    for index, (name, color) in enumerate(colors.items()):
        row = Image.new('RGBA', (im.width, 56))
        row.alpha_composite(label(name, 38), (80, 0))
        # One swatch matches the tint across the whole chest icon.
        draw = ImageDraw.Draw(row)
        draw.rectangle((28, 5, 60, 37), fill=(*color, 255))
        draw.rectangle((28, 5, 60, 37), outline=(220, 226, 234, 255), width=2)
        layers.append((row, 0, 105+index*56))
    return layers


def legend_image(colors):
    from PIL import Image
    im = Image.new('RGBA', (LEGEND_WIDTH*2, LEGEND_HEIGHT*2))
    for layer, x, y in legend_layers(colors):
        im.alpha_composite(layer, (x, y))
    return im


def legend_art(colors):
    shapes, placements = b'', b''
    for index, (layer, x, y) in enumerate(legend_layers(colors)):
        shape_id = 9 if index == 0 else 10+index
        shapes += rgba_shape(layer, shape_id)
        # Named one-frame sprites let the runtime compact the eligible rows
        # without resizing text/swatches or tessellating a new legend in game.
        sprite_id = 30+index
        shapes += tag(39, struct.pack('<HH', sprite_id, 1) +
                      place(shape_id, 1, matrix(scale=.5)) + tag(1) + tag(0))
        name = 'panel' if index == 0 else 'title' if index == 1 else f'row{index-2}'
        placements += place(sprite_id, index+1, matrix(x=x*10, y=y*10), name)
    return shapes + tag(39, struct.pack('<HH', 10, 1) + placements + tag(1) + tag(0))


def keybind_layers():
    from PIL import Image, ImageDraw, ImageFont
    for text in KEYBIND_LABELS:
        im = Image.new('RGBA', (KEYBIND_WIDTH*2, KEYBIND_ROW_HEIGHT*2))
        mask = Image.new('L', im.size)
        ImageDraw.Draw(mask).text((0, 7), text, font=ImageFont.load_default(size=36), fill=255)
        mask = mask.point(lambda a: round(a/17)*17)
        ink = Image.new('RGBA', im.size, (245, 247, 250, 255))
        ink.putalpha(mask)
        im.alpha_composite(ink)
        yield im


def keybind_art():
    shapes, placements = b'', b''
    for index, layer in enumerate(keybind_layers()):
        shapes += rgba_shape(layer, 50+index)
        shapes += tag(39, struct.pack('<HH', 53+index, 1) +
                      place(50+index, 1, matrix(scale=.5)) + tag(1) + tag(0))
        placements += place(53+index, index+1, matrix(y=index*KEYBIND_ROW_HEIGHT*20),
                            ('zoomOut', 'zoomIn', 'lockedChests')[index])
    return shapes + tag(39, struct.pack('<HH', 56, 1) + placements + tag(1) + tag(0))


def chest_frames(center, colors):
    transform = matrix(x=-int(center[0]*20), y=-int(center[1]*20))
    frames = place(2, 1, transform) + tag(1)
    for color in colors.values():
        frames += tag(28, struct.pack('<H', 1))  # replace the previous frame's art
        frames += place(8, 1, transform, tint=color) + tag(1)
    return tag(39, struct.pack('<HH', 3, len(colors)+1) + frames + tag(0))


def build(script):
    colors = keyblade_colors()
    chest, chest_width, center = icon_shape('chest.png', 2, 0)
    chest += rgba_shape(icon_image('chest.png', grayscale=True), 8) + chest_frames(center, colors)
    emblem, emblem_width, _ = icon_shape('lucky-emblem.png', 4, 5)
    ingredient, ingredient_width, _ = icon_shape('ingredient.png', 6, 7)
    body = rect(0, 1920*20, 0, 1080*20) + struct.pack('<HH', 60*256, 1)
    body += tag(69, struct.pack('<I', 8)) + chest + emblem + ingredient + legend_art(colors) + keybind_art() + script
    # Pan the icon pool as one layer inside a fixed viewport. The legend is a
    # sibling, so map zoom/clipping cannot move, scale, or clip its swatches.
    markers = b''.join(place(3, i+1, matrix(), f'chest{i}', True) for i in range(128))
    markers += b''.join(place(5, i+129, matrix(), f'emblem{i}', True) for i in range(90))
    markers += b''.join(place(7, i+219, matrix(), f'ingredient{i}', True) for i in range(128))
    body += tag(39, struct.pack('<HH', 28, 1) + markers + tag(1) + tag(0))
    body += tag(39, struct.pack('<HH', 29, 1) + place(28, 1, matrix(), 'markers') + tag(1) + tag(0))
    body += place(29, 1, matrix(), 'markerViewport')
    body += place(10, 347, matrix(), 'keybladeLegend', True)
    body += place(56, 348, matrix(), 'keybindHints', True)
    body += tag(1) + tag(0)
    swf = b'FWS\x0a' + struct.pack('<I', len(body)+8) + body
    header = ('// Generated by authoring/kh3/map_overlay/build_chest_icon.py; supplied icons and authored legend.\n'
        '// ActionScript source: authoring/kh3/map_overlay/APMapChestIcons.as and APMapChestIcon.as.\n'
        '// Artwork/layout: authoring/kh3/map_overlay/build_chest_icon.py.\n'
        '// Variable guide: docs/MOVIE_SOURCES.md. Do not edit generated bytes.\n'
        + ''.join(f'// Source SHA256 (LF) {name}: {digest}\n' for name, digest in source_hashes().items()) +
        f'// Movie SHA256: {hashlib.sha256(swf).hexdigest()}\n'
        '#pragma once\n#include <array>\n#include <cstdint>\n#include <span>\n#include <string_view>\nnamespace kh3ap {\n'
        f'inline constexpr std::array<std::wstring_view,{len(colors)}> map_chest_keyblades={{\n' +
        ''.join(f'L{json.dumps(name)},\n' for name in colors) + '};\n' +
        f'inline constexpr double map_chest_art_width={chest_width};\n'
        f'inline constexpr double map_emblem_art_width={emblem_width};\n'
        f'inline constexpr double map_ingredient_art_width={ingredient_width};\n'
        f'inline constexpr double map_keyblade_legend_width={LEGEND_WIDTH};\n'
        f'inline constexpr double map_keyblade_legend_height={LEGEND_HEIGHT};\n'
        f'inline constexpr double map_keybind_row_height={KEYBIND_ROW_HEIGHT};\n'
        f'inline constexpr uint32_t map_chest_movie_size={len(swf)};\n'
        'auto map_chest_movie_bytes() -> std::span<const uint8_t>;\n}\n')
    if not OUTPUT.exists() or OUTPUT.read_text(encoding='utf-8') != header:
        OUTPUT.write_text(header, encoding='utf-8')
    if not MOVIE.exists() or MOVIE.read_bytes() != swf:
        MOVIE.write_bytes(swf)
    print(f'{len(swf)} bytes; {MOVIE}; metadata {OUTPUT}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--java', type=Path)
    parser.add_argument('--ffdec', type=Path)
    parser.add_argument('--work-dir', type=Path, default=HERE.parents[2] / 'build/map-overlay')
    parser.add_argument('--check', action='store_true', help='Validate checked-in sources/movie; no compiler or Pillow needed')
    args = parser.parse_args()
    if args.check:
        check_generated()
        return
    if not args.java or not args.ffdec:
        parser.error('Generation requires --java JAVA_EXE and --ffdec FFDEC_CLI_JAR')
    build(compile_document_classes(args.java, args.ffdec, args.work_dir.resolve()))


if __name__ == '__main__':
    main()
