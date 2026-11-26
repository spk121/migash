#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miga/xalloc.h"

// Default arena configuration constants
#define ARENA_INITIAL_CAP 64
#define ARENA_MAX_ALLOCATIONS 1000000

// Forward declarations (needed by ensure_initialized / arena_oom_bail)
static void ensure_mutex(miga_arena_t *arena);
static void ensure_initialized(miga_arena_t *arena);
static void arena_oom_bail(miga_arena_t *arena);

// Global singleton arena instance
static miga_arena_t global_arena = {.rollback_in_progress = false,
                               .initialized = false,
                               .setjmp_set = false,
                               .mutex_initialized = false,
                               .allocated_ptrs = NULL,
                               .allocated_count = 0,
                               .allocated_cap = 0,
                               .initial_cap = ARENA_INITIAL_CAP,
                               .max_allocations = ARENA_MAX_ALLOCATIONS};

// Provide access to global arena for miga_setjmp() macro
miga_arena_t *miga_arena_get_global(void)
{
    return &global_arena;
}

// -------------------------------------------------------------
// Internal helpers for maintaining the sorted pointer list
// -------------------------------------------------------------
#ifdef MIGA_ARENA_DEBUG
// Helper function to extract basename from file path (returns pointer to basename within the
// original string)
static const char *get_basename(const char *file)
{
    const char *basename = file;
    const char *p = file;
    while (*p)
    {
        if (*p == '/' || *p == '\\')
        {
            basename = p + 1;
        }
        p++;
    }
    return basename;
}

static void strncpy_t(char *s1, const char *s2, int n)
{
    if (!s1 || !s2 || n <= 0)
        return;
    if (n == 1)
    {
        *s1 = '\0';
        return;
    }
    if (s1 == s2)
    {
        s1[n - 1] = '\0';
        return;
    }
    strncpy(s1, s2, n - 1);
    s1[n - 1] = '\0';
}

static int ptr_compare(const void *a, const void *b)
{
    const miga_arena_alloc_t *pa = (const miga_arena_alloc_t *)a;
    const miga_arena_alloc_t *pb = (const miga_arena_alloc_t *)b;
    return (pa->ptr > pb->ptr) - (pa->ptr < pb->ptr);
}
#else
static int ptr_compare(const void *a, const void *b)
{
    const void *pa = *(const void **)a;
    const void *pb = *(const void **)b;
    return (pa > pb) - (pa < pb);
}
#endif

// Find index of pointer p (returns -1 if not found)
static long find_ptr(miga_arena_t *arena, const void *p)
{
    if (!p)
    {
        fprintf(stderr, "find_ptr: NULL pointer passed\n");
        abort();
    }
    // binary search because list is kept sorted
    miga_arena_alloc_t key = {.ptr = (void *)p};
    miga_arena_alloc_t *res = bsearch(&key, arena->allocated_ptrs, arena->allocated_count,
                                 sizeof(miga_arena_alloc_t), ptr_compare);
    if (!res)
        return -1;
    return (long)(res - arena->allocated_ptrs);
}

static long find_insertion_index(miga_arena_t *arena, void *new_ptr)
{
    if (arena->allocated_count == 0)
        return 0;

    long low = 0;
    long high = arena->allocated_count;

    while (low < high)
    {
        long mid = (low + high) / 2;
        if (new_ptr < arena->allocated_ptrs[mid].ptr)
        {
            high = mid;
        }
        else
        {
            low = mid + 1;
        }
    }
    return low;
}

// -------------------------------------------------------------
// Memory-usage statistics (MIGA_ARENA_STATS)
// -------------------------------------------------------------
#ifdef MIGA_ARENA_STATS
// Class 0: 1..16 bytes; class k: (2^(k+3), 2^(k+4)]; last class: larger.
static int stats_size_class(size_t size)
{
    int k = 0;
    size_t limit = 16;
    while (size > limit && k < MIGA_ARENA_STATS_SIZE_CLASSES - 1)
    {
        limit *= 2;
        k++;
    }
    return k;
}

