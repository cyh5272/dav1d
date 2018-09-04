/*
 * ..
 */

#include <assert.h>
#include <string.h>

#include "common/intops.h"

#include "src/levels.h"
#include "src/lf_mask.h"
#include "src/tables.h"

static void decomp_tx(uint8_t (*txa)[2 /* txsz, step */][32 /* y */][32 /* x */],
                      const enum RectTxfmSize from,
                      const int depth,
                      const int y_off, const int x_off,
                      const uint16_t *const tx_masks)
{
    const TxfmInfo *const t_dim = &av1_txfm_dimensions[from];
    int is_split;

    if (depth > 1) {
        is_split = 0;
    } else {
        const int off = y_off * 4 + x_off;
        is_split = (tx_masks[depth] >> off) & 1;
    }

    if (is_split) {
        const enum RectTxfmSize sub = t_dim->sub;
        const int htw4 = t_dim->w >> 1, hth4 = t_dim->h >> 1;

        decomp_tx(txa, sub, depth + 1, y_off * 2 + 0, x_off * 2 + 0, tx_masks);
        if (t_dim->w >= t_dim->h)
            decomp_tx((uint8_t(*)[2][32][32]) &txa[0][0][0][htw4],
                      sub, depth + 1, y_off * 2 + 0, x_off * 2 + 1, tx_masks);
        if (t_dim->h >= t_dim->w) {
            decomp_tx((uint8_t(*)[2][32][32]) &txa[0][0][hth4][0],
                      sub, depth + 1, y_off * 2 + 1, x_off * 2 + 0, tx_masks);
            if (t_dim->w >= t_dim->h)
                decomp_tx((uint8_t(*)[2][32][32]) &txa[0][0][hth4][htw4],
                          sub, depth + 1, y_off * 2 + 1, x_off * 2 + 1, tx_masks);
        }
    } else {
        const int lw = imin(2, t_dim->lw), lh = imin(2, t_dim->lh);
        int y;

        for (y = 0; y < t_dim->h; y++) {
            memset(txa[0][0][y], lw, t_dim->w);
            memset(txa[1][0][y], lh, t_dim->w);
            txa[0][1][y][0] = t_dim->w;
        }
        memset(txa[1][1][0], t_dim->h, t_dim->w);
    }
}

static inline void mask_edges_inter(uint32_t (*masks)[32][3],
                                    const int by4, const int bx4,
                                    const int w4, const int h4, const int skip,
                                    const enum RectTxfmSize max_tx,
                                    const uint16_t *const tx_masks,
                                    uint8_t *const a, uint8_t *const l)
{
    const TxfmInfo *const t_dim = &av1_txfm_dimensions[max_tx];
    int y, x;

    uint8_t txa[2 /* edge */][2 /* txsz, step */][32 /* y */][32 /* x */];
    int y_off, x_off;
    for (y_off = 0, y = 0; y < h4; y += t_dim->h, y_off++)
        for (x_off = 0, x = 0; x < w4; x += t_dim->w, x_off++)
            decomp_tx((uint8_t(*)[2][32][32]) &txa[0][0][y][x],
                      max_tx, 0, y_off, x_off, tx_masks);

    // left block edge
    unsigned mask = 1U << bx4;
    for (y = 0; y < h4; y++)
        masks[0][by4 + y][imin(txa[0][0][y][0], l[y])] |= mask;

    // top block edge
    for (x = 0; x < w4; x++, mask <<= 1)
        masks[1][by4][imin(txa[1][0][0][x], a[x])] |= mask;

    if (!skip) {
        // inner (tx) left|right edges
        for (y = 0; y < h4; y++) {
            int ltx = txa[0][0][y][0];
            int step = txa[0][1][y][0];
            for (x = step, mask = 1U << (bx4 + step);
                 x < w4; x += step, mask <<= step)
            {
                const int rtx = txa[0][0][y][x];
                masks[0][by4 + y][imin(rtx, ltx)] |= mask;
                ltx = rtx;
                step = txa[0][1][y][x];
            }
        }

        //            top
        // inner (tx) --- edges
        //           bottom
        for (x = 0, mask = 1U << bx4; x < w4; x++, mask <<= 1) {
            int ttx = txa[1][0][0][x];
            int step = txa[1][1][0][x];
            for (y = step; y < h4; y += step) {
                const int btx = txa[1][0][y][x];
                masks[1][by4 + y][imin(ttx, btx)] |= mask;
                ttx = btx;
                step = txa[1][1][y][x];
            }
        }
    }

    for (y = 0; y < h4; y++)
        l[y] = txa[0][0][y][w4 - 1];
    memcpy(a, txa[1][0][h4 - 1], w4);
}

