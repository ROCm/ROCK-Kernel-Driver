/*

  ne2k_cbus.c: A driver for the NE2000 like ethernet on NEC PC-9800.

	This is a copy of the 2.5.66 Linux ISA NE2000 driver "ne.c" 
	(Donald Becker/Paul Gortmaker) with the NEC PC-9800 specific
	changes added by Osamu Tomita. 

From ne.c:
-----------
    Copyright 1993 United States Government as represented by the
    Director, National Security Agency.

    This software may be used and distributed according to the terms
    of the GNU General Public License, incorporated herein by reference.
-----------

*/

/* Routines for the NatSemi-based designs (NE[12]000). */

static const char version[] =
"ne2k_cbus.c:v1.0 3/24/03 Osamu Tomita\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/isapnp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <asm/system.h>
#include <asm/io.h>

#include "8390.h"

/* Some defines that people can play with if so inclined. */

/* Do we support clones that don't adhere to 14,15 of the SAprom ? */
#define SUPPORT_NE_BAD_CLONES

/* Do we perform extra sanity checks on stuff ? */
/* #define NE_SANITY_CHECK */

/* Do we implement the read before write bugfix ? */
/* #define NE_RW_BUGFIX */

/* Do we have a non std. amount of memory? (in units of 256 byte pages) */
/* #define PACKETBUF_MEMSIZE	0x40 */

#ifdef SUPPORT_NE_BAD_CLONES
/* A list of bad clones that we none-the-less recognize. */
static struct { const char *name8, *name16; unsigned char SAprefix[4];}
bad_clone_list[] __initdata = {
    {"LA/T-98?", "LA/T-98", {0x00, 0xa0, 0xb0}},	/* I/O Data */
    {"EGY-98?", "EGY-98", {0x00, 0x40, 0x26}},		/* Melco EGY98 */
    {"ICM?", "ICM-27xx-ET", {0x00, 0x80, 0xc8}},	/* ICM IF-27xx-ET */
    {"CNET-98/EL?", "CNET(98)E/L", {0x00, 0x80, 0x4C}},	/* Contec CNET-98/EL */
    {0,}
};
#endif

/* ---- No user-serviceable parts below ---- */

#define NE_BASE	 (dev->base_addr)
#define NE_CMD	 	EI_SHIFT(0x00)
#define NE_DATAPORT	EI_SHIFT(0x10)	/* NatSemi-defined port window offset. */
#define NE_RESET	EI_SHIFT(0x1f) /* Issue a read to reset, a write to clear. */
#define NE_IO_EXTENT	0x20

#define NE1SM_START_PG	0x20	/* First page of TX buffer */
#define NE1SM_STOP_PG 	0x40	/* Last page +1 of RX ring */
#define NESM_START_PG	0x40	/* First page of TX buffer */
#define NESM_STOP_PG	0x80	/* Last page +1 of RX ring */

#include "ne2k_cbus.h"

int ne_probe(struct net_device *dev);
static int ne_probe1(struct net_device *dev, int ioaddr);
static int ne_open(struct net_device *dev);
static int ne_close(struct net_device *dev);

static void ne_reset_8390(struct net_device *dev);
static void ne_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
			  int ring_page);
static void ne_block_input(struct net_device *dev, int count,
			  struct sk_buff *skb, int ring_offset);
static void ne_block_output(struct net_device *dev, const int count,
		const unsigned char *buf, const int start_page);


/*  Probe for various non-shared-memory ethercards.

   NEx000-clone boards have a Station Address PROM (SAPROM) in the packet
   buffer memory space.  NE2000 clones have 0x57,0x57 in bytes 0x0e,0x0f of
   the SAPROM, while other supposed NE2000 clones must be detected by their
   SA prefix.

   Reading the SAPROM from a word-wide card with the 8390 set in byte-wide
   mode results in doubled values, which can be detected and compensated for.

   The probe is also responsible for initializing the card and filling
   in the 'dev' and 'ei_status' structures.

   We use the minimum memory size for some ethercard product lines, iff we can't
   distinguish models.  You can increase the packet buffer size by setting
   PACKETBUF_MEMSIZE.  Reported Cabletron packet buffer locations are:
	E1010   starts at 0x100 and ends at 0x2000.
	E1010-x starts at 0x100 and ends at 0x8000. ("-x" means "more memory")
	E2010	 starts at 0x100 and ends at 0x4000.
	E2010-x starts at 0x100 and ends at 0xffff.  */