// A block of `size` bytes became live (allocation or reallocation result).
static void stats_add_block(miga_arena_t *arena, size_t size)
{
    miga_arena_stats_t *st = &arena->stats;
    st->live_blocks++;
    st->live_bytes += size;
    if (st->live_blocks > st->peak_blocks)
        st->peak_blocks = st->live_blocks;
    if (st->live_bytes > st->peak_bytes)
        st->peak_bytes = st->live_bytes;
    st->total_bytes_allocated += size;
    if (size > st->largest_block)
        st->largest_block = size;
    st->size_classes[stats_size_class(size)]++;
}

// A tracked block of `size` bytes stopped being live.
static void stats_remove_block(miga_arena_t *arena, size_t size)
{
    miga_arena_stats_t *st = &arena->stats;
    st->live_blocks--;
    st->live_bytes -= size;
}
#endif

// -------------------------------------------------------------
// Lazy initialization and safe OOM handling
// -------------------------------------------------------------

/**
 * Ensure the arena's mutex is initialized.  Separate from arena init because
 * the mutex must survive across reset/re-init cycles.  Only arena_end_ex
 * destroys it.
 *
 * Note: This is inherently racy if two threads both hit an uninitialized arena
 * at the same instant.  In practice the very first call into the library
 * (whether explicit miga_arena_init() or the first lazy xmalloc) should happen
 * on a single thread during startup.  After that the mutex is live and all
 * subsequent calls are safe.
 */
static void ensure_mutex(miga_arena_t *arena)
{
    if (!arena->mutex_initialized)
    {
        miga_mutex_init(&arena->mtx);
        arena->mutex_initialized = true;
    }
}

/**
 * Ensure the arena is initialized. Called at the top of every allocation
 * function so that library consumers who forget miga_arena_init() get
 * automatic lazy initialization instead of a crash.
 *
 * When called after an OOM reset, this preserves the setjmp_set flag and
 * the cleanup callback — only the allocation bookkeeping is rebuilt.
 *
 * IMPORTANT: The caller must already hold arena->mtx (or call ensure_mutex
 * first).  This function does NOT lock — the caller is responsible.
 */
static void ensure_initialized(miga_arena_t *arena)
{
    if (!arena->initialized)
    {
        // Save fields that should survive a re-init after OOM reset
        bool saved_setjmp = arena->setjmp_set;
        miga_arena_resource_cleanup_fn saved_cleanup = arena->resource_cleanup;
        void *saved_cleanup_data = arena->resource_cleanup_user_data;

        arena_init_ex(arena);

        // Restore preserved fields
        arena->setjmp_set = saved_setjmp;
        arena->resource_cleanup = saved_cleanup;
        arena->resource_cleanup_user_data = saved_cleanup_data;
    }
}

/**
 * Handle an out-of-memory condition safely.
 *
 * IMPORTANT: The caller holds arena->mtx.  This function unlocks it before
 * any non-local exit (longjmp or exit) so the mutex is not left dangling.
 *
 * If the caller previously called miga_setjmp() (setjmp_set == true),
 * we unlock and longjmp back to the saved rollback point.
 *
 * If setjmp was never called (library mode without host cooperation),
 * we run cleanup, free all tracked allocations, unlock, and exit().
 */
static void arena_oom_bail(miga_arena_t *arena)
{
    if (arena->setjmp_set)
    {
        miga_mutex_unlock(&arena->mtx);
        longjmp(arena->rollback_point, 1);
    }
    else
    {
        fprintf(stderr, "Fatal: out of memory (no setjmp rollback point configured)\n");
        arena_reset_ex(arena);
        miga_mutex_unlock(&arena->mtx);
        exit(1);
    }
}

