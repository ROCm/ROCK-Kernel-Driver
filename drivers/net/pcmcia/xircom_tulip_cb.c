/* tulip.c: A DEC 21040-family ethernet driver for Linux. */
/*
	Written/copyright 1994-1999 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	This driver is for the Digital "Tulip" Ethernet adapter interface.
	It should work with most DEC 21*4*-based chips/ethercards, as well as
	with work-alike chips from Lite-On (PNIC) and Macronix (MXIC) and ASIX.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	Support and updates available at
	http://cesdis.gsfc.nasa.gov/linux/drivers/tulip.html
*/

#define CARDBUS 1
static const char version[] = "xircom_tulip_cb.c:v0.91 4/14/99 becker@scyld.com (modified by danilo@cs.uni-magdeburg.de for XIRCOM CBE, fixed by Doug Ledford)\n";

/* A few user-configurable values. */

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static int max_interrupt_work = 25;

#define MAX_UNITS 8
/* Used to pass the full-duplex flag, etc. */
static int full_duplex[MAX_UNITS];
static int options[MAX_UNITS];
static int mtu[MAX_UNITS];			/* Jumbo MTU for interfaces. */

/*  The possible media types that can be set in options[] are: */
static const char * const medianame[] = {
	"10baseT", "10base2", "AUI", "100baseTx",
	"10baseT-FD", "100baseTx-FD", "100baseT4", "100baseFx",
	"100baseFx-FD", "MII 10baseT", "MII 10baseT-FD", "MII",
	"10baseT(forced)", "MII 100baseTx", "MII 100baseTx-FD", "MII 100baseT4",
};

/* Keep the ring sizes a power of two for efficiency.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	16
#define RX_RING_SIZE	32

/* Set the copy breakpoint for the copy-only-tiny-buffer Rx structure. */
#ifdef __alpha__
static int rx_copybreak = 1518;
#else
static int rx_copybreak = 100;
#endif

/*
  Set the bus performance register.
	Typical: Set 16 longword cache alignment, no burst limit.
	Cache alignment bits 15:14	     Burst length 13:8
		0000	No alignment  0x00000000 unlimited		0800 8 longwords
		4000	8  longwords		0100 1 longword		1000 16 longwords
		8000	16 longwords		0200 2 longwords	2000 32 longwords
		C000	32  longwords		0400 4 longwords
	Warning: many older 486 systems are broken and require setting 0x00A04800
	   8 longword cache alignment, 8 longword burst.
	ToDo: Non-Intel setting could be better.
*/

#if defined(__alpha__) || defined(__ia64__) || defined(__x86_64__)
static int csr0 = 0x01A00000 | 0xE000;
#elif defined(__powerpc__)
static int csr0 = 0x01B00000 | 0x8000;
#elif defined(__sparc__)
static int csr0 = 0x01B00080 | 0x8000;
#elif defined(__i386__)
static int csr0 = 0x01A00000 | 0x8000;
#else
#warning Processor architecture undefined!
static int csr0 = 0x00A00000 | 0x4800;
#endif

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (4*HZ)
#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

#if !defined(__OPTIMIZE__)  ||  !defined(__KERNEL__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error You must compile this driver with "-O".
#endif

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */

/* Kernel compatibility defines, some common to David Hinds' PCMCIA package.
   This is only in the support-all-kernels source code. */

MODULE_AUTHOR("Donald Becker <becker@scyld.com>");
MODULE_DESCRIPTION("Digital 21*4* Tulip ethernet driver");
MODULE_LICENSE("GPL");

