/*
 * context.h — Software CPU Context Model
 *
 * This header defines the CPUContext register file used by the Mini RTOS
 * simulator to model Cortex-M task contexts in portable C99.
 *
 * CPUContext layout
 * -----------------
 * The struct mirrors the Cortex-M exception frame and callee-saved register
 * set.  Field names match ARM Architecture Reference Manual conventions:
 *
 *   R0–R12    General-purpose registers
 *   LR        Link register (EXC_RETURN value: 0xFFFFFFFD in the simulator)
 *   PC        Program counter (function pointer of the task entry point)
 *   xPSR      Program Status Register (Thumb bit set: 0x01000000)
 *
 * Active context
 * --------------
 * context.c maintains a single module-private `active_context` struct that
 * represents the currently executing task's register state.
 * context_save() snapshots it into a TCB-owned CPUContext.
 * context_restore() loads a TCB-owned CPUContext back into it.
 * context_switch() performs both operations atomically.
 *
 * Public API
 * ----------
 *   context_init()     Initialise a context with entry-point PC and
 *                      standard LR / xPSR values.
 *   context_save()     Snapshot active_context into *destination.
 *   context_restore()  Load *source into active_context.
 *   context_copy()     Copy one CPUContext into another.
 *   context_active()   Return a read-only pointer to active_context.
 *
 * Internal scheduler API (do not call from outside scheduler.c)
 * -------------------------------------------------------------
 *   context_switch()   Atomically save outgoing and restore incoming.
 */

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

/* Public context API */
void context_init(CPUContext *context, void (*entry_point)(void));
void context_save(CPUContext *destination);
void context_restore(const CPUContext *source);
void context_copy(CPUContext *destination, const CPUContext *source);
const CPUContext *context_active(void);

/*
 * Internal scheduler API — do not call from outside scheduler.c.
 *
 * context_switch() atomically saves the CPU context of the outgoing task
 * into *outgoing and loads the CPU context from *incoming into the active
 * context register file.  Both pointers must be non-NULL.
 */
void context_switch(CPUContext *outgoing, const CPUContext *incoming);

#endif
