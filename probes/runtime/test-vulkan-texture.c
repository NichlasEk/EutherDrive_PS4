// Desktop offscreen Vulkan checks shader scaling/channel order; not PS4 WSI proof.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ED_VK_OFFSCREEN
#include "vulkan-player.h"
static uint32_t pixels[1280*720], hud[1280*64];
static int check(int w,int h,int show_hud) {
    int scale=720/h;if(scale>1280/w)scale=1280/w;
    int left=(1280-w*scale)/2, top=(720-h*scale)/2;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        pixels[y*w+x]=0xff000000u|((unsigned)(x*13+y*4)&255)<<16|((unsigned)(x/7+y/3)&255)<<8|((unsigned)(x^y)&255);
    for(int i=0;i<1280*64;++i)hud[i]=0xff22ee44;
    if(!ed_vk_present(pixels,w,h,left,top,w*scale,h*scale,show_hud?hud:NULL)) {
        fprintf(stderr,"Vulkan %s result=%d\n",ed_vk_operation,(int)ed_vk_error);return 0;
    }
    uint32_t *actual=ed_vk_readback_pixels;
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x) {
        uint32_t expected=0xff102020;
        if(x>=left && x<left+w*scale && y>=top && y<top+h*scale)
            expected=pixels[((y-top)/scale)*w+(x-left)/scale];
        if(show_hud && y<64)expected=0xff22ee44;
        uint32_t raw=actual[y*1280+x];
        uint32_t argb=(raw&0xff00ff00)|((raw&255)<<16)|((raw>>16)&255);
        if(argb!=expected) {fprintf(stderr,"Pixel %d,%d got=%08x expected=%08x\n",x,y,argb,expected);return 0;}
    }
    printf("PASS Vulkan shader: %dx%d scale=%d HUD=%d all 921600 pixels\n",w,h,scale,show_hud);return 1;
}
int main(void) {
    if(!ed_vk_init()) {fprintf(stderr,"Init %s result=%d\n",ed_vk_operation,(int)ed_vk_error);return 1;}
    assert(check(256,192,0));assert(check(320,224,0));assert(check(256,224,0));
    assert(check(320,240,0));assert(check(640,480,0));assert(check(1280,720,0));
    assert(check(256,192,1));assert(check(256,192,0));
    return 0;
}
