/**
 * The MIT License (MIT)
 * Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef VRTD_ARRAY_H
#define VRTD_ARRAY_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "utils.h"

// DECLARE_ARRAY declares a dynamic array of type T.
// 
// Access is done directly via a.d[i] and a.len. The 0-value is the 0-len array.
// 
#define DECLARE_ARRAY_IMPL(T_ARRAY, T) \
    struct T_ARRAY { \
        T *d; \
        size_t len; \
        size_t cap; \
    }; \
    \
    static inline \
    struct T_ARRAY T_ARRAY##_init(void) { \
        return (struct T_ARRAY) { \
            .d = NULL, \
            .len = 0, \
            .cap = 0, \
        }; \
    } \
    \
    static inline NODISCARD \
    int T_ARRAY##_resize(struct T_ARRAY *arr, size_t len) \
    { \
        size_t cap = likely(len > 0) ? bit_ceil(len) : 0; \
        T *d; \
    \
        /* Don't constantly reallocate for add-remove 1024-1025 elements */ \
        /* This may reallocate unnecessarily (once) for tightened arrays but this is fine */ \
        /* Tighthening should only be done for arrays that will keep their size for a long time */ \
        /* Otherwise we'd have to complicate this hot comparison */ \
        if (cap == arr->cap || cap == (arr->cap >> 1)) { \
            arr->len = len; \
            return 0; \
        } \
    \
        d = (T *) reallocarray(arr->d, cap, sizeof(T)); \
        if (unlikely(d == NULL && cap != 0)) { \
            return -1; \
        } \
    \
        arr->d = d; \
        arr->len = len; \
        arr->cap = cap; \
    \
        return 0; \
    } \
    \
    static inline NODISCARD \
    int T_ARRAY##_push(struct T_ARRAY *arr, T v) { \
        if (unlikely(T_ARRAY##_resize(arr, arr->len + 1) == -1)) { \
            return -1; \
        } \
        arr->d[arr->len - 1] = v; \
        return 0; \
    } \
    \
    static inline int T_ARRAY##_pop(struct T_ARRAY *arr, T *out) { \
        if (arr->len == 0) { \
            return -1; \
        } \
    \
        if (out != NULL) { \
            *out = arr->d[arr->len - 1]; \
        } \
    \
        return T_ARRAY##_resize(arr, arr->len - 1); \
    } \
    \
    static inline void T_ARRAY##_pop_safe(struct T_ARRAY *arr, T *out) { \
        if (arr->len == 0) { \
            return; \
        } \
    \
        if (out != NULL) { \
            *out = arr->d[arr->len - 1]; \
        } \
    \
        arr->len--; \
    } \
    \
    static inline \
    void T_ARRAY##_zero(struct T_ARRAY *arr) \
    { \
        memset(arr->d, 0, arr->cap * sizeof(T)); \
    } \
    \
    static inline \
    int T_ARRAY##_shrink_to_fit(struct T_ARRAY *arr) \
    { \
        T *d; \
    \
        d = (T *) reallocarray(arr->d, arr->len, sizeof(T)); \
        if (unlikely(d == NULL && arr->len != 0)) { \
            return -1; \
        } \
    \
        arr->d = d; \
        arr->cap = arr->len; \
    \
        return 0; \
    } \
    \
    static inline \
    void T_ARRAY##_rm_by_value_impl(struct T_ARRAY *arr, T value) \
    { \
        size_t j = 0; \
        for (size_t i = 0; i < arr->len; i++) { \
            if (arr->d[i] == value) { \
                continue; \
            } \
            \
            arr->d[j++] = arr->d[i]; \
        } \
        \
        arr->len = j; \
    } \
    \
    static inline \
    void T_ARRAY##_free_impl(struct T_ARRAY *arr) \
    { \
        free(arr->d); \
    \
        arr->d = NULL; \
        arr->len = 0; \
        arr->cap = 0; \
    }

#define DECLARE_ARRAY(T_ARRAY, T) \
    DECLARE_ARRAY_IMPL(T_ARRAY, T) \
    \
    static inline \
    void T_ARRAY##_free(struct T_ARRAY *arr) \
    { \
        T_ARRAY##_free_impl(arr); \
    } \
\
    static inline \
    void T_ARRAY##_freep(struct T_ARRAY **arr) \
    { \
        T_ARRAY##_free(*arr); \
        *arr = NULL; \
    } \
    \
    static inline \
    void T_ARRAY##_rm_by_value(struct T_ARRAY *arr, T value) \
    { \
        T_ARRAY##_rm_by_value_impl(arr, value); \
    }

#define DECLARE_OWNING_PTR_ARRAY(T_ARRAY, T, CLEANUP) \
    DECLARE_ARRAY_IMPL(T_ARRAY, T) \
    \
    static inline \
    int T_ARRAY##_push_move(struct T_ARRAY *arr, T *ptr) \
    { \
        int ret = T_ARRAY##_push(arr, *ptr); \
        if (unlikely(ret == -1)) { \
            return -1; \
        } \
    \
        *ptr = NULL; \
    \
        return 0; \
    } \
    \
    static inline \
    void T_ARRAY##_free(struct T_ARRAY *arr) \
    { \
        for (size_t i = 0; i < arr->len; ++i) { \
            CLEANUP(arr->d[i]); \
        } \
    \
        T_ARRAY##_free_impl(arr); \
    } \
    \
    static inline \
    void T_ARRAY##_rm_by_value(struct T_ARRAY *arr, T value) \
    { \
        T_ARRAY##_rm_by_value_impl(arr, value); \
    \
        CLEANUP(value); \
    }

DECLARE_ARRAY(int_array, int)
DECLARE_ARRAY(uint_array, unsigned int)
DECLARE_ARRAY(gid_t_array, gid_t)
DECLARE_OWNING_PTR_ARRAY(str_array, char *, free)

#endif // VRTD_ARRAY_H
