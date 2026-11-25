#ifndef MIGA_XALLOC_H
#define MIGA_XALLOC_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>

#include "miga/api.h"
#include "miga/migaconf.h"
#include "miga/mutex.h"

MIGA_EXTERN_C_START

// #define MIGA_ARENA_DEBUG
#ifdef MIGA_ARENA_DEBUG
/**
 * Structure to track allocation information in MIGA_ARENA_DEBUG mode.
 */
#define MIGA_ARENA_DEBUG_FILENAME_MAX_LEN 256
typedef struct
{
    void *ptr;
    char file[MIGA_ARENA_DEBUG_FILENAME_MAX_LEN];
    int line;
    size_t size;
} miga_arena_alloc_t;
#else
typedef struct
{
    void *ptr;
#ifdef MIGA_ARENA_STATS
    size_t size; // requested size, needed to keep live_bytes exact
#endif
} miga_arena_alloc_t;
#endif

#ifdef MIGA_ARENA_STATS
/**
 * Number of allocation size classes in miga_arena_stats_t.size_classes.
 * Class 0 counts blocks of 1..16 bytes, class k (k >= 1) counts blocks of
 * (2^(k+3), 2^(k+4)] bytes, and the last class counts everything larger.
 */
#define MIGA_ARENA_STATS_SIZE_CLASSES 16

/**
 * Memory-usage statistics kept by the arena when MIGA_ARENA_STATS is defined
 * (configure with -DENABLE_ARENA_STATS=ON or --enable-arena-stats).
 *
 * All sizes are the sizes requested by the caller, not what the C library
 * actually reserves. The arena's own bookkeeping array is not counted.
 */
typedef struct miga_arena_stats_t
{
    /* Current state */
    size_t live_blocks; // blocks allocated and not yet freed
    size_t live_bytes;  // bytes in those blocks

    /* High-water marks since the last miga_arena_stats_reset_peak() */
    size_t peak_blocks;
    size_t peak_bytes;

    /* Cumulative counters since the last miga_arena_stats_clear() */
    size_t total_allocs;          // xmalloc, xcalloc, xstrdup (and xrealloc from NULL)
    size_t total_reallocs;        // xrealloc of an existing block
    size_t total_frees;           // xfree (and xrealloc to size 0)
    size_t total_bytes_allocated; // sum of all allocation and reallocation sizes
    size_t largest_block;         // largest single allocation or reallocation
    size_t size_classes[MIGA_ARENA_STATS_SIZE_CLASSES]; // allocations + reallocations by size

    /* Blocks still live when the arena was last reset (miga_arena_reset(),
     * miga_arena_end(), or an out-of-memory rollback), counted after the
     * cleanup callback ran. Blocks that nothing freed before teardown are
     * leaks, so in a shell that cleans up after itself these should be 0. */
    size_t teardown_blocks;
    size_t teardown_bytes;
    size_t resets; // number of resets that found the arena initialized
} miga_arena_stats_t;

/** Callback for miga_arena_stats_foreach_live(). */
typedef void (*miga_arena_live_block_fn)(const void *ptr, size_t size, void *user_data);
#endif /* MIGA_ARENA_STATS */

typedef void (*miga_arena_resource_cleanup_fn)(void *user_data);

/**
 * Arena allocator state structure.
 * Encapsulates all the state needed for memory tracking.
 */
typedef struct miga_arena_t
{
    jmp_buf rollback_point;
    bool rollback_in_progress;
    bool initialized;       // true after arena_init_ex has been called
    bool setjmp_set;        // true after miga_setjmp() has been called
    bool mutex_initialized; // true after miga_mutex_init has been called on mtx
    char reserved[4];
    miga_mutex_t mtx;
    miga_arena_alloc_t *allocated_ptrs; // dynamically resized sorted array
    long allocated_count;
    long allocated_cap;
    long initial_cap;     // initial capacity for allocated_ptrs array
    long max_allocations; // maximum number of allocations allowed
    miga_arena_resource_cleanup_fn resource_cleanup;
    void *resource_cleanup_user_data;
#ifdef MIGA_ARENA_STATS
    miga_arena_stats_t stats;
#endif
} miga_arena_t;

