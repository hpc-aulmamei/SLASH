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

#define _GNU_SOURCE

#define VRTD_DEFAULT_CONFIG_PATH "/etc/vrt/vrtd.conf"

#include "array.h"
#include "config.h"
#include "utils.h"

#include <assert.h>
#include <errno.h>
#include <grp.h>
#include <glob.h>
#include <pwd.h>
#include <sys/syslog.h>
#include <stdbool.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <ini.h>
#include <systemd/sd-journal.h>

// This is on Ubuntu
static_assert(INI_HANDLER_LINENO == 0, "vrtd does not support INI_HANDLER_LINENO = 1");

static const char DEFAULT_USER_NAME[] = "*";

struct config_parse_state {
    struct config *config;

    struct str_array visited_files;
};

static int parse_file_glob(struct config_parse_state *state, const char *pattern);
static int parse_file(struct config_parse_state *state, const char *path);
static int parse_file_unique(struct config_parse_state *state, const char *path);
static int parse_config_callback(void *user, const char *section, const char *name, const char *value);
static int role_find_and_add_value(struct config *config, const char *objname, const char *name, const char *value);
static int role_add_value(struct role *role, const char *name, const char *value);
static int user_find_and_add_value(struct config *config, const char *objname, const char *name, const char *value);
static int user_add_value(struct user_config *user, const char *name, const char *value);
static int group_find_and_add_value(struct config *config, const char *objname, const char *name, const char *value);
static int group_add_value(struct group_config *group, const char *name, const char *value);
static int set_user_uid(uid_t *uid, const char *name);
static int set_group_gid(gid_t *gid, const char *name);
static int assign_users_roles(struct config *config);
static int assign_user_roles(struct config *config, struct user_config *user);
static int assign_groups_roles(struct config *config);
static int assign_group_roles(struct config *config, struct group_config *group);

// Cleanups

void cleanup_role(struct role *role)
{
    if (role == NULL) {
        return;
    }

    free(role->name);
    role->name = NULL;

    uint_array_free(&role->allowed_devices);

    free(role);
}

void cleanup_user_config(struct user_config *user)
{
    if (user == NULL) {
        return;
    }

    free(user->name);
    user->name = NULL;

    str_array_free(&user->role_names);
    role_ref_array_free(&user->roles);

    free(user);
}

void cleanup_group_config(struct group_config *group)
{
    if (group == NULL) {
        return;
    }

    free(group->name);
    group->name = NULL;

    str_array_free(&group->role_names);
    role_ref_array_free(&group->roles);

    free(group);
}

void cleanup_config(struct config *config)
{
    if (config == NULL) {
        return;
    }

    role_ptr_array_free(&config->roles);

    cleanup_user_config(config->default_user);

    user_config_ptr_array_free(&config->users);
    group_config_ptr_array_free(&config->groups);

    free(config);
}

static inline
void cleanup_parse_state_stack(struct config_parse_state *state)
{
    state->config = NULL;

    str_array_free(&state->visited_files);
}

// Role merge

int role_merge_new(struct role **rolep, const char *name)
{
    assert(rolep != NULL);

    _cleanup_(cleanup_rolep)
    struct role *role = calloc(1, sizeof *role);
    PROPAGATE_ERROR_NULL_STDC_LOG(role, LOG_ERR, "Error allocating new role");

    _cleanup_(cleanup_free)
    char *s = strdup(name);
    PROPAGATE_ERROR_NULL_STDC_LOG(s, LOG_ERR, "Error allocating new role");

    role->name = s;
    s = NULL;

    *rolep = role;
    role = NULL;

    return 0;
}

int role_merge_add_role(struct role *dst, const struct role *src)
{
    if (dst == NULL || src == NULL) {
        assert(false);
        return -1;
    }

    /* Highest privilege wins => OR the booleans. */
    dst->pcie_hotplug    = dst->pcie_hotplug    || src->pcie_hotplug;
    dst->query           = dst->query           || src->query;
    dst->allow_any_device= dst->allow_any_device|| src->allow_any_device;
    dst->bar_policy.any  = dst->bar_policy.any  || src->bar_policy.any;

    /* TODO: Intentionally skip allowed_devices merging for now. */
    return 0;
}

int role_merge_add_array(struct role *dst, const struct role_ref_array *roles)
{
    if (dst == NULL || roles == NULL) {
        assert(false);
        return -1;
    }

    for (size_t i = 0; i < roles->len; ++i) {
        const struct role *r = roles->d[i];
        assert(r != NULL);

        int ret = role_merge_add_role(dst, r);
        PROPAGATE_ERROR(ret);
    }

    return 0;
}



