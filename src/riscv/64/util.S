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

.macro push_regs
    addi sp, sp, -128
    sd ra, 0(sp)
    sd a0, 8(sp)
    sd a1, 16(sp)
    sd a2, 24(sp)
    sd a3, 32(sp)
    sd a4, 40(sp)
    sd a5, 48(sp)
    sd a6, 56(sp)
    sd a7, 64(sp)
    sd t0, 72(sp)
    sd t1, 80(sp)
    sd t2, 88(sp)
    sd t3, 96(sp)
    sd t4, 104(sp)
    sd t5, 112(sp)
    sd t6, 120(sp)
    csrr t0, vlenb
    slli t0, t0, 3
    sub sp, sp, t0
    vs8r.v v0, (sp)
    sub sp, sp, t0
    vs8r.v v8, (sp)
    sub sp, sp, t0
    vs8r.v v16, (sp)
    sub sp, sp, t0
    vs8r.v v24, (sp)
.endm

.macro pop_regs
    csrr t0, vlenb
    slli t0, t0, 3
    vl8r.v v24, (sp)
    add sp, sp, t0
    vl8r.v v16, (sp)
    add sp, sp, t0
    vl8r.v v8, (sp)
    add sp, sp, t0
    vl8r.v v0, (sp)
    add sp, sp, t0
    ld ra, 0(sp)
    ld a0, 8(sp)
    ld a1, 16(sp)
    ld a2, 24(sp)
    ld a3, 32(sp)
    ld a4, 40(sp)
    ld a5, 48(sp)
    ld a6, 56(sp)
    ld a7, 64(sp)
    ld t0, 72(sp)
    ld t1, 80(sp)
    ld t2, 88(sp)
    ld t3, 96(sp)
    ld t4, 104(sp)
    ld t5, 112(sp)
    ld t6, 120(sp)
    addi sp, sp, 128
.endm

.macro print_vreg vr, num
    push_regs
    csrr t0, vlenb
    slli t0, t0, 3
    sub a2, sp, t0
    vs\num\()r.v \vr, (a2)
    addi sp, a2, -16
    csrr a0, vl
    sd a0, 0(sp)
    csrr a1, vtype
    sd a1, 8(sp)
    call EXTERN\()dump_vrr
    ld a0, 0(sp)
    ld a1, 8(sp)
    vsetvl zero, a0, a1
    add sp, sp, 16
    csrr t0, vlenb
    slli t0, t0, 3
    add sp, sp, t0
    pop_regs
.endm
