/*
 * screen_estop.h  —  Emergency-stop full-screen banner
 *
 *  Displayed whenever ESTOP_IsActive() returns true. Inverts the display
 *  every ~400 ms so the banner flashes; prints the reason byte (decoded
 *  from FAULT_ESTOP_* codes in comm_protocol.h) and the clear sequence
 *  hint "[*] then [#]".
 */

#ifndef SCREEN_ESTOP_H
#define SCREEN_ESTOP_H

#include <stdint.h>

typedef enum {
    SCREEN_ESTOP_ACTION_NONE = 0,
    SCREEN_ESTOP_ACTION_CLEAR   /* operator completed the * → # sequence */
} ScreenEstop_Action_t;

void                 ScreenEstop_Draw(void);
ScreenEstop_Action_t ScreenEstop_HandleKey(uint8_t key);

/* Reset the internal "* pressed" flag (call whenever the screen is entered). */
void                 ScreenEstop_Reset(void);

#endif /* SCREEN_ESTOP_H */
