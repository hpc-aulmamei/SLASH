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

/**
 * @file config.h
 * @brief Configuration data model for the vrtd daemon.
 *
 * The daemon reads a configuration file that defines:
 *   - Named @em roles, each granting a specific set of permissions (which
 *     devices may be accessed, which BARs, whether queries or PCIe hotplug
 *     operations are allowed).
 *   - @em User entries that map a system UID to one or more roles.
 *   - @em Group entries that map a system GID to one or more roles.
 *   - A @em default user entry applied when no explicit UID/GID match is found.
 *
 * Role names inside user/group entries are resolved lazily: the config parser
 * stores the names first, then links them to the actual @c struct @c role
 * objects in a second pass via @c role_merge_add_array.
 */

#ifndef VRTD_CONFIG_H
#define VRTD_CONFIG_H

#include <stdbool.h>
#include <sys/types.h>

#include "array.h"

/**
 * @brief Per-BAR access control policy.
 *
 * Controls which PCI Base Address Registers a role is allowed to mmap.
 * When @c any is true, access to all BARs is granted unconditionally.
 * Future extensions may add an explicit list of allowed BAR indices.
 */
struct bar_policy {
    /** @brief If true, the role may access any BAR on a permitted device. */
    bool any;
};

/**
 * @brief A named permission set that governs what a client may do.
 *
 * Roles are the central unit of the vrtd access-control model.  Each
 * connecting client is assigned a merged role derived from its UID and GID
 * credentials.  The role determines:
 *   - Which device indices the client may address (@c allowed_devices).
 *   - Which BARs the client may mmap (@c bar_policy).
 *   - Whether the client may issue informational queries (@c query).
 *   - Whether the client may access any device without an explicit allowlist
 *     (@c allow_any_device).
 *   - Whether the client may perform PCIe hotplug operations (@c pcie_hotplug).
 */
struct role {
    /** @brief Human-readable name of this role (heap-allocated, owning). */
    char *name; /* owning */
    /** @brief Set of device indices this role is allowed to access. */
    struct uint_array allowed_devices;

    /** @brief BAR-level access control policy for this role. */
    struct bar_policy bar_policy;

    /** @brief If true, the role permits device enumeration and info queries. */
    bool query;
    /** @brief If true, the role permits access to any device (overrides @c allowed_devices). */
    bool allow_any_device;
    /** @brief If true, the role permits PCIe Secondary Bus Reset (SBR) hotplug operations. */
    bool pcie_hotplug;
};

/**
 * @brief Release all resources owned by a role (name string, allowed_devices array).
 * @param role Pointer to the role to clean up.
 */
void cleanup_role(struct role *role);

/**
 * @brief Cleanup helper for use with __attribute__((cleanup)).
 * @param rolep Address of a @c struct @c role pointer.
 */
static inline
void cleanup_rolep(struct role **rolep)
{
    if (rolep == NULL) {
        return;
    }

    cleanup_role(*rolep);

    *rolep = NULL;
}

/** @brief Non-owning array of role pointers (for referencing roles without ownership). */
DECLARE_ARRAY(role_ref_array, struct role *)
/** @brief Owning array of role pointers (frees roles on cleanup). */
DECLARE_OWNING_PTR_ARRAY(role_ptr_array, struct role *, cleanup_role)

/**
 * @brief Maps a system user (by UID) to a set of roles.
 *
 * During configuration loading, role names are stored in @c role_names for
 * lazy resolution.  After all roles are parsed, @c roles is populated with
 * direct pointers.
 */
struct user_config {
    /** @brief Username string (heap-allocated, owning). */
    char *name; /* owning */
    /** @brief Numeric user ID resolved from @c name via getpwnam(). */
    uid_t uid;

    /** @brief Role names from the config file, used for lazy resolution. */
    struct str_array role_names; /* Used for lazy loading roles */
    /** @brief Resolved role pointers (non-owning references into config.roles). */
    struct role_ref_array roles;
};

/**
 * @brief Release all resources owned by a user_config.
 * @param user Pointer to the user_config to clean up.
 */
