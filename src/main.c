#include "rtos.h"

#include <stdio.h>

static Semaphore data_ready;
static Mutex uart_mutex;
static MessageQueue sensor_queue;

static void sensor_task(void)
{
    static int sample = 100;

    if (rtos_queue_send(&sensor_queue, sample)) {
        uart_log("Sensor Task produced sample=%d", sample);
        sample += 5;
        rtos_sem_signal(&data_ready);
    }

    rtos_task_sleep(3);
}

static void logger_task(void)
{
    int sample = 0;

    if (!rtos_sem_wait(&data_ready)) {
        return;
    }

    if (!rtos_queue_receive(&sensor_queue, &sample)) {
        return;
    }

    if (!rtos_mutex_lock(&uart_mutex)) {
        return;
    }

    uart_log("Logger Task stored sample=%d", sample);
    rtos_mutex_unlock(&uart_mutex);
    rtos_task_sleep(2);
}

static void display_task(void)
{
    if (!rtos_mutex_lock(&uart_mutex)) {
        return;
    }

    uart_log("Display Task refreshed");
    rtos_mutex_unlock(&uart_mutex);
    rtos_task_sleep(4);
}

int main(void)
{
    rtos_init();
    semaphore_init(&data_ready, 0);
    mutex_init(&uart_mutex);
    queue_init(&sensor_queue);

    rtos_create_task("Sensor Task", 3, sensor_task);
    rtos_create_task("Logger Task", 2, logger_task);
    rtos_create_task("Display Task", 1, display_task);

    puts("Mini RTOS PC Simulator");
    puts("----------------------");
    rtos_run(25);

    return 0;
}
