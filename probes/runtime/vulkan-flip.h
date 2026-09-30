// SPDX-License-Identifier: MIT
#pragma once
#ifndef VULKAN_FLIP_TEST
#include <gnm_helpers.h>
#endif
// The reused ICD discards the helper's return code; retain it for the frontend.
// Poll boundedly instead of waiting indefinitely on a kernel event queue.
static int ed_vk_flip_error;
GnmError __wrap_sceGnmVideoOutSubmitFlipAndWait(GnmVideoOut *out,uint32_t index,int64_t argument,int32_t mode) {
    ed_vk_flip_error=GNM_ERROR_INVALID_ARGS;
    if(!out || out->handle<0 || index>=out->numbuffers)return (GnmError)ed_vk_flip_error;
    ed_vk_flip_error=sceVideoOutSubmitFlip(out->handle,(int)index,mode,argument);
    if(ed_vk_flip_error)return (GnmError)ed_vk_flip_error;
    for(int attempt=0;attempt<2000;++attempt) {
        OrbisVideoOutFlipStatus status={0};
        int result=sceVideoOutGetFlipStatus(out->handle,&status);
        if(result<0) { ed_vk_flip_error=result;return (GnmError)result; }
        if(status.flipArg==argument) {
            ++out->frame;out->currentbuffer=(index+1)%out->numbuffers;
            return GNM_ERROR_OK;
        }
        sceKernelUsleep(1000);
    }
    ed_vk_flip_error=GNM_ERROR_INVALID_STATE;
    return (GnmError)ed_vk_flip_error;
}
