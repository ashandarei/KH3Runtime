"""Compile our standalone AS3 helper and extend a local vanilla Shop fixture.

Explicit, audited splices replace binary diffs of recompiled game classes.
All stock code/graphics come from copy ranges into the player's movie. Literals
contain only our helper, hooks, new identifiers, and encoded container sizes.
Neither fixture movie is distributed. JPEXS compiles a blank, original movie
with no KH3 inputs.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from movie_resources import write_patch_resource

from abc_format import ABC, tag, tags, u30

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
OUTPUT = ROOT / 'KH3APProbeCpp/kh3ap/features/moogle_shop/shop_movie_patch_data.h'
HOOKS = {'CoTradeListItem': 'attachRow', 'Shop_fla.McMenuMixTopObj_122': 'attachForge'}


def fingerprint(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value


def blank_helper_movie(helper_name='APShopRuntime'):
    """Original one-class compiler input, with no game implementations."""
    strings = ('kh3ap', helper_name, 'Object')
    abc = struct.pack('<HH', 16, 46) + b'\x01\x01\x01' + u30(4)
    abc += b''.join(u30(len(s)) + s.encode() for s in strings)
    abc += bytes([3, 0x16, 0, 0x16, 1, 1, 3, 7, 2, 2, 7, 1, 3])
    abc += b'\x03' + b'\x00\x00\x00\x00' * 3
    abc += bytes([0, 1, 1, 2, 1, 0, 0, 0, 1, 0, 1, 2, 1, 1, 4, 1, 0])
    bodies = (
        (0, 1, 1, 3, 4, bytes([0xd0, 0x30, 0xd0, 0x49, 0, 0x47])),
        (1, 1, 1, 2, 3, bytes([0xd0, 0x30, 0x47])),
        (2, 2, 1, 1, 3, bytes([0xd0, 0x30, 0x65, 0, 0x60, 2, 0x30, 0x60, 2, 0x58, 0, 0x1d, 0x68, 1, 0x47])),
    )
    abc += u30(len(bodies))
    for method, stack, locals_, initial, maximum, code in bodies:
        abc += b''.join(u30(v) for v in (method, stack, locals_, initial, maximum, len(code))) + code + b'\x00\x00'
    payload = b'\x08\x00\x00\x18\x01\x00' + tag(69, struct.pack('<I', 8))
    payload += tag(82, struct.pack('<I', 1) + b'APShopRuntime\0' + abc) + tag(1) + tag(0)
    return b'FWS\x15' + struct.pack('<I', len(payload) + 8) + payload


def abc_tags(movie):
    for kind, start, payload, end in tags(movie):
        if kind == 82:
            abc_start = movie.index(0, payload + 4) + 1
            yield start, payload, abc_start, end, ABC(movie[abc_start:end])


def compile_helper(java, ffdec, directory, helper_name='APShopRuntime', source=None):
    blank, compiled = directory / 'helper-blank.swf', directory / 'helper.swf'
    blank.write_bytes(blank_helper_movie(helper_name))
    subprocess.run([str(java), '-jar', str(ffdec), '-replace', str(blank), str(compiled),
                    'kh3ap.' + helper_name, str(source or HERE / 'APShopRuntime.as')], check=True)
    data = compiled.read_bytes()
    entries = list(abc_tags(data))
    if len(entries) != 1 or set(entries[0][4].classes) != {'kh3ap.' + helper_name}:
        raise ValueError('Compiler output must contain only our helper class')
    return data[entries[0][0]:entries[0][3]]


@dataclass
class Edit:
    start: int
    end: int
    literal: bytes
    provenance: str


def materialize(original, edits):
    result, delta, audit = bytearray(), bytearray(), []
    cursor = 0
    for edit in sorted(edits, key=lambda e: (e.start, e.end)):
        if edit.start < cursor or edit.end < edit.start or edit.end > len(original):
            raise ValueError('Overlapping/out-of-range edits')
        if edit.start > cursor:
            length = edit.start - cursor
            result += original[cursor:edit.start]
            delta += b'\x00' + struct.pack('<II', cursor, length)
        if edit.literal:
            result += edit.literal
            delta += b'\x01' + struct.pack('<I', len(edit.literal)) + edit.literal
        audit.append(dict(start=edit.start, removed=edit.end-edit.start,
                          added=len(edit.literal), provenance=edit.provenance))
        cursor = edit.end
    if cursor < len(original):
        result += original[cursor:]
        delta += b'\x00' + struct.pack('<II', cursor, len(original)-cursor)
    return bytes(result), bytes(delta), audit


def constructor_is_straight_line(body):
    """No return/branch/exception may bypass a hook or need target rewriting."""
    if body['exceptions'] or not body['code'].endswith(b'\x47'):
        return False
    reader = object.__new__(ABC)
    reader.data, reader.pos = body['code'], 0
    zero = {0xd0, 0xd1, 0xd2, 0xd3, 0x30, 0x26, 0x27}
    one = {0x49, 0x25, 0x60, 0x66, 0x5d, 0xf0, 0xf1}
    while reader.pos < len(reader.data) - 1:
        opcode = reader.byte()
        if opcode in zero:
            pass
        elif opcode == 0x24:
            reader.byte()
        elif opcode in one:
            reader.number()
        elif opcode == 0x4f:
            reader.number()
            reader.number()
        elif opcode == 0xef:
            reader.byte()
            reader.number()
            reader.byte()
            reader.number()
        else:
            return False
    return reader.pos == len(reader.data) - 1


def extend_movie(original, helper, hooks=None, helper_name='APShopRuntime'):
    hooks = HOOKS if hooks is None else hooks
    matches = [entry for entry in abc_tags(original) if any(name in entry[4].classes for name in hooks)]
    if len(matches) != 1 or not set(hooks).issubset(matches[0][4].classes):
        raise ValueError('Expected the requested constructors in one stock ABC tag')
    start, payload, abc_start, end, abc = matches[0]
    if payload - start != 6:
        raise ValueError('Expected long DoABC tag header')
    edits = [Edit(start, start, helper, 'original AP helper DoABC')]
    # Append to pools, never reindex or rewrite an existing entry.
    string_count, string_end, _ = abc.pools['strings']
    functions = tuple(dict.fromkeys(hooks.values()))
    new_strings = ('kh3ap', helper_name, *functions)
    first_string = max(1, string_count.value)
    ns_count, ns_end, namespaces = abc.pools['namespaces']
    first_ns = max(1, ns_count.value)
    public = next((i for i, ns in enumerate(namespaces) if ns == (0x16, 0)), None)
    new_namespaces = [(0x16, first_string)]
    if public is None:
        public = first_ns + 1
        new_namespaces.append((0x16, 0))
    name_count, name_end, _ = abc.pools['names']
    first_name = max(1, name_count.value)
    appended_names = b'\x07' + u30(first_ns) + u30(first_string + 1)
    for index in range(len(functions)):
        appended_names += b'\x07' + u30(public) + u30(first_string + 2 + index)
    for name, count, pool_end, added, encoded in (
        ('strings', string_count, string_end, len(new_strings), b''.join(u30(len(s)) + s.encode() for s in new_strings)),
        ('namespaces', ns_count, ns_end, len(new_namespaces), b''.join(bytes([kind]) + u30(index) for kind, index in new_namespaces)),
        ('names', name_count, name_end, 1 + len(functions), appended_names),
    ):
        edits.append(Edit(abc_start + count.start, abc_start + count.end,
                          u30(max(1, count.value) + added), 'encoded ' + name + ' count'))
        edits.append(Edit(abc_start + pool_end, abc_start + pool_end, encoded, 'our new ' + name))
    hook_bytes = {}
    for class_name, function in hooks.items():
        body = abc.bodies[abc.classes[class_name]]
        if not constructor_is_straight_line(body):
            raise ValueError('Unsafe constructor: ' + class_name)
        target = first_name + 1 + functions.index(function)
        hook = b'\x60' + u30(first_name) + b'\xd0\x4f' + u30(target) + b'\x01'
        hook_bytes[class_name] = hook
        at = abc_start + body['start'] + len(body['code']) - 1
        edits.append(Edit(at, at, hook, 'our constructor call to ' + function))
        length, stack = body['length'], body['stack']
        edits.append(Edit(abc_start + length.start, abc_start + length.end,
                          u30(length.value + len(hook)), 'encoded constructor length'))
        if stack.value < 2:
            edits.append(Edit(abc_start + stack.start, abc_start + stack.end,
                              u30(2), 'encoded constructor max_stack'))
    growth = sum(len(e.literal) - (e.end-e.start) for e in edits if e.start > start)
    edits.append(Edit(start+2, start+6, struct.pack('<I', end-payload+growth), 'encoded stock DoABC length'))
    total_growth = sum(len(e.literal) - (e.end-e.start) for e in edits)
    edits.append(Edit(4, 8, struct.pack('<I', len(original)+total_growth), 'encoded movie length'))
    result, delta, audit = materialize(original, edits)
    verify_preservation(original, result, hook_bytes, helper_name)
    return result, delta, audit


def verify_preservation(original, result, hooks, helper_name='APShopRuntime'):
    """Reject any altered game instruction, exception or frame script."""
    old_tags, new_tags = list(abc_tags(original)), list(abc_tags(result))
    if len(new_tags) != len(old_tags) + 1 or set(new_tags[0][4].classes) != {'kh3ap.' + helper_name}:
        raise ValueError('Expected exactly one new helper ABC')
    checked, hooked = 0, 0
    for before, after in zip(old_tags, new_tags[1:]):
        old, new = before[4], after[4]
        if old.classes != new.classes or old.bodies.keys() != new.bodies.keys():
            raise ValueError('Stock classes/method set changed')
        for name, (_, _, pool) in old.pools.items():
            if new.pools[name][2][:len(pool)] != pool:
                raise ValueError('Stock constant pool changed')
        constructors = {old.classes[name]: hook for name, hook in hooks.items()}
        for method, body in old.bodies.items():
            current = new.bodies[method]
            expected = body['code']
            if method in constructors:
                expected = expected[:-1] + constructors[method] + expected[-1:]
                hooked += 1
            if current['code'] != expected:
                raise ValueError('Stock method instructions changed')
            if any(current[key] != body[key] for key in ('locals', 'initial', 'maximum', 'exceptions')):
                raise ValueError('Stock method context changed')
            expected_stack = max(2, body['stack'].value) if method in constructors else body['stack'].value
            if current['stack'].value != expected_stack:
                raise ValueError('Unexpected method stack change')
            old_end = body['start'] + len(body['code'])
            new_end = current['start'] + len(current['code'])
            next_old = min((b['stack'].start for b in old.bodies.values() if b['stack'].start > old_end), default=len(old.data))
            next_new = min((b['stack'].start for b in new.bodies.values() if b['stack'].start > new_end), default=len(new.data))
            if old.data[old_end:next_old] != new.data[new_end:next_new]:
                raise ValueError('Stock exception/trait metadata changed')
            checked += 1
    if hooked != len(hooks):
        raise ValueError('Missing constructor hook')
    # No graphics, symbol bindings, frame tags, or other movie data changed.
    old_other = [original[s:e] for k, s, _, e in tags(original) if k != 82]
    new_other = [result[s:e] for k, s, _, e in tags(result) if k != 82]
    if old_other != new_other:
        raise ValueError('Stock movie assets or symbol bindings changed')
    print(f'Preserved all {checked} stock method bodies; appended {hooked} constructor calls')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path)
    parser.add_argument('--java', type=Path, required=True)
    parser.add_argument('--ffdec', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, default=ROOT / 'build/shop-runtime-extension')
    parser.add_argument('--output', type=Path, default=OUTPUT)
    parser.add_argument('--resource-output', type=Path, help='Override the generated binary patch path')
    parser.add_argument('--namespace', choices=('kh3ap::shop_movie_patch', 'kh3ap::shop_movie_patch_steam'),
                        default='kh3ap::shop_movie_patch')
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    helper = compile_helper(args.java.resolve(), args.ffdec.resolve(), args.work_dir.resolve())
    original = args.original.read_bytes()
    result, delta, audit = extend_movie(original, helper)
    source_hash = hashlib.sha256((HERE / 'APShopRuntime.as').read_text(encoding='utf-8').encode('utf-8')).hexdigest()
    (args.work_dir / 'extended.gfx').write_bytes(result)
    (args.work_dir / 'audit.json').write_text(json.dumps(dict(
        original_sha256=hashlib.sha256(original).hexdigest(),
        helper_source_sha256=source_hash,
        helper_sha256=hashlib.sha256(helper).hexdigest(),
        added_bytes=sum(edit['added'] for edit in audit), edits=audit), indent=2) + '\n')
    steam = args.namespace == 'kh3ap::shop_movie_patch_steam'
    resource = args.resource_output or ROOT / 'KH3APProbeCpp/resources' / (
        'shop_movie_patch_steam.bin' if steam else 'shop_movie_patch_egs.bin')
    write_patch_resource(header=args.output, resource=resource,
        resource_name='KH3AP_SHOP_PATCH_STEAM' if steam else 'KH3AP_SHOP_PATCH_EGS',
        namespace=args.namespace, source=HERE / 'APShopRuntime.as',
        generator='authoring/kh3/shop_ui/build_runtime_patch.py',
        original=original, result=result, delta=delta)
    print(f'{len(original)} -> {len(result)} movie bytes; {len(helper)} original helper bytes; {len(delta)} runtime patch bytes')


if __name__ == '__main__':
    main()
