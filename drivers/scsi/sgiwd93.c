/*
 * sgiwd93.c: SGI WD93 scsi driver.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *		 1999 Andrew R. Baker (andrewb@uab.edu)
 *		      - Support for 2nd SCSI controller on Indigo2
 *		 2001 Florian Lohoff (flo@rfc822.org)
 *		      - Delete HPC scatter gather (Read corruption on 
 *		        multiple disks)
 *		      - Cleanup wback cache handling
 * 
 * (In all truth, Jed Schimmel wrote all this code.)
 *
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/blkdev.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/spinlock.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sgialib.h>
#include <asm/sgi/sgi.h>
#include <asm/sgi/sgimc.h>
#include <asm/sgi/sgihpc.h>
#include <asm/sgi/sgint23.h>
#include <asm/irq.h>
#include <asm/io.h>

#include "scsi.h"
#include "hosts.h"
#include "wd33c93.h"
#include "sgiwd93.h"

#include <linux/stat.h>

struct hpc_chunk {
	struct hpc_dma_desc desc;
	u32 _padding;	/* align to quadword boundary */
};

struct Scsi_Host *sgiwd93_host = NULL;
struct Scsi_Host *sgiwd93_host1 = NULL;

/* Wuff wuff, wuff, wd33c93.c, wuff wuff, object oriented, bow wow. */

/* XXX woof! */
static void sgiwd93_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	struct Scsi_Host *dev = dev_id;
	
	spin_lock_irqsave(dev->host_lock, flags);
	wd33c93_intr((struct Scsi_Host *) dev_id);
	spin_unlock_irqrestore(dev->host_lock, flags);
}

#undef DEBUG_DMA

static inline
void fill_hpc_entries (struct hpc_chunk **hcp, char *addr, unsigned long len)
{
	unsigned long physaddr;
	unsigned long count;
	
	physaddr = PHYSADDR(addr);
	while (len) {
		/*
		 * even cntinfo could be up to 16383, without
		 * magic only 8192 works correctly
		 */
		count = len > 8192 ? 8192 : len;
		(*hcp)->desc.pbuf = physaddr;
		(*hcp)->desc.cntinfo = count;
		(*hcp)++;
		len -= count;
		physaddr += count;
	}
}

static int dma_setup(Scsi_Cmnd *cmd, int datainp)
{
	struct WD33C93_hostdata *hdata = (struct WD33C93_hostdata *)cmd->host->hostdata;
	struct hpc3_scsiregs *hregs = (struct hpc3_scsiregs *) cmd->host->base;
	struct hpc_chunk *hcp = (struct hpc_chunk *) hdata->dma_bounce_buffer;

#ifdef DEBUG_DMA
	printk("dma_setup: datainp<%d> hcp<%p> ",
	       datainp, hcp);
#endif

	hdata->dma_dir = datainp;

	/*
	 * wd33c93 shouldn't pass us bogus dma_setups, but
	 * it does:-( The other wd33c93 drivers deal with
	 * it the same way (which isn't that obvious).
	 * IMHO a better fix would be, not to do these
	 * dma setups in the first place
	 */
	if (cmd->SCp.ptr == NULL)
		return 1;

	fill_hpc_entries (&hcp, cmd->SCp.ptr,cmd->SCp.this_residual);

	/* To make sure, if we trip an HPC bug, that we transfer
	 * every single byte, we tag on an extra zero length dma
	 * descriptor at the end of the chain.
	 */
	hcp->desc.pbuf = 0;
	hcp->desc.cntinfo = (HPCDMA_EOX);

#ifdef DEBUG_DMA
	printk(" HPCGO\n");
#endif

	/* Start up the HPC. */
	hregs->ndptr = PHYSADDR(hdata->dma_bounce_buffer);
	if(datainp) {
		dma_cache_inv((unsigned long) cmd->SCp.ptr, cmd->SCp.this_residual);
		hregs->ctrl = (HPC3_SCTRL_ACTIVE);
	} else {
		dma_cache_wback_inv((unsigned long) cmd->SCp.ptr, cmd->SCp.this_residual);
		hregs->ctrl = (HPC3_SCTRL_ACTIVE | HPC3_SCTRL_DIR);
	}

	return 0;
}

static void dma_stop(struct Scsi_Host *instance, Scsi_Cmnd *SCpnt,
		     int status)
{
	struct WD33C93_hostdata *hdata = (struct WD33C93_hostdata *)instance->hostdata;
	struct hpc3_scsiregs *hregs;

	if (!SCpnt)
		return;

	hregs = (struct hpc3_scsiregs *) SCpnt->host->base;

#ifdef DEBUG_DMA
	printk("dma_stop: status<%d> ", status);
#endif

	/* First stop the HPC and flush it's FIFO. */
	if(hdata->dma_dir) {
		hregs->ctrl |= HPC3_SCTRL_FLUSH;
		while(hregs->ctrl & HPC3_SCTRL_ACTIVE)
			barrier();
	}
	hregs->ctrl = 0;

#ifdef DEBUG_DMA
	printk("\n");
#endif
}

void sgiwd93_reset(unsigned long base)
{
	struct hpc3_scsiregs *hregs = (struct hpc3_scsiregs *) base;

	hregs->ctrl = HPC3_SCTRL_CRESET;
	udelay (50);
	hregs->ctrl = 0;
}

