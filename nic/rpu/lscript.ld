/**
 * The MIT License (MIT)
 * Copyright (c) 2025-2026 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 * NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Linker script for the RPU QDMA test ELF (copied from RP1 rp1_lscript.ld) (ARM Cortex-R5 core 1).
 *
 * Memory regions use R5-1 local addresses:
 *   ATCM at 0x0      (64KB) - boot vectors + code
 *   BTCM at 0x20000  (64KB) - stack + data
 *   DDR private at 0x10000000 (512MB) - RP1-only scratch/staging region
 *   DDR shared  at 0x30000000  (64MB) - BAR-visible control/queue region
 */

_STACK_SIZE = 0x1000;   /* 4KB stack - sufficient for baremetal bringup */

MEMORY
{
    r5_1_atcm : ORIGIN = 0x00000000, LENGTH = 0x10000    /* 64KB */
    r5_1_btcm : ORIGIN = 0x00020000, LENGTH = 0x10000    /* 64KB */
    ddr_private : ORIGIN = 0x10000000, LENGTH = 0x20000000 /* 512MB */
    ddr_shared  : ORIGIN = 0x30000000, LENGTH = 0x04000000 /* 64MB */
}

ENTRY(_boot)

SECTIONS
{
    .vectors : {
        KEEP(*(.vectors))
    } > r5_1_atcm

    .text : {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } > r5_1_atcm

    .data : {
        __data_start = .;
        *(.data)
        *(.data.*)
        __data_end = .;
    } > r5_1_btcm

    /* Hot BTCM data: node snapshot, barriers, loop counters, inflight table.
     * Explicitly placed before BSS so the complete footprint is checkable. */
    .btcm (NOLOAD) : {
        . = ALIGN(4);
        __btcm_snapshot_start = .;
        *(.btcm)
        . = ALIGN(4);
        __btcm_snapshot_end = .;
    } > r5_1_btcm

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start__ = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > r5_1_btcm

    .stack (NOLOAD) : {
        . = ALIGN(16);
        _stack_end = .;
        . += _STACK_SIZE;
        . = ALIGN(16);
        __stack_top = .;
        . += 0x200;
        . = ALIGN(16);
        __abt_stack_top = .;
    } > r5_1_btcm

    ASSERT(__stack_top <= ORIGIN(r5_1_btcm) + LENGTH(r5_1_btcm),
           "BTCM data and stack exceed 64 KB")

    /* BAR-visible shared DDR region for control/status structures. */
    .rp1_shared (NOLOAD) : {
        *(.rp1_shared)
    } > ddr_shared

    _end = .;
}
