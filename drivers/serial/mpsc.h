/*
 * drivers/serial/mpsc.h
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef	__MPSC_H__
#define	__MPSC_H__

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/serial.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/irq.h>

#if defined(CONFIG_SERIAL_MPSC_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>
#include "mpsc_defs.h"

/*
 * Descriptors and buffers must be cache line aligned.
 * Buffers lengths must be multiple of cache line size.
 * Number of Tx & Rx descriptors must be powers of 2.
 */
#define	MPSC_RXR_ENTRIES	32
#define	MPSC_RXRE_SIZE		dma_get_cache_alignment()
#define	MPSC_RXR_SIZE		(MPSC_RXR_ENTRIES * MPSC_RXRE_SIZE)
#define	MPSC_RXBE_SIZE		dma_get_cache_alignment()
#define	MPSC_RXB_SIZE		(MPSC_RXR_ENTRIES * MPSC_RXBE_SIZE)

#define	MPSC_TXR_ENTRIES	32
#define	MPSC_TXRE_SIZE		dma_get_cache_alignment()
#define	MPSC_TXR_SIZE		(MPSC_TXR_ENTRIES * MPSC_TXRE_SIZE)
#define	MPSC_TXBE_SIZE		dma_get_cache_alignment()
#define	MPSC_TXB_SIZE		(MPSC_TXR_ENTRIES * MPSC_TXBE_SIZE)

#define	MPSC_DMA_ALLOC_SIZE	(MPSC_RXR_SIZE + MPSC_RXB_SIZE +	\
				MPSC_TXR_SIZE + MPSC_TXB_SIZE +		\
				dma_get_cache_alignment() /* for alignment */)

/* Rx and Tx Ring entry descriptors -- assume entry size is <= cacheline size */
struct mpsc_rx_desc {
	u16 bufsize;
	u16 bytecnt;
	u32 cmdstat;
	u32 link;
	u32 buf_ptr;
} __attribute((packed));

struct mpsc_tx_desc {
	u16 bytecnt;
	u16 shadow;
	u32 cmdstat;
	u32 link;
	u32 buf_ptr;
} __attribute((packed));

/*
 * Some regs that have the erratum that you can't read them are are shared
 * between the two MPSC controllers.  This struct contains those shared regs.
 */
struct mpsc_shared_regs {
	u32 mpsc_routing_base_p;
	u32 sdma_intr_base_p;

	u32 mpsc_routing_base;
	u32 sdma_intr_base;

	u32 MPSC_MRR_m;
	u32 MPSC_RCRR_m;
	u32 MPSC_TCRR_m;
	u32 SDMA_INTR_CAUSE_m;
	u32 SDMA_INTR_MASK_m;
};

/* The main driver data structure */
struct mpsc_port_info {
	struct uart_port port;	/* Overlay uart_port structure */

	/* Internal driver state for this ctlr */
	u8 ready;
	u8 rcv_data;
	tcflag_t c_iflag;	/* save termios->c_iflag */
	tcflag_t c_cflag;	/* save termios->c_cflag */

	/* Info passed in from platform */
	u8 mirror_regs;		/* Need to mirror regs? */
	u8 cache_mgmt;		/* Need manual cache mgmt? */
	u8 brg_can_tune;	/* BRG has baud tuning? */
	u32 brg_clk_src;
	u16 mpsc_max_idle;
	int default_baud;
	int default_bits;
	int default_parity;
	int default_flow;

	/* Physical addresses of various blocks of registers (from platform) */
	u32 mpsc_base_p;
	u32 sdma_base_p;
	u32 brg_base_p;

	/* Virtual addresses of various blocks of registers (from platform) */
	u32 mpsc_base;
	u32 sdma_base;
	u32 brg_base;

	/* Descriptor ring and buffer allocations */
	void *dma_region;
	dma_addr_t dma_region_p;

	dma_addr_t rxr;		/* Rx descriptor ring */
	dma_addr_t rxr_p;	/* Phys addr of rxr */
	u8 *rxb;		/* Rx Ring I/O buf */
	u8 *rxb_p;		/* Phys addr of rxb */
	u32 rxr_posn;		/* First desc w/ Rx data */

	dma_addr_t txr;		/* Tx descriptor ring */
	dma_addr_t txr_p;	/* Phys addr of txr */
	u8 *txb;		/* Tx Ring I/O buf */
	u8 *txb_p;		/* Phys addr of txb */
	int txr_head;		/* Where new data goes */
	int txr_tail;		/* Where sent data comes off */

	/* Mirrored values of regs we can't read (if 'mirror_regs' set) */
	u32 MPSC_MPCR_m;
	u32 MPSC_CHR_1_m;
	u32 MPSC_CHR_2_m;
	u32 MPSC_CHR_10_m;
	u32 BRG_BCR_m;
	struct mpsc_shared_regs *shared_regs;
};

#if defined(CONFIG_PPC32)

#if defined(CONFIG_NOT_COHERENT_CACHE)
/* No-ops when coherency is off b/c dma_cache_sync() does that work */
#define	MPSC_CACHE_INVALIDATE(pi, s, e)
#define	MPSC_CACHE_FLUSH(pi, s, e)
#else /* defined(CONFIG_NOT_COHERENT_CACHE) */
/* Coherency is on so dma_cache_sync() is no-op so must do manually */
#define	MPSC_CACHE_INVALIDATE(pi, s, e) {			\
	if (pi->cache_mgmt) {					\
		invalidate_dcache_range((ulong)s, (ulong)e);	\
	}							\
}

