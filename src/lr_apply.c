/*
 * ..
 */

#include <stdio.h>

#include "common/intops.h"

#include "src/lr_apply.h"


enum LrRestorePlanes {
    LR_RESTORE_Y = 1 << 0,
    LR_RESTORE_U = 1 << 1,
    LR_RESTORE_V = 1 << 2,
};

static void backup_lpf(pixel *dst, const ptrdiff_t dst_stride,
                       const pixel *src, const ptrdiff_t src_stride,
                       const int first_stripe_h, const int next_stripe_h,
                       int row, const int row_h, const int w, const int h)
{
    if (row) {
        // Copy the top part of the stored loop filtered pixels from the
        // previous sb row needed above the first stripe of this sb row.
        pixel_copy(&dst[PXSTRIDE(dst_stride) *  0],
                   &dst[PXSTRIDE(dst_stride) *  8], w);
        pixel_copy(&dst[PXSTRIDE(dst_stride) *  1],
                   &dst[PXSTRIDE(dst_stride) *  9], w);
        pixel_copy(&dst[PXSTRIDE(dst_stride) *  2],
                   &dst[PXSTRIDE(dst_stride) * 10], w);
        pixel_copy(&dst[PXSTRIDE(dst_stride) *  3],
                   &dst[PXSTRIDE(dst_stride) * 11], w);
    }

    int stripe_h = first_stripe_h;
    dst += 4 * PXSTRIDE(dst_stride);
    src += (stripe_h - 2) * PXSTRIDE(src_stride);
    for (; row + stripe_h <= row_h; row += stripe_h) {
        for (int i = 0; i < 4; i++) {
            pixel_copy(dst, src, w);
            dst += PXSTRIDE(dst_stride);
            src += PXSTRIDE(src_stride);
        }
        stripe_h = next_stripe_h;
        src += (stripe_h - 4) * PXSTRIDE(src_stride);
    }
}

void bytefn(dav1d_lr_copy_lpf)(Dav1dFrameContext *const f,
                               /*const*/ pixel *const src[3], const int sby)
{
    const int stripe_h = 64 - (8 * !sby);
    const ptrdiff_t offset = 8 * !!sby;
    const ptrdiff_t *const src_stride = f->cur.p.stride;

    // TODO Also check block level restore type to reduce copying.
    const int restore_planes =
        ((f->frame_hdr.restoration.type[0] != RESTORATION_NONE) << 0) +
        ((f->frame_hdr.restoration.type[1] != RESTORATION_NONE) << 1) +
        ((f->frame_hdr.restoration.type[2] != RESTORATION_NONE) << 2);

    if (restore_planes & LR_RESTORE_Y) {
        const int h = f->bh << 2;
        const int w = f->bw << 2;
        const int row_h = imin((sby + 1) << (6 + f->seq_hdr.sb128), h);
        const int y_stripe = (sby << (6 + f->seq_hdr.sb128)) - offset;
        backup_lpf(f->lf.lr_lpf_line_ptr[0], sizeof(pixel) * f->b4_stride * 4,
                   src[0] - offset * PXSTRIDE(src_stride[0]),
                   PXSTRIDE(src_stride[0]), stripe_h, 64, y_stripe, row_h, w, h);
    }
    if (restore_planes & (LR_RESTORE_U | LR_RESTORE_V)) {
        const int ss_ver = f->cur.p.p.layout == DAV1D_PIXEL_LAYOUT_I420;
        const int ss_hor = f->cur.p.p.layout != DAV1D_PIXEL_LAYOUT_I444;
        const int h = f->bh << (2 - ss_ver);
        const int w = f->bw << (2 - ss_hor);
        const int row_h = imin((sby + 1) << ((6 - ss_ver) + f->seq_hdr.sb128), h);
        const int stripe_h_uv = stripe_h >> ss_ver;
        const ptrdiff_t offset_uv = offset >> ss_ver;
        const int y_stripe =
            (sby << ((6 - ss_ver) + f->seq_hdr.sb128)) - offset_uv;

        if (restore_planes & LR_RESTORE_U) {
            backup_lpf(f->lf.lr_lpf_line_ptr[1], sizeof(pixel) * f->b4_stride * 4,
                       src[1] - offset_uv * PXSTRIDE(src_stride[1]),
                       PXSTRIDE(src_stride[1]), stripe_h_uv, 32, y_stripe,
                       row_h, w, h);
        }
        if (restore_planes & LR_RESTORE_V) {
            backup_lpf(f->lf.lr_lpf_line_ptr[2], sizeof(pixel) * f->b4_stride * 4,
                       src[2] - offset_uv * PXSTRIDE(src_stride[1]),
                       PXSTRIDE(src_stride[1]), stripe_h_uv, 32, y_stripe,
                       row_h, w, h);
        }
    }
}


