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

#ifndef VRTD_CONFIG_H
#define VRTD_CONFIG_H

#include <stdbool.h>
#include <sys/types.h>

#include "array.h"

struct bar_policy {
    bool any;
};

struct role {
    char *name; /* owning */
    struct uint_array allowed_devices;

    struct bar_policy bar_policy;

    bool query;
    bool allow_any_device;
    bool pcie_hotplug;
};

void cleanup_role(struct role *role);
static inline
void cleanup_rolep(struct role **rolep)
{
    if (rolep == NULL) {
        return;
    }

    cleanup_role(*rolep);

    *rolep = NULL;
}

DECLARE_ARRAY(role_ref_array, struct role *)
DECLARE_OWNING_PTR_ARRAY(role_ptr_array, struct role *, cleanup_role)

struct user_config {
    char *name; /* owning */
    uid_t uid;

    struct str_array role_names; /* Used for lazy loading roles */
    struct role_ref_array roles;
};

void cleanup_user_config(struct user_config *user);
static inline
void cleanup_user_configp(struct user_config **userp)
{
    if (userp == NULL) {
        return;
    }

    cleanup_user_config(*userp);

    *userp = NULL;
}

DECLARE_OWNING_PTR_ARRAY(user_config_ptr_array, struct user_config *, cleanup_user_config)

struct group_config {
    char *name; /* owning */
    gid_t gid;

    struct str_array role_names; /* Used for lazy loading roles */
    struct role_ref_array roles;
};

void cleanup_group_config(struct group_config *group);
static inline
void cleanup_group_configp(struct group_config **groupp)
{
    if (groupp == NULL) {
        return;
    }

    cleanup_group_config(*groupp);

    *groupp = NULL;
}

DECLARE_OWNING_PTR_ARRAY(group_config_ptr_array, struct group_config *, cleanup_group_config)

struct config {
    struct role_ptr_array roles;

    struct user_config *default_user;

    struct user_config_ptr_array users;
    struct group_config_ptr_array groups;

    bool mock_device;
};

void cleanup_config(struct config *config);
static inline
void cleanup_configp(struct config **configp)
{
    if (configp == NULL) {
        return;
    }

    cleanup_config(*configp);

    *configp = NULL;
}

int config_load(struct config **config);

int role_merge_new(struct role **rolep, const char *name);
int role_merge_add_role(struct role *dst, const struct role *src);
int role_merge_add_array(struct role *dst, const struct role_ref_array *roles);

#endif // VRTD_CONFIG_H
