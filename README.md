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
- **v1.8 — Fixed-size memory pool allocator** with O(1) alloc/free and double-free protection
- **v1.9 — Software timers and event flags**
- Counting semaphore, mutex with owner tracking, fixed-size message queue
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

## Architecture

```text
Task Function
     │
     ▼
Task Control Block
     │
     ├──▶ Task metadata  (task id, priority, state, sleep_ticks, slice_ticks_used)
     ├──▶ Stack          (stack_memory, stack_size, stack_pointer)
     └──▶ CPUContext     (R0–R12, LR, PC, xPSR)

Timer Module (per tick)
     │
     ├──▶ [1] timer_tick()           — advance g_kernel_tick
     ├──▶ [2] timer_update_sleep()   — decrement sleep counters; wake expired tasks
     └──▶ [3] timer_update_soft()    — decrement & fire armed software timers

Scheduler (per tick, after timer)
     │
     ├──▶ Peek best READY task
     ├──▶ Preemption decision (priority / slice / forced / continue)
     └──▶ do_context_switch()  ──▶  task_function()

Memory Pool (available at any time after rtos_init)
     │
     ├──▶ memory_alloc()    — pop from free list, O(1)
     └──▶ memory_free()     — push to free list, O(1), double-free safe

Event Flags (available at any time after event_flags_init)
     │
     ├──▶ event_flags_set()   — OR mask into flags; wake matching waiters
     ├──▶ event_flags_clear() — AND-NOT mask from flags
     └──▶ event_flags_wait()  — immediate return (true) or block (false)
```

## Software Timers (v1.9)

Software timers are statically allocated kernel objects that fire a callback
function when their countdown expires.  They are driven by the existing kernel
tick in `timer_update_soft()`, which is called automatically at the end of
`timer_update_sleep()` — no scheduler changes are required.

### Timer Modes

| Mode | Behaviour |
|------|-----------|
| `TIMER_ONE_SHOT` | Fires once, then automatically disarms |
| `TIMER_PERIODIC` | Fires every `period_ticks` ticks, auto-reloads |

### Software Timer Lifecycle

```text
timer_soft_create()   → allocate slot, configure (NOT yet running)
       │
timer_soft_start()    → set remaining = period_ticks, active = true
       │
  [tick N arrives]
       │
timer_update_soft()   → remaining--
       │
  remaining == 0?
       ├─ YES → callback(arg) fires
       │          ├─ ONE_SHOT: active = false  (auto-disarm)
       │          └─ PERIODIC: remaining = period_ticks  (auto-reload)
       └─ NO  → wait
       │
timer_soft_stop()     → active = false (slot still allocated)
timer_soft_delete()   → active = false, in_use = false (slot returned to pool)
```

### Per-expiry log sequence

```text
[HH:MM] Timer Expired  : <name> (mode=PERIODIC|ONE_SHOT)
[HH:MM] Timer Callback : <name> executing
[HH:MM] Timer Callback : <name> done
[HH:MM] Timer Reload   : <name> (next in N ticks)   ← PERIODIC only
[HH:MM] Timer Stopped  : <name> (one-shot complete) ← ONE_SHOT only
```

### Example usage

```c
static void heartbeat_cb(void *arg) {
    uart_log("Heartbeat ping");
}

SoftTimer *hb = timer_soft_create("heartbeat", TIMER_PERIODIC, 5, heartbeat_cb, NULL);
timer_soft_start(hb);
/* fires every 5 ticks until timer_soft_stop(hb) or timer_soft_delete(hb) */
```

## Event Flags (v1.9)

Event flags provide bitmask-based task synchronisation.  A 32-bit `flags`
field allows up to 32 independent boolean signals per `EventFlags` object.

### Blocking model

`event_flags_wait()` tests the requested bits immediately:

