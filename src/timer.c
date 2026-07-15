/*
 * timer.c — Kernel Tick, Sleep Management & Software Timers (v1.9)
 *
 * This translation unit is the single owner of:
 *
 *   - g_kernel_tick     : monotonic kernel tick counter
 *   - sleep countdown   : per-tick decrement of TCB.sleep_ticks for every
 *                         sleeping task
 *   - wake-up           : transition sleeping tasks to TASK_READY when their
 *                         sleep duration expires
 *   - software timers   : static pool of SoftTimer objects, decremented
 *                         and fired once per kernel tick
 *
 * No other module tracks wall-clock or tick time.  scheduler.c delegates
 * all tick management here and calls timer_now() wherever it previously
 * read its internal system_tick variable.
 *
 * Software Timer Design (v1.9)
 * ----------------------------
 * Software timers use a statically allocated pool of RTOS_SOFT_TIMER_COUNT
 * SoftTimer structs.  Each timer has an independent countdown (`remaining`)
 * that is decremented once per kernel tick inside timer_update_soft().
 * When `remaining` reaches zero the callback is fired in-place (cooperative,
 * no separate context), and:
 *
 *   - ONE_SHOT timers are automatically disarmed (`active = false`).
 *   - PERIODIC timers reload `remaining = period_ticks` automatically.
 *
 * timer_update_soft() is called by timer_update_sleep() so both paths share
 * the same single call site in scheduler.c — no scheduler changes required.
 *
 * Design constraints
 * ------------------
 *   - Host-based, portable C99 only.
 *   - No hardware timers, interrupts, threads, or assembly.
 *   - No global variables exposed outside this file.
 *   - No malloc / calloc / realloc / free.
 *   - Modifies TCB state exclusively through scheduler_set_task_state().
 */

#include "timer.h"
#include "scheduler.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Module-private state                                                 */
/* ------------------------------------------------------------------ */

/*
 * g_kernel_tick — monotonic kernel tick counter.
 *
 * Starts at 0.  Incremented by timer_tick() exactly once per scheduler
 * cycle.  uint32_t provides ~49 days of 1 ms ticks before rollover.
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

/*
 * g_soft_timers — static pool of software timer control blocks.
 *
 * Slots are claimed by timer_soft_create() and released by
 * timer_soft_delete().  The `in_use` field distinguishes allocated slots
 * from free slots.
 */
static SoftTimer g_soft_timers[RTOS_SOFT_TIMER_COUNT];

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * timer_update_soft() — decrement and fire all armed software timers.
 *
 * Called once per kernel tick from timer_update_sleep().  Iterates the
 * static pool; for each active timer, decrements `remaining`.  When
 * `remaining` reaches zero:
 *
 *   1. Invokes the callback with its argument.
 *   2. ONE_SHOT — sets active = false.
 *   3. PERIODIC  — reloads remaining = period_ticks.
 *
 * This function is static (translation-unit scope) because it is an
 * implementation detail of timer_update_sleep(); nothing outside timer.c
 * needs to call it directly.
 */
