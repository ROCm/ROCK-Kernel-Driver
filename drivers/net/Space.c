/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Holds initial configuration information for devices.
 *
 * Version:	@(#)Space.c	1.0.7	08/12/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald J. Becker, <becker@scyld.com>
 *
 * Changelog:
 *		Stephen Hemminger (09/2003)
 *		- get rid of pre-linked dev list, dynamic device allocation
 *		Paul Gortmaker (03/2002)
 *		- struct init cleanup, enable multiple ISA autoprobes.
 *		Arnaldo Carvalho de Melo <acme@conectiva.com.br> - 09/1999
 *		- fix sbni: s/device/net_device/
 *		Paul Gortmaker (06/98): 
 *		 - sort probes in a sane way, make sure all (safe) probes
 *		   get run once & failed autoprobes don't autoprobe again.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/trdevice.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netlink.h>
#include <linux/divert.h>

/* A unified ethernet device probe.  This is the easiest way to have every
   ethernet adaptor have the name "eth[0123...]".
   */

extern int ne2_probe(struct net_device *dev);
extern int hp100_probe(struct net_device *dev);
extern int ultra_probe(struct net_device *dev);
extern int ultra32_probe(struct net_device *dev);
extern int wd_probe(struct net_device *dev);
extern int el2_probe(struct net_device *dev);
extern int ne_probe(struct net_device *dev);
extern int hp_probe(struct net_device *dev);
extern int hp_plus_probe(struct net_device *dev);
extern int express_probe(struct net_device *);
extern int eepro_probe(struct net_device *);
extern int at1500_probe(struct net_device *);
extern int at1700_probe(struct net_device *);
extern int fmv18x_probe(struct net_device *);
extern int eth16i_probe(struct net_device *);
extern int i82596_probe(struct net_device *);
extern int ewrk3_probe(struct net_device *);
extern int el1_probe(struct net_device *);
extern int wavelan_probe(struct net_device *);
extern int arlan_probe(struct net_device *);
extern int el16_probe(struct net_device *);
extern int elmc_probe(struct net_device *);
extern int skmca_probe(struct net_device *);
extern int elplus_probe(struct net_device *);
extern int ac3200_probe(struct net_device *);
extern int es_probe(struct net_device *);
extern int lne390_probe(struct net_device *);
extern int e2100_probe(struct net_device *);
extern int ni5010_probe(struct net_device *);
extern int ni52_probe(struct net_device *);
extern int ni65_probe(struct net_device *);
extern int sonic_probe(struct net_device *);
extern int SK_init(struct net_device *);
extern int seeq8005_probe(struct net_device *);
extern int smc_init( struct net_device * );
extern int atarilance_probe(struct net_device *);
extern int sun3lance_probe(struct net_device *);
extern int sun3_82586_probe(struct net_device *);
extern int apne_probe(struct net_device *);
extern int bionet_probe(struct net_device *);
extern int pamsnet_probe(struct net_device *);
extern int cs89x0_probe(struct net_device *dev);
extern int hplance_probe(struct net_device *dev);
extern int bagetlance_probe(struct net_device *);
extern int mvme147lance_probe(struct net_device *dev);
extern int tc515_probe(struct net_device *dev);
extern int lance_probe(struct net_device *dev);
extern int mace_probe(struct net_device *dev);
extern int macsonic_probe(struct net_device *dev);
extern int mac8390_probe(struct net_device *dev);
extern int mac89x0_probe(struct net_device *dev);
extern int mc32_probe(struct net_device *dev);
extern struct net_device *cops_probe(int unit);
extern struct net_device *ltpc_probe(void);
  
/* Detachable devices ("pocket adaptors") */
extern int de620_probe(struct net_device *);

/* Fibre Channel adapters */
extern int iph5526_probe(struct net_device *dev);

/* SBNI adapters */
extern int sbni_probe(int unit);

struct devprobe
{
	int (*probe)(struct net_device *dev);
	int status;	/* non-zero if autoprobe has failed */
};

/*
 * probe_list walks a list of probe functions and calls each so long
 * as a non-zero ioaddr is given, or as long as it hasn't already failed 
 * to find a card in the past (as recorded by "status") when asked to
 * autoprobe (i.e. a probe that fails to find a card when autoprobing
 * will not be asked to autoprobe again).  It exits when a card is found.
 */
static int __init probe_list(struct net_device *dev, struct devprobe *plist)
{
	struct devprobe *p = plist;
	unsigned long base_addr = dev->base_addr;

	while (p->probe != NULL) {
		if (base_addr && p->probe(dev) == 0) 	/* probe given addr */
			return 0;
		else if (p->status == 0) {		/* has autoprobe failed yet? */
			p->status = p->probe(dev);	/* no, try autoprobe */
			if (p->status == 0)
				return 0;
		}
		p++;
	}
	return -ENODEV;
}

/*
 * This is a bit of an artificial separation as there are PCI drivers
 * that also probe for EISA cards (in the PCI group) and there are ISA
 * drivers that probe for EISA cards (in the ISA group).  These are the
 * legacy EISA only driver probes, and also the legacy PCI probes
 */
static struct devprobe eisa_probes[] __initdata = {
#ifdef CONFIG_ULTRA32 
	{ultra32_probe, 0},	
#endif
#ifdef CONFIG_AC3200	
	{ac3200_probe, 0},
#endif
#ifdef CONFIG_ES3210
	{es_probe, 0},
#endif
#ifdef CONFIG_LNE390
	{lne390_probe, 0},
#endif
	{NULL, 0},
};


static struct devprobe mca_probes[] __initdata = {
#ifdef CONFIG_NE2_MCA
	{ne2_probe, 0},
#endif
#ifdef CONFIG_ELMC		/* 3c523 */
	{elmc_probe, 0},
#endif
#ifdef CONFIG_ELMC_II		/* 3c527 */
	{mc32_probe, 0},
#endif
#ifdef CONFIG_SKMC              /* SKnet Microchannel */
        {skmca_probe, 0},
#endif
	{NULL, 0},
};

/*
 * ISA probes that touch addresses < 0x400 (including those that also
 * look for EISA/PCI/MCA cards in addition to ISA cards).
 */
static struct devprobe isa_probes[] __initdata = {
#ifdef CONFIG_HP100 		/* ISA, EISA & PCI */
	{hp100_probe, 0},
#endif	
#ifdef CONFIG_3C515
	{tc515_probe, 0},
#endif
#ifdef CONFIG_ULTRA 
	{ultra_probe, 0},
#endif
#ifdef CONFIG_WD80x3 
	{wd_probe, 0},
#endif
#ifdef CONFIG_EL2 		/* 3c503 */
	{el2_probe, 0},
#endif
#ifdef CONFIG_HPLAN
	{hp_probe, 0},
#endif
#ifdef CONFIG_HPLAN_PLUS
	{hp_plus_probe, 0},
#endif
#ifdef CONFIG_E2100		/* Cabletron E21xx series. */
	{e2100_probe, 0},
#endif
#if defined(CONFIG_NE2000) || defined(CONFIG_NE2K_CBUS)	/* ISA & PC-9800 CBUS (use ne2k-pci for PCI cards) */
	{ne_probe, 0},
#endif
#ifdef CONFIG_LANCE		/* ISA/VLB (use pcnet32 for PCI cards) */
	{lance_probe, 0},
#endif
#ifdef CONFIG_SMC9194
	{smc_init, 0},
#endif
#ifdef CONFIG_SEEQ8005 
	{seeq8005_probe, 0},
#endif
#ifdef CONFIG_AT1500
	{at1500_probe, 0},
#endif
#ifdef CONFIG_CS89x0
 	{cs89x0_probe, 0},
#endif
#ifdef CONFIG_AT1700
	{at1700_probe, 0},
#endif
#ifdef CONFIG_FMV18X		/* Fujitsu FMV-181/182 */
	{fmv18x_probe, 0},
#endif
#ifdef CONFIG_ETH16I
	{eth16i_probe, 0},	/* ICL EtherTeam 16i/32 */
#endif
#ifdef CONFIG_EEXPRESS		/* Intel EtherExpress */
	{express_probe, 0},
#endif
#ifdef CONFIG_EEXPRESS_PRO	/* Intel EtherExpress Pro/10 */
	{eepro_probe, 0},
#endif
#ifdef CONFIG_EWRK3             /* DEC EtherWORKS 3 */
    	{ewrk3_probe, 0},
#endif
#if defined(CONFIG_APRICOT) || defined(CONFIG_MVME16x_NET) || defined(CONFIG_BVME6000_NET)	/* Intel I82596 */
	{i82596_probe, 0},
#endif
#ifdef CONFIG_EL1		/* 3c501 */
	{el1_probe, 0},
#endif
#ifdef CONFIG_WAVELAN		/* WaveLAN */
	{wavelan_probe, 0},
#endif
#ifdef CONFIG_ARLAN		/* Aironet */
	{arlan_probe, 0},
#endif
#ifdef CONFIG_EL16		/* 3c507 */
	{el16_probe, 0},
#endif
#ifdef CONFIG_ELPLUS		/* 3c505 */
	{elplus_probe, 0},
#endif
#ifdef CONFIG_SK_G16
	{SK_init, 0},
#endif
#ifdef CONFIG_NI5010
	{ni5010_probe, 0},
#endif
#ifdef CONFIG_NI52
	{ni52_probe, 0},
#endif
#ifdef CONFIG_NI65
	{ni65_probe, 0},
#endif
	{NULL, 0},
};

static struct devprobe parport_probes[] __initdata = {
#ifdef CONFIG_DE620		/* D-Link DE-620 adapter */
	{de620_probe, 0},
#endif
	{NULL, 0},
};

static struct devprobe m68k_probes[] __initdata = {
#ifdef CONFIG_ATARILANCE	/* Lance-based Atari ethernet boards */
	{atarilance_probe, 0},
#endif
#ifdef CONFIG_SUN3LANCE         /* sun3 onboard Lance chip */
	{sun3lance_probe, 0},
#endif
#ifdef CONFIG_SUN3_82586        /* sun3 onboard Intel 82586 chip */
	{sun3_82586_probe, 0},
#endif
#ifdef CONFIG_APNE		/* A1200 PCMCIA NE2000 */
	{apne_probe, 0},
#endif
#ifdef CONFIG_ATARI_BIONET	/* Atari Bionet Ethernet board */
	{bionet_probe, 0},
#endif
#ifdef CONFIG_ATARI_PAMSNET	/* Atari PAMsNet Ethernet board */
	{pamsnet_probe, 0},
#endif
#ifdef CONFIG_HPLANCE		/* HP300 internal Ethernet */
	{hplance_probe, 0},
#endif
#ifdef CONFIG_MVME147_NET	/* MVME147 internal Ethernet */
	{mvme147lance_probe, 0},
#endif
#ifdef CONFIG_MACMACE		/* Mac 68k Quadra AV builtin Ethernet */
	{mace_probe, 0},
#endif
#ifdef CONFIG_MACSONIC		/* Mac SONIC-based Ethernet of all sorts */ 
	{macsonic_probe, 0},
#endif
#ifdef CONFIG_MAC8390           /* NuBus NS8390-based cards */
	{mac8390_probe, 0},
#endif
#ifdef CONFIG_MAC89x0
 	{mac89x0_probe, 0},
#endif
	{NULL, 0},
};

static struct devprobe mips_probes[] __initdata = {
#ifdef CONFIG_MIPS_JAZZ_SONIC
	{sonic_probe, 0},
#endif
#ifdef CONFIG_BAGETLANCE        /* Lance-based Baget ethernet boards */
        {bagetlance_probe, 0},
#endif
	{NULL, 0},
};

/*
 * Unified ethernet device probe, segmented per architecture and
 * per bus interface. This drives the legacy devices only for now.
 */
 
static int __init ethif_probe(int unit)
{
	struct net_device *dev;
	int err = -ENODEV;

	dev = alloc_etherdev(0);
	if (!dev)
		return -ENOMEM;

	sprintf(dev->name, "eth%d", unit);
	netdev_boot_setup_check(dev);

	/* 
	 * Backwards compatibility - historically an I/O base of 1 was 
	 * used to indicate not to probe for this ethN interface 
	 */
	if (dev->base_addr == 1) {
		free_netdev(dev);
		return -ENXIO;
	}

	/* 
	 * The arch specific probes are 1st so that any on-board ethernet
	 * will be probed before other ISA/EISA/MCA/PCI bus cards.
	 */
	if (probe_list(dev, m68k_probes) == 0 ||
	    probe_list(dev, mips_probes) == 0 ||
	    probe_list(dev, eisa_probes) == 0 ||
	    probe_list(dev, mca_probes) == 0 ||
	    probe_list(dev, isa_probes) == 0 ||
	    probe_list(dev, parport_probes) == 0) 
		err = register_netdev(dev);

	if (err)
		free_netdev(dev);
	return err;

}

#ifdef CONFIG_TR
/* Token-ring device probe */
extern int ibmtr_probe(struct net_device *);
extern int sk_isa_probe(struct net_device *);
extern int proteon_probe(struct net_device *);
extern int smctr_probe(struct net_device *);

static __init int trif_probe(int unit)
{
	struct net_device *dev;
	int err = -ENODEV;
	
	dev = alloc_trdev(0);
	if (!dev)
		return -ENOMEM;

	sprintf(dev->name, "tr%d", unit);
	netdev_boot_setup_check(dev);
	if (
#ifdef CONFIG_IBMTR
	    ibmtr_probe(dev) == 0  ||
#endif
#ifdef CONFIG_SKISA
	    sk_isa_probe(dev) == 0 || 
#endif
#ifdef CONFIG_PROTEON
	    proteon_probe(dev) == 0 ||
#endif
#ifdef CONFIG_SMCTR
	    smctr_probe(dev) == 0 ||
#endif
	    0 ) 
		err = register_netdev(dev);
		
	if (err)
		free_netdev(dev);
	return err;

}
#endif

	
/*
 *	The loopback device is global so it can be directly referenced
 *	by the network code. Also, it must be first on device list.
 */
extern int loopback_init(void);

/*  Statically configured drivers -- order matters here. */
static int __init net_olddevs_init(void)
{
	int num;

	if (loopback_init()) {
		printk(KERN_ERR "Network loopback device setup failed\n");
	}

	
#ifdef CONFIG_SBNI
	for (num = 0; num < 8; ++num)
		sbni_probe(num);
#endif
#ifdef CONFIG_TR
	for (num = 0; num < 8; ++num)
		trif_probe(num);
#endif
	for (num = 0; num < 8; ++num)
		ethif_probe(num);

#ifdef CONFIG_COPS
	cops_probe(0);
	cops_probe(1);
	cops_probe(2);
#endif
#ifdef CONFIG_LTPC
	ltpc_probe();
#endif

	return 0;
}

device_initcall(net_olddevs_init);

/*
 * The @dev_base list is protected by @dev_base_lock and the rtln
 * semaphore.
 *
 * Pure readers hold dev_base_lock for reading.
 *
 * Writers must hold the rtnl semaphore while they loop through the
 * dev_base list, and hold dev_base_lock for writing when they do the
 * actual updates.  This allows pure readers to access the list even
 * while a writer is preparing to update it.
 *
 * To put it another way, dev_base_lock is held for writing only to
 * protect against pure readers; the rtnl semaphore provides the
 * protection against other writers.
 *
 * See, for example usages, register_netdevice() and
 * unregister_netdevice(), which must be called with the rtnl
 * semaphore held.
 */
struct net_device *dev_base;
rwlock_t dev_base_lock = RW_LOCK_UNLOCKED;

EXPORT_SYMBOL(dev_base);
EXPORT_SYMBOL(dev_base_lock);
