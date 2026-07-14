/*
 * timer.h — Kernel Tick & Timer Management (v1.6)
 *
 * This module owns the monotonic kernel tick counter and all sleep-management
 * logic.  It is the single authoritative source for the current kernel time
 * inside the RTOS simulator.
 *
 * Public API
 * ----------
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
 * Internal scheduler API
 * ----------------------
 *   timer_update_sleep()   Decrement each sleeping task's remaining sleep
 *                          counter by one tick and wake any task whose counter
 *                          has reached zero.  Must be called once per cycle,
 *                          immediately after timer_tick(), from scheduler.c.
 *
 * Usage contract
 * --------------
 *   Per scheduler cycle, the expected call order is:
 *
 *     timer_tick();           // advance the kernel clock
 *     timer_update_sleep();   // expire sleeping tasks that are now due
 *     // ... scheduling decisions ...
 *
 * Compatibility note
 * ------------------
 *   Replaces the static `system_tick` counter and `update_sleeping_tasks()`
 *   that lived in scheduler.c up to v1.5.  All v1.5 public APIs are unchanged.
 */

#ifndef MINI_RTOS_TIMER_H
#define MINI_RTOS_TIMER_H

#include <stdint.h>

#include "rtos.h"

/* ------------------------------------------------------------------ */
/* Public kernel API                                                    */
/* ------------------------------------------------------------------ */

/*
 * timer_init() — reset kernel tick and bind the task list.
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
 * timer_update_sleep() — decrement sleep counters and wake expired tasks.
 *
 * For every task that is BLOCKED with reason BLOCK_SLEEP, this function:
 *   1. Decrements TCB.sleep_ticks by one.
 *   2. When TCB.sleep_ticks reaches zero, transitions the task to TASK_READY
 *      via scheduler_set_task_state() so it re-enters the ready queue.
 *
 * Must be called once per scheduler cycle immediately after timer_tick().
 * Scheduler.c is the only permitted caller.
 */
void timer_update_sleep(void);

#endif /* MINI_RTOS_TIMER_H */
