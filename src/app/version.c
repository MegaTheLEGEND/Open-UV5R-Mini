/* Open-UV5R-Mini: the build id (see app/version.h). The Makefile rebuilds this file every time. */
#include "app/version.h"

#ifndef FW_BUILD
#define FW_BUILD "dev"
#endif

const char *VERSION_Build(void) { return FW_BUILD; }
