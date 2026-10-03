/* SPDX-License-Identifier: MIT */
#include "uw_console.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void uw_console_open(uw_console *c) {
    c->active = 1;
    c->pos = 0;
    c->input[0] = 0;
    snprintf(c->message, sizeof c->message, "god | affine, widescreen, bright on/off");
}

void uw_console_text(uw_console *c, const char *text) {
    int len = (int)strlen(c->input);
    if (!c->active) return;
    for (; *text; text++) {
        unsigned char ch = (unsigned char)*text;
        if (ch == '\t') ch = ' ';
        if (ch < 32 || ch > 126 || len >= (int)sizeof c->input - 1) continue;
        memmove(c->input + c->pos + 1, c->input + c->pos, (size_t)(len - c->pos + 1));
        c->input[c->pos++] = (char)ch;
        len++;
    }
}

static int submit(uw_console *c, uint8_t *god_mode, int *non_affine,
                  int *widescreen, int *full_bright) {
    char cmd[16], arg[16], extra[2];
    char text[sizeof c->input];
    int i, n, changed = 0;
    for (i = 0; c->input[i]; i++) text[i] = (char)tolower((unsigned char)c->input[i]);
    text[i] = 0;
    n = sscanf(text, "%15s %15s %1s", cmd, arg, extra);
    if (n == 1 && !strcmp(cmd, "god")) {
        *god_mode = !*god_mode;
        snprintf(c->message, sizeof c->message, "God mode %s", *god_mode ? "on" : "off");
        changed = 1;
    } else if (n == 2 && !strcmp(cmd, "affine")
               && (!strcmp(arg, "on") || !strcmp(arg, "off"))) {
        *non_affine = !strcmp(arg, "off");
        snprintf(c->message, sizeof c->message, "Affine rendering %s", *non_affine ? "off" : "on");
        changed = 1;
    } else if (n == 2 && !strcmp(cmd, "widescreen")
               && (!strcmp(arg, "on") || !strcmp(arg, "off"))) {
        *widescreen = !strcmp(arg, "on");
        snprintf(c->message, sizeof c->message, "Widescreen %s", *widescreen ? "on" : "off");
        changed = 1;
    } else if (n == 2 && !strcmp(cmd, "bright")
               && (!strcmp(arg, "on") || !strcmp(arg, "off"))) {
        *full_bright = !strcmp(arg, "on");
        snprintf(c->message, sizeof c->message, "Full bright rendering %s", *full_bright ? "on" : "off");
        changed = 1;
    } else if (n > 0) {
        snprintf(c->message, sizeof c->message, "Use: god | affine, widescreen, bright on/off");
    }
    c->input[0] = 0;
    c->pos = 0;
    return changed;
}

int uw_console_key(uw_console *c, int key, uint8_t *god_mode, int *non_affine,
                   int *widescreen, int *full_bright) {
    int len = (int)strlen(c->input);
    if (!c->active) return 0;
    switch (key) {
    case '\r': return submit(c, god_mode, non_affine, widescreen, full_bright);
    case '\b':
        if (c->pos > 0) {
            memmove(c->input + c->pos - 1, c->input + c->pos, (size_t)(len - c->pos + 1));
            c->pos--;
        }
        break;
    case UW_CONSOLE_DELETE:
        if (c->pos < len)
            memmove(c->input + c->pos, c->input + c->pos + 1, (size_t)(len - c->pos));
        break;
    case UW_CONSOLE_LEFT: if (c->pos > 0) c->pos--; break;
    case UW_CONSOLE_RIGHT: if (c->pos < len) c->pos++; break;
    case UW_CONSOLE_HOME: c->pos = 0; break;
    case UW_CONSOLE_END: c->pos = len; break;
    }
    return 0;
}