// Insert pointer in sorted order (keeps the array sorted).
// On allocation failure, frees p and does longjmp to rollback point.
// Returns false if the pointer wasn't inserted.
#ifdef MIGA_ARENA_DEBUG
static bool insert_ptr(miga_arena_t *arena, void *p, const char *file, int line, size_t size)
#else
static bool insert_ptr(miga_arena_t *arena, void *p, size_t size)
#endif
{
#if !defined(MIGA_ARENA_STATS) && !defined(MIGA_ARENA_DEBUG)
    (void)size; // only recorded when statistics or debug tracking is on
#endif
    if (!p)
    {
        fprintf(stderr, "insert_ptr: NULL pointer passed\n");
        abort();
    }

#ifdef MIGA_ARENA_DEBUG
    // Check for ANY overlap with existing allocations.
    // Yes, I know this is O(n^2) in the worst case, but this is only debug mode.
    for (long i = 0; i < arena->allocated_count; i++)
    {
        void *old_begin = arena->allocated_ptrs[i].ptr;
        void *old_end = (void *)((char *)old_begin + arena->allocated_ptrs[i].size);
        void *new_begin = p;
        void *new_end = (void *)((char *)p + size);

        if (!(old_end <= new_begin || new_end <= old_begin))
        {
            fprintf(stderr,
                    "SHADOW: existing allocation [%p-%p] %s:%d %zu overlaps new allocation [%p-%p] "
                    "%s:%d %zu\n",
                    old_begin, old_end, arena->allocated_ptrs[i].file,
                    arena->allocated_ptrs[i].line, arena->allocated_ptrs[i].size, new_begin,
                    new_end, get_basename(file), line, size);
            fprintf(stderr,
                    "ERROR: overlapping memory allocations detected - possible heap corruption\n");
            abort();
        }
    }
#endif

    if (arena->allocated_count >= arena->max_allocations)
    {
        fprintf(stderr, "exceeded maximum allocation limit (%ld)\n", arena->max_allocations);
        if (!arena->rollback_in_progress)
            arena_oom_bail(arena);
        return false;
    }

    // Note that we can't use xrealloc here because it would call insert_ptr again.
    if (arena->allocated_count == arena->allocated_cap)
    {
        long new_cap = (arena->allocated_cap > 0) ? arena->allocated_cap * 2 : arena->initial_cap;
        if (new_cap > arena->max_allocations)
            new_cap = arena->max_allocations;
        miga_arena_alloc_t *new_ptrs = calloc(new_cap, sizeof(miga_arena_alloc_t));
        if (!new_ptrs)
        {
            if (!arena->rollback_in_progress)
                arena_oom_bail(arena);
            return false;
        }
        memmove(new_ptrs, arena->allocated_ptrs, arena->allocated_count * sizeof(miga_arena_alloc_t));
        free(arena->allocated_ptrs);
        arena->allocated_ptrs = new_ptrs;
        arena->allocated_cap = new_cap;
    }

    // Find insertion position with binary search
    long idx = find_insertion_index(arena, p);

    // Shift everything right one position
    if (idx < arena->allocated_count)
    {
        memmove(arena->allocated_ptrs + idx + 1, arena->allocated_ptrs + idx,
                (arena->allocated_count - idx) * sizeof(miga_arena_alloc_t));
    }
    arena->allocated_ptrs[idx].ptr = p;
#if defined(MIGA_ARENA_STATS) && !defined(MIGA_ARENA_DEBUG)
    arena->allocated_ptrs[idx].size = size;
#endif
#ifdef MIGA_ARENA_STATS
    stats_add_block(arena, size);
#endif
#ifdef MIGA_ARENA_DEBUG
    strncpy_t(arena->allocated_ptrs[idx].file, get_basename(file), MIGA_ARENA_DEBUG_FILENAME_MAX_LEN);
    arena->allocated_ptrs[idx].line = line;
    arena->allocated_ptrs[idx].size = size;
    fprintf(stderr, "ALLOC: %p %s:%d %zu\n", p, arena->allocated_ptrs[idx].file, line, size);
#endif
    arena->allocated_count++;
    return true;
}

