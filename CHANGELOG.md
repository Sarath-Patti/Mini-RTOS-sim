# Changelog

All notable changes to Mini RTOS Simulator are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions correspond to git tags in the repository.

---

## [v2.0] — 2026-07-15 — Final Release

### Added
- Professional open-source `README.md` with ASCII architecture diagrams,
  module responsibility table, design decisions, and configuration reference.
- `CHANGELOG.md` (this file).
- `LICENSE` (MIT).
- `CONTRIBUTING.md` with build instructions, coding style guide, and PR
  guidelines.
- `Makefile`: `help`, `format` (clang-format), and `docs` (Doxygen) targets.
- Improved Makefile header comment documenting all targets.

### Changed
- `README.md` completely rewritten; all content verified against the
  implementation.

### Fixed
- Nothing (no functional changes in this milestone).

---

## [v1.9] — 2026-07-15 — Software Timers & Event Flags

### Added
- **`include/event.h`** — `EventFlags` and `EventWaiter` types; full event
  flag API with auto-reset semantics and 32-bit bitmask.
- **`src/event.c`** — Event flag implementation: `event_flags_init()`,
  `event_flags_set()`, `event_flags_clear()`, `event_flags_wait()`,
  `event_flags_get()`.  Waiter table uses reverse-scan compaction for O(1)
  dequeue.
- **Software timer API** in `timer.h` / `timer.c`: `TimerMode` enum,
  `SoftTimer` struct, `timer_soft_create()`, `timer_soft_start()`,
  `timer_soft_stop()`, `timer_soft_delete()`.
- Static soft-timer pool (`RTOS_SOFT_TIMER_COUNT` slots) processed by the
  private `timer_update_soft()` function called from `timer_update_sleep()`.
- `BLOCK_EVENT` added to the `BlockReason` enum in `rtos.h`.
- `RTOS_SOFT_TIMER_COUNT` configuration macro in `rtos.h`.
- `scheduler_current_task_index()` in `scheduler.h` / `scheduler.c` — allows
  `event.c` to record the blocked task's index without depending on `rtos.c`.

### Changed
- `timer_update_sleep()` now calls `timer_update_soft()` after processing
  sleepers.
- `src/rtos.c` includes `event.h`; no kernel-init call required for event
  flags (objects are initialised individually with `event_flags_init()`).
- `Makefile`: `src/event.c` added to both `SRCS` and `TEST_SRCS`.

---

## [v1.8] — 2026-07-14 — Fixed-Size Memory Pool Allocator

### Added
- **`include/memory.h`** — pool allocator API and full design documentation.
- **`src/memory.c`** — embedded free-list allocator with O(1) alloc/free,
  `memcpy`-based link I/O to avoid strict-aliasing violations, and
  double-free detection via a parallel `uint8_t` bitmap.
- Public API: `memory_init()`, `memory_alloc()`, `memory_free()`,
  `memory_available_blocks()`.
- `RTOS_POOL_BLOCK_SIZE` and `RTOS_POOL_BLOCK_COUNT` configuration macros.

### Changed
- `rtos_init()` now calls `memory_init()`.
- `src/rtos.c` includes `memory.h`.
- `Makefile`: `src/memory.c` added to both `SRCS` and `TEST_SRCS`.

---

## [v1.7] — 2026-07-14 — Preemptive Priority Scheduler

### Added
- Priority preemption: a higher-priority READY task immediately displaces the
  running task on the next tick.
- Time-slice round-robin: equal-priority tasks rotate after
  `RTOS_TIME_SLICE_TICKS` consecutive ticks.
- `RTOS_TIME_SLICE_TICKS` configuration macro (default: 1, strict
  round-robin).
- `slice_ticks_used` field in `TCB`.
- `do_context_switch()` internal helper in `scheduler.c` — six-phase atomic
  switch sequence with structured logging.
- Forced-switch guard: a BLOCKED or SUSPENDED task can never enter the
  "Continue" path.
- Detailed scheduler log tags: `[First tick]`, `[Forced switch]`,
  `[Preempted]`, `[Slice expired]`.
- `slice_ticks_used` initialised to `1` on every context switch-in (not 0)
  so the slice-expiry threshold is exact.

### Changed
- `src/rtos.c`: `slice_ticks_used` initialised in `rtos_create_task()`.

---

