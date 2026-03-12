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

#include "auth.h"
#include "config.h"
#include "state.h"
#include "utils.h"

#include <assert.h>
#include <sys/syslog.h>
#include <stdio.h>
#include <string.h>

int ensure_role(struct client *client);

/*
 * Build a heap-allocated comma-separated string of all role names that apply
 * to @client (default, per-user, and per-group config entries).
 * Returns NULL if the client has no roles or on allocation failure.
 * Caller must free() the returned string.
 */
static char *auth_collect_role_names(const struct client *client)
{
    assert(client->state != NULL);
    assert(client->state->config != NULL);
    const struct config *config = client->state->config;

    char *roles_str = NULL;
    size_t roles_len = 0;
    bool any_role = false;

    #define APPEND_ROLE(r) \
        do { \
            const char *rname = (r)->name; \
            if (rname == NULL) break; \
            size_t rlen = strlen(rname); \
            size_t need = roles_len + (any_role ? 2 : 0) + rlen + 1; \
            char *tmp = realloc(roles_str, need); \
            if (tmp == NULL) { free(roles_str); return NULL; } \
            roles_str = tmp; \
            if (any_role) { \
                memcpy(roles_str + roles_len, ", ", 2); \
                roles_len += 2; \
            } \
            memcpy(roles_str + roles_len, rname, rlen + 1); \
            roles_len += rlen; \
            any_role = true; \
        } while (0)

    if (config->default_user != NULL) {
        for (size_t i = 0; i < config->default_user->roles.len; i++) {
            APPEND_ROLE(config->default_user->roles.d[i]);
        }
    }

    for (size_t i = 0; i < config->users.len; i++) {
        const struct user_config *uc = config->users.d[i];
        if (uc == NULL || uc->uid != client->uid) {
            continue;
        }
        for (size_t j = 0; j < uc->roles.len; j++) {
            APPEND_ROLE(uc->roles.d[j]);
        }
    }

    for (size_t i = 0; i < config->groups.len; i++) {
        const struct group_config *gc = config->groups.d[i];
        if (gc == NULL) {
            continue;
        }
        for (size_t j = 0; j < client->gids.len; j++) {
            if (gc->gid != client->gids.d[j]) {
                continue;
            }
            for (size_t k = 0; k < gc->roles.len; k++) {
                APPEND_ROLE(gc->roles.d[k]);
            }
            break;
        }
    }

    #undef APPEND_ROLE

    return roles_str;
}

static void auth_log_denied(
    struct client *client,
    const char *operation,
    const char *missing_permission
)
{
    char pwbuf[1024];
    const char *username = uid_to_username(client->uid, pwbuf, sizeof(pwbuf));

    (void) sd_journal_print(
        LOG_WARNING,
        "Permission denied for uid %u(%s): '%s' requires '%s'",
        (unsigned int) client->uid,
        username,
        operation,
        missing_permission
    );

    char *roles_str = auth_collect_role_names(client);
    if (roles_str != NULL) {
        (void) sd_journal_print(
            LOG_INFO,
            "User uid %u(%s) has roles: %s",
            (unsigned int) client->uid,
            username,
            roles_str
        );
        free(roles_str);
    } else {
        (void) sd_journal_print(
            LOG_INFO,
            "User uid %u(%s) has no roles",
            (unsigned int) client->uid,
            username
        );
    }
}

int auth_request_get_device_info(
    struct client *client,
    const struct vrtd_req_get_device_info *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (client->role->query) {
        return 1;
    } else {
        auth_log_denied(client, "get_device_info", "query");
        return 0;
    }
}

int auth_request_get_device_by_bdf(
    struct client *client,
    const struct vrtd_req_get_device_by_bdf *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (client->role->query) {
        return 1;
    } else {
        auth_log_denied(client, "get_device_by_bdf", "query");
        return 0;
    }
}

int auth_request_get_num_devices(
    struct client *client,
    const struct vrtd_req_get_num_devices *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (client->role->query) {
        return 1;
    } else {
        auth_log_denied(client, "get_num_devices", "query");
        return 0;
    }
}

int auth_request_get_bar_info(
    struct client *client,
    const struct vrtd_req_get_bar_info *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (client->role->query) {
        return 1;
    } else {
        auth_log_denied(client, "get_bar_info", "query");
        return 0;
    }
}

int auth_request_get_bar_fd(
    struct client *client,
    const struct vrtd_req_get_bar_fd *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (!client->role->query) {
        auth_log_denied(client, "get_bar_fd", "query");
        return 0;
    }

    if (!client->role->allow_any_device) {
        auth_log_denied(client, "get_bar_fd", "allow_any_device");
        return 0;
    }

    if (!client->role->bar_policy.any) {
        auth_log_denied(client, "get_bar_fd", "bar_policy");
        return 0;
    }

    return 1;
}

int auth_request_qdma_get_info(
    struct client *client,
    const struct vrtd_req_qdma_get_info *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (client->role->query) {
        return 1;
    } else {
        auth_log_denied(client, "qdma_get_info", "query");
        return 0;
    }
}

int auth_request_qdma_qpair_add(
    struct client *client,
    const struct vrtd_req_qdma_qpair_add *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (!client->role->query) {
        auth_log_denied(client, "qdma_qpair_add", "query");
        return 0;
    }

    if (!client->role->allow_any_device) {
        auth_log_denied(client, "qdma_qpair_add", "allow_any_device");
        return 0;
    }

    if (!client->role->bar_policy.any) {
        /* TODO: introduce a dedicated QDMA policy instead of reusing bar_policy. */
        auth_log_denied(client, "qdma_qpair_add", "bar_policy");
        return 0;
    }

    return 1;
}