// Remove pointer at index idx (fast removal, preserves order)
static void remove_ptr_at(miga_arena_t *arena, long idx)
{
    if (idx < 0 || idx >= arena->allocated_count)
    {
        fprintf(stderr, "remove_ptr_at: invalid index %ld\n", idx);
        abort();
    }
#ifdef MIGA_ARENA_STATS
    stats_remove_block(arena, arena->allocated_ptrs[idx].size);
#endif
    memmove(arena->allocated_ptrs + idx, arena->allocated_ptrs + idx + 1,
            (arena->allocated_count - idx - 1) * sizeof(miga_arena_alloc_t));
    arena->allocated_count--;
}

// -------------------------------------------------------------
// Arena-based public API
// -------------------------------------------------------------
void *arena_xmalloc(miga_arena_t *arena, size_t size MIGA_ARENA_DEBUG_PARAMS)
{
    if (!arena)
    {
        fprintf(stderr, "arena_xmalloc: NULL pointer passed\n");
        abort();
    }
    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);
    ensure_initialized(arena);
    if (size == 0)
    {
        fprintf(stderr, "arena_xmalloc: invalid argument (size=0)\n");
        abort();
    }

    void *p = malloc(size);
    if (!p)
    {
        if (!arena->rollback_in_progress)
            arena_oom_bail(arena); // unlocks, then longjmp or exit
        miga_mutex_unlock(&arena->mtx);
        return NULL;                           // during cleanup we must not jump again
    }

    bool inserted;
#ifdef MIGA_ARENA_DEBUG
    inserted = insert_ptr(arena, p, file, line, size);
#else
    inserted = insert_ptr(arena, p, size);
#endif
    if (!inserted)
    {
        fprintf(stderr, "%s: %p failed to track allocation\n", __func__, p);
    }
#ifdef MIGA_ARENA_STATS
    else
        arena->stats.total_allocs++;
#endif
    miga_mutex_unlock(&arena->mtx);
    return p;
}

void *arena_xcalloc(miga_arena_t *arena, size_t n, size_t size MIGA_ARENA_DEBUG_PARAMS)
{
    if (!arena)
    {
        fprintf(stderr, "arena_xcalloc: NULL pointer passed\n");
        abort();
    }
    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);
    ensure_initialized(arena);
    if (n == 0 || size == 0)
    {
        fprintf(stderr, "arena_xcalloc: invalid arguments (n=%zu, size=%zu)\n", n, size);
        abort();
    }

    void *p = calloc(n, size);
    if (!p)
    {
        if (!arena->rollback_in_progress)
        {
            arena_oom_bail(arena); // unlocks, then longjmp or exit
        }
        miga_mutex_unlock(&arena->mtx);
        return NULL;
    }

    bool inserted;
#ifdef MIGA_ARENA_DEBUG
    inserted = insert_ptr(arena, p, file, line, n * size);
#else
    inserted = insert_ptr(arena, p, n * size);
#endif
    if (!inserted)
    {
        fprintf(stderr, "%s: %p failed to track allocation\n", __func__, p);
    }
#ifdef MIGA_ARENA_STATS
    else
        arena->stats.total_allocs++;
#endif
    miga_mutex_unlock(&arena->mtx);
    return p;
}

