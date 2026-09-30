// Native frontend ABI/renderer mock. Does not replace console validation.
#define main audio_fixture_main
#include "test-gb-audio.c"
#undef main
#define ORBIS_USER_SERVICE_ERROR_ALREADY_INITIALIZED 0x80960001u
#define ORBIS_PAD_ERROR_ALREADY_OPENED 0x80920004u
#define ORBIS_PAD_ERROR_DEVICE_NOT_CONNECTED 0x80920007u
#define ORBIS_PAD_ERROR_SEND_AGAIN 0x80920105u
#define ORBIS_PAD_PORT_TYPE_STANDARD 0
#define ORBIS_VIDEO_OUT_FLIP_VSYNC 1
typedef struct { uint32_t buttons; int connected; } OrbisPadData;
typedef struct { int64_t flipArg; } OrbisVideoOutFlipStatus;
static int pad_connected = 1;
static int pad_read_result;
static uint32_t pad_buttons=0x4000;
static int sceUserServiceInitialize(void *p) { (void)p; return 0; }
static int sceUserServiceGetInitialUser(int *u) { *u=1; return 0; }
static int scePadInit(void) { return 0; }
static int scePadOpen(int u, int t, int i, void *p) { (void)u; (void)t; (void)i; (void)p; return 1; }
static int scePadGetHandle(int u, int t, int i) { (void)u; (void)t; (void)i; return 1; }
static int scePadReadState(int h, OrbisPadData *p) { assert(h==1); p->connected=pad_connected; p->buttons=pad_buttons; return pad_read_result; }
static int scePadClose(int h) { assert(h==1); return 0; }
static uint64_t sceKernelGetProcessTime(void) { static uint64_t tick; return tick += 20000; }
static int64_t submitted;
static int sceVideoOutSubmitFlip(int v, int i, int mode, int64_t seq) {
    assert(v==1 && i>=0 && i<=1 && mode==1); submitted=seq; return 0;
}
static int sceVideoOutGetFlipStatus(int v, OrbisVideoOutFlipStatus *s) { assert(v==1); s->flipArg=submitted; return 0; }
static uint32_t storage[2][1280*720];
static uint32_t *frames[2] = { storage[0], storage[1] };
static int video = 1;
static int64_t sequence;
static unsigned char report_busy;
static int text_lines;
static void draw_text(uint32_t *pixels, int y, char *text) { assert(pixels && y>=0 && text); ++text_lines; }
#ifdef UI_CAPTURE
#include <stb/stb_easy_font.h>
static void draw_text_at(uint32_t *frame, int x, int y, char *text, uint32_t color) {
    struct Vertex { float x,y,z; unsigned char color[4]; };
    static struct Vertex vertices[4096];
    int count=stb_easy_font_print(0,0,text,NULL,vertices,sizeof(vertices));
    ++text_lines;
    for(int q=0;q<count;++q) {
        struct Vertex a=vertices[q*4],b=vertices[q*4+2];
        for(int row=y+(int)(a.y*2);row<y+(int)(b.y*2)&&row<720;++row)
            for(int col=x+(int)(a.x*2);col<x+(int)(b.x*2)&&col<1280;++col)
                if(row>=0&&col>=0)frame[row*1280+col]=color;
    }
}
#else
static void draw_text_at(uint32_t *pixels, int x, int y, char *text, uint32_t color) { (void)x; (void)color; draw_text(pixels,y,text); }
#endif
#include "gb-player.h"
static void check_console_image(int width, int height) {
    static uint32_t image[640*480];
    for(int i=0;i<width*height;++i) image[i]=0xff000000u | (unsigned)(i*2654435761u);
    assert(console_present(image,width*height,width,height,16667));
    uint32_t *shown=storage[(sequence-1)%2];
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x) {
        uint32_t expected=0xff102020;
        if(x>=player_left && x<player_left+width*player_scale &&
           y>=player_top && y<player_top+height*player_scale)
            expected=image[((y-player_top)/player_scale)*width+(x-player_left)/player_scale];
        assert(shown[y*1280+x]==expected);
    }
}
int main(void) {
    console_performance(NULL);
    assert(gb_input()==0x4000);
    pad_connected=0; assert(gb_input()==0);
    pad_read_result=(int)ORBIS_PAD_ERROR_DEVICE_NOT_CONNECTED;
    assert(gb_input()==0 && (uint32_t)gb_input_error()==ORBIS_PAD_ERROR_DEVICE_NOT_CONNECTED);
    pad_read_result=(int)ORBIS_PAD_ERROR_SEND_AGAIN;
    assert(gb_input()==0);
    pad_read_result=(int)0x809200FFu;
    assert(gb_input()==-1 && (uint32_t)gb_input_error()==0x809200FFu);
    pad_read_result=0; pad_connected=1;
    assert(gb_input()==0x4000 && gb_input_error()==0 && !gb_pad_transient);
    pad_buttons=0x80104000u;
    assert(gb_input()==0x104000 && !gb_input_error());
    pad_buttons=0x4000;
    static uint32_t pixels[PLAYER_WIDTH*PLAYER_HEIGHT];
    for (int i=0;i<PLAYER_WIDTH*PLAYER_HEIGHT;++i) pixels[i]=0xff000000u+(unsigned)i;
    assert(!gb_present(pixels,1));
    assert(gb_present(pixels,PLAYER_WIDTH*PLAYER_HEIGHT));
    assert(storage[0][PLAYER_TOP*1280+PLAYER_LEFT]==pixels[0]);
    assert(storage[0][(PLAYER_TOP+PLAYER_HEIGHT*PLAYER_SCALE-1)*1280+PLAYER_LEFT+PLAYER_WIDTH*PLAYER_SCALE-1]==pixels[PLAYER_WIDTH*PLAYER_HEIGHT-1]);
    assert(storage[0][PLAYER_TOP*1280+PLAYER_LEFT-1]==0xff102020);
    assert(gb_menu("LIBRARY\n\n> Mario\n  Zelda"));
    assert(text_lines>=8 && sequence==2 && !report_busy);
    static uint32_t console_pixels[320*240];
    assert(!console_present(console_pixels,320*240,0,240,16667));
    assert(!console_present(console_pixels,320*240,320,240,9999));
    assert(console_present(console_pixels,256*192,256,192,16667));
    assert(player_width==256 && player_height==192 && player_left==256 && player_top==72 && player_scale==3);
    assert(console_present(console_pixels,320*240,320,240,20000));
    assert(player_left==160 && player_top==0 && player_scale==3 && player_period==20000);
    assert(console_present(console_pixels,320*224,320,224,16667));
    assert(player_top==24 && player_scale==3);
    assert(!console_preview(console_pixels,1,320,240));
    assert(console_preview(console_pixels,320*240,320,240));
#ifdef UI_CAPTURE
    FILE *asset=fopen("build/ui-preview/rom00.preview","rb");
    assert(asset);
    int32_t pw,ph;
    assert(fread(&pw,4,1,asset)==1 && fread(&ph,4,1,asset)==1);
    assert(pw>0 && pw<=320 && ph>0 && ph<=240);
    assert(fread(console_pixels,4,(size_t)(pw*ph),asset)==(size_t)(pw*ph)); fclose(asset);
    assert(console_preview(console_pixels,pw*ph,pw,ph));
#endif
    assert(gb_menu("Master System / Mega Drive / SNES / 5 games\n\n> Black Belt\n  Alex Kidd in Miracle World\n  Sonic the Hedgehog\n  Streets of Rage 2\n  Zelda - A Link to the Past\n\nNo battery saves yet."));
#ifdef UI_CAPTURE
    FILE *capture=fopen("build/ui-preview/library.ppm","wb"); assert(capture);
    fputs("P6\n1280 720\n255\n",capture);
    uint32_t *shown=storage[(sequence-1)%2];
    for(int i=0;i<1280*720;++i) { fputc(shown[i]>>16&255,capture);fputc(shown[i]>>8&255,capture);fputc(shown[i]&255,capture); }
    fclose(capture);
#else
    assert(preview_width==320 && preview_height==240);
#endif
    assert(console_preview(NULL,0,0,0));
    check_console_image(256,192);
    check_console_image(320,224);
    check_console_image(256,224);
    check_console_image(320,240);
    check_console_image(640,480);
#ifdef CONSOLE_PLAYER
    console_performance("12 FPS | core 70 audio 2 video 8 ms");
    int before=text_lines;
    assert(console_present(console_pixels,320*224,320,224,16667));
    assert(text_lines==before+1);
    console_performance(NULL); assert(!player_performance[0]);
#endif
#ifdef UI_BENCHMARK
    for(int i=0;i<1000;++i)assert(console_present(console_pixels,320*224,320,224,16667));
#endif
    gb_close(); assert(gb_pad==-1);
    puts("PASS UI: frame bounds, GB/SMS/MD sizes, PAL timing, previews, buffer rotation, pad disconnect");
}
