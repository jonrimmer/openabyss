/* SPDX-License-Identifier: MIT */
/* Include the host to exercise its actual event routing and pause loop. */
#define SDL_MAIN_HANDLED
#define main uw_test_program_main
#include "../src/tools/uwshell.c"
#undef main
#define rw motion_test_rw
#define ww motion_test_ww
#include "uw_motion_int.h"
#undef rw
#undef ww

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
    exit(1); } } while (0)

static uw_shell test_shell;
static uint8_t test_ds[65536], test_ls[65536], test_keys[65536];
static uint8_t test_screen[64000], test_written[64000], test_pal[768];
static uint32_t test_frame[64000];
static uint8_t test_font[12 + 128 * 7];

static void reset_shell(void) {
    uw_shell *sh = &test_shell;
    int i;
    memset(sh, 0, sizeof *sh);
    memset(test_ds, 0, sizeof test_ds);
    memset(test_ls, 0, sizeof test_ls);
    memset(test_keys, 0, sizeof test_keys);
    memset(test_written, 7, sizeof test_written);
    for (i = 0; i < 64000; i++) test_screen[i] = (uint8_t)(i % 251);
    memset(test_font, 0, sizeof test_font);
    test_font[0] = 1; test_font[6] = 6; test_font[8] = 1;
    for (i = 0; i < 128; i++) {
        memset(test_font + 12 + i * 7, i == ' ' ? 0 : 0xf0, 6);
        test_font[12 + i * 7 + 6] = 4;
    }
    sh->ds = test_ds; sh->lseg = test_ls; sh->keybuf = test_keys;
    sh->screen = test_screen; sh->pal = test_pal; sh->frame = test_frame;
    sh->m.ds = test_ds; sh->m.lseg = test_ls; sh->m.keys = test_keys;
    sh->m.screen = test_screen; sh->m.screen_written = test_written;
    sh->m.font = test_font; sh->m.font_size = sizeof test_font;
    sh->scroll.m = &sh->m; sh->scroll.font = test_font;
    sh->scroll.font_size = sizeof test_font;
    sh->m.scroll = &sh->scroll;
    sh->have_game = sh->running = 1; sh->mode = MODE_DUNGEON;
    sh->clock0 = sh->m.clock = 1000; sh->t0 = SDL_GetTicksNS();
    sh->pass_start = sh->t0;
    ww(sh->ds, 0x2344, 0x2000);
    ww(sh->ds, 0x2334, 0x2100); ww(sh->ds, 0x233c, 0x2101); ww(sh->ds, 0x2340, 0x2102);
    ww(sh->ds, UW_SCROLL_WINDOW, 30); ww(sh->ds, UW_SCROLL_WINDOW + 2, 1);
    ww(sh->ds, UW_SCROLL_WINDOW + 4, 15); ww(sh->ds, UW_SCROLL_WINDOW + 6, 304);
    ww(sh->ds, 0x0125, 0xffff);
    ww(sh->ds, 0x5666, 1);
    non_affine = 0;
    full_bright = 0;
    widescreen = 1;
}

static int command(uw_console *c, const char *text, uint8_t *god, int *renderer) {
    uw_console_text(c, text);
    return uw_console_key(c, '\r', god, renderer, &widescreen, &full_bright);
}

