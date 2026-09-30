// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Nichlas Eklöf
static int gb_pad = -1, gb_pad_owned;
static uint64_t gb_deadline;
static void draw_text(uint32_t *, int, char *);
static void draw_text_at(uint32_t *, int, int, char *, uint32_t);
static void report(const char *, ...);
#include "gb-audio.h"

static int gb_input(void) {
    if (gb_pad < 0) {
        int rc = sceUserServiceInitialize(NULL), user = -1;
        if (rc && (uint32_t)rc != ORBIS_USER_SERVICE_ERROR_ALREADY_INITIALIZED) {
            report("FAIL UserService: %08x", rc); return -1;
        }
        if (sceUserServiceGetInitialUser(&user) < 0 || scePadInit() < 0) {
            report("FAIL pad initialization"); return -1;
        }
        gb_pad = scePadOpen(user, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
        gb_pad_owned = gb_pad > 0;
        if ((uint32_t)gb_pad == ORBIS_PAD_ERROR_ALREADY_OPENED)
            gb_pad = scePadGetHandle(user, ORBIS_PAD_PORT_TYPE_STANDARD, 0);
        if (gb_pad <= 0) { report("FAIL pad open: %08x", gb_pad); return -1; }
        report("GB controller ready");
    }
    OrbisPadData data = {0};
    int rc = scePadReadState(gb_pad, &data);
    if (rc < 0) { report("FAIL pad read: %08x", rc); return -1; }
    return data.connected ? (int)data.buttons : 0;
}

#ifdef SMS_PLAYER
#define PLAYER_WIDTH 256
#define PLAYER_HEIGHT 240
#define PLAYER_SCALE 3
#define PLAYER_LEFT 256
#define PLAYER_TOP 0
#define PLAYER_PERIOD 16667
#else
#define PLAYER_WIDTH 160
#define PLAYER_HEIGHT 144
#define PLAYER_SCALE 4
#define PLAYER_LEFT 320
#define PLAYER_TOP 64
#define PLAYER_PERIOD 16743
#endif
// First subset of Android default.apa: opaque panels, teal focus, muted labels.
static void ui_panel(uint32_t *frame, int x, int y, int w, int h, uint32_t color) {
    for (int row=y; row<y+h && row<720; ++row)
        for (int col=x; col<x+w && col<1280; ++col)
            if (row>=0 && col>=0) frame[row*1280+col]=color;
}
static int player_width = PLAYER_WIDTH, player_height = PLAYER_HEIGHT;
static int player_scale = PLAYER_SCALE, player_left = PLAYER_LEFT, player_top = PLAYER_TOP;
static int player_period = PLAYER_PERIOD;
static uint32_t library_preview[320*240];
static int preview_width, preview_height;
static int player_frontend_active;
static int console_preview(const uint32_t *pixels, int count, int width, int height) {
    if (!pixels && count==0) { preview_width=preview_height=0; return 1; }
    if (!pixels || width<1 || width>320 || height<1 || height>240 || count!=width*height) return 0;
    memcpy(library_preview,pixels,(size_t)count*sizeof(uint32_t));
    preview_width=width; preview_height=height;
    return 1;
}
static int gb_render(const uint32_t *pixels, int count, const char *menu) {
    if ((!menu && (!pixels || count != player_width * player_height)) || video < 0 || !frames[0]) return 0;
    while (__atomic_test_and_set(&report_busy, __ATOMIC_ACQUIRE)) sceKernelUsleep(1000);
    player_frontend_active = 1;
    int index = sequence % 2;
    ++sequence;
    uint32_t *frame = frames[index];
    for (int i = 0; i < 1280 * 720; ++i) frame[i] = menu ? 0xff091119 : 0xff102020;
    if (!menu) {
        for (int y = 0; y < player_height; ++y) {
            uint32_t *row = frame + (y * player_scale + player_top) * 1280 + player_left;
            const uint32_t *source = pixels + y * player_width;
            for (int x = 0; x < player_width; ++x)
                for (int repeat = 0; repeat < player_scale; ++repeat)
                    row[x * player_scale + repeat] = source[x];
            for (int repeat = 1; repeat < player_scale; ++repeat)
                memcpy(row + repeat * 1280, row, (size_t)player_width * player_scale * sizeof(uint32_t));
        }
#if defined(SMS_PLAYER) || defined(CONSOLE_PLAYER)
#ifndef CONSOLE_PLAYER
        draw_text_at(frame, 30, 40, "EutherDrive", 0xff5eead4);
        draw_text_at(frame, 30, 78, "Console player", 0xff91a8bd);
#ifndef CONSOLE_PLAYER
        draw_text_at(frame, 30, 560, "X / O: 1 / 2", 0xffeef6ff);
        draw_text_at(frame, 30, 598, "Options: pause", 0xff91a8bd);
#endif
        draw_text_at(frame, 1040, 40, "L1 + R1", 0xff5eead4);
        draw_text_at(frame, 1040, 78, "Library", 0xff91a8bd);
        draw_text_at(frame, 1040, 598, gb_muted ? "Muted" : "Sound on", 0xff91a8bd);
#endif
#else
        draw_text(frame, 24, "EutherDrive 0.09 - Game Boy / Color - Nichlas Eklof");
        draw_text(frame, 662, "D-pad: move  X: A  O: B  Options: Start  Square: Select");
        draw_text(frame, 690, gb_muted ? "L1+R1: library | Triangle: sound ON | Muted" : "L1+R1: library | Triangle: mute | Stereo 48 kHz");
#endif
    } else {
        ui_panel(frame, 24, 20, 1232, 78, 0xff121c27);
        ui_panel(frame, 24, 20, 6, 78, 0xff5eead4);
        draw_text_at(frame, 52, 38, "EutherDrive", 0xff5eead4);
        draw_text_at(frame, 52, 70, "YOUR GAME LIBRARY", 0xff91a8bd);
        draw_text_at(frame, 998, 44, gb_muted ? "SOUND OFF" : "SOUND ON", 0xffeef6ff);
        char text[2048];
        snprintf(text, sizeof(text), "%s", menu);
        char *line = text;
        int row = 0;
        for (int y = 122; line && y < 620; y += 40, ++row) {
            char *end = strchr(line, '\n');
            if (end) *end = 0;
            int selected = line[0] == '>';
            if (row >= 2 && row < 10 && *line) {
                ui_panel(frame, 32, y-8, (preview_width ? 744 : 1216), 36, selected ? 0xff163b41 : 0xff121c27);
                if (selected) ui_panel(frame, 32, y-8, 4, 36, 0xff5eead4);
            }
            draw_text_at(frame, 56, y, selected ? line+2 : line,
                         selected ? 0xff5eead4 : (row==0 ? 0xffeef6ff : 0xff91a8bd));
            line = end ? end + 1 : NULL;
        }
        if (preview_width) {
            ui_panel(frame, 800, 178, 448, 386, 0xff172433);
            draw_text_at(frame, 828, 196, "GAME PREVIEW", 0xff5eead4);
            int w=400, h=preview_height*400/preview_width;
            if(h>300) { h=300; w=preview_width*300/preview_height; }
            int left=824+(400-w)/2, top=242+(300-h)/2;
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)
                frame[(top+y)*1280+left+x]=library_preview[(y*preview_height/h)*preview_width+x*preview_width/w];
        }
        ui_panel(frame, 24, 644, 1232, 58, 0xff172433);
        draw_text_at(frame, 48, 664, "D-pad: choose    X: play    Triangle: sound    O: exit", 0xffeef6ff);
    }
    uint64_t now = sceKernelGetProcessTime();
    if (gb_deadline > now && gb_deadline - now < 100000)
        sceKernelUsleep((unsigned)(gb_deadline - now));
    gb_deadline = (gb_deadline && now < gb_deadline + 100000 ? gb_deadline : now) + player_period;
    int ok = 0;
    if (sceVideoOutSubmitFlip(video, index, ORBIS_VIDEO_OUT_FLIP_VSYNC, sequence) >= 0) {
        for (int attempt = 0; attempt < 2000; ++attempt) {
            OrbisVideoOutFlipStatus state = {0};
            if (sceVideoOutGetFlipStatus(video, &state) >= 0 && state.flipArg == sequence) {
                ok = 1; break;
            }
            sceKernelUsleep(1000);
        }
    }
    if (!ok) video = -1; // Do not reuse an in-flight buffer on timeout.
    __atomic_clear(&report_busy, __ATOMIC_RELEASE);
    return ok;
}
static int gb_present(const uint32_t *pixels, int count) { return gb_render(pixels, count, NULL); }
static int console_present(const uint32_t *pixels, int count, int width, int height, int period) {
    if (!pixels || width < 1 || width > 640 || height < 1 || height > 480 ||
        count != width * height || period < 10000 || period > 25000) return 0;
    player_width=width; player_height=height;
    player_scale=720/height;
    if (player_scale > 1280/width) player_scale=1280/width;
    player_left=(1280-width*player_scale)/2;
    player_top=(720-height*player_scale)/2;
    player_period=period;
    return gb_render(pixels,count,NULL);
}
static int gb_menu(const char *text) { return text ? gb_render(NULL, 0, text) : 0; }

static void gb_close(void) {
    gb_audio_close();
    if (gb_pad_owned && gb_pad > 0) scePadClose(gb_pad);
    gb_pad = -1;
    gb_pad_owned = 0;
}
