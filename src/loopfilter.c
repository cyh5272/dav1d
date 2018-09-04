/*
 * ..
 */

#include <stdlib.h>

#include "common/intops.h"

#include "src/loopfilter.h"

static __attribute__((noinline)) void
loop_filter(pixel *dst, int E, int I, int H,
            const ptrdiff_t stridea, const ptrdiff_t strideb, const int wd)
{
    int i, F = 1 << (BITDEPTH - 8);

    E <<= BITDEPTH - 8;
    I <<= BITDEPTH - 8;
    H <<= BITDEPTH - 8;

    for (i = 0; i < 4; i++, dst += stridea) {
        int p6, p5, p4, p3, p2;
        int p1 = dst[strideb * -2], p0 = dst[strideb * -1];
        int q0 = dst[strideb * +0], q1 = dst[strideb * +1];
        int q2, q3, q4, q5, q6;
        int fm, flat8out, flat8in;

        fm = abs(p1 - p0) <= I && abs(q1 - q0) <= I &&
             abs(p0 - q0) * 2 + (abs(p1 - q1) >> 1) <= E;

        if (wd > 4) {
            p2 = dst[strideb * -3];
            q2 = dst[strideb * +2];

            fm &= abs(p2 - p1) <= I && abs(q2 - q1) <= I;

            if (wd > 6) {
                p3 = dst[strideb * -4];
                q3 = dst[strideb * +3];

                fm &= abs(p3 - p2) <= I && abs(q3 - q2) <= I;
            }
        }

        if (!fm) continue;

        if (wd >= 16) {
            p6 = dst[strideb * -7];
            p5 = dst[strideb * -6];
            p4 = dst[strideb * -5];
            q4 = dst[strideb * +4];
            q5 = dst[strideb * +5];
            q6 = dst[strideb * +6];

            flat8out = abs(p6 - p0) <= F && abs(p5 - p0) <= F &&
                       abs(p4 - p0) <= F && abs(q4 - q0) <= F &&
                       abs(q5 - q0) <= F && abs(q6 - q0) <= F;
        }

        if (wd >= 6)
            flat8in = abs(p2 - p0) <= F && abs(p1 - p0) <= F &&
                      abs(q1 - q0) <= F && abs(q2 - q0) <= F;

        if (wd >= 8)
            flat8in &= abs(p3 - p0) <= F && abs(q3 - q0) <= F;

        if (wd >= 16 && (flat8out & flat8in)) {
            dst[strideb * -6] = (p6 + p6 + p6 + p6 + p6 + p6 * 2 + p5 * 2 +
                                 p4 * 2 + p3 + p2 + p1 + p0 + q0 + 8) >> 4;
            dst[strideb * -5] = (p6 + p6 + p6 + p6 + p6 + p5 * 2 + p4 * 2 +
                                 p3 * 2 + p2 + p1 + p0 + q0 + q1 + 8) >> 4;
            dst[strideb * -4] = (p6 + p6 + p6 + p6 + p5 + p4 * 2 + p3 * 2 +
                                 p2 * 2 + p1 + p0 + q0 + q1 + q2 + 8) >> 4;
            dst[strideb * -3] = (p6 + p6 + p6 + p5 + p4 + p3 * 2 + p2 * 2 +
                                 p1 * 2 + p0 + q0 + q1 + q2 + q3 + 8) >> 4;
            dst[strideb * -2] = (p6 + p6 + p5 + p4 + p3 + p2 * 2 + p1 * 2 +
                                 p0 * 2 + q0 + q1 + q2 + q3 + q4 + 8) >> 4;
            dst[strideb * -1] = (p6 + p5 + p4 + p3 + p2 + p1 * 2 + p0 * 2 +
                                 q0 * 2 + q1 + q2 + q3 + q4 + q5 + 8) >> 4;
            dst[strideb * +0] = (p5 + p4 + p3 + p2 + p1 + p0 * 2 + q0 * 2 +
                                 q1 * 2 + q2 + q3 + q4 + q5 + q6 + 8) >> 4;
            dst[strideb * +1] = (p4 + p3 + p2 + p1 + p0 + q0 * 2 + q1 * 2 +
                                 q2 * 2 + q3 + q4 + q5 + q6 + q6 + 8) >> 4;
            dst[strideb * +2] = (p3 + p2 + p1 + p0 + q0 + q1 * 2 + q2 * 2 +
                                 q3 * 2 + q4 + q5 + q6 + q6 + q6 + 8) >> 4;
            dst[strideb * +3] = (p2 + p1 + p0 + q0 + q1 + q2 * 2 + q3 * 2 +
                                 q4 * 2 + q5 + q6 + q6 + q6 + q6 + 8) >> 4;
            dst[strideb * +4] = (p1 + p0 + q0 + q1 + q2 + q3 * 2 + q4 * 2 +
                                 q5 * 2 + q6 + q6 + q6 + q6 + q6 + 8) >> 4;
            dst[strideb * +5] = (p0 + q0 + q1 + q2 + q3 + q4 * 2 + q5 * 2 +
                                 q6 * 2 + q6 + q6 + q6 + q6 + q6 + 8) >> 4;
        } else if (wd >= 8 && flat8in) {
            dst[strideb * -3] = (p3 + p3 + p3 + 2 * p2 + p1 + p0 + q0 + 4) >> 3;
            dst[strideb * -2] = (p3 + p3 + p2 + 2 * p1 + p0 + q0 + q1 + 4) >> 3;
            dst[strideb * -1] = (p3 + p2 + p1 + 2 * p0 + q0 + q1 + q2 + 4) >> 3;
            dst[strideb * +0] = (p2 + p1 + p0 + 2 * q0 + q1 + q2 + q3 + 4) >> 3;
            dst[strideb * +1] = (p1 + p0 + q0 + 2 * q1 + q2 + q3 + q3 + 4) >> 3;
            dst[strideb * +2] = (p0 + q0 + q1 + 2 * q2 + q3 + q3 + q3 + 4) >> 3;
        } else if (wd == 6 && flat8in) {
            dst[strideb * -2] = (p2 + 2 * p2 + 2 * p1 + 2 * p0 + q0 + 4) >> 3;
            dst[strideb * -1] = (p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1 + 4) >> 3;
            dst[strideb * +0] = (p1 + 2 * p0 + 2 * q0 + 2 * q1 + q2 + 4) >> 3;
            dst[strideb * +1] = (p0 + 2 * q0 + 2 * q1 + 2 * q2 + q2 + 4) >> 3;
        } else {
            int hev = abs(p1 - p0) > H || abs(q1 - q0) > H;

#define iclip_diff(v) iclip(v, -128 * (1 << (BITDEPTH - 8)), \
                                127 * (1 << (BITDEPTH - 8)))

            if (hev) {
                int f = iclip_diff(p1 - q1), f1, f2;
                f = iclip_diff(3 * (q0 - p0) + f);

                f1 = imin(f + 4, 127) >> 3;
                f2 = imin(f + 3, 127) >> 3;

                dst[strideb * -1] = iclip_pixel(p0 + f2);
                dst[strideb * +0] = iclip_pixel(q0 - f1);
            } else {
                int f = iclip_diff(3 * (q0 - p0)), f1, f2;

                f1 = imin(f + 4, 127) >> 3;
                f2 = imin(f + 3, 127) >> 3;

                dst[strideb * -1] = iclip_pixel(p0 + f2);
                dst[strideb * +0] = iclip_pixel(q0 - f1);

                f = (f1 + 1) >> 1;
                dst[strideb * -2] = iclip_pixel(p1 + f);
                dst[strideb * +1] = iclip_pixel(q1 - f);
            }
#undef iclip_diff
        }
    }
}

