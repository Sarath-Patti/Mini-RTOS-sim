#ifndef MINI_RTOS_H
#define MINI_RTOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "context.h"

#define RTOS_MAX_TASKS 8
#define RTOS_QUEUE_SIZE 10
#define RTOS_STACK_SIZE 256u

typedef uintptr_t RtosStackWord;

#define RTOS_STACK_WORDS (RTOS_STACK_SIZE / sizeof(RtosStackWord))

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_SUSPENDED
} TaskState;

typedef enum {
    BLOCK_NONE,
    BLOCK_SLEEP,
    BLOCK_SEMAPHORE,
    BLOCK_MUTEX,
    BLOCK_QUEUE_EMPTY,
    BLOCK_QUEUE_FULL
} BlockReason;

typedef struct {
    int task_id;
    int priority;
    TaskState state;
    BlockReason block_reason;
    int sleep_ticks;
    RtosStackWord stack_memory[RTOS_STACK_WORDS];
    size_t stack_size;
    RtosStackWord *stack_pointer;
    CPUContext context;
    void (*task_function)(void);
    const char *name;
} TCB;

typedef struct {
    int count;
} Semaphore;

typedef struct {
    bool locked;
    int owner;
} Mutex;

typedef struct {
    int buffer[RTOS_QUEUE_SIZE];
    int front;
    int rear;
    int count;
} MessageQueue;

void rtos_init(void);
int rtos_create_task(const char *name, int priority, void (*task_function)(void));
void rtos_run(int max_ticks);
void rtos_task_sleep(int ticks);
bool rtos_suspend_task(int task_id);
bool rtos_resume_task(int task_id);
TaskState rtos_get_task_state(int task_id);
int rtos_ready_count(void);
const RtosStackWord *rtos_get_task_stack_base(int task_id);
const RtosStackWord *rtos_get_task_stack_pointer(int task_id);
size_t rtos_get_task_stack_size(int task_id);

bool rtos_sem_wait(Semaphore *sem);
void rtos_sem_signal(Semaphore *sem);

bool rtos_mutex_lock(Mutex *mutex);
void rtos_mutex_unlock(Mutex *mutex);

bool rtos_queue_send(MessageQueue *queue, int value);
bool rtos_queue_receive(MessageQueue *queue, int *value);

void semaphore_init(Semaphore *sem, int initial_count);
void mutex_init(Mutex *mutex);
void queue_init(MessageQueue *queue);

void uart_log(const char *format, ...);
TCB *rtos_current_task(void);

#endif
