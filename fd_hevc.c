#include "fd_hevc.h"
static int fd_hevc_decode_nal_sei(HEVCContext *h)
{
    return 0;
}

static void decode_profile_tier_level(HEVCContext *s, PTLCommon *ptl)
{
    int i;
    GetBitContext *gb = &s->gb;
    ptl->profile_space = (uint8_t)get_bits(gb, 2);
    ptl->tier_flag     = (uint8_t)get_bits1(gb);
    ptl->profile_idc   = (uint8_t)get_bits(gb, 5);


    if (ptl->profile_idc == FF_PROFILE_HEVC_MAIN)
        fprintf(stdout, "Main profile bitstream\n");
    else if (ptl->profile_idc == FF_PROFILE_HEVC_MAIN_10)
        fprintf(stdout, "Main 10 profile bitstream\n");
    else if (ptl->profile_idc == FF_PROFILE_HEVC_MAIN_STILL_PICTURE)
        fprintf(stdout, "Main Still Picture profile bitstream\n");
    else if (ptl->profile_idc == FF_PROFILE_HEVC_REXT)
        fprintf(stdout, "Range Extension profile bitstream\n");
    else
        fprintf(stdout, "Unknown HEVC profile: %d\n", ptl->profile_idc);

    for (i = 0; i < 32; i++)
        ptl->profile_compatibility_flag[i] = (uint8_t)get_bits1(gb);
    ptl->progressive_source_flag    = (uint8_t)get_bits1(gb);
    ptl->interlaced_source_flag     = (uint8_t)get_bits1(gb);
    ptl->non_packed_constraint_flag = (uint8_t)get_bits1(gb);
    ptl->frame_only_constraint_flag = (uint8_t)get_bits1(gb);

    skip_bits(gb, 16); // XXX_reserved_zero_44bits[0..15]
    skip_bits(gb, 16); // XXX_reserved_zero_44bits[16..31]
    skip_bits(gb, 12); // XXX_reserved_zero_44bits[32..43]
}

static int parse_ptl(HEVCContext *s, PTL *ptl, int max_num_sub_layers)
{
    int i;
    GetBitContext *gb = &s->gb;

    decode_profile_tier_level(s, &ptl->general_ptl);
    ptl->general_ptl.level_idc = (uint8_t)get_bits(gb, 8);

    for (i = 0; i < max_num_sub_layers - 1; i++) {
        ptl->sub_layer_profile_present_flag[i] = (uint8_t)get_bits1(gb);
        ptl->sub_layer_level_present_flag[i]   = (uint8_t)get_bits1(gb);
    }
    if (max_num_sub_layers - 1> 0)
        for (i = max_num_sub_layers - 1; i < 8; i++) {
            skip_bits(gb, 2); // reserved_zero_2bits[i]
        }
    for (i = 0; i < max_num_sub_layers - 1; i++) {
        if (ptl->sub_layer_profile_present_flag[i])
            decode_profile_tier_level(s, &ptl->sub_layer_ptl[i]);
        if (ptl->sub_layer_level_present_flag[i]) {
            ptl->sub_layer_ptl[i].level_idc = (uint8_t)get_bits(gb, 8);
        }
    }
    return 0;
}

static void decode_sublayer_hrd(HEVCContext *s, unsigned int nb_cpb, int subpic_params_present)
{
    GetBitContext *gb = &s->gb;
    int i;

    for (i = 0; i < nb_cpb; i++) {
        get_ue_golomb_long(gb); // bit_rate_value_minus1
        get_ue_golomb_long(gb); // cpb_size_value_minus1

        if (subpic_params_present) {
            get_ue_golomb_long(gb); // cpb_size_du_value_minus1
            get_ue_golomb_long(gb); // bit_rate_du_value_minus1
        }
        skip_bits1(gb); // cbr_flag
    }
}