int __init ne_probe(struct net_device *dev)
{
	unsigned int base_addr = dev->base_addr;

	SET_MODULE_OWNER(dev);

	if (ei_debug > 2)
		printk(KERN_DEBUG "ne_probe(): entered.\n");

	/* If CONFIG_NET_CBUS,
	   we need dev->priv->reg_offset BEFORE to probe */
	if (ne2k_cbus_init(dev) != 0)
		return -ENOMEM;

	/* First check any supplied i/o locations. User knows best. <cough> */
	if (base_addr > 0) {
		int result;
		const struct ne2k_cbus_hwinfo *hw = ne2k_cbus_get_hwinfo((int)(dev->mem_start & NE2K_CBUS_HARDWARE_TYPE_MASK));

		if (ei_debug > 2)
			printk(KERN_DEBUG "ne_probe(): call ne_probe_cbus(base_addr=0x%x)\n", base_addr);

		result = ne_probe_cbus(dev, hw, base_addr);
		if (result != 0)
			ne2k_cbus_destroy(dev);

		return result;
	}

	if (ei_debug > 2)
		printk(KERN_DEBUG "ne_probe(): base_addr is not specified.\n");

#ifndef MODULE
	/* Last resort. The semi-risky C-Bus auto-probe. */
	if (ei_debug > 2)
		printk(KERN_DEBUG "ne_probe(): auto-probe start.\n");

	{
		const struct ne2k_cbus_hwinfo *hw = ne2k_cbus_get_hwinfo((int)(dev->mem_start & NE2K_CBUS_HARDWARE_TYPE_MASK));

		if (hw && hw->hwtype) {
			const unsigned short *plist;
			for (plist = hw->portlist; *plist; plist++)
				if (ne_probe_cbus(dev, hw, *plist) == 0)
					return 0;
		} else {
			for (hw = &ne2k_cbus_hwinfo_list[0]; hw->hwtype; hw++) {
				const unsigned short *plist;
				for (plist = hw->portlist; *plist; plist++)
					if (ne_probe_cbus(dev, hw, *plist) == 0)
						return 0;
			}
		}
	}
#endif

	ne2k_cbus_destroy(dev);

	return -ENODEV;
}

static int __init ne_probe_cbus(struct net_device *dev, const struct ne2k_cbus_hwinfo *hw, int ioaddr)
{
	if (ei_debug > 2)
		printk(KERN_DEBUG "ne_probe_cbus(): entered. (called from %p)\n",
		       __builtin_return_address(0));

	if (hw && hw->hwtype) {
		ne2k_cbus_set_hwtype(dev, hw, ioaddr);
		return ne_probe1(dev, ioaddr);
	} else {
		/* auto detect */

		printk(KERN_DEBUG "ne_probe_cbus(): try to determine hardware types.\n");
		for (hw = &ne2k_cbus_hwinfo_list[0]; hw->hwtype; hw++) {
			ne2k_cbus_set_hwtype(dev, hw, ioaddr);
			if (ne_probe1(dev, ioaddr) == 0)
				return 0;
		}
	}
	return -ENODEV;
}