static void check_commands(void) {
    uw_console c = {0};
    uint8_t god = 0;
    int renderer = 0, i;
    widescreen = 1;
    full_bright = 0;
    uw_console_open(&c);
    CHECK(command(&c, "god", &god, &renderer) && god == 1);
    CHECK(command(&c, "  GOD  ", &god, &renderer) && god == 0);
    CHECK(command(&c, "affine off", &god, &renderer) && renderer == 1);
    CHECK(command(&c, " affine\tON ", &god, &renderer) && renderer == 0);
    CHECK(!command(&c, "affine", &god, &renderer) && renderer == 0);
    CHECK(!command(&c, "affine off extra", &god, &renderer) && renderer == 0);
    CHECK(!command(&c, "god on", &god, &renderer) && god == 0);
    CHECK(!command(&c, "", &god, &renderer));
    CHECK(command(&c, "widescreen off", &god, &renderer) && !widescreen);
    CHECK(!strcmp(c.message, "Widescreen off"));
    CHECK(!command(&c, "widescreen", &god, &renderer) && !widescreen);
    CHECK(!command(&c, "widescreen on extra", &god, &renderer) && !widescreen);
    CHECK(!command(&c, "widescreen yes", &god, &renderer) && !widescreen);
    CHECK(command(&c, " WIDESCREEN\tON ", &god, &renderer) && widescreen);
    CHECK(!strcmp(c.message, "Widescreen on"));
    CHECK(command(&c, "bright on", &god, &renderer) && full_bright);
    CHECK(!strcmp(c.message, "Full bright rendering on"));
    CHECK(!command(&c, "bright", &god, &renderer) && full_bright);
    CHECK(!command(&c, "bright off extra", &god, &renderer) && full_bright);
    CHECK(!command(&c, "bright yes", &god, &renderer) && full_bright);
    CHECK(command(&c, " BRIGHT\tOFF ", &god, &renderer) && !full_bright);
    CHECK(!strcmp(c.message, "Full bright rendering off"));
    uw_console_text(&c, "afine off");
    uw_console_key(&c, UW_CONSOLE_HOME, &god, &renderer, &widescreen, &full_bright);
    uw_console_key(&c, UW_CONSOLE_RIGHT, &god, &renderer, &widescreen, &full_bright);
    uw_console_text(&c, "f");
    CHECK(!strcmp(c.input, "affine off"));
    CHECK(uw_console_key(&c, '\r', &god, &renderer, &widescreen, &full_bright) && renderer == 1);
    uw_console_text(&c, "godx");
    uw_console_key(&c, '\b', &god, &renderer, &widescreen, &full_bright);
    CHECK(!strcmp(c.input, "god"));
    uw_console_key(&c, UW_CONSOLE_LEFT, &god, &renderer, &widescreen, &full_bright);
    uw_console_key(&c, UW_CONSOLE_DELETE, &god, &renderer, &widescreen, &full_bright);
    CHECK(!strcmp(c.input, "go"));
    uw_console_key(&c, UW_CONSOLE_END, &god, &renderer, &widescreen, &full_bright);
    uw_console_text(&c, "d");
    CHECK(uw_console_key(&c, '\r', &god, &renderer, &widescreen, &full_bright) && god == 1);
    for (i = 0; i < 200; i++) uw_console_text(&c, "x");
    CHECK(strlen(c.input) == sizeof c.input - 1 && c.pos == 51);
    c.active = 0;
    uw_console_text(&c, "god");
    CHECK(!uw_console_key(&c, '\r', &god, &renderer, &widescreen, &full_bright));
}

static void key(SDL_Scancode scan, SDL_Keycode symbol, bool repeat) {
    SDL_Event e = {0};
    e.type = SDL_EVENT_KEY_DOWN; e.key.scancode = scan;
    e.key.key = symbol; e.key.repeat = repeat;
    CHECK(SDL_PushEvent(&e));
}

static void text(const char *str) {
    SDL_Event e = {0};
    e.type = SDL_EVENT_TEXT_INPUT; e.text.text = str;
    CHECK(SDL_PushEvent(&e));
}

