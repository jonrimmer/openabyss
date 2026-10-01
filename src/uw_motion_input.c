/* SPDX-License-Identifier: MIT */
/* the pass's input: input_tick's mouse arm and its hotspot handlers, the
 * key handlers and the ends of their button-release waits
 * (uw_motion_input_click, uw_motion_input_key and the waits' ends).
 *
 * One of src/uw_motion*.c: see uw_motion.h and uw_motion_int.h. */
#include "uw_motion_int.h"

/* active_spell_click, the active spell icons' hotspot:
 * outside action_states 1..3, the slot 2 - (the event's
 * x >> 4), if a spell holds it -- the left button cancels it
 * (active_spell_expire, player_state_recalc), the right names it (string
 * block 6, the icon + 0x180) and bands what is left of its duration (under
 * 3, under 11, more: messages 0x89..0x8b); then the release wait. */
static void active_spell_click(uw_motion *m) {
    uint8_t *ds = m->ds;
    uint16_t rec = rw(ds, PLAYER_RECORD_PTR), ev = rw(ds, 0x00e2), slot, left;
    int16_t i;
    if (rs(ds, ACTION_STATE_WORD) >= 1 && rs(ds, ACTION_STATE_WORD) <= 3) return;
    slot = (uint16_t)(2 - (rs(ds, ev) >> 4));
    if (slot >= ((rw(ds, (uint16_t)(rec + 0x5f)) >> 6) & 0xf)) return;
    if (!(rw(ds, (uint16_t)(ev + 6)) & 2)) {
        i = (int16_t)slot;
        if (active_spell_expire(m, &i)) player_state_recalc(m);
    } else {
        uint16_t e = rw(ds, (uint16_t)(rec + 0x3e + slot * 2));
        uint8_t icon = (uint8_t)(ds[(uint16_t)(SPELL_ICON_BASE + (e & 0xf))] + ((e & 0xf0) >> 4));
        print_string(m, (uint16_t)((icon + 0x180) | 0xc00));
        left = (uint16_t)(e >> 8);
        print_message(m, (uint16_t)((left < 3 ? 0 : left < 0xb ? 1 : 2) + 0x89));
    }
    input_wait_button_release(m, 1);
}

/* input_bind_hotspot(x1, y1, x2, y2, param, mask, handler) ->
 * id: an 18-byte record appended to the table (0x24a4, its count 0x24a8)
 * under the next hotspot id (0x24a2, counting up; the key table's count
 * down, so an id's sign names its table). The original grows the block
 * with realloc at each bind; the port's stays where it is, and no bind
 * takes it past the records a leave gave back. */
int16_t input_bind_hotspot(uw_motion *m, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                           int16_t param, uint16_t mask, uint16_t off, uint16_t seg) {
    uint8_t *ds = m->ds;
    uint16_t n = rw(ds, 0x24a8), r = (uint16_t)(rw(ds, 0x24a4) + n * 0x12), id = rw(ds, 0x24a2);
    ww(ds, 0x24a8, (uint16_t)(n + 1));
    ww(ds, r, id);
    ww(ds, 0x24a2, (uint16_t)(id + 1));
    ww(ds, (uint16_t)(r + 0xa), (uint16_t)param);
    ww(ds, (uint16_t)(r + 0xc), mask);
    ww(ds, (uint16_t)(r + 0xe), off);
    ww(ds, (uint16_t)(r + 0x10), seg);
    ww(ds, (uint16_t)(r + 6), (uint16_t)x1);
    ww(ds, (uint16_t)(r + 8), (uint16_t)y1);
    ww(ds, (uint16_t)(r + 2), (uint16_t)x2);
    ww(ds, (uint16_t)(r + 4), (uint16_t)y2);
    return (int16_t)id;
}

/* input_unbind(id): 0 nothing; a negative id the key table's
 * 12-byte records (0x24b4, count 0x24a6), a positive one the hotspots'
 * 18-byte ones -- the record found, the table's last copied over it unless
 * it is the last, and the count one less. A key id not found is reported
 * (debug_printf, "bad kbd hndl"), a hotspot's dropped. */