static void decode_hrd(HEVCContext *s, int common_inf_present, int max_sublayers)
{
    GetBitContext *gb = &s->gb;
    int nal_params_present = 0, vcl_params_present = 0;
    int subpic_params_present = 0;
    int i;

    if (common_inf_present) {
        nal_params_present = get_bits1(gb);
        vcl_params_present = get_bits1(gb);

        if (nal_params_present || vcl_params_present) {
            subpic_params_present = get_bits1(gb);

            if (subpic_params_present) {
                skip_bits(gb, 8); // tick_divisor_minus2
                skip_bits(gb, 5); // du_cpb_removal_delay_increment_length_minus1
                skip_bits(gb, 1); // sub_pic_cpb_params_in_pic_timing_sei_flag
                skip_bits(gb, 5); // dpb_output_delay_du_length_minus1
            }

            skip_bits(gb, 4); // bit_rate_scale
            skip_bits(gb, 4); // cpb_size_scale

            if (subpic_params_present)
                skip_bits(gb, 4);  // cpb_size_du_scale

            skip_bits(gb, 5); // initial_cpb_removal_delay_length_minus1
            skip_bits(gb, 5); // au_cpb_removal_delay_length_minus1
            skip_bits(gb, 5); // dpb_output_delay_length_minus1
        }
    }

    for (i = 0; i < max_sublayers; i++) {
        int low_delay = 0;
        unsigned int nb_cpb = 1;
        int fixed_rate = get_bits1(gb);

        if (!fixed_rate)
            fixed_rate = get_bits1(gb);

        if (fixed_rate)
            get_ue_golomb_long(gb);  // elemental_duration_in_tc_minus1
        else
            low_delay = get_bits1(gb);

        if (!low_delay)
            nb_cpb = get_ue_golomb_long(gb) + 1;

        if (nal_params_present)
            decode_sublayer_hrd(s, nb_cpb, subpic_params_present);
        if (vcl_params_present)
            decode_sublayer_hrd(s, nb_cpb, subpic_params_present);
    }
}