static int __init ne_probe1(struct net_device *dev, int ioaddr)
{
	int i;
	unsigned char SA_prom[32];
	int wordlength = 2;
	const char *name = NULL;
	int start_page, stop_page;
	int neX000, bad_card;
	int reg0, ret;
	static unsigned version_printed;
	const struct ne2k_cbus_region *rlist;
	const struct ne2k_cbus_hwinfo *hw = ne2k_cbus_get_hwinfo((int)(dev->mem_start & NE2K_CBUS_HARDWARE_TYPE_MASK));
	struct ei_device *ei_local = (struct ei_device *)(dev->priv);

#ifdef CONFIG_NE2K_CBUS_CNET98EL
	if (hw->hwtype == NE2K_CBUS_HARDWARE_TYPE_CNET98EL) {
		outb_p(0, CONFIG_NE2K_CBUS_CNET98EL_IO_BASE);
		/* udelay(5000);	*/
		outb_p(1, CONFIG_NE2K_CBUS_CNET98EL_IO_BASE);
		/* udelay(5000);	*/
		outb_p((ioaddr & 0xf000) >> 8 | 0x08 | 0x01, CONFIG_NE2K_CBUS_CNET98EL_IO_BASE + 2);
		/* udelay(5000); */
	}
#endif

	for (rlist = hw->regionlist; rlist->range; rlist++)
		if (!request_region(ioaddr + rlist->start,
					rlist->range, dev->name)) {
			ret = -EBUSY;
			goto err_out;
		}

	reg0 = inb_p(ioaddr + EI_SHIFT(0));
	if (reg0 == 0xFF) {
		ret = -ENODEV;
		goto err_out;
	}

	/* Do a preliminary verification that we have a 8390. */
#ifdef CONFIG_NE2K_CBUS_CNET98EL
	if (hw->hwtype != NE2K_CBUS_HARDWARE_TYPE_CNET98EL)
#endif
	{
		int regd;
		outb_p(E8390_NODMA+E8390_PAGE1+E8390_STOP, ioaddr + E8390_CMD);
		regd = inb_p(ioaddr + EI_SHIFT(0x0d));
		outb_p(0xff, ioaddr + EI_SHIFT(0x0d));
		outb_p(E8390_NODMA+E8390_PAGE0, ioaddr + E8390_CMD);
		inb_p(ioaddr + EN0_COUNTER0); /* Clear the counter by reading. */
		if (inb_p(ioaddr + EN0_COUNTER0) != 0) {
			outb_p(reg0, ioaddr);
			outb_p(regd, ioaddr + EI_SHIFT(0x0d));	/* Restore the old values. */
			ret = -ENODEV;
			goto err_out;
		}
	}

	if (ei_debug  &&  version_printed++ == 0)
		printk(KERN_INFO "%s", version);

	printk(KERN_INFO "NE*000 ethercard probe at %#3x:", ioaddr);

	/* A user with a poor card that fails to ack the reset, or that
	   does not have a valid 0x57,0x57 signature can still use this
	   without having to recompile. Specifying an i/o address along
	   with an otherwise unused dev->mem_end value of "0xBAD" will
	   cause the driver to skip these parts of the probe. */

	bad_card = ((dev->base_addr != 0) && (dev->mem_end == 0xbad));

	/* Reset card. Who knows what dain-bramaged state it was left in. */

	{
		unsigned long reset_start_time = jiffies;

		/* derived from CNET98EL-patch for bad clones */
		outb_p(E8390_NODMA | E8390_STOP, ioaddr + E8390_CMD);

		/* DON'T change these to inb_p/outb_p or reset will fail on clones. */
		outb(inb(ioaddr + NE_RESET), ioaddr + NE_RESET);

		while ((inb_p(ioaddr + EN0_ISR) & ENISR_RESET) == 0)
		if (jiffies - reset_start_time > 2*HZ/100) {
			if (bad_card) {
				printk(" (warning: no reset ack)");
				break;
			} else {
				printk(" not found (no reset ack).\n");
				ret = -ENODEV;
				goto err_out;
			}
		}

		outb_p(0xff, ioaddr + EN0_ISR);		/* Ack all intr. */
	}

#ifdef CONFIG_NE2K_CBUS_CNET98EL
	if (hw->hwtype == NE2K_CBUS_HARDWARE_TYPE_CNET98EL) {
		static const char pat[32] ="AbcdeFghijKlmnoPqrstUvwxyZ789012";
		char buf[32];
		int maxwait = 200;

		if (ei_debug > 2)
			printk(" [CNET98EL-specific initialize...");
		outb_p(E8390_NODMA | E8390_STOP, ioaddr + E8390_CMD); /* 0x20|0x1 */
		i = inb(ioaddr);
		if ((i & ~0x2) != (0x20 | 0x01))
			return -ENODEV;
		if ((inb(ioaddr + 0x7) & 0x80) != 0x80)
			return -ENODEV;
		outb_p(E8390_RXOFF, ioaddr + EN0_RXCR); /* out(ioaddr+0xc, 0x20) */
		/* outb_p(ENDCFG_WTS|ENDCFG_FT1|ENDCFG_LS, ioaddr+EN0_DCFG); */
		outb_p(ENDCFG_WTS | 0x48, ioaddr + EN0_DCFG); /* 0x49 */
		outb_p(CNET98EL_START_PG, ioaddr + EN0_STARTPG);
		outb_p(CNET98EL_STOP_PG, ioaddr + EN0_STOPPG);
		if (ei_debug > 2)
			printk("memory check");
		for (i = 0; i < 65536; i += 1024) {
			if (ei_debug > 2)
				printk(" %04x", i);
			ne2k_cbus_writemem(dev, ioaddr, i, pat, 32);
			while (((inb(ioaddr + EN0_ISR) & ENISR_RDC) != ENISR_RDC) && --maxwait)
				;
			ne2k_cbus_readmem(dev, ioaddr, i, buf, 32);
			if (memcmp(pat, buf, 32)) {
				if (ei_debug > 2)
					printk(" failed.");
				break;
			}
		}
		if (i != 16384) {
			if (ei_debug > 2)
				printk("] ");
			printk("memory failure at %x\n", i);
			return -ENODEV;
		}
		if (ei_debug > 2)
			printk(" good...");
		if (!dev->irq) {
			if (ei_debug > 2)
				printk("] ");
			printk("IRQ must be specified for C-NET(98)E/L. probe failed.\n");
			return -ENODEV;
		}
		outb((dev->irq > 5) ? (dev->irq & 4):(dev->irq >> 1), ioaddr + (0x2 | 0x400));
		outb(0x7e, ioaddr + (0x4 | 0x400));
		ne2k_cbus_readmem(dev, ioaddr, 16384, SA_prom, 32);
		outb(0xff, ioaddr + EN0_ISR);
		if (ei_debug > 2)
			printk("done]");
	} else
#endif /* CONFIG_NE2K_CBUS_CNET98EL */
	/* Read the 16 bytes of station address PROM.
	   We must first initialize registers, similar to NS8390_init(eifdev, 0).
	   We can't reliably read the SAPROM address without this.
	   (I learned the hard way!). */
	{
		struct {unsigned char value; unsigned short offset;} program_seq[] = 
		{
			{E8390_NODMA+E8390_PAGE0+E8390_STOP, E8390_CMD}, /* Select page 0*/
			/* NEC PC-9800: some board can only handle word-wide access? */
			{0x48 | ENDCFG_WTS,	EN0_DCFG},	/* Set word-wide (0x48) access. */
			{16384 / 256, EN0_STARTPG},
			{32768 / 256, EN0_STOPPG},
			{0x00,	EN0_RCNTLO},	/* Clear the count regs. */
			{0x00,	EN0_RCNTHI},
			{0x00,	EN0_IMR},	/* Mask completion irq. */
			{0xFF,	EN0_ISR},
			{E8390_RXOFF, EN0_RXCR},	/* 0x20  Set to monitor */
			{E8390_TXOFF, EN0_TXCR},	/* 0x02  and loopback mode. */
			{32,	EN0_RCNTLO},
			{0x00,	EN0_RCNTHI},
			{0x00,	EN0_RSARLO},	/* DMA starting at 0x0000. */
			{0x00,	EN0_RSARHI},
			{E8390_RREAD+E8390_START, E8390_CMD},
		};

		for (i = 0; i < sizeof(program_seq)/sizeof(program_seq[0]); i++)
			outb_p(program_seq[i].value, ioaddr + program_seq[i].offset);
		insw(ioaddr + NE_DATAPORT, SA_prom, 32 >> 1);

	}

	if (wordlength == 2)
	{
		for (i = 0; i < 16; i++)
			SA_prom[i] = SA_prom[i+i];
		start_page = NESM_START_PG;
		stop_page = NESM_STOP_PG;
#ifdef CONFIG_NE2K_CBUS_CNET98EL
		if (hw->hwtype == NE2K_CBUS_HARDWARE_TYPE_CNET98EL) {
			start_page = CNET98EL_START_PG;
			stop_page = CNET98EL_STOP_PG;
		}
#endif
	} else {
		start_page = NE1SM_START_PG;
		stop_page = NE1SM_STOP_PG;
	}

	neX000 = (SA_prom[14] == 0x57  &&  SA_prom[15] == 0x57);
	if (neX000) {
		name = "C-Bus-NE2K-compat";
	}
	else
	{
#ifdef SUPPORT_NE_BAD_CLONES
		/* Ack!  Well, there might be a *bad* NE*000 clone there.
		   Check for total bogus addresses. */
		for (i = 0; bad_clone_list[i].name8; i++)
		{
			if (SA_prom[0] == bad_clone_list[i].SAprefix[0] &&
				SA_prom[1] == bad_clone_list[i].SAprefix[1] &&
				SA_prom[2] == bad_clone_list[i].SAprefix[2])
			{
				if (wordlength == 2)
				{
					name = bad_clone_list[i].name16;
				} else {
					name = bad_clone_list[i].name8;
				}
				break;
			}
		}
		if (bad_clone_list[i].name8 == NULL)
		{
			printk(" not found (invalid signature %2.2x %2.2x).\n",
				SA_prom[14], SA_prom[15]);
			ret = -ENXIO;
			goto err_out;
		}
#else
		printk(" not found.\n");
		ret = -ENXIO;
		goto err_out;
#endif
	}

	if (dev->irq < 2)
	{
		unsigned long cookie = probe_irq_on();
		outb_p(0x50, ioaddr + EN0_IMR);	/* Enable one interrupt. */
		outb_p(0x00, ioaddr + EN0_RCNTLO);
		outb_p(0x00, ioaddr + EN0_RCNTHI);
		outb_p(E8390_RREAD+E8390_START, ioaddr); /* Trigger it... */
		mdelay(10);		/* wait 10ms for interrupt to propagate */
		outb_p(0x00, ioaddr + EN0_IMR); 		/* Mask it again. */
		dev->irq = probe_irq_off(cookie);
		if (ei_debug > 2)
			printk(" autoirq is %d\n", dev->irq);
	} else if (dev->irq == 7)
		/* Fixup for users that don't know that IRQ 7 is really IRQ 11,
		   or don't know which one to set. */
		dev->irq = 11;

	if (! dev->irq) {
		printk(" failed to detect IRQ line.\n");
		ret = -EAGAIN;
		goto err_out;
	}

	/* Allocate dev->priv and fill in 8390 specific dev fields. */
	if (ethdev_init(dev))
	{
        	printk (" unable to get memory for dev->priv.\n");
        	ret = -ENOMEM;
		goto err_out;
	}

	/* Snarf the interrupt now.  There's no point in waiting since we cannot
	   share and the board will usually be enabled. */
	ret = request_irq(dev->irq, ei_interrupt, 0, name, dev);
	if (ret) {
		printk (" unable to get IRQ %d (errno=%d).\n", dev->irq, ret);
		goto err_out_kfree;
	}

	dev->base_addr = ioaddr;

	for(i = 0; i < ETHER_ADDR_LEN; i++) {
		printk(" %2.2x", SA_prom[i]);
		dev->dev_addr[i] = SA_prom[i];
	}

	printk("\n%s: %s found at %#x, hardware type %d(%s), using IRQ %d.\n",
		   dev->name, name, ioaddr, hw->hwtype, hw->hwident, dev->irq);

	ei_status.name = name;
	ei_status.tx_start_page = start_page;
	ei_status.stop_page = stop_page;
	ei_status.word16 = (wordlength == 2);

	ei_status.rx_start_page = start_page + TX_PAGES;
#ifdef PACKETBUF_MEMSIZE
	 /* Allow the packet buffer size to be overridden by know-it-alls. */
	ei_status.stop_page = ei_status.tx_start_page + PACKETBUF_MEMSIZE;
#endif

	ei_status.reset_8390 = &ne_reset_8390;
	ei_status.block_input = &ne_block_input;
	ei_status.block_output = &ne_block_output;
	ei_status.get_8390_hdr = &ne_get_8390_hdr;
	ei_status.priv = 0;
	dev->open = &ne_open;
	dev->stop = &ne_close;
	NS8390_init(dev, 0);
	return 0;

err_out_kfree:
	ne2k_cbus_destroy(dev);
err_out:
	while (rlist > hw->regionlist) {
		rlist --;
		release_region(ioaddr + rlist->start, rlist->range);
	}
	return ret;
}

