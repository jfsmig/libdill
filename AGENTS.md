# AGENTS.md

Guidance for AI coding agents (and humans) working in this repository.

## Project

libdill is a C library providing structured concurrency: coroutines, channels,
handles, deadlines, and a set of composable network protocols (TCP, IPC, UDP,
TLS, HTTP, WebSocket, SOCKS5, ...). It is a low-level library with a stable
public ABI, used on Linux, macOS and the BSDs. Licensed MIT/X11.

## Build and test

Autotools is the reference build and the one CI uses (`.travis.yml`,
`.github/workflows/ci.yml`, `.circleci/config.yml`):

```sh
./autogen.sh
./configure            # see options below
make
make check             # runs every program in tests/
```

Useful `configure` options: `--enable-debug`, `--enable-valgrind`,
`--enable-census`, `--enable-gcov`, `--enable-tls`, `--disable-threads`,
`--disable-sockets`, `--disable-shared`. Useful `CFLAGS` defines: `-DDILL_POLL`
(force the `poll` backend), `-DDILL_ARCH_FALLBACK`, `-DDILL_THREAD_FALLBACK`,
`-DDILL_SLOW_TESTS` (loosen timing assertions on slow machines).

Run a single test with `make check TESTS=tests/chan`; on failure read
`tests/<name>.log` and `test-suite.log`.

A CMake build (`CMakeLists.txt`) also exists. Its source, test and perf lists
are maintained by hand, separately from `Makefile.am`.

## Layout

| Path | Contents |
| --- | --- |
| `libdill.h` | Public API. `libdillimpl.h` is the API for protocol implementers. |
| `*.c`, `*.h` (root) | Library sources. Core: `cr`, `ctx`, `handle`, `chan`, `pollset`, `stack`, `now`. Sockets and protocols: the rest. |
| `epoll`/`kqueue`/`poll` `.inc` | Pollset backends, selected at compile time by `pollset.c`. |
| `dns/` | Vendored third-party DNS resolver. Do not restyle or refactor it. |
| `tests/` | One standalone program per area; exit status is the verdict. |
| `perf/` | Micro-benchmarks. |
| `tutorial/`, `examples/` | Sample programs, built but not run by `make`. |
| `man/`, `website/` | Generated documentation (see below). |

## Conventions in this codebase

- **C99**, 4-space indentation, no tabs, lines up to 80 columns, `/* */`
  comments, opening brace on the same line, `if(cond)` without a space.
- **Naming**: every symbol is prefixed `dill_` (macros `DILL_`). Short public
  names (`go`, `chsend`, ...) are `#define` aliases in `libdill.h`. Library
  sources define `DILL_DISABLE_RAW_NAMES` and use the prefixed names only;
  tests and tutorials use the short ones.
- **Errors**: functions return `-1` (or `NULL`) and set `errno`. Use the
  `goto errorN` unwinding ladder, releasing resources in reverse order of
  acquisition, and restore `errno` before returning. Wrap unlikely branches in
  `dill_slow()` and hot ones in `dill_fast()`.
- **Invariants** are checked with `dill_assert()`; conditions a caller can
  trigger are reported through `errno`, never asserted.
- **Blocking functions** take a deadline as their last argument and must
  honour cancellation (`ECANCELED`) and timeouts (`ETIMEDOUT`).
- **Memory**: where an object can be allocated by the caller, provide a `_mem`
  variant taking opaque storage, and guard its size with `DILL_CT_ASSERT`.
- **Symbols**: the library is built with `-fvisibility=hidden`; public
  functions need `DILL_EXPORT` in the header.
- **New files** carry the MIT license header and must be added to
  `Makefile.am` and to `CMakeLists.txt`.
- **Tests** use `errno_assert()`, `choose_assert()` and `time_assert()` from
  `tests/assert.h`. A new test must be registered in `check_PROGRAMS`
  (`Makefile.am`) and in the test list of `CMakeLists.txt`.
- **Documentation** is generated: `man/manpages.src` (a Node.js script) is the
  single source for `man/*.3` and the function pages in `website/`. Edit the
  source, then run `man/generate.sh` and `website/generate.sh` (both need
  `pandoc`). Never hand-edit the generated `.3` or `.html` files.
- **ABI**: any change to `libdill.h` or `libdillimpl.h` may affect the ABI.
  The `DILL_VERSION_CURRENT/REVISION/AGE` macros in `libdill.h` follow libtool
  versioning; flag ABI-affecting changes explicitly instead of bumping the
  numbers silently.

