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
- **v1.5 — Software context switching** via `context_switch()`
- **v1.6 — Dedicated timer module** for kernel tick and sleep management
- **v1.7 — Preemptive priority scheduler** with time-slice round-robin
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
     |     task id, priority, state, sleep_ticks, slice_ticks_used
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
     +---> [3] Peek best READY task  — scan ready queue for highest priority
     |
     +---> [4] Decision (see below)
     |
     +---> [5] Context Switch        — context_switch() atomically saves
     |                                 outgoing and loads incoming in one call
     |
     +---> [6] Task Running          — task function is called cooperatively
```

## Preemptive Scheduling Algorithm (v1.7)

At the start of each scheduler cycle, after advancing the tick and waking
sleeping tasks, the scheduler makes one of four decisions:

| Case | Condition | Action | Log tag |
|------|-----------|--------|---------|
| **First tick** | No current task exists | Switch to best READY task | `[First tick]` |
| **Priority preemption** | `best.priority > current.priority` | Immediately switch to the higher-priority task | `[Preempted]` |
| **Time-slice expiry** | `best.priority == current.priority` AND `slice_ticks_used >= RTOS_TIME_SLICE_TICKS` | Rotate to the next equal-priority task | `[Slice expired]` |
| **Continue** | All other cases | Current task runs for another tick | `Continue` |

### Key properties

- **No unnecessary context switches.** If the current task is still the best
  candidate and its slice has not expired, it continues without any context
  save/restore overhead.
- **Priority always wins.** A higher-priority task that becomes READY (e.g.
  by being woken from sleep) immediately preempts the current task on the
  very next tick.
- **Equal-priority fairness.** When multiple tasks share the same priority,
  they are rotated on a configurable time slice (`RTOS_TIME_SLICE_TICKS`,
  default 3 ticks).  Increase this value for coarser slicing.
- **Blocking is cooperative.** A task that calls `rtos_task_sleep()`,
  `rtos_sem_wait()`, `rtos_mutex_lock()`, or `rtos_queue_receive/send()` and
  cannot proceed immediately blocks itself and cedes the CPU in the same tick.

### Time-slice counter (`slice_ticks_used`)

Stored in `TCB.slice_ticks_used`.  Lifecycle:

- **Incremented** each tick the current task continues without a context switch.
- **Reset to 0** whenever the task is preempted, blocks, completes, or
  re-enters the ready queue via `scheduler_set_task_state()`.

### Scheduler state diagram

```text
                         rtos_create_task()
                              |
                              v
                        +-----------+
                        | SUSPENDED |<-------- rtos_suspend_task()
                        +-----------+
                              |
              scheduler_set_task_state(READY)
              rtos_resume_task()
                              |
                              v
                  +----------------------+
     +----------->|        READY         |<--------+
     |            | (in ready queue)     |         |
     |            +----------------------+         |
     |                      |                      |
     |         scheduler picks this task            |
     |         (priority preemption or first tick)  |
     |                      |                      |
     |                      v                      |
     |            +----------------------+         |
     |            |      RUNNING         |         |
     |            | slice_ticks_used++   |         |
     |            +----------------------+         |
     |            /           |          \         |
     |           /            |           \        |
     |  task blocks   task completes   time-slice  |
     |           |            |         expired    |
     |           v            v              v     |
     |      +----------+  (re-queued       (re-queued
     |      | BLOCKED  |   as READY)        as READY)--+
     |      +----------+
     |           |
     |  sleep/sem/mutex
     |  condition met
     |           |
     +-----------+   (READY again via timer_update_sleep
                       or scheduler_unblock_one)
```

## Module Interaction (v1.7)

```text
rtos_init()
  └─► scheduler_init()  ──► timer_init()      [binds TCB array, resets tick]

rtos_run() ──► scheduler_run() [per cycle]:
  1. timer_tick()                ← advance g_kernel_tick
  2. timer_update_sleep()        ← expire sleeping tasks
  3. peek_highest_priority()     ← identify best READY task (no dequeue yet)
  4. Preemption decision:
       Priority preempt?  ──► re-queue outgoing ──► do_context_switch()
       Slice expired?     ──► re-queue outgoing ──► do_context_switch()
       Continue?          ──► run current task directly (no context switch)
  5. do_context_switch()         ← context_switch() or context_restore()
                                   + task_function() call

rtos_task_sleep(n)
  └─► sets TCB.sleep_ticks = n
  └─► scheduler_set_current_task_state(BLOCKED, BLOCK_SLEEP)

timer_update_sleep()
  └─► scheduler_set_task_state(TASK_READY) when sleep_ticks reaches 0

