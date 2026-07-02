#ifndef MINI_RTOS_CONTEXT_H
#define MINI_RTOS_CONTEXT_H

#include <stdint.h>

typedef struct {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r4;
    uint32_t r5;
    uint32_t r6;
    uint32_t r7;
    uint32_t r8;
    uint32_t r9;
    uint32_t r10;
    uint32_t r11;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
} CPUContext;

void context_init(CPUContext *context, void (*entry_point)(void));
void context_save(CPUContext *destination);
void context_restore(const CPUContext *source);
void context_copy(CPUContext *destination, const CPUContext *source);
const CPUContext *context_active(void);

#endif
