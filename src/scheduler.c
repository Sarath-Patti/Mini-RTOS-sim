#include "context.h"
#include "scheduler.h"

static TCB *task_list;
static int *task_count;
static int *current_task_index;
static int ready_queue[RTOS_MAX_TASKS];
static int ready_count;
static int system_tick;

static const char *state_name(TaskState state)
{
    switch (state) {
    case TASK_READY:
        return "READY";
    case TASK_RUNNING:
        return "RUNNING";
    case TASK_BLOCKED:
        return "BLOCKED";
    case TASK_SUSPENDED:
        return "SUSPENDED";
    default:
        return "UNKNOWN";
    }
}

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
    if (task_list == 0 || task_count == 0) {
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

static int ready_queue_pop_highest_priority(void)
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
            task_list[task_index].priority > task_list[ready_queue[best_queue_pos]].priority) {
            best_queue_pos = i;
        }
    }

    if (best_queue_pos < 0) {
        return -1;
    }

    int selected_task_index = ready_queue[best_queue_pos];
    ready_queue_remove(selected_task_index);
    return selected_task_index;
}

static void update_sleeping_tasks(void)
{
    for (int i = 0; i < *task_count; i++) {
        TCB *task = &task_list[i];

        if (task->state == TASK_BLOCKED && task->block_reason == BLOCK_SLEEP) {
            task->sleep_ticks--;
            if (task->sleep_ticks <= 0) {
                scheduler_set_task_state(i, TASK_READY, BLOCK_NONE);
            }
        }
    }
}

static int pick_next_task(void)
{
    return ready_queue_pop_highest_priority();
}

void scheduler_init(TCB *tasks, int *task_count_ptr, int *current_task_index_ptr)
{
    task_list = tasks;
    task_count = task_count_ptr;
    current_task_index = current_task_index_ptr;
    ready_count = 0;
    system_tick = 0;
}

void scheduler_run(int max_ticks)
{
    for (int tick = 0; tick < max_ticks; tick++) {
        int next;
        TCB *task;
        TCB *current_task;

        system_tick++;
        update_sleeping_tasks();

        /*
         * Identify the currently running task so we can save its context
         * before selecting the next task.  On the very first tick there is
         * no current task, so current_task stays NULL and the save phase is
         * skipped.
         */
        current_task = NULL;
        if (current_task_index != 0 &&
            *current_task_index >= 0 &&
            *current_task_index < *task_count) {
            current_task = &task_list[*current_task_index];
        }

        /* Select the highest-priority READY task */
        next = pick_next_task();
        if (next < 0) {
            uart_log("[Tick %d] Idle: no READY tasks", system_tick);
            continue;
        }

        task = &task_list[next];

        /*
         * --- Software Context Switch sequence ---
         *
         * Phase 1: announce the outgoing task and save its context.
         * Phase 2: perform the atomic context switch.
         * Phase 3: announce the incoming task and confirm its context.
         * Phase 4: run the task function.
         */

        /* Phase 1a — Current Task */
        if (current_task != NULL) {
            uart_log("[Tick %d] Current Task  : %s (state=%s)",
                     system_tick, current_task->name,
                     state_name(current_task->state));
        }

        /* Phase 1b — Context Saved (outgoing) + Phase 2 — Context Switch */
        if (current_task != NULL) {
            /*
             * context_switch() snapshots current_task->context from
             * active_context (save) and then loads task->context into
             * active_context (restore) in a single call.
             */
            context_switch(&current_task->context, &task->context);
            uart_log("[Tick %d] Context Saved  : %s",
                     system_tick, current_task->name);
        } else {
            /*
             * First tick — no outgoing task.  Only the incoming task's
             * context needs to be loaded so execution begins cleanly.
             */
            context_restore(&task->context);
        }

        uart_log("[Tick %d] Context Switch : %s -> %s",
                 system_tick,
                 current_task != NULL ? current_task->name : "(none)",
                 task->name);

        /* Phase 3 — Next Task + Context Restored */
        *current_task_index = next;
        uart_log("[Tick %d] Next Task      : %s (priority=%d)",
                 system_tick, task->name, task->priority);
        uart_log("[Tick %d] Context Restored: %s (pc=0x%08X lr=0x%08X)",
                 system_tick, task->name,
                 context_active()->pc, context_active()->lr);

        /* Phase 4 — Task Running */
        scheduler_set_task_state(next, TASK_RUNNING, BLOCK_NONE);
        uart_log("[Tick %d] Task Running   : %s sp=%p ready=%d",
                 system_tick, task->name,
                 (void *)task->stack_pointer, ready_count);

        task->task_function();

        if (task->state == TASK_RUNNING) {
            scheduler_set_task_state(next, TASK_READY, BLOCK_NONE);
        }

        uart_log("[Tick %d] %s state=%s",
                 system_tick, task->name, state_name(task->state));
    }
}

void scheduler_set_task_state(int task_index, TaskState state, BlockReason reason)
{
    if (task_list == 0 || task_count == 0) {
        return;
    }

    if (task_index < 0 || task_index >= *task_count) {
        return;
    }

    ready_queue_remove(task_index);

    task_list[task_index].state = state;
    task_list[task_index].block_reason = reason;

    if (state == TASK_READY) {
        task_list[task_index].sleep_ticks = 0;
        task_list[task_index].block_reason = BLOCK_NONE;
        ready_queue_enqueue(task_index);
    }
}

void scheduler_set_current_task_state(TaskState state, BlockReason reason)
{
    if (current_task_index == 0) {
        return;
    }

    scheduler_set_task_state(*current_task_index, state, reason);
}

void scheduler_unblock_one(BlockReason reason)
{
    int best_index = -1;

    for (int i = 0; i < *task_count; i++) {
        if (task_list[i].state != TASK_BLOCKED || task_list[i].block_reason != reason) {
            continue;
        }

        if (best_index < 0 || task_list[i].priority > task_list[best_index].priority) {
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

int scheduler_tick(void)
{
    return system_tick;
}
