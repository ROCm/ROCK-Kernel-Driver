/* ne2k_cbus.h: 
   vender-specific information definition for NEC PC-9800
   C-bus Ethernet Cards
   Used in ne.c 

   (C)1998,1999 KITAGWA Takurou & Linux/98 project
*/

#include <linux/config.h>

#undef NE_RESET
#define NE_RESET EI_SHIFT(0x11) /* Issue a read to reset, a write to clear. */

#ifdef CONFIG_NE2K_CBUS_CNET98EL
#ifndef CONFIG_NE2K_CBUS_CNET98EL_IO_BASE
#warning CONFIG_NE2K_CBUS_CNET98EL_IO_BASE is not defined(config error?)
#warning use 0xaaed as default
#define CONFIG_NE2K_CBUS_CNET98EL_IO_BASE 0xaaed /* or 0x55ed */
#endif
#define CNET98EL_START_PG 0x00
#define CNET98EL_STOP_PG 0x40
#endif

/* Hardware type definition (derived from *BSD) */
#define NE2K_CBUS_HARDWARE_TYPE_MASK 0xff

/* 0: reserved for auto-detect */
/* 1: (not tested)
   Allied Telesis CentreCom LA-98-T */
#define NE2K_CBUS_HARDWARE_TYPE_ATLA98 1
/* 2: (not tested)
   ELECOM Laneed
   LD-BDN[123]A
   PLANET SMART COM 98 EN-2298-C
   MACNICA ME98 */
#define NE2K_CBUS_HARDWARE_TYPE_BDN 2
/* 3:
   Melco EGY-98
   Contec C-NET(98)E*A/L*A,C-NET(98)P */
#define NE2K_CBUS_HARDWARE_TYPE_EGY98 3
/* 4:
   Melco LGY-98,IND-SP,IND-SS
   MACNICA NE2098 */
#define NE2K_CBUS_HARDWARE_TYPE_LGY98 4
/* 5:
   ICM DT-ET-25,DT-ET-T5,IF-2766ET,IF-2771ET
   PLANET SMART COM 98 EN-2298-T,EN-2298P-T
   D-Link DE-298PT,DE-298PCAT
   ELECOM Laneed LD-98P */
#define NE2K_CBUS_HARDWARE_TYPE_ICM 5
/* 6: (reserved for SIC-98, which is not supported in this driver.) */
/* 7: (unused in *BSD?)
   <Original NE2000 compatible>
   <for PCI/PCMCIA cards>
*/
#define NE2K_CBUS_HARDWARE_TYPE_NE2K 7
/* 8:
   NEC PC-9801-108 */
#define NE2K_CBUS_HARDWARE_TYPE_NEC108 8
/* 9:
   I-O DATA LA-98,LA/T-98 */
#define NE2K_CBUS_HARDWARE_TYPE_IOLA98 9
/* 10: (reserved for C-NET(98), which is not supported in this driver.) */
/* 11:
   Contec C-NET(98)E,L */
#define NE2K_CBUS_HARDWARE_TYPE_CNET98EL 11

#define NE2K_CBUS_HARDWARE_TYPE_MAX 11

/* HARDWARE TYPE ID 12-31: reserved */

struct ne2k_cbus_offsetinfo {
	unsigned short skip;
	unsigned short offset8; /* +0x8 - +0xf */
	unsigned short offset10; /* +0x10 */
	unsigned short offset1f; /* +0x1f */
};

struct ne2k_cbus_region {
	unsigned short start;
	short range;
};

struct ne2k_cbus_hwinfo {
	const unsigned short hwtype;
	const unsigned char *hwident;
#ifndef MODULE
	const unsigned short *portlist;
#endif
	const struct ne2k_cbus_offsetinfo *offsetinfo;
	const struct ne2k_cbus_region *regionlist;
};

#ifdef CONFIG_NE2K_CBUS_ATLA98
#ifndef MODULE
static unsigned short atla98_portlist[] __initdata = {
	0xd0,
	0
};
#endif
#define atla98_offsetinfo ne2k_offsetinfo
#define atla98_regionlist ne2k_regionlist
#endif /* CONFIG_NE2K_CBUS_ATLA98 */