static void lr_stripe(const Dav1dFrameContext *const f, pixel *p, int x, int y,
                      const int plane, const int unit_w,
                      const int first_stripe_h, const int next_stripe_h,
                      const int row_h, const Av1RestorationUnit *const lr,
                      enum LrEdgeFlags edges)
{
    const Dav1dDSPContext *const dsp = f->dsp;
    const int sbrow_has_bottom = (edges & LR_HAVE_BOTTOM);
    const pixel *lpf = f->lf.lr_lpf_line_ptr[plane] + x;
    const ptrdiff_t p_stride = f->cur.p.stride[!!plane];
    const ptrdiff_t lpf_stride = sizeof(pixel) * f->b4_stride * 4;

    int stripe_h = first_stripe_h;

    // FIXME [8] might be easier for SIMD
    int16_t filterh[7], filterv[7];
    if (lr->type == RESTORATION_WIENER) {
        filterh[0] = filterh[6] = lr->filter_h[0];
        filterh[1] = filterh[5] = lr->filter_h[1];
        filterh[2] = filterh[4] = lr->filter_h[2];
        filterh[3] = -((filterh[0] + filterh[1] + filterh[2]) * 2);

        filterv[0] = filterv[6] = lr->filter_v[0];
        filterv[1] = filterv[5] = lr->filter_v[1];
        filterv[2] = filterv[4] = lr->filter_v[2];
        filterv[3] = -((filterv[0] + filterv[1] + filterv[2]) * 2);
    }

    while (y + stripe_h <= row_h) {
        // TODO Look into getting rid of the this if
        if (y + stripe_h == row_h) {
            edges &= ~LR_HAVE_BOTTOM;
        } else {
            edges |= LR_HAVE_BOTTOM;
        }
        if (lr->type == RESTORATION_WIENER) {
            dsp->lr.wiener(p, p_stride, lpf, lpf_stride, unit_w, stripe_h,
                           filterh, filterv, edges);
        } else {
            assert(lr->type == RESTORATION_SGRPROJ);
            dsp->lr.selfguided(p, p_stride, lpf, lpf_stride, unit_w, stripe_h,
                               lr->sgr_idx, lr->sgr_weights, edges);
        }

        y += stripe_h;
        edges |= LR_HAVE_TOP;
        if (y + stripe_h > row_h && sbrow_has_bottom) break;
        p += stripe_h * p_stride;
        stripe_h = imin(next_stripe_h, row_h - y);
        if (stripe_h == 0) break;
        lpf += 4 * lpf_stride;
    }
}

static void backup3xU(pixel *dst, const pixel *src, const ptrdiff_t src_stride,
                      int u)
{
    for (; u > 0; u--, dst += 3, src += PXSTRIDE(src_stride))
        pixel_copy(dst, src, 3);
}

static void restore3xU(pixel *dst, const ptrdiff_t dst_stride, const pixel *src,
                       int u)
{
    for (; u > 0; u--, dst += PXSTRIDE(dst_stride), src += 3)
        pixel_copy(dst, src, 3);
}

