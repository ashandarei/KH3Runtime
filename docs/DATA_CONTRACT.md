# Local runtime data contract

`data/runtime-inputs.json` is a reviewed, checked-in input to the DLL project.
Its schema is `kh3ap.runtime_inputs.v1`. It contains item identities and native
tokens, chest data, the location catalog and its checksum, encounter mappings,
bounded JSON-document limits, ingredient and adversary journal catalogs, and
alternate weapon identities. It contains no player credentials, seed
placements, saves, game files, or Python world implementation.

`scripts/generate_catalogs.py` uses only this file and Python's standard library.
It renders the owned native headers; `--check` rejects stale output. Normal
builds validate that output, and build metadata records the input file SHA-256.

The DLL project owns its checked-in snapshot. Catalog updates are explicit,
reviewed data changes. Keep stable IDs, runtime tokens, location checksum rules,
and the existing client/game protocol consistent across compatible releases.
There is no automatic synchronization, parent-repo discovery, APWorld import,
or source-module execution during builds, tests, installation, or packaging.

Native executable profiles and KH0.2 compatibility metadata are maintained
under `authoring/`, with generated headers inside this project. Audits against
an installed executable are passive and require an explicitly selected game
installation. Matching offline signatures does not establish live behavior.
