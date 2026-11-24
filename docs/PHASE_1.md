> Historical snapshot. See [current phase status](PHASES.md) and [README](../README.md).

# Phase 1: project foundation

## Initial assessment

The supplied workspace was empty and was not a Git repository. The user then
authorized starting from Phase 1 in that same folder. There was no earlier code
or uncommitted work to preserve.

## Implemented

- Server, client, shared include, test, data, logs, and documentation directories.
- Separate C entry points with default output, help, version, and argument errors.
- Central constants in `include/config.h`; string capacities include NUL bytes.
- Linux Makefile with separate compilation, header dependencies, warning flags,
  debug symbols, build/clean/debug targets, and executable smoke checks.
- Ignore rules for build products, logs, account storage, and temporary files.
- LF policy for source and build files transferred from Windows.
- Dependency checks and instructions for transfer to Kali/Mint.

## Verification status

Preparation environment: Windows PowerShell. Git and Python are available.
GCC and Make were not found on PATH. An earlier WSL distribution check returned
`Wsl/E_ACCESSDENIED`. Consequently Linux builds and executable smoke checks have
not run here. No claim of Linux compilation success or runtime correctness is
made. Local file checks are recorded in the handoff response.

Phase 1 acceptance is pending `make` and `make test` on Linux. If they fail,
provide the complete terminal output so failures can be resolved before Phase 2.

## Understanding the foundation

`main()` is each program's entry point. `argc` counts arguments and `argv`
contains their strings. `strcmp()` from `<string.h>` compares argument values.
`puts()`, `printf()`, and `fputs()` from `<stdio.h>` print normal output or errors.
Exit status zero signals success; nonzero signals invalid usage.

Each executable will run as a separate Linux process. No threads, sockets,
mutexes, semaphores, or condition variables have been introduced yet.

The Makefile compiles `.c` files into object files, then links each executable.
`-Iinclude` makes the shared configuration header discoverable. `-std=c11`
selects the C standard; warning flags catch common mistakes; `-g` retains debug
symbols. `-MMD -MP` records header dependencies, allowing a configuration change
to rebuild affected objects. `_POSIX_C_SOURCE=200809L` will expose POSIX APIs
needed by later Linux code. Python only tests the native C programs.

`.gitignore` prevents routine generated data from being added to Git; it does
not provide filesystem access control or protect files already tracked.

## Phase boundary

Current phase: Phase 1 foundation prepared, Linux verification pending.
Known implementation defects: none confirmed; Linux behavior remains unverified.
Phase 2 requires basic TCP socket setup, connection/error handling, stream
framing, and communication tests. Wait for user approval before beginning it.

Suggested commit: `chore: establish Linux chat project foundation`.
