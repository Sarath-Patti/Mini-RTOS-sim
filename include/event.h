/*
 * event.h — Event Flags (v1.9)
 *
 * Overview
 * --------
 * Event flags provide a lightweight, bitmask-based synchronisation primitive
 * that allows one or more tasks to block until a specific combination of
 * flags is set by another task or a software timer callback.
 *
 * An EventFlags object holds a 32-bit bitmask (`flags`) and a static waiter
 * table that records which tasks are waiting and which bits they require.
 * When new flags are set via event_flags_set(), the module scans the waiter
 * table and unblocks every task whose required mask is now satisfied.
 *
 * Supported primitives
 * --------------------
 *   event_flags_init()     Initialise an EventFlags object to all-zero.
 *   event_flags_set()      Set one or more bits; wake matching waiters.
 *   event_flags_clear()    Clear one or more bits.
 *   event_flags_wait()     Block until the required bits are all set.
 *
 * Blocking model
 * --------------
 * event_flags_wait() is a non-blocking check: it returns true immediately
 * if the requested bits are already set, or false after blocking the calling
 * task (BLOCK_EVENT) if they are not.  The caller must re-check in its next
 * scheduled run.
 *
 * The waiter table is scanned (and waiters woken) synchronously inside
 * event_flags_set().  No separate tick processing is required.
 *
 * Configuration
 * -------------
 *   RTOS_EVENT_MAX_WAITERS — maximum number of simultaneous waiters per
 *   EventFlags object (default 8).  Stored in the EventFlags struct itself.
 *
 * Module boundary
 * ---------------
 *   event.c depends on scheduler.h (to block/unblock tasks) and rtos.h
 *   (for TCB, BlockReason, uart_log).  It does not depend on timer.c,
 *   context.c, or memory.c.
 */

#ifndef MINI_RTOS_EVENT_H
#define MINI_RTOS_EVENT_H

#include <stdbool.h>
#include <stdint.h>

#include "rtos.h"

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */

/*
 * RTOS_EVENT_MAX_WAITERS — maximum simultaneous waiters on one EventFlags
 * object.  Must be <= RTOS_MAX_TASKS.
 */
#ifndef RTOS_EVENT_MAX_WAITERS
#define RTOS_EVENT_MAX_WAITERS RTOS_MAX_TASKS
#endif

/* ------------------------------------------------------------------ */
/* Types                                                                */
/* ------------------------------------------------------------------ */

/*
 * EventWaiter — internal record of one blocked task and its required mask.
 *
 * Fields should not be accessed directly; use the event_flags_* API.
 */
typedef struct {
    int      task_index;    /* index into the shared TCB array, or -1 = empty */
    uint32_t required_mask; /* bits the waiter is waiting for                 */
} EventWaiter;

/*
 * EventFlags — event flag group control block.
 *
 * Declare one of these in static storage and initialise it with
 * event_flags_init() before use.  Do not copy or move the struct after
 * initialisation; the waiter table contains live task indices.
 */
typedef struct {
    uint32_t     flags;                            /* current flag bitmask   */
    EventWaiter  waiters[RTOS_EVENT_MAX_WAITERS];  /* blocked task records   */
    int          waiter_count;                     /* number of live entries */
} EventFlags;

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/*
 * event_flags_init() — initialise an EventFlags object.
 *
 * Clears all flags and empties the waiter table.  Must be called once
 * before any other operation on the object.
 *
 * @param ef  Pointer to an EventFlags object.  Must not be NULL.
 */
void event_flags_init(EventFlags *ef);

/*
 * event_flags_set() — set one or more flags and wake matching waiters.
 *
 * Performs a bitwise OR of `mask` into the current flags.  After updating,
 * scans the waiter table: any task whose `required_mask` is now a subset of
 * the current flags is unblocked via scheduler_set_task_state(TASK_READY).
 *
 * @param ef    Pointer to an EventFlags object.  Must not be NULL.
 * @param mask  Bitmask of flags to set.  At least one bit must be set.
 */
void event_flags_set(EventFlags *ef, uint32_t mask);

/*
 * event_flags_clear() — clear one or more flags.
 *
 * Performs a bitwise AND-NOT of `mask` on the current flags.  Does not
 * affect the waiter table.
 *
 * @param ef    Pointer to an EventFlags object.  Must not be NULL.
 * @param mask  Bitmask of flags to clear.
 */
void event_flags_clear(EventFlags *ef, uint32_t mask);

/*
 * event_flags_wait() — wait for a set of flags.
 *
 * If the required bits are already set, clears them (auto-reset) and
 * returns true without blocking.
 *
 * If the required bits are not yet set, records the calling task in the
 * waiter table, blocks it (BLOCK_EVENT via scheduler_set_current_task_state),
 * and returns false.  The caller should return immediately from its task
 * function after receiving false; it will be rescheduled when the flags are
 * satisfied.
 *
 * Auto-reset semantics: on a successful wait the satisfied bits are cleared
 * so that subsequent waiters must wait for them to be re-set.
 *
 * @param ef    Pointer to an EventFlags object.  Must not be NULL.
 * @param mask  Bitmask of flags the calling task requires.
 *
 * @return  true  — flags were already set; task continues running.
 *          false — flags were not set; task has been blocked.
 */
bool event_flags_wait(EventFlags *ef, uint32_t mask);

/*
 * event_flags_get() — read the current flags without blocking.
 *
 * @param ef  Pointer to an EventFlags object.  Must not be NULL.
 * @return    Current 32-bit flag bitmask.
 */
uint32_t event_flags_get(const EventFlags *ef);

#endif /* MINI_RTOS_EVENT_H */
