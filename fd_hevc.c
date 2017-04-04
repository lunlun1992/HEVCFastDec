#include "fd_hevc.h"

static int fd_hevc_decode_nal_sei(HEVCContext *h)
{
    return 0;
}

static int decode_profile_tier_level(GetBitContext *gb, PTLCommon *ptl)
{
    int i;

    if (get_bits_left(gb) < 2 + 1 + 5 + 32 + 4 + 16 + 16 + 12)
        return -1;
    ptl->profile_space = (uint8_t)get_bits(gb, 2);
    ptl->tier_flag     = (uint8_t)get_bits1(gb);
    ptl->profile_idc   = (uint8_t)get_bits(gb, 5);
    if (ptl->profile_idc == FF_PROFILE_HEVC_MAIN)
        fprintf(stderr, "Main profile bitstream\n");
    else if (ptl->profile_idc == FF_PROFILE_HEVC_MAIN_10)
        fprintf(stderr, "Main 10 profile bitstream\n");
    else if (ptl->profile_idc == FF_PROFILE_HEVC_MAIN_STILL_PICTURE)
        fprintf(stderr, "Main Still Picture profile bitstream\n");
    else if (ptl->profile_idc == FF_PROFILE_HEVC_REXT)
        fprintf(stderr, "Range Extension profile bitstream\n");
    else
        fprintf(stderr, "Unknown HEVC profile: %d\n", ptl->profile_idc);

    for (i = 0; i < 32; i++) {
        ptl->profile_compatibility_flag[i] = (uint8_t)get_bits1(gb);

        if (ptl->profile_idc == 0 && i > 0 && ptl->profile_compatibility_flag[i])
            ptl->profile_idc = (uint8_t)i;
    }
    ptl->progressive_source_flag    = (uint8_t)get_bits1(gb);
    ptl->interlaced_source_flag     = (uint8_t)get_bits1(gb);
    ptl->non_packed_constraint_flag = (uint8_t)get_bits1(gb);
    ptl->frame_only_constraint_flag = (uint8_t)get_bits1(gb);

    skip_bits(gb, 16); // XXX_reserved_zero_44bits[0..15]
    skip_bits(gb, 16); // XXX_reserved_zero_44bits[16..31]
    skip_bits(gb, 12); // XXX_reserved_zero_44bits[32..43]

    return 0;
}

static int parse_ptl(GetBitContext *gb, PTL *ptl, int max_num_sub_layers)
{
    int i;
    if (decode_profile_tier_level(gb, &ptl->general_ptl) < 0 || get_bits_left(gb) < 8 + (8 * 2 * (max_num_sub_layers - 1 > 0))) {
        fprintf(stderr, "PTL information too short\n");
        return -1;
    }

    ptl->general_ptl.level_idc = (uint8_t)get_bits(gb, 8);

    for (i = 0; i < max_num_sub_layers - 1; i++)
    {
        ptl->sub_layer_profile_present_flag[i] = (uint8_t)get_bits1(gb);
        ptl->sub_layer_level_present_flag[i]   = (uint8_t)get_bits1(gb);
    }

    if (max_num_sub_layers > 1)
        for (i = max_num_sub_layers - 1; i < 8; i++)
            skip_bits(gb, 2); // reserved_zero_2bits[i]
    for (i = 0; i < max_num_sub_layers - 1; i++)
    {
        if (ptl->sub_layer_profile_present_flag[i] && decode_profile_tier_level(gb, &ptl->sub_layer_ptl[i]) < 0)
        {
            fprintf(stderr, "PTL information for sublayer %i too short\n", i);
            return -1;
        }
        if (ptl->sub_layer_level_present_flag[i])
        {
            if (get_bits_left(gb) < 8)
            {
                fprintf(stderr, "Not enough data for sublayer %i level_idc\n", i);
                return -1;
            }
            else
                ptl->sub_layer_ptl[i].level_idc = (uint8_t)get_bits(gb, 8);
        }
    }

    return 0;
}

static void decode_sublayer_hrd(GetBitContext *gb, unsigned int nb_cpb, int subpic_params_present)
{
    int i;
    for (i = 0; i < nb_cpb; i++)
    {
        get_ue_golomb_long(gb); // bit_rate_value_minus1
        get_ue_golomb_long(gb); // cpb_size_value_minus1

        if (subpic_params_present)
        {
            get_ue_golomb_long(gb); // cpb_size_du_value_minus1
            get_ue_golomb_long(gb); // bit_rate_du_value_minus1
        }
        skip_bits1(gb); // cbr_flag
    }
}