MODULE_PARM(debug, "i");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(rx_copybreak, "i");
MODULE_PARM(csr0, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");

#define RUN_AT(x) (jiffies + (x))

#define tulip_debug debug
#ifdef TULIP_DEBUG
static int tulip_debug = TULIP_DEBUG;
#else
static int tulip_debug = 1;
#endif

/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the DECchip "Tulip", Digital's
single-chip ethernet controllers for PCI.  Supported members of the family
are the 21040, 21041, 21140, 21140A, 21142, and 21143.  Similar work-alike
chips from Lite-On, Macronics, ASIX, Compex and other listed below are also
supported. 

These chips are used on at least 140 unique PCI board designs.  The great
number of chips and board designs supported is the reason for the
driver size and complexity.  Almost of the increasing complexity is in the
board configuration and media selection code.  There is very little
increasing in the operational critical path length.

II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS preferably should assign the
PCI INTA signal to an otherwise unused system IRQ line.

III. Driver operation

IIIa. Ring buffers

The Tulip can use either ring buffers or lists of Tx and Rx descriptors.
This driver uses statically allocated rings of Rx and Tx descriptors, set at
compile time by RX/TX_RING_SIZE.  This version of the driver allocates skbuffs
for the Rx ring buffers at open() time and passes the skb->data field to the
Tulip as receive data buffers.  When an incoming frame is less than
RX_COPYBREAK bytes long, a fresh skbuff is allocated and the frame is
copied to the new skbuff.  When the incoming frame is larger, the skbuff is
passed directly up the protocol stack and replaced by a newly allocated
skbuff.

The RX_COPYBREAK value is chosen to trade-off the memory wasted by
using a full-sized skbuff for small frames vs. the copying costs of larger
frames.  For small frames the copying cost is negligible (esp. considering
that we are pre-loading the cache with immediately useful header
information).  For large frames the copying cost is non-trivial, and the
larger copy might flush the cache of useful data.  A subtle aspect of this
choice is that the Tulip only receives into longword aligned buffers, thus
the IP header at offset 14 isn't longword aligned for further processing.
Copied frames are put into the new skbuff at an offset of "+2", thus copying
has the beneficial effect of aligning the IP header and preloading the
cache.

IIIC. Synchronization
The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and other software.

The send packet thread has partial control over the Tx ring and 'dev->tbusy'
flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
queue slot is empty, it clears the tbusy flag when finished otherwise it sets
the 'tp->tx_full' flag.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  (The Tx-done interrupt can't be selectively turned off, so
we can't avoid the interrupt overhead by having the Tx routine reap the Tx
stats.)	 After reaping the stats, it marks the queue entry as empty by setting
the 'base' to zero.	 Iff the 'tp->tx_full' flag is set, it clears both the
tx_full and tbusy flags.

IV. Notes

IVb. References

http://cesdis.gsfc.nasa.gov/linux/misc/NWay.html
http://www.digital.com  (search for current 21*4* datasheets and "21X4 SROM")
http://www.national.com/pf/DP/DP83840A.html

IVc. Errata

We cannot use MII interrupts because there is no defined GPIO pin to attach
them.  The MII transceiver status is polled using an kernel timer.

*/

static void tulip_timer(unsigned long data);

enum tbl_flag {
	HAS_MII=1, HAS_ACPI=2,
};
static struct tulip_chip_table {
	char *chip_name;
	int io_size;
	int valid_intrs;			/* CSR7 interrupt enable settings */
	int flags;
	void (*media_timer)(unsigned long data);
} tulip_tbl[] = {
  { "Xircom Cardbus Adapter (DEC 21143 compatible mode)", 128, 0x0801fbff,
       HAS_MII | HAS_ACPI, tulip_timer }, 
  {0},
};
/* This matches the table above. */
enum chips {
	X3201_3,
};

/* A full-duplex map for media types. */
enum MediaIs {
	MediaIsFD = 1, MediaAlwaysFD=2, MediaIsMII=4, MediaIsFx=8,
	MediaIs100=16};
static const char media_cap[] =
{0,0,0,16,  3,19,16,24,  27,4,7,5, 0,20,23,20 };

/* Offsets to the Command and Status Registers, "CSRs".  All accesses
   must be longword instructions and quadword aligned. */
enum tulip_offsets {
	CSR0=0,    CSR1=0x08, CSR2=0x10, CSR3=0x18, CSR4=0x20, CSR5=0x28,
	CSR6=0x30, CSR7=0x38, CSR8=0x40, CSR9=0x48, CSR10=0x50, CSR11=0x58,
	CSR12=0x60, CSR13=0x68, CSR14=0x70, CSR15=0x78 };

/* The bits in the CSR5 status registers, mostly interrupt sources. */
enum status_bits {
	TimerInt=0x800, TPLnkFail=0x1000, TPLnkPass=0x10,
	NormalIntr=0x10000, AbnormalIntr=0x8000,
	RxJabber=0x200, RxDied=0x100, RxNoBuf=0x80, RxIntr=0x40,
	TxFIFOUnderflow=0x20, TxJabber=0x08, TxNoBuf=0x04, TxDied=0x02, TxIntr=0x01,
};

/* The Tulip Rx and Tx buffer descriptors. */
struct tulip_rx_desc {
	s32 status;
	s32 length;
	u32 buffer1, buffer2;
};

struct tulip_tx_desc {
	s32 status;
	s32 length;
	u32 buffer1, buffer2;				/* We use only buffer 1.  */
};

enum desc_status_bits {
	DescOwned=0x80000000, RxDescFatalErr=0x8000, RxWholePkt=0x0300,
};

/* Ring-wrap flag in length field, use for last ring entry.
	0x01000000 means chain on buffer2 address,
	0x02000000 means use the ring start address in CSR2/3.
   Note: Some work-alike chips do not function correctly in chained mode.
   The ASIX chip works only in chained mode.
   Thus we indicates ring mode, but always write the 'next' field for
   chained mode as well.
*/
#define DESC_RING_WRAP 0x02000000

struct tulip_private {
	char devname[8];			/* Used only for kernel debugging. */
	const char *product_name;
	struct tulip_rx_desc rx_ring[RX_RING_SIZE];
	struct tulip_tx_desc tx_ring[TX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
#ifdef CARDBUS
	/* The X3201-3 requires double word aligned tx bufs */
	struct sk_buff* tx_aligned_skbuff[TX_RING_SIZE];
#endif
	/* The addresses of receive-in-place skbuffs. */
	struct sk_buff* rx_skbuff[RX_RING_SIZE];
	char *rx_buffs;				/* Address of temporary Rx buffers. */
	u8 setup_buf[96*sizeof(u16) + 7];
	u16 *setup_frame;		/* Pseudo-Tx frame to init address table. */
	int chip_id;
	int revision;
	struct net_device_stats stats;
	struct timer_list timer;	/* Media selection timer. */
	int interrupt;				/* In-interrupt flag. */
	unsigned int cur_rx, cur_tx;		/* The next free ring entry */
	unsigned int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int full_duplex_lock:1;
	unsigned int default_port:4;		/* Last dev->if_port value. */
	unsigned int medialock:1;			/* Don't sense media type. */
	unsigned int mediasense:1;			/* Media sensing in progress. */
	unsigned int open:1;
	unsigned int csr0;					/* CSR0 setting. */
	unsigned int csr6;					/* Current CSR6 control settings. */
	u16 to_advertise;					/* NWay capabilities advertised.  */
	u16 advertising[4];
	signed char phys[4], mii_cnt;		/* MII device addresses. */
	int saved_if_port;
	struct pci_dev *pdev;
	spinlock_t lock;
};

static int mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static void tulip_up(struct net_device *dev);
static void tulip_down(struct net_device *dev);
static int tulip_open(struct net_device *dev);
static void tulip_timer(unsigned long data);
static void tulip_tx_timeout(struct net_device *dev);
static void tulip_init_ring(struct net_device *dev);
static int tulip_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int tulip_rx(struct net_device *dev);
static void tulip_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int tulip_close(struct net_device *dev);
static struct net_device_stats *tulip_get_stats(struct net_device *dev);
#ifdef HAVE_PRIVATE_IOCTL
static int private_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
#endif
static void set_rx_mode(struct net_device *dev);

/* The Xircom cards are picky about when certain bits in CSR6 can be
   manipulated.  Keith Owens <kaos@ocs.com.au>. */

static void outl_CSR6 (u32 newcsr6, long ioaddr, int chip_idx)
{
	const int strict_bits = 0x0060e202;
    int csr5, csr5_22_20, csr5_19_17, currcsr6, attempts = 200;
    long flags;
    save_flags(flags);
    cli();
    newcsr6 &= 0x726cfecb; /* mask out the reserved CSR6 bits that always */
			   /* read 0 on the Xircom cards */
    newcsr6 |= 0x320c0000; /* or in the reserved bits that always read 1 */
    currcsr6 = inl(ioaddr + CSR6);
    if (((newcsr6 & strict_bits) == (currcsr6 & strict_bits)) ||
	((currcsr6 & ~0x2002) == 0)) {
		outl(newcsr6, ioaddr + CSR6);	/* safe */
		restore_flags(flags);
		return;
    }
    /* make sure the transmitter and receiver are stopped first */
    currcsr6 &= ~0x2002;
    while (1) {
		csr5 = inl(ioaddr + CSR5);
		if (csr5 == 0xffffffff)
			break;  /* cannot read csr5, card removed? */
		csr5_22_20 = csr5 & 0x700000;
		csr5_19_17 = csr5 & 0x0e0000;
		if ((csr5_22_20 == 0 || csr5_22_20 == 0x600000) &&
			(csr5_19_17 == 0 || csr5_19_17 == 0x80000 || csr5_19_17 == 0xc0000))
			break;  /* both are stopped or suspended */
		if (!--attempts) {
			printk(KERN_INFO "tulip.c: outl_CSR6 too many attempts,"
				   "csr5=0x%08x\n", csr5);
			outl(newcsr6, ioaddr + CSR6);  /* unsafe but do it anyway */
			restore_flags(flags);
			return;
		}
		outl(currcsr6, ioaddr + CSR6);
		udelay(1);
    }
    /* now it is safe to change csr6 */
    outl(newcsr6, ioaddr + CSR6);
    restore_flags(flags);
}

static struct net_device *tulip_probe1(struct pci_dev *pdev,
				       long ioaddr, int irq,
				       int chip_idx, int board_idx)
{
	static int did_version;			/* Already printed version info. */
	struct net_device *dev;
	struct tulip_private *tp;
	u8 chip_rev;
	int i;

	if (tulip_debug > 0  &&  did_version++ == 0)
		printk(KERN_INFO "%s", version);

	dev = alloc_etherdev(0);
	if (!dev)
		return NULL;

	pci_read_config_byte(pdev, PCI_REVISION_ID, &chip_rev);
	/* Bring the 21143 out of sleep mode.
	   Caution: Snooze mode does not work with some boards! */
	if (tulip_tbl[chip_idx].flags & HAS_ACPI)
		pci_write_config_dword(pdev, 0x40, 0x00000000);

	/* Stop the chip's Tx and Rx processes. */
	outl_CSR6(inl(ioaddr + CSR6) & ~0x2002, ioaddr, chip_idx);
	/* Clear the missed-packet counter. */
	(volatile int)inl(ioaddr + CSR8);

	/* The station address ROM is read byte serially.  The register must
	   be polled, waiting for the value to be read bit serially from the
	   EEPROM.
	   */
	if (chip_idx == X3201_3) {
		/* Xircom has its address stored in the CIS
		 * we access it through the boot rom interface for now
		 * this might not work, as the CIS is not parsed but I
		 * (danilo) use the offset I found on my card's CIS !!!
		 * 
		 * Doug Ledford: I changed this routine around so that it
		 * walks the CIS memory space, parsing the config items, and
		 * finds the proper lan_node_id tuple and uses the data
		 * stored there.
		 */
		unsigned char j, tuple, link, data_id, data_count;
		outl(1<<12, ioaddr + CSR9); /* enable boot rom access */
		for (i = 0x100; i < 0x1f7; i += link+2) {
			outl(i, ioaddr + CSR10);
			tuple = inl(ioaddr + CSR9) & 0xff;
			outl(i + 1, ioaddr + CSR10);
			link = inl(ioaddr + CSR9) & 0xff;
			outl(i + 2, ioaddr + CSR10);
			data_id = inl(ioaddr + CSR9) & 0xff;
			outl(i + 3, ioaddr + CSR10);
			data_count = inl(ioaddr + CSR9) & 0xff;
			if ( (tuple == 0x22) &&
				 (data_id == 0x04) && (data_count == 0x06) ) {
				/*
				 * This is it.  We have the data we want.
				 */
				for (j = 0; j < 6; j++) {
					outl(i + j + 4, ioaddr + CSR10);
					dev->dev_addr[j] = inl(ioaddr + CSR9) & 0xff;
				}
				break;
			} else if (link == 0) {
				break;
			}
		}
	}

	/* We do a request_region() only to register /proc/ioports info. */
	request_region(ioaddr, tulip_tbl[chip_idx].io_size, "xircom_tulip_cb");

	dev->base_addr = ioaddr;
	dev->irq = irq;

	/* Make certain the data structures are quadword aligned. */
	tp = (void *)(((long)kmalloc(sizeof(*tp), GFP_KERNEL | GFP_DMA) + 7) & ~7);
	memset(tp, 0, sizeof(*tp));
	dev->priv = tp;

	tp->lock = SPIN_LOCK_UNLOCKED;
	tp->pdev = pdev;
	tp->chip_id = chip_idx;
	tp->revision = chip_rev;
	tp->csr0 = csr0;
	tp->setup_frame = (u16 *)(((unsigned long)tp->setup_buf + 7) & ~7);

	/* BugFixes: The 21143-TD hangs with PCI Write-and-Invalidate cycles.
	   And the ASIX must have a burst limit or horrible things happen. */
	if (chip_idx == X3201_3)
		tp->csr0 &= ~0x01000000;

	/* The lower four bits are the media type. */
	if (board_idx >= 0  &&  board_idx < MAX_UNITS) {
		tp->default_port = options[board_idx] & 15;
		if ((options[board_idx] & 0x90) || full_duplex[board_idx] > 0)
			tp->full_duplex = 1;
		if (mtu[board_idx] > 0)
			dev->mtu = mtu[board_idx];
	}
	if (dev->mem_start)
		tp->default_port = dev->mem_start;
	if (tp->default_port) {
		tp->medialock = 1;
		if (media_cap[tp->default_port] & MediaAlwaysFD)
			tp->full_duplex = 1;
	}
	if (tp->full_duplex)
		tp->full_duplex_lock = 1;

	if (media_cap[tp->default_port] & MediaIsMII) {
		u16 media2advert[] = { 0x20, 0x40, 0x03e0, 0x60, 0x80, 0x100, 0x200 };
		tp->to_advertise = media2advert[tp->default_port - 9];
	} else
		tp->to_advertise = 0x03e1;

	if (tulip_tbl[chip_idx].flags & HAS_MII) {
		int phy, phy_idx;
		/* Find the connected MII xcvrs.
		   Doing this in open() would allow detecting external xcvrs later,
		   but takes much time. */
		for (phy = 0, phy_idx = 0; phy < 32 && phy_idx < sizeof(tp->phys);
			 phy++) {
			int mii_status = mdio_read(dev, phy, 1);
			if ((mii_status & 0x8301) == 0x8001 ||
				((mii_status & 0x8000) == 0  && (mii_status & 0x7800) != 0)) {
				int mii_reg0 = mdio_read(dev, phy, 0);
				int mii_advert = mdio_read(dev, phy, 4);
				int reg4 = ((mii_status>>6) & tp->to_advertise) | 1;
				tp->phys[phy_idx] = phy;
				tp->advertising[phy_idx++] = reg4;
				printk(KERN_INFO "xircom(%s):  MII transceiver #%d "
					   "config %4.4x status %4.4x advertising %4.4x.\n",
					   pdev->slot_name, phy, mii_reg0, mii_status, mii_advert);
				/* Fixup for DLink with miswired PHY. */
				if (mii_advert != reg4) {
					printk(KERN_DEBUG "xircom(%s):  Advertising %4.4x on PHY %d,"
						   " previously advertising %4.4x.\n",
						   pdev->slot_name, reg4, phy, mii_advert);
					mdio_write(dev, phy, 4, reg4);
				}
				/* Enable autonegotiation: some boards default to off. */
				mdio_write(dev, phy, 0, mii_reg0 |
						   (tp->full_duplex ? 0x1100 : 0x1000) |
						   (media_cap[tp->default_port]&MediaIs100 ? 0x2000:0));
			}
		}
		tp->mii_cnt = phy_idx;
		if (phy_idx == 0) {
			printk(KERN_INFO "xircom(%s): ***WARNING***: No MII transceiver found!\n",
			       pdev->slot_name);
			tp->phys[0] = 1;
		}
	}

	/* The Tulip-specific entries in the device structure. */
	dev->open = &tulip_open;
	dev->hard_start_xmit = &tulip_start_xmit;
	dev->stop = &tulip_close;
	dev->get_stats = &tulip_get_stats;
#ifdef HAVE_PRIVATE_IOCTL
	dev->do_ioctl = &private_ioctl;
#endif
#ifdef HAVE_MULTICAST
	dev->set_multicast_list = &set_rx_mode;
#endif
	dev->tx_timeout = tulip_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;

	/* Reset the xcvr interface and turn on heartbeat. */
	switch (chip_idx) {
	case X3201_3:
		outl(0x0008, ioaddr + CSR15);
		udelay(5);  /* The delays are Xircom recommended to give the
					 * chipset time to reset the actual hardware
					 * on the PCMCIA card
					 */
		outl(0xa8050000, ioaddr + CSR15);
		udelay(5);
		outl(0xa00f0000, ioaddr + CSR15);
		udelay(5);
		outl_CSR6(0x32000200, ioaddr, chip_idx);
		break;
	}

	if (register_netdev(dev)) {
		request_region(ioaddr, tulip_tbl[chip_idx].io_size, "xircom_tulip_cb");
		kfree(dev->priv);
		kfree(dev);
		return NULL;
	}

	printk(KERN_INFO "%s: %s rev %d at %#3lx,",
	       dev->name, tulip_tbl[chip_idx].chip_name, chip_rev, ioaddr);
	for (i = 0; i < 6; i++)
		printk("%c%2.2X", i ? ':' : ' ', dev->dev_addr[i]);
	printk(", IRQ %d.\n", irq);

	return dev;
}

/* MII transceiver control section.
   Read and write the MII registers using software-generated serial
   MDIO protocol.  See the MII specifications or DP83840A data sheet
   for details. */

/* The maximum data clock rate is 2.5 Mhz.  The minimum timing is usually
   met by back-to-back PCI I/O cycles, but we insert a delay to avoid
   "overclocking" issues or future 66Mhz PCI. */
#define mdio_delay() inl(mdio_addr)

/* Read and write the MII registers using software-generated serial
   MDIO protocol.  It is just different enough from the EEPROM protocol
   to not share code.  The maxium data clock rate is 2.5 Mhz. */
#define MDIO_SHIFT_CLK	0x10000
#define MDIO_DATA_WRITE0 0x00000
#define MDIO_DATA_WRITE1 0x20000
#define MDIO_ENB		0x00000		/* Ignore the 0x02000 databook setting. */
#define MDIO_ENB_IN		0x40000
#define MDIO_DATA_READ	0x80000

static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	int i;
	int read_cmd = (0xf6 << 10) | (phy_id << 5) | location;
	int retval = 0;
	long ioaddr = dev->base_addr;
	long mdio_addr = ioaddr + CSR9;

	/* Establish sync by sending at least 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the read command bits out. */
	for (i = 15; i >= 0; i--) {
		int dataval = (read_cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;

		outl(MDIO_ENB | dataval, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Read the two transition, 16 data, and wire-idle bits. */
	for (i = 19; i > 0; i--) {
		outl(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		retval = (retval << 1) | ((inl(mdio_addr) & MDIO_DATA_READ) ? 1 : 0);
		outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	return (retval>>1) & 0xffff;
}

static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	int i;
	int cmd = (0x5002 << 16) | (phy_id << 23) | (location<<18) | value;
	long ioaddr = dev->base_addr;
	long mdio_addr = ioaddr + CSR9;

	/* Establish sync by sending 32 logic ones. */
	for (i = 32; i >= 0; i--) {
		outl(MDIO_ENB | MDIO_DATA_WRITE1, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | MDIO_DATA_WRITE1 | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Shift the command bits out. */
	for (i = 31; i >= 0; i--) {
		int dataval = (cmd & (1 << i)) ? MDIO_DATA_WRITE1 : 0;
		outl(MDIO_ENB | dataval, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB | dataval | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	/* Clear out extra bits. */
	for (i = 2; i > 0; i--) {
		outl(MDIO_ENB_IN, mdio_addr);
		mdio_delay();
		outl(MDIO_ENB_IN | MDIO_SHIFT_CLK, mdio_addr);
		mdio_delay();
	}
	return;
}

static void
tulip_up(struct net_device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int i;

	/* On some chip revs we must set the MII/SYM port before the reset!? */
	if (tp->mii_cnt)
		outl_CSR6(0x00040000, ioaddr, tp->chip_id);

	/* Reset the chip, holding bit 0 set at least 50 PCI cycles. */
	outl(0x00000001, ioaddr + CSR0);

	/* Deassert reset. */
	outl(tp->csr0, ioaddr + CSR0);
	udelay(2);

	if (tulip_tbl[tp->chip_id].flags & HAS_ACPI)
		pci_write_config_dword(tp->pdev, 0x40, 0x00000000);

	/* Clear the tx ring */
	for (i = 0; i < TX_RING_SIZE; i++) {
		tp->tx_skbuff[i] = 0;
		tp->tx_ring[i].status = 0x00000000;
	}

	if (tulip_debug > 1)
		printk(KERN_DEBUG "%s: tulip_open() irq %d.\n", dev->name, dev->irq);

	{ /* X3201_3 */
		u16 *eaddrs = (u16 *)dev->dev_addr;
		u16 *setup_frm = &tp->setup_frame[0*6];
		
		/* fill the table with the broadcast address */
		memset(tp->setup_frame, 0xff, 96*sizeof(u16));
		/* re-fill the first 14 table entries with our address */
		for(i=0; i<14; i++) {
			*setup_frm++ = eaddrs[0]; *setup_frm++ = eaddrs[0];
			*setup_frm++ = eaddrs[1]; *setup_frm++ = eaddrs[1];
			*setup_frm++ = eaddrs[2]; *setup_frm++ = eaddrs[2];
		}

		/* Put the setup frame on the Tx list. */
		tp->tx_ring[tp->cur_tx].length = 0x68000000 | 192;
		/* Lie about the address of our setup frame to make the */
		/* chip happy */
		tp->tx_ring[tp->cur_tx].buffer1 = virt_to_bus(tp->setup_frame);
		tp->tx_ring[tp->cur_tx].status = DescOwned;

		tp->cur_tx++;
	}
	outl(virt_to_bus(tp->rx_ring), ioaddr + CSR3);
	outl(virt_to_bus(tp->tx_ring), ioaddr + CSR4);

	tp->saved_if_port = dev->if_port;
	if (dev->if_port == 0)
		dev->if_port = tp->default_port;

	/* Allow selecting a default media. */
	tp->csr6 = 0;
	if (tp->chip_id == X3201_3) {
		outl(0x0008, ioaddr + CSR15);
		udelay(5);
		outl(0xa8050000, ioaddr + CSR15);
		udelay(5);
		outl(0xa00f0000, ioaddr + CSR15); 
		udelay(5);
		tp->csr6  = 0x32400000;
	}
	/* Start the chip's Tx to process setup frame. */
	outl_CSR6(tp->csr6, ioaddr, tp->chip_id);
	outl_CSR6(tp->csr6 | 0x2000, ioaddr, tp->chip_id);

	/* Enable interrupts by setting the interrupt mask. */
	outl(tulip_tbl[tp->chip_id].valid_intrs, ioaddr + CSR5);
	outl(tulip_tbl[tp->chip_id].valid_intrs, ioaddr + CSR7);
	outl_CSR6(tp->csr6 | 0x2002, ioaddr, tp->chip_id);
	outl(0, ioaddr + CSR2);		/* Rx poll demand */
	
	netif_start_queue (dev);

	if (tulip_debug > 2) {
		printk(KERN_DEBUG "%s: Done tulip_open(), CSR0 %8.8x, CSR5 %8.8x CSR6 %8.8x.\n",
			   dev->name, inl(ioaddr + CSR0), inl(ioaddr + CSR5),
			   inl(ioaddr + CSR6));
	}
	/* Set the timer to switch to check for link beat and perhaps switch
	   to an alternate media type. */
	init_timer(&tp->timer);
	tp->timer.expires = RUN_AT(5*HZ);
	tp->timer.data = (unsigned long)dev;
	tp->timer.function = tulip_tbl[tp->chip_id].media_timer;
	add_timer(&tp->timer);
}

static int
tulip_open(struct net_device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;

	if (request_irq(dev->irq, &tulip_interrupt, SA_SHIRQ, dev->name, dev))
		return -EAGAIN;

	tulip_init_ring(dev);

	tulip_up(dev);
	tp->open = 1;
	MOD_INC_USE_COUNT;

	return 0;
}

/*
  Check the MII negotiated duplex, and change the CSR6 setting if
  required.
  Return 0 if everything is OK.
  Return < 0 if the transceiver is missing or has no link beat.
  */
#if 0
static int check_duplex(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int mii_reg1, mii_reg5, negotiated, duplex;

	if (tp->full_duplex_lock)
		return 0;
	mii_reg1 = mdio_read(dev, tp->phys[0], 1);
	mii_reg5 = mdio_read(dev, tp->phys[0], 5);
	if (tulip_debug > 1)
		printk(KERN_INFO "%s: MII status %4.4x, Link partner report "
			   "%4.4x.\n", dev->name, mii_reg1, mii_reg5);
	if (mii_reg1 == 0xffff)
		return -2;
	if ((mii_reg1 & 0x0004) == 0) {
		int new_reg1 = mdio_read(dev, tp->phys[0], 1);
		if ((new_reg1 & 0x0004) == 0) {
			if (tulip_debug  > 1)
				printk(KERN_INFO "%s: No link beat on the MII interface,"
					   " status %4.4x.\n", dev->name, new_reg1);
			return -1;
		}
	}
	negotiated = mii_reg5 & tp->advertising[0];
	duplex = ((negotiated & 0x0300) == 0x0100
			  || (negotiated & 0x00C0) == 0x0040);
	/* 100baseTx-FD  or  10T-FD, but not 100-HD */
	if (tp->full_duplex != duplex) {
		tp->full_duplex = duplex;
		if (tp->full_duplex) tp->csr6 |= 0x0200;
		else				 tp->csr6 &= ~0x0200;
		outl_CSR6(tp->csr6 | 0x0002, ioaddr, tp->chip_id);
		outl_CSR6(tp->csr6 | 0x2002, ioaddr, tp->chip_id);
		if (tulip_debug > 0)
			printk(KERN_INFO "%s: Setting %s-duplex based on MII"
				   "#%d link partner capability of %4.4x.\n",
				   dev->name, tp->full_duplex ? "full" : "half",
				   tp->phys[0], mii_reg5);
	}
	return 0;
}
#endif

static void tulip_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	long ioaddr = dev->base_addr;
	u32 csr12 = inl(ioaddr + CSR12);
	int next_tick = 2*HZ;

	if (tulip_debug > 2) {
		printk(KERN_DEBUG "%s: Media selection tick, status %8.8x mode %8.8x "
			   "SIA %8.8x %8.8x %8.8x %8.8x.\n",
			   dev->name, inl(ioaddr + CSR5), inl(ioaddr + CSR6),
			   csr12, inl(ioaddr + CSR13),
			   inl(ioaddr + CSR14), inl(ioaddr + CSR15));
	}

	/* Not much that can be done.
	   Assume this a generic MII or SYM transceiver. */
	next_tick = 60*HZ;
	if (tulip_debug > 2)
		printk(KERN_DEBUG "%s: network media monitor CSR6 %8.8x "
		       "CSR12 0x%2.2x.\n",
		       dev->name, inl(ioaddr + CSR6), csr12 & 0xff);

	tp->timer.expires = RUN_AT(next_tick);
	add_timer(&tp->timer);
}

static void tulip_tx_timeout(struct net_device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	long ioaddr = dev->base_addr;

	if (media_cap[dev->if_port] & MediaIsMII) {
		/* Do nothing -- the media monitor should handle this. */
		if (tulip_debug > 1)
			printk(KERN_WARNING "%s: Transmit timeout using MII device.\n",
				   dev->name);
	}

#if defined(way_too_many_messages)
	if (tulip_debug > 3) {
		int i;
		for (i = 0; i < RX_RING_SIZE; i++) {
			u8 *buf = (u8 *)(tp->rx_ring[i].buffer1);
			int j;
			printk(KERN_DEBUG "%2d: %8.8x %8.8x %8.8x %8.8x  "
				   "%2.2x %2.2x %2.2x.\n",
				   i, (unsigned int)tp->rx_ring[i].status,
				   (unsigned int)tp->rx_ring[i].length,
				   (unsigned int)tp->rx_ring[i].buffer1,
				   (unsigned int)tp->rx_ring[i].buffer2,
				   buf[0], buf[1], buf[2]);
			for (j = 0; buf[j] != 0xee && j < 1600; j++)
				if (j < 100) printk(" %2.2x", buf[j]);
			printk(" j=%d.\n", j);
		}
		printk(KERN_DEBUG "  Rx ring %8.8x: ", (int)tp->rx_ring);
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)tp->rx_ring[i].status);
		printk("\n" KERN_DEBUG "  Tx ring %8.8x: ", (int)tp->tx_ring);
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)tp->tx_ring[i].status);
		printk("\n");
	}
#endif

	/* Stop and restart the chip's Tx processes . */
	outl_CSR6(tp->csr6 | 0x0002, ioaddr, tp->chip_id);
	outl_CSR6(tp->csr6 | 0x2002, ioaddr, tp->chip_id);
	/* Trigger an immediate transmit demand. */
	outl(0, ioaddr + CSR1);

	dev->trans_start = jiffies;
	netif_wake_queue (dev);
	tp->stats.tx_errors++;
}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void tulip_init_ring(struct net_device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int i;

	tp->tx_full = 0;
	tp->cur_rx = tp->cur_tx = 0;
	tp->dirty_rx = tp->dirty_tx = 0;

	for (i = 0; i < RX_RING_SIZE; i++) {
		tp->rx_ring[i].status = 0x00000000;
		tp->rx_ring[i].length = PKT_BUF_SZ;
		tp->rx_ring[i].buffer2 = virt_to_bus(&tp->rx_ring[i+1]);
		tp->rx_skbuff[i] = NULL;
	}
	/* Mark the last entry as wrapping the ring. */
	tp->rx_ring[i-1].length = PKT_BUF_SZ | DESC_RING_WRAP;
	tp->rx_ring[i-1].buffer2 = virt_to_bus(&tp->rx_ring[0]);

	for (i = 0; i < RX_RING_SIZE; i++) {
		/* Note the receive buffer must be longword aligned.
		   dev_alloc_skb() provides 16 byte alignment.  But do *not*
		   use skb_reserve() to align the IP header! */
		struct sk_buff *skb = dev_alloc_skb(PKT_BUF_SZ);
		tp->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		skb->dev = dev;			/* Mark as being used by this device. */
		tp->rx_ring[i].status = DescOwned;	/* Owned by Tulip chip */
		tp->rx_ring[i].buffer1 = virt_to_bus(skb->tail);
	}
	tp->dirty_rx = (unsigned int)(i - RX_RING_SIZE);

	/* The Tx buffer descriptor is filled in as needed, but we
	   do need to clear the ownership bit. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		tp->tx_skbuff[i] = 0;
		tp->tx_ring[i].status = 0x00000000;
		tp->tx_ring[i].buffer2 = virt_to_bus(&tp->tx_ring[i+1]);
#ifdef CARDBUS
		if (tp->chip_id == X3201_3)
			tp->tx_aligned_skbuff[i] = dev_alloc_skb(PKT_BUF_SZ);
#endif /* CARDBUS */
	}
	tp->tx_ring[i-1].buffer2 = virt_to_bus(&tp->tx_ring[0]);
}

static int
tulip_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int entry;
	u32 flag;

	/* Caution: the write order is important here, set the base address
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = tp->cur_tx % TX_RING_SIZE;

	tp->tx_skbuff[entry] = skb;
#ifdef CARDBUS
	if (tp->chip_id == X3201_3) {
		memcpy(tp->tx_aligned_skbuff[entry]->data,skb->data,skb->len);
		tp->tx_ring[entry].buffer1 = virt_to_bus(tp->tx_aligned_skbuff[entry]->data);
	} else
#endif
		tp->tx_ring[entry].buffer1 = virt_to_bus(skb->data);

	if (tp->cur_tx - tp->dirty_tx < TX_RING_SIZE/2) {/* Typical path */
		flag = 0x60000000; /* No interrupt */
	} else if (tp->cur_tx - tp->dirty_tx == TX_RING_SIZE/2) {
		flag = 0xe0000000; /* Tx-done intr. */
	} else if (tp->cur_tx - tp->dirty_tx < TX_RING_SIZE - 2) {
		flag = 0x60000000; /* No Tx-done intr. */
	} else {
		/* Leave room for set_rx_mode() to fill entries. */
		flag = 0xe0000000; /* Tx-done intr. */
		tp->tx_full = 1;
	}
	if (entry == TX_RING_SIZE-1)
		flag |= 0xe0000000 | DESC_RING_WRAP;

	tp->tx_ring[entry].length = skb->len | flag;
	tp->tx_ring[entry].status = DescOwned;	/* Pass ownership to the chip. */
	tp->cur_tx++;
	if (tp->tx_full)
		netif_stop_queue (dev);
	else
		netif_wake_queue (dev);

	/* Trigger an immediate transmit demand. */
	outl(0, dev->base_addr + CSR1);

	dev->trans_start = jiffies;

	return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void tulip_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *)dev_instance;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int csr5, work_budget = max_interrupt_work;

	spin_lock (&tp->lock);

	do {
		csr5 = inl(ioaddr + CSR5);
		/* Acknowledge all of the current interrupt sources ASAP. */
		outl(csr5 & 0x0001ffff, ioaddr + CSR5);

		if (tulip_debug > 4)
			printk(KERN_DEBUG "%s: interrupt  csr5=%#8.8x new csr5=%#8.8x.\n",
				   dev->name, csr5, inl(dev->base_addr + CSR5));

		if (csr5 == 0xffffffff)
			break;	/* all bits set, assume PCMCIA card removed */

		if ((csr5 & (NormalIntr|AbnormalIntr)) == 0)
			break;

		if (csr5 & (RxIntr | RxNoBuf))
			work_budget -= tulip_rx(dev);

		if (csr5 & (TxNoBuf | TxDied | TxIntr)) {
			unsigned int dirty_tx;

			for (dirty_tx = tp->dirty_tx; tp->cur_tx - dirty_tx > 0;
				 dirty_tx++) {
				int entry = dirty_tx % TX_RING_SIZE;
				int status = tp->tx_ring[entry].status;

				if (status < 0)
					break;			/* It still hasn't been Txed */
				/* Check for Rx filter setup frames. */
				if (tp->tx_skbuff[entry] == NULL)
				  continue;

				if (status & 0x8000) {
					/* There was an major error, log it. */
#ifndef final_version
					if (tulip_debug > 1)
						printk(KERN_DEBUG "%s: Transmit error, Tx status %8.8x.\n",
							   dev->name, status);
#endif
					tp->stats.tx_errors++;
					if (status & 0x4104) tp->stats.tx_aborted_errors++;
					if (status & 0x0C00) tp->stats.tx_carrier_errors++;
					if (status & 0x0200) tp->stats.tx_window_errors++;
					if (status & 0x0002) tp->stats.tx_fifo_errors++;
					if ((status & 0x0080) && tp->full_duplex == 0)
						tp->stats.tx_heartbeat_errors++;
#ifdef ETHER_STATS
					if (status & 0x0100) tp->stats.collisions16++;
#endif
				} else {
#ifdef ETHER_STATS
					if (status & 0x0001) tp->stats.tx_deferred++;
#endif
					tp->stats.tx_bytes += tp->tx_ring[entry].length & 0x7ff;
					tp->stats.collisions += (status >> 3) & 15;
					tp->stats.tx_packets++;
				}

				/* Free the original skb. */
				dev_kfree_skb_irq(tp->tx_skbuff[entry]);
				tp->tx_skbuff[entry] = 0;
			}

#ifndef final_version
			if (tp->cur_tx - dirty_tx > TX_RING_SIZE) {
				printk(KERN_ERR "%s: Out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
					   dev->name, dirty_tx, tp->cur_tx, tp->tx_full);
				dirty_tx += TX_RING_SIZE;
			}
#endif

			if (tp->tx_full && 
			    tp->cur_tx - dirty_tx  < TX_RING_SIZE - 2)
				/* The ring is no longer full */
				tp->tx_full = 0;
			
			if (tp->tx_full)
				netif_stop_queue (dev);
			else
				netif_wake_queue (dev);

			tp->dirty_tx = dirty_tx;
			if (csr5 & TxDied) {
				if (tulip_debug > 2)
					printk(KERN_WARNING "%s: The transmitter stopped."
						   "  CSR5 is %x, CSR6 %x, new CSR6 %x.\n",
						   dev->name, csr5, inl(ioaddr + CSR6), tp->csr6);
				outl_CSR6(tp->csr6 | 0x0002, ioaddr, tp->chip_id);
				outl_CSR6(tp->csr6 | 0x2002, ioaddr, tp->chip_id);
			}
		}

		/* Log errors. */
		if (csr5 & AbnormalIntr) {	/* Abnormal error summary bit. */
			if (csr5 == 0xffffffff)
				break;
			if (csr5 & TxJabber) tp->stats.tx_errors++;
			if (csr5 & TxFIFOUnderflow) {
				if ((tp->csr6 & 0xC000) != 0xC000)
					tp->csr6 += 0x4000;	/* Bump up the Tx threshold */
				else
					tp->csr6 |= 0x00200000;  /* Store-n-forward. */
				/* Restart the transmit process. */
				outl_CSR6(tp->csr6 | 0x0002, ioaddr, tp->chip_id);
				outl_CSR6(tp->csr6 | 0x2002, ioaddr, tp->chip_id);
			}
			if (csr5 & RxDied) {		/* Missed a Rx frame. */
				tp->stats.rx_errors++;
				tp->stats.rx_missed_errors += inl(ioaddr + CSR8) & 0xffff;
				outl_CSR6(tp->csr6 | 0x2002, ioaddr, tp->chip_id);
			}
			if (csr5 & TimerInt) {
				if (tulip_debug > 2)
					printk(KERN_ERR "%s: Re-enabling interrupts, %8.8x.\n",
						   dev->name, csr5);
				outl(tulip_tbl[tp->chip_id].valid_intrs, ioaddr + CSR7);
			}
			/* Clear all error sources, included undocumented ones! */
			outl(0x0800f7ba, ioaddr + CSR5);
		}
		if (--work_budget < 0) {
			if (tulip_debug > 1)
				printk(KERN_WARNING "%s: Too much work during an interrupt, "
					   "csr5=0x%8.8x.\n", dev->name, csr5);
			/* Acknowledge all interrupt sources. */
			outl(0x8001ffff, ioaddr + CSR5);
#ifdef notdef
			/* Clear all but standard interrupt sources. */
			outl((~csr5) & 0x0001ebef, ioaddr + CSR7);
#endif
			break;
		}
	} while (1);

	if (tulip_debug > 3)
		printk(KERN_DEBUG "%s: exiting interrupt, csr5=%#4.4x.\n",
			   dev->name, inl(ioaddr + CSR5));

	spin_unlock (&tp->lock);
}

