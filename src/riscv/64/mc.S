/******************************************************************************
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2024, Niklas Haas
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
 *****************************************************************************/

#include "src/riscv/asm.S"

function avg_8bpc_rvv, export=1, ext=v
    csrw vxrm, zero
    li t0, 128
    bgeu a4, t0, 128f
    li t0, 16
    bgeu a4, t0, 16f
    li t0, 8
    bgeu a4, t0, 8f
4:
    vsetvli zero, a5, e16, m2, ta, ma
    vlseg4e16.v v0, (a2)
    vlseg4e16.v v8, (a3)
    vadd.vv v0, v0, v8
    vadd.vv v2, v2, v10
    vadd.vv v4, v4, v12
    vadd.vv v6, v6, v14
.irpc i, 0246
    vmax.vx v\i, v\i, zero
.endr
    vsetvli zero, zero, e8, m1, ta, ma
    vnclipu.wi v8, v0, 4+1
    vnclipu.wi v9, v2, 4+1
    vnclipu.wi v10, v4, 4+1
    vnclipu.wi v11, v6, 4+1
    vssseg4e8.v v8, (a0), a1
    ret
8:
    vsetvli t0, a5, e16, m1, ta, ma
    vlseg8e16.v v0, (a2)
    vlseg8e16.v v8, (a3)
    sub a5, a5, t0
    slliw t1, t0, 4
    add a2, a2, t1
    add a3, a3, t1
    vadd.vv v0, v0, v8
    vadd.vv v1, v1, v9
    vadd.vv v2, v2, v10
    vadd.vv v3, v3, v11
    vadd.vv v4, v4, v12
    vadd.vv v5, v5, v13
    vadd.vv v6, v6, v14
    vadd.vv v7, v7, v15
.irpc i, 01234567
    vmax.vx v\i, v\i, zero
.endr
    vsetvli zero, zero, e8, mf2, ta, ma
    vnclipu.wi v8,  v0, 4+1
    vnclipu.wi v9,  v1, 4+1
    vnclipu.wi v10, v2, 4+1
    vnclipu.wi v11, v3, 4+1
    vnclipu.wi v12, v4, 4+1
    vnclipu.wi v13, v5, 4+1
    vnclipu.wi v14, v6, 4+1
    vnclipu.wi v15, v7, 4+1
    vssseg8e8.v v8, (a0), a1
    mul t0, t0, a1
    add a0, a0, t0
    bnez a5, 8b
    ret
16:
32:
64:
    slliw t0, a4, 1
640:
    vsetvli zero, a4, e16, m8, ta, ma
    vle16.v v0, (a2)
    vle16.v v8, (a3)
    add a2, a2, t0
    add a3, a3, t0
    vadd.vv v0, v0, v8
    vmax.vx v0, v0, zero
    vsetvli zero, zero, e8, m4, ta, ma
    vnclipu.wi v8, v0, 4+1
    vse8.v v8, (a0)
    add a0, a0, a1
    addi a5, a5, -1
    bnez a5, 640b
    ret

128:
    sub a1, a1, a4
    mv t1, a4
1280:
    vsetvli t0, a4, e16, m8, ta, ma
    vle16.v v0, (a2)
    vle16.v v8, (a3)
    subw a4, a4, t0
    slliw t3, t0, 1
    add a2, a2, t3
    add a3, a3, t3
    vadd.vv v16, v0, v8
    vmax.vx v16, v16, zero
    vsetvli zero, zero, e8, m4, ta, ma
    vnclipu.wi v0, v16, 4+1
    vse8.v v0, (a0)
    add a0, a0, t0
    bnez a4, 1280b
    add a0, a0, a1
    mv a4, t1
    addiw a5, a5, -1
    bnez a5, 1280b
    ret
endfunc

function w_avg_8bpc_rvv, export=1, ext=v
    csrw vxrm, zero
    li t4, 16
    subw t4, t4, a6
    sub a1, a1, a4
1:
    mv t1, a4
2:
    vsetvli t0, t1, e16, m4, ta, ma
    vle16.v v0, (a2)
    vle16.v v4, (a3)
    subw t1, t1, t0
    slliw t3, t0, 1
    add a2, a2, t3
    add a3, a3, t3
    vwmul.vx v8, v0, a6
    vwmacc.vx v8, t4, v4
    vnclip.wi v0, v8, 4+4
    vmax.vx v0, v0, zero
    vsetvli zero, zero, e8, m2, ta, ma
    vnclipu.wi v4, v0, 0
    vse8.v v4, (a0)
    add a0, a0, t0
    bnez t1, 2b

    add a0, a0, a1
    addiw a5, a5, -1
    bnez a5, 1b
    ret