#ifdef CONFIG_NE2K_CBUS_BDN
#ifndef MODULE
static unsigned short bdn_portlist[] __initdata = {
	0xd0,
	0
};
#endif
static struct ne2k_cbus_offsetinfo bdn_offsetinfo __initdata = {
#if 0
	/* comes from FreeBSD(98) ed98.h */
	0x1000, 0x8000, 0x100, 0xc200 /* ??? */
#else
	/* comes from NetBSD/pc98 if_ne_isa.c */
	0x1000, 0x8000, 0x100, 0x7f00 /* ??? */
#endif
};
static struct ne2k_cbus_region bdn_regionlist[] __initdata = {
	{0x0, 1}, {0x1000, 1}, {0x2000, 1}, {0x3000,1},
	{0x4000, 1}, {0x5000, 1}, {0x6000, 1}, {0x7000, 1},
	{0x8000, 1}, {0x9000, 1}, {0xa000, 1}, {0xb000, 1},
	{0xc000, 1}, {0xd000, 1}, {0xe000, 1}, {0xf000, 1},
	{0x100, 1}, {0x7f00, 1},
	{0x0, 0}
};
#endif /* CONFIG_NE2K_CBUS_BDN */

#ifdef CONFIG_NE2K_CBUS_EGY98
#ifndef MODULE
static unsigned short egy98_portlist[] __initdata = {
	0xd0,
	0
};
#endif
static struct ne2k_cbus_offsetinfo egy98_offsetinfo __initdata = {
	0x02, 0x100, 0x200, 0x300
};
static struct ne2k_cbus_region egy98_regionlist[] __initdata = {
	{0x0, 1}, {0x2, 1}, {0x4, 1}, {0x6, 1},
	{0x8, 1}, {0xa, 1}, {0xc, 1}, {0xe, 1},
	{0x100, 1}, {0x102, 1}, {0x104, 1}, {0x106, 1},
	{0x108, 1}, {0x10a, 1}, {0x10c, 1}, {0x10e, 1},
	{0x200, 1}, {0x300, 1},
	{0x0, 0}
};
#endif /* CONFIG_NE2K_CBUS_EGY98 */

#ifdef CONFIG_NE2K_CBUS_LGY98
#ifndef MODULE
static unsigned short lgy98_portlist[] __initdata = {
	0xd0, 0x10d0, 0x20d0, 0x30d0, 0x40d0, 0x50d0, 0x60d0, 0x70d0,
	0
};
#endif
static struct ne2k_cbus_offsetinfo lgy98_offsetinfo __initdata = {
	0x01, 0x08, 0x200, 0x300
};
static struct ne2k_cbus_region lgy98_regionlist[] __initdata = {
	{0x0, 16}, {0x200, 1}, {0x300, 1},
	{0x0, 0}
};
#endif /* CONFIG_NE2K_CBUS_LGY98 */

#ifdef CONFIG_NE2K_CBUS_ICM
#ifndef MODULE
static unsigned short icm_portlist[] __initdata = {
	/* ICM */
	0x56d0,
	/* LD-98PT */
	0x46d0, 0x66d0, 0x76d0, 0x86d0, 0x96d0, 0xa6d0, 0xb6d0, 0xc6d0,
	0
};
#endif
static struct ne2k_cbus_offsetinfo icm_offsetinfo __initdata = {
	0x01, 0x08, 0x100, 0x10f
};
static struct ne2k_cbus_region icm_regionlist[] __initdata = {
	{0x0, 16}, {0x100, 16},
	{0x0, 0}
};
#endif /* CONFIG_NE2K_CBUS_ICM */