#define	MPSC_CACHE_FLUSH(pi, s, e) {			\
	if (pi->cache_mgmt) {				\
		flush_dcache_range((ulong)s, (ulong)e);	\
	}						\
}
#endif /* defined(CONFIG_NOT_COHERENT_CACHE) */

#else /* defined(CONFIG_PPC32) */
/* Other architectures need to fill this in */
#define	MPSC_CACHE_INVALIDATE(pi, s, e)	BUG()
#define	MPSC_CACHE_FLUSH(pi, s, e)	BUG()
#endif /* defined(CONFIG_PPC32) */

/*
 * 'MASK_INSERT' takes the low-order 'n' bits of 'i', shifts it 'b' bits to
 * the left, and inserts it into the target 't'.  The corresponding bits in
 * 't' will have been cleared before the bits in 'i' are inserted.
 */
#ifdef CONFIG_PPC32
#define MASK_INSERT(t, i, n, b) ({				\
	u32	rval = (t);					\
        __asm__ __volatile__(					\
		"rlwimi %0,%2,%4,32-(%3+%4),31-%4\n"		\
		: "=r" (rval)					\
		: "0" (rval), "r" (i), "i" (n), "i" (b));	\
	rval;							\
})
#else
/* These macros are really just examples.  Feel free to change them --MAG */
#define GEN_MASK(n, b)			\
({					\
	u32	m, sl, sr;		\
	sl = 32 - (n);			\
	sr = sl - (b);			\
	m = (0xffffffff << sl) >> sr;	\
})

#define MASK_INSERT(t, i, n, b)		\
({					\
	u32	m, rval = (t);		\
	m = GEN_MASK((n), (b));		\
	rval &= ~m;			\
	rval |= (((i) << (b)) & m);	\
})
#endif

/* I/O macros for regs that you can read */
#define	MPSC_READ(pi, unit, offset)					\
	readl((volatile void *)((pi)->unit##_base + (offset)))

#define	MPSC_WRITE(pi, unit, offset, v)					\
	writel(v, (volatile void *)((pi)->unit##_base + (offset)))

#define	MPSC_MOD_FIELD(pi, unit, offset, num_bits, shift, val)		\
{									\
	u32	v;							\
	v = readl((volatile void *)((pi)->unit##_base + (offset)));	\
	writel(MASK_INSERT(v,val,num_bits,shift),			\
		(volatile void *)((pi)->unit##_base+(offset)));		\
}

/* Macros for regs with erratum that are not shared between MPSC ctlrs */
#define	MPSC_READ_M(pi, unit, offset)					\
({									\
	u32	v;							\
	if ((pi)->mirror_regs) v = (pi)->offset##_m;			\
	else v = readl((volatile void *)((pi)->unit##_base + (offset)));\
	v;								\
})

#define	MPSC_WRITE_M(pi, unit, offset, v)				\
({									\
	if ((pi)->mirror_regs) (pi)->offset##_m = v;			\
	writel(v, (volatile void *)((pi)->unit##_base + (offset)));	\
})

#define	MPSC_MOD_FIELD_M(pi, unit, offset, num_bits, shift, val)	\
({									\
	u32	v;							\
	if ((pi)->mirror_regs) v = (pi)->offset##_m;			\
	else v = readl((volatile void *)((pi)->unit##_base + (offset)));\
	v = MASK_INSERT(v, val, num_bits, shift);			\
	if ((pi)->mirror_regs) (pi)->offset##_m = v;			\
	writel(v, (volatile void *)((pi)->unit##_base + (offset)));	\
})

/* Macros for regs with erratum that are shared between MPSC ctlrs */
#define	MPSC_READ_S(pi, unit, offset)					\
({									\
	u32	v;							\
	if ((pi)->mirror_regs) v = (pi)->shared_regs->offset##_m;	\
	else v = readl((volatile void *)((pi)->shared_regs->unit##_base + \
		(offset)));						\
	v;								\
})

#define	MPSC_WRITE_S(pi, unit, offset, v)				\
({									\
	if ((pi)->mirror_regs) (pi)->shared_regs->offset##_m = v;	\
	writel(v, (volatile void *)((pi)->shared_regs->unit##_base +	\
		(offset)));						\
})

#define	MPSC_MOD_FIELD_S(pi, unit, offset, num_bits, shift, val)	\
({									\
	u32	v;							\
	if ((pi)->mirror_regs) v = (pi)->shared_regs->offset##_m;	\
	else v = readl((volatile void *)((pi)->shared_regs->unit##_base + \
		(offset)));						\
	v = MASK_INSERT(v, val, num_bits, shift);			\
	if ((pi)->mirror_regs) (pi)->shared_regs->offset##_m = v;	\
	writel(v, (volatile void *)((pi)->shared_regs->unit##_base +	\
		(offset)));						\
})

/* Hooks to platform-specific code */
int mpsc_platform_register_driver(void);
void mpsc_platform_unregister_driver(void);

/* Hooks back in to mpsc common to be called by platform-specific code */
struct mpsc_port_info *mpsc_device_probe(int index);
struct mpsc_port_info *mpsc_device_remove(int index);

#endif				/* __MPSC_H__ */
