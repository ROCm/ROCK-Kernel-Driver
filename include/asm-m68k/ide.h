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

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned bit7		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit5		: 1;	/* always 1 */
		unsigned unit		: 1;	/* drive select number, 0 or 1 */
		unsigned head		: 4;	/* always zeros here */
	} b;
	} select_t;

static __inline__ int ide_request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
			unsigned long flags, const char *device, void *dev_id)
{
#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA)
		return request_irq(irq, handler, 0, device, dev_id);
#endif /* CONFIG_AMIGA */
#ifdef CONFIG_Q40
	if (MACH_IS_Q40)
	  	return request_irq(irq, handler, 0, device, dev_id);
#endif /* CONFIG_Q40*/
#ifdef CONFIG_MAC
	if (MACH_IS_MAC)
		return request_irq(irq, handler, 0, device, dev_id);
#endif /* CONFIG_MAC */
	return 0;
}

static __inline__ void ide_free_irq(unsigned int irq, void *dev_id)
{
#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA)
		free_irq(irq, dev_id);
#endif /* CONFIG_AMIGA */
#ifdef CONFIG_Q40
	if (MACH_IS_Q40)
	  	free_irq(irq, dev_id);
#endif /* CONFIG_Q40*/
#ifdef CONFIG_MAC
	if (MACH_IS_MAC)
		free_irq(irq, dev_id);
#endif /* CONFIG_MAC */
}

/*
 * We should really implement those some day.
 */
static __inline__ int ide_check_region (ide_ioreg_t from, unsigned int extent)
{
	return 0;
}

static __inline__ void ide_request_region (ide_ioreg_t from, unsigned int extent, const char *name)
{
#ifdef CONFIG_Q40
        if (MACH_IS_Q40)
            request_region((q40ide_ioreg_t)from,extent,name);
#endif
}

static __inline__ void ide_release_region (ide_ioreg_t from, unsigned int extent)
{
#ifdef CONFIG_Q40
        if (MACH_IS_Q40)
            release_region((q40ide_ioreg_t)from,extent);
#endif
}

#undef SUPPORT_SLOW_DATA_PORTS
#define SUPPORT_SLOW_DATA_PORTS 0

#undef SUPPORT_VLB_SYNC
#define SUPPORT_VLB_SYNC 0

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


#define T_CHAR          (0x0000)        /* char:  don't touch  */
#define T_SHORT         (0x4000)        /* short: 12 -> 21     */
#define T_INT           (0x8000)        /* int:   1234 -> 4321 */
#define T_TEXT          (0xc000)        /* text:  12 -> 21     */

#define T_MASK_TYPE     (0xc000)
#define T_MASK_COUNT    (0x3fff)

#define D_CHAR(cnt)     (T_CHAR  | (cnt))
#define D_SHORT(cnt)    (T_SHORT | (cnt))
#define D_INT(cnt)      (T_INT   | (cnt))
#define D_TEXT(cnt)     (T_TEXT  | (cnt))

/* Q40 and Atari have byteswapped IDE bus and since many interesting
 * values in the identification string are text, chars and words they
 * happened to be almost correct without swapping.. However *_capacity 
 * is needed for drives over 8 GB. RZ */
#if defined(CONFIG_Q40) || defined(CONFIG_ATARI)
#define M68K_IDE_SWAPW  (MACH_IS_Q40 || MACH_IS_ATARI)
#endif

#if defined(CONFIG_AMIGA) || defined (CONFIG_MAC) || defined(M68K_IDE_SWAPW)
static u_short driveid_types[] = {
	D_SHORT(10),	/* config - vendor2 */
	D_TEXT(20),	/* serial_no */
	D_SHORT(3),	/* buf_type, buf_size - ecc_bytes */
	D_TEXT(48),	/* fw_rev - model */
	D_CHAR(2),	/* max_multsect - vendor3 */
	D_SHORT(1),	/* dword_io */
	D_CHAR(2),	/* vendor4 - capability */
	D_SHORT(1),	/* reserved50 */
	D_CHAR(4),	/* vendor5 - tDMA */
	D_SHORT(4),	/* field_valid - cur_sectors */
	D_INT(1),	/* cur_capacity */
	D_CHAR(2),	/* multsect - multsect_valid */
	D_INT(1),	/* lba_capacity */
	D_SHORT(194)	/* dma_1word - reserved */
};

