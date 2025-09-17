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

int ensure_role(struct client *client);

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
        return 0;
    }

    if (!client->role->allow_any_device) {
        return 0;
    }

    if (!client->role->bar_policy.any) {
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
