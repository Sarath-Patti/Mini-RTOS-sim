#include "context.h"

#include <stddef.h>

#define CONTEXT_INITIAL_XPSR 0x01000000u
#define CONTEXT_INITIAL_LR 0xFFFFFFFDu

static CPUContext active_context;

static void context_clear(CPUContext *context)
{
    context->r0 = 0;
    context->r1 = 0;
    context->r2 = 0;
    context->r3 = 0;
    context->r4 = 0;
    context->r5 = 0;
    context->r6 = 0;
    context->r7 = 0;
    context->r8 = 0;
    context->r9 = 0;
    context->r10 = 0;
    context->r11 = 0;
    context->r12 = 0;
    context->lr = 0;
    context->pc = 0;
    context->xpsr = 0;
}

void context_init(CPUContext *context, void (*entry_point)(void))
{
    if (context == NULL) {
        return;
    }

    context_clear(context);
    context->lr = CONTEXT_INITIAL_LR;
    context->pc = (uint32_t)(uintptr_t)entry_point;
    context->xpsr = CONTEXT_INITIAL_XPSR;
}

void context_save(CPUContext *destination)
{
    if (destination == NULL) {
        return;
    }

    context_copy(destination, &active_context);
}

void context_restore(const CPUContext *source)
{
    if (source == NULL) {
        return;
    }

    context_copy(&active_context, source);
}

void context_copy(CPUContext *destination, const CPUContext *source)
{
    if (destination == NULL || source == NULL) {
        return;
    }

    *destination = *source;
}

const CPUContext *context_active(void)
{
    return &active_context;
}
