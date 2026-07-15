# Contributing to Mini RTOS Simulator

Thank you for your interest in contributing.  This guide covers everything you
need to build the project, understand the coding style, submit a change, and
get it reviewed.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Build Instructions](#build-instructions)
3. [Project Layout](#project-layout)
4. [Coding Style](#coding-style)
5. [Commit Message Format](#commit-message-format)
6. [Pull Request Guidelines](#pull-request-guidelines)
7. [Adding a New Module](#adding-a-new-module)
8. [Design Principles](#design-principles)

---

## Prerequisites

| Tool | Minimum Version | Notes |
|------|----------------|-------|
| GCC or Clang | Any C99-conformant release | `gcc -std=c99` |
| GNU Make | 3.81+ | Ships with macOS Xcode CLT and most Linux distros |
| clang-format | 14+ | Optional; used by `make format` |
| Doxygen | 1.9+ | Optional; used by `make docs` |

No embedded toolchain, hardware, or OS-specific library is required.

---

## Build Instructions

```sh
# Clone
git clone https://github.com/Sarath-Patti/Mini-RTOS-sim.git
cd Mini-RTOS-sim

# Build demo + test binaries
make

# Run the demo application
make run

# Run the test suite (must pass before any PR is submitted)
make test

# Auto-format source files (requires clang-format)
make format

# Remove build artefacts
make clean

# Show all available targets
make help
```

The build must produce **zero warnings** under `-Wall -Wextra -Wpedantic`.
If your change introduces a warning, fix it before submitting.

---

## Project Layout

```text
include/    Public and internal header files (one per module)
src/        Implementation files (one per module)
tests/      Test suite (test_states.c)
Makefile    Build system
```

Each module is a header/source pair (`foo.h` / `foo.c`).  Every public type
and function is declared in the header.  Implementation details stay in the
source file.

---

## Coding Style

The project follows the coding conventions already present in the source.  Read
one or two source files before writing new code.

### Formatting

- **Indentation:** 4 spaces.  No tabs.
- **Line length:** 80 columns preferred; 100 columns hard limit.
- **Braces:** K&R style — opening brace on the same line as the control
  statement, closing brace on its own line.
- **Blank lines:** one blank line between function definitions; no trailing
  blank lines.

Run `make format` to apply `clang-format` automatically.

### Naming

| Element | Convention | Example |
|---------|-----------|---------|
| Types (struct, enum, typedef) | `PascalCase` | `TaskState`, `SoftTimer` |
| Enum values | `SCREAMING_SNAKE_CASE` | `TASK_READY`, `BLOCK_EVENT` |
| Functions (public API) | `module_verb_noun()` | `rtos_create_task()`, `memory_alloc()` |
| Functions (static internal) | `verb_noun()` | `timer_update_soft()`, `block_index()` |
| Global variables (static) | `g_snake_case` | `g_kernel_tick`, `g_free_list_head` |
| Macros | `SCREAMING_SNAKE_CASE` | `RTOS_MAX_TASKS`, `POOL_END_OF_LIST` |
| Parameters and locals | `snake_case` | `task_index`, `period_ticks` |

### Comments

- **File header:** every `.c` and `.h` file starts with a block comment
  describing its purpose, design constraints, and public API.
- **Function comments:** public functions are documented in the header, not the
  source, unless the implementation has non-obvious behaviour.
- **Inline comments:** used sparingly to explain *why*, not *what*.
- **Do not** comment out code; delete it and use git history.

### Safety Rules

- No `malloc`, `calloc`, `realloc`, or `free`.
- No `setjmp` / `longjmp`, `ucontext`, threads, or platform-specific APIs.
- No global variables with external linkage (everything is `static`).
- No circular includes.
- All pointer parameters are checked for `NULL` at the top of every function.

---

## Commit Message Format

Follow the [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/)
specification.

```
<type>(<scope>): <short summary>

[optional body — explain WHY, not WHAT]

[optional footer — Breaking Change or Issue reference]
```

### Types

| Type | When to use |
|------|-------------|
| `feat` | New user-visible feature |
| `fix` | Bug fix |
| `docs` | Documentation only |
| `style` | Formatting, whitespace (no logic change) |
| `refactor` | Code restructuring without behaviour change |
| `test` | Adding or correcting tests |
| `chore` | Build system, CI, tooling |

### Examples

```
feat(timer): add one-shot and periodic software timers

Introduces SoftTimer pool, timer_update_soft(), and the four-function
public API (create/start/stop/delete).  Processing is hooked into the
existing timer_update_sleep() call site so no scheduler changes are needed.
```

```
fix(scheduler): prevent blocked task from entering Continue path

A task that transitions to TASK_BLOCKED during its own run could
previously re-enter the Continue branch on the next tick.  Add a
runnability guard that forces a context switch when the current task
is not TASK_READY or TASK_RUNNING.
```

```
docs(readme): rewrite for v2.0 release

Adds ASCII architecture diagrams, module responsibility table, design
decisions section, and full configuration reference.
```

---

## Pull Request Guidelines

1. **Fork** the repository and create a feature branch from `main`.
   Use the naming pattern `feat/<short-description>` or `fix/<short-description>`.

2. **One concern per PR.**  A PR that adds a feature and fixes an unrelated bug
   is hard to review and harder to revert.

3. **All tests must pass.**  Run `make test` before pushing.  A PR that breaks
   the test suite will not be merged.

4. **No new warnings.**  The CI build uses `-Wall -Wextra -Wpedantic`.

5. **Update documentation.**  If your change adds a new public API, update the
   relevant header comment and `README.md`.  If it changes behaviour, update
   `CHANGELOG.md`.

6. **Keep diffs minimal.**  Run `make format` before committing so that
   formatting noise does not obscure functional changes.

7. **Write a meaningful PR description** explaining what the change does and
   why it is necessary.  Reference any related issues.

---

## Adding a New Module

Follow this checklist when adding a new kernel module (e.g., `watchdog`):

1. Create `include/watchdog.h` with:
   - File header comment (purpose, design constraints, public API overview).
   - Include guard (`#ifndef MINI_RTOS_WATCHDOG_H`).
   - Any new types (`typedef struct { … } Watchdog;`).
   - Public API declarations with parameter and return-value documentation.

2. Create `src/watchdog.c` with:
   - File header comment (implementation notes).
   - `static` module-private variables prefixed with `g_`.
   - All pointer parameters checked for `NULL`.
   - `uart_log()` calls for every observable operation (init, trigger, reset).

3. Add `src/watchdog.c` to both `SRCS` and `TEST_SRCS` in `Makefile`.

4. If the module needs kernel-tick processing, hook into `timer_update_sleep()`
   or add a dedicated `watchdog_update()` call in `scheduler_run()`.

5. If the module needs to block tasks, add a new `BLOCK_WATCHDOG` value to the
   `BlockReason` enum in `rtos.h`.

6. Update `README.md`, `CHANGELOG.md`, and any affected header comments.

7. Add at least one test in `tests/test_states.c` that exercises the new module
   in isolation (one `rtos_init()` per test function).

---

## Design Principles

> These are not suggestions — they are invariants of the project.

- **No dynamic allocation** anywhere in the kernel.
- **No platform-specific code** — the kernel must build with `gcc -std=c99` on
  any POSIX or Windows host without modification.
- **One module, one responsibility** — do not mix scheduler logic into
  `timer.c` or memory logic into `scheduler.c`.
- **Dependency direction is fixed:** `event.c` may call into `scheduler.c`;
  `scheduler.c` must not call into `event.c`.
- **Every observable operation is logged** via `uart_log()`.
- **All tests are deterministic** — no randomness, no timing dependencies, no
  shared mutable state between test functions.
