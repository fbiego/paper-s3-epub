/* Portable C codec; no platform SIMD or arithmetic coding. */
#define BUILD "20261026"
#define HIDDEN __attribute__((visibility("hidden")))
#define INLINE __inline__ __attribute__((always_inline))
#define THREAD_LOCAL __thread
#define PACKAGE_NAME "libjpeg-turbo"
#define VERSION "3.2.0"
#define SIZEOF_SIZE_T __SIZEOF_SIZE_T__
#define HAVE_BUILTIN_CTZL
#define FALLTHROUGH __attribute__((fallthrough));
#define NO_GETENV
#ifndef BITS_IN_JSAMPLE
#define BITS_IN_JSAMPLE 8
#endif