static void lr_sbrow(const Dav1dFrameContext *const f, pixel *p, const int y,
                     const int w, const int h, const int row_h, const int plane)
{
    const int ss_ver = !!plane * f->cur.p.p.layout == DAV1D_PIXEL_LAYOUT_I420;
    const ptrdiff_t p_stride = f->cur.p.stride[!!plane];

    const int unit_size_log2 = f->frame_hdr.restoration.unit_size[!!plane];
    const int unit_size = 1 << unit_size_log2;
    const int half_unit_size = unit_size >> 1;
    const int max_unit_size = unit_size + half_unit_size;

    const int row_y = y + ((8 >> ss_ver) * !!y);

    // FIXME This is an ugly hack to lookup the proper AV1Filter unit for
    // chroma planes. Question: For Multithreaded decoding, is it better
    // to store the chroma LR information with collocated Luma information?
    // In other words. For a chroma restoration unit locate at 128,128 and
    // with a 4:2:0 chroma subsampling, do we store the filter information at
    // the AV1Filter unit located at (128,128) or (256,256)
    // TODO Support chroma subsampling.
    const int shift = plane ? 6 : 7;

    int ruy = (row_y >> unit_size_log2);
    // Merge last restoration unit if its height is < half_unit_size
    if (ruy > 0) ruy -= (ruy << unit_size_log2) + half_unit_size > h;

    const int proc_h = 64 >> ss_ver;
    const int stripe_h = proc_h - ((8 >> ss_ver) * !y);
    const int filter_h = imin(stripe_h + proc_h * f->seq_hdr.sb128, h - y);

    pixel pre_lr_border[filter_h * 3];
    pixel post_lr_border[filter_h * 3];

    int unit_w = unit_size;

    enum LrEdgeFlags edges = (y > 0 ? LR_HAVE_TOP : 0) |
                             (row_h < h ? LR_HAVE_BOTTOM : 0);

    for (int x = 0, rux = 0; x < w; x+= unit_w, rux++, edges |= LR_HAVE_LEFT) {
        // TODO Clean up this if statement.
        if (x + max_unit_size > w) {
            unit_w = w - x;
            edges &= ~LR_HAVE_RIGHT;
        } else {
            edges |= LR_HAVE_RIGHT;
        }

        // Based on the position of the restoration unit, find the corresponding
        // AV1Filter unit.
        const int unit_idx = ((ruy & 16) >> 3) + ((rux & 16) >> 4);
        const Av1RestorationUnit *const lr =
            &f->lf.mask[(((ruy << unit_size_log2) >> shift) * f->sb128w) +
                        (x >> shift)].lr[plane][unit_idx];

        if (edges & LR_HAVE_LEFT) {
            restore3xU(p - 3, p_stride, pre_lr_border, filter_h);
        }
        // FIXME Don't backup if the next restoration unit is RESTORE_NONE
        // This also requires not restoring in the same conditions.
        if (edges & LR_HAVE_RIGHT) {
            backup3xU(pre_lr_border, p + unit_w - 3, p_stride, filter_h);
        }
        if (lr->type != RESTORATION_NONE) {
            lr_stripe(f, p, x, y, plane, unit_w, stripe_h, proc_h,
                      row_h, lr, edges);
        }
        if (edges & LR_HAVE_LEFT) {
            restore3xU(p - 3, p_stride, post_lr_border, filter_h);
        }
        if (edges & LR_HAVE_RIGHT) {
            backup3xU(post_lr_border, p + unit_w - 3, p_stride, filter_h);
        }
        p += unit_w;
    }
}

void bytefn(dav1d_lr_sbrow)(Dav1dFrameContext *const f, pixel *const dst[3],
                            const int sby)
{
    const ptrdiff_t offset_y = 8 * !!sby;
    const ptrdiff_t *const dst_stride = f->cur.p.stride;

    const int restore_planes =
        ((f->frame_hdr.restoration.type[0] != RESTORATION_NONE) << 0) +
        ((f->frame_hdr.restoration.type[1] != RESTORATION_NONE) << 1) +
        ((f->frame_hdr.restoration.type[2] != RESTORATION_NONE) << 2);

    if (restore_planes & LR_RESTORE_Y) {
        const int h = f->bh << 2;
        const int w = f->bw << 2;
        const int row_h = imin((sby + 1) << (6 + f->seq_hdr.sb128), h);
        const int y_stripe = (sby << (6 + f->seq_hdr.sb128)) - offset_y;
        lr_sbrow(f, dst[0] - offset_y * PXSTRIDE(dst_stride[0]), y_stripe, w,
                 h, row_h, 0);
    }
    if (restore_planes & (LR_RESTORE_U | LR_RESTORE_V)) {
        const int ss_ver = f->cur.p.p.layout == DAV1D_PIXEL_LAYOUT_I420;
        const int ss_hor = f->cur.p.p.layout != DAV1D_PIXEL_LAYOUT_I444;
        const int h = f->bh << (2 - ss_ver);
        const int w = f->bw << (2 - ss_hor);
        const int row_h = imin((sby + 1) << ((6 - ss_ver) + f->seq_hdr.sb128), h);
        const ptrdiff_t offset_uv = offset_y >> ss_ver;
        const int y_stripe =
            (sby << ((6 - ss_ver) + f->seq_hdr.sb128)) - offset_uv;
        if (restore_planes & LR_RESTORE_U)
            lr_sbrow(f, dst[1] - offset_uv * PXSTRIDE(dst_stride[1]), y_stripe,
                     w, h, row_h, 1);

        if (restore_planes & LR_RESTORE_V)
            lr_sbrow(f, dst[2] - offset_uv * PXSTRIDE(dst_stride[1]), y_stripe,
                     w, h, row_h, 2);
    }
}
