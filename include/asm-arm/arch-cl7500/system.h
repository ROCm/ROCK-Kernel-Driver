/*
 * linux/include/asm-arm/arch-cl7500/system.h
 *
 * Copyright (c) 1999 Nexus Electronics Ltd.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/hardware/iomd.h>

static void arch_idle(void)
{
	while (!current->need_resched && !hlt_counter)
		outb(0, IOMD_SUSMODE);
}

#define arch_reset(mode)			\
	do {					\
		outb (0, IOMD_ROMCR0);		\
		cpu_reset(0);			\
	} while (0);

#endif
