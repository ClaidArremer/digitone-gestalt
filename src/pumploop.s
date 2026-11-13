| SPDX-License-Identifier: GPL-2.0-or-later
| PUMP's frames for a block, in assembly (GESTALT 4.4): pump.c's pm_frames (the C is the
| reference, and the host test runs it), here with the state in registers.
|
| int pm_frames_asm(const int32_t *in, int32_t *out, int g, int dg, int nr, struct pm_svf *hp)
| in and out: 32 frames, interleaved L, R. g: the gain (Q12) before the block; dg its step, over
| the first nr frames. hp: the detector's 30 Hz high-pass (lo, bd). Returns the detected peak.
|
|   d2 g   d3 dg   d4 frames left to ramp   d5 peak   d6 hp.lo   d7 hp.bd   d0 d1 scratch
|   a0 in   a1 out   a2 the end of out   a3 x0 (the frame's left, 24 bits)   a4 x1 (right)

        .equ    KNEE, 0x600000
        .equ    LMAX, 0x740000

| one channel: x24 in REG, times g (split: (x >> 12) g + ((x & 0xfff) g) >> 12), the ceiling, out
        .macro  CHAN reg
        move.l  \reg, %d0
        move.l  %d0, %d1
        asr.l   #8, %d0
        asr.l   #4, %d0
        muls.l  %d2, %d0
        andi.l  #0xfff, %d1
        muls.l  %d2, %d1
        asr.l   #8, %d1
        asr.l   #4, %d1
        add.l   %d1, %d0
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

| a frame: its two samples, the ramp, (the detector,) the two channels
        .macro  FRAME detect
        move.l  (%a0)+, %d0
        asr.l   #8, %d0
        movea.l %d0, %a3
        move.l  (%a0)+, %d0
        asr.l   #8, %d0
        movea.l %d0, %a4
        tst.l   %d4                     | the ramp
        beq.s   1f
        add.l   %d3, %d2
        subq.l  #1, %d4
1:
        .if \detect
        move.l  %a3, %d0                | the mid, (l + r) >> 1, through the high-pass:
        add.l   %a4, %d0                |   lo += bd >> 7; hi = 16 m - lo - bd - (bd >> 1);
        asr.l   #1, %d0                 |   bd += hi >> 7; |hi >> 4|
        lsl.l   #4, %d0
        move.l  %d7, %d1
        asr.l   #7, %d1
        add.l   %d1, %d6
        sub.l   %d6, %d0
        sub.l   %d7, %d0
        move.l  %d7, %d1
        asr.l   #1, %d1
        sub.l   %d1, %d0
        move.l  %d0, %d1
        asr.l   #7, %d1
        add.l   %d1, %d7
        asr.l   #4, %d0
        bpl.s   2f
        neg.l   %d0
2:      move.l  %a3, %d1                | + |the side|, (l - r) >> 1
        sub.l   %a4, %d1
        asr.l   #1, %d1
        bpl.s   3f
        neg.l   %d1
3:      add.l   %d1, %d0
        cmp.l   %d5, %d0
        ble.s   4f
        move.l  %d0, %d5
4:
        .endif
        CHAN    %a3
        CHAN    %a4
        .endm

        .section .run, "ax"
        .globl  pm_frames_asm
pm_frames_asm:
        lea     -44(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        movea.l 48(%sp), %a0            | in
        movea.l 52(%sp), %a1            | out
        move.l  56(%sp), %d2            | g
        move.l  60(%sp), %d3            | dg
        move.l  64(%sp), %d4            | nr
        movea.l 68(%sp), %a5            | hp
        move.l  (%a5), %d6
        move.l  4(%a5), %d7
        moveq   #0, %d5
        lea     256(%a1), %a2
.Lpair: FRAME   1
        FRAME   0
        cmpa.l  %a2, %a1
        bne.w   .Lpair
        movea.l 68(%sp), %a5
        move.l  %d6, (%a5)
        move.l  %d7, 4(%a5)
        move.l  %d5, %d0
        movem.l (%sp), %d2-%d7/%a2-%a6
        lea     44(%sp), %sp
        rts
