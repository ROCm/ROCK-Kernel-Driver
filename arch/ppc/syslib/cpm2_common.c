/*
 * General Purpose functions for the global management of the
 * 8260 Communication Processor Module.
 * Copyright (c) 1999 Dan Malek (dmalek@jlc.net)
 * Copyright (c) 2000 MontaVista Software, Inc (source@mvista.com)
 *	2.3.99 Updates
 *
 * In addition to the individual control of the communication
 * channels, there are a few functions that globally affect the
 * communication processor.
 *
 * Buffer descriptors must be allocated from the dual ported memory
 * space.  The allocator for that is here.  When the communication
 * process is reset, we reclaim the memory available.  There is
 * currently no deallocator for this memory.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <asm/irq.h>
#include <asm/mpc8260.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/immap_cpm2.h>
#include <asm/cpm2.h>
#include <asm/rheap.h>

static void cpm2_dpinit(void);
cpm_cpm2_t	*cpmp;		/* Pointer to comm processor space */

/* We allocate this here because it is used almost exclusively for
 * the communication processor devices.
 */
cpm2_map_t *cpm2_immr;

void
cpm2_reset(void)
{
	cpm2_immr = (cpm2_map_t *)CPM_MAP_ADDR;

	/* Reclaim the DP memory for our use.
	 */
	cpm2_dpinit();

	/* Tell everyone where the comm processor resides.
	 */
	cpmp = &cpm2_immr->im_cpm;
}

/* Set a baud rate generator.  This needs lots of work.  There are
 * eight BRGs, which can be connected to the CPM channels or output
 * as clocks.  The BRGs are in two different block of internal
 * memory mapped space.
 * The baud rate clock is the system clock divided by something.
 * It was set up long ago during the initial boot phase and is
 * is given to us.
 * Baud rate clocks are zero-based in the driver code (as that maps
 * to port numbers).  Documentation uses 1-based numbering.
 */
#define BRG_INT_CLK	(((bd_t *)__res)->bi_brgfreq)
#define BRG_UART_CLK	(BRG_INT_CLK/16)

/* This function is used by UARTS, or anything else that uses a 16x
 * oversampled clock.
 */
void
cpm2_setbrg(uint brg, uint rate)
{
	volatile uint	*bp;

	/* This is good enough to get SMCs running.....
	*/
	if (brg < 4) {
		bp = (uint *)&cpm2_immr->im_brgc1;
	}
	else {
		bp = (uint *)&cpm2_immr->im_brgc5;
		brg -= 4;
	}
	bp += brg;
	*bp = ((BRG_UART_CLK / rate) << 1) | CPM_BRG_EN;
}

/* This function is used to set high speed synchronous baud rate
 * clocks.
 */
void
cpm2_fastbrg(uint brg, uint rate, int div16)
{
	volatile uint	*bp;

	if (brg < 4) {
		bp = (uint *)&cpm2_immr->im_brgc1;
	}
	else {
		bp = (uint *)&cpm2_immr->im_brgc5;
		brg -= 4;
	}
	bp += brg;
	*bp = ((BRG_INT_CLK / rate) << 1) | CPM_BRG_EN;
	if (div16)
		*bp |= CPM_BRG_DIV16;
}

/*
 * dpalloc / dpfree bits.
 */
static spinlock_t cpm_dpmem_lock;
/* 16 blocks should be enough to satisfy all requests
 * until the memory subsystem goes up... */
static rh_block_t cpm_boot_dpmem_rh_block[16];
static rh_info_t cpm_dpmem_info;

static void cpm2_dpinit(void)
{
	void *dprambase = &((cpm2_map_t *)CPM_MAP_ADDR)->im_dprambase;

	spin_lock_init(&cpm_dpmem_lock);

	/* initialize the info header */
	rh_init(&cpm_dpmem_info, 1,
			sizeof(cpm_boot_dpmem_rh_block) /
			sizeof(cpm_boot_dpmem_rh_block[0]),
			cpm_boot_dpmem_rh_block);

	/* Attach the usable dpmem area */
	/* XXX: This is actually crap. CPM_DATAONLY_BASE and
	 * CPM_DATAONLY_SIZE is only a subset of the available dpram. It
	 * varies with the processor and the microcode patches activated.
	 * But the following should be at least safe.
	 */
	rh_attach_region(&cpm_dpmem_info, dprambase + CPM_DATAONLY_BASE,
			CPM_DATAONLY_SIZE);
}

/* This function used to return an index into the DPRAM area.
 * Now it returns the actuall physical address of that area.
 * use cpm2_dpram_offset() to get the index
 */
void *cpm2_dpalloc(uint size, uint align)
{
	void *start;
	unsigned long flags;

	spin_lock_irqsave(&cpm_dpmem_lock, flags);
	cpm_dpmem_info.alignment = align;
	start = rh_alloc(&cpm_dpmem_info, size, "commproc");
	spin_unlock_irqrestore(&cpm_dpmem_lock, flags);

	return start;
}
EXPORT_SYMBOL(cpm2_dpalloc);

int cpm2_dpfree(void *addr)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&cpm_dpmem_lock, flags);
	ret = rh_free(&cpm_dpmem_info, addr);
	spin_unlock_irqrestore(&cpm_dpmem_lock, flags);

	return ret;
}
EXPORT_SYMBOL(cpm2_dpfree);

/* not sure if this is ever needed */
void *cpm2_dpalloc_fixed(void *addr, uint size, uint align)
{
	void *start;
	unsigned long flags;

	spin_lock_irqsave(&cpm_dpmem_lock, flags);
	cpm_dpmem_info.alignment = align;
	start = rh_alloc_fixed(&cpm_dpmem_info, addr, size, "commproc");
	spin_unlock_irqrestore(&cpm_dpmem_lock, flags);

	return start;
}
EXPORT_SYMBOL(cpm2_dpalloc_fixed);

void cpm2_dpdump(void)
{
	rh_dump(&cpm_dpmem_info);
}
EXPORT_SYMBOL(cpm2_dpdump);

uint cpm2_dpram_offset(void *addr)
{
	return (uint)((u_char *)addr -
			((uint)((cpm2_map_t *)CPM_MAP_ADDR)->im_dprambase));
}
EXPORT_SYMBOL(cpm2_dpram_offset);

void *cpm2_dpram_addr(int offset)
{
	return (void *)&((cpm2_map_t *)CPM_MAP_ADDR)->im_dprambase[offset];
}
EXPORT_SYMBOL(cpm2_dpram_addr);
