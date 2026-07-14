/*
 * memory.h — Fixed-Size Memory Pool Allocator (v1.8)
 *
 * Overview
 * --------
 * This module provides a statically allocated, fixed-size memory pool for
 * deterministic, fragmentation-free dynamic allocation within the Mini RTOS
 * simulator.  It is designed to serve as a drop-in replacement for heap-based
 * dynamic allocation (malloc/free) in embedded contexts where:
 *
 *   - Allocation latency must be bounded and predictable (O(1)).
 *   - Memory fragmentation must be eliminated.
 *   - All memory must be visible to a static analyser at link time.
 *
 * Allocation Strategy — Embedded Free List
 * -----------------------------------------
 * The pool consists of RTOS_POOL_BLOCK_COUNT blocks, each of exactly
 * RTOS_POOL_BLOCK_SIZE bytes, laid out in a single contiguous static array:
 *
 *   g_pool_storage[RTOS_POOL_BLOCK_COUNT][RTOS_POOL_BLOCK_SIZE]
 *
 * Free blocks are chained via an embedded singly-linked free list.  The first
 * sizeof(int) bytes of every free block hold the index of the next free block
 * (or POOL_END_OF_LIST when it is the last free block).  No separate metadata
 * array is required for list traversal — the list is intrusive.
 *
 * A parallel uint8_t bitmap (g_pool_allocated[]) records whether each block is
 * currently allocated.  This bitmap is used exclusively for double-free
 * protection; it does not participate in the allocation path.
 *
 * Allocation:   Pop the head of the free list.  O(1).
 * Deallocation: Validate via bitmap, then push the block back onto the head.
 *               O(1).
 *
 * Public API
 * ----------
 *   memory_init()              Initialise the pool.  Called from rtos_init().
 *   memory_alloc()             Allocate one block; returns NULL on exhaustion.
 *   memory_free(ptr)           Return one block to the pool.
 *   memory_available_blocks()  Query the number of free blocks remaining.
 *
 * Configuration
 * -------------
 *   RTOS_POOL_BLOCK_SIZE    Bytes per block  (default: 32)
 *   RTOS_POOL_BLOCK_COUNT   Number of blocks (default: 16)
 *
 * Advantages over heap allocation
 * ---------------------------------
 *   - Zero fragmentation: every block is the same size.
 *   - Deterministic O(1) alloc/free — no coalesce, no best-fit scan.
 *   - All memory is statically visible; no hidden OS heap growth.
 *   - Double-free detection with a dedicated bitmap guard.
 *   - Safe for use inside ISRs (no system calls, no blocking).
 *
 * Module boundary
 * ---------------
 *   memory.c does not call into scheduler.c, timer.c, or context.c.
 *   It depends only on rtos.h (for uart_log) and standard headers.
 */

#ifndef MINI_RTOS_MEMORY_H
#define MINI_RTOS_MEMORY_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */

/*
 * RTOS_POOL_BLOCK_SIZE — size in bytes of each fixed-size pool block.
 *
 * Must be >= sizeof(int) so the free-list link fits inside a free block.
 * Increase to the largest allocation unit your application uses.
 */
#ifndef RTOS_POOL_BLOCK_SIZE
#define RTOS_POOL_BLOCK_SIZE  32u
#endif

/*
 * RTOS_POOL_BLOCK_COUNT — total number of blocks in the pool.
 *
 * Determines the maximum number of concurrent live allocations.
 * Total pool RAM = RTOS_POOL_BLOCK_SIZE * RTOS_POOL_BLOCK_COUNT bytes.
 */
#ifndef RTOS_POOL_BLOCK_COUNT
#define RTOS_POOL_BLOCK_COUNT 16u
#endif

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/*
 * memory_init() — initialise the memory pool.
 *
 * Builds the embedded free list and clears the allocation bitmap.
 * Must be called once from rtos_init() before any memory_alloc() call.
 * Safe to call again to reset the pool (e.g., between test cases).
 */
void memory_init(void);

/*
 * memory_alloc() — allocate one block from the pool.
 *
 * Pops the head of the free list in O(1) time.
 *
 * @return  Pointer to the allocated block (RTOS_POOL_BLOCK_SIZE bytes),
 *          or NULL if the pool is exhausted.
 */
void *memory_alloc(void);

/*
 * memory_free() — return a block to the pool.
 *
 * Pushes the block back onto the head of the free list in O(1) time.
 * Logs an error and returns without action if:
 *   - ptr is NULL.
 *   - ptr does not point to the start of a valid pool block.
 *   - ptr points to a block that is not currently allocated (double-free).
 *
 * @param ptr  Pointer previously returned by memory_alloc().
 */
void memory_free(void *ptr);

/*
 * memory_available_blocks() — query the number of free blocks.
 *
 * @return  Count of blocks currently on the free list.
 *          Equals RTOS_POOL_BLOCK_COUNT immediately after memory_init().
 */
int memory_available_blocks(void);

#endif /* MINI_RTOS_MEMORY_H */
