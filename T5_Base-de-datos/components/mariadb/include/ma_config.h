
#define HAVE_LONG_LONG   1

/*
 * Hard to even find any records of these types.
 */
#undef HAVE_UCHAR
#undef HAVE_UINT
#undef HAVE_ULONG
#undef HAVE_INT64
#undef HAVE_UINT64

#define RETSIGTYPE void
#define RETQSORTTYPE void

/*
 * Endianess.
 */
#ifdef __FreeBSD__
#  include <sys/endian.h> /* BYTE_ORDER */
#else
#  if defined(_WIN32)
#    ifndef BYTE_ORDER
#      define BIG_ENDIAN    4321
#      define LITTLE_ENDIAN 1234
#      define BYTE_ORDER    LITTLE_ENDIAN
#    endif
#  else
#    include <sys/param.h>  /* BYTE_ORDER/__BYTE_ORDER */
#    ifndef BYTE_ORDER
#      ifdef __BYTE_ORDER
#        define BYTE_ORDER    __BYTE_ORDER
#        define BIG_ENDIAN    __BIG_ENDIAN
#        define LITTLE_ENDIAN __LITTLE_ENDIAN
#      else
#        error no BYTE_ORDER/__BYTE_ORDER define
#      endif
#    endif
#  endif
#endif

#if BYTE_ORDER == BIG_ENDIAN
# define HAVE_BIGENDIAN 1
#endif

/*
 * GCC and Clang provide __SIZEOF_*__ predefined macros that we use to define
 * the required libpq macros. Note that on Windows long and long long types are
 * always of 32 and 64 bits width respectively.
 */
#ifndef _WIN32
#  define SIZEOF_CHARP     __SIZEOF_POINTER__
#  define SIZEOF_INT       __SIZEOF_INT__
#  define SIZEOF_LONG      __SIZEOF_LONG__
#  define SIZEOF_LONG_LONG __SIZEOF_LONG_LONG__
#  define SIZEOF_SIZE_T    __SIZEOF_SIZE_T__
#  define SOCKET_SIZE_TYPE socklen_t
#else
#  define SIZEOF_INT       4
#  define SIZEOF_LONG      4
#  define SIZEOF_LONG_LONG 8
#  ifdef _WIN64
#    define SIZEOF_CHARP   8
#    define SIZEOF_SIZE_T  8
#  else
#    define SIZEOF_CHARP   4
#    define SIZEOF_SIZE_T  4
#  endif
#  define SOCKET_SIZE_TYPE int
#endif

/*
 * This one is unconditionally defined compile-time via -D option. Keep it here
 * for the change tracking (see README-DEV).
 */
/*
#undef HAVE_COMPRESS
*/

/*
 * Use thread-safe function versions on Linux.
 */
#ifdef __linux__
#  define HAVE_GETHOSTBYADDR_R 1
#  define HAVE_GETHOSTBYNAME_R 1
#endif

/*
 * Specific for POSIX.
 */
#ifndef _WIN32
#  define HAVE_NL_LANGINFO  1
#  define HAVE_DLFCN_H      1
#  define HAVE_PWD_H        1
#  define HAVE_SCHED_H      1
#  define HAVE_SYS_IOCTL_H  1
#  define HAVE_SYS_SELECT_H 1
#  define HAVE_SYS_SOCKET_H 1
#  define HAVE_SYS_UN_H     1
#  define HAVE_UNISTD_H     1
#  define HAVE_DLERROR      1
#  define HAVE_FCNTL        1
#  define HAVE_GETPWUID     1
#  define HAVE_GETRUSAGE    1
#  define HAVE_LOCALTIME_R  1
#  define HAVE_MLOCK        1
#  define HAVE_POLL         1
#  define HAVE_PREAD        1
#  define HAVE_READLINK     1
#  define HAVE_REALPATH     1
#  define HAVE_SIGWAIT      1
#  define HAVE_SNPRINTF     1
#  define HAVE_STRTOK_R     1

/*
 * Not POSIX but are present on Linux and BSDs.
 */
#  define HAVE_FINITE       1
#  define HAVE_INITGROUPS   1

/*
 * Upstream package undefines if for Apple, but 'man setcontext' recognizes it
 * on MacOS 10.11, so lets try.
 */
#  define HAVE_UCONTEXT_H   1

/*
 * Pthreads.
 */
#  define HAVE_PTHREAD_ATTR_SETSCOPE     1
#  define HAVE_PTHREAD_ATTR_SETSTACKSIZE 1
#  define HAVE_PTHREAD_KILL              1
#  define HAVE_PTHREAD_RWLOCK_RDLOCK     1
#  define HAVE_PTHREAD_SIGMASK           1
#  undef  HAVE_PTHREAD_ATTR_CREATE
#  undef  HAVE_PTHREAD_ATTR_SETPRIO
#  undef  HAVE_PTHREAD_CONDATTR_CREATE
#  undef  HAVE_PTHREAD_SETPRIO_NP

/*
 * Specific for Windows (none).
 */
#else
#endif

/*
 * Common for all supported OSes/compilers.
 */
#define HAVE_SETLOCALE   1
#define HAVE_FCNTL_H     1
#define HAVE_FLOAT_H     1
#define HAVE_LIMITS_H    1
#define HAVE_STDDEF_H    1
#define HAVE_STDLIB_H    1
#define HAVE_SYS_TYPES_H 1
#define HAVE_MEMCPY      1
#define HAVE_VSNPRINTF   1

/*
 * On Windows dl*() macros are defined (see mysql/ma_global.h).
 */
#define HAVE_DLOPEN 1

/*
 * Usage of alloca() is discouraged on Linux and BSDs, so let's not use it.
 */
#undef HAVE_ALLOCA_H
#undef HAVE_ALLOCA

#undef HAVE_SYNCH_H
#undef HAVE_SYS_STREAM_H
#undef HAVE_AIOWAIT
#undef HAVE_SIGTHREADMASK
#undef HAVE_THR_SETCONCURRENCY
#undef HAVE_SELECT_H

/*
 * The upstream package declares API functions as __stdcall on Windows,
 * defining STDCALL macro respectively (see mysql/mysql.h). This doesn't
 * correlate with the way the functions are exported. The cmake-generated .def
 * file always contains raw names, while the real names are decorated for 32
 * bit architecture (on x64 processors __stdcall is ignored), having the
 * _<name>@<size> form for VC and <name>@<size> form for MinGW. VC linker
 * manages to deal with this properly at both the library creation and linkage
 * times. MinGW linker issues warnings when undecorates the names during the
 * library creation and fails to link the import library to the executable
 * later on, not being able to resolve the decorated names wanted by the
 * executable to the non-decorated ones in the library.
 *
 * So we will just cancel the __stdcall calling convention for MinGW when
 * build 32 bits targets. Note that we also need to add the same directives to
 * mysql/mariadb_version.h (client side).
 *
 * Also note that the upstream package (as of 10.2.10) can't be compiled with
 * MinGW GCC.
 */
#if defined(__MINGW32__) && !defined(_WIN64)
#  undef  STDCALL
#  define STDCALL
#endif