// Access to global singleton arena for use in miga_setjmp() macro
MIGA_API extern miga_arena_t *miga_arena_get_global(void);

/**
 * Place this macro early in startup. Memory failures will cause the code to
 * jump to this location. Calls miga_arena_init() automatically if needed.
 */
#define miga_setjmp()                                                                              \
    do                                                                                             \
    {                                                                                              \
        if (!miga_arena_get_global()->initialized)                                                 \
            miga_arena_init();                                                                     \
        miga_arena_get_global()->setjmp_set = true;                                                \
        if (setjmp(miga_arena_get_global()->rollback_point) != 0)                                  \
        {                                                                                          \
            miga_arena_reset();                                                                    \
            fprintf(stderr,                                                                        \
                    "Out of memory — all allocated memory has been freed, restarting logic...\n"); \
        }                                                                                          \
    } while (0)


/**
 * Initialize the global arena allocator.
 * Optional — the arena will lazy-initialize on first use if this is not called.
 * Calling this explicitly allows a clean reset of all state.
 */
MIGA_API void miga_arena_init(void);

/**
 * This sets a function that will be called upon an out-of-memory
 * condition. It is called just before the memory allocator
 * frees all the memory it is tracking and then either jumps to
 * the location of the miga_setjmp call or exits.
 */
MIGA_API void miga_set_out_of_memory_cleanup_cb(miga_arena_resource_cleanup_fn fn, void *user_data);

/**
 * Free all allocated memory tracked by the arena and invoke the cleanup
 * callback.  The setjmp rollback point and cleanup callback are preserved,
 * so the arena can be reused immediately (it will lazy-reinitialize on the
 * next allocation).  This is called automatically on out-of-memory conditions.
 */
MIGA_API void miga_arena_reset(void);

/**
 * Full teardown.  Like miga_arena_reset(), but also clears the setjmp
 * rollback point and cleanup callback, returning the arena to its completely
 * dormant initial state.  Call this at program exit or when the library is
 * being unloaded.
 */
MIGA_API void miga_arena_end(void);

#ifdef MIGA_ARENA_STATS
/**
 * Copy the global arena's statistics into *out.
 */
MIGA_API void miga_arena_stats_get(miga_arena_stats_t *out);

/**
 * Set the peak counters to the current live counters, so that the next
 * miga_arena_stats_get() reports the high-water mark of the code run since.
 */
MIGA_API void miga_arena_stats_reset_peak(void);

/**
 * Zero the cumulative counters, the size classes and the teardown counters,
 * and reset the peaks. The live counters are kept, since those blocks are
 * still allocated.
 */
MIGA_API void miga_arena_stats_clear(void);

/**
 * Call fn once for each block currently allocated from the global arena,
 * in address order. fn must not allocate from or free to the arena.
 */
MIGA_API void miga_arena_stats_foreach_live(miga_arena_live_block_fn fn, void *user_data);

/**
 * Write a human-readable report of the global arena's statistics to out.
 */
MIGA_API void miga_arena_stats_print(FILE *out);
#endif /* MIGA_ARENA_STATS */

#ifndef MIGA_HIDE_LOCALS    
/**
 * Allocate memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
#ifdef MIGA_ARENA_DEBUG
#define xmalloc(size) arena_xmalloc(miga_arena_get_global(), (size), __FILE__, __LINE__)
#else
MIGA_LOCAL void *xmalloc(size_t size);
#endif

/**
 * Allocate zero-initialized memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
#ifdef MIGA_ARENA_DEBUG
#define xcalloc(n, size) arena_xcalloc(miga_arena_get_global(), (n), (size), __FILE__, __LINE__)
#else
MIGA_LOCAL void *xcalloc(size_t n, size_t size);
#endif

/**
 * Reallocate memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 * If old_ptr is NULL, behaves like xmalloc.
 * If new_size is 0, behaves like xfree.
 */
