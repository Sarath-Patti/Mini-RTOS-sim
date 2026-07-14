/*
 * scheduler.c — Preemptive Priority Scheduler (v1.7)
 *
 * Responsibilities
 * ----------------
 *   - Maintain the static ready queue.
 *   - On every tick: select the highest-priority READY task.
 *   - Perform a context switch only when necessary (preemption or
 *     time-slice expiry).  Avoid unnecessary switches when the current
 *     task should continue running.
 *   - Delegate kernel-tick management and sleep expiry to timer.c.
 *
 * Preemptive scheduling algorithm (v1.7)
 * ---------------------------------------
 * At the start of each cycle, after advancing the tick and expiring
 * sleepers, the scheduler performs the following decision:
 *
 *   1. Peek at the highest-priority READY task (without removing it
 *      from the queue).
 *   2. Determine whether to switch:
 *
 *      a. PRIORITY PREEMPTION  — the best READY task has strictly higher
 *         priority than the current running task.  Switch immediately.
 *         Log: "[Preempted] <current> -> <best>"
 *
 *      b. TIME-SLICE EXPIRY    — the best task has equal priority to the
 *         current task AND the current task has consumed >= RTOS_TIME_SLICE_TICKS
 *         consecutive ticks.  Switch to give the next task its turn.
 *         Log: "[Slice expired] <current> yielding to <next>"
 *
 *      c. CONTINUE             — the current task is still the best
 *         candidate and its slice has not expired.  No context switch.
 *         Log: "[Tick N] Continue : <task> (slice=S/MAX)"
 *
 *   3. If a switch is needed, pop the best task from the ready queue,
 *      push the outgoing task back (if still runnable), save/restore
 *      contexts, and run the incoming task.
 *
 * Time-slice counter (slice_ticks_used)
 * ---------------------------------------
 *   - Incremented each tick the task continues running.
 *   - Reset to 0 whenever the task is preempted, blocks, or completes.
 *   - Also reset when the task is re-enqueued (scheduler_set_task_state).
 *
 * Per-cycle call contract with timer.c
 * -------------------------------------
 *   timer_tick();           // advance the kernel clock by one unit
 *   timer_update_sleep();   // expire sleeping tasks that are now due
 *   // ... preemption decisions ...
 */

#include "context.h"
#include "scheduler.h"
#include "timer.h"

