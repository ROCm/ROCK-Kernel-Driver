/*
 *  linux/include/asm-m68k/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */
 
/* Copyright(c) 1996 Kars de Jong */
/* Based on the ide driver from 1.2.13pl8 */

/*
 * Credits (alphabetical):
 *
 *  - Bjoern Brauel
 *  - Kars de Jong
 *  - Torsten Ebeling
 *  - Dwight Engen
 *  - Thorsten Floeck
 *  - Roman Hodek
 *  - Guenther Kelleter
 *  - Chris Lawrence
 *  - Michael Rausch
 *  - Christian Sauer
 *  - Michael Schmitz
 *  - Jes Soerensen
 *  - Michael Thurm
 *  - Geert Uytterhoeven
 */

#ifndef _M68K_IDE_H
#define _M68K_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>

#include <asm/setup.h>
#include <asm/io.h>
#include <asm/irq.h>

#ifdef CONFIG_ATARI
#include <linux/interrupt.h>
#include <asm/atari_stdma.h>
#endif

#ifdef CONFIG_MAC
#include <asm/macints.h>
#endif

#ifndef MAX_HWIFS
#define MAX_HWIFS	4	/* same as the other archs */
#endif


static __inline__ int ide_default_irq(ide_ioreg_t base)
{
	  return 0;
}

static __inline__ ide_ioreg_t ide_default_io_base(int index)
{
          return 0;
}


/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */
static __inline__ void ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port, ide_ioreg_t ctrl_port, int *irq)
{
	if (data_port || ctrl_port)
		printk("ide_init_hwif_ports: must not be called\n");
}

/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static __inline__ void ide_init_default_hwifs(void)
{
}

#undef SUPPORT_SLOW_DATA_PORTS
#define SUPPORT_SLOW_DATA_PORTS 0

/* this definition is used only on startup .. */
#undef HD_DATA
#define HD_DATA NULL


/* get rid of defs from io.h - ide has its private and conflicting versions */
#undef inb
#undef inw
#undef outb
#undef outw
#undef inb_p
#undef outb_p
#undef insw
#undef outsw
#undef insw_swapw
#undef outsw_swapw

/* 
 * define IO method and translation,
 * so far only Q40 has ide-if on ISA 
*/
#ifndef CONFIG_Q40

#define ADDR_TRANS_B(_addr_) (_addr_)
#define ADDR_TRANS_W(_addr_) (_addr_)

#else

#define ADDR_TRANS_B(_addr_) (MACH_IS_Q40 ? ((unsigned char *)Q40_ISA_IO_B(_addr_)) : (_addr_))
#define ADDR_TRANS_W(_addr_) (MACH_IS_Q40 ? ((unsigned char *)Q40_ISA_IO_W(_addr_)) : (_addr_))
#endif

#define inb(p)     in_8(ADDR_TRANS_B(p))
#define inb_p(p)     in_8(ADDR_TRANS_B(p))
#define inw(p)     in_be16(ADDR_TRANS_W(p))
#define inw_p(p)     in_be16(ADDR_TRANS_W(p))
#define outb(v,p)  out_8(ADDR_TRANS_B(p),v)
#define outb_p(v,p)  out_8(ADDR_TRANS_B(p),v)
#define outw(v,p)  out_be16(ADDR_TRANS_W(p),v)

#define insw(port, buf, nr) raw_insw(ADDR_TRANS_W(port), buf, nr)
#define outsw(port, buf, nr) raw_outsw(ADDR_TRANS_W(port), buf, nr)

#define insl(data_reg, buffer, wcount) insw(data_reg, buffer, (wcount)<<1)
#define outsl(data_reg, buffer, wcount) outsw(data_reg, buffer, (wcount)<<1)


#if defined(CONFIG_ATARI) || defined(CONFIG_Q40)

#define insl_swapw(data_reg, buffer, wcount) \
    insw_swapw(data_reg, buffer, (wcount)<<1)
#define outsl_swapw(data_reg, buffer, wcount) \
    outsw_swapw(data_reg, buffer, (wcount)<<1)

#define insw_swapw(port, buf, nr) raw_insw_swapw(ADDR_TRANS_W(port), buf, nr)
#define outsw_swapw(port, buf, nr) raw_outsw_swapw(ADDR_TRANS_W(port),buf,nr)

#endif /* CONFIG_ATARI || CONFIG_Q40 */

#define ATA_ARCH_ACK_INTR

#ifdef CONFIG_BLK_DEV_FALCON_IDE
#define ATA_ARCH_LOCK

extern int falconide_intr_lock;

static __inline__ void ide_release_lock (void)
{
	if (MACH_IS_ATARI) {
		if (falconide_intr_lock == 0) {
			printk("ide_release_lock: bug\n");
			return;
		}
		falconide_intr_lock = 0;
		stdma_release();
	}
}

static __inline__ void
ide_get_lock(irqreturn_t (*handler)(int, void *, struct pt_regs *), void *data)
{
	if (MACH_IS_ATARI) {
		if (falconide_intr_lock == 0) {
			if (in_interrupt() > 0)
				panic( "Falcon IDE hasn't ST-DMA lock in interrupt" );
			stdma_lock(handler, data);
			falconide_intr_lock = 1;
		}
	}
}
#endif /* CONFIG_BLK_DEV_FALCON_IDE */
#endif /* __KERNEL__ */
#endif /* _M68K_IDE_H */