#if defined(CONFIG_NE2K_CBUS_NE2K) && !defined(MODULE)
static unsigned short ne2k_portlist[] __initdata = {
	0xd0, 0x300, 0x280, 0x320, 0x340, 0x360, 0x380,
	0
};
#endif
#if defined(CONFIG_NE2K_CBUS_NE2K) || defined(CONFIG_NE2K_CBUS_ATLA98)
static struct ne2k_cbus_offsetinfo ne2k_offsetinfo __initdata = {
	0x01, 0x08, 0x10, 0x1f
};
static struct ne2k_cbus_region ne2k_regionlist[] __initdata = {
	{0x0, 32},
	{0x0, 0}
};
#endif

#ifdef CONFIG_NE2K_CBUS_NEC108
#ifndef MODULE
static unsigned short nec108_portlist[] __initdata = {
	0x770, 0x2770, 0x4770, 0x6770,
	0
};
#endif
static struct ne2k_cbus_offsetinfo nec108_offsetinfo __initdata = {
	0x02, 0x1000, 0x888, 0x88a
};
static struct ne2k_cbus_region nec108_regionlist[] __initdata = {
	{0x0, 1}, {0x2, 1}, {0x4, 1}, {0x6, 1},
	{0x8, 1}, {0xa, 1}, {0xc, 1}, {0xe, 1},
	{0x1000, 1}, {0x1002, 1}, {0x1004, 1}, {0x1006, 1},
	{0x1008, 1}, {0x100a, 1}, {0x100c, 1}, {0x100e, 1},
	{0x888, 1}, {0x88a, 1}, {0x88c, 1}, {0x88e, 1},
	{0x0, 0}
};
#endif

#ifdef CONFIG_NE2K_CBUS_IOLA98
#ifndef MODULE
static unsigned short iola98_portlist[] __initdata = {
	0xd0, 0xd2, 0xd4, 0xd6, 0xd8, 0xda, 0xdc, 0xde,
	0
};
#endif
static struct ne2k_cbus_offsetinfo iola98_offsetinfo __initdata = {
	0x1000, 0x8000, 0x100, 0xf100
};
static struct ne2k_cbus_region iola98_regionlist[] __initdata = {
	{0x0, 1}, {0x1000, 1}, {0x2000, 1}, {0x3000, 1},
	{0x4000, 1}, {0x5000, 1}, {0x6000, 1}, {0x7000, 1},
	{0x8000, 1}, {0x9000, 1}, {0xa000, 1}, {0xb000, 1},
	{0xc000, 1}, {0xd000, 1}, {0xe000, 1}, {0xf000, 1},
	{0x100, 1}, {0xf100, 1},
	{0x0,0}
};
#endif /* CONFIG_NE2K_CBUS_IOLA98 */

#ifdef CONFIG_NE2K_CBUS_CNET98EL
#ifndef MODULE
static unsigned short cnet98el_portlist[] __initdata = {
	0x3d0, 0x13d0, 0x23d0, 0x33d0, 0x43d0, 0x53d0, 0x60d0, 0x70d0,
	0
};
#endif
static struct ne2k_cbus_offsetinfo cnet98el_offsetinfo __initdata = {
	0x01, 0x08, 0x40e, 0x400
};
static struct ne2k_cbus_region cnet98el_regionlist[] __initdata = {
	{0x0, 16}, {0x400, 16},
	{0x0, 0}
};
#endif


/* port information table (for ne.c initialize/probe process) */

