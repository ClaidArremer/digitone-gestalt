| SPDX-License-Identifier: GPL-2.0-or-later
| GESTALT's wavefolder loop for a block, in assembly: fold.c's fd_frames (the C is the reference,
| and the host test runs it), here with the state in registers.
|
| void fd_frames_asm(const int32_t *in, int32_t *out, struct fd_chs *s, const struct fd_k *k)
| in and out: 32 frames, interleaved L, R (in place is fine). s: two channels' state.
|
|   d2 lp L  d3 dx L  d4 dy L   d5 lp R  d6 dx R  d7 dy R   d0 d1 scratch
|   a0 in   a1 out   a2 the drive (Q11)   a4 k   a5 the end of out   a6 the sine
|   locals: 56(sp) the blend's ramp (dwm (i + 1)), 60(sp) this frame's blend
|
| struct fd_k: g0 0, dg 4, b0 8, db 12, k 16, dc 20, wm0 24, dwm 28, full 32, sin 36
| struct fd_chs: lp 0, dx 4, dy 8 (left), then the right at 12.

        .equ    K_G0, 0
        .equ    K_DG, 4
        .equ    K_B0, 8
        .equ    K_DB, 12
        .equ    K_K, 16
        .equ    K_DC, 20
        .equ    K_WM0, 24
        .equ    K_DWM, 28
        .equ    K_FULL, 32
        .equ    K_SIN, 36
        .equ    KNEE, 0x600000
        .equ    LMAX, 0x740000

| the folder on the sample at OFF(a0): the phase ((x24 >> 9) g) >> 11, the curve there << 4, the
| low-pass (if TONE), then the DC block on DX / DY (if DC; if not, DY is t)
        .macro  FOLD off, lp, dx, dy, tone, dc
        move.l  \off(%a0), %d0
        asr.l   #8, %d0
        asr.l   #8, %d0
        asr.l   #1, %d0                 | x24 >> 9
        move.l  %a2, %d1
        muls.l  %d1, %d0
        asr.l   #8, %d0
        asr.l   #3, %d0                 | the phase
        asr.l   #6, %d0
        andi.l  #1023, %d0
        mvs.w   (0, %a6, %d0.l*2), %d0  | the curve, Q15
        lsl.l   #4, %d0                 | t
        .if \tone
        move.l  %d0, %d1                | the low-pass: lp += (((t - lp) >> 5) k) >> 10; t = lp
        sub.l   \lp, %d1
        asr.l   #5, %d1
        muls.l  K_K(%a4), %d1
        asr.l   #8, %d1
        asr.l   #2, %d1
        add.l   %d1, \lp
        move.l  \lp, %d0
        .endif
        .if \dc
        move.l  \dy, %d1                | DC block: dy += t - dx - leak, leak = dy >> 11,
        asr.l   #8, %d1                 |   at least 1 while dy > 0 (it settles at 0)
        asr.l   #3, %d1
        bne.s   9f
        tst.l   \dy
        ble.s   9f
        moveq   #1, %d1
9:      sub.l   %d1, \dy
        add.l   %d0, \dy
        sub.l   \dx, \dy
        move.l  %d0, \dx
        .else
        move.l  %d0, \dy
        .endif
        .endm

| DY to 24 bits, the fade against the dry at OFF(a0) (if BLEND: FOLD coming on or going OFF), the
| ceiling, out
        .macro  OUT off, dy, blend
        move.l  \dy, %d0                | 24 bits: dy << 4
        lsl.l   #4, %d0
        .if \blend
        move.l  \off(%a0), %d1          | the blend: x24 + (((w - x24) >> 7) wm) >> 3
        asr.l   #8, %d1
        sub.l   %d1, %d0
        asr.l   #7, %d0
        muls.l  60(%sp), %d0
        asr.l   #3, %d0
        add.l   %d1, %d0
        .endif
        cmpi.l  #KNEE, %d0              | the ceiling: the common case falls straight through
        bgt.s   7f
        cmpi.l  #-KNEE, %d0
        bge.s   8f
        move.l  #-KNEE, %d1
        sub.l   %d0, %d1
        asr.l   #1, %d1
        move.l  #-KNEE, %d0
        sub.l   %d1, %d0
        cmpi.l  #-LMAX, %d0
        bge.s   8f
        move.l  #-LMAX, %d0
        bra.s   8f
