/*
 * event.c — Event Flags (v1.9)
 *
 * Implementation notes
 * --------------------
 * The EventFlags object is fully self-contained.  All state lives in the
 * struct passed by the caller (declared in static application storage).
 * This module never allocates memory.
 *
 * Waiter table
 * ------------
 * The waiter table (EventFlags.waiters[RTOS_EVENT_MAX_WAITERS]) is a flat
 * array of (task_index, required_mask) pairs.  Slots with task_index == -1
 * are free.  On event_flags_set(), each live entry is tested: if
 * (current_flags & required_mask) == required_mask the task is unblocked and
 * the slot is freed.
 *
 * Auto-reset
 * ----------
 * When event_flags_wait() succeeds immediately (flags already set), or when
 * event_flags_set() wakes a waiter, the satisfied bits are cleared from
 * EventFlags.flags.  This prevents other waiters from consuming the same
 * event set without the producer explicitly setting it again.
 *
 * Concurrency model
 * -----------------
 * The simulator is single-threaded and cooperative; no locking is needed.
 *
 * Module boundary
 * ---------------
 * event.c depends on scheduler.h for scheduler_set_task_state() and
 * scheduler_set_current_task_state(), and on rtos.h for TCB / uart_log.
 * It does not depend on timer.c, context.c, or memory.c.
 */

#include "event.h"
#include "scheduler.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void event_flags_init(EventFlags *ef)
{
    if (ef == NULL) {
        return;
    }

    ef->flags        = 0u;
    ef->waiter_count = 0;

    for (int i = 0; i < RTOS_EVENT_MAX_WAITERS; i++) {
        ef->waiters[i].task_index    = -1;
        ef->waiters[i].required_mask = 0u;
    }
}

void event_flags_set(EventFlags *ef, uint32_t mask)
{
    if (ef == NULL || mask == 0u) {
        return;
    }

    ef->flags |= mask;

    uart_log("Event Set      : flags=0x%08X mask=0x%08X",
             (unsigned)ef->flags, (unsigned)mask);

    /*
     * Scan the waiter table.  Any task whose required_mask is now a subset
     * of the current flags is unblocked.  We walk the table in reverse so
     * that compacting (swapping with the last entry) does not skip entries.
     */
    for (int i = ef->waiter_count - 1; i >= 0; i--) {
        EventWaiter *w = &ef->waiters[i];

        if ((ef->flags & w->required_mask) != w->required_mask) {
            continue;
        }

        int task_index = w->task_index;

        /* Clear the satisfied bits (auto-reset) */
        ef->flags &= ~w->required_mask;

        /* Compact the table: replace this slot with the last entry */
        ef->waiters[i] = ef->waiters[ef->waiter_count - 1];
        ef->waiters[ef->waiter_count - 1].task_index    = -1;
        ef->waiters[ef->waiter_count - 1].required_mask = 0u;
        ef->waiter_count--;

        uart_log("Event Wake     : task_index=%d flags=0x%08X",
                 task_index, (unsigned)ef->flags);

        scheduler_set_task_state(task_index, TASK_READY, BLOCK_NONE);
    }
}

void event_flags_clear(EventFlags *ef, uint32_t mask)
{
    if (ef == NULL) {
        return;
    }

    ef->flags &= ~mask;

    uart_log("Event Clear    : flags=0x%08X mask=0x%08X",
             (unsigned)ef->flags, (unsigned)mask);
}

bool event_flags_wait(EventFlags *ef, uint32_t mask)
{
    if (ef == NULL || mask == 0u) {
        return false;
    }

    /* Fast path: flags already satisfied */
    if ((ef->flags & mask) == mask) {
        ef->flags &= ~mask;   /* auto-reset */
        uart_log("Event Wait OK  : mask=0x%08X (immediate, flags now 0x%08X)",
                 (unsigned)mask, (unsigned)ef->flags);
        return true;
    }

    /* Slow path: block the calling task */
    if (ef->waiter_count >= RTOS_EVENT_MAX_WAITERS) {
        uart_log("Event Wait FULL: waiter table full, mask=0x%08X dropped",
                 (unsigned)mask);
        return false;
    }

    /* Record the waiter */
    EventWaiter *slot          = &ef->waiters[ef->waiter_count];
    slot->required_mask        = mask;

    /*
     * We need the calling task's index into the TCB array.  rtos_current_task()
     * returns a TCB pointer; the index is derived from scheduler's current_task_index
     * which is exposed indirectly through the BLOCK_EVENT path.
     *
     * The cleanest way without adding a new API: call
     * scheduler_set_current_task_state() to block the task.  The current
     * task index is tracked internally by scheduler.c.  We capture the TCB
     * pointer from rtos_current_task() to derive the index by scanning — but
     * rtos.c owns that.
     *
     * Design decision: store task_index = -1 as a sentinel and patch it
     * immediately using the scheduler_current_task_index value.  Since
     * scheduler_set_current_task_state modifies *current_task_index, we
     * call it first, then fix up the waiter entry.
     *
     * Simpler approach used here: the scheduler exposes the current task's
     * TCB pointer through rtos_current_task().  We keep a separate
     * "task_index" field by recording it before blocking.
     *
     * Because we cannot call rtos_current_task() from event.c (that would
     * create a dependency on rtos.c), we instead rely on a small helper
     * declared below: event_current_task_index().  We inline the logic here
     * using a scheduler-level call: scheduler_set_current_task_state already
     * knows the current task index.  We record the index via a two-step:
     *
     *   1. Mark the slot with task_index = -1 (unknown yet).
     *   2. Call scheduler_set_current_task_state(BLOCKED, BLOCK_EVENT).
     *   3. Because the scheduler has just blocked the current task, the next
     *      time event_flags_set() fires it will unblock by index.  However,
     *      we still need to know WHICH index was blocked.
     *
     * Resolution: we add a minimal dependency — event.c is allowed to
     * reference the scheduler_get_current_task_index() helper which we
     * declare here as an inline retrieval using the existing public API.
     * The scheduler stores `*current_task_index` internally; the only way
     * to retrieve it without touching private state is indirectly.
     *
     * Pragmatic solution for this cooperative, host-based simulator:
     * add a minimal scheduler_current_task_index() function to scheduler.h
     * that returns the current running task index.  This preserves module
     * boundaries (scheduler.c owns task indices) without leaking TCB
     * internals through rtos.h.
     */
    slot->task_index = scheduler_current_task_index();
    ef->waiter_count++;

    uart_log("Event Wait     : task_index=%d mask=0x%08X (blocking)",
             slot->task_index, (unsigned)mask);

    scheduler_set_current_task_state(TASK_BLOCKED, BLOCK_EVENT);

    return false;
}

uint32_t event_flags_get(const EventFlags *ef)
{
    if (ef == NULL) {
        return 0u;
    }

    return ef->flags;
}