static void check_pause_and_restore(int immersive) {
    uw_shell *sh = &test_shell;
    uint8_t screen[64000], scroll_state[0x43];
    uint32_t clock;
    SDL_Event e = {0};
    reset_shell();
    sh->immersive = sh->m.immersive = (uint8_t)immersive;
    sh->scroll.npending = 1;
    strcpy(sh->scroll.pending[0].text, "A pending game message");
    memcpy(screen, sh->screen, sizeof screen);
    memcpy(scroll_state, sh->ds + UW_SCROLL_WINDOW, sizeof scroll_state);
    sh->m.buttons = 1; sh->context_click = 1;
    sh->keybuf[0x2100] = sh->keybuf[0x2101] = sh->keybuf[0x2102] = 1;
    sh->keybuf[0x2011] = 1; uw_motion_key_press(&sh->m, 0x11);
    /* A layout-specific section-sign keycode, not the grave scan code. */
    key(SDL_SCANCODE_NONUSBACKSLASH, 0xa7, false);
    host_events(sh);
    CHECK(sh->console.active && sh->console_changed);
    CHECK(!sh->m.buttons && !sh->context_click && !sh->keybuf[0x2011]);
    CHECK(!sh->keybuf[0x2100] && !sh->keybuf[0x2101] && !sh->keybuf[0x2102]);
    CHECK(sh->m.key_ring_rd == sh->m.key_ring_wr);
    CHECK(sh->m.immersive == immersive);
    console_draw(sh);
    CHECK(memcmp(screen, sh->screen, 320 * 160) == 0);
    CHECK(memcmp(screen + 320 * 160, sh->screen + 320 * 160, 320 * 40) != 0);
    CHECK(memcmp(scroll_state, sh->ds + UW_SCROLL_WINDOW, sizeof scroll_state) == 0);
    clock = sh->m.clock;
    SDL_Delay(80);
    host_clock(sh);
    CHECK(sh->m.clock == clock);
    key(SDL_SCANCODE_NONUSBACKSLASH, 0xa7, true);
    key(SDL_SCANCODE_W, SDLK_W, false);
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.button = SDL_BUTTON_LEFT;
    CHECK(SDL_PushEvent(&e));
    text("god"); key(SDL_SCANCODE_RETURN, SDLK_RETURN, false);
    text("affine off"); key(SDL_SCANCODE_RETURN, SDLK_RETURN, false);
    text("widescreen off"); key(SDL_SCANCODE_RETURN, SDLK_RETURN, false);
    host_events(sh);
    CHECK(sh->console.active && sh->m.god_mode && non_affine && !widescreen);
    text("bright on"); key(SDL_SCANCODE_RETURN, SDLK_RETURN, false);
    host_events(sh);
    CHECK(full_bright && (rw(sh->ds, 0x56aa) & 2));
    CHECK(!sh->m.buttons && !sh->keybuf[0x2011]);
    CHECK(sh->m.key_ring_rd == sh->m.key_ring_wr);
    CHECK(sh->scroll.npending == 1);
    CHECK(!strcmp(sh->scroll.pending[0].text, "A pending game message"));
    console_draw(sh);
    key(SDL_SCANCODE_NONUSBACKSLASH, 0xa7, false);
    key(SDL_SCANCODE_W, SDLK_W, false); /* queued input must not leak on close */
    host_events(sh);
    CHECK(!sh->console.active && sh->console_changed && !widescreen && full_bright);
    CHECK(memcmp(screen, sh->screen, sizeof screen) == 0);
    CHECK(memcmp(scroll_state, sh->ds + UW_SCROLL_WINDOW, sizeof scroll_state) == 0);
    CHECK(sh->m.key_ring_rd == sh->m.key_ring_wr);
    CHECK(sh->m.immersive == immersive);
    CHECK(test_written[320 * 180] == 7);
    host_clock(sh);
    CHECK(sh->m.clock - clock <= 4); /* no catch-up after the paused 80 ms */

    /* The real loop must skip event dispatch, physics, NPCs and timers. */
    key(SDL_SCANCODE_GRAVE, SDLK_GRAVE, false);
    host_events(sh);
    clock = sh->m.clock;
    e.type = SDL_EVENT_QUIT; CHECK(SDL_PushEvent(&e));
    game_loop(sh);
    CHECK(sh->m.clock == clock && sh->console.active);
    CHECK(sh->m.immersive == immersive);
    CHECK(sh->m.key_ring_rd == sh->m.key_ring_wr);
    console_toggle(sh);
}

static void check_god_mode(void) {
    uw_motion *m = &test_shell.m;
    uint16_t player = 0x1000, other = 0x1080;
    reset_shell();
    ww(m->ds, TRACKED_OBJECT, player); ww(m->ds, STATIC_BASE, 0x2000);
    ww(m->lseg, player, 0x40); ww(m->lseg, other, 0x40);
    m->lseg[player + 8] = 100; m->lseg[other + 8] = 50;
    m->god_mode = 1;
    CHECK(apply_damage(m, player, 0, 0, 0, 255, 0) == 0);
    CHECK(m->lseg[player + 8] == 100);
    effect_nonlethal_damage(m, player, 10);
    player_fall_damage(m);
    CHECK(m->lseg[player + 8] == 100 && !m->screen_flash);
    CHECK(apply_damage(m, other, 0, 0, 0, 10, 0) == 0);
    CHECK(m->lseg[other + 8] == 40);
    m->lseg[player + 8] = 0;
    player_death(m);
    CHECK(m->lseg[player + 8] == 1 && !m->return_to_menu && !m->screen_fade);
    m->lseg[player + 8] = 100; m->god_mode = 0;
    CHECK(apply_damage(m, player, 0, 0, 0, 10, 0) == 0);
    CHECK(m->lseg[player + 8] == 90);
    effect_nonlethal_damage(m, player, 1);
    CHECK(m->lseg[player + 8] < 90 && m->screen_flash);
    m->screen_flash = 0;
    player_fall_damage(m);
    CHECK(m->screen_flash);
}

