/*
 * memory.c — Fixed-Size Memory Pool Allocator (v1.8)
 *
 * Implementation notes
 * --------------------
 * The pool is a 2-D static array:
 *
 *   g_pool_storage[RTOS_POOL_BLOCK_COUNT][RTOS_POOL_BLOCK_SIZE]
 *
 * Each row is one independently addressable block.  The allocator treats the
 * rows as opaque byte arrays from the caller's perspective but writes an int
 * index into the first sizeof(int) bytes of every FREE block so the free list
 * can be traversed without any separate metadata array.
 *
 * Invariant: g_pool_allocated[i] == 1  ⟺  block i is live.
 *            g_pool_allocated[i] == 0  ⟺  block i is on the free list.
 *
 * The sentinel value POOL_END_OF_LIST (-1) marks the tail of the free list.
 *
 * Thread safety: this simulator is cooperative and single-threaded, so no
 * locking is required.
 */

#include "memory.h"
#include "rtos.h"    /* uart_log */

#include <string.h>  /* memset, memcpy */

/* ------------------------------------------------------------------ */
/* Internal sentinel                                                    */
/* ------------------------------------------------------------------ */

#define POOL_END_OF_LIST (-1)

/* ------------------------------------------------------------------ */
/* Pool storage                                                         */
/* ------------------------------------------------------------------ */

/*
 * g_pool_storage — the pool itself.
 *
 * Declared as a 2-D array of uint8_t so that each block can hold any type
 * when returned to the caller as a void *.  The char-aliasing rule (C99 §6.5)
 * permits reading any object through a char/uint8_t pointer, making this
 * representation portable.
 */
static uint8_t g_pool_storage[RTOS_POOL_BLOCK_COUNT][RTOS_POOL_BLOCK_SIZE];

/*
 * g_pool_allocated — double-free protection bitmap.
 *
 * g_pool_allocated[i] == 0  → block i is free.
 * g_pool_allocated[i] == 1  → block i is allocated.
 *
 * Kept separate from the free-list links so that the link fields in a freed
 * block cannot be confused with live allocation state.
 */
static uint8_t g_pool_allocated[RTOS_POOL_BLOCK_COUNT];

/*
 * g_free_list_head — index of the first free block, or POOL_END_OF_LIST.
 */
static int g_free_list_head;

/*
 * g_available_blocks — count of blocks currently on the free list.
 */
static int g_available_blocks;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * block_index() — compute the pool index for a pointer returned by
 *                 memory_alloc(), or -1 if ptr is not a valid block start.
 */
static int block_index(const void *ptr)
{
    /*
     * Cast to a uint8_t pointer so that pointer arithmetic is in bytes.
     * The pool base is &g_pool_storage[0][0].
     */
    const uint8_t *base  = &g_pool_storage[0][0];
    const uint8_t *block = (const uint8_t *)ptr;

    if (block < base) {
        return -1;
    }

    ptrdiff_t offset = block - base;

    /* Must be exactly aligned to a block boundary */
    if (offset % RTOS_POOL_BLOCK_SIZE != 0) {
        return -1;
    }

    int idx = (int)(offset / RTOS_POOL_BLOCK_SIZE);

    if (idx < 0 || idx >= (int)RTOS_POOL_BLOCK_COUNT) {
        return -1;
    }

    return idx;
}

/*
 * read_next_link() / write_next_link() — read/write the free-list link stored
 * in the first sizeof(int) bytes of a free block.
 *
 * Using memcpy for the int read/write avoids strict-aliasing violations that
 * would occur from a direct cast (e.g., *(int *)block_ptr).
 */
static int read_next_link(int idx)
{
    int val;
    memcpy(&val, g_pool_storage[idx], sizeof(int));
    return val;
}

static void write_next_link(int idx, int next)
{
    memcpy(g_pool_storage[idx], &next, sizeof(int));
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void memory_init(void)
{
    /*
     * Build the free list: block 0 → block 1 → … → block N-1 → END.
     * Clear the allocation bitmap.
     */
    for (int i = 0; i < (int)RTOS_POOL_BLOCK_COUNT; i++) {
        int next = (i + 1 < (int)RTOS_POOL_BLOCK_COUNT) ? (i + 1) : POOL_END_OF_LIST;
        write_next_link(i, next);
        g_pool_allocated[i] = 0;
    }

    g_free_list_head  = 0;
    g_available_blocks = (int)RTOS_POOL_BLOCK_COUNT;

    uart_log("Memory Pool Init: %d blocks x %d bytes = %d bytes total",
             (int)RTOS_POOL_BLOCK_COUNT,
             (int)RTOS_POOL_BLOCK_SIZE,
             (int)(RTOS_POOL_BLOCK_COUNT * RTOS_POOL_BLOCK_SIZE));
}

void *memory_alloc(void)
{
    if (g_free_list_head == POOL_END_OF_LIST) {
        uart_log("Memory Alloc FAILED: pool exhausted (0/%d blocks free)",
                 (int)RTOS_POOL_BLOCK_COUNT);
        return NULL;
    }

    /* Pop the head of the free list */
    int  idx      = g_free_list_head;
    int  next     = read_next_link(idx);
    g_free_list_head  = next;
    g_pool_allocated[idx] = 1;
    g_available_blocks--;

    void *ptr = (void *)g_pool_storage[idx];

    uart_log("Memory Alloc: block %d @ %p (%d/%d blocks free)",
             idx, ptr, g_available_blocks, (int)RTOS_POOL_BLOCK_COUNT);

    return ptr;
}

void memory_free(void *ptr)
{
    /* Reject NULL */
    if (ptr == NULL) {
        uart_log("Memory Free IGNORED: NULL pointer");
        return;
    }

    /* Validate that ptr is the start of a pool block */
    int idx = block_index(ptr);

    if (idx < 0) {
        uart_log("Memory Free ERROR: %p is not a valid pool block", ptr);
        return;
    }

    /* Double-free protection */
    if (g_pool_allocated[idx] == 0) {
        uart_log("Memory Free ERROR: double-free detected on block %d @ %p",
                 idx, ptr);
        return;
    }

    /* Push the block onto the head of the free list */
    write_next_link(idx, g_free_list_head);
    g_free_list_head       = idx;
    g_pool_allocated[idx]  = 0;
    g_available_blocks++;

    uart_log("Memory Free: block %d @ %p (%d/%d blocks free)",
             idx, ptr, g_available_blocks, (int)RTOS_POOL_BLOCK_COUNT);
}

int memory_available_blocks(void)
{
    return g_available_blocks;
}
