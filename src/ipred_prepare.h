/*
 * ..
 */

#ifndef __DAV1D_SRC_IPRED_PREPARE_H__
#define __DAV1D_SRC_IPRED_PREPARE_H__

#include <stddef.h>
#include <stdint.h>

#include "common/bitdepth.h"

#include "src/env.h"
#include "src/intra_edge.h"
#include "src/levels.h"

/*
 * Luma intra edge preparation.
 *
 * x/y/start/w/h are in luma block (4px) units:
 * - x and y are the absolute block positions in the image;
 * - start/w/h are the *dependent tile* boundary positions. In practice, start
 *   is the horizontal tile start, w is the horizontal tile end, the vertical
 *   tile start is assumed to be 0 and h is the vertical image end.
 *
 * edge_flags signals which edges are available for this transform-block inside
 * the given partition, as well as for the partition inside the superblock
 * structure.
 *
 * dst and stride are pointers to the top/left position of the current block,
 * and can be used to locate the top, left, top/left, top/right and bottom/left
 * edge pointers also.
 *
 * angle is the angle_delta [-3..3] on input, and the absolute angle on output.
 *
 * mode is the intra prediction mode as coded in the bitstream. The return value
 * is this same mode, converted to an index in the DSP functions.
 *
 * tw/th are the size of the transform block in block (4px) units.
 *
 * topleft_out is a pointer to scratch memory that will be filled with the edge
 * pixels. The memory array should have space to be indexed in the [-2*w,2*w]
 * range, in the following order:
 * - [0] will be the top/left edge pixel;
 * - [1..w] will be the top edge pixels (1 being left-most, w being right-most);
 * - [w+1..w*w] will be the top/right edge pixels;
 * - [-1..-w] will be the left edge pixels (-1 being top-most, -w being bottom-
 *   most);
 * - [-w-1..-2*w] will be the bottom/left edge pixels.
 * Each edge may remain uninitialized if it is not used by the returned mode
 * index. If edges are not available (because the edge position is outside the
 * tile dimensions or because edge_flags indicates lack of edge availability),
 * they will be extended from nearby edges as defined by the av1 spec.
 */
enum IntraPredMode
    bytefn(prepare_intra_edges)(int x, int have_left, int y, int have_top,
                                int w, int h, enum EdgeFlags edge_flags,
                                const pixel *dst, ptrdiff_t stride,
                                const pixel *prefilter_toplevel_sb_edge,
                                enum IntraPredMode mode, int *angle,
                                int tw, int th, pixel *topleft_out);

// is or'ed with the angle argument into intra predictors to signal that edges
// are smooth and should use reduced filter strength
#define ANGLE_SMOOTH_EDGE_FLAG 512
static inline int sm_flag(const BlockContext *const b, const int idx) {
    if (!b->intra[idx]) return 0;
    const enum IntraPredMode m = b->mode[idx];
    return (m == SMOOTH_PRED || m == SMOOTH_H_PRED ||
            m == SMOOTH_V_PRED) ? ANGLE_SMOOTH_EDGE_FLAG : 0;
}

static inline int sm_uv_flag(const BlockContext *const b, const int idx) {
    const enum IntraPredMode m = b->uvmode[idx];
    return (m == SMOOTH_PRED || m == SMOOTH_H_PRED ||
            m == SMOOTH_V_PRED) ? ANGLE_SMOOTH_EDGE_FLAG : 0;
}

#endif /* __DAV1D_SRC_IPRED_PREPARE_H__ */
