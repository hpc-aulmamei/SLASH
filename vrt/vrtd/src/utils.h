#ifndef VRTD_UTILS_H
#define VRTD_UTILS_H

#include <stdint.h>
#include <stdlib.h>

#include <glob.h>
#include <systemd/sd-journal.h>

#if defined(__has_include)
#  if __has_include(<stdbit.h>)
#    include <stdbit.h>
#    define HAVE_STDBIT 1
#  endif
#endif

#define NODISCARD __attribute__((warn_unused_result))

/* ---- type-specific helpers ---- */
#if HAVE_STDBIT
static inline uint32_t bit_ceil_u32(uint32_t n) {
    return stdc_bit_ceil((unsigned int)(x));
}
static inline uint64_t bit_ceil_u64(uint64_t n) {
   return stdc_bit_ceil((unsigned long long)(x));
}
#else
static inline uint32_t bit_ceil_u32(uint32_t n) {
    if (n == 0) return 1u;
    if (n > 0x80000000u) return 0u;                 // not representable
    return 1u << (32 - __builtin_clz(n - 1));       // GCC/Clang
}
static inline uint64_t bit_ceil_u64(uint64_t n) {
    if (n == 0) return 1ull;
    if (n > 0x8000000000000000ull) return 0ull;     // not representable
    return 1ull << (64 - __builtin_clzll(n - 1));
}
#endif

/* ---- generic front-end ---- */
#define bit_ceil(n) _Generic((n), \
    uint32_t:               bit_ceil_u32, \
    uint64_t:               bit_ceil_u64  \
)(n)

#define max(a,b) \
   ({ __auto_type _a = (a); \
      __auto_type _b = (b); \
     _a > _b ? _a : _b; })

#define min(a,b) \
   ({ __auto_type _a = (a); \
      __auto_type _b = (b); \
     _a > _b ? _a : _b; })

#define SIZEOF_ARRAY(X) (sizeof(X) / sizeof(X[0]))

#define NOP() ((void) 0)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
    #define NODISCARD [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
    #define NODISCARD __attribute__((warn_unused_result))
#else
    #define NODISCARD
#endif

#ifndef unlikey
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif

// #define ASSERT_TYPE(var, type)                                      \
//         static_assert(                                              \
//             __builtin_types_compatible_p(__typeof__(var), type),    \
//             "Type assertion failed: variable is not of type " #type \
//         )                                                          

#define _PROPAGATE_ERROR_INTERNAL_NOLOG(RET, CMP, JUMP)      \
    ({                                                       \
        __auto_type _ret = RET;                                      \
        if (CMP) {                                           \
            JUMP;                                            \
        }                                                    \
    })

#define _PROPAGATE_ERROR_INTERNAL_LOG(RET, CMP, JUMP, LOGLEVEL, FMT, ...) \
    ({                                                                    \
        __auto_type _ret = RET;                                                   \
        if (CMP) {                                                        \
            sd_journal_print(LOGLEVEL, FMT, ##__VA_ARGS__);               \
            JUMP;                                                         \
        }                                                                \
    })

#define PROPAGATE_ERROR(RET) \
    _PROPAGATE_ERROR_INTERNAL_NOLOG(RET, (_ret == -1), return -1)
#define PROPAGATE_ERROR_NULL(RET) \
    _PROPAGATE_ERROR_INTERNAL_NOLOG(RET, (_ret == NULL), return -1)
#define PROPAGATE_ERROR_SD(RET) \
    _PROPAGATE_ERROR_INTERNAL_NOLOG(RET, (_ret < 0), return -1)

#define PROPAGATE_ERROR_LOG(RET, LOGLEVEL, FMT, ...) \
    _PROPAGATE_ERROR_INTERNAL_LOG(RET, (_ret == -1), return -1, LOGLEVEL, FMT, ##__VA_ARGS__)
#define PROPAGATE_ERROR_NULL_LOG(RET, LOGLEVEL, FMT, ...) \
    _PROPAGATE_ERROR_INTERNAL_LOG(RET, (_ret == NULL), return -1, LOGLEVEL, FMT, ##__VA_ARGS__)
#define PROPAGATE_ERROR_STDC_LOG(RET, LOGLEVEL, FMT, ...) \
    _PROPAGATE_ERROR_INTERNAL_LOG(RET, (_ret == -1), return -1, LOGLEVEL, FMT ": %m", ##__VA_ARGS__)
#define PROPAGATE_ERROR_NULL_STDC_LOG(RET, LOGLEVEL, FMT, ...) \
    _PROPAGATE_ERROR_INTERNAL_LOG(RET, (_ret == NULL), return -1, LOGLEVEL, FMT ": %m", ##__VA_ARGS__)
#define PROPAGATE_ERROR_SD_LOG(RET, LOGLEVEL, FMT, ...) \
    _PROPAGATE_ERROR_INTERNAL_LOG(RET, (_ret < 0), return -1, LOGLEVEL, FMT ": %s", ##__VA_ARGS__, strerrordesc_np(-_ret))


#define _cleanup_(f) __attribute__((cleanup(f)))

static inline const char *glob_err_to_string(int err)
{
    switch (err) {
    case 0:
        return "OK";
    case GLOB_NOSPACE:
        return "out of memory";
    case GLOB_ABORTED:
        return "read error";
    case GLOB_NOMATCH:
        return "no matches found";
    default:
        return "unknown glob(3) error";
    }
}

static inline
void cleanup_free(void *p) {
    void **ptr = (void**)p;
    free(*ptr);

    *ptr = NULL;
}

static inline
void cleanup_argv(char ***p) {
    char **ptr = *p;
    char **ptrel = ptr;

    if (!ptr) {
        return;
    }

    while (*ptrel) {
        free(*ptrel);
        ptrel++;
    }

    free(ptr);

    *p = NULL;
}

#endif // VRTD_UTILS_H
