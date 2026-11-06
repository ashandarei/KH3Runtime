# KH0.2 asset loading

The source companion verifies the selected executable before starting its
asset service. After the engine finishes mounting its paks, the companion
enumerates supported content paths and sizes once. The immutable index lives
in memory and is shared with its owning KH3 process through asset protocol 3.
Both sides derive virtual paths and English/Japanese voice pairs from it.
The index is limited to 65,536 files and 16 MiB of serialized metadata.

The first read of a package prepares its header and exports together. The
adapter parses the package tables, resolves names and imports, applies the
authored compatibility rules, and retains only the calculated byte edits.
Subsequent range reads apply those edits to bytes from the source provider.
Raw reads and preparation use separate locks. Only one package prepares at a
time, bounding temporary buffers to a 16 MiB header and 128 MiB exports per
input/output pair. Bulk payloads and localization files pass through unchanged.

The edit cache holds at most 4096 packages and 32 MiB of edit storage. Entries
in active reads cannot be evicted. Eviction discards only calculated edits;
a later read can prepare that package again. There is no background scanning,
idle polling, persistent index, or persistent patch cache. An unsupported
package fails its read rather than being served with incomplete adaptation.

Edit rules live in `authoring/kh02/asset_compatibility.json`. They contain
semantic identities, verification hashes, bounded ranges, and authored
replacement values. They contain no original byte/replacement byte pairs.
Regenerate the checked-in header with:

```powershell
python authoring/kh02/asset_compatibility.py
```

Unit tests cover malformed metadata/packages, range boundaries and concurrent
cache reuse. The optional `kh02_asset_provider_smoke` build target runs the
owned source service against an explicitly supplied supported installation;
it is not part of automatic CTest runs. It needs the matching 02-GoA 2.0 DLL
installed in that source game. It never loads a gameplay save.

Player packages pin a protocol 3 companion downloaded from its GitHub release
and verified by size and SHA-256. Both DLLs must support the same asset
protocol. Bundled companion mode is rejected. Local native development
installs the companion directly from its separate source project.