static int
tulip_rx(struct net_device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int entry = tp->cur_rx % RX_RING_SIZE;
	int rx_work_limit = tp->dirty_rx + RX_RING_SIZE - tp->cur_rx;
	int work_done = 0;

	if (tulip_debug > 4)
		printk(KERN_DEBUG " In tulip_rx(), entry %d %8.8x.\n", entry,
			   tp->rx_ring[entry].status);
	/* If we own the next entry, it's a new packet. Send it up. */
	while (tp->rx_ring[entry].status >= 0) {
		s32 status = tp->rx_ring[entry].status;

		if (tulip_debug > 5)
			printk(KERN_DEBUG " In tulip_rx(), entry %d %8.8x.\n", entry,
				   tp->rx_ring[entry].status);
		if (--rx_work_limit < 0)
			break;
		if ((status & 0x38008300) != 0x0300) {
			if ((status & 0x38000300) != 0x0300) {
				/* Ingore earlier buffers. */
				if ((status & 0xffff) != 0x7fff) {
					if (tulip_debug > 1)
						printk(KERN_WARNING "%s: Oversized Ethernet frame "
							   "spanned multiple buffers, status %8.8x!\n",
							   dev->name, status);
					tp->stats.rx_length_errors++;
				}
			} else if (status & RxDescFatalErr) {
				/* There was a fatal error. */
				if (tulip_debug > 2)
					printk(KERN_DEBUG "%s: Receive error, Rx status %8.8x.\n",
						   dev->name, status);
				tp->stats.rx_errors++; /* end of a packet.*/
				if (status & 0x0890) tp->stats.rx_length_errors++;
				if (status & 0x0004) tp->stats.rx_frame_errors++;
				if (status & 0x0002) tp->stats.rx_crc_errors++;
				if (status & 0x0001) tp->stats.rx_fifo_errors++;
			}
		} else {
			/* Omit the four octet CRC from the length. */
			short pkt_len = ((status >> 16) & 0x7ff) - 4;
			struct sk_buff *skb;

#ifndef final_version
			if (pkt_len > 1518) {
				printk(KERN_WARNING "%s: Bogus packet size of %d (%#x).\n",
					   dev->name, pkt_len, pkt_len);
				pkt_len = 1518;
				tp->stats.rx_length_errors++;
			}
#endif
			/* Check if the packet is long enough to accept without copying
			   to a minimally-sized skbuff. */
			if (pkt_len < rx_copybreak
				&& (skb = dev_alloc_skb(pkt_len + 2)) != NULL) {
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align the IP header */
#if ! defined(__alpha__)
				eth_copy_and_sum(skb, bus_to_virt(tp->rx_ring[entry].buffer1),
								 pkt_len, 0);
				skb_put(skb, pkt_len);
#else
				memcpy(skb_put(skb, pkt_len),
					   bus_to_virt(tp->rx_ring[entry].buffer1), pkt_len);
#endif
				work_done++;
			} else { 	/* Pass up the skb already on the Rx ring. */
				char *temp = skb_put(skb = tp->rx_skbuff[entry], pkt_len);
				tp->rx_skbuff[entry] = NULL;
#ifndef final_version
				if (bus_to_virt(tp->rx_ring[entry].buffer1) != temp)
					printk(KERN_ERR "%s: Internal fault: The skbuff addresses "
						   "do not match in tulip_rx: %p vs. %p / %p.\n",
						   dev->name, bus_to_virt(tp->rx_ring[entry].buffer1),
						   skb->head, temp);
#endif
			}
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			tp->stats.rx_packets++;
			tp->stats.rx_bytes += pkt_len;
		}
		entry = (++tp->cur_rx) % RX_RING_SIZE;
	}

	/* Refill the Rx ring buffers. */
	for (; tp->cur_rx - tp->dirty_rx > 0; tp->dirty_rx++) {
		entry = tp->dirty_rx % RX_RING_SIZE;
		if (tp->rx_skbuff[entry] == NULL) {
			struct sk_buff *skb;
			skb = tp->rx_skbuff[entry] = dev_alloc_skb(PKT_BUF_SZ);
			if (skb == NULL)
				break;
			skb->dev = dev;			/* Mark as being used by this device. */
			tp->rx_ring[entry].buffer1 = virt_to_bus(skb->tail);
			work_done++;
		}
		tp->rx_ring[entry].status = DescOwned;
	}

	return work_done;
}

static void
tulip_down(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;

	del_timer_sync(&tp->timer);

	/* Disable interrupts by clearing the interrupt mask. */
	outl(0x00000000, ioaddr + CSR7);
	/* Stop the chip's Tx and Rx processes. */
	outl_CSR6(inl(ioaddr + CSR6) & ~0x2002, ioaddr, tp->chip_id);

	if (inl(ioaddr + CSR6) != 0xffffffff)
		tp->stats.rx_missed_errors += inl(ioaddr + CSR8) & 0xffff;

	dev->if_port = tp->saved_if_port;
}

static int
tulip_close(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int i;

	if (tulip_debug > 1)
		printk(KERN_DEBUG "%s: Shutting down ethercard, status was %2.2x.\n",
			   dev->name, inl(ioaddr + CSR5));

	del_timer_sync(&tp->timer);

	netif_stop_queue(dev);

	if (netif_device_present(dev))
		tulip_down(dev);

	free_irq(dev->irq, dev);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		struct sk_buff *skb = tp->rx_skbuff[i];
		tp->rx_skbuff[i] = 0;
		tp->rx_ring[i].status = 0;		/* Not owned by Tulip chip. */
		tp->rx_ring[i].length = 0;
		tp->rx_ring[i].buffer1 = 0xBADF00D0; /* An invalid address. */
		if (skb) {
			dev_kfree_skb(skb);
		}
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (tp->tx_skbuff[i])
			dev_kfree_skb(tp->tx_skbuff[i]);
		tp->tx_skbuff[i] = 0;
	}

	MOD_DEC_USE_COUNT;
	tp->open = 0;
	return 0;
}

