# Turbo build identity - known DLLs (review 2026-10-31)

Seed table for the build-identity log. Every row below is copied from the 2026-10-31 review pack and nothing else: the
MD5 column holds the 8-character prefix the pack states (full hashes were not in the pack), the commit column holds what the
pack states ("unknown" / "not stated" where it states nothing). No row here was computed from a file.

From this point on, each install or check appends a full row (both complete MD5s, computed on the PC) to
`C:\FC 27 Live Editor\turbo_output\BUILD_IDENTITY.md` with `tools\turbo-x\Record-BuildIdentity.ps1`; that file is the live log,
this one is the history before it existed.

| label | Turbo.dll MD5 (prefix, as stated) | git commit (as stated) | context (as stated) | row source |
|---|---|---|---|---|
| DLL A | cc831f13 | 075a53a9 (integration commit) | TEST install 2026-10-31 | review pack |
| 5.0b | c07f2c87 | unknown | kept build | review pack |
| v2.21.0 release | 5c7e899e | not stated | release DLL | review pack |
| DLL B | c173ac08 | unknown | boot failed | review pack |
| DLL C | 3a8e4094 | 00b28622 (per the hand-off) | pass-4 | review pack |
| p3r2 | 547258bf | not stated | not stated | review pack |

Live log row format (written by Record-BuildIdentity.ps1):

```
| <timestamp> | <git commit> | <source zip or branch> | <packaged Turbo.dll MD5> | <installed Turbo.dll MD5> | <note> [MATCH|MISMATCH] |
```

| 2026-10-31T18:18Z | (owner's own build, commit unknown) | C:\FC 27 Live Editor\turbo\Turbo.dll as installed | — | c07f2c878bd7434e393c7d2bdaab2f45 | read by the Claude cloud session through the folder bridge before any Turbo X test; 16,313,856 bytes; copy kept at turbo_backups\claude_dll_20261031_181803\ |
| 2026-10-31T18:17Z | 1d25688 (src ff1c547) | dist/TurboX_1d25688_8b23d9c1.zip → C:\FC_Tools\TurboX\ | 8b23d9c157f7c33475ed8f9ad5fa0bc1 | c07f2c878bd7434e393c7d2bdaab2f45 | placed, NOT installed; the live DLL is untouched [MISMATCH by design: not installed] |