static int ne_open(struct net_device *dev)
{
	ei_open(dev);
	return 0;
}

static int ne_close(struct net_device *dev)
{
	if (ei_debug > 1)
		printk(KERN_DEBUG "%s: Shutting down ethercard.\n", dev->name);
	ei_close(dev);
	return 0;
}

/* Hard reset the card.  This used to pause for the same period that a
   8390 reset command required, but that shouldn't be necessary. */

static void ne_reset_8390(struct net_device *dev)
{
	unsigned long reset_start_time = jiffies;
	struct ei_device *ei_local = (struct ei_device *)(dev->priv);

	if (ei_debug > 1)
		printk(KERN_DEBUG "resetting the 8390 t=%ld...", jiffies);

	/* derived from CNET98EL-patch for bad clones... */
	outb_p(E8390_NODMA | E8390_STOP, NE_BASE + E8390_CMD);  /* 0x20 | 0x1 */

	/* DON'T change these to inb_p/outb_p or reset will fail on clones. */
	outb(inb(NE_BASE + NE_RESET), NE_BASE + NE_RESET);

	ei_status.txing = 0;
	ei_status.dmaing = 0;

	/* This check _should_not_ be necessary, omit eventually. */
	while ((inb_p(NE_BASE+EN0_ISR) & ENISR_RESET) == 0)
		if (jiffies - reset_start_time > 2*HZ/100) {
			printk(KERN_WARNING "%s: ne_reset_8390() did not complete.\n", dev->name);
			break;
		}
	outb_p(ENISR_RESET, NE_BASE + EN0_ISR);	/* Ack intr. */
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void ne_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	int nic_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *)(dev->priv);

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */

	if (ei_status.dmaing)
	{
		printk(KERN_EMERG "%s: DMAing conflict in ne_get_8390_hdr "
			"[DMAstat:%d][irqlock:%d].\n",
			dev->name, ei_status.dmaing, ei_status.irqlock);
		return;
	}

	ei_status.dmaing |= 0x01;
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
	outb_p(sizeof(struct e8390_pkt_hdr), nic_base + EN0_RCNTLO);
	outb_p(0, nic_base + EN0_RCNTHI);
	outb_p(0, nic_base + EN0_RSARLO);		/* On page boundary */
	outb_p(ring_page, nic_base + EN0_RSARHI);
	outb_p(E8390_RREAD+E8390_START, nic_base + NE_CMD);

	if (ei_status.word16)
		insw(NE_BASE + NE_DATAPORT, hdr, sizeof(struct e8390_pkt_hdr)>>1);
	else
		insb(NE_BASE + NE_DATAPORT, hdr, sizeof(struct e8390_pkt_hdr));

	outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;

	le16_to_cpus(&hdr->count);
}