static struct net_device_stats *tulip_get_stats(struct net_device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	long ioaddr = dev->base_addr;

	if (netif_device_present(dev))
		tp->stats.rx_missed_errors += inl(ioaddr + CSR8) & 0xffff;

	return &tp->stats;
}

#ifdef HAVE_PRIVATE_IOCTL
/* Provide ioctl() calls to examine the MII xcvr state. */
static int private_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	u16 *data = (u16 *)&rq->ifr_data;
	int phy = tp->phys[0] & 0x1f;
	long flags;

	switch(cmd) {
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		if (tp->mii_cnt)
			data[0] = phy;
		else
			return -ENODEV;
		return 0;
	case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
		{
			save_flags(flags);
			cli();
			data[3] = mdio_read(dev, data[0] & 0x1f, data[1] & 0x1f);
			restore_flags(flags);
		}
		return 0;
	case SIOCDEVPRIVATE+2:		/* Write the specified MII register */
#if defined(CAP_NET_ADMIN)
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
#else
		if (!suser())
			return -EPERM;
#endif
		{
			save_flags(flags);
			cli();
			mdio_write(dev, data[0] & 0x1f, data[1] & 0x1f, data[2]);
			restore_flags(flags);
		}
		return 0;
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}
#endif  /* HAVE_PRIVATE_IOCTL */