- **Bits already set** → clear them (auto-reset) and return `true`.  Task continues.
- **Bits not yet set** → record the task in the waiter table, block it
  (`BLOCK_EVENT`), and return `false`.  The caller must return from the task
  function immediately; it is rescheduled when `event_flags_set()` satisfies
  its mask.

`event_flags_set()` scans the waiter table after updating the flags and wakes
every task whose `required_mask` is now a subset of the current flags.

### Auto-reset semantics

When a wait is satisfied (either immediately or via `event_flags_set()`), the
bits that were waited on are cleared from the `EventFlags.flags` field.  This
prevents a second waiter from seeing the same event without the producer
explicitly re-setting it.

### Event flags state diagram

```text
Producer task            EventFlags object           Consumer task
──────────               ─────────────────           ─────────────
event_flags_set(mask) ──▶  flags |= mask
                            scan waiters
                            waiter.mask ⊆ flags?
                              YES ──▶ flags &= ~mask ──▶ scheduler_set_task_state(READY)
                              NO  ──▶ skip

                                                      event_flags_wait(mask)
                                                        flags & mask == mask?
                                                          YES ──▶ flags &= ~mask; return true
                                                          NO  ──▶ record waiter; BLOCK_EVENT; return false
```

### Example usage

```c
static EventFlags data_ready_event;

/* Producer (e.g. sensor task or timer callback) */
event_flags_set(&data_ready_event, 0x01u);

/* Consumer */
if (!event_flags_wait(&data_ready_event, 0x01u)) {
    return;   /* blocked; will retry on next scheduling */
}
/* process data */
```

## Module Interaction (v1.9)

```text
rtos_init()
  ├─▶ scheduler_init()  ──▶ timer_init()     [binds TCB array, resets tick + soft-timer pool]
  └─▶ memory_init()                          [builds free list, clears bitmap]

Application startup:
  event_flags_init(&ef)                      [zero flags, empty waiter table]
  t = timer_soft_create(…)                   [claim timer slot]
  timer_soft_start(t)                        [arm timer]

rtos_run() ──▶ scheduler_run() [per cycle]:
  1. timer_tick()                ← advance g_kernel_tick
  2. timer_update_sleep()
       ├─ expire sleeping tasks (BLOCK_SLEEP → TASK_READY)
       └─ timer_update_soft()   ← decrement & fire soft timers (calls callbacks)
                                   callback may call event_flags_set() to wake waiters
  3. peek_highest_priority()     ← identify best READY task
  4. Preemption decision:
       Forced switch?    → do_context_switch()  (current task BLOCKED/SUSPENDED)
       Priority preempt? → re-queue current → do_context_switch()
       Slice expired?    → re-queue current → do_context_switch()
       Continue?         → run current task in-place
  5. task_function()             ← cooperative task body runs
       may call:
         event_flags_wait()      → BLOCK_EVENT if flags not satisfied
         event_flags_set()       → wake matching waiters
         memory_alloc/free()     → pool allocation
         rtos_task_sleep()       → BLOCK_SLEEP

event_flags_set(ef, mask)
  ├─ ef->flags |= mask
  └─ for each waiter: (ef->flags & waiter.mask) == waiter.mask?
       YES → ef->flags &= ~waiter.mask; scheduler_set_task_state(READY)

event_flags_wait(ef, mask)
  ├─ (ef->flags & mask) == mask? → ef->flags &= ~mask; return true
  └─ else: record (task_index, mask); scheduler_set_current_task_state(BLOCKED, BLOCK_EVENT); return false
```

## Memory Pool Architecture (v1.8)

The pool is a 2-D static array `g_pool_storage[RTOS_POOL_BLOCK_COUNT][RTOS_POOL_BLOCK_SIZE]`.
Free blocks are chained via an embedded free list (no separate metadata array).
A parallel `uint8_t g_pool_allocated[]` bitmap provides O(1) double-free detection.

| Operation | Time Complexity |
|-----------|----------------|
| `memory_alloc()` | O(1) — pop head of free list |
| `memory_free()` | O(1) — validate + push to head |
| `memory_available_blocks()` | O(1) — pre-maintained counter |

