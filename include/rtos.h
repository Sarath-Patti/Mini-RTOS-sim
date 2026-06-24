#ifndef MINI_RTOS_H
#define MINI_RTOS_H

#include <stdbool.h>
#include <stddef.h>

#define RTOS_MAX_TASKS 8
#define RTOS_QUEUE_SIZE 10

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED
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
    int pc;
    int sp;
} Context;

typedef struct {
    int task_id;
    int priority;
    TaskState state;
    BlockReason block_reason;
    int sleep_ticks;
    Context context;
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