void *arena_xrealloc(miga_arena_t *arena, void *old_ptr, size_t new_size MIGA_ARENA_DEBUG_PARAMS)
{
    if (arena == NULL)
    {
        fprintf(stderr, "arena_xrealloc: NULL pointer passed\n");
        abort();
    }
    // Delegate to xmalloc/xfree for edge cases — they handle their own locking.
    if (old_ptr == NULL)
#ifdef MIGA_ARENA_DEBUG
        return arena_xmalloc(arena, new_size, file, line);
#else
        return arena_xmalloc(arena, new_size);
#endif

    if (new_size == 0)
    {
#ifdef MIGA_ARENA_DEBUG
        arena_xfree(arena, old_ptr, file, line);
#else
        arena_xfree(arena, old_ptr);
#endif
        return NULL;
    }

    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);
    ensure_initialized(arena);

    long idx = find_ptr(arena, old_ptr);
#ifdef MIGA_ARENA_DEBUG
    if (idx >= 0)
    {
        // Print old allocation info for tracked pointer
        fprintf(stderr, "REALLOC: %p %s:%d %zu -> ", arena->allocated_ptrs[idx].ptr,
                arena->allocated_ptrs[idx].file, arena->allocated_ptrs[idx].line,
                arena->allocated_ptrs[idx].size);
    }
    else
    {
        // Pointer was not tracked; still log the reallocation for consistency
        fprintf(stderr, "REALLOC: %p (untracked) -> ", old_ptr);
    }
#endif
    void *p = realloc(old_ptr, new_size);
    if (!p)
    {
        if (!arena->rollback_in_progress)
        {
            arena_oom_bail(arena); // unlocks, then longjmp or exit
        }
        miga_mutex_unlock(&arena->mtx);
        return NULL;
    }
    if (idx >= 0)
        remove_ptr_at(arena, idx);
    bool inserted;
#ifdef MIGA_ARENA_DEBUG
    // Print new allocation info
    fprintf(stderr, "%p %s:%d %zu\n", p, get_basename(file), line, new_size);
    inserted = insert_ptr(arena, p, file, line, new_size);
#else
    inserted = insert_ptr(arena, p, new_size);
#endif
    if (!inserted)
    {
        fprintf(stderr, "%s: %p failed to track allocation\n", __func__, p);
    }
#ifdef MIGA_ARENA_STATS
    else
        arena->stats.total_reallocs++;
#endif
    miga_mutex_unlock(&arena->mtx);
    return p;
}

char *arena_xstrdup(miga_arena_t *arena, const char *s MIGA_ARENA_DEBUG_PARAMS)
{
    if (!arena)
    {
        fprintf(stderr, "arena_xstrdup: NULL arena pointer\n");
        abort();
    }
    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);
    ensure_initialized(arena);
    if (s == NULL)
    {
        fprintf(stderr, "arena_xstrdup: NULL pointer passed\n");
        abort();
    }

    char *p = strdup(s);
    if (!p)
    {
        if (!arena->rollback_in_progress)
            arena_oom_bail(arena); // unlocks, then longjmp or exit
        miga_mutex_unlock(&arena->mtx);
        return NULL;
    }
    bool inserted;
#ifdef MIGA_ARENA_DEBUG
    inserted = insert_ptr(arena, p, file, line, strlen(s) + 1);
#else
    inserted = insert_ptr(arena, p, strlen(s) + 1);
#endif
    if (!inserted)
    {
        fprintf(stderr, "%s: %p failed to track allocation\n", __func__, p);
    }
#ifdef MIGA_ARENA_STATS
    else
        arena->stats.total_allocs++;
#endif
    miga_mutex_unlock(&arena->mtx);
    return p;
}

void arena_xfree(miga_arena_t *arena, void *p MIGA_ARENA_DEBUG_PARAMS)
{
    if (!arena)
    {
        fprintf(stderr, "arena_xfree: NULL arena pointer\n");
        abort();
    }
    if (!p)
        return;
    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);
    ensure_initialized(arena);

#ifdef MIGA_ARENA_DEBUG
    fprintf(stderr, "FREE: %p %s:%d 0 \n", p, get_basename(file), line);