void input_unbind(uw_motion *m, int16_t id) {
    uint8_t *ds = m->ds;
    uint16_t base, count_at, stride, n, k;
    if (!id) return;
    if (id < 1) { base = rw(ds, 0x24b4); count_at = 0x24a6; stride = 0xc; }
    else        { base = rw(ds, 0x24a4); count_at = 0x24a8; stride = 0x12; }
    n = rw(ds, count_at);
    for (k = 0; k < n && rs(ds, (uint16_t)(base + k * stride)) != id; k++) { }
    if (k == n) return;
    if (k + 1 < n) memmove(ds + (uint16_t)(base + k * stride), ds + (uint16_t)(base + (n - 1) * stride), stride);
    ww(ds, count_at, (uint16_t)(n - 1));
}

/* A clicked hotspot's handler, by its runtime address, up to its button
 * release wait or (`after`) from it on. */
static int hotspot_handler_run(uw_motion *m, uint32_t handler, int16_t param, int after) {
    if (handler == 0x35c00334u) { if (!after) key_step_or_turn(m, param); }
    else if (handler == 0x622f0020u) {
        /* the rune shelf: the cast after its release wait */
        if (!after) spell_cast_from_shelf_wait(m, param);
        else spell_cast_from_shelf_rest(m);
    }
    else if (handler == 0x622f0052u) { if (!after) active_spell_click(m); }
    else if (handler == 0x2b13010fu) { if (!after) print_flask_status(m); }
    else if (handler == 0x2b130032u) { if (!after) print_time_status(m); }
    else if (handler == 0x2b1313d5u) panel_button_click(m, param, after);
    else if (handler == 0x2b130effu) { if (!after) view_action_dispatch(m); }
    else if (handler == 0x2b13131du) { if (!after) panel_inventory_click(m); }
    else if (handler == 0x61a20070u) { if (!after) barter_click_npc_slot(m); else uw_motion_barter_wait_end(m); }
    else if (handler == 0x61a20084u) { if (!after) barter_click_player_slot(m); else uw_motion_barter_wait_end(m); }
    else if (handler == 0x61c00075u) {
        /* conv_menu_choose(0): the option
         * under the cursor, chosen by the shell's conversation (src/uw_talk.c) */
        if (!after) { m->menu_choose = 1; m->menu_option = 0; }
    }
    else return 0;
    return 1;
}

void uw_motion_input_click(uw_motion *m, int16_t code, int down) {
    uint8_t *ds = m->ds;
    uint16_t ev = rw(ds, 0x00e2), table = rw(ds, 0x24a4);
    int16_t x, y, i;
    /* input_poll_source: the text window marked dirty (the source
     * toggle it flips is left: the release wait's polls flip it as often as
     * they run); mouse_read: the pending code consumed -- with the
     * button up, delivered with the click latch active; still down, the live
     * mask, which is the code, and the latch as it was */
    ds[0x0a8f] = 1;
    ww(ds, 0x0115, 0xffff);
    if (!down) ds[0x011d] = 1;
    ww(ds, 0x011e, (uint16_t)code);
    /* cursor_get_pos: the latched position, or the cursor's */
    x = rs(ds, ds[0x011d] ? 0x0117 : CURSOR_X);
    y = rs(ds, ds[0x011d] ? 0x0119 : CURSOR_Y);
    ww(ds, (uint16_t)(ev + 6), (uint16_t)code);
    ww(ds, (uint16_t)(ev + 4), 1);
    for (i = (int16_t)(rs(ds, 0x24a8) - 1); i >= 0; i--) {
        uint16_t r = (uint16_t)(table + i * 0x12);
        uint32_t handler = (uint32_t)rw(ds, (uint16_t)(r + 0x10)) << 16 | rw(ds, (uint16_t)(r + 0xe));
        if (x < rs(ds, (uint16_t)(r + 6)) || y < rs(ds, (uint16_t)(r + 8)) || rs(ds, (uint16_t)(r + 2)) < x
            || rs(ds, (uint16_t)(r + 4)) < y || !(rw(ds, (uint16_t)(r + 0xc)) & rw(ds, (uint16_t)(ev + 8))) || !handler)
            continue;
        ww(ds, ev, (uint16_t)(x - rs(ds, (uint16_t)(r + 6))));
        ww(ds, (uint16_t)(ev + 2), (uint16_t)(y - rs(ds, (uint16_t)(r + 8))));
        /* the handler by its runtime address, with the record's parameter */
        if (!hotspot_handler_run(m, handler, rs(ds, (uint16_t)(r + 0xa)), 0))
            UW_NOT_CARRIED(m->not_carried);
        return;
    }
}