/* Set or clear the multicast filter for this adaptor.
   Note that we only use exclusion around actually queueing the
   new frame, not around filling tp->setup_frame.  This is non-deterministic
   when re-entered but still correct. */

/* The little-endian AUTODIN32 ethernet CRC calculation.
   N.B. Do not use for bulk data, use a table-based routine instead.
   This is common code and should be moved to net/core/crc.c */
static unsigned const ethernet_polynomial_le = 0xedb88320U;
static inline u32 ether_crc_le(int length, unsigned char *data)
{
	u32 crc = 0xffffffff;	/* Initial value. */
	while(--length >= 0) {
		unsigned char current_octet = *data++;
		int bit;
		for (bit = 8; --bit >= 0; current_octet >>= 1) {
			if ((crc ^ current_octet) & 1) {
				crc >>= 1;
				crc ^= ethernet_polynomial_le;
			} else
				crc >>= 1;
		}
	}
	return crc;
}
static unsigned const ethernet_polynomial = 0x04c11db7U;
static inline u32 ether_crc(int length, unsigned char *data)
{
    int crc = -1;

    while(--length >= 0) {
		unsigned char current_octet = *data++;
		int bit;
		for (bit = 0; bit < 8; bit++, current_octet >>= 1)
			crc = (crc << 1) ^
				((crc < 0) ^ (current_octet & 1) ? ethernet_polynomial : 0);
    }
    return crc;
}

