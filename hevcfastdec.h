#ifndef _HEVCFASTDEC_H
#define _HEVCFASTDEC_H
#include <stdint.h>
typedef struct Ctx_t
{
    uint8_t got_frame;
    const uint8_t * const *frame;
    int nChannel;
    int width;
    int height;
}OutContext;

//create Decoder
void hevc_fast_dec_create();

//decode NALUs
int hevc_fast_dec_decode(uint8_t *bs, uint64_t bs_len, OutContext *out);

//free decoder
int hevc_fast_dec_free();

#endif