endfunc

function mask_8bpc_rvv, export=1, ext=v
    csrw vxrm, zero
    vsetvli t0, zero, e16, m4, ta, ma
    li t0, 64
    vmv.v.x v12, t0
    sub a1, a1, a4
1:
    mv t1, a4
2:
    vsetvli t0, t1, e8, m2, ta, ma
    vle8.v v0, (a6)
    subw t1, t1, t0
    add a6, a6, t0
    vwcvtu.x.x.v v8, v0
    vsetvli zero, zero, e16, m4, ta, ma
    vle16.v v0, (a2)
    vle16.v v4, (a3)
    slliw t3, t0, 1
    add a2, a2, t3
    add a3, a3, t3
    vwmul.vv v16, v0, v8
    vsub.vv v8, v12, v8
    vwmacc.vv v16, v4, v8
    vnclip.wi v0, v16, 4+6
    vmax.vx v0, v0, zero
    vsetvli zero, zero, e8, m2, ta, ma
    vnclipu.wi v4, v0, 0
    vse8.v v4, (a0)
    add a0, a0, t0
    bnez t1, 2b

    add a0, a0, a1
    addiw a5, a5, -1
    bnez a5, 1b
    ret
endfunc

function blend_8bpc_rvv, export=1, ext=v
    csrw vxrm, zero
    vsetvli t0, zero, e8, m4, ta, ma
    li t0, 64
    vmv.v.x v12, t0
1:
    mv t1, a3
    mv t2, a0
2:
    vsetvli t0, t1, e8, m4, ta, ma
    vle8.v v0, (t2)
    vle8.v v4, (a2)
    vle8.v v8, (a5)
    subw t1, t1, t0
    add a2, a2, t0
    add a5, a5, t0
    vwmulu.vv v16, v4, v8
    vsub.vv v8, v12, v8
    vwmaccu.vv v16, v0, v8
    vnclipu.wi v0, v16, 6
    vse8.v v0, (t2)
    add t2, t2, t0
    bnez t1, 2b

    add a0, a0, a1
    addiw a4, a4, -1
    bnez a4, 1b
    ret
endfunc

function blend_v_8bpc_rvv, export=1, ext=v
    csrw vxrm, zero
    li t6, 64
    la a5, dav1d_obmc_masks
    add a5, a5, a3
    addw a6, a3, a3
    addw a6, a6, a3
    srliw a6, a6, 2
1:
    vsetvli t0, a6, e8, m4, ta, ma
    vle8.v v8, (a5)
    add a5, a5, t0
    subw a6, a6, t0
    mv t1, a0
    mv t2, a2
    mv t3, a4
    vmv.v.x v12, t6
    vsub.vv v12, v12, v8
2:
    vle8.v v0, (t1)
    vle8.v v4, (t2)
    add t2, t2, a3
    addiw t3, t3, -1
    vwmulu.vv v16, v4, v8
    vwmaccu.vv v16, v0, v12
    vnclipu.wi v0, v16, 6
    vse8.v v0, (t1)
    add t1, t1, a1
    bnez t3, 2b

    add a0, a0, t0
    add a2, a2, t0
    bnez a6, 1b
    ret
endfunc

function blend_h_8bpc_rvv, export=1, ext=v
    csrw vxrm, zero
    la a5, dav1d_obmc_masks
    add a5, a5, a4
    addw t0, a4, a4
    addw a4, a4, t0
    srliw a4, a4, 2
    li t5, 64
1:
    mv t1, a3
    mv t2, a0
    lbu t3, (a5)
    addi a5, a5, 1
    subw t4, t5, t3
2:
    vsetvli t0, t1, e8, m4, ta, ma
    vle8.v v0, (t2)
    vle8.v v4, (a2)
    subw t1, t1, t0
    add a2, a2, t0
    vwmulu.vx v8, v0, t4
    vwmaccu.vx v8, t3, v4
    vnclipu.wi v0, v8, 6
    vse8.v v0, (t2)
    add t2, t2, t0
    bnez t1, 2b

    add a0, a0, a1
    addiw a4, a4, -1
    bnez a4, 1b
    ret
endfunc
