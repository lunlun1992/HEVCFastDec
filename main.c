#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>

#include "hevcfastdec.h"
//http://blog.csdn.net/xiaoxiaoyeyaya/article/details/42541419

uint8_t *buf;
uint64_t buf_size;
uint64_t au_buf[101010];
uint32_t au_pos;
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
    uint64_t malloc_size = buf_size;
    while(malloc_size % 4)
        malloc_size++;
    buf = (uint8_t *)malloc(sizeof(uint8_t) * malloc_size);
    if(!buf)
    {
        fprintf(stderr, "Error malloc file buf\n");
        goto ERR;
    }

    if(buf_size != fread(buf, 1, buf_size, input))
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

    for(uint64_t i = 0; i < buf_size - 6;)
    {
        uint32_t re_buf_cache = *((uint32_t *)(buf + i));
        if(0x01000000 == re_buf_cache)
        {
            au_buf[au_pos++] = i + 1;
            i += 4;
        }
        else
            i++;
    }
}

int main(int argc, char * const *argv)
{
    FdOutContext out;
    FdInputContext in;
    if(parse_opt_open_file(argc, argv))
        return -1;

    void *fd_ctx = hevc_fast_dec_create(&in);
    divide_nalus();

    au_buf[au_pos] = buf_size;
//    for(int i = 0; i <= au_pos; i++)
//        printf("%d\n", au_buf[i]);
    for(int i = 0; i < au_pos; i++)
    {
        int64_t pts = i * 40; //25fps
        hevc_fast_dec_decode(buf + au_buf[i], au_buf[i + 1] - au_buf[i], pts, fd_ctx, &out);
        if(out.got_frame)
        {
            //render
        }
    }



    hevc_fast_dec_free(fd_ctx);
    free(buf);
    return 0;
}