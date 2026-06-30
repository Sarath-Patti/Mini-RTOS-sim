#include "rtos.h"
#include "scheduler.h"

#include <stdarg.h>
#include <stdio.h>

static TCB task_list[RTOS_MAX_TASKS];
static int task_count = 0;
static int current_task_index = -1;

static int task_index_from_id(int task_id)
{
    for (int i = 0; i < task_count; i++) {
        if (task_list[i].task_id == task_id) {
            return i;
        }
    }

    return -1;
}

void rtos_init(void)
{
    task_count = 0;
    current_task_index = -1;
    scheduler_init(task_list, &task_count, &current_task_index);
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
    task->state = TASK_SUSPENDED;
    task->block_reason = BLOCK_NONE;
    task->sleep_ticks = 0;
    task->context.pc = 0;
    task->context.sp = 0x1000 + (task_count * 0x100);
    task->task_function = task_function;
    task->name = name;

    task_count++;
    scheduler_set_task_state(task_count - 1, TASK_READY, BLOCK_NONE);
    uart_log("Task Created: %s priority=%d", task->name, task->priority);
    return task->task_id;
}

void rtos_run(int max_ticks)
{
    scheduler_run(max_ticks);
}

void rtos_task_sleep(int ticks)
{
    TCB *task = rtos_current_task();
    if (task == NULL) {
        return;
    }

    task->sleep_ticks = ticks;
    scheduler_set_current_task_state(TASK_BLOCKED, BLOCK_SLEEP);
}

bool rtos_suspend_task(int task_id)
{
    int task_index = task_index_from_id(task_id);

    if (task_index < 0) {
        return false;
    }

    scheduler_set_task_state(task_index, TASK_SUSPENDED, BLOCK_NONE);
    uart_log("Task Suspended: %s", task_list[task_index].name);
    return true;
}

bool rtos_resume_task(int task_id)
{
    int task_index = task_index_from_id(task_id);

    if (task_index < 0 || task_list[task_index].state != TASK_SUSPENDED) {
        return false;
    }

    scheduler_set_task_state(task_index, TASK_READY, BLOCK_NONE);
    uart_log("Task Resumed: %s", task_list[task_index].name);
    return true;
}

TaskState rtos_get_task_state(int task_id)
{
    int task_index = task_index_from_id(task_id);

    if (task_index < 0) {
        return TASK_SUSPENDED;
    }

    return task_list[task_index].state;
}

int rtos_ready_count(void)
{
    return scheduler_ready_count();
}

bool rtos_sem_wait(Semaphore *sem)
{
    TCB *task = rtos_current_task();

    if (sem->count > 0) {
        sem->count--;
        uart_log("Semaphore Acquired by %s count=%d", task->name, sem->count);
        return true;
    }

    scheduler_set_current_task_state(TASK_BLOCKED, BLOCK_SEMAPHORE);
    uart_log("%s waiting on semaphore", task->name);
    return false;
}

void rtos_sem_signal(Semaphore *sem)
{
    sem->count++;
    uart_log("Semaphore Released count=%d", sem->count);
    scheduler_unblock_one(BLOCK_SEMAPHORE);
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

    scheduler_set_current_task_state(TASK_BLOCKED, BLOCK_MUTEX);
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
    scheduler_unblock_one(BLOCK_MUTEX);
}

bool rtos_queue_send(MessageQueue *queue, int value)
{
    TCB *task = rtos_current_task();

    if (queue->count == RTOS_QUEUE_SIZE) {
        scheduler_set_current_task_state(TASK_BLOCKED, BLOCK_QUEUE_FULL);
        uart_log("%s waiting: queue full", task->name);
        return false;
    }

    queue->buffer[queue->rear] = value;
    queue->rear = (queue->rear + 1) % RTOS_QUEUE_SIZE;
    queue->count++;

    uart_log("Queue Send by %s value=%d count=%d", task->name, value, queue->count);
    scheduler_unblock_one(BLOCK_QUEUE_EMPTY);
    return true;
}

bool rtos_queue_receive(MessageQueue *queue, int *value)
{
    TCB *task = rtos_current_task();

    if (queue->count == 0) {
        scheduler_set_current_task_state(TASK_BLOCKED, BLOCK_QUEUE_EMPTY);
        uart_log("%s waiting: queue empty", task->name);
        return false;
    }

    *value = queue->buffer[queue->front];
    queue->front = (queue->front + 1) % RTOS_QUEUE_SIZE;
    queue->count--;

    uart_log("Queue Receive by %s value=%d count=%d", task->name, *value, queue->count);
    scheduler_unblock_one(BLOCK_QUEUE_FULL);
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

    printf("[%02d:%02d] ", scheduler_tick() / 60, scheduler_tick() % 60);
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
