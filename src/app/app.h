/* Open-UV5R-Mini: application entry points, shared by the bare-metal build and the JieLi-SDK task. */
#ifndef APP_APP_H
#define APP_APP_H

#define APP_TICK_MS 10

void APP_Init(void);
void APP_Tick(void); /* call every APP_TICK_MS */

#endif