static int fd_hevc_decode_nal_vps(GetBitContext *gb, HEVCContext *h)
{
    int i,j;
    int vps_id = 0;
    ptrdiff_t nal_size;
    HEVCVPS *vps = fd_malloc(sizeof(HEVCVPS));
    if (!vps)
        goto err;

    nal_size = gb->buffer_end - gb->buffer;
    if (nal_size > sizeof(HEVCVPS))
        vps->data_size = sizeof(vps->data);
    else
        vps->data_size = nal_size;


    vps_id = get_bits(gb, 4);
    if (vps_id >= MAX_VPS_COUNT)
    {
        fprintf(stderr, "VPS id out of range: %d\n", vps_id);
        goto err;
    }

    if (get_bits(gb, 2) != 3) // vps_reserved_three_2bits
    {
        fprintf(stderr, "vps_reserved_three_2bits is not three\n");
        goto err;
    }
    vps->vps_max_layers = get_bits(gb, 6) + 1;
    vps->vps_max_sub_layers = get_bits(gb, 3) + 1;
    vps->vps_temporal_id_nesting_flag = get_bits1(gb);

    if (get_bits(gb, 16) != 0xffff)// vps_reserved_ffff_16bits
    {
        fprintf(stderr, "vps_reserved_ffff_16bits is not 0xffff\n");
        goto err;
    }

    if (vps->vps_max_sub_layers > MAX_SUB_LAYERS)
    {
        fprintf(stderr, "vps_max_sub_layers out of range: %d\n", vps->vps_max_sub_layers);
        goto err;
    }

    if (parse_ptl(gb, &vps->ptl, vps->vps_max_sub_layers) < 0)
        goto err;

    vps->vps_sub_layer_ordering_info_present_flag = get_bits1(gb);

    i = vps->vps_sub_layer_ordering_info_present_flag ? 0 : vps->vps_max_sub_layers - 1;
    for (; i < vps->vps_max_sub_layers; i++)
    {
        vps->vps_max_dec_pic_buffering[i] = get_ue_golomb_long(gb) + 1;
        vps->vps_num_reorder_pics[i]      = get_ue_golomb_long(gb);
        vps->vps_max_latency_increase[i]  = get_ue_golomb_long(gb) - 1;

        if (vps->vps_max_dec_pic_buffering[i] > MAX_DPB_SIZE || !vps->vps_max_dec_pic_buffering[i])
        {
            fprintf(stderr, "vps_max_dec_pic_buffering_minus1 out of range: %d\n", vps->vps_max_dec_pic_buffering[i] - 1);
            goto err;
        }
        if (vps->vps_num_reorder_pics[i] > vps->vps_max_dec_pic_buffering[i] - 1)
        {
            fprintf(stderr, "vps_max_num_reorder_pics out of range: %d\n", vps->vps_num_reorder_pics[i]);
            goto err;
        }
    }

    vps->vps_max_layer_id   = get_bits(gb, 6);
    vps->vps_num_layer_sets = get_ue_golomb_long(gb) + 1;
    if (vps->vps_num_layer_sets < 1 || vps->vps_num_layer_sets > 1024 || (vps->vps_num_layer_sets - 1LL) * (vps->vps_max_layer_id + 1LL) > get_bits_left(gb))
    {
        fprintf(stderr, "too many layer_id_included_flags\n");
        goto err;
    }

    for (i = 1; i < vps->vps_num_layer_sets; i++)
        for (j = 0; j <= vps->vps_max_layer_id; j++)
            skip_bits(gb, 1);  // layer_id_included_flag[i][j]

    vps->vps_timing_info_present_flag = get_bits1(gb);
    if (vps->vps_timing_info_present_flag) {
        vps->vps_num_units_in_tick               = get_bits_long(gb, 32);
        vps->vps_time_scale                      = get_bits_long(gb, 32);
        vps->vps_poc_proportional_to_timing_flag = get_bits1(gb);
        if (vps->vps_poc_proportional_to_timing_flag)
            vps->vps_num_ticks_poc_diff_one = get_ue_golomb_long(gb) + 1;
        vps->vps_num_hrd_parameters = get_ue_golomb_long(gb);
        if (vps->vps_num_hrd_parameters > (unsigned)vps->vps_num_layer_sets) {
            fprintf(stderr, "vps_num_hrd_parameters %d is invalid\n", vps->vps_num_hrd_parameters);
            goto err;
        }
        for (i = 0; i < vps->vps_num_hrd_parameters; i++) {
            int common_inf_present = 1;

            get_ue_golomb_long(gb); // hrd_layer_set_idx
            if (i)
                common_inf_present = get_bits1(gb);
            decode_hrd(gb, common_inf_present, vps->vps_max_sub_layers);
        }
    }
    get_bits1(gb); /* vps_extension_flag */

    if (get_bits_left(gb) < 0)
    {
        fprintf(stderr, "Overread VPS by %d bits\n", -get_bits_left(gb));
        if (h->ps->vps_list[vps_id])
            goto err;
    }


    fd_free(h->ps->vps);
    h->ps->vps_list[vps_id] = vps;
    return 0;

err:
    fd_free(vps);
    return -1;
}

static int fd_hevc_decode_nal_sps(GetBitContext *gb, HEVCContext *h)
{
    return 0;
}

static int fd_hevc_decode_nal_pps(GetBitContext *gb, HEVCContext *h)
{
    return 0;
}