void cleanup_user_config(struct user_config *user);

/**
 * @brief Cleanup helper for use with __attribute__((cleanup)).
 * @param userp Address of a @c struct @c user_config pointer.
 */
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

/**
 * @brief Maps a system group (by GID) to a set of roles.
 *
 * Analogous to @c struct @c user_config, but keyed on GID.  Role names are
 * lazily resolved the same way.
 */
struct group_config {
    /** @brief Group name string (heap-allocated, owning). */
    char *name; /* owning */
    /** @brief Numeric group ID resolved from @c name via getgrnam(). */
    gid_t gid;

    /** @brief Role names from the config file, used for lazy resolution. */
    struct str_array role_names; /* Used for lazy loading roles */
    /** @brief Resolved role pointers (non-owning references into config.roles). */
    struct role_ref_array roles;
};

/**
 * @brief Release all resources owned by a group_config.
 * @param group Pointer to the group_config to clean up.
 */
void cleanup_group_config(struct group_config *group);

/**
 * @brief Cleanup helper for use with __attribute__((cleanup)).
 * @param groupp Address of a @c struct @c group_config pointer.
 */
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

/**
 * @brief Top-level daemon configuration container.
 *
 * Owns all roles, user mappings, and group mappings.  The @c default_user
 * entry (if present) is applied to any connecting client whose UID/GIDs do
 * not match an explicit user or group entry.
 */
struct config {
    /** @brief All defined roles (owning array). */
    struct role_ptr_array roles;

    /** @brief Fallback user entry applied when no UID/GID match is found (non-owning,
     *         points into @c users or is a standalone allocation). */
    struct user_config *default_user;

    /** @brief Per-UID user configuration entries (owning array). */
    struct user_config_ptr_array users;
    /** @brief Per-GID group configuration entries (owning array). */
    struct group_config_ptr_array groups;

    /** @brief If true, use mock devices instead of real hardware (for testing). */
    bool mock_device;
};

/**
 * @brief Release all resources owned by the config (roles, users, groups).
 * @param config Pointer to the config to clean up.
 */
void cleanup_config(struct config *config);

/**
 * @brief Cleanup helper for use with __attribute__((cleanup)).
 * @param configp Address of a @c struct @c config pointer.
 */
static inline
void cleanup_configp(struct config **configp)
{
    if (configp == NULL) {
        return;
    }

    cleanup_config(*configp);

    *configp = NULL;
}

/**
 * @brief Load the daemon configuration from the default config file.
 *
 * Parses the configuration file, resolves UIDs/GIDs, and lazily links role
 * name references to actual @c struct @c role objects.
 *
 * @param[out] config On success, receives a heap-allocated config. Caller
 *                    must free with cleanup_config().
 * @return 0 on success, -1 on error (logged via sd_journal).
 */
int config_load(struct config **config);

/**
 * @brief Allocate a new empty role and assign it a name.
 *
 * Used during configuration loading and role merging to create a fresh role
 * that can then be populated via @c role_merge_add_role.
 *
 * @param[out] rolep  Receives the newly allocated role on success.
 * @param      name   Name to assign to the role (copied).
 * @return 0 on success, -1 on allocation failure.
 */
int role_merge_new(struct role **rolep, const char *name);

/**
 * @brief Merge permissions from one role into another (union of permissions).
 *
 * Adds @p src's allowed_devices, bar_policy flags, and boolean permissions
 * into @p dst.  This implements the "most permissive wins" merging semantic.
 *
 * @param dst Destination role to merge into (modified in place).
 * @param src Source role to merge from (not modified).
 * @return 0 on success, -1 on error.
 */
int role_merge_add_role(struct role *dst, const struct role *src);

/**
 * @brief Merge an array of roles into a single destination role.
 *
 * Iterates over @p roles and calls @c role_merge_add_role for each entry.
 *
 * @param dst   Destination role to merge into.
 * @param roles Array of role pointers to merge from.
 * @return 0 on success, -1 on error.
 */
int role_merge_add_array(struct role *dst, const struct role_ref_array *roles);

#endif // VRTD_CONFIG_H
