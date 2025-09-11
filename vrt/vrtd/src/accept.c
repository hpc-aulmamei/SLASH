#define _GNU_SOURCE

#include "accept.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <grp.h>
#include <pwd.h>
#include <syslog.h>
#include <systemd/sd-event.h>
#include <systemd/sd-journal.h>

#include "utils.h"
#include "serve.h"
#include "state.h"

static int create_client_event(sd_event_source *listener_event_source, int cfd, struct vrtd *state, struct client **clientp);
static int populate_uid_gid(int cfd, struct client *client);

int on_event_new_connection(sd_event_source *s, int fd, uint32_t revents, void *userdata)
{
    struct vrtd *state = userdata;

    assert(state != NULL);

    if (!(revents & EPOLLIN)) {
        return 0;
    }

    for (;;) {
        struct sockaddr_un peer;
        socklen_t peerlen = sizeof(peer);
        int cfd = accept4(fd, (struct sockaddr*)&peer, &peerlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (cfd == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  /* all pending connections accepted */
            }
            (void) sd_journal_print(LOG_ERR, "accept4() failed: %m");
            return -1;
        }

        _cleanup_(cleanup_clientp)
        struct client *client;
        int ret = create_client_event(s, cfd, state, &client);
        if (ret == -1) {
            close(cfd);
            continue;
        }

        assert(client != NULL);

        ret = client_ptr_array_push_move(&state->clients, &client);
        if (ret == -1) {
            (void) sd_journal_print(LOG_ERR, "Failed to allocate memory when adding new client");
            continue;
        }
    }

    return 0;
}


static int create_client_event(sd_event_source *listener_event_source, int cfd, struct vrtd *state, struct client **clientp)
{
    *clientp = calloc(1, sizeof **clientp);
    PROPAGATE_ERROR_NULL_STDC_LOG(clientp, LOG_ERR, "Out of memory allocating client data");

    _cleanup_(cleanup_clientp)
    struct client *client = *clientp;

    _cleanup_(cleanup_free)
    char *description = NULL;

    // If something fails, we should disable + unref.
    _cleanup_(sd_event_source_disable_unrefp)
    sd_event_source *source = NULL;
    
    sd_event *ev = sd_event_source_get_event(listener_event_source);
    PROPAGATE_ERROR_NULL_LOG(ev, LOG_ERR, "Failed to get event for source");

    int ret = sd_event_add_io(ev, &source, cfd, EPOLLIN | EPOLLRDHUP, on_client_io, client);
    PROPAGATE_ERROR_SD_LOG(ret, LOG_ERR, "Failed to add client as event source");

    {
        struct ucred cred;
        socklen_t clen = sizeof(cred);
        if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cred, &clen) == 0) {
            ret = asprintf(&description, "client fd=%d pid=%d uid=%d gid=%d",
                            cfd, (int)cred.pid, (int)cred.uid, (int)cred.gid);
        } else {
            ret = asprintf(&description, "client fd=%d", cfd);
        }
    }
    PROPAGATE_ERROR_STDC_LOG(ret, LOG_ERR, "Failed to allocate description for client");

    ret = populate_uid_gid(cfd, client);
    PROPAGATE_ERROR_STDC_LOG(ret, LOG_ERR, "Failed to obtain user/group information for lcient");

    ret = sd_event_source_set_description(source, description);
    PROPAGATE_ERROR_STDC_LOG(ret, LOG_ERR, "Could not set description for client fd");

    client->fd = cfd;
    client->state = state;
    client->event_source = source;

    // Nothing went wrong. Do not unref.
    source = NULL;

    // Nothing went wrong. Do not remove client.
    client = NULL;

    return 0;
}

static
int populate_uid_gid(int cfd, struct client *client)
{
    if (!client || cfd < 0) {
        sd_journal_print(LOG_ERR, "populate_uid_gid: invalid arguments");
        return -1;
    }

    // --- Identify peer (SO_PEERCRED) ---
    struct ucred cred = {0};
    socklen_t len = sizeof(cred);
    int rc = getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cred, &len);
    PROPAGATE_ERROR_STDC_LOG(rc, LOG_ERR, "SO_PEERCRED failed");

    uid_t new_uid = cred.uid;

    // --- Lookup passwd entry for username (for getgrouplist) ---
    long buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
    if (buflen <= 0 || buflen > (1 << 20)) buflen = 16384;

    _cleanup_(cleanup_free) char *pwbuf = malloc((size_t)buflen);
    PROPAGATE_ERROR_NULL_STDC_LOG(pwbuf, LOG_ERR, "malloc pwbuf");

    struct passwd pwent, *pw = NULL;
    int pr = getpwuid_r(new_uid, &pwent, pwbuf, (size_t)buflen, &pw);
    if (pr != 0 || !pw) {
        sd_journal_print(LOG_ERR, "getpwuid_r(%u) failed: %s",
                         (unsigned)new_uid,
                         pr ? strerrordesc_np(pr) : "not found");
        return -1;
    }

    // --- Probe exact group count, then allocate exactly that many ---
    int ngroups = 0;
    (void)getgrouplist(pw->pw_name, cred.gid, NULL, &ngroups); // expected to return -1
    if (ngroups <= 0) {
        sd_journal_print(LOG_ERR, "getgrouplist probe returned non-positive size for user %s", pw->pw_name);
        return -1;
    }

    _cleanup_(cleanup_free) gid_t *groups = malloc((size_t)ngroups * sizeof(gid_t));
    PROPAGATE_ERROR_NULL_STDC_LOG(groups, LOG_ERR, "malloc groups[%d]", ngroups);

    int gl = getgrouplist(pw->pw_name, cred.gid, groups, &ngroups);
    if (gl < 0 || ngroups <= 0) {
        sd_journal_print(LOG_ERR, "getgrouplist fetch failed for user %s", pw->pw_name);
        return -1;
    }

    // --- Commit atomically: reset gids, set uid, then push all groups ---
    // Leave client untouched until now; from here on, rollbacks are manual.
    gid_t_array_free(&client->gids);
    client->uid = new_uid;

    for (int i = 0; i < ngroups; ++i) {
        int r = gid_t_array_push(&client->gids, groups[i]);
        if (r == -1) {
            sd_journal_print(LOG_ERR, "gid_t_array_push failed at index %d", i);
            // Roll back to consistent "unset" state
            gid_t_array_free(&client->gids);
            client->uid = (uid_t)-1;
            return -1;
        }
    }

    return 0;
}


