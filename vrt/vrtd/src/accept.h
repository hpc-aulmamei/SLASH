#ifndef VRTD_ACCEPT_H
#define VRTD_ACCEPT_H

#include <stdint.h>

#include <systemd/sd-event.h>

int on_event_new_connection(sd_event_source *s, int fd, uint32_t revents, void *userdata);

#endif // VRTD_ACCEPT_H