#ifdef MIGA_ARENA_DEBUG
#define xrealloc(old_ptr, new_size)                                                                \
    arena_xrealloc(miga_arena_get_global(), (old_ptr), (new_size), __FILE__, __LINE__)
#else
MIGA_LOCAL void *xrealloc(void *old_ptr, size_t new_size);
#endif

/**
 * Duplicate a string, with memory tracked by the arena.
 * On allocation failure, triggers a longjmp to the arena rollback point.
 */
#ifdef MIGA_ARENA_DEBUG
#define xstrdup(s) arena_xstrdup(miga_arena_get_global(), (s), __FILE__, __LINE__)
#else
MIGA_LOCAL char *xstrdup(const char *s);
#endif

/**
 * Free memory allocated by the arena.
 * Safe to call with NULL.
 */
#ifdef MIGA_ARENA_DEBUG
#define xfree(ptr) arena_xfree(miga_arena_get_global(), (ptr), __FILE__, __LINE__)
#else
MIGA_LOCAL void xfree(void *ptr);
#endif

/**
 * Arena-based allocation functions that operate on an explicit miga_arena_t.
 *
 * These functions provide the same functionality as the global arena functions
 * (xmalloc, xcalloc, etc.) but operate on a specific arena instance, allowing
 * multiple independent arenas and better testability.
 *
 * Note: These are primarily provided for unit testing. General code should use
 * the shorter API like xmalloc and xfree which operate on the global arena.
 */
#ifdef MIGA_ARENA_DEBUG
#define MIGA_ARENA_DEBUG_PARAMS , const char *file, int line
#else
#define MIGA_ARENA_DEBUG_PARAMS
#endif

MIGA_LOCAL void *arena_xmalloc(miga_arena_t *arena, size_t size MIGA_ARENA_DEBUG_PARAMS);
MIGA_LOCAL void *arena_xcalloc(miga_arena_t *arena, size_t n, size_t size MIGA_ARENA_DEBUG_PARAMS);
MIGA_LOCAL void *arena_xrealloc(miga_arena_t *arena, void *old_ptr, size_t new_size MIGA_ARENA_DEBUG_PARAMS);
MIGA_LOCAL char *arena_xstrdup(miga_arena_t *arena, const char *s MIGA_ARENA_DEBUG_PARAMS);
MIGA_LOCAL void arena_xfree(miga_arena_t *arena, void *p MIGA_ARENA_DEBUG_PARAMS);

MIGA_LOCAL void arena_init_ex(miga_arena_t *arena);
MIGA_LOCAL void arena_set_cleanup_ex(miga_arena_t *arena, miga_arena_resource_cleanup_fn fn, void *user_data);
MIGA_LOCAL void arena_reset_ex(miga_arena_t *arena);
MIGA_LOCAL void arena_end_ex(miga_arena_t *arena);
#ifdef MIGA_ARENA_STATS
MIGA_LOCAL void arena_stats_get_ex(miga_arena_t *arena, miga_arena_stats_t *out);
MIGA_LOCAL void arena_stats_reset_peak_ex(miga_arena_t *arena);
MIGA_LOCAL void arena_stats_clear_ex(miga_arena_t *arena);
MIGA_LOCAL void arena_stats_foreach_live_ex(miga_arena_t *arena, miga_arena_live_block_fn fn,
                                            void *user_data);
MIGA_LOCAL void arena_stats_print_ex(miga_arena_t *arena, FILE *out);
#endif
#endif /* MIGA_HIDE_LOCALS */

MIGA_EXTERN_C_END
#endif // MIGA_XALLOC_H
