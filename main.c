#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>

#include "hevcfastdec.h"


uint8_t *buf;
uint64_t buf_size;

static inline void *align_32_malloc(uint64_t size)
{
    while(size % 4 != 0)
        size++;
    return malloc(size);
}

int parse_opt_open_file(int argc, char * const *argv)
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
    buf_size = (uint64_t)ftell(input);
    fseek(input, 0, SEEK_SET);
    buf = (uint8_t *)align_32_malloc(sizeof(uint8_t) * buf_size);
    if(!buf)
    {
        fprintf(stderr, "Error malloc file buf\n");
        goto ERR;
    }

    if(buf_size != fread(buf, buf_size, 1, input))
        goto ERR;
    fclose(input);
    return 0;
ERR:
    if(buf)
        free(buf);
    fclose(input);
    return -1;
}

void divide_nalus()
{

}

int main(int argc, char * const *argv)
{
    OutContext out;
    if(parse_opt_open_file(argc, argv))
        return -1;

    hevc_fast_dec_create();
    divide_nalus();

    //hevc_fast_dec_decode(bs, bs_len, &out);

    if(out.got_frame)
    {
        //render
    }
    hevc_fast_dec_free();
    free(buf);
    return 0;
}