static void timer_update_soft(void)
{
    for (int i = 0; i < (int)RTOS_SOFT_TIMER_COUNT; i++) {
        SoftTimer *t = &g_soft_timers[i];

        if (!t->in_use || !t->active) {
            continue;
        }

        if (t->remaining > 0) {
            t->remaining--;
        }

        if (t->remaining == 0) {
            uart_log("[Tick %u] Timer Expired : %s (mode=%s)",
                     (unsigned)g_kernel_tick, t->name,
                     (t->mode == TIMER_PERIODIC) ? "PERIODIC" : "ONE_SHOT");

            uart_log("[Tick %u] Timer Callback: %s executing",
                     (unsigned)g_kernel_tick, t->name);

            t->callback(t->arg);

            uart_log("[Tick %u] Timer Callback: %s done",
                     (unsigned)g_kernel_tick, t->name);

            if (t->mode == TIMER_PERIODIC) {
                t->remaining = t->period_ticks;
                uart_log("[Tick %u] Timer Reload  : %s (next in %u ticks)",
                         (unsigned)g_kernel_tick, t->name,
                         (unsigned)t->period_ticks);
            } else {
                t->active = false;
                uart_log("[Tick %u] Timer Stopped : %s (one-shot complete)",
                         (unsigned)g_kernel_tick, t->name);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API — kernel tick                                             */
/* ------------------------------------------------------------------ */

/*
 * timer_init() — reset the kernel tick, clear the soft-timer pool, and
 *               bind the task list.
 *
 * Safe to call multiple times (e.g. between unit tests) because it
 * unconditionally resets all module state.
 */
void timer_init(TCB *tasks, int *task_count)
{
    g_kernel_tick = 0;
    g_tasks       = tasks;
    g_task_count  = task_count;

    /* Clear all soft-timer slots */
    memset(g_soft_timers, 0, sizeof(g_soft_timers));
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
 * timer_update_sleep() — decrement sleep counters, wake expired tasks,
 *                        and process all armed software timers.
 *
 * Iterates over every task.  For each task that is BLOCKED with reason
 * BLOCK_SLEEP:
 *
 *   1. Decrements TCB.sleep_ticks.
 *   2. When the counter reaches zero (or goes negative due to a missed
 *      tick), calls scheduler_set_task_state(TASK_READY, BLOCK_NONE) to
 *      transition the task back to the ready queue.
 *
 * After processing sleep, calls timer_update_soft() to fire expired
 * software timers.
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
        goto update_soft;
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

update_soft:
    timer_update_soft();
}

/* ------------------------------------------------------------------ */
/* Public API — software timers                                         */
/* ------------------------------------------------------------------ */

SoftTimer *timer_soft_create(const char *name, TimerMode mode,
                              uint32_t period_ticks,
                              void (*callback)(void *arg), void *arg)
{
    /* Validate parameters */
    if (callback == NULL) {
        uart_log("Timer Create FAILED: callback is NULL (name=%s)",
                 name != NULL ? name : "(null)");
        return NULL;
    }

    if (period_ticks == 0) {
        uart_log("Timer Create FAILED: period_ticks must be >= 1 (name=%s)",
                 name != NULL ? name : "(null)");
        return NULL;
    }

    /* Find a free slot */
    for (int i = 0; i < (int)RTOS_SOFT_TIMER_COUNT; i++) {
        SoftTimer *t = &g_soft_timers[i];

        if (t->in_use) {
            continue;
        }

        t->name         = (name != NULL) ? name : "unnamed";
        t->mode         = mode;
        t->period_ticks = period_ticks;
        t->remaining    = period_ticks;
        t->callback     = callback;
        t->arg          = arg;
        t->active       = false;
        t->in_use       = true;

        uart_log("Timer Created  : %s (mode=%s period=%u ticks)",
                 t->name,
                 (mode == TIMER_PERIODIC) ? "PERIODIC" : "ONE_SHOT",
                 (unsigned)period_ticks);

        return t;
    }

    uart_log("Timer Create FAILED: pool exhausted (%u/%u slots used)",
             (unsigned)RTOS_SOFT_TIMER_COUNT,
             (unsigned)RTOS_SOFT_TIMER_COUNT);
    return NULL;
}

void timer_soft_start(SoftTimer *timer)
{
    if (timer == NULL || !timer->in_use) {
        uart_log("Timer Start IGNORED: invalid timer");
        return;
    }

    timer->remaining = timer->period_ticks;
    timer->active    = true;

    uart_log("Timer Started  : %s (fires in %u ticks)",
             timer->name, (unsigned)timer->period_ticks);
}

void timer_soft_stop(SoftTimer *timer)
{
    if (timer == NULL || !timer->in_use) {
        uart_log("Timer Stop IGNORED: invalid timer");
        return;
    }

    timer->active = false;

    uart_log("Timer Stopped  : %s", timer->name);
}

void timer_soft_delete(SoftTimer *timer)
{
    if (timer == NULL || !timer->in_use) {
        uart_log("Timer Delete IGNORED: invalid timer");
        return;
    }

    const char *name = timer->name;
    timer->active  = false;
    timer->in_use  = false;

    uart_log("Timer Deleted  : %s", name);
}
