/* Open-UV5R-Mini small C library (tools/sdk/README.md). The SDK's generic/errno-base.h has the E* codes. */
#ifndef MINILIBC_ERRNO_H
#define MINILIBC_ERRNO_H

#undef errno            /* a host C library's macro, when tests/sdk_libc_host.py builds this on a PC */
extern int errno;
#ifndef EDOM
#define EDOM 33
#endif
#ifndef ERANGE
#define ERANGE 34
#endif
#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef ENOMEM
#define ENOMEM 12
#endif

#endif