7:      subi.l  #KNEE, %d0
        asr.l   #1, %d0
        addi.l  #KNEE, %d0
        cmpi.l  #LMAX, %d0
        ble.s   8f
        move.l  #LMAX, %d0
8:      lsl.l   #8, %d0
        move.l  %d0, (%a1)+
        .endm

| the frames, made eight times: TONE (the low-pass runs), BLEND (not fully wet) and DC (the DC block
| runs) or not, so each leaves out what it does not need, tests included
        .macro  FRAMES tone, blend, dc
.Lframe\@:
        adda.l  K_DG(%a4), %a2          | the drive, a frame's step
        .if \blend
        move.l  56(%sp), %d0            | the fade for this frame: wm0 + (dwm (i + 1)) >> 11
        add.l   K_DWM(%a4), %d0
        move.l  %d0, 56(%sp)
        asr.l   #8, %d0
        asr.l   #3, %d0
        add.l   K_WM0(%a4), %d0
        move.l  %d0, 60(%sp)
        .endif
        FOLD    0, %d2, %d3, %d4, \tone, \dc
        FOLD    4, %d5, %d6, %d7, \tone, \dc
        OUT     0, %d4, \blend
        OUT     4, %d7, \blend
        addq.l  #8, %a0
        cmpa.l  %a5, %a1
        bne.w   .Lframe\@
        bra.w   .Ldone
        .endm

| which of the eight: DC, TONE, BLEND
        .macro  PICK dc
        move.l  K_K(%a4), %d0
        cmpi.l  #32768, %d0
        beq.w   .Lopen\@
        tst.l   K_FULL(%a4)
        beq.w   .Ltb\@
        FRAMES  1, 0, \dc
.Ltb\@: FRAMES  1, 1, \dc
.Lopen\@:
        tst.l   K_FULL(%a4)
        beq.w   .Lb\@
        FRAMES  0, 0, \dc
.Lb\@:  FRAMES  0, 1, \dc
        .endm

        .section .run, "ax"
        .globl  fd_frames_asm
fd_frames_asm:
        lea     -64(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        movea.l 68(%sp), %a0            | in
        movea.l 72(%sp), %a1            | out
        movea.l 76(%sp), %a5            | s
        movea.l 80(%sp), %a4            | k
        move.l  (%a5), %d2
        move.l  4(%a5), %d3
        move.l  8(%a5), %d4
        move.l  12(%a5), %d5
        move.l  16(%a5), %d6
        move.l  20(%a5), %d7
        movea.l K_G0(%a4), %a2
        movea.l K_SIN(%a4), %a6
        lea     256(%a1), %a5           | the end of out
        clr.l   56(%sp)
        tst.l   K_DC(%a4)
        beq.w   .Lnodc
        PICK    1
.Lnodc: PICK    0
.Ldone:
        tst.l   K_DC(%a4)               | the DC block resting: dx where it takes up
        bne.s   1f
        move.l  %d4, %d3
        move.l  %d7, %d6
1:      movea.l 76(%sp), %a5            | the state back
        move.l  %d2, (%a5)
        move.l  %d3, 4(%a5)
        move.l  %d4, 8(%a5)
        move.l  %d5, 12(%a5)
        move.l  %d6, 16(%a5)
        move.l  %d7, 20(%a5)
        movem.l (%sp), %d2-%d7/%a2-%a6
        lea     64(%sp), %sp
        rts
