"""Validate that every native compatibility adapter has a registered alias."""
from __future__ import annotations

from pathlib import Path
import re


ADAPTER_TABLE = (
    Path(__file__).resolve().parents[2]
    / 'KH3APProbeCpp/kh3ap/features/kh02/compatibility/kh02_compatibility_adapters.inc'
)


def adapter_contract() -> dict[int, tuple[str, str]]:
    rows = re.findall(r'^KH02_COMPATIBILITY_ADAPTER\((\w+), (\d+), (\w+)\)$',
                      ADAPTER_TABLE.read_text(encoding='utf-8'), re.MULTILINE)
    result = {int(identifier): (name, domain) for name, identifier, domain in rows}
    if len(result) != len(rows) or set(result) != set(range(len(result))) or not result:
        raise ValueError('compatibility_adapter_table')
    return result


def validate_adapter_coverage(metadata: dict) -> None:
    contract = adapter_contract()
    used = set()
    for row in metadata['aliases']:
        match = re.fullmatch(r'compat_thunk<(\d+)>', row['handler'])
        if match:
            identifier = int(match[1])
            if identifier not in contract:
                raise ValueError('unknown_compatibility_adapter:' + row['handler'])
            used.add(identifier)
    if used != set(contract):
        raise ValueError('unregistered_compatibility_adapters:' + ','.join(map(str, sorted(set(contract) - used))))
