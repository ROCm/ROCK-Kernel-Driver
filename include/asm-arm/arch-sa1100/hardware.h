/*
 * linux/include/asm-arm/arch-sa1100/hardware.h
 *
 * Copyright (C) 1998 Nicolas Pitre <nico@cam.org>
 *
 * This file contains the hardware definitions for SA1100 architecture
 *
 * 2000/05/23 John Dorsey <john+@cs.cmu.edu>
 *      Definitions for SA1111 added.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <linux/config.h>
#include <asm/mach-types.h>


/* Flushing areas */
#define FLUSH_BASE_PHYS		0xe0000000	/* SA1100 zero bank */
#define FLUSH_BASE		0xf5000000
#define FLUSH_BASE_MINICACHE	0xf5800000
#define UNCACHEABLE_ADDR	0xfa050000


/*
 * Those are statically mapped PCMCIA IO space for designs using it as a
 * generic IO bus, typically with ISA parts, hardwired IDE interfaces, etc.
 * The actual PCMCIA code is mapping required IO region at run time.
 */
#define PCMCIA_IO_0_BASE	0xf6000000
#define PCMCIA_IO_1_BASE	0xf7000000


/*
 * We requires absolute addresses i.e. (PCMCIA_IO_0_BASE + 0x3f8) for 
 * in*()/out*() macros to be usable for all cases.
 */
#define PCIO_BASE		0


/*
 * SA1100 internal I/O mappings
 *
 * We have the following mapping:
 *      phys            virt
 *      80000000        f8000000
 *      90000000        fa000000
 *      a0000000        fc000000
 *      b0000000        fe000000
 */

#define VIO_BASE        0xf8000000	/* virtual start of IO space */
#define VIO_SHIFT       3		/* x = IO space shrink power */
#define PIO_START       0x80000000	/* physical start of IO space */

#define io_p2v( x )             \
   ( (((x)&0x00ffffff) | (((x)&0x30000000)>>VIO_SHIFT)) + VIO_BASE )
#define io_v2p( x )             \
   ( (((x)&0x00ffffff) | (((x)&(0x30000000>>VIO_SHIFT))<<VIO_SHIFT)) + PIO_START )

#include "SA-1100.h"


/*
 * SA1100 GPIO edge detection for IRQs:
 * IRQs are generated on Falling-Edge, Rising-Edge, or both.
 * This must be called *before* the corresponding IRQ is registered.
 * Use this instead of directly setting GRER/GFER.
 */
#define GPIO_FALLING_EDGE       1
#define GPIO_RISING_EDGE        2
#define GPIO_BOTH_EDGES         3
#ifndef __ASSEMBLY__
extern void set_GPIO_IRQ_edge( int gpio_mask, int edge_mask );
#endif


/*
 * Implementation specifics
 */

#ifdef CONFIG_SA1100_ASSABET
#include "assabet.h"
#else
#define machine_has_neponset()	(0)
#endif

#ifdef CONFIG_SA1100_CERF
#include "cerf.h"
#endif

#ifdef CONFIG_SA1100_EMPEG
#include "empeg.h"
#endif

#ifdef CONFIG_SA1100_BITSY
#include "bitsy.h"
#endif

#if defined(CONFIG_SA1100_THINCLIENT)
#include "thinclient.h"
#endif

#if defined(CONFIG_SA1100_GRAPHICSCLIENT)
#include "graphicsclient.h"
#endif


#ifdef CONFIG_SA1101

/*
 * We have mapped the sa1101 depending on the value of SA1101_BASE.
 * It then appears from 0xf4000000.
 */

#define SA1101_p2v( x )         ((x) - SA1101_BASE + 0xf4000000)
#define SA1101_v2p( x )         ((x) - 0xf4000000  + SA1101_BASE)

#include "SA-1101.h"

#endif


#ifdef CONFIG_SA1111

#define SA1111_p2v( x )         ((x) - SA1111_BASE + 0xf4000000)
#define SA1111_v2p( x )         ((x) - 0xf4000000 + SA1111_BASE)

#include "SA-1111.h"

#endif

#endif  /* _ASM_ARCH_HARDWARE_H */
