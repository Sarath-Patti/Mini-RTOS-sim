#include "context.h"
#include "rtos.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static int ready_runs;
static int blocked_runs;
static int fallback_runs;
static int suspended_runs;
static int queue_runs[3];
static CPUContext *context_runs[3];

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

static void context_task_0(void)
{
    context_runs[0] = &rtos_current_task()->context;
}

static void context_task_1(void)
{
    context_runs[1] = &rtos_current_task()->context;
}

static void context_task_2(void)
{
    context_runs[2] = &rtos_current_task()->context;
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
    context_runs[0] = NULL;
    context_runs[1] = NULL;
    context_runs[2] = NULL;
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

static bool ranges_overlap(uintptr_t start_a, uintptr_t end_a, uintptr_t start_b, uintptr_t end_b)
{
    return start_a < end_b && start_b < end_a;
}

static void test_task_stack_allocation(void)
{
    int task_a;
    int task_b;
    int task_c;
    const RtosStackWord *base_a;
    const RtosStackWord *base_b;
    const RtosStackWord *base_c;
    const RtosStackWord *sp_a;
    const RtosStackWord *sp_b;
    const RtosStackWord *sp_c;
    size_t stack_words;
    uintptr_t start_a;
    uintptr_t end_a;
    uintptr_t start_b;
    uintptr_t end_b;
    uintptr_t start_c;
    uintptr_t end_c;

    reset_counters();
    rtos_init();

    task_a = rtos_create_task("Stack Task A", 1, ready_task);
    task_b = rtos_create_task("Stack Task B", 1, ready_task);
    task_c = rtos_create_task("Stack Task C", 1, ready_task);

    assert(task_a > 0);
    assert(task_b > 0);
    assert(task_c > 0);

    base_a = rtos_get_task_stack_base(task_a);
    base_b = rtos_get_task_stack_base(task_b);
    base_c = rtos_get_task_stack_base(task_c);
    sp_a = rtos_get_task_stack_pointer(task_a);
    sp_b = rtos_get_task_stack_pointer(task_b);
    sp_c = rtos_get_task_stack_pointer(task_c);
    stack_words = RTOS_STACK_SIZE / sizeof(RtosStackWord);

    assert(base_a != NULL);
    assert(base_b != NULL);
    assert(base_c != NULL);
    assert(base_a != base_b);
    assert(base_b != base_c);
    assert(base_a != base_c);

    assert(rtos_get_task_stack_size(task_a) == RTOS_STACK_SIZE);
    assert(rtos_get_task_stack_size(task_b) == RTOS_STACK_SIZE);
    assert(rtos_get_task_stack_size(task_c) == RTOS_STACK_SIZE);

    assert(sp_a == base_a + stack_words);
    assert(sp_b == base_b + stack_words);
    assert(sp_c == base_c + stack_words);

    start_a = (uintptr_t)base_a;
    end_a = start_a + rtos_get_task_stack_size(task_a);
    start_b = (uintptr_t)base_b;
    end_b = start_b + rtos_get_task_stack_size(task_b);
    start_c = (uintptr_t)base_c;
    end_c = start_c + rtos_get_task_stack_size(task_c);

    assert(!ranges_overlap(start_a, end_a, start_b, end_b));
    assert(!ranges_overlap(start_a, end_a, start_c, end_c));
    assert(!ranges_overlap(start_b, end_b, start_c, end_c));

    puts("PASS: private task stack allocation");
}

static void test_task_context_ownership(void)
{
    reset_counters();
    rtos_init();

    assert(rtos_create_task("Context Task 0", 1, context_task_0) > 0);
    assert(rtos_create_task("Context Task 1", 1, context_task_1) > 0);
    assert(rtos_create_task("Context Task 2", 1, context_task_2) > 0);

    rtos_run(3);

    assert(context_runs[0] != NULL);
    assert(context_runs[1] != NULL);
    assert(context_runs[2] != NULL);
    assert(context_runs[0] != context_runs[1]);
    assert(context_runs[0] != context_runs[2]);
    assert(context_runs[1] != context_runs[2]);

    puts("PASS: private task CPU context ownership");
}

static void test_context_save_updates_destination(void)
{
    CPUContext active_seed;
    CPUContext task_a_context;
    CPUContext task_b_context;

    context_init(&active_seed, ready_task);
    context_init(&task_a_context, queue_task_0);
    context_init(&task_b_context, queue_task_1);

    active_seed.r0 = 0x11u;
    active_seed.r7 = 0x77u;
    active_seed.r12 = 0xCCu;
    active_seed.lr = 0xABCD1234u;

    task_b_context.r0 = 0x2222u;

    context_restore(&active_seed);
    context_save(&task_a_context);

    assert(task_a_context.r0 == 0x11u);
    assert(task_a_context.r7 == 0x77u);
    assert(task_a_context.r12 == 0xCCu);
    assert(task_a_context.lr == 0xABCD1234u);
    assert(task_b_context.r0 == 0x2222u);

    puts("PASS: context save updates only destination");
}

static void test_context_restore_expected_registers(void)
{
    CPUContext expected;
    const CPUContext *active;

    context_init(&expected, blocking_task);
    expected.r0 = 1u;
    expected.r1 = 2u;
    expected.r2 = 3u;
    expected.r12 = 12u;
    expected.lr = 0xFFFFFFFDu;
    expected.xpsr = 0x01000000u;

    context_restore(&expected);
    active = context_active();

    assert(active->r0 == expected.r0);
    assert(active->r1 == expected.r1);
    assert(active->r2 == expected.r2);
    assert(active->r12 == expected.r12);
    assert(active->lr == expected.lr);
    assert(active->pc == expected.pc);
    assert(active->xpsr == expected.xpsr);

    puts("PASS: context restore loads expected registers");
}

static void test_contexts_do_not_overwrite(void)
{
    CPUContext context_a;
    CPUContext context_b;

    context_init(&context_a, queue_task_0);
    context_init(&context_b, queue_task_1);

    context_a.r4 = 0xA4u;
    context_b.r4 = 0xB4u;

    context_restore(&context_a);
    context_save(&context_a);
    context_restore(&context_b);
    context_save(&context_b);

    assert(context_a.r4 == 0xA4u);
    assert(context_b.r4 == 0xB4u);
    assert(context_a.pc != context_b.pc);

    puts("PASS: CPU contexts do not overwrite one another");
}

int main(void)
{
    test_ready_task_execution();
    test_blocked_task_skipping();
    test_suspended_task_skipping();
    test_multiple_ready_tasks_in_queue();
    test_task_stack_allocation();
    test_task_context_ownership();
    test_context_save_updates_destination();
    test_context_restore_expected_registers();
    test_contexts_do_not_overwrite();

    puts("All task state, stack, and context tests passed");
    return 0;
}
