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

Memory Pool (available at any time after rtos_init)
     |
     +---> memory_alloc()    — pop from free list, O(1)
     |
     +---> memory_free()     — push to free list, O(1), double-free safe
```

## Memory Pool Architecture (v1.8)

### Pool Layout

The pool is a 2-D static array of `uint8_t`:

```text
g_pool_storage[RTOS_POOL_BLOCK_COUNT][RTOS_POOL_BLOCK_SIZE]

  Block 0   Block 1   Block 2   ...   Block N-1
 ┌────────┬─────────┬─────────┬─────┬──────────┐
 │BLOCK   │ BLOCK   │ BLOCK   │     │  BLOCK   │
 │SIZE    │ SIZE    │ SIZE    │ ... │  SIZE    │
 │ bytes  │ bytes   │ bytes   │     │  bytes   │
 └────────┴─────────┴─────────┴─────┴──────────┘

Total pool memory = RTOS_POOL_BLOCK_SIZE × RTOS_POOL_BLOCK_COUNT bytes
```

### Embedded Free List

Free blocks are chained intrinsically — the first `sizeof(int)` bytes of
each free block store the index of the next free block.  No separate linked
list or metadata array is needed.

```text
After memory_init():

 g_free_list_head = 0

  Block 0       Block 1       Block 2       Block 3
 ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
 │ next = 1 │→ │ next = 2 │→ │ next = 3 │→ │ next = -1│ (END)
 └──────────┘  └──────────┘  └──────────┘  └──────────┘

After memory_alloc() (returns Block 0):

 g_free_list_head = 1

  Block 0       Block 1       Block 2       Block 3
 ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
 │[in use]  │  │ next = 2 │→ │ next = 3 │→ │ next = -1│
 └──────────┘  └──────────┘  └──────────┘  └──────────┘

After memory_free(Block 0) (prepend back to head):

 g_free_list_head = 0

  Block 0       Block 1       Block 2       Block 3
 ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐
 │ next = 1 │→ │ next = 2 │→ │ next = 3 │→ │ next = -1│
 └──────────┘  └──────────┘  └──────────┘  └──────────┘
```

### Allocation Strategy

| Operation | Time Complexity | Description |
|-----------|----------------|-------------|
| `memory_alloc()` | O(1) | Pop head of free list; set bitmap[idx] = 1 |
| `memory_free()` | O(1) | Validate via bitmap; push to head; set bitmap[idx] = 0 |
| `memory_available_blocks()` | O(1) | Return pre-maintained counter |

### Double-Free Protection

A parallel `uint8_t g_pool_allocated[RTOS_POOL_BLOCK_COUNT]` bitmap records
allocation state independently of the free-list links:

- `g_pool_allocated[i] == 1` → block i is live.
- `g_pool_allocated[i] == 0` → block i is free.

`memory_free()` checks the bitmap before modifying the list.  If the block is
already free, it logs an error and returns without corrupting the list.

### Advantages over Dynamic Heap Allocation

| Property | Heap (malloc/free) | Memory Pool |
|----------|--------------------|-------------|
| Allocation time | O(n) worst case (best-fit scan) | O(1) always |
| Deallocation time | O(n) worst case (coalesce) | O(1) always |
| Fragmentation | External + internal | None (fixed size) |
| Double-free safety | Undefined behaviour | Detected & logged |
| Static footprint | Hidden (OS-managed) | Fully visible at link time |
| ISR-safe | No (system call) | Yes (no blocking) |
| MISRA-C compliant | No (dynamic allocation forbidden) | Yes |

## Configuration

| Macro | Default | Description |
|-------|---------|-------------|
| `RTOS_MAX_TASKS` | 8 | Maximum number of tasks |
| `RTOS_QUEUE_SIZE` | 10 | Message queue capacity |
| `RTOS_STACK_SIZE` | 256 | Per-task stack in bytes |
| `RTOS_TIME_SLICE_TICKS` | 1 | Ticks before equal-priority preemption |
| `RTOS_POOL_BLOCK_SIZE` | 32 | Memory pool block size in bytes |
| `RTOS_POOL_BLOCK_COUNT` | 16 | Number of blocks in the memory pool |

## Module Interaction (v1.8)

```text
rtos_init()
  ├─► scheduler_init()  ──► timer_init()     [binds TCB array, resets tick]
  └─► memory_init()                          [builds free list, clears bitmap]

rtos_run() ──► scheduler_run() [per cycle]:
  1. timer_tick()                ← advance g_kernel_tick
  2. timer_update_sleep()        ← expire sleeping tasks
  3. peek_highest_priority()     ← identify best READY task (non-destructive)
  4. Preemption decision:
       Forced switch?     ──► current task is BLOCKED/SUSPENDED → do_context_switch()
       Priority preempt?  ──► re-queue outgoing ──► do_context_switch()
       Slice expired?     ──► re-queue outgoing ──► do_context_switch()
       Continue?          ──► run current task directly (no context switch)
  5. do_context_switch()         ← context_switch() or context_restore()
                                   + task_function() call

memory_alloc()   ← callable from any task at any time after rtos_init()
memory_free()    ← callable from any task; validates block and bitmap before returning

rtos_task_sleep(n)
  └─► sets TCB.sleep_ticks = n
  └─► scheduler_set_current_task_state(BLOCKED, BLOCK_SLEEP)

timer_update_sleep()
  └─► scheduler_set_task_state(TASK_READY) when sleep_ticks reaches 0

scheduler_unblock_one(reason)
  └─► called by rtos_sem_signal(), rtos_mutex_unlock(), rtos_queue_send/receive()
  └─► scheduler_set_task_state(TASK_READY) for the highest-priority waiter
```

## Preemptive Scheduling Algorithm (v1.7)

At the start of each scheduler cycle, after advancing the tick and waking
sleeping tasks, the scheduler makes one of five decisions:

| Case | Condition | Action | Log tag |
|------|-----------|--------|---------|
| **First tick** | No current task exists | Switch to best READY task | `[First tick]` |
| **Forced switch** | Current task is BLOCKED or SUSPENDED | Switch to best READY task without re-queuing | `[Forced switch]` |
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
  default 1 tick for strict round-robin).

## Timer Module (v1.6)

| Function | Description |
|----------|-------------|
| `timer_init(tasks, task_count)` | Reset `g_kernel_tick` to zero and bind the shared TCB array. |
| `timer_tick()` | Advance the kernel tick by one unit per cycle. |
| `timer_now()` | Return the current tick (used for logging and scheduling). |
| `timer_update_sleep()` | Decrement sleep counters; wake tasks whose counter reaches zero. |

## Memory Pool API (v1.8)

| Function | Description |
|----------|-------------|
| `memory_init()` | Initialise pool: build free list, clear bitmap. Called from `rtos_init()`. |
| `memory_alloc()` | Allocate one block (O(1)); returns `NULL` on exhaustion. |
| `memory_free(ptr)` | Return a block to the pool (O(1)); validates pointer and double-free. |
| `memory_available_blocks()` | Return count of free blocks remaining. |

## Project Map

```text
include/context.h    CPU context model API + context_switch() declaration
include/memory.h     Fixed-size memory pool API and design documentation
include/rtos.h       Public kernel API, data structures, configuration macros
include/scheduler.h  Internal scheduler module API
include/timer.h      Kernel tick and sleep management API
src/context.c        context_init, context_save, context_restore,
                     context_copy, context_active, context_switch
src/memory.c         Embedded free-list allocator, double-free protection,
                     O(1) memory_alloc / memory_free
src/rtos.c           Task lifecycle, sync primitives, message queue, logging
src/scheduler.c      Ready queue, preemptive priority selection, time-slice
                     round-robin, six-phase context-switch dispatch
src/timer.c          Kernel tick counter, sleep countdown, wake-up logic
src/main.c           Demo application using sensor/logger/display tasks
tests/               Focused simulator behaviour tests
Makefile             Build commands
```

## Example Log — Memory Pool

```text
[00:00] Memory Pool Init: 16 blocks x 32 bytes = 512 bytes total
[00:01] Memory Alloc: block 0 @ 0x... (15/16 blocks free)
[00:01] Memory Alloc: block 1 @ 0x... (14/16 blocks free)
[00:02] Memory Free: block 0 @ 0x... (15/16 blocks free)
[00:03] Memory Alloc FAILED: pool exhausted (0/16 blocks free)
[00:04] Memory Free ERROR: double-free detected on block 1 @ 0x...
```

## Suggested Next Milestones

1. Add task deletion and task statistics (run count, total ticks used).
2. Replace `printf` with `UART_SendString()` behind the same `uart_log()` API.
3. Port the scheduler tick to a hardware timer interrupt.
4. Map task stacks to real memory regions on STM32 or ESP32.
5. Add a watchdog tick that terminates tasks exceeding a deadline.
6. Add a variable-size memory pool using a buddy allocator or slab allocator.