int config_load(struct config **configp)
{
    _cleanup_(cleanup_parse_state_stack)
    struct config_parse_state state = {0};

    // Cleanup on error
    _cleanup_(cleanup_configp)
    struct config *config = calloc(1, sizeof(**configp));

    config->default_user = calloc(1, sizeof(*config->default_user));
    PROPAGATE_ERROR_NULL_LOG(config->default_user, LOG_ERR, "Memory error assigning default user");

    config->default_user->name = strdup(DEFAULT_USER_NAME);
    PROPAGATE_ERROR_NULL_LOG(config->default_user->name, LOG_ERR, "Memory error assigning default user name");

    state.config = config;

    const char *path = getenv("VRTD_CONFIG");
    if (path == NULL || path[0] == '\0') {
        path = VRTD_DEFAULT_CONFIG_PATH;
    }

    int ret = parse_file(&state, path);

    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed to parse config file");

    ret = assign_users_roles(config);
    PROPAGATE_ERROR(ret);

    ret = assign_groups_roles(config);
    PROPAGATE_ERROR(ret);

    // No error, do not cleanup
    *configp = config;
    config = NULL;

    return 0;
}

static int parse_file_glob(struct config_parse_state *state, const char *pattern)
{
    _cleanup_(globfree)
    glob_t glob_state;
    memset(&glob_state, 0, sizeof(glob_state));

    int ret = glob(pattern, GLOB_ERR, NULL, &glob_state);
    if (ret == GLOB_NOMATCH) {
        return 0;
    } else if (ret != 0) {
        (void) sd_journal_print(
            LOG_ERR,
            "Error matching pattern %s: %s",
            pattern,
            glob_err_to_string(ret)
        );

        return -1;
    }

    for (size_t i = 0; i < glob_state.gl_pathc; i++) {
        ret = parse_file(state, glob_state.gl_pathv[i]);
        PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Found by pattern %s", pattern);
    }

    return 0;
}

static int parse_file(struct config_parse_state *state, const char *path)
{
    _cleanup_(cleanup_free)
    char *full_path = realpath(path, NULL);
    PROPAGATE_ERROR_NULL_STDC_LOG(full_path, LOG_ERR, "Error obtaining the cannonical path for %s", path);

    for (size_t i = 0; i < state->visited_files.len; i++) {
        if (strcmp(full_path, state->visited_files.d[i]) == 0) {
            /* We have already parsed this file -- exit as OK */    
            return 0;
        }
    }

    char *full_path_ref = full_path;

    int ret = str_array_push_move(&state->visited_files, &full_path);
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Error processing %s", full_path);

    ret = parse_file_unique(state, full_path_ref);
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Error parsing file %s", full_path_ref);

    return 0;
}

static int parse_file_unique(struct config_parse_state *state, const char *path)
{
    int ret = ini_parse(path, parse_config_callback, state);
    if (ret != 0) {
        if (ret > 0) {
            (void) sd_journal_print(LOG_ERR, "Parse error at %s:%d", path, ret);
            return -1;
        } else if (ret == -1) {
            (void) sd_journal_print(LOG_ERR, "Could not open file %s", path);
            return -1;
        } else if (ret == -2) {
            (void) sd_journal_print(LOG_ERR, "Out of memory reading file %s", path);
            return -1;
        } else {
            (void) sd_journal_print(LOG_WARNING, "Unknown error reading file %s", path);
            return 0;
        }
    }

    return 0;
}

// This callback uses 0 for error and 1 for success, as per inih spec
static int parse_config_callback(void *user, const char *section, const char *name, const char *value)
{
    #define MATCH(s, n) (strcmp(section, s) == 0 && strcmp(name, n) == 0)
    #define MATCH_OBJECT(c, n) \
    ({ const char *colon__ = strchr(section, ':'); \
       colon__ && (size_t)(colon__ - section) == strlen(c) && \
       memcmp(section, (c), strlen(c)) == 0 && \
       (n = colon__ + 1, n[0] != '\0'); \
    })

    int ret;
    const char *objname;
    struct config_parse_state *state = user;

    if (MATCH("", "include")) {
        ret = parse_file(state, value);
        if (ret == -1) {
            return 0;
        }
    } else if (MATCH("", "include-glob")) {
        ret = parse_file_glob(state, value);
        if (ret == -1) {
            return 0;
        }
    } else if (MATCH("", "enable-mock-device")) {
        state->config->mock_device = string_to_bool(value);
    } else if (MATCH_OBJECT("role", objname)) {
        ret = role_find_and_add_value(state->config, objname, name, value);
        if (ret == -1) {
            return 0;
        }
    } else if (MATCH_OBJECT("user", objname)) {
        ret = user_find_and_add_value(state->config, objname, name, value);
        if (ret == -1) {
            return 0;
        }
    } else if (MATCH_OBJECT("group", objname)) {
        ret = group_find_and_add_value(state->config, objname, name, value);
        if (ret == -1) {
            return 0;
        }
    } else {
        (void) sd_journal_print(LOG_WARNING, "Unknown section/key: [%s] %s", section, name);
        return 1;
    }

    return 1;

    #undef MATCH
    #undef MATCH_OBJECT 
}

