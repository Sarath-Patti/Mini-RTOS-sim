/*
 * scheduler.h — Internal Scheduler API
 *
 * This header is intended for use by kernel-internal modules only.
 * Application code should not call these functions directly; use the
 * public rtos_* API in rtos.h instead.
 *
 * Functions
 * ---------
 *   scheduler_init()                 Bind task list and initialise the timer.
 *   scheduler_run()                  Main scheduling loop (tick-driven).
 *   scheduler_set_task_state()       Transition a task by index; maintain queue.
 *   scheduler_set_current_task_state() Transition the currently running task.
 *   scheduler_unblock_one()          Wake the highest-priority waiter.
 *   scheduler_ready_count()          Query number of READY tasks.
 *   scheduler_current_task_index()   Return the current task's TCB index.
 */

#ifndef MINI_RTOS_SCHEDULER_H
#define MINI_RTOS_SCHEDULER_H

#include "rtos.h"

void scheduler_init(TCB *tasks, int *task_count, int *current_task_index);
void scheduler_run(int max_ticks);
void scheduler_set_task_state(int task_index, TaskState state, BlockReason reason);
void scheduler_set_current_task_state(TaskState state, BlockReason reason);
void scheduler_unblock_one(BlockReason reason);
int  scheduler_ready_count(void);
int  scheduler_current_task_index(void);   /* returns -1 when no task runs */

#endif