#define lf_4_fn(dir, wd, stridea, strideb) \
static void loop_filter_##dir##_##wd##wd_4px_c(pixel *const dst, \
                                               const ptrdiff_t stride, \
                                               const int E, const int I, \
                                               const int H) \
{ \
    loop_filter(dst, E, I, H, stridea, strideb, wd); \
}

#define lf_4_fns(wd) \
lf_4_fn(h, wd, PXSTRIDE(stride), 1) \
lf_4_fn(v, wd, 1, PXSTRIDE(stride))

lf_4_fns(4)
lf_4_fns(6)
lf_4_fns(8)
lf_4_fns(16)

#undef lf_4_fn
#undef lf_4_fns

void bitfn(dav1d_loop_filter_dsp_init)(Dav1dLoopFilterDSPContext *const c) {
    c->loop_filter[0][0] = loop_filter_h_4wd_4px_c;
    c->loop_filter[0][1] = loop_filter_v_4wd_4px_c;
    c->loop_filter[1][0] = loop_filter_h_8wd_4px_c;
    c->loop_filter[1][1] = loop_filter_v_8wd_4px_c;
    c->loop_filter[2][0] = loop_filter_h_16wd_4px_c;
    c->loop_filter[2][1] = loop_filter_v_16wd_4px_c;

    c->loop_filter_uv[0][0] = loop_filter_h_4wd_4px_c;
    c->loop_filter_uv[0][1] = loop_filter_v_4wd_4px_c;
    c->loop_filter_uv[1][0] = loop_filter_h_6wd_4px_c;
    c->loop_filter_uv[1][1] = loop_filter_v_6wd_4px_c;
}
