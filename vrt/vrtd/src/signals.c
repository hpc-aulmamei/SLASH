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