int auth_request_qdma_qpair_op(
    struct client *client,
    const struct vrtd_req_qdma_qpair_op *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (!client->role->query) {
        auth_log_denied(client, "qdma_qpair_op", "query");
        return 0;
    }

    if (!client->role->allow_any_device) {
        auth_log_denied(client, "qdma_qpair_op", "allow_any_device");
        return 0;
    }

    if (!client->role->bar_policy.any) {
        /* TODO: introduce a dedicated QDMA policy instead of reusing bar_policy. */
        auth_log_denied(client, "qdma_qpair_op", "bar_policy");
        return 0;
    }

    return 1;
}

int auth_request_qdma_qpair_get_fd(
    struct client *client,
    const struct vrtd_req_qdma_qpair_get_fd *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (!client->role->query) {
        auth_log_denied(client, "qdma_qpair_get_fd", "query");
        return 0;
    }

    if (!client->role->allow_any_device) {
        auth_log_denied(client, "qdma_qpair_get_fd", "allow_any_device");
        return 0;
    }

    if (!client->role->bar_policy.any) {
        /* TODO: introduce a dedicated QDMA policy instead of reusing bar_policy. */
        auth_log_denied(client, "qdma_qpair_get_fd", "bar_policy");
        return 0;
    }

    return 1;
}

int auth_request_buffer_open(
    struct client *client,
    const struct vrtd_req_buffer_open *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (!client->role->query) {
        auth_log_denied(client, "buffer_open", "query");
        return 0;
    }

    if (!client->role->allow_any_device) {
        auth_log_denied(client, "buffer_open", "allow_any_device");
        return 0;
    }

    if (!client->role->bar_policy.any) {
        /* TODO: introduce a dedicated buffer policy instead of reusing bar_policy. */
        auth_log_denied(client, "buffer_open", "bar_policy");
        return 0;
    }

    return 1;
}

int auth_request_buffer_close(
    struct client *client,
    const struct vrtd_req_buffer_close *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (!client->role->query) {
        auth_log_denied(client, "buffer_close", "query");
        return 0;
    }

    if (!client->role->allow_any_device) {
        auth_log_denied(client, "buffer_close", "allow_any_device");
        return 0;
    }

    if (!client->role->bar_policy.any) {
        /* TODO: introduce a dedicated buffer policy instead of reusing bar_policy. */
        auth_log_denied(client, "buffer_close", "bar_policy");
        return 0;
    }

    return 1;
}

int auth_request_design_write(
    struct client *client,
    const struct vrtd_req_design_write *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (!client->role->query) {
        auth_log_denied(client, "design_write", "query");
        return 0;
    }

    if (!client->role->allow_any_device) {
        auth_log_denied(client, "design_write", "allow_any_device");
        return 0;
    }

    if (!client->role->bar_policy.any) {
        /* TODO: introduce a dedicated policy instead of reusing bar_policy. */
        auth_log_denied(client, "design_write", "bar_policy");
        return 0;
    }

    return 1;
}

int auth_request_device_hotplug_op(
    struct client *client,
    const struct vrtd_req_device_hotplug_op *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (!client->role->query) {
        auth_log_denied(client, "device_hotplug_op", "query");
        return 0;
    }

    if (!client->role->allow_any_device) {
        auth_log_denied(client, "device_hotplug_op", "allow_any_device");
        return 0;
    }

    if (!client->role->pcie_hotplug) {
        auth_log_denied(client, "device_hotplug_op", "pcie_hotplug");
        return 0;
    }

    return 1;
}

int auth_request_clock_op(
    struct client *client,
    const struct vrtd_req_clock_op *req_body
)
{
    assert(client != NULL);
    assert(req_body != NULL);

    int ret = ensure_role(client);
    PROPAGATE_ERROR(ret);

    assert(client->role != NULL);

    if (!client->role->query) {
        auth_log_denied(client, "clock_op", "query");
        return 0;
    }

    if (!client->role->allow_any_device) {
        auth_log_denied(client, "clock_op", "allow_any_device");
        return 0;
    }

    if (!client->role->bar_policy.any) {
        /* TODO: introduce a dedicated clock policy instead of reusing bar_policy. */
        auth_log_denied(client, "clock_op", "bar_policy");
        return 0;
    }

    return 1;
}

int ensure_role(struct client *client)
{
    assert(client != NULL);

    if (client->role != NULL) {
        return 0;
    }

    _cleanup_(cleanup_free)
    char *role_name = NULL;

    int ret = asprintf(&role_name, "Internal role for user: %u", (unsigned int) client->uid);
    PROPAGATE_ERROR_STDC_LOG(ret, LOG_ERR, "Allocation error when intenral role for user");

    _cleanup_(cleanup_rolep)
    struct role *role = NULL;

    ret = role_merge_new(&role, "TODO: Change this string");
    PROPAGATE_ERROR(ret);

    assert(client->state != NULL);
    assert(client->state->config != NULL);

    const struct config *config = client->state->config;

    ret = role_merge_add_array(role, &config->default_user->roles);
    PROPAGATE_ERROR(ret);

    for (size_t i = 0; i < config->users.len; i++) {
        const struct user_config *user_config = config->users.d[i];
        assert(user_config != NULL);

        if (user_config->uid == client->uid) {
            ret = role_merge_add_array(role, &user_config->roles);
            PROPAGATE_ERROR(ret);
        }
    }

    for (size_t i = 0; i < config->groups.len; i++) {
        const struct group_config *group_config = config->groups.d[i];
        assert(group_config != NULL);

        for (size_t j = 0; j < client->gids.len; j++) {
            gid_t gid = client->gids.d[j];

            if (group_config->gid == gid) {
                ret = role_merge_add_array(role, &group_config->roles);
                PROPAGATE_ERROR(ret);
            }
        }
    }

    client->role = role;
    role = NULL;

    return 0;
}