/* Block input and output, similar to the Crynwr packet driver.  If you
   are porting to a new ethercard, look at the packet driver source for hints.
   The NEx000 doesn't share the on-board packet memory -- you have to put
   the packet out through the "remote DMA" dataport using outb. */

static void ne_block_input(struct net_device *dev, int count, struct sk_buff *skb, int ring_offset)
{
#ifdef NE_SANITY_CHECK
	int xfer_count = count;
#endif
	int nic_base = dev->base_addr;
	char *buf = skb->data;
	struct ei_device *ei_local = (struct ei_device *)(dev->priv);

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */
	if (ei_status.dmaing)
	{
		printk(KERN_EMERG "%s: DMAing conflict in ne_block_input "
			"[DMAstat:%d][irqlock:%d].\n",
			dev->name, ei_status.dmaing, ei_status.irqlock);
		return;
	}
	ei_status.dmaing |= 0x01;

	/* round up count to a word (derived from ICM-patch) */
	count = (count + 1) & ~1;

	outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
	outb_p(count & 0xff, nic_base + EN0_RCNTLO);
	outb_p(count >> 8, nic_base + EN0_RCNTHI);
	outb_p(ring_offset & 0xff, nic_base + EN0_RSARLO);
	outb_p(ring_offset >> 8, nic_base + EN0_RSARHI);
	outb_p(E8390_RREAD+E8390_START, nic_base + NE_CMD);
	if (ei_status.word16)
	{
		insw(NE_BASE + NE_DATAPORT,buf,count>>1);
		if (count & 0x01)
		{
			buf[count-1] = inb(NE_BASE + NE_DATAPORT);
#ifdef NE_SANITY_CHECK
			xfer_count++;
#endif
		}
	} else {
		insb(NE_BASE + NE_DATAPORT, buf, count);
	}

#ifdef NE_SANITY_CHECK
	/* This was for the ALPHA version only, but enough people have
	   been encountering problems so it is still here.  If you see
	   this message you either 1) have a slightly incompatible clone
	   or 2) have noise/speed problems with your bus. */

	if (ei_debug > 1)
	{
		/* DMA termination address check... */
		int addr, tries = 20;
		do {
			/* DON'T check for 'inb_p(EN0_ISR) & ENISR_RDC' here
			   -- it's broken for Rx on some cards! */
			int high = inb_p(nic_base + EN0_RSARHI);
			int low = inb_p(nic_base + EN0_RSARLO);
			addr = (high << 8) + low;
			if (((ring_offset + xfer_count) & 0xff) == low)
				break;
		} while (--tries > 0);
	 	if (tries <= 0)
			printk(KERN_WARNING "%s: RX transfer address mismatch,"
				"%#4.4x (expected) vs. %#4.4x (actual).\n",
				dev->name, ring_offset + xfer_count, addr);
	}
#endif
	outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
}

