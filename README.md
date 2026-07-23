<!--
  SPDX-License-Identifier: MIT
  Mini RTOS Simulator — README
-->

<div align="center">

# Mini RTOS Simulator

**A host-based, incremental Real-Time Operating System kernel written in portable C99.**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Build](https://img.shields.io/badge/build-passing-brightgreen)](#build-instructions)
[![Standard](https://img.shields.io/badge/C-C99-orange)](#build-instructions)

</div>

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Motivation](#motivation)
3. [Key Features](#key-features)
4. [Architecture Overview](#architecture-overview)
5. [Scheduling Workflow](#scheduling-workflow)
6. [Software Context Switching (CPU Context Simulation)](#software-context-switching-cpu-context-simulation)
7. [Simulation Scope](#simulation-scope)
8. [Module Responsibilities](#module-responsibilities)
9. [Repository Structure](#repository-structure)
10. [Build Instructions](#build-instructions)
11. [Running the Demo](#running-the-demo)
12. [Running Tests](#running-tests)
13. [Example Output](#example-output)
14. [Project Evolution](#project-evolution)
15. [Current RTOS Capabilities](#current-rtos-capabilities)
16. [Design Decisions](#design-decisions)
17. [Configuration Reference](#configuration-reference)
18. [Future Improvements](#future-improvements)
19. [Contributing](#contributing)
20. [License](#license)

---

## Project Overview

Mini RTOS Simulator is a fully self-contained, host-runnable RTOS kernel implemented in portable C99. It models the essential services of a production-grade embedded RTOS — task management, preemptive priority scheduling, software context switching, inter-task synchronisation, memory management, and software timers — without relying on any hardware, OS threads, or platform-specific code.

The project is designed to be read, understood, and extended one milestone at a time. Every module has a single, clearly documented responsibility. The complete kernel builds with `gcc -std=c99` and produces no warnings even under `-Wall -Wextra -Wpedantic`.

---

## Motivation

Most embedded RTOS textbooks and tutorials jump immediately to Cortex-M assembly, hardware timers, and linker scripts. This project takes the opposite approach: implement a complete, correct RTOS kernel entirely in portable C, run it on a developer laptop, verify it with a deterministic test suite, and only then consider porting it to silicon.

The goals are:

- **Readable first.** Every design decision is explained in the source.
- **Testable.** Kernel execution and tick simulation are purely deterministic software operations, so tests are reproducible without hardware.
- **Incremental.** Each git tag corresponds to one well-defined feature milestone so the full history tells the story of how an RTOS is built.
- **Port-ready.** The Cortex-M register layout, EXC_RETURN values, and NVIC conventions are modeled in the context structure.

---

## Key Features

✔ **Priority-based preemptive scheduling** — Highest-priority READY task always executes  
✔ **Round-robin scheduling** — Equal-priority tasks rotate fairly based on configurable time slices  
✔ **Task Control Blocks (TCBs)** — Complete task state, priority, sleep countdown, and stack pointers  
✔ **Ready queue management** — Static ready queue tracking runnable tasks  
✔ **Software context switching** — Simulated CPU context save and restore per task  
✔ **Kernel tick simulation** — Monotonic tick-driven scheduler and sleep management  
✔ **Software timers** — One-shot and periodic timer callbacks  
✔ **Mutexes** — Priority-capable binary locks with owner tracking  
✔ **Semaphores** — Counting semaphores for task synchronization  
✔ **Message queues** — Fixed-capacity FIFO queues for inter-task communication  
✔ **Event flags** — 32-bit bitmask event synchronization with auto-reset  
✔ **Deterministic memory pool allocator** — O(1) allocation/deallocation with double-free protection  
✔ **Static task allocation** — No dynamic heap memory (`malloc`/`free`)  
✔ **Modular kernel architecture** — Clean separation of scheduler, context, timer, memory, and event modules  
✔ **Comprehensive unit test suite** — Deterministic test suite validating task states, context ownership, and stacks  

---

## Architecture Overview

```text
                Application Tasks
                        │
                        ▼
                 RTOS Kernel API (rtos.c / rtos.h)
                        │
        ┌───────────────┼───────────────┐
        │               │               │
    Scheduler      Context Manager   Timer Manager
   (scheduler.c)     (context.c)       (timer.c)
        │               │               │
        ├───────────────┼───────────────┤
        │               │               │
 Synchronisation   Memory Manager     Event Manager
 (Mutex/Sem/Queue)   (memory.c)        (event.c)
```

### Detailed Kernel Module Interaction

```text
┌─────────────────────────────────────────────────────────────────────┐
│                       Application / Demo                            │
│  sensor_task()   logger_task()   display_task()   timer callbacks   │
└────────────────────────────┬────────────────────────────────────────┘
                             │ rtos_* API
┌────────────────────────────▼────────────────────────────────────────┐
│                         rtos.c  (Kernel Core)                       │
│  Task lifecycle  ·  Semaphore  ·  Mutex  ·  MessageQueue  ·  Logging│
└───────┬──────────────────┬───────────────────┬───────────────────────┘
        │ scheduler_*      │ timer_*            │ memory_* / event_*
┌───────▼──────┐  ┌────────▼───────┐  ┌────────▼──────────────────────┐
│ scheduler.c  │  │   timer.c      │  │  memory.c        event.c       │
│              │  │                │  │                                 │
│ Ready queue  │  │ g_kernel_tick  │  │ Fixed-size pool  Event flags   │
│ Priority sel │  │ Sleep mgmt     │  │ Free list O(1)   Waiter table  │
│ Time slicing │  │ Soft timers    │  │ Double-free det  BLOCK_EVENT   │
│ Context disp │  │                │  │                                 │
└───────┬──────┘  └────────────────┘  └───────────────────────────────┘
        │ context_switch()
┌───────▼──────────────────┐
│       context.c          │
│  CPUContext (R0–xPSR)    │
│  save / restore / switch │
└──────────────────────────┘
```

---

## Scheduling Workflow

The scheduler (`scheduler.c`) is a tick-driven, priority-based preemptive scheduler with round-robin time slicing among equal-priority tasks. On every scheduler cycle (`scheduler_run`):

1. **Kernel Tick & Timer Processing:** The kernel tick counter is advanced (`timer_tick()`), sleeping tasks are updated (`timer_update_sleep()`), and armed software timers evaluate and fire their callbacks.
2. **Ready Queue Management & Priority Selection:** The scheduler inspects the static READY queue (`peek_highest_priority()`) to identify the highest-priority runnable task.
3. **Preemption & Scheduling Decision:**
   - **Higher-Priority Preemption:** If a READY task has a higher priority than the running task, it immediately preempts the current task.
   - **Time-Slice Expiration:** If the highest-priority READY task has equal priority to the current task and the current task's time slice (`RTOS_TIME_SLICE_TICKS`) has expired, the current task yields and equal-priority tasks rotate in round-robin order.
   - **Forced Switch:** If the current task transitions to `TASK_BLOCKED` or `TASK_SUSPENDED`, a context switch is forced to the highest-priority READY task.
   - **Continuation:** If the current task remains runnable (`TASK_RUNNING`/`TASK_READY`), holds the highest priority, and its time slice has not expired, execution continues directly without context switch overhead.
4. **Task State Transitions & Context Switching:** When a context switch occurs, `do_context_switch()` saves the outgoing task's simulated CPU state into its TCB (`context_save`), updates task states, restores the incoming task's saved `CPUContext` (`context_restore`), and invokes the incoming task function.

---

## Software Context Switching (CPU Context Simulation)

Context switching in the Mini RTOS Simulator is implemented entirely in software in portable C99:

- **TCB CPU Context Storage:** Every task owns an independent `CPUContext` structure stored inside its Task Control Block (`TCB.context`).
- **Simulated CPU Registers:** The `CPUContext` struct simulates Cortex-M register layout (general-purpose registers `R0–R12`, link register `LR`, program counter `PC`, and status register `xPSR`).
- **Context Save & Restore:** During a context switch, the scheduler saves the active simulated CPU state from `active_context` into the outgoing task's TCB (`context_save`), and restores the incoming task's saved state into `active_context` (`context_restore`).
- **Software Implementation:** Context switching is performed completely in software via `context_switch()`. The project intentionally does not use architecture-specific assembly instructions (`PUSH`/`POP`), inline assembly, PendSV interrupt handlers, OS threads, setjmp/longjmp, or ucontext.

---

## Simulation Scope

This project defines a clear boundary between implemented RTOS kernel logic, simulated CPU components, and non-implemented hardware features:

### Implemented
✔ Priority-based preemptive scheduler  
✔ Software context switching engine  
✔ Kernel tick manager  
✔ Software timers (one-shot and periodic)  
✔ Synchronization primitives (Semaphores, Mutexes, Message Queues, Event Flags)  
✔ Fixed-size deterministic memory pool allocator  

### Simulated
• CPU registers (`CPUContext` modeling `R0–R12`, `LR`, `PC`, `xPSR`)  
• Context switching flow (software register state transfers)  
• Timer interrupts (simulated via host-driven `timer_tick()`)  

### Not Implemented
• Hardware interrupts and vector tables  
• ARM PendSV and SysTick hardware registers  
• Assembly-level context switching instructions  
• Memory Management Unit (MMU) / MPU protection  
• Hardware device drivers  
• Multicore / SMP scheduling  

---

## Module Responsibilities

| Module | File(s) | Owns |
|--------|---------|------|
| **Kernel Core** | `rtos.c` / `rtos.h` | Task lifecycle, semaphore, mutex, queue, logging |
| **Scheduler** | `scheduler.c` / `scheduler.h` | Ready queue, preemption, time-slice, context dispatch |
| **Timer** | `timer.c` / `timer.h` | Kernel tick, sleep countdown, software timer pool |
| **Context** | `context.c` / `context.h` | CPUContext save/restore/switch, active context register |
| **Memory** | `memory.c` / `memory.h` | Fixed-size pool, free list, double-free protection |
| **Event** | `event.c` / `event.h` | Event flag groups, waiter table, BLOCK_EVENT unblocking |

**Dependency rule:** modules may only depend on modules listed to their right in the table above (or on `rtos.h` for shared types). No circular dependencies exist.

---

## Repository Structure

```text
mini-rtos-sim/
├── include/
│   ├── context.h       CPUContext type and context API
│   ├── event.h         EventFlags type and event API
│   ├── memory.h        Memory pool API and design documentation
│   ├── rtos.h          Public kernel API, TCB, config macros
│   ├── scheduler.h     Internal scheduler API
│   └── timer.h         Kernel tick + software timer API
├── src/
│   ├── context.c       Software context switch implementation
│   ├── event.c         Event flag implementation
│   ├── main.c          Demo: sensor / logger / display tasks
│   ├── memory.c        Fixed-size pool allocator
│   ├── rtos.c          Kernel core: tasks, sync primitives, logging
│   ├── scheduler.c     Preemptive priority scheduler
│   └── timer.c         Tick management + soft timer engine
├── tests/
│   └── test_states.c   Task state, stack, and context tests
├── CHANGELOG.md        Milestone-by-milestone change log
├── CONTRIBUTING.md     Development guide
├── LICENSE             MIT License
├── Makefile            Build system
└── README.md           This file
```

---

## Build Instructions

**Requirements:** GCC (or any C99-conformant compiler), GNU Make.

```sh
# Clone the repository
git clone https://github.com/Sarath-Patti/Mini-RTOS-sim.git
cd Mini-RTOS-sim

# Build both the demo and the test binary
make

# Optional: auto-format source (requires clang-format)
make format
```

The build produces two executables in the repository root:

| Binary | Description |
|--------|-------------|
| `mini_rtos` | Demo application (sensor / logger / display tasks) |
| `test_states` | Unit test suite |

---

## Running the Demo

```sh
make run
# or
./mini_rtos
```

The demo runs a 25-tick simulation with three tasks:

| Task | Priority | Period | Role |
|------|----------|--------|------|
| `Sensor Task` | 3 (highest) | 3 ticks | Produces integer samples, sends to queue, signals semaphore |
| `Logger Task` | 2 | 2 ticks | Acquires semaphore, receives from queue, logs via mutex-protected UART |
| `Display Task` | 1 (lowest) | 4 ticks | Locks mutex and refreshes the display |

---

## Running Tests

```sh
make test
# or
./test_states
```

The project contains a comprehensive unit test suite in `tests/test_states.c` covering:
- READY task execution
- BLOCKED task skipping
- SUSPENDED task skipping
- Scheduler behavior & ready queue correctness
- Round-robin rotation among multiple READY tasks
- Private task stack allocation & non-overlap
- Task CPU context ownership
- Context save updating destination
- Context restore loading expected registers
- Memory isolation between CPU contexts

Each test function calls `rtos_init()` for clean state isolation.

Expected output:

```text
PASS: READY task execution
PASS: BLOCKED task skipping
PASS: SUSPENDED task skipping
PASS: multiple READY tasks in queue
PASS: private task stack allocation
PASS: private task CPU context ownership
PASS: context save updates only destination
PASS: context restore loads expected registers
PASS: CPU contexts do not overwrite one another
All task state, stack, and context tests passed
```

---

## Example Output

```text
Mini RTOS PC Simulator
----------------------
[00:00] Memory Pool Init: 16 blocks x 32 bytes = 512 bytes total
[00:00] Task Created: Sensor Task priority=3
[00:00] Task Created: Logger Task priority=2
[00:00] Task Created: Display Task priority=1
[00:01] [Tick 1] Context Switch [First tick]: (none) -> Sensor Task
[00:01] Queue Send by Sensor Task value=100 count=1
[00:01] Sensor Task produced sample=100
[00:01] Semaphore Released count=1
[00:01] [Tick 1] Sensor Task state=BLOCKED
[00:02] [Tick 2] [Forced switch]: Sensor Task (BLOCKED) -> Logger Task
[00:02] Semaphore Acquired by Logger Task count=0
[00:02] Queue Receive by Logger Task value=100 count=0
[00:02] Mutex Locked by Logger Task
[00:02] Logger Task stored sample=100
[00:02] Mutex Released by Logger Task
[00:02] [Tick 2] Logger Task state=BLOCKED
[00:03] [Tick 3] [Forced switch]: Logger Task (BLOCKED) -> Display Task
[00:03] Mutex Locked by Display Task
[00:03] Display Task refreshed
[00:03] Mutex Released by Display Task
[00:03] [Tick 3] Display Task state=BLOCKED
[00:04] [Tick 4] Idle: no READY tasks
...
```

---

## Project Evolution

Each git tag marks one completed milestone. The repository history is the complete narrative of how this RTOS was built.

| Tag | Milestone | Key Addition |
|-----|-----------|--------------|
| `v1.0` | Task Control Blocks | `TCB`, task states, task list |
| `v1.1` | Cooperative Scheduler | Ready queue, basic round-robin |
| `v1.2` | Synchronisation | Semaphore, mutex, message queue |
| `v1.3` | Per-task Stacks | Private stack allocation, overlap checks |
| `v1.4` | CPU Context Model | `CPUContext` (R0–xPSR), `context_init/save/restore` |
| `v1.5` | Software Context Switch | `context_switch()`, `context_active()`, scheduler integration |
| `v1.6` | Timer Module | `timer.c/h`, kernel tick, sleep management moved from scheduler |
| `v1.7` | Preemptive Priority Scheduler | Priority preemption, time-slice round-robin, `do_context_switch()` |
| `v1.8` | Memory Pool | Fixed-size allocator, embedded free list, double-free protection |
| `v1.9` | Software Timers & Event Flags | `SoftTimer`, `EventFlags`, `BLOCK_EVENT` |
| `v2.0` | Final Release | README, CHANGELOG, LICENSE, CONTRIBUTING, Makefile polish |

---

## Current RTOS Capabilities

### Task Management

- Up to `RTOS_MAX_TASKS` (default 8) concurrent tasks.
- Each task has a numeric priority (higher = more urgent) and a private stack.
- States: `TASK_READY`, `TASK_RUNNING`, `TASK_BLOCKED`, `TASK_SUSPENDED`.
- `rtos_suspend_task()` / `rtos_resume_task()` for explicit lifecycle control.

### Scheduler

- **Priority preemption:** the highest-priority READY task always runs.
- **Time-slice round-robin:** equal-priority tasks share the CPU in configurable tick slices (`RTOS_TIME_SLICE_TICKS`, default 1 for strict round-robin).
- **Forced switch:** a blocked or suspended task is never allowed to continue; the scheduler immediately selects the next best READY task.
- No unnecessary context switches: if the current task is still the best candidate and its slice has not expired, it runs without any save/restore.

### Synchronisation

| Primitive | API | Semantics |
|-----------|-----|-----------|
| Counting semaphore | `rtos_sem_wait()` / `rtos_sem_signal()` | Blocks on zero; signals wake one waiter |
| Mutex | `rtos_mutex_lock()` / `rtos_mutex_unlock()` | Owner-tracked; non-recursive |
| Message queue | `rtos_queue_send()` / `rtos_queue_receive()` | Fixed `int` payload; blocks when full/empty |
| Event flags | `event_flags_wait()` / `event_flags_set()` | 32-bit bitmask; auto-reset on satisfaction |

### Timer

- Monotonic kernel tick counter owned by `timer.c`.
- `rtos_task_sleep(n)` blocks a task for `n` ticks.
- Software timers: one-shot or periodic, callback-based, statically allocated pool.

### Memory

- Fixed-size block allocator: `memory_alloc()` / `memory_free()`.
- O(1) allocation and deallocation via embedded free list.
- Double-free detection with a separate allocation bitmap.
- No fragmentation; all memory visible at link time.

---

## Design Decisions

**No dynamic memory.** The kernel uses no `malloc`, `calloc`, `realloc`, or `free`. Every data structure is statically allocated. This matches MISRA-C guidelines for safety-critical embedded software.

**Software task dispatch.** Task execution and context switching are managed entirely in portable C99 software. There are no OS threads, `setjmp`/`longjmp`, `ucontext`, assembly, or hardware interrupt handlers. This makes the kernel fully portable and trivially debuggable.

**Single active context register.** `context.c` maintains one `active_context` struct. `context_switch()` copies the outgoing task's fields into this struct and copies the incoming task's fields out. The host PC/LR values are therefore accurate Cortex-M-style values (EXC_RETURN in LR, Thumb bit in PC).

**Embedded free list.** The memory pool stores the next-free-block index in the first `sizeof(int)` bytes of every free block, using `memcpy` to avoid strict-aliasing violations. No separate linked-list node is required.

**`scheduler_current_task_index()` accessor.** Rather than exposing the current task index through `rtos.h` (which would blur the boundary between the kernel core and the scheduler), `event.c` calls a minimal scheduler accessor. This keeps the dependency graph acyclic.

**Auto-reset event flags.** When `event_flags_set()` wakes a waiter, or when `event_flags_wait()` succeeds immediately, the satisfied bits are cleared. This prevents a second waiter from consuming the same event without the producer explicitly re-setting it, which is the correct semantics for edge-triggered hardware events.

---

## Configuration Reference

All configuration macros are defined in `include/rtos.h` and can be overridden at compile time with `-D<MACRO>=<value>`.

| Macro | Default | Description |
|-------|---------|-------------|
| `RTOS_MAX_TASKS` | `8` | Maximum concurrent tasks |
| `RTOS_QUEUE_SIZE` | `10` | Message queue capacity (number of `int` slots) |
| `RTOS_STACK_SIZE` | `256` | Per-task stack size in bytes |
| `RTOS_TIME_SLICE_TICKS` | `1` | Time-slice length for equal-priority round-robin |
| `RTOS_SOFT_TIMER_COUNT` | `8` | Maximum simultaneous software timers |
| `RTOS_EVENT_MAX_WAITERS` | `8` | Maximum waiters per `EventFlags` object |
| `RTOS_POOL_BLOCK_SIZE` | `32` | Memory pool block size in bytes |
| `RTOS_POOL_BLOCK_COUNT` | `16` | Number of blocks in the memory pool |

> **Note:** `RTOS_POOL_BLOCK_SIZE` and `RTOS_POOL_BLOCK_COUNT` are defined in `include/memory.h` and can be overridden independently. `RTOS_EVENT_MAX_WAITERS` is defined in `include/event.h`.

---

## Future Improvements

The following features are candidates for future milestones:

1. **Task statistics** — run count, total CPU ticks, worst-case latency.
2. **Task deletion** — reclaim a TCB slot at runtime.
3. **Deadlock detection** — cycle detection in the mutex ownership graph.
4. **Priority inheritance** — prevent priority inversion in mutex scenarios.
5. **Watchdog** — terminate tasks that exceed a tick deadline.
6. **Variable-size allocator** — buddy allocator or slab allocator on top of the pool.
7. **Event flag timeout** — `event_flags_wait_timeout(ef, mask, ticks)`.
8. **AND/OR wait modes** — wait for *any* flag in a mask rather than *all*.
9. **Hardware port** — replace `context_switch()` with Cortex-M PendSV handler.
10. **Doxygen integration** — generate HTML API docs from existing header comments.

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, coding style, commit message format, and pull request guidelines.

---

## License

This project is released under the [MIT License](LICENSE).

```text
Copyright (c) 2026 Sarath Patti

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
```