static int role_find_and_add_value(struct config *config, const char *objname, const char *name, const char *value)
{
    for (size_t i = 0; i < config->roles.len; ++i) {
        if (strcmp(config->roles.d[i]->name, objname) == 0) {
            return role_add_value(config->roles.d[i], name, value);
        }
    }

    _cleanup_(cleanup_rolep)
    struct role *role = calloc(1, sizeof *role);
    PROPAGATE_ERROR_NULL_STDC_LOG(role, LOG_ERR, "Could not allocate role");

    role->name = strdup(objname);
    PROPAGATE_ERROR_NULL_STDC_LOG(role->name, LOG_ERR, "Could not allocate role name");

    int ret = role_add_value(role, name, value);
    PROPAGATE_ERROR(ret);

    ret = role_ptr_array_push_move(&config->roles, &role);
    PROPAGATE_ERROR(ret);

    return 0;
}

static int role_add_value(struct role *role, const char *name, const char *value)
{
    if (strcmp(name, "pcie-hotplug") == 0) {
        if (strcmp(value, "yes") == 0) {
            role->pcie_hotplug = true;
            return 0;
        } else if (strcmp(value, "no") == 0) {
            role->pcie_hotplug = false;
            return 0;
        } else {
            return -1;
        }
    } else if (strcmp(name, "bar-access") == 0) {
        if (strcmp(value, "full") == 0) {
            role->bar_policy.any = true;
            return 0;
        } else {
            return -1;
        }
    } else if (strcmp(name, "device") == 0) {
        if (strcmp(value, "any") == 0) {
            role->allow_any_device = true;
            return 0;
        } else {
            return -1;
        }
    } else if (strcmp(name, "query-devices") == 0) {
        if (strcmp(value, "yes") == 0) {
            role->query = true;
            return 0;
        } else if (strcmp(value, "no") == 0) {
            role->query = false;
            return 0;
        } else {
            return -1;
        }
    } else {
        return -1;
    }
}

static int user_find_and_add_value(struct config *config, const char *objname, const char *name, const char *value)
{
    if (strcmp(objname, "*") == 0) {
        return user_add_value(config->default_user, name, value);
    }

    for (size_t i = 0; i < config->users.len; ++i) {
        if (strcmp(config->users.d[i]->name, objname) == 0) {
            return user_add_value(config->users.d[i], name, value);
        }
    }

    _cleanup_(cleanup_user_configp)
    struct user_config *user = calloc(1, sizeof *user);
    PROPAGATE_ERROR_NULL_STDC_LOG(user, LOG_ERR, "Could not allocate user");

    user->name = strdup(objname);
    PROPAGATE_ERROR_NULL_STDC_LOG(user->name, LOG_ERR, "Could not allocate user name");

    int ret = set_user_uid(&user->uid, objname);
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Could not find uid for user %s", objname);

    ret = user_add_value(user, name, value);
    PROPAGATE_ERROR(ret);

    ret = user_config_ptr_array_push_move(&config->users, &user);
    PROPAGATE_ERROR(ret);

    return 0;
}

static int user_add_value(struct user_config *user, const char *name, const char *value)
{
    if (strcmp(name, "role") == 0) {
        _cleanup_(cleanup_free)
        char *role = strdup(value);
        PROPAGATE_ERROR_NULL_STDC_LOG(role, LOG_ERR, "Could not allocate role name");

        int ret = str_array_push_move(&user->role_names, &role);
        PROPAGATE_ERROR(ret);

        return 0;
    } else {
        return -1;
    }
}