#endif
    long idx = find_ptr(arena, p);
    if (idx < 0)
    {
#ifdef MIGA_ARENA_DEBUG
        fprintf(stderr, "DEALLOC: %p (unknown):0 0 (double free or corruption detected)\n", p);
#endif
        fprintf(stderr, "arena_xfree: double free or corruption detected (%p)\n", p);
        abort();
    }

#ifdef MIGA_ARENA_DEBUG
    fprintf(stderr, "DEALLOC: %p %s:%d %zu -> %p (freed):0 0\n", arena->allocated_ptrs[idx].ptr,
            arena->allocated_ptrs[idx].file, arena->allocated_ptrs[idx].line,
            arena->allocated_ptrs[idx].size, p);
#endif
    remove_ptr_at(arena, idx);
#ifdef MIGA_ARENA_STATS
    arena->stats.total_frees++;
#endif
    free(p);
    miga_mutex_unlock(&arena->mtx);
}

void arena_init_ex(miga_arena_t *arena)
{
    if (!arena)
    {
        fprintf(stderr, "arena_init_ex: NULL arena pointer\n");
        abort();
    }
    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);

    // Free existing allocations if arena was previously initialized and still has allocations
    if (arena->initialized && arena->allocated_ptrs != NULL && arena->allocated_count > 0)
    {
        arena_reset_ex(arena);
    }
    arena->rollback_in_progress = false;
    arena->setjmp_set = false;
    arena->initial_cap = ARENA_INITIAL_CAP;
    arena->max_allocations = ARENA_MAX_ALLOCATIONS;
    arena->resource_cleanup = NULL;
    arena->resource_cleanup_user_data = NULL;
    arena->allocated_ptrs = calloc(arena->initial_cap, sizeof(miga_arena_alloc_t));
    if (!arena->allocated_ptrs)
    {
        fprintf(stderr, "arena_init_ex: failed to allocate initial pointer array\n");
        abort();
    }
    arena->allocated_cap = arena->initial_cap;
    arena->allocated_count = 0;
    arena->initialized = true;
    miga_mutex_unlock(&arena->mtx);
}

void arena_set_cleanup_ex(miga_arena_t *arena, miga_arena_resource_cleanup_fn fn, void *user_data)
{
    if (!arena)
    {
        fprintf(stderr, "arena_set_cleanup_ex: NULL arena pointer\n");
        abort();
    }
    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);
    arena->resource_cleanup = fn;
    arena->resource_cleanup_user_data = user_data;
    miga_mutex_unlock(&arena->mtx);
}

void arena_reset_ex(miga_arena_t *arena)
{
    if (!arena)
    {
        fprintf(stderr, "arena_reset_ex: NULL arena pointer\n");
        abort();
    }
    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);
    if (!arena->initialized)
    {
        miga_mutex_unlock(&arena->mtx);
        return; // nothing to reset
    }

#ifdef MIGA_ARENA_DEBUG
    long count = arena->allocated_count;
#endif
    arena->rollback_in_progress = true;

    // Call cleanup callback BEFORE freeing memory so it can access the structures
    if (arena->resource_cleanup)
    {
        arena->resource_cleanup(arena->resource_cleanup_user_data);
    }

#ifdef MIGA_ARENA_STATS
    // Whatever the cleanup callback did not free is about to be reclaimed
    // wholesale: record it, since those blocks are leaks.
    arena->stats.teardown_blocks = arena->stats.live_blocks;
    arena->stats.teardown_bytes = arena->stats.live_bytes;
    arena->stats.resets++;
#endif

    // Free from the end to avoid expensive memmove on every removal
#ifdef MIGA_ARENA_DEBUG
    for (long i = 0; i < arena->allocated_count; i++)
    {
        fprintf(stderr, "LEAK: %p %s:%d %zu\n", arena->allocated_ptrs[i].ptr,
                arena->allocated_ptrs[i].file, arena->allocated_ptrs[i].line,
                arena->allocated_ptrs[i].size);
    }