static inline void init_hpc_chain(uchar *buf)
{
	struct hpc_chunk *hcp = (struct hpc_chunk *) buf;
	unsigned long start, end;

	start = (unsigned long) buf;
	end = start + PAGE_SIZE;
	while(start < end) {
		hcp->desc.pnext = PHYSADDR((hcp + 1));
		hcp->desc.cntinfo = HPCDMA_EOX;
		hcp++;
		start += sizeof(struct hpc_chunk);
	};
	hcp--;
	hcp->desc.pnext = PHYSADDR(buf);

	/* Force flush to memory */
	dma_cache_wback_inv((unsigned long) buf, PAGE_SIZE);
}

int __init sgiwd93_detect(Scsi_Host_Template *SGIblows)
{
	static unsigned char called = 0;
	struct hpc3_scsiregs *hregs = &hpc3c0->scsi_chan0;
	struct hpc3_scsiregs *hregs1 = &hpc3c0->scsi_chan1;
	struct WD33C93_hostdata *hdata;
	struct WD33C93_hostdata *hdata1;
	wd33c93_regs regs;
	uchar *buf;
	
	if(called)
		return 0; /* Should bitch on the console about this... */

	SGIblows->proc_name = "SGIWD93";

	sgiwd93_host = scsi_register(SGIblows, sizeof(struct WD33C93_hostdata));
	if(sgiwd93_host == NULL)
		return 0;
	sgiwd93_host->base = (unsigned long) hregs;
	sgiwd93_host->irq = SGI_WD93_0_IRQ;

	buf = (uchar *) get_zeroed_page(GFP_KERNEL);
	if (!buf) {
		printk(KERN_WARNING "sgiwd93: Could not allocate memory for host0 buffer.\n");
		scsi_unregister(sgiwd93_host);
		return 0;
	}
	init_hpc_chain(buf);
	
	/* HPC_SCSI_REG0 | 0x03 | KSEG1 */
	regs.SASR = (unsigned char*) KSEG1ADDR (0x1fbc0003);
	regs.SCMD = (unsigned char*) KSEG1ADDR (0x1fbc0007);
	wd33c93_init(sgiwd93_host, regs, dma_setup, dma_stop, WD33C93_FS_16_20);

	hdata = (struct WD33C93_hostdata *)sgiwd93_host->hostdata;
	hdata->no_sync = 0;
	hdata->dma_bounce_buffer = (uchar *) (KSEG1ADDR(buf));

	if (request_irq(SGI_WD93_0_IRQ, sgiwd93_intr, 0, "SGI WD93", (void *) sgiwd93_host)) {
		printk(KERN_WARNING "sgiwd93: Could not register IRQ %d (for host 0).\n", SGI_WD93_0_IRQ);
		wd33c93_release();
		free_page((unsigned long)buf);
		scsi_unregister(sgiwd93_host);
		return 0;
	}
        /* set up second controller on the Indigo2 */
	if(!sgi_guiness) {
		sgiwd93_host1 = scsi_register(SGIblows, sizeof(struct WD33C93_hostdata));
		if(sgiwd93_host1 != NULL)
		{
			sgiwd93_host1->base = (unsigned long) hregs1;
			sgiwd93_host1->irq = SGI_WD93_1_IRQ;
	
			buf = (uchar *) get_zeroed_page(GFP_KERNEL);
			if (!buf) {
				printk(KERN_WARNING "sgiwd93: Could not allocate memory for host1 buffer.\n");
				scsi_unregister(sgiwd93_host1);
				called = 1;
				return 1; /* We registered host0 so return success*/
			}
			init_hpc_chain(buf);

			/* HPC_SCSI_REG1 | 0x03 | KSEG1 */
			regs.SASR = (unsigned char*) KSEG1ADDR(0x1fbc8003);
			regs.SCMD = (unsigned char*) KSEG1ADDR(0x1fbc8007);
			wd33c93_init(sgiwd93_host1, regs, dma_setup, dma_stop,
			             WD33C93_FS_16_20);
	
			hdata1 = (struct WD33C93_hostdata *)sgiwd93_host1->hostdata;
			hdata1->no_sync = 0;
			hdata1->dma_bounce_buffer = (uchar *) (KSEG1ADDR(buf));
	
			if (request_irq(SGI_WD93_1_IRQ, sgiwd93_intr, 0, "SGI WD93", (void *) sgiwd93_host1)) {
				printk(KERN_WARNING "sgiwd93: Could not allocate irq %d (for host1).\n", SGI_WD93_1_IRQ);
				wd33c93_release();
				free_page((unsigned long)buf);
				scsi_unregister(sgiwd93_host1);
				/* Fall through since host0 registered OK */
			}
		}
	}
	
	called = 1;

	return 1; /* Found one. */
}

int sgiwd93_release(struct Scsi_Host *instance)
{
	free_irq(SGI_WD93_0_IRQ, sgiwd93_intr);
	free_page(KSEG0ADDR(hdata->dma_bounce_buffer));
	wd33c93_release();
	if(!sgi_guiness) {
		free_irq(SGI_WD93_1_IRQ, sgiwd93_intr);
		free_page(KSEG0ADDR(hdata1->dma_bounce_buffer));
		wd33c93_release();
	}
	return 1;
}

static Scsi_Host_Template driver_template = {
	.proc_name	     = "SGIWD93",
	.name                = "SGI WD93",
	.detect              = sgiwd93_detect,
	.release             = sgiwd93_release,
	.queuecommand        = wd33c93_queuecommand,
	.abort               = wd33c93_abort,
	.reset               = wd33c93_reset,
	.can_queue           = CAN_QUEUE,
	.this_id             = 7,
	.sg_tablesize        = SG_ALL,
	.cmd_per_lun	     = CMD_PER_LUN,
	.use_clustering      = DISABLE_CLUSTERING,
};
#include "scsi_module.c"
