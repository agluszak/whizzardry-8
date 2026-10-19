# Agent instructions

## What this project is

Whizzardry 8 is a **modernization and simplification** of
[Wizardry 8 Decomp](https://github.com/agluszak/wizardry-8-decomp).
The decompilation is a source of game logic and historical context, **not** an
architecture, API, or binary-layout specification for the native program.

The goal is clean, maintainable, idiomatic **C++26** that runs natively on
64-bit Linux, macOS, and Windows. Preserve the game's behavior, content, visual
identity, and compatibility with retail assets and save data. Do **not** preserve
the original implementation's accidental complexity.

This is **not** the faithful recompilation project. We do not need matching
assembly, original compiler behavior, 32-bit runtime layouts, Windows API
emulation, Wine support, or compatibility with removed internal APIs.
Do not reintroduce them.

## Engineering principles

- **Simplify and delete.** Prefer removing obsolete layers, duplicated code,
  dead state, and redundant abstractions to renaming or wrapping them. When an
  old API is replaced, migrate its callers and remove it. Do not add
  compatibility shims, parallel implementations, or speculative fallbacks.
- **Remove SGP.** SGP's bespoke managers, integer handles, registries, queues,
  containers, allocators, button/input machinery, and C-style utilities are
  migration targets, not foundations to build on. Replace them with ordinary
  C++ objects and existing libraries. Keep required game functionality; do
  not retain infrastructure merely because the decompiled code used it.
  Preserve required third-party license notices.
- **Use the standard library.** Prefer standard containers, algorithms,
  strings, views, smart pointers, `std::filesystem`, `std::chrono`, and other
  standard facilities to homegrown equivalents. Do not write custom sorting,
  search, ownership, or collection machinery when the standard library
  already expresses the operation.
- **Use SDL directly.** SDL3 owns platform-facing windows, events, input,
  surfaces, pixels, and GPU integration. Use SDL3_image and SDL3_mixer where
  appropriate, and the existing FFmpeg integration for video. Avoid
  reconstructing SGP or Win32-shaped wrappers around these APIs. An adapter
  should exist only when it encodes real game-specific semantics.
- **Make ownership obvious.** Use RAII and value members by default.
  `std::unique_ptr` expresses exclusive ownership; `std::shared_ptr` is for
  genuinely shared ownership, not as a default. Borrow through references,
  pointers, `std::span`, and `std::string_view` when appropriate. Eliminate
  manual teardown, global resource slots, integer object handles, and
  `Init`/`Destroy` pairs when object lifetimes can do the work.
- **Design normal C++ APIs.** Return results as values instead of mutable
  output parameters when practical. Prefer clear types, small functions,
  standard algorithms, and explicit dependencies to macros, casts, mutable
  globals, opaque flags, and duplicated loops. Do not introduce a framework
  for a problem that needs a straightforward function or class.
- **Consolidate, don't multiply systems.** Aim for one coherent ownership and
  event model for UI, one clear path for image/sprite resources, and consistent
  color and rendering representations. Build on the existing native direction;
  do not create another competing subsystem. Preserve the original game's
  artwork, layout, and interactions.
- **Separate disk formats from live objects.** The compatibility contract is
  reading and writing retail data: retail saves must load, and saves we write
  must stay loadable by the retail format. Exact sizes, field widths, packing,
  offsets, and encodings matter only in the serialized records at those file
  boundaries. Runtime classes may change layout, members, and ownership
  freely; never keep a field, duplicate pointer, or ordering only to mirror
  retail memory offsets. Use explicit fixed-width serialized records and
  conversion code; give runtime types normal C++ ownership and layout. Keep
  unknown serialized bytes intact. Runtime strings are UTF-8; decode and
  encode retail UTF-16LE explicitly.
- **Prefer understandable code over clever code.** No assembly-matching
  tricks, deliberate undefined behavior, unsafe pointer punning, unnecessary
  metaprogramming, or elaborate generic infrastructure. Modern C++ is a means
  to clearer code, not an excuse for complexity.

These are migration goals, not claims that every subsystem already satisfies
them. Avoid entrenching transitional code as a permanent public interface.

## Tests: fast, useful, justified

**Every test must earn its maintenance cost.** Before writing one, be able to
say what plausible bug or important behavioral contract it protects. Do not
write tests for the sake of having tests, achieving a coverage number, or
exercising a trivial getter, constructor, or thin standard-library wrapper.
Do not duplicate coverage that already catches the same regression.

- Prefer **small, deterministic, fast** tests of observable behavior.
  Exercise real invariants, format boundaries, ownership/lifetime mistakes,
  gameplay rules, and regressions we have encountered.
- Use the existing **Catch2 and CTest** setup. Reuse small fixtures and extract
  repeated setup, but keep tests independent and readable. Avoid bespoke test
  runners, hidden ordering dependencies, and oversized fixture frameworks.
- Avoid starting the entire game, initializing a GPU, loading retail assets,
  or sleeping merely to test logic that can be checked directly. Reserve
  expensive integration/runtime scenarios for behavior that genuinely needs
  the real engine. Keep those scenarios representative rather than exhaustive.
- Do not enumerate every artificial failure point or combinatorial variation
  without a concrete reason. A handful of meaningful failure and lifetime
  cases beats a slow, brittle matrix.
- Refactoring does not automatically require new tests. Prefer strengthening
  an existing test when it covers the change. Delete or consolidate redundant,
  flaky, obsolete, and low-signal tests, especially when they slow down CI.
- Test helpers must be simpler than the code they support. Do not distort
  production APIs solely to make testing convenient.

Run the smallest relevant checks while iterating. Run broader build and test
checks when the scope warrants them. Report what was actually run; do not
claim unexecuted tests pass.

## How to work

Understand current ownership and callers before changing an API. Make the
simplification end-to-end: update consumers, remove dead code, and do not
leave two ways to do the same thing. Favor focused, reviewable changes with
a clear net reduction in complexity. Consult `README.md`,
`UI_MODERNIZATION.md`, and `NATIVE_WORK.md` for existing conventions and
remaining migration work; where those notes describe transitional machinery,
the principles above describe the desired end state.
