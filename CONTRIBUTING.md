# Contributing

Follow [README.md](README.md) for setup. Keep changes focused; explain the problem,
resulting behavior and validation. Add regression coverage where practical. List
live-game checks separately, including the storefront and executable build.

Before submitting, run:

```powershell
python scripts/generate_catalogs.py --check
.\scripts\test.ps1 -VcVarsVer 14.38
.\scripts\build.ps1  # For native code changes
```

Diagnostic changes also need a build with `-EnableDiagnostics`. Protocol changes
need matching client tests using `KH3_PIPE_TEST_HOST` (see README); keep native
builds independent of the client and APWorld.

- Use events for idle work and bound retries to pending actions.
- Validate UObjects with `kh3ap::is_uobject_valid_fast`; retain weak references
  across frames. Use existing workers for file/JSON work.
- Validate external input before allocating memory or changing game state.
- Format changed C++ files with `clang-format -style=file:clang-format-kh02.yaml`
  for KH0.2 or `-style=file:clang-format.yaml` for other KH3 code, from this root.
- Preserve MIT and third-party notices. Exclude game files, engine headers,
  local settings, credentials and dumps.

## Character profiles

[`authoring/kh3/character_profiles.json`](authoring/kh3/character_profiles.json)
contains executable identities, native entry signatures and source bindings used
by table generation and executable auditing. After an intentional profile edit:

```powershell
python authoring/kh3/generate_character_profiles.py
python authoring/kh3/generate_character_profiles.py --check
```

For an optional passive audit, pass the selected installation path explicitly:

```powershell
python authoring/kh3/audit_character_profiles.py --egs-game-dir "$env:KH3_DIR" --output artifacts/character-audit.json
```

The audit reads the executable on disk without launching or attaching to KH3.
If the path is only in `.env`, pass its value explicitly. Use `--steam-game-dir`
only for explicitly selected Steam work. An offline signature match does not
establish live gameplay compatibility. Character behavior changes also need
live checks of the affected actions, returning to Sora and map transitions.
Keep dated validation results in the change's test report, not this guide.
