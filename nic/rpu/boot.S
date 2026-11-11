/*
 * Copyright (c) 2025-2026 Advanced Micro Devices, Inc. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Minimal ARM Cortex-R5 boot stub for the RPU QDMA test ELF (copied from RP1 rp1_boot.S).
 * Sets up the vector table, zeroes BSS, and jumps to rpuq_main().
 */

.section .vectors, "ax"
.global _boot
_boot:
    b       _start          /* Reset */
    b       .               /* Undefined Instruction */
    b       .               /* SVC */
    b       .               /* Prefetch Abort */
    b       _dabort         /* Data Abort: report to the mailbox, then park */
    b       .               /* Reserved */
    b       .               /* IRQ */
    b       .               /* FIQ */

.section .text, "ax"
.global _start
_start:
    /* Loaded over JTAG, possibly without a core reset (RP1 state left behind):
       mask IRQ/FIQ/async aborts first. */
    cpsid   aif
    /* Abort mode stack (data abort handler), then SVC mode stack */
    cps     #0x17
    ldr     sp, =__abt_stack_top
    cps     #0x13
    ldr     sp, =__stack_top

    /* Zero the BSS section */
    ldr     r0, =__bss_start__
    ldr     r1, =__bss_end__
    mov     r2, #0
bss_loop:
    cmp     r0, r1
    strlt   r2, [r0], #4
    blt     bss_loop

    /* Jump to C entry point */
    bl      rpuq_main

    /* Should never return */
hang:
    wfi
    b       hang

.global _dabort
_dabort:
    /* While OP_PROBE has armed probe_armed, a data abort on the probe load is
       recorded (probe_fault = 1, DFSR, DFAR) and the faulting ldr is skipped. */
    push    {r0-r3}
    ldr     r0, =probe_armed
    ldr     r1, [r0]
    cmp     r1, #0
    beq     1f
    mrc     p15, 0, r2, c5, c0, 0       /* DFSR */
    mrc     p15, 0, r3, c6, c0, 0       /* DFAR */
    ldr     r0, =probe_dfsr
    str     r2, [r0]
    ldr     r0, =probe_dfar
    str     r3, [r0]
    ldr     r0, =probe_fault
    mov     r1, #1
    str     r1, [r0]
    pop     {r0-r3}
    subs    pc, lr, #4                  /* ARM state: next instruction after the ldr */
1:
    pop     {r0-r3}
    bl      rpuq_dabort     /* does not return */
    b       .
