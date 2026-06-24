#include "rtos.h"

#include <assert.h>
#include <stdio.h>

static int ready_runs;
static int blocked_runs;
static int fallback_runs;
static int suspended_runs;
static int queue_runs[3];

static void ready_task(void)
{
    ready_runs++;
}

static void blocking_task(void)
{
    blocked_runs++;
    rtos_task_sleep(3);
}

static void fallback_task(void)
{
    fallback_runs++;
}

static void suspended_task(void)
{
    suspended_runs++;
}

static void queue_task_0(void)
{
    queue_runs[0]++;
}

static void queue_task_1(void)
{
    queue_runs[1]++;
}

static void queue_task_2(void)
{
    queue_runs[2]++;
}

static void reset_counters(void)
{
    ready_runs = 0;
    blocked_runs = 0;
    fallback_runs = 0;
    suspended_runs = 0;
    queue_runs[0] = 0;
    queue_runs[1] = 0;
    queue_runs[2] = 0;
}

static void test_ready_task_execution(void)
{
    reset_counters();
    rtos_init();

    assert(rtos_create_task("Ready Task", 1, ready_task) > 0);
    assert(rtos_ready_count() == 1);

    rtos_run(1);

    assert(ready_runs == 1);
    assert(rtos_ready_count() == 1);
    puts("PASS: READY task execution");
}

static void test_blocked_task_skipping(void)
{
    reset_counters();
    rtos_init();

    assert(rtos_create_task("Blocking Task", 2, blocking_task) > 0);
    assert(rtos_create_task("Fallback Task", 1, fallback_task) > 0);
    assert(rtos_ready_count() == 2);

    rtos_run(2);

    assert(blocked_runs == 1);
    assert(fallback_runs == 1);
    puts("PASS: BLOCKED task skipping");
}

static void test_suspended_task_skipping(void)
{
    int suspended_id;

    reset_counters();
    rtos_init();

    suspended_id = rtos_create_task("Suspended Task", 3, suspended_task);
    assert(suspended_id > 0);
    assert(rtos_create_task("Fallback Task", 1, fallback_task) > 0);
    assert(rtos_suspend_task(suspended_id));
    assert(rtos_get_task_state(suspended_id) == TASK_SUSPENDED);
    assert(rtos_ready_count() == 1);

    rtos_run(1);

    assert(suspended_runs == 0);
    assert(fallback_runs == 1);
    puts("PASS: SUSPENDED task skipping");
}

static void test_multiple_ready_tasks_in_queue(void)
{
    reset_counters();
    rtos_init();

    assert(rtos_create_task("Queue Task 0", 1, queue_task_0) > 0);
    assert(rtos_create_task("Queue Task 1", 1, queue_task_1) > 0);
    assert(rtos_create_task("Queue Task 2", 1, queue_task_2) > 0);
    assert(rtos_ready_count() == 3);

    rtos_run(3);

    assert(queue_runs[0] == 1);
    assert(queue_runs[1] == 1);
    assert(queue_runs[2] == 1);
    assert(rtos_ready_count() == 3);
    puts("PASS: multiple READY tasks in queue");
}

int main(void)
{
    test_ready_task_execution();
    test_blocked_task_skipping();
    test_suspended_task_skipping();
    test_multiple_ready_tasks_in_queue();

    puts("All v1.1 task state tests passed");
    return 0;
}