static void check_widescreen(void) {
    uw_shell *sh = &test_shell;
    SDL_Surface *surface = SDL_CreateSurface(640, 360, SDL_PIXELFORMAT_XRGB8888);
    SDL_Renderer *ren;
    SDL_FRect rect;
    static uint8_t view[UW_IMMERSIVE_BYTES], pal[768];
    uint8_t r, g, b, a;
    int square;
    CHECK(surface);
    ren = SDL_CreateSoftwareRenderer(surface);
    CHECK(ren);
    reset_shell();
    sh->ren = ren;
    sh->immersive = sh->m.immersive = 1;
    sc = calloc(1, sizeof *sc);
    CHECK(sc);
    memset(view, 1, sizeof view);
    pal[3] = pal[4] = pal[5] = 63;
    for (square = 0; square < 2; square++) {
        /* Fixed 4:3 also applies when the normal UI uses square pixels. */
        present_aspect = !square;
        gameplay_presentation(sh);
        immersive_sync(sh);
        CHECK(sh->m.view_output_width == 640 && sh->m.view_output_height == 360);
        scene_camera(&sh->m);
        CHECK(sc->output_width == 640 && sc->output_height == 360);
        CHECK(SDL_GetRenderLogicalPresentationRect(ren, &rect));
        CHECK(rect.x == 0 && rect.y == 0 && rect.w == 640 && rect.h == 360);
        show_immersive(view, pal, ren, 0);
        CHECK(SDL_ReadSurfacePixel(surface, 0, 180, &r, &g, &b, &a));
        CHECK(r == 255 && g == 255 && b == 255);

        console_toggle(sh);
        text("widescreen off"); key(SDL_SCANCODE_RETURN, SDLK_RETURN, false);
        host_events(sh);
        CHECK(!widescreen && sh->console.active);
        CHECK(rw(sh->ds, 0x56aa) & 2);
        console_toggle(sh);
        scene_camera(&sh->m);
        CHECK(sc->output_width == 4 && sc->output_height == 3);
        CHECK(SDL_GetRenderLogicalPresentationRect(ren, &rect));
        CHECK(rect.x == 80 && rect.y == 0 && rect.w == 480 && rect.h == 360);
        show_immersive(view, pal, ren, 0);
        CHECK(SDL_ReadSurfacePixel(surface, 79, 180, &r, &g, &b, &a));
        CHECK(r == 0 && g == 0 && b == 0);
        CHECK(SDL_ReadSurfacePixel(surface, 80, 180, &r, &g, &b, &a));
        CHECK(r == 255 && g == 255 && b == 255);
        CHECK(SDL_ReadSurfacePixel(surface, 559, 180, &r, &g, &b, &a));
        CHECK(r == 255 && g == 255 && b == 255);
        CHECK(SDL_ReadSurfacePixel(surface, 560, 180, &r, &g, &b, &a));
        CHECK(r == 0 && g == 0 && b == 0);

        /* Leaving immersive mode retains the preference and normal UI aspect. */
        sh->m.immersive = 0;
        gameplay_presentation(sh);
        scene_camera(&sh->m);
        CHECK(sc->output_width == 0 && sc->output_height == 0);
        CHECK(SDL_GetRenderLogicalPresentationRect(ren, &rect));
        CHECK(rect.w == (square ? 576 : 480));
        sh->m.immersive = 1;
        gameplay_presentation(sh);
        CHECK(SDL_GetRenderLogicalPresentationRect(ren, &rect));
        CHECK(rect.w == 480);
        console_toggle(sh);
        text("widescreen on"); key(SDL_SCANCODE_RETURN, SDLK_RETURN, false);
        host_events(sh);
        console_toggle(sh);
        CHECK(widescreen);
        CHECK(SDL_GetRenderLogicalPresentationRect(ren, &rect));
        CHECK(rect.x == 0 && rect.w == 640);
        scene_camera(&sh->m);
        CHECK(sc->output_width == 640 && sc->output_height == 360);
    }
    free(sc); sc = NULL;
    sh->ren = NULL;
    present_aspect = 1;
    SDL_DestroyRenderer(ren);
    SDL_DestroySurface(surface);
}

