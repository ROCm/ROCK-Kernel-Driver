/*
 * linux/include/asm-arm/arch-shark/system.h
 *
 * Copyright (c) 1996-1998 Russell King.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/io.h>

static void arch_reset(char mode)
{
	short temp;
	cli();
	/* Reset the Machine via pc[3] of the sequoia chipset */
	outw(0x09,0x24);
	temp=inw(0x26);
	temp = temp | (1<<3) | (1<<10);
	outw(0x09,0x24);
	outw(temp,0x26);

}

static void arch_idle(void)
{
}

#endif
