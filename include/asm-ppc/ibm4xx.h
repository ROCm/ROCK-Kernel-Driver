/*
 *
 *    Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *
 *    Module name: ibm4xx.h
 *
 *    Description:
 *	A generic include file which pulls in appropriate include files
 *      for specific board types based on configuration settings.
 *
 */

#ifdef __KERNEL__
#ifndef __ASM_IBM4XX_H__
#define __ASM_IBM4XX_H__

#include <linux/config.h>

#ifdef CONFIG_40x

#if defined(CONFIG_ASH)
#include <platforms/4xx/ash.h>
#endif

#if defined (CONFIG_CEDAR)
#include <platforms/4xx/cedar.h>
#endif

#if defined(CONFIG_CPCI405)
#include <platforms/4xx/cpci405.h>
#endif

#if defined(CONFIG_EP405)
#include <platforms/4xx/ep405.h>
#endif

#if defined(CONFIG_OAK)
#include <platforms/4xx/oak.h>
#endif

#if defined(CONFIG_REDWOOD_4)
#include <platforms/4xx/redwood.h>
#endif

#if defined(CONFIG_REDWOOD_5)
#include <platforms/4xx/redwood5.h>
#endif

#if defined(CONFIG_WALNUT)
#include <platforms/4xx/walnut.h>
#endif

#ifndef __ASSEMBLY__

/*
 * The "residual" board information structure the boot loader passes
 * into the kernel.
 */
extern bd_t __res;

void ppc4xx_setup_arch(void);
void ppc4xx_map_io(void);
void ppc4xx_init_IRQ(void);
void ppc4xx_init(unsigned long r3, unsigned long r4, unsigned long r5,
		 unsigned long r6, unsigned long r7);
#endif

#ifndef PPC4xx_MACHINE_NAME
#define PPC4xx_MACHINE_NAME	"Unidentified 4xx class"
#endif


/* IO_BASE is for PCI I/O.
 * ISA not supported, just here to resolve copilation.
 */

#ifndef _IO_BASE
#define _IO_BASE	0xe8000000	/* The PCI address window */
#define _ISA_MEM_BASE	0
#define PCI_DRAM_OFFSET	0
#endif

#elif CONFIG_44x

#if defined(CONFIG_EBONY)
#include <platforms/4xx/ebony.h>
#endif

#if defined(CONFIG_OCOTEA)
#include <platforms/4xx/ocotea.h>
#endif

#endif /* CONFIG_40x */

#ifndef __ASSEMBLY__
/*
 * The "residual" board information structure the boot loader passes
 * into the kernel.
 */
extern bd_t __res;
#endif

#endif /* __ASM_IBM4XX_H__ */
#endif /* __KERNEL__ */
