/*
 * timer.c — Kernel Tick & Timer Management (v1.6)
 *
 * This translation unit is the single owner of:
 *
 *   - g_kernel_tick   : monotonic kernel tick counter
 *   - sleep countdown : per-tick decrement of TCB.sleep_ticks for every
 *                       sleeping task
 *   - wake-up         : transition sleeping tasks to TASK_READY when their
 *                       sleep duration expires
 *
 * No other module tracks wall-clock or tick time.  scheduler.c delegates
 * all tick management here and calls timer_now() wherever it previously
 * read its internal system_tick variable.
 *
 * Design constraints
 * ------------------
 *   - Host-based, portable C99 only.
 *   - No hardware timers, interrupts, threads, or assembly.
 *   - No global variables exposed outside this file.
 *   - Modifies TCB state exclusively through scheduler_set_task_state()
 *     to keep ready-queue bookkeeping consistent with scheduler.c.
 */

#include "timer.h"
#include "scheduler.h"

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Module-private state                                                 */
/* ------------------------------------------------------------------ */

/*
 * g_kernel_tick — monotonic kernel tick counter.
 *
 * Starts at 0.  Incremented by timer_tick() exactly once per scheduler
 * cycle.  uint32_t provides ~49 days of 1 ms ticks before rollover,
 * which is sufficient for a host simulator.
 */
static uint32_t g_kernel_tick;

/*
 * g_tasks / g_task_count — references to the shared TCB array.
 *
 * Set by timer_init() and never changed thereafter.  Both pointers are
 * owned by rtos.c; this module holds read/write access to the task state
 * fields only.
 */
static TCB *g_tasks;
static int *g_task_count;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/*
 * timer_init() — reset the kernel tick and bind the task list.
 *
 * Safe to call multiple times (e.g. between unit tests) because it
 * unconditionally resets all module state.
 */
void timer_init(TCB *tasks, int *task_count)
{
    g_kernel_tick = 0;
    g_tasks       = tasks;
    g_task_count  = task_count;
}

/*
 * timer_tick() — advance the kernel tick by exactly one unit.
 *
 * Called once at the start of every scheduler cycle, before any
 * scheduling decision or sleep-expiry check is performed.
 */
void timer_tick(void)
{
    g_kernel_tick++;
}

/*
 * timer_now() — return the current kernel tick value.
 *
 * Reads the tick without advancing it.  Safe to call from anywhere
 * because the simulator is single-threaded and cooperative.
 */
uint32_t timer_now(void)
{
    return g_kernel_tick;
}

/* ------------------------------------------------------------------ */
/* Internal scheduler API                                               */
/* ------------------------------------------------------------------ */

/*
 * timer_update_sleep() — decrement sleep counters and wake expired tasks.
 *
 * Iterates over every task.  For each task that is BLOCKED with reason
 * BLOCK_SLEEP:
 *
 *   1. Decrements TCB.sleep_ticks.
 *   2. When the counter reaches zero (or goes negative due to a missed
 *      tick), calls scheduler_set_task_state(TASK_READY, BLOCK_NONE) to
 *      transition the task back to the ready queue.
 *
 * Preconditions
 *   - timer_init() has been called with valid pointers.
 *   - timer_tick() has already been called for this cycle.
 *
 * Called exclusively from scheduler.c — not part of the public API.
 */
void timer_update_sleep(void)
{
    int i;

    if (g_tasks == NULL || g_task_count == NULL) {
        return;
    }

    for (i = 0; i < *g_task_count; i++) {
        TCB *task = &g_tasks[i];

        if (task->state != TASK_BLOCKED || task->block_reason != BLOCK_SLEEP) {
            continue;
        }

        task->sleep_ticks--;

        if (task->sleep_ticks <= 0) {
            uart_log("[Tick %u] Sleep expired : %s waking up",
                     (unsigned)g_kernel_tick, task->name);
            scheduler_set_task_state(i, TASK_READY, BLOCK_NONE);
        }
    }
}