static inline void mask_edges_intra(uint32_t (*const masks)[32][3],
                                    const int by4, const int bx4,
                                    const int w4, const int h4,
                                    const enum RectTxfmSize tx,
                                    uint8_t *const a, uint8_t *const l)
{
    const TxfmInfo *const t_dim = &av1_txfm_dimensions[tx];
    const int twl4 = t_dim->lw, thl4 = t_dim->lh;
    const int twl4c = imin(2, twl4), thl4c = imin(2, thl4);
    int y, x;

    // left block edge
    unsigned mask = 1U << bx4;
    for (y = 0; y < h4; y++)
        masks[0][by4 + y][imin(twl4c, l[y])] |= mask;

    // top block edge
    for (mask = 1U << bx4, x = 0; x < w4; x++, mask <<= 1)
        masks[1][by4][imin(thl4c, a[x])] |= mask;

    static const uint32_t hstep[] = {
        0xffffffff, 0x55555555, 0x11111111, 0x01010101, 0x00010001
    };

    // inner (tx) left|right edges
    const unsigned t = 1U << bx4;
    const unsigned inner = (((uint64_t) t) << w4) - t;
    mask = (inner - t) & hstep[twl4];
    for (y = 0; y < h4; y++)
        masks[0][by4 + y][twl4c] |= mask;

    //            top
    // inner (tx) --- edges
    //           bottom
    const int vstep = t_dim->h;
    for (y = vstep; y < h4; y += vstep)
        masks[1][by4 + y][thl4c] |= inner;

    memset(a, thl4c, w4);
    memset(l, twl4c, h4);
}

static inline void mask_edges_chroma(uint32_t (*const masks)[32][2],
                                     const int cby4, const int cbx4,
                                     const int cw4, const int ch4,
                                     const int skip_inter,
                                     const enum RectTxfmSize tx,
                                     uint8_t *const a, uint8_t *const l)
{
    const TxfmInfo *const t_dim = &av1_txfm_dimensions[tx];
    const int twl4 = t_dim->lw, thl4 = t_dim->lh;
    const int twl4c = !!twl4, thl4c = !!thl4;
    int y, x;

    // left block edge
    unsigned mask = 1U << cbx4;
    for (y = 0; y < ch4; y++)
        masks[0][cby4 + y][imin(twl4c, l[y])] |= mask;

    // top block edge
    for (mask = 1U << cbx4, x = 0; x < cw4; x++, mask <<= 1)
        masks[1][cby4][imin(thl4c, a[x])] |= mask;

    if (!skip_inter) {
        static const uint32_t hstep[] = {
            0xffffffff, 0x55555555, 0x11111111, 0x01010101
        };

        // inner (tx) left|right edges
        const int t = 1U << cbx4;
        const unsigned inner = (((uint64_t) t) << cw4) - t;
        mask = (inner - t) & hstep[twl4];
        for (y = 0; y < ch4; y++)
            masks[0][cby4 + y][twl4c] |= mask;

        //            top
        // inner (tx) --- edges
        //           bottom
        const int vstep = t_dim->h;
        for (y = vstep; y < ch4; y += vstep)
            masks[1][cby4 + y][thl4c] |= inner;
    }

    memset(a, thl4c, cw4);
    memset(l, twl4c, ch4);
}

