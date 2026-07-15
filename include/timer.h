/*
 * timer.h — Kernel Tick, Sleep Management & Software Timers (v1.9)
 *
 * This module owns the monotonic kernel tick counter, all sleep-management
 * logic, and the software timer subsystem.  It is the single authoritative
 * source for the current kernel time inside the RTOS simulator.
 *
 * Public API — Kernel Tick
 * ------------------------
 *   timer_init()           Reset the kernel tick to zero and prepare the
 *                          timer module.  Must be called before any other
 *                          timer function, typically from rtos_init().
 *
 *   timer_tick()           Advance the kernel tick by one unit.  Called once
 *                          per scheduler cycle, before any scheduling decision
 *                          is made for that cycle.
 *
 *   timer_now()            Return the current kernel tick value.  Safe to call
 *                          at any time; does not advance the tick.
 *
 * Internal scheduler API — Sleep
 * --------------------------------
 *   timer_update_sleep()   Decrement each sleeping task's remaining sleep
 *                          counter by one tick and wake any task whose counter
 *                          has reached zero.  Must be called once per cycle,
 *                          immediately after timer_tick(), from scheduler.c.
 *
 * Public API — Software Timers (v1.9)
 * ------------------------------------
 * Software timers are statically allocated objects that fire a callback when
 * their countdown expires.  They are evaluated once per kernel tick inside
 * timer_update_soft(), which is called automatically from timer_update_sleep().
 *
 *   timer_soft_create()    Claim one timer slot and configure it.
 *   timer_soft_start()     Arm a timer.  Resets the countdown.
 *   timer_soft_stop()      Disarm a timer without resetting configuration.
 *
 * Timer modes
 * -----------
 *   TIMER_ONE_SHOT    — fires once, then auto-stops.
 *   TIMER_PERIODIC    — fires every `period_ticks` ticks, auto-reloads.
 *
 * Usage contract
 * --------------
 *   Per scheduler cycle, the expected call order is:
 *
 *     timer_tick();           // advance the kernel clock
 *     timer_update_sleep();   // expire sleeping tasks + fire soft timers
 *     // ... scheduling decisions ...
 *
 * Compatibility note
 * ------------------
 *   Replaces the static `system_tick` counter and `update_sleeping_tasks()`
 *   that lived in scheduler.c up to v1.5.  All v1.5 public APIs are unchanged.
 *   Software timer support added in v1.9; no existing APIs were modified.
 */

#ifndef MINI_RTOS_TIMER_H
#define MINI_RTOS_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include "rtos.h"

/* ------------------------------------------------------------------ */
/* Software timer types                                                 */
/* ------------------------------------------------------------------ */

/*
 * TimerMode — one-shot vs. periodic behaviour.
 */
typedef enum {
    TIMER_ONE_SHOT,   /* fires once, then disarms automatically          */
    TIMER_PERIODIC    /* fires every period_ticks ticks, auto-reloads    */
} TimerMode;

/*
 * SoftTimer — software timer control block.
 *
 * Allocated from a static pool (RTOS_SOFT_TIMER_COUNT slots) by
 * timer_soft_create().  Fields should not be accessed directly by
 * application code; use the API functions below.
 */
typedef struct {
    const char          *name;          /* human-readable identifier        */
    TimerMode            mode;          /* one-shot or periodic             */
    uint32_t             period_ticks;  /* reload value (ticks)             */
    uint32_t             remaining;     /* ticks until next expiry          */
    void               (*callback)(void *arg); /* invoked on expiry         */
    void                *arg;          /* opaque argument passed to callback*/
    bool                 active;       /* true while the timer is running   */
    bool                 in_use;       /* true when this slot is claimed    */
} SoftTimer;

/* ------------------------------------------------------------------ */
/* Public kernel API — tick                                             */
/* ------------------------------------------------------------------ */

/*
 * timer_init() — reset kernel tick, clear soft-timer pool, bind task list.
 *
 * @param tasks       Pointer to the shared TCB array (owned by rtos.c).
 * @param task_count  Pointer to the live task count (owned by rtos.c).
 *
 * Both pointers must remain valid for the lifetime of the RTOS session.
 * Pass NULL for both only in unit-test scenarios where sleep is not used.
 */
void timer_init(TCB *tasks, int *task_count);

/*
 * timer_tick() — advance the kernel tick by exactly one unit.
 *
 * Intended to be called once at the top of every scheduler cycle, before
 * any scheduling decision is made for that cycle.
 */
void timer_tick(void);

/*
 * timer_now() — return the current kernel tick value.
 *
 * Thread-safe by design: this simulator is cooperative and single-threaded,
 * so no locking is required.
 *
 * @return  Monotonically increasing kernel tick (uint32_t).
 *          Returns 0 before the first call to timer_tick().
 */
uint32_t timer_now(void);

/* ------------------------------------------------------------------ */
/* Internal scheduler API — do not call from outside scheduler.c       */
/* ------------------------------------------------------------------ */

/*
 * timer_update_sleep() — decrement sleep counters, wake expired tasks,
 *                        and process all armed software timers.
 *
 * For every task that is BLOCKED with reason BLOCK_SLEEP, this function:
 *   1. Decrements TCB.sleep_ticks by one.
 *   2. When TCB.sleep_ticks reaches zero, transitions the task to TASK_READY
 *      via scheduler_set_task_state() so it re-enters the ready queue.
 *
 * After processing sleep, this function calls timer_update_soft() to
 * decrement and fire any armed software timers.
 *
 * Must be called once per scheduler cycle immediately after timer_tick().
 * Scheduler.c is the only permitted caller.
 */
void timer_update_sleep(void);

/* ------------------------------------------------------------------ */
/* Public API — software timers (v1.9)                                 */
/* ------------------------------------------------------------------ */

/*
 * timer_soft_create() — claim a timer slot and configure it.
 *
 * @param name          Human-readable identifier (used in log messages).
 * @param mode          TIMER_ONE_SHOT or TIMER_PERIODIC.
 * @param period_ticks  Number of kernel ticks per period.  Must be >= 1.
 * @param callback      Function to call on expiry.  Must not be NULL.
 * @param arg           Opaque pointer forwarded to the callback.
 *
 * @return  Pointer to the configured SoftTimer, or NULL if:
 *            - all RTOS_SOFT_TIMER_COUNT slots are already in use.
 *            - period_ticks == 0 or callback == NULL.
 *
 * The returned timer is NOT yet running; call timer_soft_start() to arm it.
 */
SoftTimer *timer_soft_create(const char *name, TimerMode mode,
                              uint32_t period_ticks,
                              void (*callback)(void *arg), void *arg);

/*
 * timer_soft_start() — arm a timer.
 *
 * Resets the countdown to period_ticks and marks the timer as active.
 * Safe to call on an already-running timer (restarts the countdown).
 *
 * @param timer  Timer returned by timer_soft_create().  Must not be NULL.
 */
void timer_soft_start(SoftTimer *timer);

/*
 * timer_soft_stop() — disarm a timer.
 *
 * The timer slot remains allocated; call timer_soft_start() to re-arm or
 * timer_soft_delete() to release the slot.
 *
 * @param timer  Timer returned by timer_soft_create().  Must not be NULL.
 */
void timer_soft_stop(SoftTimer *timer);

/*
 * timer_soft_delete() — release a timer slot back to the pool.
 *
 * The timer is stopped (if active) and its slot is returned so that
 * timer_soft_create() may reuse it.
 *
 * @param timer  Timer returned by timer_soft_create().  Must not be NULL.
 */
void timer_soft_delete(SoftTimer *timer);

#endif /* MINI_RTOS_TIMER_H */
