#include "rtos.h"

#include <stdarg.h>
#include <stdio.h>

static TCB task_list[RTOS_MAX_TASKS];
static int task_count = 0;
static int current_task_index = -1;
static int system_tick = 0;

static const char *state_name(TaskState state)
{
    switch (state) {
    case TASK_READY:
        return "READY";
    case TASK_RUNNING:
        return "RUNNING";
    case TASK_BLOCKED:
        return "BLOCKED";
    default:
        return "UNKNOWN";
    }
}

static void unblock_one(BlockReason reason)
{
    int best_index = -1;

    for (int i = 0; i < task_count; i++) {
        if (task_list[i].state != TASK_BLOCKED || task_list[i].block_reason != reason) {
            continue;
        }

        if (best_index < 0 || task_list[i].priority > task_list[best_index].priority) {
            best_index = i;
        }
    }

    if (best_index >= 0) {
        task_list[best_index].state = TASK_READY;
        task_list[best_index].block_reason = BLOCK_NONE;
        uart_log("%s unblocked", task_list[best_index].name);
    }
}

static void update_sleeping_tasks(void)
{
    for (int i = 0; i < task_count; i++) {
        TCB *task = &task_list[i];

        if (task->state == TASK_BLOCKED && task->block_reason == BLOCK_SLEEP) {
            task->sleep_ticks--;
            if (task->sleep_ticks <= 0) {
                task->state = TASK_READY;
                task->block_reason = BLOCK_NONE;
                task->sleep_ticks = 0;
            }
        }
    }
}

static int pick_next_task(void)
{
    int best_index = -1;

    for (int i = 0; i < task_count; i++) {
        if (task_list[i].state != TASK_READY) {
            continue;
        }

        if (best_index < 0 || task_list[i].priority > task_list[best_index].priority) {
            best_index = i;
        }
    }

    return best_index;
}

void rtos_init(void)
{
    task_count = 0;
    current_task_index = -1;
    system_tick = 0;
}

int rtos_create_task(const char *name, int priority, void (*task_function)(void))
{
    if (task_count >= RTOS_MAX_TASKS) {
        uart_log("Task create failed: task table full");
        return -1;
    }

    TCB *task = &task_list[task_count];
    task->task_id = task_count + 1;
    task->priority = priority;
    task->state = TASK_READY;
    task->block_reason = BLOCK_NONE;
    task->sleep_ticks = 0;
    task->context.pc = 0;
    task->context.sp = 0x1000 + (task_count * 0x100);
    task->task_function = task_function;
    task->name = name;

    task_count++;
    uart_log("Task Created: %s priority=%d", task->name, task->priority);
    return task->task_id;
}

void rtos_run(int max_ticks)
{
    for (int tick = 0; tick < max_ticks; tick++) {
        system_tick++;
        update_sleeping_tasks();

        int next = pick_next_task();
        if (next < 0) {
            uart_log("Idle: no READY tasks");
            continue;
        }

        current_task_index = next;
        TCB *task = &task_list[next];
        task->state = TASK_RUNNING;
        task->context.pc++;

        uart_log("Task Switched: %s pc=%d sp=0x%X", task->name, task->context.pc, task->context.sp);
        task->task_function();

        if (task->state == TASK_RUNNING) {
            task->state = TASK_READY;
        }

        uart_log("%s state=%s", task->name, state_name(task->state));
    }
}

void rtos_task_sleep(int ticks)
{
    TCB *task = rtos_current_task();
    if (task == NULL) {
        return;
    }

    task->sleep_ticks = ticks;
    task->state = TASK_BLOCKED;
    task->block_reason = BLOCK_SLEEP;
}

bool rtos_sem_wait(Semaphore *sem)
{
    TCB *task = rtos_current_task();

    if (sem->count > 0) {
        sem->count--;
        uart_log("Semaphore Acquired by %s count=%d", task->name, sem->count);
        return true;
    }

    task->state = TASK_BLOCKED;
    task->block_reason = BLOCK_SEMAPHORE;
    uart_log("%s waiting on semaphore", task->name);
    return false;
}

void rtos_sem_signal(Semaphore *sem)
{
    sem->count++;
    uart_log("Semaphore Released count=%d", sem->count);
    unblock_one(BLOCK_SEMAPHORE);
}

bool rtos_mutex_lock(Mutex *mutex)
{
    TCB *task = rtos_current_task();

    if (!mutex->locked) {
        mutex->locked = true;
        mutex->owner = task->task_id;
        uart_log("Mutex Locked by %s", task->name);
        return true;
    }

    if (mutex->owner == task->task_id) {
        return true;
    }

    task->state = TASK_BLOCKED;
    task->block_reason = BLOCK_MUTEX;
    uart_log("%s waiting on mutex", task->name);
    return false;
}

void rtos_mutex_unlock(Mutex *mutex)
{
    TCB *task = rtos_current_task();

    if (!mutex->locked || mutex->owner != task->task_id) {
        uart_log("Mutex unlock ignored for %s", task->name);
        return;
    }

    mutex->locked = false;
    mutex->owner = -1;
    uart_log("Mutex Released by %s", task->name);
    unblock_one(BLOCK_MUTEX);
}

bool rtos_queue_send(MessageQueue *queue, int value)
{
    TCB *task = rtos_current_task();

    if (queue->count == RTOS_QUEUE_SIZE) {
        task->state = TASK_BLOCKED;
        task->block_reason = BLOCK_QUEUE_FULL;
        uart_log("%s waiting: queue full", task->name);
        return false;
    }

    queue->buffer[queue->rear] = value;
    queue->rear = (queue->rear + 1) % RTOS_QUEUE_SIZE;
    queue->count++;

    uart_log("Queue Send by %s value=%d count=%d", task->name, value, queue->count);
    unblock_one(BLOCK_QUEUE_EMPTY);
    return true;
}

bool rtos_queue_receive(MessageQueue *queue, int *value)
{
    TCB *task = rtos_current_task();

    if (queue->count == 0) {
        task->state = TASK_BLOCKED;
        task->block_reason = BLOCK_QUEUE_EMPTY;
        uart_log("%s waiting: queue empty", task->name);
        return false;
    }

    *value = queue->buffer[queue->front];
    queue->front = (queue->front + 1) % RTOS_QUEUE_SIZE;
    queue->count--;

    uart_log("Queue Receive by %s value=%d count=%d", task->name, *value, queue->count);
    unblock_one(BLOCK_QUEUE_FULL);
    return true;
}

void semaphore_init(Semaphore *sem, int initial_count)
{
    sem->count = initial_count;
}

void mutex_init(Mutex *mutex)
{
    mutex->locked = false;
    mutex->owner = -1;
}

void queue_init(MessageQueue *queue)
{
    queue->front = 0;
    queue->rear = 0;
    queue->count = 0;
}

void uart_log(const char *format, ...)
{
    va_list args;

    printf("[%02d:%02d] ", system_tick / 60, system_tick % 60);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

TCB *rtos_current_task(void)
{
    if (current_task_index < 0 || current_task_index >= task_count) {
        return NULL;
    }

    return &task_list[current_task_index];
}