static void set_rx_mode(struct net_device *dev)
{
	long ioaddr = dev->base_addr;
	int csr6 = inl(ioaddr + CSR6) & ~0x00D5;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;

	tp->csr6 &= ~0x00D5;
	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		tp->csr6 |= 0x00C0;
		csr6 |= 0x00C0;
		/* Unconditionally log net taps. */
		printk(KERN_INFO "%s: Promiscuous mode enabled.\n", dev->name);
	} else if ((dev->mc_count > 1000)  ||  (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter well -- accept all multicasts. */
		tp->csr6 |= 0x0080;
		csr6 |= 0x0080;
	} else {
		u16 *eaddrs, *setup_frm = tp->setup_frame;
		struct dev_mc_list *mclist;
		u32 tx_flags = 0x68000000 | 192;
		int i;

		/* Note that only the low-address shortword of setup_frame is valid!
		   The values are doubled for big-endian architectures. */
		if (dev->mc_count > 14) { /* Must use a multicast hash table. */
			u16 hash_table[32];
			tx_flags = 0x68400000 | 192;		/* Use hash filter. */
			memset(hash_table, 0, sizeof(hash_table));
			set_bit(255, hash_table); 			/* Broadcast entry */
			/* This should work on big-endian machines as well. */
			for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
				 i++, mclist = mclist->next)
				set_bit(ether_crc_le(ETH_ALEN, mclist->dmi_addr) & 0x1ff,
						hash_table);
			for (i = 0; i < 32; i++) {
				*setup_frm++ = hash_table[i];
				*setup_frm++ = hash_table[i];
			}
			setup_frm = &tp->setup_frame[13*6];
		} else {
			/* We have <= 14 addresses so we can use the wonderful
			   16 address perfect filtering of the Tulip. */
			for (i = 0, mclist = dev->mc_list; i < dev->mc_count;
				 i++, mclist = mclist->next) {
				eaddrs = (u16 *)mclist->dmi_addr;
				*setup_frm++ = eaddrs[0]; *setup_frm++ = eaddrs[0];
				*setup_frm++ = eaddrs[1]; *setup_frm++ = eaddrs[1];
				*setup_frm++ = eaddrs[2]; *setup_frm++ = eaddrs[2];
			}
			/* Fill the unused entries with the broadcast address. */
			memset(setup_frm, 0xff, (15-i)*12);
			setup_frm = &tp->setup_frame[15*6];
		}

		/* Fill the final entry with our physical address. */
		eaddrs = (u16 *)dev->dev_addr;
		*setup_frm++ = eaddrs[0]; *setup_frm++ = eaddrs[0];
		*setup_frm++ = eaddrs[1]; *setup_frm++ = eaddrs[1];
		*setup_frm++ = eaddrs[2]; *setup_frm++ = eaddrs[2];
		/* Now add this frame to the Tx list. */
		if (tp->cur_tx - tp->dirty_tx > TX_RING_SIZE - 2) {
			/* Same setup recently queued, we need not add it. */
		} else {
			unsigned long flags;
			unsigned int entry;
			int dummy = -1;

			save_flags(flags); cli();
			entry = tp->cur_tx++ % TX_RING_SIZE;

			if (entry != 0) {
				/* Avoid a chip errata by prefixing a dummy entry. */
				tp->tx_skbuff[entry] = 0;
				tp->tx_ring[entry].length =
					(entry == TX_RING_SIZE-1) ? DESC_RING_WRAP : 0;
				tp->tx_ring[entry].buffer1 = 0;
				/* race with chip, set DescOwned later */
				dummy = entry;
				entry = tp->cur_tx++ % TX_RING_SIZE;
			}

			tp->tx_skbuff[entry] = 0;
			/* Put the setup frame on the Tx list. */
			if (entry == TX_RING_SIZE-1)
				tx_flags |= DESC_RING_WRAP;		/* Wrap ring. */
			tp->tx_ring[entry].length = tx_flags;
			tp->tx_ring[entry].buffer1 = virt_to_bus(tp->setup_frame);
			tp->tx_ring[entry].status = DescOwned;
			if (tp->cur_tx - tp->dirty_tx >= TX_RING_SIZE - 2) {
				tp->tx_full = 1;
				netif_stop_queue (dev);
			}
			if (dummy >= 0)
				tp->tx_ring[dummy].status = DescOwned;
			restore_flags(flags);
			/* Trigger an immediate transmit demand. */
			outl(0, ioaddr + CSR1);
		}
	}
	outl_CSR6(csr6 | 0x0000, ioaddr, tp->chip_id);
}

