#ifndef MINI_RTOS_SCHEDULER_H
#define MINI_RTOS_SCHEDULER_H

#include "rtos.h"

void scheduler_init(TCB *tasks, int *task_count, int *current_task_index);
void scheduler_run(int max_ticks);
void scheduler_set_task_state(int task_index, TaskState state, BlockReason reason);
void scheduler_set_current_task_state(TaskState state, BlockReason reason);
void scheduler_unblock_one(BlockReason reason);
int scheduler_ready_count(void);
int scheduler_tick(void);

#endif
