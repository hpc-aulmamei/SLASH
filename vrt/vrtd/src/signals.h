#ifndef VRTD_SIGNALS_H
#define VRTD_SIGNALS_H

#include <systemd/sd-event.h>

int on_event_signal(sd_event_source *s, const struct signalfd_siginfo *si, void *userdata);

#endif // VRTD_SIGNALS_H
