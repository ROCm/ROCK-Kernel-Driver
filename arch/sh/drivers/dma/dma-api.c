/*
 * arch/sh/drivers/dma/dma-api.c
 *
 * SuperH-specific DMA management API
 *
 * Copyright (C) 2003  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */ 
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <asm/dma.h>

struct dma_info dma_info[MAX_DMA_CHANNELS] = { { 0, } };
spinlock_t dma_spin_lock = SPIN_LOCK_UNLOCKED;

/* 
 * A brief note about the reasons for this API as it stands.
 *
 * For starters, the old ISA DMA API didn't work for us for a number of
 * reasons, for one, the vast majority of channels on the SH DMAC are
 * dual-address mode only, and both the new and the old DMA APIs are after the
 * concept of managing a DMA buffer, which doesn't overly fit this model very
 * well. In addition to which, the new API is largely geared at IOMMUs and
 * GARTs, and doesn't even support the channel notion very well.
 *
 * The other thing that's a marginal issue, is the sheer number of random DMA
 * engines that are present (ie, in boards like the Dreamcast), some of which
 * cascade off of the SH DMAC, and others do not. As such, there was a real
 * need for a scalable subsystem that could deal with both single and
 * dual-address mode usage, in addition to interoperating with cascaded DMACs.
 *
 * There really isn't any reason why this needs to be SH specific, though I'm
 * not aware of too many other processors (with the exception of some MIPS)
 * that have the same concept of a dual address mode, or any real desire to
 * actually make use of the DMAC even if such a subsystem were exposed
 * elsewhere.
 *
 * The idea for this was derived from the ARM port, which acted as an excellent
 * reference when trying to address these issues.
 *
 * It should also be noted that the decision to add Yet Another DMA API(tm) to
 * the kernel wasn't made easily, and was only decided upon after conferring
 * with jejb with regards to the state of the old and new APIs as they applied
 * to these circumstances. Philip Blundell was also a great help in figuring
 * out some single-address mode DMA semantics that were otherwise rather
 * confusing.
 */

struct dma_info *get_dma_info(unsigned int chan)
{
	return dma_info + chan;
}

int get_dma_residue(unsigned int chan)
{
	struct dma_info *info = get_dma_info(chan);

	if (info->ops->get_residue)
		return info->ops->get_residue(info);
	
	return 0;
}

int request_dma(unsigned int chan, const char *dev_id)
{
	struct dma_info *info = get_dma_info(chan);

	down(&info->sem);

	if (!info->ops || chan >= MAX_DMA_CHANNELS) {
		up(&info->sem);
		return -EINVAL;
	}
	
	atomic_set(&info->busy, 1);

	info->dev_id = dev_id;

	up(&info->sem);

	if (info->ops->request)
		return info->ops->request(info);
	
	return 0;
}

void free_dma(unsigned int chan)
{
	struct dma_info *info = get_dma_info(chan);

	if (info->ops->free)
		info->ops->free(info);
	
	atomic_set(&info->busy, 0);
}

void dma_wait_for_completion(unsigned int chan)
{
	struct dma_info *info = get_dma_info(chan);

	while (info->ops->get_residue(info))
		cpu_relax();
}

void dma_configure_channel(unsigned int chan, unsigned long flags)
{
	struct dma_info *info = get_dma_info(chan);

	if (info->ops->configure)
		info->ops->configure(info, flags);
}

int dma_xfer(unsigned int chan, unsigned long from,
	     unsigned long to, size_t size, unsigned int mode)
{
	struct dma_info *info = get_dma_info(chan);

	info->sar	= from;
	info->dar	= to;
	info->count	= size;
	info->mode	= mode;

	return info->ops->xfer(info);
}

#ifdef CONFIG_PROC_FS
static int dma_read_proc(char *buf, char **start, off_t off,
			 int len, int *eof, void *data)
{
	struct dma_info *info;
	char *p = buf;
	int i;

	for (i = 0, info = dma_info; i < MAX_DMA_CHANNELS; i++, info++) {
		if (!atomic_read(&info->busy))
			continue;

		p += sprintf(p, "%2d: %14s    %s\n", i,
			     info->ops->name, info->dev_id);
	}

	return p - buf;
}
#endif

int __init register_dmac(struct dma_ops *ops)
{
	int i;

	printk("DMA: Registering %s handler.\n", ops->name);

	for (i = 0; i < MAX_DMA_CHANNELS; i++) {
		struct dma_info *info = get_dma_info(i);

		info->chan = i;

		init_MUTEX(&info->sem);
	}

	return 0;
}

static int __init dma_api_init(void)
{
	printk("DMA: Registering DMA API.\n");

#ifdef CONFIG_PROC_FS
	create_proc_read_entry("dma", 0, 0, dma_read_proc, 0);
#endif

	return 0;
}

subsys_initcall(dma_api_init);

MODULE_AUTHOR("Paul Mundt <lethal@linux-sh.org>");
MODULE_DESCRIPTION("DMA API for SuperH");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(request_dma);
EXPORT_SYMBOL(free_dma);
EXPORT_SYMBOL(get_dma_residue);
EXPORT_SYMBOL(get_dma_info);
EXPORT_SYMBOL(dma_xfer);
EXPORT_SYMBOL(dma_wait_for_completion);
EXPORT_SYMBOL(dma_configure_channel);

