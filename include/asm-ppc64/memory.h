#ifndef _ASM_PPC64_MEMORY_H_ 
#define _ASM_PPC64_MEMORY_H_ 

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>

/*
 * Arguably the bitops and *xchg operations don't imply any memory barrier
 * or SMP ordering, but in fact a lot of drivers expect them to imply
 * both, since they do on x86 cpus.
 */
#ifdef CONFIG_SMP
#define EIEIO_ON_SMP	"eieio\n"
#define ISYNC_ON_SMP	"\n\tisync"
#else
#define EIEIO_ON_SMP
#define ISYNC_ON_SMP
#endif

static inline void eieio(void)
{
	__asm__ __volatile__ ("eieio" : : : "memory");
}

static inline void isync(void)
{
	__asm__ __volatile__ ("isync" : : : "memory");
}

#ifdef CONFIG_SMP
#define eieio_on_smp()	eieio()
#define isync_on_smp()	isync()
#else
#define eieio_on_smp()	__asm__ __volatile__("": : :"memory")
#define isync_on_smp()	__asm__ __volatile__("": : :"memory")
#endif

#endif
