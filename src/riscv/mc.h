/*
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2024, Nathan Egge
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "src/cpu.h"
#include "src/mc.h"

decl_blend_fn(BF(dav1d_blend, rvv));
decl_blend_dir_fn(BF(dav1d_blend_h, rvv));
decl_blend_dir_fn(BF(dav1d_blend_v, rvv));

decl_blend_fn(BF(dav1d_blend_vl256, rvv));
decl_blend_dir_fn(BF(dav1d_blend_h_vl256, rvv));
decl_blend_dir_fn(BF(dav1d_blend_v_vl256, rvv));

int dav1d_get_vlenb(void);

static ALWAYS_INLINE void mc_dsp_init_riscv(Dav1dMCDSPContext *const c) {
  const unsigned flags = dav1d_get_cpu_flags();

  if (!(flags & DAV1D_RISCV_CPU_FLAG_V)) return;

#if BITDEPTH == 8
  c->blend = BF(dav1d_blend, rvv);
  c->blend_h = BF(dav1d_blend_h, rvv);
  c->blend_v = BF(dav1d_blend_v, rvv);

  if (dav1d_get_vlenb()*8 >= 256) {
    c->blend = BF(dav1d_blend_vl256, rvv);
    c->blend_h = BF(dav1d_blend_h_vl256, rvv);
    c->blend_v = BF(dav1d_blend_v_vl256, rvv);
  }
#endif
}