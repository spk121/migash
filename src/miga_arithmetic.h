#ifndef MIGA_ARITHMETIC_H
#define MIGA_ARITHMETIC_H

#include "miga_cppguard.h"
#include "miga_visibility.h"

#include "type_pub.h"
#include "miga_string.h"

MIGA_EXTERN_C_START

/**
 * Result of an arithmetic expression evaluation.
 *
 * The caller owns this struct and must call miga_arithmetic_result_free() to clean up.
 * The error field (if non-NULL) is owned by the caller and will be freed by
 * miga_arithmetic_result_free().
 */
typedef struct miga_arithmetic_result_t{
    long value;      // Result value if successful
    int failed;      // 1 if error occurred, 0 if successful
    miga_string_t *error; // Error message if failed (owned by caller, freed by miga_arithmetic_result_free)
} miga_arithmetic_result_t;

/**
 * Evaluate an arithmetic expression with full POSIX semantics.
 *
 * This function performs parameter expansion, command substitution, and arithmetic
 * evaluation according to POSIX shell arithmetic rules.
 *
 * @param frame The execution frame (provides executor, variables, etc.)
 *              The frame's variable store may be modified for assignment operations like x=5.
 * @param expression The arithmetic expression to evaluate (not modified, deep copied internally)
 *
 * @return miga_arithmetic_result_t struct containing the result or error. Caller must call
 *         miga_arithmetic_result_free() to clean up, even on success (though it's a no-op
 *         if no error occurred).
 */
MIGA_API miga_arithmetic_result_t
miga_arithmetic_evaluate(miga_frame_t *frame, const miga_string_t *expression);

/**
 * Free resources associated with an miga_arithmetic_result_t.
 *
 * This function frees the error string if present and resets the result struct.
 * It is safe to call even if the result was successful (no error).
 * After calling this function, the result struct is left in a clean state.
 *
 * @param result The result to free (must not be NULL)
 */
MIGA_API void
miga_arithmetic_result_free(miga_arithmetic_result_t *result);

MIGA_EXTERN_C_END

#endif // MIGA_ARITHMETIC_H