void dav1d_create_lf_mask_intra(Av1Filter *const lflvl,
                                uint8_t (*level_cache)[4],
                                const ptrdiff_t b4_stride,
                                const Av1FrameHeader *const hdr,
                                const uint8_t (*filter_level)[8][2],
                                const int bx, const int by,
                                const int iw, const int ih,
                                const enum BlockSize bs,
                                const enum RectTxfmSize ytx,
                                const enum RectTxfmSize uvtx,
                                const enum Dav1dPixelLayout layout,
                                uint8_t *const ay, uint8_t *const ly,
                                uint8_t *const auv, uint8_t *const luv)
{
    if (!hdr->loopfilter.level_y[0] && !hdr->loopfilter.level_y[1])
        return;

    const uint8_t *const b_dim = av1_block_dimensions[bs];
    const int bw4 = imin(iw - bx, b_dim[0]);
    const int bh4 = imin(ih - by, b_dim[1]);
    const int bx4 = bx & 31;
    const int by4 = by & 31;

    level_cache += by * b4_stride + bx;
    for (int y = 0; y < bh4; y++) {
        for (int x = 0; x < bw4; x++) {
            level_cache[x][0] = filter_level[0][0][0];
            level_cache[x][1] = filter_level[1][0][0];
            level_cache[x][2] = filter_level[2][0][0];
            level_cache[x][3] = filter_level[3][0][0];
        }
        level_cache += b4_stride;
    }

    mask_edges_intra(lflvl->filter_y, by4, bx4, bw4, bh4, ytx, ay, ly);

    if (!auv) return;

    const int ss_ver = layout == DAV1D_PIXEL_LAYOUT_I420;
    const int ss_hor = layout != DAV1D_PIXEL_LAYOUT_I444;
    const int cbw4 = (bw4 + ss_hor) >> ss_hor;
    const int cbh4 = (bh4 + ss_ver) >> ss_ver;
    const int cbx4 = bx4 >> ss_hor;
    const int cby4 = by4 >> ss_ver;

    mask_edges_chroma(lflvl->filter_uv, cby4, cbx4, cbw4, cbh4, 0, uvtx, auv, luv);
}

void dav1d_create_lf_mask_inter(Av1Filter *const lflvl,
                                uint8_t (*level_cache)[4],
                                const ptrdiff_t b4_stride,
                                const Av1FrameHeader *const hdr,
                                const uint8_t (*filter_level)[8][2],
                                const int bx, const int by,
                                const int iw, const int ih,
                                const int skip, const enum BlockSize bs,
                                const uint16_t *const tx_masks,
                                const enum RectTxfmSize uvtx,
                                const enum Dav1dPixelLayout layout,
                                uint8_t *const ay, uint8_t *const ly,
                                uint8_t *const auv, uint8_t *const luv)
{
    if (!hdr->loopfilter.level_y[0] && !hdr->loopfilter.level_y[1])
        return;

    const uint8_t *const b_dim = av1_block_dimensions[bs];
    const int bw4 = imin(iw - bx, b_dim[0]);
    const int bh4 = imin(ih - by, b_dim[1]);
    const int bx4 = bx & 31;
    const int by4 = by & 31;

    level_cache += by * b4_stride + bx;
    for (int y = 0; y < bh4; y++) {
        for (int x = 0; x < bw4; x++) {
            level_cache[x][0] = filter_level[0][0][0];
            level_cache[x][1] = filter_level[1][0][0];
            level_cache[x][2] = filter_level[2][0][0];
            level_cache[x][3] = filter_level[3][0][0];
        }
        level_cache += b4_stride;
    }

    mask_edges_inter(lflvl->filter_y, by4, bx4, bw4, bh4, skip,
                     av1_max_txfm_size_for_bs[bs][0], tx_masks, ay, ly);

    if (!auv) return;

    const int ss_ver = layout == DAV1D_PIXEL_LAYOUT_I420;
    const int ss_hor = layout != DAV1D_PIXEL_LAYOUT_I444;
    const int cbw4 = (bw4 + ss_hor) >> ss_hor;
    const int cbh4 = (bh4 + ss_ver) >> ss_ver;
    const int cbx4 = bx4 >> ss_hor;
    const int cby4 = by4 >> ss_ver;

    mask_edges_chroma(lflvl->filter_uv, cby4, cbx4, cbw4, cbh4, skip, uvtx, auv, luv);
}

