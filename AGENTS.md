# Agent instructions

## What this project is

Whizzardry 8 is a **modernization and simplification** of
[Wizardry 8 Decomp](https://github.com/agluszak/wizardry-8-decomp). The
decompilation is a source of game logic and historical context, **not** an
architecture, API, or binary-layout specification.

The goal is clean, maintainable, idiomatic **C++26** that runs natively on 64-bit
Linux, macOS, and Windows. Preserve the game's behavior, content, visual identity,
gameplay timing (including random draws), and compatibility with retail assets and
save data. Do **not** preserve the original implementation's accidental complexity:
no matching assembly, original compiler behavior, 32-bit runtime layouts, Windows
API emulation, Wine support, or removed internal APIs.

## What we are doing: ownership instead of globals

The decompiled code is C-era spaghetti: free functions that reach any state they
want through global lookups (`CurrentGame()`, `GetWorld()`, `Data()`, `MainGame()`,
`MainGameHud()`, `FindScreen<T>()`) and through chains of pointers into other
objects. Most remaining work is turning that into ordinary objects with clear
owners. Every change should move the code toward this:

- **Clear ownership.** Every piece of state has exactly one owner, and the owner's
  lifetime is the state's lifetime. Use RAII and value members by default;
  `std::unique_ptr` for exclusive ownership, `std::shared_ptr` only for genuinely
  shared ownership. No manual teardown, `Init`/`Destroy` pairs, global resource
  slots, integer handles, or register/unregister protocols where a lifetime can do
  the work.
- **Encapsulation.** State owners expose complete operations that maintain their
  invariants; callers must not edit another owner's internals. Stateless
  calculations over explicit values can be free functions. Do not create a class
  merely to give old functions a receiver.
- **Explicit dependencies, matched to lifetime.** Retain dependencies in
  constructors when they define a continuing object's identity or lifetime
  (`Automap{world}`, a live actor's `World&`). Pass inputs that vary per
  operation. A stored `Game&` does not justify unrestricted access to everything
  the game owns.
- **No global mutable state or global accessors.** Do not add globals, singletons
  or `Current*()` lookups for gameplay. Move mutations to their real owner
  and cross-owner sequences to explicit coordinators (ARCHITECTURE.md).

What does **not** count as progress:

- Adding `Game&`/`World&` parameters to C-era free functions, so callers pass
  `CurrentGame()` instead. That moves the global lookup further away; it does not
  remove it.
- Moving a `GetWorld()` call up into the caller, or wrapping it in a helper.
- Reaching through pointers (`game.level.primary->monsters...`) instead of asking
  the owner.
- Piling operations and state onto `Game` until it becomes a god object. `Game`
  owns one play-through's parts; behavior belongs in those parts.
- Lowering a count of lookups. A migration is done when the whole operation uses
  its owner **and the old lookup or fallback is deleted**.

## Engineering principles

- **Simplify and delete.** Prefer removing obsolete layers, duplicated code, dead
  state and redundant abstractions to renaming or wrapping them. When an API is
  replaced, migrate its callers and remove it. No compatibility shims, parallel
  implementations, or speculative fallbacks; never leave two ways to do the same
  thing.
- **Remove SGP.** SGP's managers, integer handles, registries, queues, containers,
  allocators, button/input machinery and C-style utilities are migration targets,
  not foundations. Keep required game functionality and third-party license
  notices; drop the infrastructure.
- **Use the standard library** for containers, algorithms, strings, views, smart
  pointers, `std::filesystem` and `std::chrono`. No homegrown sorting, search,
  ownership or collection machinery.
- **Use platform and rendering libraries directly.** SDL3 owns windows, events,
  input, surfaces, pixels and audio (with SDL3_image and SDL3_mixer); VSG owns the
  render graph, graphics resources, views and presentation; FFmpeg plays video. Do
  not rebuild SGP- or Win32-shaped wrappers around them. An adapter exists only when
  it encodes real game semantics.
- **Design normal C++ APIs.** Return values instead of output parameters. Prefer
  clear types, small functions and standard algorithms to macros, casts, mutable
  globals, opaque flags and duplicated loops.
- **Give each operation an owner.** An owner's method enforces that owner's invariant;
  a named game-level operation sequences obligations across owners in a visible order;
  a calculation over explicit values needs no receiver. A class that only holds
  `Game&` and reaches through it does not establish a boundary, and a `Game` method
  that forwards an already complete local operation earns nothing. Retain constructor
  dependencies that define lifetime; pass changing inputs on the operation.
- **One fact, one representation.** No parallel mutable copies, cached summaries,
  flags or collections that must be kept in sync. Derive cheap values when needed.
- **New abstractions must pay for themselves.** A new class, wrapper, manager,
  callback interface, variant or event queue must remove more complexity than it
  adds. No type per processing phase, inheritance to share fields, or command lists
  that replay ordinary calls. Don't implement hypothetical generality.
- **Separate disk formats from live objects.** Retail saves must load, and saves we
  write must load in retail. Exact sizes, offsets, packing and encodings matter only
  in explicit fixed-width serialized records and their codecs. Runtime types use
  normal C++ layout and ownership; never keep a field or ordering only to mirror
  retail memory. Keep unknown serialized bytes intact. Runtime strings are UTF-8;
  retail UTF-16LE is decoded and encoded explicitly.
- **Prefer understandable code over clever code.** No assembly-matching tricks,
  undefined behavior, pointer punning or unnecessary metaprogramming.

## How to work

1. **Check for existing work.** Before starting an issue, search open PRs for
   `#N`; some drafts mention an issue without linking it.
2. **Understand the owner first.** Find the actual invariant and who should own it.
   Trace creation, use, suspension, replacement and destruction, not just the
   function where a problem surfaced. Decide whether this is an owner's operation,
   a cross-owner sequence or a stateless calculation before choosing a class.
   Preserve the behavioral distinctions in ARCHITECTURE.md. A lifecycle problem
   usually calls for changing when work happens or who owns it, not for a new
   state type or cleanup protocol.
3. **Pin behavior before refactoring.** When a refactor touches behavior that no
   test covers, first add a small characterization test that passes on the current
   code, then refactor against it. Gameplay rules, random draw order, save
   round-trips and lifetimes are the usual candidates.
4. **Migrate end to end.** Move every caller to the new owner and delete the old
   function, global or lookup in the same change.
5. **Account for complexity.** Refactors should normally be net-negative. Count
   owners, representations, lifecycle paths and levels of indirection, not just
   lines. A net-positive change needs a concrete correctness or functionality
   reason. If a fix adds another representation or lifecycle path, reconsider the
   design before polishing it. Passing tests show behavior is covered, not that the
   design is sound.
6. **Run the relevant checks** (see `CLAUDE.md`/`README.md` for build and test
   commands) and report what was actually run.

### Pull requests

- Keep PRs focused and reviewable.
- A PR body says `Closes #N` for each issue it resolves (not "Addresses #N").
- Use proper stacks: a PR that depends on another targets its parent PR's branch,
  not `main`, so it shows only its own commits. Rebase the stack when the parent
  changes.
- Explain in the PR body which state, code paths or obligations disappear.

## Tests

Every test must earn its maintenance cost: be able to say what plausible bug or
contract it protects.

- Prefer small, deterministic, fast tests of observable behavior: invariants,
  format boundaries, ownership and lifetime mistakes, gameplay rules, regressions.
- Use the existing Catch2 and CTest setup. Keep tests independent and readable; no
  bespoke runners, hidden ordering or oversized fixture frameworks.
- Don't start the whole game, a GPU or retail assets to test logic that can be
  checked directly. Keep runtime scenarios representative, not exhaustive.
- No coverage-driven tests of trivial getters, constructors or standard-library
  wrappers, and no combinatorial failure matrices without a concrete reason.
- Refactoring does not automatically need new tests; strengthen an existing one
  when it covers the change. Delete redundant, flaky and low-signal tests.
- Test helpers must be simpler than the code they support. Don't distort
  production APIs for testing.

## Documentation

- `ARCHITECTURE.md` describes current owners and layers plus the target
  ownership, operation and transition contracts. Update it when those change. It is not a
  change log: no per-PR notes, history of what used to be, or descriptions of a
  single class's internals that the code already says.
- `ROADMAP.md` lists remaining migration work. Delete an item when you finish it;
  record new architectural work there, not in a new file.
- `README.md` covers building, playing and diagnostics.