## [v1.6] — 2026-07-14 — Kernel Tick & Timer Management

### Added
- **`include/timer.h`** — kernel tick and sleep management API.
- **`src/timer.c`** — monotonic `g_kernel_tick`, `timer_init()`,
  `timer_tick()`, `timer_now()`, `timer_update_sleep()`.
- `timer_init()` integrated into `scheduler_init()`.
- Sleep expiry logic (`timer_update_sleep()`) moved from `scheduler.c` into
  `timer.c`.

### Changed
- `scheduler.c` delegates all tick tracking to `timer.c`.
- `Makefile`: `src/timer.c` added to both source lists.

---

## [v1.5] — 2026-07-14 — Software Context Switch

### Added
- `context_switch(outgoing, incoming)` in `context.c` — atomically saves the
  outgoing task's register state and loads the incoming task's state into
  `active_context`.
- `context_active()` — returns a pointer to the live `active_context`.
- Scheduler integration: `scheduler.c` calls `context_switch()` on every task
  handoff.
- Log messages: `Context Saved`, `Context Restored`.
- Tests: `test_context_save_updates_destination()`,
  `test_context_restore_expected_registers()`,
  `test_contexts_do_not_overwrite()`.

### Changed
- `scheduler_run()` now drives a full save/restore cycle on each context
  switch rather than just updating the task index.

---

## [v1.4] — CPU Context Model

### Added
- `CPUContext` struct in `context.h`: R0–R12, LR, PC, xPSR (Cortex-M layout).
- `context_init()` — initialise a context with EXC_RETURN LR and Thumb xPSR.
- `context_save()`, `context_restore()`, `context_copy()`.
- `CPUContext context` field added to `TCB`.
- Tests: `test_task_context_ownership()`.

---

## [v1.3] — Per-task Stack Management

### Added
- `stack_memory[RTOS_STACK_WORDS]`, `stack_size`, `stack_pointer` fields in
  `TCB`.
- `RTOS_STACK_SIZE` configuration macro (default: 256 bytes).
- `init_task_stack()` in `rtos.c` — zeroes the stack and sets the stack
  pointer to the top.
- `rtos_get_task_stack_base()`, `rtos_get_task_stack_pointer()`,
  `rtos_get_task_stack_size()` public API functions.
- Tests: `test_task_stack_allocation()` verifies non-overlap and correct
  sizes.

---

## [v1.2] — Synchronisation Primitives

### Added
- Counting `Semaphore` with `rtos_sem_wait()` / `rtos_sem_signal()`.
- `Mutex` with owner tracking: `rtos_mutex_lock()` / `rtos_mutex_unlock()`.
- `MessageQueue` (fixed `int` payload): `rtos_queue_send()` /
  `rtos_queue_receive()`.
- `semaphore_init()`, `mutex_init()`, `queue_init()` initialisers.
- `BlockReason` enum: `BLOCK_SEMAPHORE`, `BLOCK_MUTEX`, `BLOCK_QUEUE_EMPTY`,
  `BLOCK_QUEUE_FULL`.
- `scheduler_unblock_one()` — wakes the highest-priority waiter for a given
  block reason.
- Demo (`main.c`) updated to use all three primitives.

---

## [v1.1] — Cooperative Scheduler

### Added
- Static ready queue in `scheduler.c`.
- `scheduler_run(max_ticks)` — tick-driven cooperative scheduling loop.
- `scheduler_set_task_state()`, `scheduler_set_current_task_state()`.
- `BLOCK_SLEEP` and `rtos_task_sleep(n)`.
- `rtos_ready_count()`.
- `uart_log()` with tick-stamped output.

---

## [v1.0] — Task Control Blocks

### Added
- Initial project structure: `include/`, `src/`, `tests/`, `Makefile`.
- `TCB` struct: `task_id`, `priority`, `state`, `block_reason`, `sleep_ticks`,
  `task_function`, `name`.
- `TaskState` enum: `TASK_READY`, `TASK_RUNNING`, `TASK_BLOCKED`,
  `TASK_SUSPENDED`.
- `rtos_init()`, `rtos_create_task()`, `rtos_run()`.
- `rtos_suspend_task()`, `rtos_resume_task()`, `rtos_get_task_state()`.
- `scheduler_init()` in `scheduler.c`.
- Initial test suite in `tests/test_states.c`.
