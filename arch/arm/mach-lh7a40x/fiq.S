/*
 *  linux/arch/arm/lib/lh7a400-fiqhandler.S
 *     Copyright (C) 2002, Lineo, Inc.
 *  based on  linux/arch/arm/lib/floppydma.S, which is
 *      Copyright (C) 1995, 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
       .text

       .global fiqhandler_end

       @ register usage:
       @        r8  &interrupt controller registers
       @        r9  &gpio registers
       @       r11  work
       @       r12  work

ENTRY(fiqhandler)

       @ read the status register to find out which FIQ this is

       ldr     r12, [r8]               @ intc->status
       and     r12, r12, #0xf          @ only interested in low-order 4 bits

       @ translate FIQ 0:3 to IRQ 23:26
       @ disable this FIQ and enable the corresponding IRQ

       str     r12, [r8, #0xc]         @ disable this FIQ
       mov     r12, r12, lsl #23       @ get the corresopnding IRQ bit
       str     r12, [r8, #0x8]         @ enable that IRQ

       subs    pc, lr, #4
fiqhandler_end:

