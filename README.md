# Mini Real-Time Operating System Simulator

This is a PC-based Mini RTOS simulator written in C. It is meant to be built first on a normal computer, then gradually ported to embedded hardware such as STM32 or ESP32.

## Features

- Task Control Blocks with task id, priority, state, and private stack metadata
- READY, RUNNING, BLOCKED, and SUSPENDED task states
- Fixed-size per-task stacks configured by `RTOS_STACK_SIZE`
- Cortex-M-style CPU context model with R0-R12, LR, PC, and xPSR
- Static ready queue containing only READY tasks
- Scheduler module separated from kernel services
- Priority-based cooperative scheduler
- Scheduler logs with each task's current stack pointer
- Counting semaphore
- Mutex with owner tracking
- Fixed-size integer message queue
- UART-style debug logs using `printf`

## Build and Run

```sh
make
make run
```

## Test

```sh
make test
```

The tests verify READY task execution, BLOCKED task skipping, SUSPENDED task skipping, multiple READY tasks in the ready queue, private task stack layout, and simulated CPU context save/restore behavior.

## Architecture

```text
Task Function
     |
     v
Task Control Block
     |
     +--> Task metadata
     |    task id, priority, state
     |
     +--> Stack
     |    stack memory, stack size, stack pointer
     |
     +--> CPUContext
          R0-R12, LR, PC, xPSR

Scheduler
     |
     +--> context_save(current task)
     |
     +--> select next READY task
     |
     +--> context_restore(next task)
     |
     +--> run task function cooperatively
```

## Memory Layout

Each task owns a fixed stack inside its TCB. The stack pointer is initialized to the high end of that stack region to model the downward-growing stack used by Cortex-M cores.

```text
Task A TCB
+-----------------------------+
| task id / priority / state  |
| stack_size = RTOS_STACK_SIZE|
| stack_pointer ------------+ |
| stack_memory[0]           | |
| ...                       | |
| stack_memory[N - 1]       |<+
+-----------------------------+

Task B TCB
+-----------------------------+
| task id / priority / state  |
| stack_size = RTOS_STACK_SIZE|
| stack_pointer ------------+ |
| stack_memory[0]           | |
| ...                       | |
| stack_memory[N - 1]       |<+
+-----------------------------+

Low address                         High address
stack_memory[0]  ...  stack_memory[N - 1]  initial SP
      ^                                            ^
      |                                            |
 stack base                              stack base + size
```

## Example Output

```text
[00:01] Task Switched: Sensor Task sp=0x1000 ready=2
[00:01] Queue Send by Sensor Task value=100 count=1
[00:01] Sensor Task produced sample=100
[00:01] Semaphore Released count=1
[00:02] Task Switched: Logger Task sp=0x1100 ready=1
[00:02] Semaphore Acquired by Logger Task count=0
```

## Project Map

```text
include/context.h    Internal CPU context model API
include/rtos.h       Public kernel API and data structures
include/scheduler.h  Internal scheduler module API
src/context.c        Simulated context save/restore/copy
src/rtos.c           Task lifecycle, sync primitives, message queue, logging
src/scheduler.c      Ready queue, priority selection, and task dispatch
src/main.c           Demo application using sensor/logger/display tasks
tests/               Focused simulator behavior tests
Makefile             Build commands
```

## Suggested Next Milestones

1. Add same-priority round-robin scheduling.
2. Add task deletion and task statistics.
3. Replace `printf` with `UART_SendString()` behind the same `uart_log()` API.
4. Port the scheduler tick to a hardware timer interrupt.
5. Map task stacks to real memory regions on STM32 or ESP32.