/* bounded_counter_step(&value, limit, step, dir): with dir -1
 * the value less the step must stay at or above the limit, otherwise the
 * value plus the step at or below it; only then value += dir * step. 1 when
 * it moved. */
static int bounded_counter_step(uw_motion *m, uint16_t at, int16_t limit, int16_t step, int16_t dir) {
    int16_t v = rs(m->ds, at);
    if (dir == -1 ? limit > (int16_t)(v - step) : (int16_t)(v + step) > limit) return 0;
    ww(m->ds, at, (uint16_t)(v + dir * step));
    return 1;
}

/* view_angle_step(&angle, dir, limit):
 * dir 0 resets the angle; a limit of 0 adds dir * 0x400 unbounded; otherwise
 * bounded_counter_step by 0x400 toward +limit, or -limit for dir -1 --
 * nothing when refused. Any change posts event 2. */
static void view_angle_step(uw_motion *m, uint16_t at, int16_t dir, int16_t limit) {
    uint8_t *ds = m->ds;
    if (dir == 0) {
        ww(ds, PENDING_EVENTS, (uint16_t)(rw(ds, PENDING_EVENTS) | 2));
        ww(ds, at, 0);
        return;
    }
    if (limit == 0) {
        ww(ds, at, (uint16_t)(rw(ds, at) + dir * 0x400));
    } else if (!bounded_counter_step(m, at, dir == -1 ? (int16_t)-limit : limit, 0x400, dir)) {
        return;
    }
    ww(ds, PENDING_EVENTS, (uint16_t)(rw(ds, PENDING_EVENTS) | 2));
}

/* view_pitch_step(dir), the 1, 2 and 3 keys: view_pitch,
 * or free_camera_pitch with the free camera on, stepped within a sixteenth of a turn each way. */
static void view_pitch_step(uw_motion *m, int16_t dir) {
    view_angle_step(m, m->ds[0x0762] ? 0x358e : 0x3588, dir, 0x1000);
}

/* print_game_version, Alt-F7: message 0x113 ("Ultima
 * Underworld: The Stygian Abyss v") and the version string "F1.94S\n". */
static void print_game_version(uw_motion *m) {
    print_message(m, 0x113);
    uw_motion_print_ds_string(m, 0x1b39);
}

/* debug_show_position, Alt-F8: the level as a digit, then the
 * player's tile x and y each as two -- its eighths' count and the tile
 * within it, `+ '0'` on both -- and a newline. */
static void debug_show_position(uw_motion *m) {
    uint8_t *ds = m->ds;
    uint16_t pos = rw(m->lseg, (uint16_t)(rw(ds, TRACKED_OBJECT) + 0x16));
    uint8_t x = (uint8_t)(pos >> 10), y = (uint8_t)((pos & 0x3f0) >> 4);
    char text[7];
    text[0] = (char)(ds[CURRENT_LEVEL_WORD] + '0');
    text[1] = (char)((x >> 3) + '0');
    text[2] = (char)((x & 7) + '0');
    text[3] = (char)((y >> 3) + '0');
    text[4] = (char)((y & 7) + '0');
    text[5] = '\n';
    text[6] = 0;
    scroll_print(m, text);
}

/* A key binding's handler by its runtime address: movement_set_mode,
 * key_step_or_turn, panel_button_click, combat_swing, view_pitch_step,
 * announce_skill_by_index, try_sleep and spell_cast_from_shelf (through
 * their overlay stubs) with
 * the record's parameter, panel_set_mode_icon on F7, and Alt-F7's and
 * Alt-F8's printouts, cursor_key_move, Alt-q's screenshot (asked of the
 * host) and Alt-F4's debugger trampoline (nothing); 0 for a handler the
 * binding tables do not hold. panel_button_click's
 * release wait, with no button down, ends at its first poll, and its rest
 * runs. */