int decode_nal_unit(HEVCContext *h)
{
    uint8_t ret;
    GetBitContext *gb = &h->gb;


    switch (h->nal_unit_type) {
        case NAL_VPS:
            ret = fd_hevc_decode_nal_vps(gb, h);
            if (ret < 0)
                goto fail;
            break;
        case NAL_SPS:
            ret = fd_hevc_decode_nal_sps(gb, h);
            if (ret < 0)
                goto fail;
            break;
        case NAL_PPS:
            ret = fd_hevc_decode_nal_pps(gb, h);
            if (ret < 0)
                goto fail;
            break;
        case NAL_SEI_PREFIX:
        case NAL_SEI_SUFFIX:
            ret = fd_hevc_decode_nal_sei(h);
            if (ret < 0)
                goto fail;
            break;
        case NAL_TRAIL_R:
        case NAL_TRAIL_N:
        case NAL_TSA_N:
        case NAL_TSA_R:
        case NAL_STSA_N:
        case NAL_STSA_R:
        case NAL_BLA_W_LP:
        case NAL_BLA_W_RADL:
        case NAL_BLA_N_LP:
        case NAL_IDR_W_RADL:
        case NAL_IDR_N_LP:
        case NAL_CRA_NUT:
        case NAL_RADL_N:
        case NAL_RADL_R:
        case NAL_RASL_N:
        case NAL_RASL_R:
//            ret = hls_slice_header(s);
//            if (ret < 0)
//                return ret;
//
//            if (s->max_ra == INT_MAX) {
//                if (s->nal_unit_type == NAL_CRA_NUT || IS_BLA(s)) {
//                    s->max_ra = s->poc;
//                } else {
//                    if (IS_IDR(s))
//                        s->max_ra = INT_MIN;
//                }
//            }
//
//            if ((s->nal_unit_type == NAL_RASL_R || s->nal_unit_type == NAL_RASL_N) &&
//                s->poc <= s->max_ra) {
//                s->is_decoded = 0;
//                break;
//            } else {
//                if (s->nal_unit_type == NAL_RASL_R && s->poc > s->max_ra)
//                    s->max_ra = INT_MIN;
//            }
//
//            if (s->sh.first_slice_in_pic_flag) {
//                ret = hevc_frame_start(s);
//                if (ret < 0)
//                    return ret;
//            } else if (!s->ref) {
//                av_log(s->avctx, AV_LOG_ERROR, "First slice in a frame missing.\n");
//                goto fail;
//            }
//
//            if (s->nal_unit_type != s->first_nal_type) {
//                av_log(s->avctx, AV_LOG_ERROR,
//                       "Non-matching NAL types of the VCL NALUs: %d %d\n",
//                       s->first_nal_type, s->nal_unit_type);
//                return AVERROR_INVALIDDATA;
//            }
//
//            if (!s->sh.dependent_slice_segment_flag &&
//                s->sh.slice_type != I_SLICE) {
//                ret = ff_hevc_slice_rpl(s);
//                if (ret < 0) {
//                    av_log(s->avctx, AV_LOG_WARNING,
//                           "Error constructing the reference lists for the current slice.\n");
//                    goto fail;
//                }
//            }
//
//            if (s->sh.first_slice_in_pic_flag && s->avctx->hwaccel) {
//                ret = s->avctx->hwaccel->start_frame(s->avctx, NULL, 0);
//                if (ret < 0)
//                    goto fail;
//            }
//
//            if (s->avctx->hwaccel) {
//                ret = s->avctx->hwaccel->decode_slice(s->avctx, nal->raw_data, nal->raw_size);
//                if (ret < 0)
//                    goto fail;
//            } else {
//                if (s->threads_number > 1 && s->sh.num_entry_point_offsets > 0)
//                    ctb_addr_ts = hls_slice_data_wpp(s, nal);
//                else
//                    ctb_addr_ts = hls_slice_data(s);
//                if (ctb_addr_ts >= (s->ps.sps->ctb_width * s->ps.sps->ctb_height)) {
//                    s->is_decoded = 1;
//                }
//
//                if (ctb_addr_ts < 0) {
//                    ret = ctb_addr_ts;
//                    goto fail;
//                }
//            }
//            break;
        case NAL_EOS_NUT:
        case NAL_EOB_NUT:
            h->seq_decode = (h->seq_decode + 1) & 0xff;
            h->max_ra     = INT32_MAX;
            break;
        case NAL_AUD:
        case NAL_FD_NUT:
            break;
        default:
            printf("Skipping NAL unit %d\n", h->nal_unit_type);
    }
    return 0;
fail:
    return -1;
}


