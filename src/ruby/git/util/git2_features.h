#ifndef INCLUDE_features_h__
#define INCLUDE_features_h__

#define GIT_THREADS 1

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
# define GIT_ARCH_64 1
#else
# define GIT_ARCH_32 1
#endif

#define GIT_USE_NSEC 1
#define GIT_USE_STAT_MTIM 1
#define GIT_USE_FUTIMENS 1

#define GIT_REGEX_REGCOMP 1

#if defined(__GLIBC__)
# define GIT_QSORT_GNU 1
#elif defined(_MSC_VER)
# define GIT_QSORT_MSC 1
#endif

#define GIT_SHA1_COLLISIONDETECT 1
#define GIT_SHA256_BUILTIN 1

#define GIT_COMPRESSION_ZLIB 1

#define GIT_RAND_GETENTROPY 1
#define GIT_IO_POLL 1

#endif