static int key_handler_run(uw_motion *m, uint32_t handler, int16_t param) {
    if (handler == 0x23000e29u) cursor_key_move(m, param);   /* the keypad's and Tab's */
    else if (handler == 0x62050020u) m->screenshot = 1;      /* screenshot_write_gif, Alt-q: the host's */
    else if (handler == 0x62590020u) { }                     /* debug_int0c_trampoline, Alt-F4: a call through
                                                              * INT 0Ch's vector, the serial port's -- a debugger's hook,
                                                              * nothing a port has behind it */
    else if (handler == 0x35c0004eu) movement_set_mode(m, param);
    else if (handler == 0x62770052u) view_pitch_step(m, param);
    else if (handler == 0x629a0093u) announce_skill_by_index(m, param);
    else if (handler == 0x62770061u) print_game_version(m);      /* Alt-F7 */
    else if (handler == 0x62770066u) debug_show_position(m);     /* Alt-F8 */
    else if (handler == 0x629a008eu) try_sleep(m, 0x9574);
    else if (handler == 0x2b13000fu) panel_set_mode_icon(m);   /* F7, which asks for the stats view */
    else if (handler == 0x622f0020u) spell_cast_from_shelf(m, param);
    else if (handler == 0x35c00334u) key_step_or_turn(m, param);
    else if (handler == 0x2b1313d5u) {
        panel_button_click(m, param, 0);
        panel_button_click(m, param, 1);
    }
    else if (handler == 0x294511bau) combat_swing(m, param);
    else if (handler == 0x6261007fu) {
        /* options_open_from_key: the shell's
         * panel (src/uw_options.c) for the key's setting */
        m->options_open = 1;
        m->options_key = (uint16_t)param;
    }
    else if (handler == 0x61c00075u) {
        /* conv_menu_choose(n): the digit keys 1..4 of mode 4 */
        m->menu_choose = 1;
        m->menu_option = param;
    }
    else if (handler == 0x61c0005cu) { /* conv_screen_noop: Escape in mode 4 */ }
    else if (handler == 0x61f7003eu) ww(m->ds, 0x5666, 0);   /* game_request_quit: game_running cleared, Alt-x */
    else return 0;
    return 1;
}

void uw_motion_key_press(uw_motion *m, uint8_t at) {
    uint8_t next = (uint8_t)((m->key_ring_wr + 1) % sizeof m->key_ring);
    if (next == m->key_ring_rd) return;
    m->key_ring[m->key_ring_wr] = at;
    m->key_ring_wr = next;
}

int uw_motion_key_take(uw_motion *m, uint8_t *at) {
    if (m->key_ring_rd == m->key_ring_wr) return 0;
    *at = m->key_ring[m->key_ring_rd];
    m->key_ring_rd = (uint8_t)((m->key_ring_rd + 1) % sizeof m->key_ring);
    return 1;
}

void uw_motion_keyboard_drain(uw_motion *m) {
    m->key_ring_rd = m->key_ring_wr;
}

void uw_motion_input_key(uw_motion *m, uint16_t code, uint16_t index) {
    uint16_t scan = index & 0x7f;
    /* WASD is sampled continuously by the immersive movement path. In
     * particular, shifted A/D must not invoke the original step/turn keys. */
    if (m->immersive && !(code & 0x300)
        && (scan == 0x11 || scan == 0x1f || scan == 0x1e || scan == 0x20)) return;
    uint8_t *ds = m->ds;
    uint16_t ev = rw(ds, 0x00e2), table = rw(ds, 0x24b4);
    int16_t i;
    ds[0x0a8f] = 1;
    ww(ds, 0x0128, index);
    ww(ds, 0x012e, (uint16_t)m->clock);
    ww(ds, 0x0130, (uint16_t)(m->clock >> 16));
    ww(ds, (uint16_t)(ev + 4), 0);
    for (i = 0; i < rs(ds, 0x24a6); i++) {
        uint16_t r = (uint16_t)(table + i * 0xc);
        uint32_t handler = (uint32_t)rw(ds, (uint16_t)(r + 0xa)) << 16 | rw(ds, (uint16_t)(r + 8));
        if (rw(ds, (uint16_t)(r + 2)) != code || !(rw(ds, (uint16_t)(r + 6)) & rw(ds, (uint16_t)(ev + 8))) || !handler)
            continue;
        if (!key_handler_run(m, handler, rs(ds, (uint16_t)(r + 4))))
            UW_NOT_CARRIED(m->not_carried);
        return;
    }
}

