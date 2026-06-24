# Mini Real-Time Operating System Simulator

This is a PC-based Mini RTOS simulator written in C. It is meant to be built first on a normal computer, then gradually ported to embedded hardware such as STM32 or ESP32.

## Features

- Task Control Blocks with task id, priority, state, and simulated context
- READY, RUNNING, BLOCKED, and SUSPENDED task states
- Static ready queue containing only READY tasks
- Priority-based cooperative scheduler
- Simulated context switch logs with `pc` and `sp`
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

The v1.1 tests verify READY task execution, BLOCKED task skipping, SUSPENDED task skipping, and multiple READY tasks in the ready queue.

## Example Output

```text
[00:01] Task Switched: Sensor Task pc=1 sp=0x1000
[00:01] Queue Send by Sensor Task value=100 count=1
[00:01] Sensor Task produced sample=100
[00:01] Semaphore Released count=1
[00:02] Task Switched: Logger Task pc=1 sp=0x1100
[00:02] Semaphore Acquired by Logger Task count=0
```

## Project Map

```text
include/rtos.h   Public kernel API and data structures
src/rtos.c       Scheduler, sync primitives, message queue, logging
src/main.c       Demo application using sensor/logger/display tasks
tests/           Focused simulator behavior tests
Makefile         Build commands
```

## Suggested Next Milestones

1. Add same-priority round-robin scheduling.
2. Add task deletion and task statistics.
3. Replace `printf` with `UART_SendString()` behind the same `uart_log()` API.
4. Port the scheduler tick to a hardware timer interrupt.
5. Map task stacks to real memory regions on STM32 or ESP32.
