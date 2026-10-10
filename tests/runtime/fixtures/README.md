# Runtime fixtures

`vi.CHR` was generated on 2026-10-10 by the native `character` runtime
scenario at commit `ac41d0c`, using the installed GOG 22306 assets and
`WIZ8_RUNTIME_KEEP_USER_ROOT=1`. It contains the character created through
the real UI: a level-one male human fighter named `vi`, with 18 HP and
zero experience. The file has a four-byte record-size prefix (6242) and
a version-one character record.

Keep this file fixed so the runtime scenarios also exercise loading a
previously written character. It was produced by the native port and is
not an independent retail compatibility reference.

`runtime_save-fixture` starts a new game with this character, walks a few
steps and quick-saves through the key binding. CTest writes `Fixture.SAV`
and its expected position, level and experience to the build directory's
`tests/runtime-fixtures`, then supplies that directory to `runtime_load-game`.
The generated save tests the current writer and menu loader together; it
does not prove compatibility with historical retail saves.
