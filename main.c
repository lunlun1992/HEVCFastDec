#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "hevcfastdec.h"
#include <getopt.h>
#include <stdlib.h>

uint8_t *buf;

int parse_opt(int argc, char * const *argv)
{
    char filename[1024];
    char c;

    while((c = getopt(argc, argv, NULL)) != -1)
    {
        //to add options
    }
    strcpy(filename, argv[argc - 1]);

    FILE *input = fopen(filename, "rb");
    if(!input)
    {
        fprintf(stderr, "Error open input file\n");
        goto ERR;
    }
    fseek(input, 0, SEEK_END);
    uint64_t size = (uint64_t)ftell(input);
    fseek(input, 0, SEEK_SET);
    buf = (uint8_t *)malloc(sizeof(uint8_t) * size);
    if(!buf)
    {
        fprintf(stderr, "Error malloc file buf\n");
        goto ERR;
    }

    if(size != fread(buf, size, 1, input))
        goto ERR;
    fclose(input);
    return 0;
ERR:
    if(buf)
        free(buf);
    fclose(input);
    return -1;
}

int main(int argc, char * const *argv)
{
    OutContext out;
    if(parse_opt(argc, argv))
        return -1;

    void *ctx = hevc_fast_dec_create();


    hevc_fast_dec_decode(bs, bs_len, ctx, &out);

    if(out.got_frame)
    {
        //render
    }
    hevc_fast_dec_free(ctx);
    free(buf);
    return 0;
}