#define num_driveid_types       (sizeof(driveid_types)/sizeof(*driveid_types))
#endif /* CONFIG_AMIGA */

static __inline__ void ide_fix_driveid(struct hd_driveid *id)
{
#if defined(CONFIG_AMIGA) || defined (CONFIG_MAC) || defined(M68K_IDE_SWAPW)
   u_char *p = (u_char *)id;
   int i, j, cnt;
   u_char t;

   if (!MACH_IS_AMIGA && !MACH_IS_MAC && !MACH_IS_Q40 && !MACH_IS_ATARI)
   	return;
#ifdef M68K_IDE_SWAPW
   if (M68K_IDE_SWAPW)    /* fix bus byteorder first */
      for (i=0; i < 512; i+=2) {
	 t = p[i]; p[i] = p[i+1]; p[i+1] = t; 
      }
#endif
   for (i = 0; i < num_driveid_types; i++) {
      cnt = driveid_types[i] & T_MASK_COUNT;
      switch (driveid_types[i] & T_MASK_TYPE) {
         case T_CHAR:
            p += cnt;
            break;
         case T_SHORT:
            for (j = 0; j < cnt; j++) {
	       t = p[0];
	       p[0] = p[1];
	       p[1] = t;
               p += 2;
            }
            break;
         case T_INT:
            for (j = 0; j < cnt; j++) {
	       t = p[0];
	       p[0] = p[3];
	       p[3] = t;
	       t = p[1];
	       p[1] = p[2];
	       p[2] = t;
               p += 4;
            }
            break;
         case T_TEXT:
            for (j = 0; j < cnt; j += 2) {
	       t = p[0];
	       p[0] = p[1];
	       p[1] = t;
               p += 2;
            }
            break;
      }
   }
#endif /* CONFIG_AMIGA */
}

static __inline__ void ide_release_lock (int *ide_lock)
{
#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI) {
		if (*ide_lock == 0) {
			printk("ide_release_lock: bug\n");
			return;
		}
		*ide_lock = 0;
		stdma_release();
	}
#endif /* CONFIG_ATARI */
}

static __inline__ void ide_get_lock (int *ide_lock, void (*handler)(int, void *, struct pt_regs *), void *data)
{
#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI) {
		if (*ide_lock == 0) {
			if (in_interrupt() > 0)
				panic( "Falcon IDE hasn't ST-DMA lock in interrupt" );
			stdma_lock(handler, data);
			*ide_lock = 1;
		}
	}
#endif /* CONFIG_ATARI */
}

#define ide_ack_intr(hwif)	((hwif)->hw.ack_intr ? (hwif)->hw.ack_intr(hwif) : 1)

/*
 * On the Atari, we sometimes can't enable interrupts:
 */

/* MSch: changed sti() to STI() wherever possible in ide.c; moved STI() def. 
 * to asm/ide.h 
 */
/* The Atari interrupt structure strictly requires that the IPL isn't lowered
 * uncontrolled in an interrupt handler. In the concrete case, the IDE
 * interrupt is already a slow int, so the irq is already disabled at the time
 * the handler is called, and the IPL has been lowered to the minimum value
 * possible. To avoid going below that, STI() checks for being called inside
 * an interrupt, and in that case it does nothing. Hope that is reasonable and
 * works. (Roman)
 */
#ifdef MACH_ATARI_ONLY
#define	ide__sti()					\
    do {						\
	if (!in_interrupt()) __sti();			\
    } while(0)
#elif defined(CONFIG_ATARI)
#define	ide__sti()						\
    do {							\
	if (!MACH_IS_ATARI || !in_interrupt()) sti();		\
    } while(0)
#else /* !defined(CONFIG_ATARI) */
#define	ide__sti()	__sti()
#endif

#endif /* __KERNEL__ */

#endif /* _M68K_IDE_H */
