# Mini Real-Time Operating System Simulator

This is a PC-based Mini RTOS simulator written in C. It is meant to be built
first on a normal computer, then gradually ported to embedded hardware such as
STM32 or ESP32.

## Features

- Task Control Blocks with task id, priority, state, and private stack metadata
- READY, RUNNING, BLOCKED, and SUSPENDED task states
- Fixed-size per-task stacks configured by `RTOS_STACK_SIZE`
- Cortex-M-style CPU context model with R0–R12, LR, PC, and xPSR
- Static ready queue containing only READY tasks
- Scheduler module separated from kernel services
- Priority-based cooperative scheduler
- **v1.5 — Software context switching** via `context_switch()`
- Six-phase context-switch log sequence per scheduling tick
- **v1.6 — Dedicated timer module** for kernel tick and sleep management
- Counting semaphore
- Mutex with owner tracking
- Fixed-size integer message queue
- UART-style debug logs annotated with the current kernel tick

## Build and Run

```sh
make
make run
```

## Test

```sh
make test
```

The tests verify READY task execution, BLOCKED task skipping, SUSPENDED task
skipping, multiple READY tasks in the ready queue, private task stack layout,
and simulated CPU context save/restore behaviour.

## Architecture

```text
Task Function
     |
     v
Task Control Block
     |
     +---> Task metadata
     |     task id, priority, state, sleep_ticks
     |
     +---> Stack
     |     stack memory, stack size, stack pointer
     |
     +---> CPUContext
           R0–R12, LR, PC, xPSR

Timer Module (per tick)
     |
     +---> [1] timer_tick()          — advance g_kernel_tick
     |
     +---> [2] timer_update_sleep()  — decrement each sleeping task's
                                       sleep_ticks; wake tasks that have
                                       reached zero

Scheduler (per tick, after timer)
     |
     +---> [3] Current Task          — identify the outgoing task
     |
     +---> [4] Context Saved         — snapshot outgoing task's CPUContext
     |                                 via context_switch() Phase 1
     |
     +---> [5] Context Switch        — context_switch() atomically saves
     |                                 outgoing and loads incoming in one call
     |
     +---> [6] Next Task             — highest-priority READY task selected
     |
     +---> [7] Context Restored      — incoming task's CPUContext is now live
     |                                 (confirmed by context_active())
     |
     +---> [8] Task Running          — task function is called cooperatively
```

## Timer Module (v1.6)

### Kernel Tick

The kernel tick is a monotonically increasing `uint32_t` counter maintained
exclusively in `src/timer.c` as the module-private variable `g_kernel_tick`.

| Function | Description |
|----------|-------------|
| `timer_init(tasks, task_count)` | Reset `g_kernel_tick` to zero and bind the shared TCB array. Called from `scheduler_init()`. |
| `timer_tick()` | Advance the kernel tick by exactly one unit. Called once per scheduler cycle, before any scheduling decision. |
| `timer_now()` | Return the current tick value without advancing it. Safe to call from anywhere in the kernel. |

`timer_now()` is used by `uart_log()` (in `rtos.c`) and by `scheduler_run()`
(in `scheduler.c`) to embed the current tick in every log line.

### Sleep Management

Sleep management was moved from `scheduler.c` to `timer.c` in v1.6.

| Function | Description |
|----------|-------------|
| `timer_update_sleep()` | Iterate all tasks; for each `TASK_BLOCKED / BLOCK_SLEEP` task, decrement `TCB.sleep_ticks`. When the counter reaches zero, call `scheduler_set_task_state(TASK_READY, BLOCK_NONE)` to wake the task. |

`timer_update_sleep()` is declared in `timer.h` as an internal scheduler API
and must only be called from `scheduler.c`.

### Per-cycle call order

```text
timer_tick();           // 1. advance the kernel clock
timer_update_sleep();   // 2. wake tasks whose sleep has expired
pick_next_task();       // 3. scheduling decision
context_switch();       // 4. software context switch
task->task_function();  // 5. run the selected task
```

## Module Interaction

```text
rtos.c
  |-- calls --> scheduler_init()  which calls --> timer_init()
  |-- calls --> scheduler_run()   which calls --> timer_tick()
  |                                           --> timer_update_sleep()
  |                                           --> scheduler_set_task_state()
  |-- calls --> uart_log()        which calls --> timer_now()
  |
  |   (sleep path)
  +-- rtos_task_sleep(n) sets TCB.sleep_ticks = n
        then calls scheduler_set_current_task_state(BLOCKED, BLOCK_SLEEP)

timer.c
  |-- timer_update_sleep() calls --> scheduler_set_task_state()
  |                                  to keep the ready queue consistent

context.c
  |-- context_switch()  called exclusively from scheduler.c
  |-- context_restore() called on first tick (no outgoing task)
  +-- context_active()  called to log PC/LR after restore
```

## Software Context-Switching Workflow (v1.5)