static struct ne2k_cbus_hwinfo ne2k_cbus_hwinfo_list[] __initdata = {
#ifdef CONFIG_NE2K_CBUS_ATLA98
/* NOT TESTED */
	{
		NE2K_CBUS_HARDWARE_TYPE_ATLA98,
		"LA-98-T",
#ifndef MODULE
		atla98_portlist,
#endif
		&atla98_offsetinfo, atla98_regionlist
	},
#endif
#ifdef CONFIG_NE2K_CBUS_BDN
/* NOT TESTED */
	{
		NE2K_CBUS_HARDWARE_TYPE_BDN,
		"LD-BDN[123]A",
#ifndef MODULE
		bdn_portlist,
#endif
		&bdn_offsetinfo, bdn_regionlist
	},
#endif
#ifdef CONFIG_NE2K_CBUS_ICM
	{
		NE2K_CBUS_HARDWARE_TYPE_ICM,
		"IF-27xxET",
#ifndef MODULE
		icm_portlist,
#endif
		&icm_offsetinfo, icm_regionlist
	},
#endif
#ifdef CONFIG_NE2K_CBUS_NE2K
	{
		NE2K_CBUS_HARDWARE_TYPE_NE2K,
		"NE2000 compat.",
#ifndef MODULE
		ne2k_portlist,
#endif
		&ne2k_offsetinfo, ne2k_regionlist
	},
#endif
#ifdef CONFIG_NE2K_CBUS_NEC108
	{
		NE2K_CBUS_HARDWARE_TYPE_NEC108,
		"PC-9801-108",
#ifndef MODULE
		nec108_portlist,
#endif
		&nec108_offsetinfo, nec108_regionlist
	},
#endif
#ifdef CONFIG_NE2K_CBUS_IOLA98
	{
		NE2K_CBUS_HARDWARE_TYPE_IOLA98,
		"LA-98",
#ifndef MODULE
		iola98_portlist,
#endif
		&iola98_offsetinfo, iola98_regionlist
	},
#endif
#ifdef CONFIG_NE2K_CBUS_CNET98EL
	{
		NE2K_CBUS_HARDWARE_TYPE_CNET98EL,
		"C-NET(98)E/L",
#ifndef MODULE
		cnet98el_portlist,
#endif
		&cnet98el_offsetinfo, cnet98el_regionlist
	},
#endif
/* NOTE: LGY98 must be probed before EGY98, or system stalled!? */
#ifdef CONFIG_NE2K_CBUS_LGY98
	{
		NE2K_CBUS_HARDWARE_TYPE_LGY98,
		"LGY-98",
#ifndef MODULE
		lgy98_portlist,
#endif
		&lgy98_offsetinfo, lgy98_regionlist
	},
#endif
#ifdef CONFIG_NE2K_CBUS_EGY98
	{
		NE2K_CBUS_HARDWARE_TYPE_EGY98,
		"EGY-98",
#ifndef MODULE
		egy98_portlist,
#endif
		&egy98_offsetinfo, egy98_regionlist
	},
#endif
	{
		0,
		"unsupported hardware",
#ifndef MODULE
		NULL,
#endif
		NULL, NULL
	}
};

static int __init ne2k_cbus_init(struct net_device *dev)
{
	struct ei_device *ei_local;
	if (dev->priv == NULL) {
		ei_local = kmalloc(sizeof(struct ei_device), GFP_KERNEL);
		if (ei_local == NULL)
			return -ENOMEM;
		memset(ei_local, 0, sizeof(struct ei_device));
		ei_local->reg_offset = kmalloc(sizeof(typeof(*ei_local->reg_offset))*18, GFP_KERNEL);
		if (ei_local->reg_offset == NULL) {
			kfree(ei_local);
			return -ENOMEM;
		}
		spin_lock_init(&ei_local->page_lock);
		dev->priv = ei_local;
	}
	return 0;
}

static void ne2k_cbus_destroy(struct net_device *dev)
{
	struct ei_device *ei_local = (struct ei_device *)(dev->priv);
	if (ei_local != NULL) {
		if (ei_local->reg_offset)
			kfree(ei_local->reg_offset);
		kfree(dev->priv);
		dev->priv = NULL;
	}
}

static const struct ne2k_cbus_hwinfo * __init ne2k_cbus_get_hwinfo(int hwtype)
{
	const struct ne2k_cbus_hwinfo *hw;

	for (hw = &ne2k_cbus_hwinfo_list[0]; hw->hwtype; hw++) {
		if (hw->hwtype == hwtype) break;
	}
	return hw;
}

