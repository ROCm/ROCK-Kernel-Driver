/*
 *  This file contains quirk handling code for PnP devices
 *  Some devices do not report all their resources, and need to have extra
 *  resources added. This is most easily accomplished at initialisation time
 *  when building up the resource structure for the first time.
 *
 *  Copyright (c) 2000 Peter Denison <peterd@pnd-pc.demon.co.uk>
 *
 *  Heavily based on PCI quirks handling which is
 *
 *  Copyright (c) 1999 Martin Mares <mj@ucw.cz>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>

#ifdef CONFIG_PNP_DEBUG
	#define DEBUG
#else
	#undef DEBUG
#endif

#include <linux/pnp.h>
#include "base.h"


static void quirk_awe32_resources(struct pnp_dev *dev)
{
	struct pnp_port *port, *port2, *port3;
	struct pnp_resources *res = dev->possible->dep;

	/*
	 * Unfortunately the isapnp_add_port_resource is too tightly bound
	 * into the PnP discovery sequence, and cannot be used. Link in the
	 * two extra ports (at offset 0x400 and 0x800 from the one given) by
	 * hand.
	 */
	for ( ; res ; res = res->dep ) {
		port2 = pnp_alloc(sizeof(struct pnp_port));
		if (!port2)
			return;
		port3 = pnp_alloc(sizeof(struct pnp_port));
		if (!port3) {
			kfree(port2);
			return;
		}
		port = res->port;
		memcpy(port2, port, sizeof(struct pnp_port));
		memcpy(port3, port, sizeof(struct pnp_port));
		port->next = port2;
		port2->next = port3;
		port2->min += 0x400;
		port2->max += 0x400;
		port3->min += 0x800;
		port3->max += 0x800;
	}
	printk(KERN_INFO "pnp: AWE32 quirk - adding two ports\n");
}

static void quirk_cmi8330_resources(struct pnp_dev *dev)
{
	struct pnp_resources *res = dev->possible->dep;

	for ( ; res ; res = res->dep ) {

		struct pnp_irq *irq;
		struct pnp_dma *dma;

		for( irq = res->irq; irq; irq = irq->next )	// Valid irqs are 5, 7, 10
			irq->map = 0x04A0;						// 0000 0100 1010 0000

		for( dma = res->dma; dma; dma = dma->next ) // Valid 8bit dma channels are 1,3
			if( ( dma->flags & IORESOURCE_DMA_TYPE_MASK ) == IORESOURCE_DMA_8BIT )
				dma->map = 0x000A;
	}
	printk(KERN_INFO "pnp: CMI8330 quirk - fixing interrupts and dma\n");
}

static void quirk_sb16audio_resources(struct pnp_dev *dev)
{
	struct pnp_port *port;
	struct pnp_resources *res = dev->possible->dep;
	int    changed = 0;

	/*
	 * The default range on the mpu port for these devices is 0x388-0x388.
	 * Here we increase that range so that two such cards can be
	 * auto-configured.
	 */

	for( ; res ; res = res->dep ) {
		port = res->port;
		if(!port)
			continue;
		port = port->next;
		if(!port)
			continue;
		port = port->next;
		if(!port)
			continue;
		if(port->min != port->max)
			continue;
		port->max += 0x70;
		changed = 1;
	}
	if(changed)
		printk(KERN_INFO "pnp: SB audio device quirk - increasing port range\n");
	return;
}

extern int pnp_allow_dma0;
static void quirk_opl3sax_resources(struct pnp_dev *dev)
{
	/* This really isn't a device quirk but isapnp core code
	 * doesn't allow a DMA channel of 0, afflicted card is an
	 * OPL3Sax where x=4.
	 */
	struct pnp_resources *res;
	int max;
	res = dev->possible;
	max = 0;
	for (res = res->dep; res; res = res->dep) {
		if (res->dma->map > max)
			max = res->dma->map;
	}
	if (max == 1 && pnp_allow_dma0 == -1) {
		printk(KERN_INFO "pnp: opl3sa4 quirk: Allowing dma 0.\n");
		pnp_allow_dma0 = 1;
	}
	return;
}

/*
 *  PnP Quirks
 *  Cards or devices that need some tweaking due to incomplete resource info
 */

static struct pnp_fixup pnp_fixups[] = {
	/* Soundblaster awe io port quirk */
	{ "CTL0021", quirk_awe32_resources },
	{ "CTL0022", quirk_awe32_resources },
	{ "CTL0023", quirk_awe32_resources },
	/* CMI 8330 interrupt and dma fix */
	{ "@X@0001", quirk_cmi8330_resources },
	/* Soundblaster audio device io port range quirk */
	{ "CTL0001", quirk_sb16audio_resources },
	{ "CTL0031", quirk_sb16audio_resources },
	{ "CTL0041", quirk_sb16audio_resources },
	{ "CTL0042", quirk_sb16audio_resources },
	{ "CTL0043", quirk_sb16audio_resources },
	{ "CTL0044", quirk_sb16audio_resources },
	{ "CTL0045", quirk_sb16audio_resources },
	{ "YMH0021", quirk_opl3sax_resources },
	{ "" }
};

void pnp_fixup_device(struct pnp_dev *dev)
{
	int i = 0;

	while (*pnp_fixups[i].id) {
		if (compare_pnp_id(dev->id,pnp_fixups[i].id)) {
			pnp_dbg("Calling quirk for %s",
		                  dev->dev.bus_id);
			pnp_fixups[i].quirk_function(dev);
		}
		i++;
	}
}

