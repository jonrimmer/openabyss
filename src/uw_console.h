/* SPDX-License-Identifier: MIT */
#ifndef UW_CONSOLE_H
#define UW_CONSOLE_H
#include <stdint.h>

typedef struct {
    int active, pos;
    char input[52];
    char message[80];
} uw_console;

enum {
    UW_CONSOLE_LEFT = 0x100, UW_CONSOLE_RIGHT, UW_CONSOLE_HOME,
    UW_CONSOLE_END, UW_CONSOLE_DELETE
};

void uw_console_open(uw_console *c);
void uw_console_text(uw_console *c, const char *text);
/* Enter executes the field; other keys edit it. Returns 1 for a command
 * that changed a setting. Commands are case-insensitive and ignore extra
 * whitespace. Rendering and widescreen settings are host preferences. */
int uw_console_key(uw_console *c, int key, uint8_t *god_mode, int *non_affine,
                   int *widescreen, int *full_bright);
#endif