static int group_find_and_add_value(struct config *config, const char *objname, const char *name, const char *value)
{
    for (size_t i = 0; i < config->groups.len; ++i) {
        if (strcmp(config->groups.d[i]->name, objname) == 0) {
            return group_add_value(config->groups.d[i], name, value);
        }
    }

    _cleanup_(cleanup_group_configp)
    struct group_config *group = calloc(1, sizeof *group);
    PROPAGATE_ERROR_NULL_STDC_LOG(group, LOG_ERR, "Could not allocate group");

    group->name = strdup(objname);
    PROPAGATE_ERROR_NULL_STDC_LOG(group->name, LOG_ERR, "Could not allocate group name");

    int ret = set_group_gid(&group->gid, objname);
    PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Could not find gid for group %s", objname);

    ret = group_add_value(group, name, value);
    PROPAGATE_ERROR(ret);

    ret = group_config_ptr_array_push_move(&config->groups, &group);
    PROPAGATE_ERROR(ret);

    return 0;
}

static int group_add_value(struct group_config *group, const char *name, const char *value)
{
    if (strcmp(name, "role") == 0) {
        _cleanup_(cleanup_free)
        char *role = strdup(value);
        PROPAGATE_ERROR_NULL_STDC_LOG(role, LOG_ERR, "Could not allocate role name");

        int ret = str_array_push_move(&group->role_names, &role);
        PROPAGATE_ERROR(ret);

        return 0;
    } else {
        return -1;
    }
}

static int set_user_uid(uid_t *uid, const char *name)
{
    size_t bufsz = BUFSIZ;
    int ret;
    struct passwd pwd;
    struct passwd *result;

    do {
        char *buf = malloc(bufsz);
        PROPAGATE_ERROR_NULL_STDC_LOG(buf, LOG_ERR, "Failed malloc in get_user_uid");

retry:
        ret = getpwnam_r(name, &pwd, buf, bufsz, &result);
        if (ret == EINTR) {
            goto retry;
        }

        free(buf);

        bufsz *= 2;
    } while (ret == ERANGE);

    PROPAGATE_ERROR_NULL_STDC_LOG(result, LOG_ERR, "User %s not found", name);

    *uid = result->pw_uid;
    return 0;
}

static int set_group_gid(gid_t *gid, const char *name)
{
    size_t bufsz = BUFSIZ;
    int ret;
    struct group pwd;
    struct group *result;

    do {
        char *buf = malloc(bufsz);
        PROPAGATE_ERROR_NULL_STDC_LOG(buf, LOG_ERR, "Failed malloc in set_group_gid");

retry:
        ret = getgrnam_r(name, &pwd, buf, bufsz, &result);
        if (ret == EINTR) {
            goto retry;
        }

        free(buf);

        bufsz *= 2;
    } while (ret == ERANGE);

    PROPAGATE_ERROR_NULL_STDC_LOG(result, LOG_ERR, "Group %s not found", name);

    *gid = result->gr_gid;
    return 0;
}

static int assign_users_roles(struct config *config)
{
    int ret = assign_user_roles(config, config->default_user);
    PROPAGATE_ERROR(ret);

    for (size_t i = 0; i < config->users.len; i++) {
        ret = assign_user_roles(config, config->users.d[i]);
        PROPAGATE_ERROR(ret);        
    }

    return 0;
}

static int assign_user_roles(struct config *config, struct user_config *user)
{
    for (size_t j = 0; j < user->role_names.len; j++) {
        bool found_role_name = false;

        for (size_t k = 0; k < config->roles.len; k++) {
            if (strcmp(user->role_names.d[j], config->roles.d[k]->name) == 0) {
                int ret = role_ref_array_push(&user->roles, config->roles.d[k]);
                PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed allocation in assign_user_roles");

                found_role_name = true;
                break;
            }
        }

        if (!found_role_name) {
            (void) sd_journal_print(LOG_WARNING, "Failed to find user role %s for user %s", user->role_names.d[j], user->name);
        }
    }

    return 0;
}

static int assign_groups_roles(struct config *config)
{
    for (size_t i = 0; i < config->groups.len; i++) {
        int ret = assign_group_roles(config, config->groups.d[i]);
        PROPAGATE_ERROR(ret);
    }

    return 0;
}

static int assign_group_roles(struct config *config, struct group_config *group)
{
    for (size_t j = 0; j < group->role_names.len; j++) {
        bool found_role_name = false;

        for (size_t k = 0; k < config->roles.len; k++) {
            if (strcmp(group->role_names.d[j], config->roles.d[k]->name) == 0) {
                int ret = role_ref_array_push(&group->roles, config->roles.d[k]);
                PROPAGATE_ERROR_LOG(ret, LOG_ERR, "Failed allocation in assign_group_roles");

                found_role_name = true;
                break;
            }
        }

        if (!found_role_name) {
            (void) sd_journal_print(LOG_WARNING, "Failed to find group role %s for group %s", group->role_names.d[j], group->name);
        }
    }

    return 0;
}