static int decode_hrd(GetBitContext *gb, int common_inf_present, int max_sublayers)
{
    int nal_params_present = 0, vcl_params_present = 0;
    int subpic_params_present = 0;
    int i;

    if (common_inf_present)
    {
        nal_params_present = get_bits1(gb);
        vcl_params_present = get_bits1(gb);

        if (nal_params_present || vcl_params_present)
        {
            subpic_params_present = get_bits1(gb);
            if (subpic_params_present)
            {
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

    for (i = 0; i < max_sublayers; i++)
    {
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
        {
            nb_cpb = get_ue_golomb_long(gb) + 1;
            if (nb_cpb < 1 || nb_cpb > 32)
            {
                fprintf(stderr, "nb_cpb %d invalid\n", nb_cpb);
                return -1;
            }
        }

        if (nal_params_present)
            decode_sublayer_hrd(gb, nb_cpb, subpic_params_present);
        if (vcl_params_present)
            decode_sublayer_hrd(gb, nb_cpb, subpic_params_present);
    }
    return 0;
}

static int fd_hevc_decode_nal_vps(GetBitContext *gb, HEVCContext *h)
{
    int i,j;
    int vps_id = 0;
    HEVCVPS *vps = fd_malloc(sizeof(HEVCVPS));
    if (!vps)
        goto err;

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
    vps->vps_temporal_id_nesting_flag = (uint8_t)get_bits1(gb);

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

    vps->vps_timing_info_present_flag = (uint8_t)get_bits1(gb);
    if (vps->vps_timing_info_present_flag)
    {
        vps->vps_num_units_in_tick               = get_bits_long(gb, 32);
        vps->vps_time_scale                      = get_bits_long(gb, 32);
        vps->vps_poc_proportional_to_timing_flag = (uint8_t)get_bits1(gb);
        if (vps->vps_poc_proportional_to_timing_flag)
            vps->vps_num_ticks_poc_diff_one = get_ue_golomb_long(gb) + 1;
        vps->vps_num_hrd_parameters = get_ue_golomb_long(gb);
        if (vps->vps_num_hrd_parameters > (unsigned)vps->vps_num_layer_sets)
        {
            fprintf(stderr, "vps_num_hrd_parameters %d is invalid\n", vps->vps_num_hrd_parameters);
            goto err;
        }
        for (i = 0; i < vps->vps_num_hrd_parameters; i++)
        {
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


    fd_free((void*)h->ps->vps);
    h->ps->vps_list[vps_id] = vps;
    return 0;

err:
    fd_free(vps);
    return -1;
}

static int map_pixel_format(HEVCSPS *sps)
{
    switch (sps->bit_depth)
    {
        case 8:
            if (sps->chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY8;
            if (sps->chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P;
            if (sps->chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P;
            if (sps->chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P;
            break;
        case 9:
            if (sps->chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY16;
            if (sps->chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P9;
            if (sps->chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P9;
            if (sps->chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P9;
            break;
        case 10:
            if (sps->chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY10;
            if (sps->chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P10;
            if (sps->chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P10;
            if (sps->chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P10;
            break;
        case 12:
            if (sps->chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY12;
            if (sps->chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P12;
            if (sps->chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P12;
            if (sps->chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P12;
            break;
        default:
            fprintf(stderr, "The following bit-depths are currently specified: 8, 9, 10 and 12 bits, chroma_format_idc is %d, depth is %d\n", sps->chroma_format_idc, sps->bit_depth);
            return -1;
    }
    sps->pixel_shift = sps->bit_depth > 8;

    return 0;
}

static void set_default_scaling_list_data(ScalingList *sl)
{
    int matrixId;

    for (matrixId = 0; matrixId < 6; matrixId++) {
        // 4x4 default is 16
        memset(sl->sl[0][matrixId], 16, 16);
        sl->sl_dc[0][matrixId] = 16; // default for 16x16
        sl->sl_dc[1][matrixId] = 16; // default for 32x32
    }
    memcpy(sl->sl[1][0], default_scaling_list_intra, 64);
    memcpy(sl->sl[1][1], default_scaling_list_intra, 64);
    memcpy(sl->sl[1][2], default_scaling_list_intra, 64);
    memcpy(sl->sl[1][3], default_scaling_list_inter, 64);
    memcpy(sl->sl[1][4], default_scaling_list_inter, 64);
    memcpy(sl->sl[1][5], default_scaling_list_inter, 64);
    memcpy(sl->sl[2][0], default_scaling_list_intra, 64);
    memcpy(sl->sl[2][1], default_scaling_list_intra, 64);
    memcpy(sl->sl[2][2], default_scaling_list_intra, 64);
    memcpy(sl->sl[2][3], default_scaling_list_inter, 64);
    memcpy(sl->sl[2][4], default_scaling_list_inter, 64);
    memcpy(sl->sl[2][5], default_scaling_list_inter, 64);
    memcpy(sl->sl[3][0], default_scaling_list_intra, 64);
    memcpy(sl->sl[3][1], default_scaling_list_intra, 64);
    memcpy(sl->sl[3][2], default_scaling_list_intra, 64);
    memcpy(sl->sl[3][3], default_scaling_list_inter, 64);
    memcpy(sl->sl[3][4], default_scaling_list_inter, 64);
    memcpy(sl->sl[3][5], default_scaling_list_inter, 64);
}

static int scaling_list_data(GetBitContext *gb, ScalingList *sl, HEVCSPS *sps)
{
    uint8_t scaling_list_pred_mode_flag;
    int32_t scaling_list_dc_coef[2][6];
    int size_id, matrix_id, pos;
    int i;

    for (size_id = 0; size_id < 4; size_id++)
    {
        for (matrix_id = 0; matrix_id < 6; matrix_id += ((size_id == 3) ? 3 : 1))
        {
            scaling_list_pred_mode_flag = (uint8_t) get_bits1(gb);
            if (!scaling_list_pred_mode_flag)
            {
                unsigned int delta = get_ue_golomb_long(gb);
                /* Only need to handle non-zero delta. Zero means default,
                 * which should already be in the arrays. */
                if (delta)
                {
                    // Copy from previous array.
                    if (matrix_id < delta)
                    {
                        fprintf(stderr, "Invalid delta in scaling list data: %d.\n", delta);
                        return -1;
                    }
                    memcpy(sl->sl[size_id][matrix_id], sl->sl[size_id][matrix_id - delta], size_id > 0 ? 64 : 16);
                    if (size_id > 1)
                        sl->sl_dc[size_id - 2][matrix_id] = sl->sl_dc[size_id - 2][matrix_id - delta];
                }
            }
            else
            {
                int next_coef, coef_num;
                int32_t scaling_list_delta_coef;

                next_coef = 8;
                coef_num = FFMIN(64, 1 << (4 + (size_id << 1)));
                if (size_id > 1)
                {
                    scaling_list_dc_coef[size_id - 2][matrix_id] = get_se_golomb(gb) + 8;
                    next_coef = scaling_list_dc_coef[size_id - 2][matrix_id];
                    sl->sl_dc[size_id - 2][matrix_id] = (uint8_t)next_coef;
                }
                for (i = 0; i < coef_num; i++)
                {
                    if (size_id == 0)
                        pos = 4 * hevc_diag_scan4x4_y[i] + hevc_diag_scan4x4_x[i];
                    else
                        pos = 8 * hevc_diag_scan8x8_y[i] + hevc_diag_scan8x8_x[i];

                    scaling_list_delta_coef = get_se_golomb(gb);
                    next_coef = (next_coef + scaling_list_delta_coef + 256) % 256;
                    sl->sl[size_id][matrix_id][pos] = (uint8_t)next_coef;
                }
            }
        }
    }
    if (sps->chroma_format_idc == 3)
    {
        for (i = 0; i < 64; i++)
        {
            sl->sl[3][1][i] = sl->sl[2][1][i];
            sl->sl[3][2][i] = sl->sl[2][2][i];
            sl->sl[3][4][i] = sl->sl[2][4][i];
            sl->sl[3][5][i] = sl->sl[2][5][i];
        }
        sl->sl_dc[1][1] = sl->sl_dc[0][1];
        sl->sl_dc[1][2] = sl->sl_dc[0][2];
        sl->sl_dc[1][4] = sl->sl_dc[0][4];
        sl->sl_dc[1][5] = sl->sl_dc[0][5];
    }
    return 0;
}

int ff_hevc_decode_short_term_rps(GetBitContext *gb, ShortTermRPS *rps, const HEVCSPS *sps, int is_slice_header)
{
    uint8_t rps_predict = 0;
    int delta_poc;
    int k0 = 0;
    int k1 = 0;
    int k  = 0;
    int i;

    if (rps != sps->st_rps && sps->nb_st_rps)
        rps_predict = (uint8_t)get_bits1(gb);

    if (rps_predict)
    {
        const ShortTermRPS *rps_ridx;
        int delta_rps;
        unsigned abs_delta_rps;
        uint8_t use_delta_flag = 0;
        uint8_t delta_rps_sign;

        if (is_slice_header)
        {
            unsigned int delta_idx = get_ue_golomb_long(gb) + 1;
            if (delta_idx > sps->nb_st_rps)
            {
                fprintf(stderr, "Invalid value of delta_idx in slice header RPS: %d > %d.\n", delta_idx, sps->nb_st_rps);
                return -1;
            }
            rps_ridx = &sps->st_rps[sps->nb_st_rps - delta_idx];
            rps->rps_idx_num_delta_pocs = rps_ridx->num_delta_pocs;
        }
        else
            rps_ridx = &sps->st_rps[rps - sps->st_rps - 1];

        delta_rps_sign = (uint8_t)get_bits1(gb);
        abs_delta_rps  = get_ue_golomb_long(gb) + 1;
        if (abs_delta_rps < 1 || abs_delta_rps > 32768)
        {
            fprintf(stderr, "Invalid value of abs_delta_rps: %d\n", abs_delta_rps);
            return -1;
        }
        delta_rps = (1 - (delta_rps_sign << 1)) * abs_delta_rps;
        for (i = 0; i <= rps_ridx->num_delta_pocs; i++)
        {
            int used = rps->used[k] = (uint8_t)get_bits1(gb);
            if (!used)
                use_delta_flag = (uint8_t)get_bits1(gb);

            if (used || use_delta_flag)
            {
                if (i < rps_ridx->num_delta_pocs)
                    delta_poc = delta_rps + rps_ridx->delta_poc[i];
                else
                    delta_poc = delta_rps;
                rps->delta_poc[k] = delta_poc;
                if (delta_poc < 0)
                    k0++;
                else
                    k1++;
                k++;
            }
        }

        rps->num_delta_pocs    = k;
        rps->num_negative_pics = (uint8_t)k0;
        // sort in increasing order (smallest first)
        if (rps->num_delta_pocs != 0)
        {
            int used, tmp;
            for (i = 1; i < rps->num_delta_pocs; i++)
            {
                delta_poc = rps->delta_poc[i];
                used      = rps->used[i];
                for (k = i - 1; k >= 0; k--)
                {
                    tmp = rps->delta_poc[k];
                    if (delta_poc < tmp)
                    {
                        rps->delta_poc[k + 1] = tmp;
                        rps->used[k + 1]      = rps->used[k];
                        rps->delta_poc[k]     = delta_poc;
                        rps->used[k]          = (uint8_t)used;
                    }
                }
            }
        }
        if ((rps->num_negative_pics >> 1) != 0)
        {
            int used;
            k = rps->num_negative_pics - 1;
            // flip the negative values to largest first
            for (i = 0; i < rps->num_negative_pics >> 1; i++)
            {
                delta_poc         = rps->delta_poc[i];
                used              = rps->used[i];
                rps->delta_poc[i] = rps->delta_poc[k];
                rps->used[i]      = rps->used[k];
                rps->delta_poc[k] = delta_poc;
                rps->used[k]      = (uint8_t)used;
                k--;
            }
        }
    }
    else
    {
        unsigned int prev, nb_positive_pics;
        rps->num_negative_pics = get_ue_golomb_long(gb);
        nb_positive_pics       = get_ue_golomb_long(gb);
        if (rps->num_negative_pics >= MAX_REFS || nb_positive_pics >= MAX_REFS)
        {
            fprintf(stderr, "Too many refs in a short term RPS.\n");
            return -1;
        }
        rps->num_delta_pocs = rps->num_negative_pics + nb_positive_pics;
        if (rps->num_delta_pocs)
        {
            prev = 0;
            for (i = 0; i < rps->num_negative_pics; i++)
            {
                delta_poc = get_ue_golomb_long(gb) + 1;
                prev -= delta_poc;
                rps->delta_poc[i] = prev;
                rps->used[i]      = (uint8_t)get_bits1(gb);
            }
            prev = 0;
            for (i = 0; i < nb_positive_pics; i++)
            {
                delta_poc = get_ue_golomb_long(gb) + 1;
                prev += delta_poc;
                rps->delta_poc[rps->num_negative_pics + i] = prev;
                rps->used[rps->num_negative_pics + i]      = (uint8_t)get_bits1(gb);
            }
        }
    }
    return 0;
}

static void decode_vui(GetBitContext *gb, HEVCSPS *sps)
{
    VUI *vui          = &sps->vui;
    GetBitContext backup;
    int sar_present, alt = 0;

    fprintf(stderr, "Decoding VUI\n");

    sar_present = get_bits1(gb);
    if (sar_present)
    {
        uint8_t sar_idx = (uint8_t)get_bits(gb, 8);
        if (sar_idx < sizeof(vui_sar) / sizeof(vui_sar[0]))
            vui->sar = vui_sar[sar_idx];
        else if (sar_idx == 255)
        {
            vui->sar.num = get_bits(gb, 16);
            vui->sar.den = get_bits(gb, 16);
        }
        else
            fprintf(stderr, "Unknown SAR index: %u.\n", sar_idx);
    }

    vui->overscan_info_present_flag = get_bits1(gb);
    if (vui->overscan_info_present_flag)
        vui->overscan_appropriate_flag = get_bits1(gb);

    vui->video_signal_type_present_flag = get_bits1(gb);
    if (vui->video_signal_type_present_flag)
    {
        vui->video_format                    = get_bits(gb, 3);
        vui->video_full_range_flag           = get_bits1(gb);
        vui->colour_description_present_flag = get_bits1(gb);
        if (vui->video_full_range_flag && sps->pix_fmt == AV_PIX_FMT_YUV420P)
            sps->pix_fmt = AV_PIX_FMT_YUVJ420P;
        if (vui->colour_description_present_flag)
        {
            vui->colour_primaries        = (uint8_t)get_bits(gb, 8);
            vui->transfer_characteristic = (uint8_t)get_bits(gb, 8);
            vui->matrix_coeffs           = (uint8_t)get_bits(gb, 8);

            // Set invalid values to "unspecified"
            if (vui->colour_primaries >= AVCOL_PRI_NB)
                vui->colour_primaries = AVCOL_PRI_UNSPECIFIED;
            if (vui->transfer_characteristic >= AVCOL_TRC_NB)
                vui->transfer_characteristic = AVCOL_TRC_UNSPECIFIED;
            if (vui->matrix_coeffs >= AVCOL_SPC_NB)
                vui->matrix_coeffs = AVCOL_SPC_UNSPECIFIED;
            if (vui->matrix_coeffs == AVCOL_SPC_RGB)
            {
                switch (sps->pix_fmt)
                {
                    case AV_PIX_FMT_YUV444P:
                        sps->pix_fmt = AV_PIX_FMT_GBRP;
                        break;
                    case AV_PIX_FMT_YUV444P10:
                        sps->pix_fmt = AV_PIX_FMT_GBRP10;
                        break;
                    case AV_PIX_FMT_YUV444P12:
                        sps->pix_fmt = AV_PIX_FMT_GBRP12;
                        break;
                    default:
                        assert(0);
                }
            }
        }
    }

    vui->chroma_loc_info_present_flag = get_bits1(gb);
    if (vui->chroma_loc_info_present_flag)
    {
        vui->chroma_sample_loc_type_top_field    = get_ue_golomb_long(gb);
        vui->chroma_sample_loc_type_bottom_field = get_ue_golomb_long(gb);
    }

    vui->neutra_chroma_indication_flag = get_bits1(gb);
    vui->field_seq_flag                = get_bits1(gb);
    vui->frame_field_info_present_flag = get_bits1(gb);

    if (get_bits_left(gb) >= 68 && show_bits_long(gb, 21) == 0x100000)
    {
        vui->default_display_window_flag = 0;
        fprintf(stderr, "Invalid default display window\n");
    }
    else
        vui->default_display_window_flag = get_bits1(gb);
    // Backup context in case an alternate header is detected
    memcpy(&backup, gb, sizeof(backup));

    if (vui->default_display_window_flag)
    {
        int vert_mult  = 1 + (sps->chroma_format_idc < 2);
        int horiz_mult = 1 + (sps->chroma_format_idc < 3);
        vui->def_disp_win.left_offset   = get_ue_golomb_long(gb) * horiz_mult;
        vui->def_disp_win.right_offset  = get_ue_golomb_long(gb) * horiz_mult;
        vui->def_disp_win.top_offset    = get_ue_golomb_long(gb) *  vert_mult;
        vui->def_disp_win.bottom_offset = get_ue_golomb_long(gb) *  vert_mult;
    }

    vui->vui_timing_info_present_flag = get_bits1(gb);

    if (vui->vui_timing_info_present_flag)
    {
        if( get_bits_left(gb) < 66)
        {
            // The alternate syntax seem to have timing info located
            // at where def_disp_win is normally located
            fprintf(stderr, "Strange VUI timing information, retrying...\n");
            vui->default_display_window_flag = 0;
            memset(&vui->def_disp_win, 0, sizeof(vui->def_disp_win));
            memcpy(gb, &backup, sizeof(backup));
            alt = 1;
        }
        vui->vui_num_units_in_tick               = get_bits_long(gb, 32);
        vui->vui_time_scale                      = get_bits_long(gb, 32);
        if (alt)
            fprintf(stderr, "Retry got %i/%i fps\n", vui->vui_time_scale, vui->vui_num_units_in_tick);
        vui->vui_poc_proportional_to_timing_flag = get_bits1(gb);
        if (vui->vui_poc_proportional_to_timing_flag)
            vui->vui_num_ticks_poc_diff_one_minus1 = get_ue_golomb_long(gb);
        vui->vui_hrd_parameters_present_flag = get_bits1(gb);
        if (vui->vui_hrd_parameters_present_flag)
            decode_hrd(gb, 1, sps->max_sub_layers);
    }

    vui->bitstream_restriction_flag = get_bits1(gb);
    if (vui->bitstream_restriction_flag)
    {
        vui->tiles_fixed_structure_flag              = get_bits1(gb);
        vui->motion_vectors_over_pic_boundaries_flag = get_bits1(gb);
        vui->restricted_ref_pic_lists_flag           = get_bits1(gb);
        vui->min_spatial_segmentation_idc            = get_ue_golomb_long(gb);
        vui->max_bytes_per_pic_denom                 = get_ue_golomb_long(gb);
        vui->max_bits_per_min_cu_denom               = get_ue_golomb_long(gb);
        vui->log2_max_mv_length_horizontal           = get_ue_golomb_long(gb);
        vui->log2_max_mv_length_vertical             = get_ue_golomb_long(gb);
    }
}

static int fd_hevc_decode_nal_sps(GetBitContext *gb, HEVCContext *h)
{
    HEVCSPS *sps = (HEVCSPS *)fd_malloc(sizeof(HEVCSPS));
    unsigned int sps_id;
    int log2_diff_max_min_transform_block_size;
    int bit_depth_chroma, start, vui_present, sublayer_ordering_info;
    int i, ret;
    if (!sps)
        return -1;
    fprintf(stderr, "Decoding SPS\n");

    // Coded parameters

    sps->vps_id = get_bits(gb, 4);
    if (sps->vps_id >= MAX_VPS_COUNT)
    {
        fprintf(stderr, "VPS id out of range: %d\n", sps->vps_id);
        return -1;
    }

    if (!h->ps->vps_list[sps->vps_id])
    {
        fprintf(stderr, "VPS %d does not exist\n", sps->vps_id);
        return -1;
    }

    sps->max_sub_layers = get_bits(gb, 3) + 1;
    if (sps->max_sub_layers > MAX_SUB_LAYERS)
    {
        fprintf(stderr, "sps_max_sub_layers out of range: %d\n", sps->max_sub_layers);
        return -1;
    }

    skip_bits1(gb); // temporal_id_nesting_flag

    if ((ret = parse_ptl(gb, &sps->ptl, sps->max_sub_layers)) < 0)
        return ret;

    sps_id = get_ue_golomb_long(gb);
    if (sps_id >= MAX_SPS_COUNT)
    {
        fprintf(stderr, "SPS id out of range: %d\n", sps_id);
        return -1;
    }

    sps->chroma_format_idc = get_ue_golomb_long(gb);
    if (sps->chroma_format_idc > 3U)
    {
        fprintf(stderr, "chroma_format_idc %d is invalid\n", sps->chroma_format_idc);
        return -1;
    }

    if (sps->chroma_format_idc == 3)
        sps->separate_colour_plane_flag = (uint8_t)get_bits1(gb);

    if (sps->separate_colour_plane_flag)
        sps->chroma_format_idc = 0;

    sps->width  = get_ue_golomb_long(gb);
    sps->height = get_ue_golomb_long(gb);

    if (get_bits1(gb))
    { // pic_conformance_flag
        int vert_mult  = 1 + (sps->chroma_format_idc < 2);
        int horiz_mult = 1 + (sps->chroma_format_idc < 3);
        sps->pic_conf_win.left_offset   = get_ue_golomb_long(gb) * horiz_mult;
        sps->pic_conf_win.right_offset  = get_ue_golomb_long(gb) * horiz_mult;
        sps->pic_conf_win.top_offset    = get_ue_golomb_long(gb) *  vert_mult;
        sps->pic_conf_win.bottom_offset = get_ue_golomb_long(gb) *  vert_mult;
        sps->output_window = sps->pic_conf_win;
    }

    sps->bit_depth   = get_ue_golomb_long(gb) + 8;
    bit_depth_chroma = get_ue_golomb_long(gb) + 8;
    if (sps->chroma_format_idc && bit_depth_chroma != sps->bit_depth)
    {
        fprintf(stderr, "Luma bit depth (%d) is different from chroma bit depth (%d), this is unsupported.\n",
               sps->bit_depth, bit_depth_chroma);
        return -1;
    }
    ret = map_pixel_format(sps);
    if (ret < 0)
        return ret;

    sps->log2_max_poc_lsb = get_ue_golomb_long(gb) + 4;
    if (sps->log2_max_poc_lsb > 16)
    {
        fprintf(stderr, "log2_max_pic_order_cnt_lsb_minus4 out range: %d\n", sps->log2_max_poc_lsb - 4);
        return -1;
    }

    sublayer_ordering_info = get_bits1(gb);
    start = sublayer_ordering_info ? 0 : sps->max_sub_layers - 1;
    for (i = start; i < sps->max_sub_layers; i++)
    {
        sps->temporal_layer[i].max_dec_pic_buffering = get_ue_golomb_long(gb) + 1;
        sps->temporal_layer[i].num_reorder_pics      = get_ue_golomb_long(gb);
        sps->temporal_layer[i].max_latency_increase  = get_ue_golomb_long(gb) - 1;
        if (sps->temporal_layer[i].max_dec_pic_buffering > MAX_DPB_SIZE)
        {
            fprintf(stderr, "sps_max_dec_pic_buffering_minus1 out of range: %d\n", sps->temporal_layer[i].max_dec_pic_buffering - 1);
            return -1;
        }
        if (sps->temporal_layer[i].num_reorder_pics > sps->temporal_layer[i].max_dec_pic_buffering - 1)
        {
            fprintf(stderr, "sps_max_num_reorder_pics out of range: %d\n", sps->temporal_layer[i].num_reorder_pics);
            if (sps->temporal_layer[i].num_reorder_pics > MAX_DPB_SIZE - 1)
                return -1;
            sps->temporal_layer[i].max_dec_pic_buffering = sps->temporal_layer[i].num_reorder_pics + 1;
        }
    }

    if (!sublayer_ordering_info)
    {
        for (i = 0; i < start; i++)
        {
            sps->temporal_layer[i].max_dec_pic_buffering = sps->temporal_layer[start].max_dec_pic_buffering;
            sps->temporal_layer[i].num_reorder_pics      = sps->temporal_layer[start].num_reorder_pics;
            sps->temporal_layer[i].max_latency_increase  = sps->temporal_layer[start].max_latency_increase;
        }
    }

    sps->log2_min_cb_size                    = get_ue_golomb_long(gb) + 3;
    sps->log2_diff_max_min_coding_block_size = get_ue_golomb_long(gb);
    sps->log2_min_tb_size                    = get_ue_golomb_long(gb) + 2;
    log2_diff_max_min_transform_block_size   = get_ue_golomb_long(gb);
    sps->log2_max_trafo_size                 = log2_diff_max_min_transform_block_size +
                                               sps->log2_min_tb_size;

    if (sps->log2_min_cb_size < 3 || sps->log2_min_cb_size > 30)
    {
        fprintf(stderr, "Invalid value %d for log2_min_cb_size", sps->log2_min_cb_size);
        return -1;
    }

    if (sps->log2_diff_max_min_coding_block_size > 30)
    {
        fprintf(stderr, "Invalid value %d for log2_diff_max_min_coding_block_size", sps->log2_diff_max_min_coding_block_size);
        return -1;
    }

    if (sps->log2_min_tb_size >= sps->log2_min_cb_size || sps->log2_min_tb_size < 2)
    {
        fprintf(stderr,  "Invalid value for log2_min_tb_size");
        return -1;
    }

    if (log2_diff_max_min_transform_block_size < 0 || log2_diff_max_min_transform_block_size > 30)
    {
        fprintf(stderr, "Invalid value %d for log2_diff_max_min_transform_block_size", log2_diff_max_min_transform_block_size);
        return -1;
    }

    sps->max_transform_hierarchy_depth_inter = get_ue_golomb_long(gb);
    sps->max_transform_hierarchy_depth_intra = get_ue_golomb_long(gb);

    sps->scaling_list_enable_flag = (uint8_t)get_bits1(gb);
    if (sps->scaling_list_enable_flag)
    {
        set_default_scaling_list_data(&sps->scaling_list);

        if (get_bits1(gb))
        {
            ret = scaling_list_data(gb, &sps->scaling_list, sps);
            if (ret < 0)
                return ret;
        }
    }

    sps->amp_enabled_flag = (uint8_t)get_bits1(gb);
    sps->sao_enabled      = (uint8_t)get_bits1(gb);

    sps->pcm_enabled_flag = (uint8_t)get_bits1(gb);
    if (sps->pcm_enabled_flag) {
        sps->pcm.bit_depth   = (uint8_t)(get_bits(gb, 4) + 1);
        sps->pcm.bit_depth_chroma = (uint8_t)(get_bits(gb, 4) + 1);
        sps->pcm.log2_min_pcm_cb_size = get_ue_golomb_long(gb) + 3;
        sps->pcm.log2_max_pcm_cb_size = sps->pcm.log2_min_pcm_cb_size +
                                        get_ue_golomb_long(gb);
        if (sps->pcm.bit_depth > sps->bit_depth) {
            fprintf(stderr, "PCM bit depth (%d) is greater than normal bit depth (%d)\n", sps->pcm.bit_depth, sps->bit_depth);
            return -1;
        }

        sps->pcm.loop_filter_disable_flag = (uint8_t)get_bits1(gb);
    }

    sps->nb_st_rps = get_ue_golomb_long(gb);
    if (sps->nb_st_rps > MAX_SHORT_TERM_RPS_COUNT)
    {
        fprintf(stderr, "Too many short term RPS: %d.\n", sps->nb_st_rps);
        return -1;
    }
    for (i = 0; i < sps->nb_st_rps; i++)
        if ((ret = ff_hevc_decode_short_term_rps(gb, &sps->st_rps[i], sps, 0)) < 0)
            return ret;

    sps->long_term_ref_pics_present_flag = (uint8_t)get_bits1(gb);
    if (sps->long_term_ref_pics_present_flag)
    {
        sps->num_long_term_ref_pics_sps = (uint8_t)get_ue_golomb_long(gb);
        if (sps->num_long_term_ref_pics_sps > 31U)
        {
            fprintf(stderr, "num_long_term_ref_pics_sps %d is out of range.\n", sps->num_long_term_ref_pics_sps);
            return -1;
        }
        for (i = 0; i < sps->num_long_term_ref_pics_sps; i++)
        {
            sps->lt_ref_pic_poc_lsb_sps[i]       = (uint8_t)get_bits(gb, sps->log2_max_poc_lsb);
            sps->used_by_curr_pic_lt_sps_flag[i] = (uint8_t)get_bits1(gb);
        }
    }

    sps->sps_temporal_mvp_enabled_flag          = (uint8_t)get_bits1(gb);
    sps->sps_strong_intra_smoothing_enable_flag = (uint8_t)get_bits1(gb);
    sps->vui.sar = (Rational){0, 1};
    vui_present = get_bits1(gb);
    if (vui_present)
        decode_vui(gb, sps);

    if (get_bits1(gb))
    { // sps_extension_flag
        int sps_extension_flag[1];
        for (i = 0; i < 1; i++)
            sps_extension_flag[i] = get_bits1(gb);
        skip_bits(gb, 7); //sps_extension_7bits = get_bits(gb, 7);
        if (sps_extension_flag[0])
        {
            int extended_precision_processing_flag;
            int high_precision_offsets_enabled_flag;
            int cabac_bypass_alignment_enabled_flag;

            sps->transform_skip_rotation_enabled_flag = get_bits1(gb);
            sps->transform_skip_context_enabled_flag  = get_bits1(gb);
            sps->implicit_rdpcm_enabled_flag = get_bits1(gb);

            sps->explicit_rdpcm_enabled_flag = get_bits1(gb);

            extended_precision_processing_flag = get_bits1(gb);
            if (extended_precision_processing_flag)
                fprintf(stderr, "extended_precision_processing_flag not yet implemented\n");

            sps->intra_smoothing_disabled_flag       = get_bits1(gb);
            high_precision_offsets_enabled_flag  = get_bits1(gb);
            if (high_precision_offsets_enabled_flag)
                fprintf(stderr, "high_precision_offsets_enabled_flag not yet implemented\n");
            sps->persistent_rice_adaptation_enabled_flag = get_bits1(gb);

            cabac_bypass_alignment_enabled_flag  = get_bits1(gb);
            if (cabac_bypass_alignment_enabled_flag)
                fprintf(stderr, "cabac_bypass_alignment_enabled_flag not yet implemented\n");
        }
    }
    if (h->apply_defdispwin) {
        sps->output_window.left_offset   += sps->vui.def_disp_win.left_offset;
        sps->output_window.right_offset  += sps->vui.def_disp_win.right_offset;
        sps->output_window.top_offset    += sps->vui.def_disp_win.top_offset;
        sps->output_window.bottom_offset += sps->vui.def_disp_win.bottom_offset;
    }
    if (sps->output_window.left_offset & (0x1F >> (sps->pixel_shift)))
    {
        sps->output_window.left_offset &= ~(0x1F >> (sps->pixel_shift));
        fprintf(stderr, "Reducing left output window to %d chroma samples to preserve alignment.\n", sps->output_window.left_offset);
    }
    sps->output_width  = sps->width - (sps->output_window.left_offset + sps->output_window.right_offset);
    sps->output_height = sps->height - (sps->output_window.top_offset + sps->output_window.bottom_offset);
    if (sps->width  <= sps->output_window.left_offset + (int64_t)sps->output_window.right_offset  ||
        sps->height <= sps->output_window.top_offset  + (int64_t)sps->output_window.bottom_offset)
    {
        fprintf(stderr, "Invalid visible frame dimensions: %dx%d.\n", sps->output_width, sps->output_height);
        fprintf(stderr, "Displaying the whole video surface.\n");
        memset(&sps->pic_conf_win, 0, sizeof(sps->pic_conf_win));
        memset(&sps->output_window, 0, sizeof(sps->output_window));
        sps->output_width               = sps->width;
        sps->output_height              = sps->height;
    }

    // Inferred parameters
    sps->log2_ctb_size = sps->log2_min_cb_size + sps->log2_diff_max_min_coding_block_size;
    sps->log2_min_pu_size = sps->log2_min_cb_size - 1;

    if (sps->log2_ctb_size > MAX_LOG2_CTB_SIZE)
    {
        fprintf(stderr,  "CTB size out of range: 2^%d\n", sps->log2_ctb_size);
        return -1;
    }
    if (sps->log2_ctb_size < 4)
    {
        fprintf(stderr, "log2_ctb_size %d differs from the bounds of any known profile\n", sps->log2_ctb_size);
        return -1;
    }

    sps->ctb_width  = (sps->width  + (1 << sps->log2_ctb_size) - 1) >> sps->log2_ctb_size;
    sps->ctb_height = (sps->height + (1 << sps->log2_ctb_size) - 1) >> sps->log2_ctb_size;
    sps->ctb_size   = sps->ctb_width * sps->ctb_height;

    sps->min_cb_width  = sps->width  >> sps->log2_min_cb_size;
    sps->min_cb_height = sps->height >> sps->log2_min_cb_size;
    sps->min_tb_width  = sps->width  >> sps->log2_min_tb_size;
    sps->min_tb_height = sps->height >> sps->log2_min_tb_size;
    sps->min_pu_width  = sps->width  >> sps->log2_min_pu_size;
    sps->min_pu_height = sps->height >> sps->log2_min_pu_size;
    sps->tb_mask       = (1 << (sps->log2_ctb_size - sps->log2_min_tb_size)) - 1;

    sps->qp_bd_offset = 6 * (sps->bit_depth - 8);

    if (fd_mod_uintp2_c((uint32_t)sps->width, sps->log2_min_cb_size) || fd_mod_uintp2_c((uint32_t)sps->height, sps->log2_min_cb_size))
    {
        fprintf(stderr, "Invalid coded frame dimensions.\n");
        return -1;
    }

    if (sps->max_transform_hierarchy_depth_inter > sps->log2_ctb_size - sps->log2_min_tb_size)
    {
        fprintf(stderr, "max_transform_hierarchy_depth_inter out of range: %d\n", sps->max_transform_hierarchy_depth_inter);
        return -1;
    }
    if (sps->max_transform_hierarchy_depth_intra > sps->log2_ctb_size - sps->log2_min_tb_size)
    {
        fprintf(stderr, "max_transform_hierarchy_depth_intra out of range: %d\n", sps->max_transform_hierarchy_depth_intra);
        return -1;
    }
    if (sps->log2_max_trafo_size > FFMIN(sps->log2_ctb_size, 5))
    {
        fprintf(stderr, "max transform block size out of range: %d\n", sps->log2_max_trafo_size);
        return -1;
    }

    if (get_bits_left(gb) < 0)
    {
        fprintf(stderr, "Overread SPS by %d bits\n", -get_bits_left(gb));
        return -1;
    }
    fprintf(stderr, "Parsed SPS: id %d; coded wxh: %dx%d; cropped wxh: %dx%d;", sps_id, sps->width, sps->height, sps->output_width, sps->output_height);


    /* check if this is a repeat of an already parsed SPS, then keep the
     * original one.
     * otherwise drop all PPSes that depend on it */
    fd_free((void *)h->ps->sps_list[sps_id]);
    h->ps->sps_list[sps_id] = sps;

    return 0;
}

static int pps_range_extensions(GetBitContext *gb, HEVCPPS *pps)
{
    int i;

    if (pps->transform_skip_enabled_flag)
        pps->log2_max_transform_skip_block_size = (uint8_t)(get_ue_golomb_long(gb) + 2);
    pps->cross_component_prediction_enabled_flag = (uint8_t)get_bits1(gb);
    pps->chroma_qp_offset_list_enabled_flag = (uint8_t)get_bits1(gb);
    if (pps->chroma_qp_offset_list_enabled_flag) {
        pps->diff_cu_chroma_qp_offset_depth = (uint8_t)get_ue_golomb_long(gb);
        pps->chroma_qp_offset_list_len_minus1 = (uint8_t)get_ue_golomb_long(gb);
        if (pps->chroma_qp_offset_list_len_minus1 && pps->chroma_qp_offset_list_len_minus1 >= 5)
        {
            fprintf(stderr, "chroma_qp_offset_list_len_minus1 shall be in the range [0, 5].\n");
            return -1;
        }
        for (i = 0; i <= pps->chroma_qp_offset_list_len_minus1; i++)
        {
            pps->cb_qp_offset_list[i] = (int8_t)get_se_golomb_long(gb);
            if (pps->cb_qp_offset_list[i])
                fprintf(stderr, "cb_qp_offset_list not tested yet.\n");
            pps->cr_qp_offset_list[i] = (int8_t)get_se_golomb_long(gb);
            if (pps->cr_qp_offset_list[i])
                fprintf(stderr, "cb_qp_offset_list not tested yet.\n");
        }
    }
    pps->log2_sao_offset_scale_luma = (uint8_t)get_ue_golomb_long(gb);
    pps->log2_sao_offset_scale_chroma = (uint8_t)get_ue_golomb_long(gb);

    return 0;
}

static inline int setup_pps(GetBitContext *gb, HEVCPPS *pps, HEVCSPS *sps)
{
    int log2_diff;
    int pic_area_in_ctbs;
    int i, j, x, y, ctb_addr_rs, tile_id;

    // Inferred parameters
    pps->col_bd   = fd_malloc((pps->num_tile_columns + 1) * sizeof(*pps->col_bd));
    pps->row_bd   = fd_malloc((pps->num_tile_rows + 1) * sizeof(*pps->row_bd));
    pps->col_idxX = fd_malloc(sps->ctb_width * sizeof(*pps->col_idxX));
    if (!pps->col_bd || !pps->row_bd || !pps->col_idxX)
        return -1;

    if (pps->uniform_spacing_flag)
    {
        if (!pps->column_width)
        {
            pps->column_width = fd_malloc(pps->num_tile_columns * sizeof(*pps->column_width));
            pps->row_height   = fd_malloc(pps->num_tile_rows * sizeof(*pps->row_height));
        }
        if (!pps->column_width || !pps->row_height)
            return -1;

        for (i = 0; i < pps->num_tile_columns; i++)
            pps->column_width[i] = (uint32_t)(((i + 1) * sps->ctb_width) / pps->num_tile_columns - (i * sps->ctb_width) / pps->num_tile_columns);

        for (i = 0; i < pps->num_tile_rows; i++)
            pps->row_height[i] = (uint32_t)(((i + 1) * sps->ctb_height) / pps->num_tile_rows - (i * sps->ctb_height) / pps->num_tile_rows);
    }

    pps->col_bd[0] = 0;
    for (i = 0; i < pps->num_tile_columns; i++)
        pps->col_bd[i + 1] = pps->col_bd[i] + pps->column_width[i];

    pps->row_bd[0] = 0;
    for (i = 0; i < pps->num_tile_rows; i++)
        pps->row_bd[i + 1] = pps->row_bd[i] + pps->row_height[i];

    for (i = 0, j = 0; i < sps->ctb_width; i++) {
        if (i > pps->col_bd[j])
            j++;
        pps->col_idxX[i] = j;
    }

    /**
     * 6.5
     */
    pic_area_in_ctbs     = sps->ctb_width    * sps->ctb_height;

    pps->ctb_addr_rs_to_ts = fd_malloc(pic_area_in_ctbs *sizeof(*pps->ctb_addr_rs_to_ts));
    pps->ctb_addr_ts_to_rs = fd_malloc(pic_area_in_ctbs * sizeof(*pps->ctb_addr_ts_to_rs));
    pps->tile_id           = fd_malloc(pic_area_in_ctbs * sizeof(*pps->tile_id));
    pps->min_tb_addr_zs_tab = fd_malloc((sps->tb_mask+2) * (sps->tb_mask+2) * sizeof(*pps->min_tb_addr_zs_tab));
    if (!pps->ctb_addr_rs_to_ts || !pps->ctb_addr_ts_to_rs || !pps->tile_id || !pps->min_tb_addr_zs_tab)
        return -1;

    for (ctb_addr_rs = 0; ctb_addr_rs < pic_area_in_ctbs; ctb_addr_rs++)
    {
        int tb_x   = ctb_addr_rs % sps->ctb_width;
        int tb_y   = ctb_addr_rs / sps->ctb_width;
        int tile_x = 0;
        int tile_y = 0;
        int val    = 0;

        for (i = 0; i < pps->num_tile_columns; i++)
        {
            if (tb_x < pps->col_bd[i + 1])
            {
                tile_x = i;
                break;
            }
        }

        for (i = 0; i < pps->num_tile_rows; i++)
        {
            if (tb_y < pps->row_bd[i + 1])
            {
                tile_y = i;
                break;
            }
        }

        for (i = 0; i < tile_x; i++)
            val += pps->row_height[tile_y] * pps->column_width[i];
        for (i = 0; i < tile_y; i++)
            val += sps->ctb_width * pps->row_height[i];

        val += (tb_y - pps->row_bd[tile_y]) * pps->column_width[tile_x] + tb_x - pps->col_bd[tile_x];

        pps->ctb_addr_rs_to_ts[ctb_addr_rs] = val;
        pps->ctb_addr_ts_to_rs[val]         = ctb_addr_rs;
    }

    for (j = 0, tile_id = 0; j < pps->num_tile_rows; j++)
        for (i = 0; i < pps->num_tile_columns; i++, tile_id++)
            for (y = pps->row_bd[j]; y < pps->row_bd[j + 1]; y++)
                for (x = pps->col_bd[i]; x < pps->col_bd[i + 1]; x++)
                    pps->tile_id[pps->ctb_addr_rs_to_ts[y * sps->ctb_width + x]] = tile_id;

    pps->tile_pos_rs = fd_malloc(tile_id * sizeof(*pps->tile_pos_rs));
    if (!pps->tile_pos_rs)
        return -1;

    for (j = 0; j < pps->num_tile_rows; j++)
        for (i = 0; i < pps->num_tile_columns; i++)
            pps->tile_pos_rs[j * pps->num_tile_columns + i] =
                    pps->row_bd[j] * sps->ctb_width + pps->col_bd[i];

    log2_diff = sps->log2_ctb_size - sps->log2_min_tb_size;
    pps->min_tb_addr_zs = &pps->min_tb_addr_zs_tab[1*(sps->tb_mask+2)+1];
    for (y = 0; y < sps->tb_mask+2; y++) {
        pps->min_tb_addr_zs_tab[y*(sps->tb_mask+2)] = -1;
        pps->min_tb_addr_zs_tab[y]    = -1;
    }
    for (y = 0; y < sps->tb_mask+1; y++)
    {
        for (x = 0; x < sps->tb_mask+1; x++)
        {
            int tb_x = x >> log2_diff;
            int tb_y = y >> log2_diff;
            int rs   = sps->ctb_width * tb_y + tb_x;
            int val  = pps->ctb_addr_rs_to_ts[rs] << (log2_diff * 2);
            for (i = 0; i < log2_diff; i++)
            {
                int m = 1 << i;
                val += (m & x ? m * m : 0) + (m & y ? 2 * m * m : 0);
            }
            pps->min_tb_addr_zs[y * (sps->tb_mask+2) + x] = val;
        }
    }
    return 0;
}

static int fd_hevc_decode_nal_pps(GetBitContext *gb, HEVCContext *h)
{
    HEVCSPS *sps = NULL;
    int i, ret = 0;
    unsigned int pps_id = 0;

    HEVCPPS *pps = (HEVCPPS *)fd_malloc(sizeof(HEVCSPS));

    if (!pps)
        return -1;

    fprintf(stderr, "Decoding PPS\n");
    // Default values
    pps->loop_filter_across_tiles_enabled_flag = 1;
    pps->num_tile_columns                      = 1;
    pps->num_tile_rows                         = 1;
    pps->uniform_spacing_flag                  = 1;
    pps->disable_dbf                           = 0;
    pps->beta_offset                           = 0;
    pps->tc_offset                             = 0;
    pps->log2_max_transform_skip_block_size    = 2;

    // Coded parameters
    pps_id = get_ue_golomb_long(gb);
    if (pps_id >= MAX_PPS_COUNT)
    {
        fprintf(stderr, "PPS id out of range: %d\n", pps_id);
        ret = -1;
        goto err;
    }
    pps->sps_id = get_ue_golomb_long(gb);
    if (pps->sps_id >= MAX_SPS_COUNT)
    {
        fprintf(stderr, "SPS id out of range: %d\n", pps->sps_id);
        ret = -1;
        goto err;
    }
    if (!h->ps->sps_list[pps->sps_id]) {
        fprintf(stderr, "SPS %u does not exist.\n", pps->sps_id);
        ret = -1;
        goto err;
    }
    sps = h->ps->sps_list[pps->sps_id];

    pps->dependent_slice_segments_enabled_flag = (uint8_t)get_bits1(gb);
    pps->output_flag_present_flag              = (uint8_t)get_bits1(gb);
    pps->num_extra_slice_header_bits           = get_bits(gb, 3);

    pps->sign_data_hiding_flag = (uint8_t)get_bits1(gb);

    pps->cabac_init_present_flag = (uint8_t)get_bits1(gb);

    pps->num_ref_idx_l0_default_active = get_ue_golomb_long(gb) + 1;
    pps->num_ref_idx_l1_default_active = get_ue_golomb_long(gb) + 1;

    pps->pic_init_qp_minus26 = get_se_golomb(gb);

    pps->constrained_intra_pred_flag = (uint8_t)get_bits1(gb);
    pps->transform_skip_enabled_flag = (uint8_t)get_bits1(gb);

    pps->cu_qp_delta_enabled_flag = (uint8_t)get_bits1(gb);
    pps->diff_cu_qp_delta_depth   = 0;
    if (pps->cu_qp_delta_enabled_flag)
        pps->diff_cu_qp_delta_depth = get_ue_golomb_long(gb);

    if (pps->diff_cu_qp_delta_depth < 0 || pps->diff_cu_qp_delta_depth > sps->log2_diff_max_min_coding_block_size)
    {
        fprintf(stderr, "diff_cu_qp_delta_depth %d is invalid\n", pps->diff_cu_qp_delta_depth);
        ret = -1;
        goto err;
    }

    pps->cb_qp_offset = get_se_golomb(gb);
    if (pps->cb_qp_offset < -12 || pps->cb_qp_offset > 12)
    {
        fprintf(stderr, "pps_cb_qp_offset out of range: %d\n", pps->cb_qp_offset);
        ret = -1;
        goto err;
    }
    pps->cr_qp_offset = get_se_golomb(gb);
    if (pps->cr_qp_offset < -12 || pps->cr_qp_offset > 12)
    {
        fprintf(stderr, "pps_cr_qp_offset out of range: %d\n", pps->cr_qp_offset);
        ret = -1;
        goto err;
    }
    pps->pic_slice_level_chroma_qp_offsets_present_flag = (uint8_t)get_bits1(gb);

    pps->weighted_pred_flag   = (uint8_t)get_bits1(gb);
    pps->weighted_bipred_flag = (uint8_t)get_bits1(gb);

    pps->transquant_bypass_enable_flag    = (uint8_t)get_bits1(gb);
    pps->tiles_enabled_flag               = (uint8_t)get_bits1(gb);
    pps->entropy_coding_sync_enabled_flag = (uint8_t)get_bits1(gb);

    if (pps->tiles_enabled_flag)
    {
        pps->num_tile_columns = get_ue_golomb_long(gb) + 1;
        pps->num_tile_rows    = get_ue_golomb_long(gb) + 1;
        if (pps->num_tile_columns <= 0 || pps->num_tile_columns >= sps->width)
        {
            fprintf(stderr, "num_tile_columns_minus1 out of range: %d\n", pps->num_tile_columns - 1);
            ret = -1;
            goto err;
        }
        if (pps->num_tile_rows <= 0 || pps->num_tile_rows >= sps->height)
        {
            fprintf(stderr, "num_tile_rows_minus1 out of range: %d\n", pps->num_tile_rows - 1);
            ret = -1;
            goto err;
        }

        pps->column_width = fd_malloc(pps->num_tile_columns * (sizeof(*pps->column_width)));
        pps->row_height   = fd_malloc(pps->num_tile_rows * sizeof(*pps->row_height));
        if (!pps->column_width || !pps->row_height)
        {
            ret = -1;
            goto err;
        }
        pps->uniform_spacing_flag = (uint8_t)get_bits1(gb);
        if (!pps->uniform_spacing_flag)
        {
            uint64_t sum = 0;
            for (i = 0; i < pps->num_tile_columns - 1; i++)
            {
                pps->column_width[i] = get_ue_golomb_long(gb) + 1;
                sum                 += pps->column_width[i];
            }
            if (sum >= sps->ctb_width)
            {
                fprintf(stderr, "Invalid tile widths.\n");
                ret = -1;
                goto err;
            }
            pps->column_width[pps->num_tile_columns - 1] = (uint32_t)(sps->ctb_width - sum);

            sum = 0;
            for (i = 0; i < pps->num_tile_rows - 1; i++)
            {
                pps->row_height[i] = get_ue_golomb_long(gb) + 1;
                sum               += pps->row_height[i];
            }
            if (sum >= sps->ctb_height)
            {
                fprintf(stderr, "Invalid tile heights.\n");
                ret = -1;
                goto err;
            }
            pps->row_height[pps->num_tile_rows - 1] = (uint32_t)(sps->ctb_height - sum);
        }
        pps->loop_filter_across_tiles_enabled_flag = (uint8_t)get_bits1(gb);
    }

    pps->seq_loop_filter_across_slices_enabled_flag = (uint8_t)get_bits1(gb);

    pps->deblocking_filter_control_present_flag = (uint8_t)get_bits1(gb);
    if (pps->deblocking_filter_control_present_flag)
    {
        pps->deblocking_filter_override_enabled_flag = (uint8_t)get_bits1(gb);
        pps->disable_dbf                             = (uint8_t)get_bits1(gb);
        if (!pps->disable_dbf)
        {
            pps->beta_offset = get_se_golomb(gb) * 2;
            pps->tc_offset = get_se_golomb(gb) * 2;
            if (pps->beta_offset/2 < -6 || pps->beta_offset/2 > 6)
            {
                fprintf(stderr, "pps_beta_offset_div2 out of range: %d\n", pps->beta_offset/2);
                ret = -1;
                goto err;
            }
            if (pps->tc_offset/2 < -6 || pps->tc_offset/2 > 6)
            {
                fprintf(stderr, "pps_tc_offset_div2 out of range: %d\n", pps->tc_offset/2);
                ret = -1;
                goto err;
            }
        }
    }

    pps->scaling_list_data_present_flag = (uint8_t)get_bits1(gb);
    if (pps->scaling_list_data_present_flag)
    {
        set_default_scaling_list_data(&pps->scaling_list);
        ret = scaling_list_data(gb, &pps->scaling_list, sps);
        if (ret < 0)
            goto err;
    }
    pps->lists_modification_present_flag = (uint8_t)get_bits1(gb);
    pps->log2_parallel_merge_level       = get_ue_golomb_long(gb) + 2;
    if (pps->log2_parallel_merge_level > sps->log2_ctb_size)
    {
        fprintf(stderr, "log2_parallel_merge_level_minus2 out of range: %d\n", pps->log2_parallel_merge_level - 2);
        ret = -1;
        goto err;
    }

    pps->slice_header_extension_present_flag = (uint8_t)get_bits1(gb);

    if (get_bits1(gb))
    { // pps_extension_present_flag
        int pps_range_extensions_flag = get_bits1(gb);
        /* int pps_extension_7bits = */ get_bits(gb, 7);
        if (sps->ptl.general_ptl.profile_idc == FF_PROFILE_HEVC_REXT && pps_range_extensions_flag)
            if ((ret = pps_range_extensions(gb, pps)) < 0)
                goto err;
    }

    ret = setup_pps(gb, pps, sps);
    if (ret < 0)
        goto err;

    if (get_bits_left(gb) < 0)
    {
        fprintf(stderr, "Overread PPS by %d bits\n", -get_bits_left(gb));
        goto err;
    }

    fd_free((void *)h->ps->pps_list[pps_id]);
    h->ps->pps_list[pps_id] = pps;
    return 0;

err:
    fd_free((void *)pps);
    return ret;
}


int decode_nal_unit(HEVCContext *h)
{
    int ret;
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
        uint8_t *rbsp = (uint8_t *)(bs + ptr_bs_back);
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