static TCB *task_list;
static int *task_count;
static int *current_task_index;
static int  ready_queue[RTOS_MAX_TASKS];
static int  ready_count;

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static const char *state_name(TaskState state)
{
    switch (state) {
    case TASK_READY:     return "READY";
    case TASK_RUNNING:   return "RUNNING";
    case TASK_BLOCKED:   return "BLOCKED";
    case TASK_SUSPENDED: return "SUSPENDED";
    default:             return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/* Ready-queue primitives                                               */
/* ------------------------------------------------------------------ */

static bool ready_queue_contains(int task_index)
{
    for (int i = 0; i < ready_count; i++) {
        if (ready_queue[i] == task_index) {
            return true;
        }
    }

    return false;
}

static void ready_queue_remove(int task_index)
{
    for (int i = 0; i < ready_count; i++) {
        if (ready_queue[i] != task_index) {
            continue;
        }

        for (int j = i; j < ready_count - 1; j++) {
            ready_queue[j] = ready_queue[j + 1];
        }

        ready_count--;
        return;
    }
}

static void ready_queue_enqueue(int task_index)
{
    if (task_list == NULL || task_count == NULL) {
        return;
    }

    if (task_index < 0 || task_index >= *task_count) {
        return;
    }

    if (task_list[task_index].state != TASK_READY) {
        return;
    }

    if (ready_count >= RTOS_MAX_TASKS || ready_queue_contains(task_index)) {
        return;
    }

    ready_queue[ready_count] = task_index;
    ready_count++;
}

/*
 * ready_queue_peek_highest_priority() — find the best READY candidate
 * without removing it from the queue.
 *
 * Returns the task index of the highest-priority READY task, or -1 if
 * the queue is empty.  Stale (non-READY) entries are evicted during the
 * scan.
 */
static int ready_queue_peek_highest_priority(void)
{
    int best_queue_pos = -1;

    for (int i = 0; i < ready_count; i++) {
        int task_index = ready_queue[i];

        if (task_list[task_index].state != TASK_READY) {
            ready_queue_remove(task_index);
            i--;
            continue;
        }

        if (best_queue_pos < 0 ||
            task_list[task_index].priority >
            task_list[ready_queue[best_queue_pos]].priority) {
            best_queue_pos = i;
        }
    }

    if (best_queue_pos < 0) {
        return -1;
    }

    return ready_queue[best_queue_pos];
}

/*
 * ready_queue_pop() — remove and return a specific task from the queue.
 *
 * Used after peek has already identified the task to schedule.
 */
static void ready_queue_pop(int task_index)
{
    ready_queue_remove(task_index);
}

/* ------------------------------------------------------------------ */
/* Context-switch helper                                                */
/* ------------------------------------------------------------------ */

/*
 * do_context_switch() — execute the full six-phase software context-switch
 * sequence and run the incoming task's function.
 *
 * @param outgoing_index  Index of the task being preempted, or -1 on
 *                        the very first tick (no outgoing task).
 * @param incoming_index  Index of the task to run.
 * @param switch_reason   Short label for the log (e.g. "Preempted",
 *                        "Slice expired", "Priority switch", "First tick").
 */
static void do_context_switch(int outgoing_index,
                               int incoming_index,
                               const char *switch_reason)
{
    uint32_t tick      = timer_now();
    TCB     *outgoing  = (outgoing_index >= 0) ? &task_list[outgoing_index] : NULL;
    TCB     *incoming  = &task_list[incoming_index];

    /* Phase 1a — announce the outgoing task */
    if (outgoing != NULL) {
        uart_log("[Tick %u] Current Task  : %s (state=%s)",
                 (unsigned)tick, outgoing->name,
                 state_name(outgoing->state));
    }

    /* Phase 1b / Phase 2 — save outgoing + switch to incoming context */
    if (outgoing != NULL) {
        context_switch(&outgoing->context, &incoming->context);
        uart_log("[Tick %u] Context Saved  : %s",
                 (unsigned)tick, outgoing->name);
    } else {
        /* First tick — no outgoing task; simply load the incoming context */
        context_restore(&incoming->context);
    }

    /* Phase 2 log — announce the switch with the reason */
    uart_log("[Tick %u] Context Switch [%s]: %s -> %s",
             (unsigned)tick, switch_reason,
             outgoing != NULL ? outgoing->name : "(none)",
             incoming->name);

    /* Phase 3 — record incoming as current, confirm restored context */
    *current_task_index = incoming_index;

    /*
     * Count the initial run as the first slice tick so that the slice-expiry
     * check triggers after (RTOS_TIME_SLICE_TICKS - 1) additional Continue
     * ticks, giving each task exactly RTOS_TIME_SLICE_TICKS total runs before
     * rotating.  Starting at 0 would grant one free Continue tick on top of
     * the initial switch-in run, causing an off-by-one in rotation.
     */
    incoming->slice_ticks_used = 1;

    uart_log("[Tick %u] Next Task      : %s (priority=%d)",
             (unsigned)tick, incoming->name, incoming->priority);
    uart_log("[Tick %u] Context Restored: %s (pc=0x%08X lr=0x%08X)",
             (unsigned)tick, incoming->name,
             context_active()->pc, context_active()->lr);

    /* Phase 4 — run the task */
    scheduler_set_task_state(incoming_index, TASK_RUNNING, BLOCK_NONE);
    uart_log("[Tick %u] Task Running   : %s sp=%p ready=%d",
             (unsigned)tick, incoming->name,
             (void *)incoming->stack_pointer, ready_count);

    incoming->task_function();

    if (incoming->state == TASK_RUNNING) {
        scheduler_set_task_state(incoming_index, TASK_READY, BLOCK_NONE);
    }

    uart_log("[Tick %u] %s state=%s",
             (unsigned)tick, incoming->name, state_name(incoming->state));
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void scheduler_init(TCB *tasks, int *task_count_ptr, int *current_task_index_ptr)
{
    task_list          = tasks;
    task_count         = task_count_ptr;
    current_task_index = current_task_index_ptr;
    ready_count        = 0;

    /*
     * timer_init() is called here with the same task array so the timer
     * module can decrement sleep counters and wake sleeping tasks without
     * needing a separate initialisation call from rtos_init().
     */
    timer_init(tasks, task_count_ptr);
}

/*
 * scheduler_run() — preemptive priority scheduling loop (v1.7).
 *
 * Per-cycle decision logic:
 *
 *   1. Advance the kernel tick (timer_tick).
 *   2. Wake any tasks whose sleep has expired (timer_update_sleep).
 *   3. Peek at the highest-priority READY task.
 *   4a. If no READY task exists → idle.
 *   4b. If no task is currently running (first tick) → switch to best.
 *   4c. If best.priority > current.priority → PRIORITY PREEMPTION.
 *   4d. If best.priority == current.priority AND
 *          current.slice_ticks_used >= RTOS_TIME_SLICE_TICKS → SLICE EXPIRY.
 *   4e. Otherwise → CONTINUE (increment slice counter, run current task).
 */
void scheduler_run(int max_ticks)
{
    for (int cycle = 0; cycle < max_ticks; cycle++) {

        /* --- Advance kernel time and update sleeping tasks --- */
        timer_tick();
        timer_update_sleep();

        uint32_t tick = timer_now();

        /* -------------------------------------------------------- */
        /* Identify currently running task                           */
        /* -------------------------------------------------------- */
        int   cur_idx  = -1;
        TCB  *cur_task = NULL;

        if (current_task_index != NULL &&
            *current_task_index >= 0 &&
            *current_task_index < *task_count) {
            cur_idx  = *current_task_index;
            cur_task = &task_list[cur_idx];
        }

        /* -------------------------------------------------------- */
        /* Peek at the best available READY task                     */
        /* -------------------------------------------------------- */
        int best_idx = ready_queue_peek_highest_priority();

        if (best_idx < 0) {
            uart_log("[Tick %u] Idle: no READY tasks", (unsigned)tick);
            /* If the running task is still alive (e.g. it blocked mid-tick
             * and there is nothing else), reset its slice so it can resume
             * cleanly when it wakes. */
            if (cur_task != NULL) {
                cur_task->slice_ticks_used = 0;
            }
            continue;
        }

        /* -------------------------------------------------------- */
        /* Preemption / continuation decision                        */
        /* -------------------------------------------------------- */

        /* Case A: first tick — no current task */
        if (cur_task == NULL) {
            ready_queue_pop(best_idx);
            do_context_switch(-1, best_idx, "First tick");
            continue;
        }

        int  best_prio = task_list[best_idx].priority;
        int  cur_prio  = cur_task->priority;

        /*
         * Case B (guard): current task is no longer runnable.
         *
         * If the current task blocked or was suspended during the previous
         * tick (state is TASK_BLOCKED or TASK_SUSPENDED), it must not enter
         * the Continue path.  Priority comparison is only meaningful between
         * runnable tasks.  Switch immediately to the best READY task without
         * re-enqueueing the outgoing task (it is already off the ready queue).
         */
        if (cur_task->state != TASK_RUNNING && cur_task->state != TASK_READY) {
            uart_log("[Tick %u] [Forced switch]: %s (%s) -> %s",
                     (unsigned)tick, cur_task->name,
                     state_name(cur_task->state),
                     task_list[best_idx].name);

            ready_queue_pop(best_idx);
            do_context_switch(cur_idx, best_idx, "Forced");
            continue;
        }

        /* Case C: higher-priority task is READY → priority preemption */
        if (best_prio > cur_prio) {
            uart_log("[Tick %u] [Preempted]    : %s (pri=%d) by %s (pri=%d)",
                     (unsigned)tick, cur_task->name, cur_prio,
                     task_list[best_idx].name, best_prio);

            /* Re-enqueue the outgoing task if it is still runnable */
            if (cur_task->state == TASK_RUNNING) {
                scheduler_set_task_state(cur_idx, TASK_READY, BLOCK_NONE);
            }

            ready_queue_pop(best_idx);
            do_context_switch(cur_idx, best_idx, "Preempted");
            continue;
        }

        /* Case D: equal priority, time slice expired → round-robin */
        if (best_prio == cur_prio &&
            cur_task->slice_ticks_used >= RTOS_TIME_SLICE_TICKS &&
            best_idx != cur_idx) {
            uart_log("[Tick %u] [Slice expired]: %s yielding to %s (slice=%u/%u)",
                     (unsigned)tick, cur_task->name, task_list[best_idx].name,
                     (unsigned)cur_task->slice_ticks_used,
                     (unsigned)RTOS_TIME_SLICE_TICKS);

            if (cur_task->state == TASK_RUNNING) {
                scheduler_set_task_state(cur_idx, TASK_READY, BLOCK_NONE);
            }

            ready_queue_pop(best_idx);
            do_context_switch(cur_idx, best_idx, "Slice expired");
            continue;
        }

        /* Case E: current task continues — no context switch needed.
         *
         * Precondition (enforced by the guard above): cur_task is either
         * TASK_RUNNING or TASK_READY — it is definitely runnable.
         *
         * The best READY task either has a lower priority than the running
         * task, or the slice has not yet expired for an equal-priority peer.
         * The current task gets another tick.
         *
         * We must still consume one slot from the ready queue (pop + re-run)
         * if the current task IS the best candidate, so that it actually runs.
         * If the best candidate is a *different* task that is lower priority,
         * we simply give the current task another run without touching it.
         */
        cur_task->slice_ticks_used++;

        uart_log("[Tick %u] Continue      : %s (slice=%u/%u, priority=%d)",
                 (unsigned)tick, cur_task->name,
                 (unsigned)cur_task->slice_ticks_used,
                 (unsigned)RTOS_TIME_SLICE_TICKS,
                 cur_prio);

        /* Run the current task for this tick */
        scheduler_set_task_state(cur_idx, TASK_RUNNING, BLOCK_NONE);
        uart_log("[Tick %u] Task Running   : %s sp=%p ready=%d",
                 (unsigned)tick, cur_task->name,
                 (void *)cur_task->stack_pointer, ready_count);

        cur_task->task_function();

        if (cur_task->state == TASK_RUNNING) {
            scheduler_set_task_state(cur_idx, TASK_READY, BLOCK_NONE);
        }

        uart_log("[Tick %u] %s state=%s",
                 (unsigned)tick, cur_task->name, state_name(cur_task->state));
    }
}

void scheduler_set_task_state(int task_index, TaskState state, BlockReason reason)
{
    if (task_list == NULL || task_count == NULL) {
        return;
    }

    if (task_index < 0 || task_index >= *task_count) {
        return;
    }

    ready_queue_remove(task_index);

    task_list[task_index].state        = state;
    task_list[task_index].block_reason = reason;

    if (state == TASK_READY) {
        task_list[task_index].sleep_ticks  = 0;
        task_list[task_index].block_reason = BLOCK_NONE;
        /*
         * Do NOT reset slice_ticks_used here.  The slice counter is reset
         * exclusively in do_context_switch() on the incoming task, which
         * ensures it only resets when the task actually receives the CPU.
         * Resetting it here would zero the counter every time the Continue
         * path re-queues the running task as READY at the end of a tick,
         * preventing slice_ticks_used from ever reaching RTOS_TIME_SLICE_TICKS.
         */
        ready_queue_enqueue(task_index);
    }
}

void scheduler_set_current_task_state(TaskState state, BlockReason reason)
{
    if (current_task_index == NULL) {
        return;
    }

    scheduler_set_task_state(*current_task_index, state, reason);
}

void scheduler_unblock_one(BlockReason reason)
{
    int best_index = -1;

    for (int i = 0; i < *task_count; i++) {
        if (task_list[i].state != TASK_BLOCKED ||
            task_list[i].block_reason != reason) {
            continue;
        }

        if (best_index < 0 ||
            task_list[i].priority > task_list[best_index].priority) {
            best_index = i;
        }
    }

    if (best_index >= 0) {
        scheduler_set_task_state(best_index, TASK_READY, BLOCK_NONE);
        uart_log("%s unblocked", task_list[best_index].name);
    }
}

int scheduler_ready_count(void)
{
    return ready_count;
}
