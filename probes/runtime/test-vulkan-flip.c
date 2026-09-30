#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define VULKAN_FLIP_TEST
#define GNM_ERROR_OK 0
#define GNM_ERROR_INVALID_ARGS -1
#define GNM_ERROR_INVALID_STATE 2
typedef int GnmError;
typedef struct {int handle;unsigned numbuffers,currentbuffer;int64_t frame;} GnmVideoOut;
typedef struct {int64_t flipArg;} OrbisVideoOutFlipStatus;
static int submit_error,status_error,polls,complete_at;
static int64_t pending;
static int sceVideoOutSubmitFlip(int handle,int index,int mode,int64_t arg) {
    assert(handle==7 && index==1 && mode==1);pending=arg;return submit_error;
}
static int sceVideoOutGetFlipStatus(int handle,OrbisVideoOutFlipStatus *s) {
    assert(handle==7);++polls;s->flipArg=polls>=complete_at?pending:pending-1;return status_error;
}
static void sceKernelUsleep(unsigned us) {assert(us==1000);}
#include "vulkan-flip.h"
int main(void) {
    GnmVideoOut out={.handle=7,.numbuffers=2,.frame=10};
    assert(__wrap_sceGnmVideoOutSubmitFlipAndWait(NULL,1,42,1)==-1);
    submit_error=-3;assert(__wrap_sceGnmVideoOutSubmitFlipAndWait(&out,1,42,1)==-3 && !polls);
    submit_error=0;status_error=-4;assert(__wrap_sceGnmVideoOutSubmitFlipAndWait(&out,1,42,1)==-4);
    status_error=0;polls=0;complete_at=2001;
    assert(__wrap_sceGnmVideoOutSubmitFlipAndWait(&out,1,42,1)==2 && polls==2000 && out.frame==10);
    polls=0;complete_at=3;
    assert(__wrap_sceGnmVideoOutSubmitFlipAndWait(&out,1,42,1)==0 && polls==3 && out.frame==11 && out.currentbuffer==0);
    assert(!ed_vk_flip_error);
    puts("PASS Vulkan flip: submit/status errors, timeout, completion and safe reuse");
}
