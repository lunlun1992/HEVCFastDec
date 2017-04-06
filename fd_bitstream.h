#ifndef _BITSTREAM_H
#define _BITSTREAM_H
#include "fd_common.h"

union unaligned_32 { uint32_t l; } __attribute__((packed)) av_alias;
#define AV_BSWAP16C(x) (((x) << 8 & 0xff00)  | ((x) >> 8 & 0x00ff))
#define AV_BSWAP32C(x) (AV_BSWAP16C(x) << 16 | AV_BSWAP16C((x) >> 16))
#define AV_BSWAP64C(x) (AV_BSWAP32C(x) << 32 | AV_BSWAP32C((x) >> 32))
#define AV_RN32(p) (((const union unaligned_32 *) (p))->l)
#define AV_RB32(p) AV_BSWAP32C(AV_RN32(p))

extern const uint8_t golomb_vlc_len[512];
extern const int8_t se_golomb_vlc_code[512];
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
    uint32_t re_cache = AV_RB32(re_buffer + (re_index >> 3)) << (re_index & 7);
    //printf("%x\n", re_cache);
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
    uint32_t re_cache = AV_RB32(re_buffer + (re_index >> 3)) << (re_index & 7);

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
static av_always_inline unsigned int show_bits_long(GetBitContext *s, int n)
{
    if (n <= 25)
        return show_bits(s, n);
    else
    {
        GetBitContext gb = *s;
        return get_bits_long(&gb, n);
    }
}

//Read an unsigned Exp-Golomb code in the range 0 to UINT32_MAX-1.
static inline unsigned get_ue_golomb_long(GetBitContext *gb)
{
    unsigned buf, log;

    buf = show_bits_long(gb, 32);
    log = 31 - fd_log2(buf);
    skip_bits_long(gb, log);

    return get_bits_long(gb, log + 1) - 1;
}

static inline int get_se_golomb(GetBitContext *s)
{
    unsigned int buf;

    uint32_t re_index = s->index;
    uint32_t re_size_plus8 = s->size_in_bits_plus8;
    const uint8_t *re_buffer = s->buffer;
    uint32_t re_cache = AV_RB32(re_buffer + (re_index >> 3)) << (re_index & 7);
    buf = re_cache;

    if (buf >= (1 << 27))
    {
        buf >>= 32 - 9;
        re_index = FFMIN(re_size_plus8, re_index + golomb_vlc_len[buf]);
        s->index = re_index;
        return se_golomb_vlc_code[buf];
    }
    else
    {
        int log = fd_log2(buf), sign;
        re_index = FFMIN(re_size_plus8, re_index + 31 - log);
        re_cache = AV_RB32(re_buffer + (re_index >> 3)) << (re_index & 7);
        buf = re_cache;
        buf >>= log;
        re_index = FFMIN(re_size_plus8, re_index + 32 - log);
        s->index = re_index;

        sign = -(buf & 1);
        buf  = ((buf >> 1) ^ sign) - sign;

        return buf;
    }
}

static inline int get_se_golomb_long(GetBitContext *gb)
{
    unsigned int buf = get_ue_golomb_long(gb);
    int sign = (buf & 1) - 1;
    return ((buf >> 1) ^ sign) + 1;
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


/**
 * 3. Init
 */
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