/*
 *  linux/include/asm-arm/arch-pxa/hardware.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <linux/config.h>
#include <asm/mach-types.h>


/*
 * These are statically mapped PCMCIA IO space for designs using it as a
 * generic IO bus, typically with ISA parts, hardwired IDE interfaces, etc.
 * The actual PCMCIA code is mapping required IO region at run time.
 */
#define PCMCIA_IO_0_BASE	0xf6000000
#define PCMCIA_IO_1_BASE	0xf7000000


/*
 * Intel PXA internal I/O mappings
 */

#define io_p2v(x)	\
	(((x) < 0x44000000) ? ((x) - 0x40000000 + 0xfc000000) :	\
	 ((x) < 0x48000000) ? ((x) - 0x44000000 + 0xfe000000) :	\
	 		      ((x) - 0x48000000 + 0xff000000))
#define io_v2p( x )	\
	(((x) < 0xfe000000) ? ((x) - 0xfc000000 + 0x40000000) :	\
	 ((x) < 0xff000000) ? ((x) - 0xfe000000 + 0x44000000) :	\
	 		      ((x) - 0xff000000 + 0x48000000))

#ifndef __ASSEMBLY__

#if 0
# define __REG(x)	(*((volatile u32 *)io_p2v(x)))
#else
/*
 * This __REG() version gives the same results as the one above,  except
 * that we are fooling gcc somehow so it generates far better and smaller
 * assembly code for access to contigous registers.  It's a shame that gcc
 * doesn't guess this by itself.
 */
#include <asm/types.h>
typedef struct { volatile u32 offset[1024]; } __regbase;
# define __REGP(x)	((__regbase *)((x)&~4095))->offset[((x)&4095)>>2]
# define __REG(x)	__REGP(io_p2v(x))
#endif

/* Let's kick gcc's ass again... */
# define __REG2(x,y)	\
	( __builtin_constant_p(y) ? (__REG((x) + (y))) \
				  : (*(volatile u32 *)((u32)&__REG(x) + (y))) )

# define __PREG(x)	(io_v2p((u32)&(x)))

#else

# define __REG(x)	io_p2v(x)
# define __PREG(x)	io_v2p(x)

#endif

#include "pxa-regs.h"

#ifndef __ASSEMBLY__

/*
 * Handy routine to set GPIO alternate functions
 */
extern void pxa_gpio_mode( int gpio_mode );

/*
 * return current lclk frequency in units of 10kHz
 */
extern unsigned int get_lclk_frequency_10khz(void);

#endif


/*
 * Implementation specifics
 */

#include "lubbock.h"
#include "idp.h"

#endif  /* _ASM_ARCH_HARDWARE_H */
