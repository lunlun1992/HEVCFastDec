#include "fd_hevc.h"
#include "hevcfastdec.h"
#include <stdio.h>
#include <memory.h>
void *hevc_fast_dec_create(FdInputContext *in)
{
    HEVCContext *ctx = fd_hevc_init_context();
    return ctx;
}

int hevc_fast_dec_decode(uint8_t *bs, uint64_t bs_len, int64_t pts, void *fd_ctx, FdOutContext *out)
{
    FDPacket pkt;
    FDFrame frame;
    if(fd_ctx == NULL)
        return -1;

    HEVCContext *h = (HEVCContext *)fd_ctx;
    pkt.data = bs;
    pkt.size = bs_len;
    pkt.pts = pts;
    h->input_pkt = pkt;
    memset(&frame, 0, sizeof(FDFrame));
    int ret = fd_hevc_decode_frame_single(h, &frame, &out->got_frame);
    if(ret >= 0)
    {
        //output
    }
    return 0;
}

int hevc_fast_dec_free(void *fd_ctx)
{
    fd_hevc_uninit_context((HEVCContext *)fd_ctx);
    return 0;
}