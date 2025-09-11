# vrtd

## Coding guildelines

**READ THIS SECTION BEFORE WRITING C CODE FOR THIS DAEMON**

This daemon is not written using "standard/POSIX C", but instead leans
heavily on C11, libsystemd, glibc features, Linux syscall features and
GNU compiler extensions (also supported by Clang). The goal is not to
write a "portable" application, but a modern systemd daemon, using all
the tools at our disposal.

The minimum required versions are those shipped by Ubuntu 22.04 LTS:

* cmake 3.22.1
* GCC 11.4.0
* glibc 2.35
* Linux 5.15.0
* libsystemd 249.11

Lower versions might work, but have not been tested, and may stop
working in any update to vrtd. Developers contributing to vrtd are
encouraged to make use of useful extensions and capabilities as long as
they are supported by the versions mentioned above.

The language versions is C17. C23 features should not be used unless
they are available as GNU extensions (`-std=gnu17`).

All `.c` source files should start with `#define _GNU_SOURCE` after the
copyright message and before including any other headers.

***IMPORTANT!*** For conciseness, consistency and clarity of code, vrtd
uses the conventions described below accross the codebase. Please
follow them for any code contributed.

