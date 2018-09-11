/*
 * ..
 */

#ifndef __DAV1D_SRC_LR_APPLY_H__
#define __DAV1D_SRC_LR_APPLY_H__

#include <stdint.h>
#include <stddef.h>

#include "common/bitdepth.h"

#include "src/internal.h"

void bytefn(dav1d_lr_copy_lpf)(Dav1dFrameContext *const f,
                               /*const*/ pixel *const src[3], int sby);

void bytefn(dav1d_lr_sbrow)(Dav1dFrameContext *const f, pixel *const dst[3],
                            int sby);

#endif /* __DAV1D_SRC_LR_APPLY_H__ */
