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

`runtime_quick-save` starts a new game with this character, walks a few
steps and quick-saves through the key binding. It copies the real save into
`Fixture.SAV`, along with its expected position, level, experience and HP,
in the build directory's `tests/runtime-fixtures`. It then walks away,
changes character fields and checks that quick-load restores them.

CTest runs `runtime_quick-save` as the fixture setup before
`runtime_load-game`, which loads that same file through the menu in a fresh
game process. The generated save tests the current writer and menu loader
together; it does not prove compatibility with historical retail saves.

`runtime_menu-save` independently creates a named save through the options UI,
cancels and confirms overwrite and rename requests, reloads the changed character
record, and cancels and confirms deletion. It also replaces the enumerated save
with a directory to verify that a failed delete shows an error and leaves the
row usable for a retry. All writable files stay in the scenario's temporary user
root; installed assets are read-only inputs.

`runtime_retail-save` loads a save written by the retail game: Peter and
TBell, a level-23 pair just outside the Rapax Away Camp, offered by its author
at <https://r0k.us/rock/games/wizardry8/>. It is not checked in.
`tests/runtime/fetch_downloads.cmake`, run as the `runtime_downloads` CTest
fixture, downloads it once into `WIZ8_DOWNLOAD_CACHE` (by default
`~/.cache/whizzardry8/downloads`, shared by every build tree) and verifies it
by SHA-256; CI keeps that directory in the Actions cache. The scenario checks
the save's level, position and party, walks, then quick-saves and quick-loads
it through our writer.

## Images: failure frames and snapshots

CTest gives every runtime scenario its own `WIZ8_RUNTIME_ARTIFACT_DIR`
(`tests/runtime-artifacts/<scenario>` in the build tree), emptied of PNGs when
the scenario starts. Without the variable nothing is written.

- When an assertion fails or an exception (including `FailStep`) leaves a
  scenario while the game runs, the harness saves the presented frame as
  `failure-frame<N>.png`, and the 2D interface's CPU frame buffer as
  `failure-frame<N>-ui.png`, and prints both paths. CI uploads the directory
  as the `runtime-artifacts-<preset>` artifact when the test step fails.
- `Snapshot(name, pixels)` and the script command `snap NAME` save frames
  there for inspection.