#endif
    while (arena->allocated_count > 0)
    {
        void *p = arena->allocated_ptrs[--arena->allocated_count].ptr;
        free(p);
    }

    free(arena->allocated_ptrs);
    arena->allocated_ptrs = NULL;
#ifdef MIGA_ARENA_STATS
    arena->stats.live_blocks = 0;
    arena->stats.live_bytes = 0;
#endif
#ifdef MIGA_ARENA_DEBUG
    if (count > 0)
    {
        fprintf(stderr, "Arena reset: freeing %ld allocated blocks\n", count);
    }
#endif
    arena->allocated_cap = arena->allocated_count = 0;

    arena->rollback_in_progress = false;
    // Mark as uninitialized so ensure_initialized will re-init on next use.
    // Preserve setjmp_set — the longjmp target remains valid after reset.
    arena->initialized = false;
    miga_mutex_unlock(&arena->mtx);
}

void arena_end_ex(miga_arena_t *arena)
{
    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);
    arena_reset_ex(arena);
    // Full teardown: clear everything so the arena is back to its
    // completely dormant state.  A subsequent allocation will lazy-init
    // from scratch rather than reusing stale setjmp/cleanup state.
    arena->setjmp_set = false;
    arena->resource_cleanup = NULL;
    arena->resource_cleanup_user_data = NULL;
    miga_mutex_unlock(&arena->mtx);
    miga_mutex_destroy(&arena->mtx);
    arena->mutex_initialized = false;
}

// -------------------------------------------------------------
// Public API
// -------------------------------------------------------------
#ifndef MIGA_ARENA_DEBUG
void *xmalloc(size_t size)
{
    return arena_xmalloc(&global_arena, size);
}

void *xcalloc(size_t n, size_t size)
{
    return arena_xcalloc(&global_arena, n, size);
}

void *xrealloc(void *old_ptr, size_t new_size)
{
    return arena_xrealloc(&global_arena, old_ptr, new_size);
}

char *xstrdup(const char *s)
{
    return arena_xstrdup(&global_arena, s);
}

void xfree(void *p)
{
    arena_xfree(&global_arena, p);
}
#endif

/**
 * Preps the singleton internal memory arena to its initial state.
 */
void miga_arena_init(void)
{
    arena_init_ex(&global_arena);
}

/**
 * Upon a memory allocation failure or a call to miga_arena_reset(),
 *  this function will be called
 * before freeing all the arena-allocated memory and jumping to the jmpbuf
 * location.
 */
void miga_set_out_of_memory_cleanup_cb(miga_arena_resource_cleanup_fn fn, void *user_data)
{
    arena_set_cleanup_ex(&global_arena, fn, user_data);
}

/**
 * Resets the arena-allocated memory by calling the out-of-memory cleanup
 * callback, if provided, then freeing all allocated pointers that hadn't otherwise
 * been freed.
 */
void miga_arena_reset(void)
{
    arena_reset_ex(&global_arena);
}

/**
 * Full teardown of the global arena.  Calls the cleanup callback, frees all
 * tracked allocations, and clears the setjmp rollback point and cleanup
 * callback so the arena returns to its completely dormant initial state.
 * Any subsequent use will require re-initialization (explicitly or via
 * lazy init).
 */
void miga_arena_end(void)
{
    arena_end_ex(&global_arena);
}

// -------------------------------------------------------------
// Memory-usage statistics API (MIGA_ARENA_STATS)
// -------------------------------------------------------------
#ifdef MIGA_ARENA_STATS
void arena_stats_get_ex(miga_arena_t *arena, miga_arena_stats_t *out)
{
    if (!arena || !out)
    {
        fprintf(stderr, "arena_stats_get_ex: NULL pointer passed\n");
        abort();
    }
    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);
    *out = arena->stats;
    miga_mutex_unlock(&arena->mtx);
}