scheduler_unblock_one(reason)
  └─► called by rtos_sem_signal(), rtos_mutex_unlock(), rtos_queue_send/receive()
  └─► scheduler_set_task_state(TASK_READY) for the highest-priority waiter
```

## Timer Module (v1.6 / v1.7)

| Function | Description |
|----------|-------------|
| `timer_init(tasks, task_count)` | Reset `g_kernel_tick` to zero and bind the shared TCB array. |
| `timer_tick()` | Advance the kernel tick by one unit per cycle. |
| `timer_now()` | Return the current tick (used for logging and scheduling). |
| `timer_update_sleep()` | Decrement sleep counters; wake tasks whose counter reaches zero. |

## Configuration

| Macro | Default | Description |
|-------|---------|-------------|
| `RTOS_MAX_TASKS` | 8 | Maximum number of tasks |
| `RTOS_QUEUE_SIZE` | 10 | Message queue capacity |
| `RTOS_STACK_SIZE` | 256 | Per-task stack in bytes |
| `RTOS_TIME_SLICE_TICKS` | 3 | Ticks before equal-priority preemption |

## Software Context-Switching (v1.5)

```c
void context_switch(CPUContext *outgoing, const CPUContext *incoming);
```

`context_switch()` lives in `context.c` and is called exclusively from
`scheduler.c`.  It saves the outgoing task's `active_context` then loads the
incoming task's context atomically.

### Per-tick log sequence

```text
[HH:MM] [Tick N] Sleep expired  : <task> waking up        (if applicable)
[HH:MM] [Tick N] [Preempted]    : <cur> (pri=P) by <new> (pri=Q)
[HH:MM] [Tick N] Current Task  : <outgoing> (state=RUNNING)
[HH:MM] [Tick N] Context Saved  : <outgoing>
[HH:MM] [Tick N] Context Switch [Preempted]: <outgoing> -> <incoming>
[HH:MM] [Tick N] Next Task      : <incoming> (priority=P)
[HH:MM] [Tick N] Context Restored: <incoming> (pc=0x... lr=0x...)
[HH:MM] [Tick N] Task Running   : <incoming> sp=0x... ready=R
```

Or for a continuation (no switch):

```text
[HH:MM] [Tick N] Continue      : <task> (slice=S/MAX, priority=P)
[HH:MM] [Tick N] Task Running  : <task> sp=0x... ready=R
```

## Memory Layout

```text
Task A TCB
+-----------------------------+
| task id / priority / state  |
| sleep_ticks                 |
| slice_ticks_used            |
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
[00:01] [Tick 1] Context Switch [First tick]: (none) -> Sensor Task
[00:01] [Tick 1] Next Task      : Sensor Task (priority=3)
[00:01] [Tick 1] Context Restored: Sensor Task (pc=0x... lr=0xFFFFFFFD)
[00:01] [Tick 1] Task Running   : Sensor Task sp=0x... ready=2
[00:01] Queue Send by Sensor Task value=100 count=1
[00:01] Sensor Task produced sample=100
[00:01] Semaphore Released count=1
[00:01] Logger Task unblocked
[00:01] [Tick 1] Sensor Task state=BLOCKED
[00:02] [Tick 2] [Preempted]    : Logger Task (pri=2) by Sensor Task (pri=3)
...
[00:04] [Tick 4] Sleep expired  : Sensor Task waking up
[00:04] [Tick 4] Continue      : Display Task (slice=1/3, priority=1)
```

## Project Map

```text
include/context.h    CPU context model API + context_switch() declaration
include/rtos.h       Public kernel API, data structures, RTOS_TIME_SLICE_TICKS
include/scheduler.h  Internal scheduler module API
include/timer.h      Kernel tick and sleep management API
src/context.c        context_init, context_save, context_restore,
                     context_copy, context_active, context_switch
src/rtos.c           Task lifecycle, sync primitives, message queue, logging
src/scheduler.c      Ready queue, preemptive priority selection, time-slice
                     round-robin, six-phase context-switch dispatch
src/timer.c          Kernel tick counter, sleep countdown, wake-up logic
src/main.c           Demo application using sensor/logger/display tasks
tests/               Focused simulator behaviour tests
Makefile             Build commands
```

## Suggested Next Milestones

1. Add task deletion and task statistics (run count, total ticks used).
2. Replace `printf` with `UART_SendString()` behind the same `uart_log()` API.
3. Port the scheduler tick to a hardware timer interrupt.
4. Map task stacks to real memory regions on STM32 or ESP32.
5. Add a watchdog tick that terminates tasks exceeding a deadline.
