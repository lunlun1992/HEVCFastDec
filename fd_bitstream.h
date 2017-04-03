#ifndef _BITSTREAM_H
#define _BITSTREAM_H
#include "fd_common.h"

typedef struct GetBitContext
{
    const uint8_t *buffer, *buffer_end;
    uint32_t index;
    uint32_t size_in_bits;
    uint32_t size_in_bits_plus8;
} GetBitContext;


/**
 * 1. Read Raw Bits
 */

static av_always_inline uint32_t get_bits_count(const GetBitContext *s)
{
    return s->index;
}

static av_always_inline void skip_bits_long(GetBitContext *s, int n)
{
    s->index += av_clip32_c(n, -s->index, s->size_in_bits_plus8 - s->index);
}

//Read 1-25 bits.
static av_always_inline uint32_t get_bits(GetBitContext *s, int n)
{
    register int tmp;
    uint32_t re_index = s->index;
    uint32_t re_size_plus8 = s->size_in_bits_plus8;
    const uint8_t *re_buffer = s->buffer;
    uint32_t re_cache = (uint32_t)(re_buffer + (re_index >> 3)) << (re_index & 7);

    assert(n > 0 && n <= 25);

    tmp = re_cache >> (32 - n);
    re_index = FFMIN(re_size_plus8, re_index + n);
    s->index = re_index;

    return tmp;
}

//Read 0-25 bits.
static av_always_inline uint32_t get_bitsz(GetBitContext *s, int n)
{
    return n ? get_bits(s, n) : 0;
}

//Show 1-25 bits.
static av_always_inline uint32_t show_bits(GetBitContext *s, int n)
{
    register int tmp;
    uint32_t re_index = s->index;
    const uint8_t *re_buffer = s->buffer;
    uint32_t re_cache = (uint32_t)(re_buffer + (re_index >> 3)) << (re_index & 7);

    assert(n > 0 && n <= 25);

    tmp = re_cache >> (32 - n);
    return tmp;
}

static av_always_inline void skip_bits(GetBitContext *s, int n)
{
    uint32_t re_index = s->index;
    uint32_t re_size_plus8 = s->size_in_bits_plus8;
    re_index = FFMIN(re_size_plus8, re_index + n);
    s->index = re_index;
}

static av_always_inline uint32_t get_bits1(GetBitContext *s)
{
    uint32_t index = s->index;
    register uint8_t result = s->buffer[index >> 3];
    result <<= index & 7;
    result >>= 7;
    if (s->index < s->size_in_bits_plus8)
        index++;
    s->index = index;
    return result;
}

static av_always_inline uint32_t show_bits1(GetBitContext *s)
{
    return show_bits(s, 1);
}

static av_always_inline void skip_bits1(GetBitContext *s)
{
    skip_bits(s, 1);
}
//Read 0-32 bits.
static av_always_inline unsigned int get_bits_long(GetBitContext *s, int n)
{
    if (!n)
        return 0;
    else if (n <= 25)
        return get_bits(s, n);
    else
    {
        unsigned ret = get_bits(s, 16) << (n - 16);
        return ret | get_bits(s, n - 16);
    }
}

static av_always_inline int get_bits_left(GetBitContext *gb)
{
    return gb->size_in_bits - get_bits_count(gb);
}


/**
 * 2. Golomb
 */
static av_always_inline int get_ue_golomb(GetBitContext *gb)
{
    register unsigned int buf;

    uint32_t re_index = s->index;
    uint32_t re_size_plus8 = s->size_in_bits_plus8;
    const uint8_t *re_buffer = s->buffer;
    uint32_t re_cache = (uint32_t)(re_buffer + (re_index >> 3)) << (re_index & 7);

    buf = re_cache >> (32 - n);
    if (buf >= (1 << 27)) {
        buf >>= 32 - 9;
        LAST_SKIP_BITS(re, gb, ff_golomb_vlc_len[buf]);
        CLOSE_READER(re, gb);

        return ff_ue_golomb_vlc_code[buf];
    } else {
        int log = 2 * av_log2(buf) - 31;
        LAST_SKIP_BITS(re, gb, 32 - log);
        CLOSE_READER(re, gb);
        if (log < 7) {
            fprintf(stderr, "Invalid UE golomb code\n");
            return AVERROR_INVALIDDATA;
        }
        buf >>= log;
        buf--;

        return buf;
    }
}

//Read an unsigned Exp-Golomb code in the range 0 to UINT32_MAX-1.
static inline unsigned get_ue_golomb_long(GetBitContext *gb)
{
    unsigned buf, log;

    buf = show_bits_long(gb, 32);
    log = 31 - av_log2(buf);
    skip_bits_long(gb, log);

    return get_bits_long(gb, log + 1) - 1;
}



static av_always_inline int init_get_bits(GetBitContext *s, const uint8_t *buffer, int bit_size)
{
    int buffer_size;
    int ret = 0;

    if (bit_size >= INT32_MAX - 7 || bit_size < 0 || !buffer) {
        bit_size    = 0;
        buffer      = NULL;
        ret         = -1;
    }

    buffer_size = (bit_size + 7) >> 3;

    s->buffer             = buffer;
    s->size_in_bits       = bit_size;
    s->size_in_bits_plus8 = bit_size + 8;
    s->buffer_end         = buffer + buffer_size;
    s->index              = 0;

    return ret;
}

static av_always_inline int init_get_bits8(GetBitContext *s, const uint8_t *buffer,
                                 int byte_size)
{
    if (byte_size > INT32_MAX / 8 || byte_size < 0)
        byte_size = -1;
    return init_get_bits(s, buffer, byte_size * 8);
}

static inline const uint8_t *align_get_bits(GetBitContext *s)
{
    int n = -get_bits_count(s) & 7;
    if (n)
        skip_bits(s, n);
    return s->buffer + (s->index >> 3);
}


#endif