int decode_nal_units(HEVCContext *h, const uint8_t *bs, uint64_t bs_len)
{
    //decode all NAL units in an Access Unit
    int ptr_bs = 0;
    while(1)
    {
        //1. parse NAL start code
        while(ptr_bs + 3 < bs_len)
        {
            if(bs[ptr_bs] == 0 && bs[ptr_bs + 1] == 0 && bs[ptr_bs + 2] == 1)
                break;
            ptr_bs++;
        }
        if(ptr_bs + 3 >= bs_len)
            break;
        ptr_bs += 3;

        //2. NAL Unit header
        h->nal_unit_type = (enum NALUnitType)((bs[ptr_bs++] & 0x7F) >> 1);
        h->temporal_id = (bs[ptr_bs++] & 0x7) - 1;

        //3. Extract emulated code
        int ptr_bs_back = ptr_bs;
        int nalu_length = -1;
        while(ptr_bs + 3 < bs_len)
        {
            if(bs[ptr_bs])
            {
                ptr_bs += 2;
                continue;
            }
            if(ptr_bs != ptr_bs_back && !bs[ptr_bs - 1])
                ptr_bs--;
            if(ptr_bs + 2 < bs_len && bs[ptr_bs + 1] == 0 && bs[ptr_bs + 2] <= 3)
            {
                if(bs[ptr_bs + 2] != 3)
                    nalu_length = ptr_bs - ptr_bs_back;
                break;
            }
            ptr_bs += 2;
        }
        uint8_t *rbsp = bs + ptr_bs_back;
        int rbsp_len = nalu_length;

        //4. Has 0x000003, very rare
        if(nalu_length == -1)
        {
            fd_fast_realloc((void **)&h->rbsp_buffer, &h->rbsp_buffer_size, bs_len - ptr_bs_back + 16);
            if(!h->rbsp_buffer)
            {
                fprintf(stderr, "Error Alloc memory for RBSP\n");
                return -1;
            }
            rbsp = h->rbsp_buffer;
            memcpy(rbsp, bs + ptr_bs_back, ptr_bs - ptr_bs_back);
            int si = ptr_bs;
            int di = ptr_bs - ptr_bs_back;
            while(si + 2 < bs_len)
            {
                if(bs[si + 2] > 3)
                {
                    rbsp[di++] = bs[si++];
                    rbsp[di++] = bs[si++];
                }
                else if(bs[si] == 0 && bs[si + 1] == 0)
                {
                    if(bs[si + 2] == 3)
                    {
                        rbsp[di++] = 0;
                        rbsp[di++] = 0;
                        si += 3;
                        continue;
                    }
                    goto nsc;
                }
                rbsp[di++] = bs[si++];
            }
            while(si < bs_len)
                rbsp[di++] = bs[si++];
nsc:
            rbsp_len = di;
            ptr_bs = si;
        }
        for(int i = 0; i < rbsp_len; i++)
            printf("%02x\n", rbsp[i]);

        while(rbsp_len && rbsp[rbsp_len - 1] == 0)
            rbsp_len--;
        //5. Initial get bits context
        init_get_bits8(&h->gb, rbsp, rbsp_len);

        //6. Decode this NAL Unit with this RBSP.
        decode_nal_unit(h);
    }
    return 0;
}


HEVCContext *fd_hevc_init_context()
{
    HEVCContext *h = (HEVCContext *)fd_malloc(sizeof(HEVCContext));
    memset(h, 0, sizeof(HEVCContext));
    return h;

}
int fd_hevc_decode_frame_single(HEVCContext *h, FDFrame *frame, uint8_t *got_picture)
{
    return decode_nal_units(h, h->input_pkt.data, h->input_pkt.size);
}
int fd_hevc_uninit_context(HEVCContext *ctx)
{
    fd_free(ctx);
    return 0;
}