void dav1d_calc_eih(Av1FilterLUT *const lim_lut, const int filter_sharpness) {
    int level;

    // set E/I/H values from loopfilter level
    for (level = 0; level < 64; level++) {
        const int sharp = filter_sharpness;
        int limit = level;

        if (sharp > 0) {
            limit >>= (sharp + 3) >> 2;
            limit = imin(limit, 9 - sharp);
        }
        limit = imax(limit, 1);

        lim_lut->i[level] = limit;
        lim_lut->e[level] = 2 * (level + 2) + limit;
    }
}

static void dav1d_calc_lf_value(uint8_t (*const lflvl_values)[2],
                                const int is_chroma, const int base_lvl,
                                const int lf_delta, const int seg_delta,
                                const Av1LoopfilterModeRefDeltas *const mr_delta)
{
    const int base = iclip(iclip(base_lvl + lf_delta, 0, 63) + seg_delta, 0, 63);

    if (!base_lvl && is_chroma) {
        memset(lflvl_values, 0, 8 * 2);
    } else if (!mr_delta) {
        memset(lflvl_values, base, 8 * 2);
    } else {
        const int sh = base >= 32;
        lflvl_values[0][0] = lflvl_values[0][1] =
            iclip(base + (mr_delta->ref_delta[0] * (1 << sh)), 0, 63);
        for (int r = 1; r < 8; r++) {
            for (int m = 0; m < 2; m++) {
                const int delta =
                    mr_delta->mode_delta[m] + mr_delta->ref_delta[r];
                lflvl_values[r][m] = iclip(base + (delta * (1 << sh)), 0, 63);
            }
        }
    }
}

void dav1d_calc_lf_values(uint8_t (*const lflvl_values)[4][8][2],
                          const Av1FrameHeader *const hdr,
                          const int8_t lf_delta[4])
{
    const int n_seg = hdr->segmentation.enabled ? 8 : 1;

    if (!hdr->loopfilter.level_y[0] && !hdr->loopfilter.level_y[1]) {
        memset(lflvl_values, 0, 8 * 4 * 2 * n_seg);
        return;
    }

    const Av1LoopfilterModeRefDeltas *const mr_deltas =
        hdr->loopfilter.mode_ref_delta_enabled ?
        &hdr->loopfilter.mode_ref_deltas : NULL;
    for (int s = 0; s < n_seg; s++) {
        const Av1SegmentationData *const segd =
            hdr->segmentation.enabled ? &hdr->segmentation.seg_data.d[s] : NULL;

        dav1d_calc_lf_value(lflvl_values[s][0], 0, hdr->loopfilter.level_y[0],
                            lf_delta[0], segd ? segd->delta_lf_y_v : 0, mr_deltas);
        dav1d_calc_lf_value(lflvl_values[s][1], 0, hdr->loopfilter.level_y[1],
                            lf_delta[hdr->delta_lf_multi ? 1 : 0],
                            segd ? segd->delta_lf_y_h : 0, mr_deltas);
        dav1d_calc_lf_value(lflvl_values[s][2], 1, hdr->loopfilter.level_u,
                            lf_delta[hdr->delta_lf_multi ? 2 : 0],
                            segd ? segd->delta_lf_u : 0, mr_deltas);
        dav1d_calc_lf_value(lflvl_values[s][3], 1, hdr->loopfilter.level_v,
                            lf_delta[hdr->delta_lf_multi ? 3 : 0],
                            segd ? segd->delta_lf_v : 0, mr_deltas);
    }
}