static void __init ne2k_cbus_set_hwtype(struct net_device *dev, const struct ne2k_cbus_hwinfo *hw, int ioaddr)
{
	struct ei_device *ei_local = (struct ei_device *)(dev->priv);
	int i;
	int hwtype_old = dev->mem_start & NE2K_CBUS_HARDWARE_TYPE_MASK;

	if (!ei_local)
		panic("Gieee! ei_local == NULL!! (from %p)",
		       __builtin_return_address(0));

	dev->mem_start &= ~NE2K_CBUS_HARDWARE_TYPE_MASK;
	dev->mem_start |= hw->hwtype & NE2K_CBUS_HARDWARE_TYPE_MASK;

	if (ei_debug > 2) {
		printk(KERN_DEBUG "hwtype changed: %d -> %d\n",hwtype_old,(int)(dev->mem_start & NE2K_CBUS_HARDWARE_TYPE_MASK));
	}

	if (hw->offsetinfo) {
		for (i = 0; i < 8; i++) {
			ei_local->reg_offset[i] = hw->offsetinfo->skip * i;
		}
		for (i = 8; i < 16; i++) {
			ei_local->reg_offset[i] =
				hw->offsetinfo->skip*(i-8) + hw->offsetinfo->offset8;
		}
#ifdef CONFIG_NE2K_CBUS_NEC108
		if (hw->hwtype == NE2K_CBUS_HARDWARE_TYPE_NEC108) {
			int adj = (ioaddr & 0xf000) /2;
			ei_local->reg_offset[16] = 
				(hw->offsetinfo->offset10 | adj) - ioaddr;
			ei_local->reg_offset[17] = 
				(hw->offsetinfo->offset1f | adj) - ioaddr;
		} else {
#endif /* CONFIG_NE2K_CBUS_NEC108 */
			ei_local->reg_offset[16] = hw->offsetinfo->offset10;
			ei_local->reg_offset[17] = hw->offsetinfo->offset1f;
#ifdef CONFIG_NE2K_CBUS_NEC108
		}
#endif
	} else {
		/* make dummmy offset list */
		for (i = 0; i < 16; i++) {
			ei_local->reg_offset[i] = i;
		}
		ei_local->reg_offset[16] = 0x10;
		ei_local->reg_offset[17] = 0x1f;
	}
}

#if defined(CONFIG_NE2K_CBUS_ICM) || defined(CONFIG_NE2K_CBUS_CNET98EL)
static void __init ne2k_cbus_readmem(struct net_device *dev, int ioaddr, unsigned short memaddr, char *buf, unsigned short len)
{
	struct ei_device *ei_local = (struct ei_device *)(dev->priv);
	outb_p(E8390_NODMA | E8390_START, ioaddr+E8390_CMD);
	outb_p(len & 0xff, ioaddr+EN0_RCNTLO);
	outb_p(len >> 8, ioaddr+EN0_RCNTHI);
	outb_p(memaddr & 0xff, ioaddr+EN0_RSARLO);
	outb_p(memaddr >> 8, ioaddr+EN0_RSARHI);
	outb_p(E8390_RREAD | E8390_START, ioaddr+E8390_CMD);
	insw(ioaddr+NE_DATAPORT, buf, len >> 1);
}
static void __init ne2k_cbus_writemem(struct net_device *dev, int ioaddr, unsigned short memaddr, const char *buf, unsigned short len)
{
	struct ei_device *ei_local = (struct ei_device *)(dev->priv);
	outb_p(E8390_NODMA | E8390_START, ioaddr+E8390_CMD);
	outb_p(ENISR_RDC, ioaddr+EN0_ISR);
	outb_p(len & 0xff, ioaddr+EN0_RCNTLO);
	outb_p(len >> 8, ioaddr+EN0_RCNTHI);
	outb_p(memaddr & 0xff, ioaddr+EN0_RSARLO);
	outb_p(memaddr >> 8, ioaddr+EN0_RSARHI);
	outb_p(E8390_RWRITE | E8390_START, ioaddr+E8390_CMD);
	outsw(ioaddr+NE_DATAPORT, buf, len >> 1);
}
#endif

static int ne_probe_cbus(struct net_device *dev, const struct ne2k_cbus_hwinfo *hw, int ioaddr);
/* End of ne2k_cbus.h */