static void ne_block_output(struct net_device *dev, int count,
		const unsigned char *buf, const int start_page)
{
	int nic_base = NE_BASE;
	unsigned long dma_start;
#ifdef NE_SANITY_CHECK
	int retries = 0;
#endif
	struct ei_device *ei_local = (struct ei_device *)(dev->priv);

	/* Round the count up for word writes.  Do we need to do this?
	   What effect will an odd byte count have on the 8390?
	   I should check someday. */

	if (ei_status.word16 && (count & 0x01))
		count++;

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */
	if (ei_status.dmaing)
	{
		printk(KERN_EMERG "%s: DMAing conflict in ne_block_output."
			"[DMAstat:%d][irqlock:%d]\n",
			dev->name, ei_status.dmaing, ei_status.irqlock);
		return;
	}
	ei_status.dmaing |= 0x01;
	/* We should already be in page 0, but to be safe... */
	outb_p(E8390_PAGE0+E8390_START+E8390_NODMA, nic_base + NE_CMD);

#ifdef NE_SANITY_CHECK
retry:
#endif

#ifdef NE8390_RW_BUGFIX
	/* Handle the read-before-write bug the same way as the
	   Crynwr packet driver -- the NatSemi method doesn't work.
	   Actually this doesn't always work either, but if you have
	   problems with your NEx000 this is better than nothing! */

	outb_p(0x42, nic_base + EN0_RCNTLO);
	outb_p(0x00,   nic_base + EN0_RCNTHI);
	outb_p(0x42, nic_base + EN0_RSARLO);
	outb_p(0x00, nic_base + EN0_RSARHI);
	outb_p(E8390_RREAD+E8390_START, nic_base + NE_CMD);
	/* Make certain that the dummy read has occurred. */
	udelay(6);
#endif

	outb_p(ENISR_RDC, nic_base + EN0_ISR);

	/* Now the normal output. */
	outb_p(count & 0xff, nic_base + EN0_RCNTLO);
	outb_p(count >> 8,   nic_base + EN0_RCNTHI);
	outb_p(0x00, nic_base + EN0_RSARLO);
	outb_p(start_page, nic_base + EN0_RSARHI);

	outb_p(E8390_RWRITE+E8390_START, nic_base + NE_CMD);
	if (ei_status.word16) {
		outsw(NE_BASE + NE_DATAPORT, buf, count>>1);
	} else {
		outsb(NE_BASE + NE_DATAPORT, buf, count);
	}

	dma_start = jiffies;

#ifdef NE_SANITY_CHECK
	/* This was for the ALPHA version only, but enough people have
	   been encountering problems so it is still here. */

	if (ei_debug > 1)
	{
		/* DMA termination address check... */
		int addr, tries = 20;
		do {
			int high = inb_p(nic_base + EN0_RSARHI);
			int low = inb_p(nic_base + EN0_RSARLO);
			addr = (high << 8) + low;
			if ((start_page << 8) + count == addr)
				break;
		} while (--tries > 0);

		if (tries <= 0)
		{
			printk(KERN_WARNING "%s: Tx packet transfer address mismatch,"
				"%#4.4x (expected) vs. %#4.4x (actual).\n",
				dev->name, (start_page << 8) + count, addr);
			if (retries++ == 0)
				goto retry;
		}
	}
#endif

	while ((inb_p(nic_base + EN0_ISR) & ENISR_RDC) == 0)
		if (jiffies - dma_start > 2*HZ/100) {		/* 20ms */
			printk(KERN_WARNING "%s: timeout waiting for Tx RDC.\n", dev->name);
			ne_reset_8390(dev);
			NS8390_init(dev,1);
			break;
		}

	outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
	return;
}


