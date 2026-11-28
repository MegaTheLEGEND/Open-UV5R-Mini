/* Open-UV5R-Mini: firmware name and version, shown in MENU -> ABOUT.
 *
 * FW_VERSION is raised by hand when a build is handed out (major.minor.patch). FW_BUILD is the git commit the image
 * was built from, set by the Makefile ("-dirty" if there were uncommitted changes); "dev" when built without git. */
#ifndef APP_VERSION_H
#define APP_VERSION_H

#define FW_NAME    "OPEN UV5R MINI"
#define FW_VERSION "1.0.1"
#define FW_TARGET  "NRF V0.27 HW V1.7"   /* the only kind of radio this build is for (AGENTS.md rule 4) */

const char *VERSION_Build(void);

#endif
