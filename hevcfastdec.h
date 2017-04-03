#ifndef _HEVCFASTDEC_H
#define _HEVCFASTDEC_H
#include <stdint.h>
typedef struct OutCtx_t
{
    uint8_t got_frame;
    uint8_t pixels[3];
    uint8_t line_stride[3];
    int width;
    int height;
}FdOutContext;

typedef struct InCtx_t
{
    void *placeholder;
}FdInputContext;

//create Decoder
void *hevc_fast_dec_create(FdInputContext *in);

//decode NALUs
int hevc_fast_dec_decode(uint8_t *bs, uint64_t bs_len, int64_t pts, void *fd_ctx, FdOutContext *out);

//free decoder
int hevc_fast_dec_free(void *fd_ctx);

#endif