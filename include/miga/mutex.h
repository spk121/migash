#ifndef MIGA_MUTEX_H
#define MIGA_MUTEX_H

/**
 * Cross-platform recursive mutex abstraction.
 *
 * Selection:
 *   - __STDC_NO_THREADS__ defined → no-op stubs (single-threaded)
 *   - otherwise                   → C11 mtx_t (mtx_recursive)
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "miga/api.h"
#include "miga/migaconf.h"

MIGA_EXTERN_C_START

/* ------------------------------------------------------------------ */
/* Single-threaded                                                    */
/* ------------------------------------------------------------------ */
#if defined(__STDC_NO_THREADS__)

typedef int miga_mutex_t; /* placeholder, never used */

static inline void miga_mutex_init(miga_mutex_t *mtx)    { (void)mtx; }
static inline void miga_mutex_lock(miga_mutex_t *mtx)    { (void)mtx; }
static inline void miga_mutex_unlock(miga_mutex_t *mtx)  { (void)mtx; }
static inline void miga_mutex_destroy(miga_mutex_t *mtx) { (void)mtx; }

/* ------------------------------------------------------------------ */
/* C11 threads                                                        */
/* ------------------------------------------------------------------ */
#else

#include <threads.h>

typedef mtx_t miga_mutex_t;

static inline void miga_mutex_init(miga_mutex_t *mtx)
{
    if (mtx_init(mtx, mtx_recursive) != thrd_success)
    {
        fprintf(stderr, "miga_mutex_init: mtx_init failed\n");
        abort();
    }
}

static inline void miga_mutex_lock(miga_mutex_t *mtx)
{
    if (mtx_lock(mtx) != thrd_success)
    {
        fprintf(stderr, "miga_mutex_lock: mtx_lock failed\n");
        abort();
    }
}

static inline void miga_mutex_unlock(miga_mutex_t *mtx)
{
    if (mtx_unlock(mtx) != thrd_success)
    {
        fprintf(stderr, "miga_mutex_unlock: mtx_unlock failed\n");
        abort();
    }
}

static inline void miga_mutex_destroy(miga_mutex_t *mtx)
{
    mtx_destroy(mtx);
}

#endif /* platform selection */

MIGA_EXTERN_C_END

#endif /* MIGA_MUTEX_H */