static const uint8_t *bright_test_art(void *ctx, uint16_t id, size_t *len) {
    static const uint8_t art[] = {4, 2, 2, 0, 4, 0, 200, 200, 200, 200};
    (void)ctx; (void)id;
    *len = sizeof art;
    return art;
}

static void check_bright_distance_ramp(void) {
    uw_vl *v = calloc(1, sizeof *v);
    uint8_t normal[65536], params[6] = {56, 0, 5, 0, 0xfd, 0xff};
    int row, col, preserved = 0, extended = 0;
    CHECK(v);
    v->lprm = params;
    memset(v->mem, 0xa5, sizeof v->mem);
    uw_vl_light_map(v, 3);
    memcpy(normal, v->mem, sizeof normal);
    v->full_bright = 1;
    uw_vl_light_map(v, 3);
    for (row = 0; row < 17; row++) for (col = 0; col < 33; col++) {
        int at = 0x2891 + row * 0x42 + col * 2;
        CHECK(v->mem[at - 1] == normal[at - 1]);
        if (normal[at] != 15) {
            /* These values choose the texture record and coordinate scale,
             * in addition to lighting. Bright mode must preserve them. */
            CHECK(v->mem[at] == normal[at]);
            if (normal[at] > 0) preserved++;
        } else if (v->mem[at] != 15) extended++;
    }
    CHECK(preserved > 0 && extended > 0);
    CHECK(v->mem[0x2891 + 16 * 0x42 + 16 * 2] == 15);
    v->full_bright = 0;
    uw_vl_light_map(v, 3);
    CHECK(memcmp(v->mem, normal, sizeof normal) == 0);
    free(v);
}

static void check_full_bright(void) {
    uint8_t pixels[16 * 16], texels[64 * 64], ramp[4096], shades[96] = {0};
    uint32_t rows[16];
    uw_fb fb = {pixels, sizeof pixels, rows, 16};
    uw_rast_svert face[4] = {
        {2, 2, 0, 0, 0, 0, 100}, {10, 2, 16128, 0, 0, 0, 100},
        {10, 10, 16128, 4032, 0, 0, 400}, {2, 10, 0, 4032, 0, 0, 400}
    };
    uw_dl *dl;
    int i, pass, mapper;
    reset_shell();
    sc = calloc(1, sizeof *sc);
    CHECK(sc);
    sc->hide_overlay = 1;
    /* An empty rock scene supplies lighting to the executor. Replay a
     * projected face and sprite to check their actual pixel colours. */
    for (i = 0; i < 4096; i++) ramp[i] = (uint8_t)((i & 255) / 2);
    shades[0] = 1; shades[6] = 2;
    sc->shades_file.data = shades; sc->shades_file.size = sizeof shades;
    sc->light_file.data = ramp; sc->light_file.size = sizeof ramp;
    memset(texels, 200, sizeof texels);
    for (i = 0; i < 16; i++) rows[i] = (uint32_t)i * 16;
    for (pass = 0; pass < 3; pass++) {
        full_bright = pass == 1;
        scene_camera(&test_shell.m);
        CHECK(sc->full_bright == full_bright);
        CHECK(uw_scene_draw(sc, &fb) >= 0);
        CHECK(memcmp(sc->light, ramp, sizeof ramp) == 0);
        CHECK(rw(sc->ds, 0x735a) == 2);
        CHECK((sc->vl.mem[0x2891 + 8 * 0x42 + 16 * 2] & 15)
              == (full_bright ? 1 : 15));
        CHECK((sc->vl.mem[0x2891 + 16 * 0x42] & 15) == 15);
        dl = &sc->dl;
        CHECK(sc->vl.shade[15 * 256 + 200] == (full_bright ? 200 : 100));
        dl->n_faces = 1;
        for (i = 0; i < 4; i++) dl->face[0][i] = face[3 - i];
        dl->face_n[0] = 4;
        dl->face_shader[0] = 0x1da;
        dl->face_lit[0] = 1;
        dl->face_rec[0] = (uw_rast_texrec){64, 4032, 0, 4032};
        dl->face_texels[0] = texels; dl->face_texlen[0] = sizeof texels;
        dl->view_clip = (uw_clip_rect){0, 0, 15, 15};
        for (mapper = 0; mapper < 2; mapper++) {
            dl->non_affine = mapper;
            memset(pixels, 77, sizeof pixels);
            uw_dl_render_face(dl, &fb, 0, 1);
            CHECK(pixels[6 * 16 + 6] == (full_bright ? 200 : 100));
            CHECK(pixels[0] == 77);
        }
        dl->face_shader[0] = 0x6146;
        dl->face_mode[0] = 1; dl->face_colour[0] = 200;
        for (i = 0; i < 4; i++) dl->face[0][i].u = 15;
        memset(pixels, 77, sizeof pixels);
        uw_dl_render_face(dl, &fb, 0, 1);
        CHECK(pixels[6 * 16 + 6] == (full_bright ? 200 : 100));
        dl->art = bright_test_art;
        dl->n_sprites = dl->n_events = 1;
        dl->event[0] = 0x8000;
        dl->sprite[0].level = 15;
        dl->sprite[0].desc = (uw_sprite_desc){0, 2, 2, 2, 8, 4, 4};
        dl->sprite_clip = (uw_clip_bounds){0, 15, 15, 0};
        memset(pixels, 77, sizeof pixels);
        uw_dl_render_event(dl, &fb, 0, 1);
        CHECK(pixels[8 * 16 + 2] == (full_bright ? 200 : 100));
        CHECK(pixels[0] == 77);
        dl->n_events = 0;
    }
    free(sc); sc = NULL;
    full_bright = 0;
}

