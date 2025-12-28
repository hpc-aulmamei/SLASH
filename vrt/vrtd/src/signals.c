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

#include "signals.h"

#include <assert.h>
#include <stdio.h>
#include <systemd/sd-journal.h>
#include <sys/syslog.h>

#include "state.h"
#include "config.h"
#include "utils.h"

int reload_config(struct vrtd *state);

int on_event_signal(sd_event_source *s, const struct signalfd_siginfo *si, void *userdata)
{
    int sig = si->ssi_signo;

    struct vrtd *state = userdata;
    assert(state != NULL);

    // Log or act based on the signal
    switch (sig) {
    case SIGINT:
    case SIGTERM: {
        // Stop the event loop gracefully
        sd_event *event = sd_event_source_get_event(s);
        if (event) {
            sd_event_exit(event, 0);
        }
        break;
    }

    case SIGHUP: {
        reload_config(state);
        break;
    }

    default: {
        (void) sd_journal_print(LOG_WARNING, "Unhandled signal: %s (%d)\n", sigabbrev_np(sig), sig);

        break;
    }
    }

    return 0;
}

int reload_config(struct vrtd *state)
{
    for (size_t i = 0; i < state->clients.len; i++) {
        struct client *client = state->clients.d[i];
        assert(client != NULL);

        cleanup_rolep(&client->role);
    }

    cleanup_configp(&state->config);

    int ret = config_load(&state->config);
    PROPAGATE_ERROR(ret);

    return 0;
}