## Engineering guidelines

### Before changing code

- Read the code you are about to change, and its callers, before proposing
  anything. Follow the existing patterns of the surrounding file over personal
  preference or generic best practice.
- Establish what the request actually requires. If it is ambiguous in a way
  that changes the design, ask; otherwise pick the conventional reading, say
  which one you picked, and proceed.
- Reproduce a bug before fixing it, and find its cause rather than patching
  the symptom.

### Scope

- Make the smallest change that fully solves the problem. No drive-by
  refactoring, reformatting, renaming or dependency changes in the same patch.
- Do not add abstractions, options or extension points for needs that do not
  exist yet.
- Do not add dependencies. The core library depends only on libc and pthreads;
  TLS support is the one optional exception (OpenSSL).
- Report unrelated problems you notice instead of fixing them unasked.

### Correctness

- Handle every error return. Every allocation, file descriptor and handle must
  be released on every path, including the error and cancellation paths.
- Treat everything read from the network as hostile: validate lengths before
  using them, check for integer overflow in size arithmetic, and never trust a
  peer-supplied size for an allocation or a copy.
- Keep the code portable: no Linux-only calls outside the backend that is
  specific to it, no assumptions about endianness, alignment or type widths
  beyond what C99 guarantees.
- Keep both threaded and `--disable-threads` builds working; per-thread state
  goes through `ctx`, never through plain globals.
- The compiler should stay quiet. Fix warnings; do not silence them with casts
  or pragmas unless the reason is written next to them.

### Testing

- Every behaviour change comes with a test that fails without it. Bug fixes
  come with a regression test, committed before the fix (see "Commits and
  reviews").
- Run `make check` before declaring work finished. For anything touching
  coroutine stacks, context switching or memory ownership, also run it under
  `--enable-valgrind` or with a sanitizer.
- Tests must be deterministic: no dependence on external hosts, fixed ports
  that may be taken, or tight wall-clock timing beyond what `time_assert()`
  tolerates.
- Never weaken, skip or delete a failing test to make the suite pass. If a
  test is wrong, say why and fix the test in its own commit.

### Readability

- Comments are welcome, but they never replace readable code. Readability
  comes first from symbols with meaningful names: functions, variables, types
  and constants whose names say what they are for.
- Prefer a short function with a comment above it to a comment on a block of
  code, and avoid comments on individual lines most of all. A block that needs
  a comment to be understood is usually a function waiting to be extracted and
  named.
- Comments explain why, not what. Keep existing comments accurate when the
  code under them changes.
- Prefer plain, explicit code over clever code. This library already contains
  the necessary amount of black magic (`go()`, stack switching); do not add to
  it without a measured reason.
- Optimise only with a benchmark from `perf/` showing the gain.

### Commits and reviews

- One logical change per commit, building and passing tests on its own.
- A bug fix takes two commits, in this order: first the regression test that
  reproduces the problem, then the fix. The test commit is the only exception
  to the rule above: it must build, and the new test must fail because of the
  bug and for no other reason. Run it and check the failure before writing
  the fix. The fix commit makes that test pass without modifying it.
- Subject line in the imperative, under ~70 characters, optionally prefixed
  with the area (`configure.ac: fix ssl static build`). Use the body to
  explain why the change is needed, not to restate the diff.
- Keep commit messages concise. There is no fixed length limit, but every
  line must earn its place: no restating of the subject, no file-by-file
  inventory of the diff, no account of how the change was arrived at, no
  filler. A trivial change needs a subject line and nothing else.
- AI attribution: never add a `Co-Authored-By` trailer for an AI tool, under
  any circumstances. Use an `Assisted-By` trailer instead. It names the tools
  used (for example `Assisted-By: Claude Code`), one trailer per tool. It does
  not name the model or its version, and it never contains an email address:
  hosting platforms resolve trailer addresses to accounts and would link the
  commit to one. The same applies to pull request descriptions.
- Never commit generated build output (`configure`, `Makefile.in`, objects,
  test logs) or secrets. `tests/cert.pem` and `tests/key.pem` are throwaway
  test fixtures, not credentials to reuse.
- Do not rewrite published history, force-push, or commit and push without
  being asked.

### Reporting

- State what was done, what was verified and how, and what was not. A build
  that was not run, or a test that still fails, is said plainly.
- When blocked, describe the obstacle and the options instead of working
  around it with a hack.
