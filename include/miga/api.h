#ifndef MIGA_API_H
#define MIGA_API_H

/*
   MIGA_API controls symbol visibility / dll export-import.
   MIGA_LOCAL can be used to explicitly hide symbols (very recommended).
*/

#if defined(_WIN32) || defined(__CYGWIN__)
    /* Windows - MSVC & MinGW/GCC on Windows */
    #if defined(__GNUC__) || defined(__clang__)
        /* MinGW / Clang on Windows */
        #define MIGA_EXPORT     __attribute__((dllexport))
        #define MIGA_IMPORT     __attribute__((dllimport))
    #else
        /* Classic MSVC */
        #define MIGA_EXPORT     __declspec(dllexport)
        #define MIGA_IMPORT     __declspec(dllimport)
    #endif
    #define MIGA_LOCAL
#else
    /* GCC / Clang / ICC on Linux, macOS, *BSD, etc. */
    #if __GNUC__ >= 4 || defined(__clang__)
        #define MIGA_EXPORT     __attribute__((visibility("default")))
        #define MIGA_IMPORT
        #define MIGA_LOCAL      __attribute__((visibility("hidden")))
    #else
        /* Very old compiler or unknown platform → no control */
        #define MIGA_EXPORT
        #define MIGA_IMPORT
        #define MIGA_LOCAL
    #endif
#endif

/*
   The actual public API macro — usually what you want to use.
   Controls whether we are building the library or consuming it.
*/
#if defined(MIGA_BUILD_SHARED)
/* We are building the shared library → export symbols */
    #define MIGA_API  MIGA_EXPORT
#else
/* We are consuming the shared library → import symbols (Windows only) */
    #define MIGA_API  MIGA_IMPORT
#endif

/* Optional: always mark internal/hidden symbols (good practice) */
#ifndef MIGA_HIDDEN
    #define MIGA_HIDDEN   MIGA_LOCAL
#endif

/* Optional: C++ guard (if this header is used from C++) */
#ifdef __cplusplus
    #define MIGA_EXTERN_C_START   extern "C" {
    #define MIGA_EXTERN_C_END     }
#else
    #define MIGA_EXTERN_C_START
    #define MIGA_EXTERN_C_END
#endif

#endif /* MIGA_API_H */