static void check_draw_list_bounds(void) {
    uw_vl *v = calloc(1, sizeof *v);
    uint8_t ds[65536] = {0}, rast[65536] = {0}, tiles[16384], camera[64] = {0};
    int row, col;
    CHECK(v);
    /* A fully visible open room would emit enough floor and ceiling faces
     * to overwrite the texture and sprite tables, or wrap the list pointer. */
    memset(tiles, 0, sizeof tiles);
    for (row = 0; row < 4096; row++) tiles[row * 4] = 1;
    ww(ds, 0x2cf2, 16); ww(ds, 0x2cfa, 4 + (32 * 64 + 32) * 4);
    ww(ds, 0x444, 1); ww(ds, 0x446, 64); ww(ds, 0x448, 0xffff);
    ww(ds, 0x54b, 1); ww(ds, 0x54d, 1);
    ww(ds, 0x555, 0x0a4a); ww(ds, 0x55d, 0x0a4a);
    ww(ds, 0x547, 16); ww(ds, 0x549, 16);
    ww(camera, 0xe, 500);
    for (row = 0; row < 17; row++) for (col = 0; col < 33; col++)
        ds[0x2890 + row * 0x42 + col * 2] = 0x80;
    v->ds = ds; v->rast = rast; v->tiles = tiles; v->tiles_origin = 4;
    v->vstate = camera;
    CHECK(uw_vl_build(v) < UW_VL_LIST_END);
    CHECK(v->list_overflow && v->ptr >= UW_VL_LIST0);
    for (row = UW_VL_LIST_END; row < 65536; row++) CHECK(v->list[row] == 0);
    /* A later small view must build normally after an overflowing attempt. */
    ww(ds, 0x2cf2, 0xffff);
    CHECK(uw_vl_build(v) < UW_VL_LIST_END && !v->list_overflow);
    free(v);
}

int main(void) {
    CHECK(SDL_Init(SDL_INIT_EVENTS));
    check_commands();
    check_pause_and_restore(0);
    check_pause_and_restore(1);
    check_god_mode();
    check_widescreen();
    check_bright_distance_ramp();
    check_full_bright();
    check_draw_list_bounds();
    SDL_Quit();
    puts("Console commands, input capture, pause, scroll restoration, god mode, widescreen and full bright passed");
    return 0;
}