static  struct pci_device_id tulip_pci_table[] __devinitdata = {
  { 0x115D, 0x0003, PCI_ANY_ID, PCI_ANY_ID, 0, 0, X3201_3 },
  {0},
};

MODULE_DEVICE_TABLE(pci, tulip_pci_table);

static int __devinit tulip_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct net_device *dev;
	static int board_idx;

	printk(KERN_INFO "tulip_attach(%s)\n", pdev->slot_name);

	if (pci_enable_device (pdev))
		return -ENODEV;
	pci_set_master (pdev);
	dev = tulip_probe1(pdev, pci_resource_start (pdev, 0), pdev->irq,
			   id->driver_data, board_idx++);
	if (dev) {
		pdev->driver_data = dev;
		return 0;
	}
	return -ENODEV;
}

static int tulip_suspend(struct pci_dev *pdev, u32 state)
{
	struct net_device *dev = pdev->driver_data;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	printk(KERN_INFO "tulip_suspend(%s)\n", dev->name);
	if (tp->open) tulip_down(dev);
	return 0;
}

static int tulip_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pdev->driver_data;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	printk(KERN_INFO "tulip_resume(%s)\n", dev->name);
	if (tp->open) tulip_up(dev);
	return 0;
}

static void __devexit tulip_remove(struct pci_dev *pdev)
{
	struct net_device *dev = pdev->driver_data;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;

	printk(KERN_INFO "tulip_detach(%s)\n", dev->name);
	unregister_netdev(dev);
	kfree(dev);
	kfree(tp);
}

static struct pci_driver tulip_ops = {
	name:		"tulip_cb",
	id_table:	tulip_pci_table,
	probe:		tulip_pci_probe,
	remove:		tulip_remove,
	suspend:	tulip_suspend,
	resume:		tulip_resume
};

static int __init tulip_init(void)
{
	pci_register_driver(&tulip_ops);
	return 0;
}

static void __exit tulip_exit(void)
{
	pci_unregister_driver(&tulip_ops);
}

module_init(tulip_init)
module_exit(tulip_exit)


/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -c tulip.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  cardbus-compile-command: "gcc -DCARDBUS -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -c tulip.c -o tulip_cb.o -I/usr/src/pcmcia-cs-3.0.9/include/"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