The context switch is implemented entirely in software using the `CPUContext`
struct embedded in every TCB.  No assembly, `setjmp`/`longjmp`, `ucontext`, or
OS threads are used.

### `context_switch()` — internal scheduler primitive

```c
void context_switch(CPUContext *outgoing, const CPUContext *incoming);
```

`context_switch()` lives in `context.c` and is declared in `context.h`.  It is
called **exclusively** from `scheduler.c`.  User code and kernel services must
not call it directly.

| Phase | Action |
|-------|--------|
| **Save** | Copies `active_context` → `*outgoing` (outgoing task's TCB field) |
| **Restore** | Copies `*incoming` → `active_context` (incoming task's TCB field) |

Both phases happen inside one function call so there is no intermediate state
where neither task owns the active context.

### Per-tick log sequence

Every scheduling tick produces log lines in the following order:

```text
[HH:MM] [Tick N] Sleep expired  : <task> waking up           (if applicable)
[HH:MM] [Tick N] Current Task  : <outgoing>  (state=RUNNING)
[HH:MM] [Tick N] Context Saved  : <outgoing>
[HH:MM] [Tick N] Context Switch : <outgoing> -> <incoming>
[HH:MM] [Tick N] Next Task      : <incoming> (priority=P)
[HH:MM] [Tick N] Context Restored: <incoming> (pc=0x... lr=0x...)
[HH:MM] [Tick N] Task Running   : <incoming> sp=0x... ready=R
```

On the very first tick there is no outgoing task, so "Current Task",
"Context Saved", and the left side of "Context Switch" are omitted.

### Per-task context and stack isolation

- Each `TCB` owns a `CPUContext context` field — no global context is shared
  between tasks.
- Each `TCB` owns a `stack_memory[]` array — no task shares stack memory with
  any other task.
- `context_init()` seeds a task's context with its entry-point `pc`, Cortex-M
  `xPSR` thumb-mode flag, and sentinel `lr = 0xFFFFFFFD`.
- `context_switch()` preserves each task's context independently: saving one
  task never touches another task's `CPUContext`.

## Memory Layout

Each task owns a fixed stack inside its TCB.  The stack pointer is initialized
to the high end of that stack region to model the downward-growing stack used
by Cortex-M cores.

```text
Task A TCB
+-----------------------------+
| task id / priority / state  |
| sleep_ticks                 |
| stack_size = RTOS_STACK_SIZE|
| stack_pointer ------------+ |
| CPUContext (r0..xpsr)     | |
| stack_memory[0]           | |
| ...                       | |
| stack_memory[N - 1]       |<+
+-----------------------------+
```

## Example Output

```text
Mini RTOS PC Simulator
----------------------
[00:00] Task Created: Sensor Task priority=3
[00:00] Task Created: Logger Task priority=2
[00:00] Task Created: Display Task priority=1
[00:01] [Tick 1] Context Switch : (none) -> Sensor Task
[00:01] [Tick 1] Next Task      : Sensor Task (priority=3)
[00:01] [Tick 1] Context Restored: Sensor Task (pc=0x... lr=0xFFFFFFFD)
[00:01] [Tick 1] Task Running   : Sensor Task sp=0x... ready=2
[00:01] Queue Send by Sensor Task value=100 count=1
[00:01] Sensor Task produced sample=100
[00:01] Semaphore Released count=1
[00:01] Logger Task unblocked
[00:01] [Tick 1] Sensor Task state=BLOCKED
[00:02] [Tick 2] Current Task  : Sensor Task (state=BLOCKED)
[00:02] [Tick 2] Context Saved  : Sensor Task
[00:02] [Tick 2] Context Switch : Sensor Task -> Logger Task
[00:02] [Tick 2] Next Task      : Logger Task (priority=2)
[00:02] [Tick 2] Context Restored: Logger Task (pc=0x... lr=0xFFFFFFFD)
[00:02] [Tick 2] Task Running   : Logger Task sp=0x... ready=1
[00:04] [Tick 4] Sleep expired  : Sensor Task waking up
```

## Project Map

```text
include/context.h    CPU context model API + context_switch() declaration
include/rtos.h       Public kernel API and data structures
include/scheduler.h  Internal scheduler module API
include/timer.h      Kernel tick and sleep management API (v1.6)
src/context.c        context_init, context_save, context_restore,
                     context_copy, context_active, context_switch
src/rtos.c           Task lifecycle, sync primitives, message queue, logging
src/scheduler.c      Ready queue, priority selection, six-phase task dispatch
src/timer.c          Kernel tick counter, sleep countdown, wake-up logic (v1.6)
src/main.c           Demo application using sensor/logger/display tasks
tests/               Focused simulator behaviour tests
Makefile             Build commands
```

## Suggested Next Milestones

1. Add same-priority round-robin scheduling.
2. Add task deletion and task statistics.
3. Replace `printf` with `UART_SendString()` behind the same `uart_log()` API.
4. Port the scheduler tick to a hardware timer interrupt.
5. Map task stacks to real memory regions on STM32 or ESP32.