#ifdef MODULE
#define MAX_NE_CARDS	4	/* Max number of NE cards per module */
static struct net_device dev_ne[MAX_NE_CARDS];
static int io[MAX_NE_CARDS];
static int irq[MAX_NE_CARDS];
static int bad[MAX_NE_CARDS];	/* 0xbad = bad sig or no reset ack */
static int hwtype[MAX_NE_CARDS] = { 0, }; /* board type */

MODULE_PARM(io, "1-" __MODULE_STRING(MAX_NE_CARDS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(MAX_NE_CARDS) "i");
MODULE_PARM(bad, "1-" __MODULE_STRING(MAX_NE_CARDS) "i");
MODULE_PARM(hwtype, "1-" __MODULE_STRING(MAX_NE_CARDS) "i");
MODULE_PARM_DESC(io, "I/O base address(es),required");
MODULE_PARM_DESC(irq, "IRQ number(s)");
MODULE_PARM_DESC(bad, "Accept card(s) with bad signatures");
MODULE_PARM_DESC(hwtype, "Board type of PC-9800 C-Bus NIC");
MODULE_DESCRIPTION("NE1000/NE2000 PC-9800 C-bus Ethernet driver");
MODULE_LICENSE("GPL");

/* This is set up so that no ISA autoprobe takes place. We can't guarantee
that the ne2k probe is the last 8390 based probe to take place (as it
is at boot) and so the probe will get confused by any other 8390 cards.
ISA device autoprobes on a running machine are not recommended anyway. */

int init_module(void)
{
	int this_dev, found = 0;

	for (this_dev = 0; this_dev < MAX_NE_CARDS; this_dev++) {
		struct net_device *dev = &dev_ne[this_dev];
		dev->irq = irq[this_dev];
		dev->mem_end = bad[this_dev];
		dev->base_addr = io[this_dev];
		dev->mem_start = hwtype[this_dev];
		dev->init = ne_probe;
		if (register_netdev(dev) == 0) {
			found++;
			continue;
		}
		if (found != 0) { 	/* Got at least one. */
			return 0;
		}
		if (io[this_dev] != 0)
			printk(KERN_WARNING "ne2k_cbus: No NE*000 card found at i/o = %#x\n", io[this_dev]);
		else
			printk(KERN_NOTICE "ne2k_cbus: You must supply \"io=0xNNN\" value(s) for C-Bus cards.\n");
		return -ENXIO;
	}
	return 0;
}

void cleanup_module(void)
{
	int this_dev;

	for (this_dev = 0; this_dev < MAX_NE_CARDS; this_dev++) {
		struct net_device *dev = &dev_ne[this_dev];
		if (dev->priv != NULL) {
			const struct ne2k_cbus_region *rlist;
			const struct ne2k_cbus_hwinfo *hw = ne2k_cbus_get_hwinfo((int)(dev->mem_start & NE2K_CBUS_HARDWARE_TYPE_MASK));

			free_irq(dev->irq, dev);
			for (rlist = hw->regionlist; rlist->range; rlist++) {
				release_region(dev->base_addr + rlist->start,
						rlist->range);
			}
			unregister_netdev(dev);
			ne2k_cbus_destroy(dev);
		}
	}
}
#endif /* MODULE */