## Preemptive Scheduling Algorithm (v1.7)

| Case | Condition | Log tag |
|------|-----------|---------|
| First tick | No current task | `[First tick]` |
| Forced switch | Current task is BLOCKED/SUSPENDED | `[Forced switch]` |
| Priority preemption | `best.priority > current.priority` | `[Preempted]` |
| Time-slice expiry | Equal priority, `slice_ticks_used >= RTOS_TIME_SLICE_TICKS` | `[Slice expired]` |
| Continue | All other cases | `Continue` |

## Configuration

| Macro | Default | Description |
|-------|---------|-------------|
| `RTOS_MAX_TASKS` | 8 | Maximum number of tasks |
| `RTOS_QUEUE_SIZE` | 10 | Message queue capacity |
| `RTOS_STACK_SIZE` | 256 | Per-task stack in bytes |
| `RTOS_TIME_SLICE_TICKS` | 1 | Ticks before equal-priority preemption |
| `RTOS_POOL_BLOCK_SIZE` | 32 | Memory pool block size in bytes |
| `RTOS_POOL_BLOCK_COUNT` | 16 | Number of blocks in the memory pool |
| `RTOS_SOFT_TIMER_COUNT` | 8 | Maximum simultaneous software timers |
| `RTOS_EVENT_MAX_WAITERS` | 8 | Maximum waiters per EventFlags object |

## API Reference

### Kernel Tick

| Function | Description |
|----------|-------------|
| `timer_init(tasks, n)` | Reset tick, clear soft-timer pool, bind task list |
| `timer_tick()` | Advance kernel tick by one (scheduler use only) |
| `timer_now()` | Return current kernel tick |
| `timer_update_sleep()` | Expire sleepers + fire soft timers (scheduler use only) |

### Software Timers

| Function | Description |
|----------|-------------|
| `timer_soft_create(name, mode, period, cb, arg)` | Allocate timer slot |
| `timer_soft_start(timer)` | Arm / restart timer |
| `timer_soft_stop(timer)` | Disarm timer (slot kept) |
| `timer_soft_delete(timer)` | Disarm and release slot |

### Event Flags

| Function | Description |
|----------|-------------|
| `event_flags_init(ef)` | Initialise to zero, empty waiter table |
| `event_flags_set(ef, mask)` | OR mask into flags; wake matching waiters |
| `event_flags_clear(ef, mask)` | AND-NOT mask from flags |
| `event_flags_wait(ef, mask)` | Return true immediately or block task |
| `event_flags_get(ef)` | Read flags without blocking |

### Memory Pool

| Function | Description |
|----------|-------------|
| `memory_init()` | Build free list, clear bitmap |
| `memory_alloc()` | Allocate one block (O(1)); NULL on exhaustion |
| `memory_free(ptr)` | Return block; validates pointer and double-free |
| `memory_available_blocks()` | Return free block count |

## Project Map

```text
include/context.h    CPU context model API + context_switch() declaration
include/event.h      Event flags API (EventFlags, EventWaiter types + 5 functions)
include/memory.h     Fixed-size memory pool API and design documentation
include/rtos.h       Public kernel API, data structures, configuration macros
include/scheduler.h  Internal scheduler module API (including scheduler_current_task_index)
include/timer.h      Kernel tick, sleep management, software timer API
src/context.c        context_init, context_save, context_restore, context_switch
src/event.c          EventFlags implementation: set/clear/wait/get, waiter table, BLOCK_EVENT
src/memory.c         Embedded free-list allocator, double-free protection
src/rtos.c           Task lifecycle, sync primitives, message queue, logging
src/scheduler.c      Ready queue, preemptive priority, time-slice, scheduler_current_task_index
src/timer.c          Kernel tick, sleep countdown, software timer pool, timer_update_soft
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
6. Extend event flags with AND/OR wait modes and timeout support.