void uw_motion_input_release(uw_motion *m, uint16_t held) {
    uint8_t *ds = m->ds;
    ww(ds, 0x011b, 0);
    ww(ds, 0x011e, held);
    if (held) return;
    ww(ds, 0x0117, rw(ds, CURSOR_X));
    ww(ds, 0x0119, rw(ds, CURSOR_Y));
}

void uw_motion_view_wait_end(uw_motion *m, uint16_t ret) {
    m->view_wait = 0;
    if (ret == 0x0e49) action_use(m, 1);
    else if (ret == 0x0e35) m->ds[LOOK_IN_PROGRESS] = 0;      /* action_look ends with its wait */
    else UW_NOT_CARRIED(m->not_carried);
}

void uw_motion_input_wait_end(uw_motion *m) {
    uint8_t *ds = m->ds;
    uint16_t ev = rw(ds, 0x00e2), table = rw(ds, 0x24a4);
    int16_t x = rs(ds, 0x0117), y = rs(ds, 0x0119), i;
    for (i = (int16_t)(rs(ds, 0x24a8) - 1); i >= 0; i--) {
        uint16_t r = (uint16_t)(table + i * 0x12);
        uint32_t handler = (uint32_t)rw(ds, (uint16_t)(r + 0x10)) << 16 | rw(ds, (uint16_t)(r + 0xe));
        if (x < rs(ds, (uint16_t)(r + 6)) || y < rs(ds, (uint16_t)(r + 8)) || rs(ds, (uint16_t)(r + 2)) < x
            || rs(ds, (uint16_t)(r + 4)) < y || !(rw(ds, (uint16_t)(r + 0xc)) & rw(ds, (uint16_t)(ev + 8))) || !handler)
            continue;
        if (!hotspot_handler_run(m, handler, rs(ds, (uint16_t)(r + 0xa)), 1)) UW_NOT_CARRIED(m->not_carried);
        return;
    }
}

void uw_motion_action_combat(uw_motion *m, int16_t cell) {
    combat_swing(m, cell);
}

/* Relative mouse angles use the same camera cells as the original keys.
 * Keep the player object's facing in sync for movement and combat queries. */
void uw_motion_free_look(uw_motion *m, int yaw, int pitch) {
    uint8_t *ds = m->ds;
    uint16_t pa = ds[0x0762] ? 0x358e : VIEW_PITCH;
    int angle = rs(ds, pa) + pitch;
    if (angle < -0x1000) angle = -0x1000;
    if (angle > 0x1000) angle = 0x1000;
    ww(ds, pa, (uint16_t)angle);
    if (ds[0x0762]) {
        ww(ds, 0x358c, (uint16_t)(rw(ds, 0x358c) + yaw));
    } else {
        uint16_t h = (uint16_t)(rw(ds, PLAYER_HEADING) + yaw);
        uint16_t obj = rw(ds, TRACKED_OBJECT), rec = rw(ds, PLAYER_RECORD_PTR);
        ww(ds, PLAYER_HEADING, h);
        ww(ds, (uint16_t)(rec + 0x5a), h);
        ww(m->lseg, (uint16_t)(obj + 2),
           (uint16_t)((rw(m->lseg, (uint16_t)(obj + 2)) & 0xfc7f) | ((h >> 13) << 7)));
        m->lseg[(uint16_t)(obj + 0x18)] =
            (uint8_t)((m->lseg[(uint16_t)(obj + 0x18)] & 0xe0) | ((h >> 8) & 0x1f));
    }
    ww(ds, PENDING_EVENTS, (uint16_t)(rw(ds, PENDING_EVENTS) | 2));
}
