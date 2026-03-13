#ifndef MGSH_API_H
#define MGSH_API_H

/*
   MGSH_API controls symbol visibility / dll export-import.
   MGSH_LOCAL can be used to explicitly hide symbols (very recommended).
*/

#if defined(_WIN32) || defined(__CYGWIN__)
    /* Windows - MSVC & MinGW/GCC on Windows */
    #if defined(__GNUC__) || defined(__clang__)
        /* MinGW / Clang on Windows */
        #define MGSH_EXPORT     __attribute__((dllexport))
        #define MGSH_IMPORT     __attribute__((dllimport))
    #else
        /* Classic MSVC */
        #define MGSH_EXPORT     __declspec(dllexport)
        #define MGSH_IMPORT     __declspec(dllimport)
    #endif
    #define MGSH_LOCAL
#else
    /* GCC / Clang / ICC on Linux, macOS, *BSD, etc. */
    #if __GNUC__ >= 4 || defined(__clang__)
        #define MGSH_EXPORT     __attribute__((visibility("default")))
        #define MGSH_IMPORT
        #define MGSH_LOCAL      __attribute__((visibility("hidden")))
    #else
        /* Very old compiler or unknown platform → no control */
        #define MGSH_EXPORT
        #define MGSH_IMPORT
        #define MGSH_LOCAL
    #endif
#endif

/*
   The actual public API macro — usually what you want to use.
   Controls whether we are building the library or consuming it.
*/
#if defined(MGSH_BUILD_SHARED) || defined(MGSH_EXPORTS)
/* We are building the shared library → export symbols */
    #define MGSH_API  MGSH_EXPORT
#else
/* We are consuming the shared library → import symbols (Windows only) */
    #define MGSH_API  MGSH_IMPORT
#endif

/* Optional: always mark internal/hidden symbols (good practice) */
#ifndef MGSH_HIDDEN
    #define MGSH_HIDDEN   MGSH_LOCAL
#endif

/* Optional: C++ guard (if this header is used from C++) */
#ifdef __cplusplus
    #define MGSH_EXTERN_C_START   extern "C" {
    #define MGSH_EXTERN_C_END     }
#else
    #define MGSH_EXTERN_C_START
    #define MGSH_EXTERN_C_END
#endif

#endif /* MGSH_API_H */