void arena_stats_reset_peak_ex(miga_arena_t *arena)
{
    if (!arena)
    {
        fprintf(stderr, "arena_stats_reset_peak_ex: NULL arena pointer\n");
        abort();
    }
    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);
    arena->stats.peak_blocks = arena->stats.live_blocks;
    arena->stats.peak_bytes = arena->stats.live_bytes;
    miga_mutex_unlock(&arena->mtx);
}

void arena_stats_clear_ex(miga_arena_t *arena)
{
    if (!arena)
    {
        fprintf(stderr, "arena_stats_clear_ex: NULL arena pointer\n");
        abort();
    }
    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);
    size_t live_blocks = arena->stats.live_blocks;
    size_t live_bytes = arena->stats.live_bytes;
    memset(&arena->stats, 0, sizeof(arena->stats));
    arena->stats.live_blocks = live_blocks;
    arena->stats.live_bytes = live_bytes;
    arena->stats.peak_blocks = live_blocks;
    arena->stats.peak_bytes = live_bytes;
    miga_mutex_unlock(&arena->mtx);
}

void arena_stats_foreach_live_ex(miga_arena_t *arena, miga_arena_live_block_fn fn, void *user_data)
{
    if (!arena || !fn)
    {
        fprintf(stderr, "arena_stats_foreach_live_ex: NULL pointer passed\n");
        abort();
    }
    ensure_mutex(arena);
    miga_mutex_lock(&arena->mtx);
    for (long i = 0; i < arena->allocated_count; i++)
        fn(arena->allocated_ptrs[i].ptr, arena->allocated_ptrs[i].size, user_data);
    miga_mutex_unlock(&arena->mtx);
}

void arena_stats_print_ex(miga_arena_t *arena, FILE *out)
{
    if (!arena || !out)
    {
        fprintf(stderr, "arena_stats_print_ex: NULL pointer passed\n");
        abort();
    }
    miga_arena_stats_t st;
    arena_stats_get_ex(arena, &st);

    fprintf(out, "arena: live %zu blocks, %zu bytes; peak %zu blocks, %zu bytes\n",
            st.live_blocks, st.live_bytes, st.peak_blocks, st.peak_bytes);
    fprintf(out,
            "arena: %zu allocs, %zu reallocs, %zu frees; %zu bytes allocated in total; "
            "largest block %zu bytes\n",
            st.total_allocs, st.total_reallocs, st.total_frees, st.total_bytes_allocated,
            st.largest_block);
    fprintf(out, "arena: %zu blocks, %zu bytes still allocated at teardown (%zu resets)\n",
            st.teardown_blocks, st.teardown_bytes, st.resets);
    fprintf(out, "arena: allocation sizes:");
    size_t limit = 16;
    for (int k = 0; k < MIGA_ARENA_STATS_SIZE_CLASSES; k++)
    {
        if (st.size_classes[k] != 0)
        {
            if (k == MIGA_ARENA_STATS_SIZE_CLASSES - 1)
                fprintf(out, " >%zu: %zu", limit / 2, st.size_classes[k]);
            else
                fprintf(out, " <=%zu: %zu", limit, st.size_classes[k]);
        }
        limit *= 2;
    }
    fprintf(out, "\n");
}

void miga_arena_stats_get(miga_arena_stats_t *out)
{
    arena_stats_get_ex(&global_arena, out);
}

void miga_arena_stats_reset_peak(void)
{
    arena_stats_reset_peak_ex(&global_arena);
}

void miga_arena_stats_clear(void)
{
    arena_stats_clear_ex(&global_arena);
}

void miga_arena_stats_foreach_live(miga_arena_live_block_fn fn, void *user_data)
{
    arena_stats_foreach_live_ex(&global_arena, fn, user_data);
}

void miga_arena_stats_print(FILE *out)
{
    arena_stats_print_ex(&global_arena, out);
}
#endif /* MIGA_ARENA_STATS */
