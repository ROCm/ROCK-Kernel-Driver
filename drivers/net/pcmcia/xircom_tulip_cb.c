/* tulip.c: A DEC 21040-family ethernet driver for Linux. */
/*
	Written/copyright 1994-1999 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	This driver is for the Digital "Tulip" Ethernet adapter interface.
	It should work with most DEC 21*4*-based chips/ethercards, as well as
	with work-alike chips from Lite-On (PNIC) and Macronix (MXIC) and ASIX.

	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
	Center of Excellence in Space Data and Information Sciences
	   Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771

	Support and updates available at
	http://cesdis.gsfc.nasa.gov/linux/drivers/tulip.html
*/

#define SMP_CHECK
#define CARDBUS 1
static const char version[] = "xircom_tulip_cb.c:v0.91 4/14/99 becker@cesdis.gsfc.nasa.gov (modified by danilo@cs.uni-magdeburg.de for XIRCOM CBE, fixed by Doug Ledford)\n";

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

#if defined(__alpha__)
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
/* This is a mysterious value that can be written to CSR11 in the 21040 (only)
   to support a pre-NWay full-duplex signaling mechanism using short frames.
   No one knows what it should be, but if left at its default value some
   10base2(!) packets trigger a full-duplex-request interrupt. */
#define FULL_DUPLEX_MAGIC	0x6969

#if !defined(__OPTIMIZE__)  ||  !defined(__KERNEL__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error You must compile this driver with "-O".
#endif

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/unaligned.h>

/* Kernel compatibility defines, some common to David Hinds' PCMCIA package.
   This is only in the support-all-kernels source code. */

MODULE_AUTHOR("Donald Becker <becker@cesdis.gsfc.nasa.gov>");
MODULE_DESCRIPTION("Digital 21*4* Tulip ethernet driver");
MODULE_PARM(debug, "i");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(reverse_probe, "i");
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

Some boards have EEPROMs tables with default media entry.  The factory default
is usually "autoselect".  This should only be overridden when using
transceiver connections without link beat e.g. 10base2 or AUI, or (rarely!)
for forcing full-duplex when used with old link partners that do not do
autonegotiation. 

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

Thanks to Duke Kamstra of SMC for long ago providing an EtherPower board.
Greg LaPolla at Linksys provided PNIC and other Linksys boards.
Znyx provided a four-port card for testing.

IVb. References

http://cesdis.gsfc.nasa.gov/linux/misc/NWay.html
http://www.digital.com  (search for current 21*4* datasheets and "21X4 SROM")
http://www.national.com/pf/DP/DP83840A.html
http://www.asix.com.tw/pmac.htm
http://www.admtek.com.tw/

IVc. Errata

The old DEC databooks were light on details.
The 21040 databook claims that CSR13, CSR14, and CSR15 should each be the last
register of the set CSR12-15 written.  Hmmm, now how is that possible?

The DEC SROM format is very badly designed not precisely defined, leading to
part of the media selection junkheap below.  Some boards do not have EEPROM
media tables and need to be patched up.  Worse, other boards use the DEC
design kit media table when it isn't correct for their board.

We cannot use MII interrupts because there is no defined GPIO pin to attach
them.  The MII transceiver status is polled using an kernel timer.

*/

/* This table use during operation for capabilities and media timer. */

static void tulip_timer(unsigned long data);
static void t21142_timer(unsigned long data);
static void mxic_timer(unsigned long data);
static void pnic_timer(unsigned long data);
static void comet_timer(unsigned long data);

enum tbl_flag {
	HAS_MII=1, HAS_MEDIA_TABLE=2, CSR12_IN_SROM=4, ALWAYS_CHECK_MII=8,
	HAS_ACPI=0x10, MC_HASH_ONLY=0x20, /* Hash-only multicast filter. */
	HAS_NWAY143=0x40,			/* Uses 21143-like internal NWay. */
};
static struct tulip_chip_table {
	char *chip_name;
	int io_size;
	int valid_intrs;			/* CSR7 interrupt enable settings */
	int flags;
	void (*media_timer)(unsigned long data);
} tulip_tbl[] = {
  { "Digital DC21040 Tulip", 128, 0x0001ebef, 0, tulip_timer },
  { "Digital DC21041 Tulip", 128, 0x0001ebef, HAS_MEDIA_TABLE, tulip_timer },
  { "Digital DS21140 Tulip", 128, 0x0001ebef,
	HAS_MII | HAS_MEDIA_TABLE | CSR12_IN_SROM, tulip_timer },
  { "Digital DS21143 Tulip", 128, 0x0801fbff,
	HAS_MII | HAS_MEDIA_TABLE | ALWAYS_CHECK_MII | HAS_ACPI | HAS_NWAY143, t21142_timer },
  { "Lite-On 82c168 PNIC", 256, 0x0001ebef,
	HAS_MII, pnic_timer },
  { "Macronix 98713 PMAC", 128, 0x0001ebef,
	HAS_MII | HAS_MEDIA_TABLE | CSR12_IN_SROM, mxic_timer },
  { "Macronix 98715 PMAC", 256, 0x0001ebef,
	HAS_MEDIA_TABLE, mxic_timer },
  { "Macronix 98725 PMAC", 256, 0x0001ebef,
	HAS_MEDIA_TABLE, mxic_timer },
  { "ASIX AX88140", 128, 0x0001fbff,
	HAS_MII | HAS_MEDIA_TABLE | CSR12_IN_SROM | MC_HASH_ONLY, tulip_timer },
  { "Lite-On PNIC-II", 256, 0x0001ebef,
	HAS_MII | HAS_NWAY143, pnic_timer },
  { "ADMtek Comet", 256, 0x0001abef,
	MC_HASH_ONLY, comet_timer },
  { "Compex 9881 PMAC", 128, 0x0001ebef,
	HAS_MII | HAS_MEDIA_TABLE | CSR12_IN_SROM, mxic_timer },
  { "Xircom Cardbus Adapter (DEC 21143 compatible mode)", 128, 0x0801fbff,
       HAS_MII | HAS_ACPI, tulip_timer }, 
  {0},
};
/* This matches the table above.  Note 21142 == 21143. */
enum chips {
	DC21040=0, DC21041=1, DC21140=2, DC21142=3, DC21143=3,
	LC82C168, MX98713, MX98715, MX98725, AX88140, PNIC2, COMET, COMPEX9881,
	X3201_3,
};

/* A full-duplex map for media types. */
enum MediaIs {
	MediaIsFD = 1, MediaAlwaysFD=2, MediaIsMII=4, MediaIsFx=8,
	MediaIs100=16};
static const char media_cap[] =
{0,0,0,16,  3,19,16,24,  27,4,7,5, 0,20,23,20 };
static u8 t21040_csr13[] = {2,0x0C,8,4,  4,0,0,0, 0,0,0,0, 4,0,0,0};
/* 21041 transceiver register settings: 10-T, 10-2, AUI, 10-T, 10T-FD*/
static u16 t21041_csr13[] = { 0xEF05, 0xEF09, 0xEF09, 0xEF01, 0xEF09, };
static u16 t21041_csr14[] = { 0x7F3F, 0xF7FD, 0xF7FD, 0x7F3F, 0x7F3D, };
static u16 t21041_csr15[] = { 0x0008, 0x0006, 0x000E, 0x0008, 0x0008, };

static u16 t21142_csr13[] = { 0x0001, 0x0009, 0x0009, 0x0000, 0x0001, };
static u16 t21142_csr14[] = { 0xFFFF, 0x0705, 0x0705, 0x0000, 0x7F3D, };
static u16 t21142_csr15[] = { 0x0008, 0x0006, 0x000E, 0x0008, 0x0008, };

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

#ifdef CARDBUS
#define EEPROM_ADDRLEN (chip_rev == 65 ? 8 : 6)
#else
#define EEPROM_ADDRLEN 6
#endif
#define EEPROM_SIZE 128 	/* 2 << EEPROM_ADDRLEN */

struct medialeaf {
	u8 type;
	u8 media;
	unsigned char *leafdata;
};

struct mediatable {
	u16 defaultmedia;
	u8 leafcount, csr12dir;				/* General purpose pin directions. */
	unsigned has_mii:1, has_nonmii:1, has_reset:6;
	u32 csr15dir, csr15val;				/* 21143 NWay setting. */
	struct medialeaf mleaf[0];
};

struct mediainfo {
	struct mediainfo *next;
	int info_type;
	int index;
	unsigned char *info;
};

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
	unsigned int fake_addr:1;			/* Multiport board faked address. */
	unsigned int default_port:4;		/* Last dev->if_port value. */
	unsigned int media2:4;				/* Secondary monitored media port. */
	unsigned int medialock:1;			/* Don't sense media type. */
	unsigned int mediasense:1;			/* Media sensing in progress. */
	unsigned int nway:1, nwayset:1;		/* 21143 internal NWay. */
	unsigned int open:1;
	unsigned int csr0;					/* CSR0 setting. */
	unsigned int csr6;					/* Current CSR6 control settings. */
	unsigned char eeprom[EEPROM_SIZE];	/* Serial EEPROM contents. */
	u16 to_advertise;					/* NWay capabilities advertised.  */
	u16 lpar;							/* 21143 Link partner ability. */
	u16 advertising[4];
	signed char phys[4], mii_cnt;		/* MII device addresses. */
	struct mediatable *mtable;
	int cur_index;						/* Current media index. */
	int saved_if_port;
	struct pci_dev *pdev;
	spinlock_t lock;
	int pad0, pad1;						/* Used for 8-byte alignment */
};

static void parse_eeprom(struct net_device *dev);
static int read_eeprom(long ioaddr, int location, int addr_len);
static int mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int value);
static void select_media(struct net_device *dev, int startup);
static void tulip_up(struct net_device *dev);
static void tulip_down(struct net_device *dev);
static int tulip_open(struct net_device *dev);
static void tulip_timer(unsigned long data);
static void t21142_start_nway(struct net_device *dev);
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
    if (chip_idx != X3201_3) {
		outl(newcsr6, ioaddr + CSR6);
		restore_flags(flags);
		return;
    }
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
				       struct net_device *dev, long ioaddr, int irq,
				       int chip_idx, int board_idx)
{
	static int did_version;			/* Already printed version info. */
	struct tulip_private *tp;
	/* See note below on the multiport cards. */
	static unsigned char last_phys_addr[6] = {0x00, 'L', 'i', 'n', 'u', 'x'};
	static int last_irq;
	static int multiport_cnt;		/* For four-port boards w/one EEPROM */
	u8 chip_rev;
	int i;
	unsigned short sum;

	if (tulip_debug > 0  &&  did_version++ == 0)
		printk(KERN_INFO "%s", version);

	dev = init_etherdev(dev, 0);

	pci_read_config_byte(pdev, PCI_REVISION_ID, &chip_rev);
	/* Bring the 21143 out of sleep mode.
	   Caution: Snooze mode does not work with some boards! */
	if (tulip_tbl[chip_idx].flags & HAS_ACPI)
		pci_write_config_dword(pdev, 0x40, 0x00000000);

	printk(KERN_INFO "%s: %s rev %d at %#3lx,",
		   dev->name, tulip_tbl[chip_idx].chip_name, chip_rev, ioaddr);

	/* Stop the chip's Tx and Rx processes. */
	outl_CSR6(inl(ioaddr + CSR6) & ~0x2002, ioaddr, chip_idx);
	/* Clear the missed-packet counter. */
	(volatile int)inl(ioaddr + CSR8);

	if (chip_idx == DC21041) {
		if (inl(ioaddr + CSR9) & 0x8000) {
			printk(" 21040 compatible mode,");
			chip_idx = DC21040;
		} else {
			printk(" 21041 mode,");
		}
	}

	/* The station address ROM is read byte serially.  The register must
	   be polled, waiting for the value to be read bit serially from the
	   EEPROM.
	   */
	sum = 0;
	if (chip_idx == DC21040) {
		outl(0, ioaddr + CSR9);		/* Reset the pointer with a dummy write. */
		for (i = 0; i < 6; i++) {
			int value, boguscnt = 100000;
			do
				value = inl(ioaddr + CSR9);
			while (value < 0  && --boguscnt > 0);
			dev->dev_addr[i] = value;
			sum += value & 0xff;
		}
	} else if (chip_idx == LC82C168) {
		for (i = 0; i < 3; i++) {
			int value, boguscnt = 100000;
			outl(0x600 | i, ioaddr + 0x98);
			do
				value = inl(ioaddr + CSR9);
			while (value < 0  && --boguscnt > 0);
			put_unaligned(le16_to_cpu(value), ((u16*)dev->dev_addr) + i);
			sum += value & 0xffff;
		}
	} else if (chip_idx == COMET) {
		/* No need to read the EEPROM. */
		put_unaligned(inl(ioaddr + 0xA4), (u32 *)dev->dev_addr);
		put_unaligned(inl(ioaddr + 0xA8), (u16 *)(dev->dev_addr + 4));
		for (i = 0; i < 6; i ++)
			sum += dev->dev_addr[i];
	} else if (chip_idx == X3201_3) {
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
		sum = 1; // to make check below fail!
	} else {	/* Must be a new chip, with a serial EEPROM interface. */
		/* We read the whole EEPROM, and sort it out later.  DEC has a
		   specification _Digital Semiconductor 21X4 Serial ROM Format_
		   but early vendor boards just put the address in the first six
		   EEPROM locations. */
		unsigned char ee_data[EEPROM_SIZE];
		int sa_offset = 0;

		for (i = 0; i < sizeof(ee_data)/2; i++)
			((u16 *)ee_data)[i] =
				le16_to_cpu(read_eeprom(ioaddr, i, EEPROM_ADDRLEN));

		/* Detect the simple EEPROM format by the duplicated station addr. */
		for (i = 0; i < 8; i ++)
			if (ee_data[i] != ee_data[16+i])
				sa_offset = 20;
		if (ee_data[0] == 0xff  &&  ee_data[1] == 0xff &&  ee_data[2] == 0) {
			sa_offset = 2;		/* Grrr, damn Matrox boards. */
			multiport_cnt = 4;
		}
		for (i = 0; i < 6; i ++) {
			dev->dev_addr[i] = ee_data[i + sa_offset];
			sum += ee_data[i + sa_offset];
		}
	}
	/* Lite-On boards have the address byte-swapped. */
	if ((dev->dev_addr[0] == 0xA0  ||  dev->dev_addr[0] == 0xC0)
		&&  dev->dev_addr[1] == 0x00)
		for (i = 0; i < 6; i+=2) {
			char tmp = dev->dev_addr[i];
			dev->dev_addr[i] = dev->dev_addr[i+1];
			dev->dev_addr[i+1] = tmp;
		}
	/* On the Zynx 315 Etherarray and other multiport boards only the
	   first Tulip has an EEPROM.
	   The addresses of the subsequent ports are derived from the first.
	   Many PCI BIOSes also incorrectly report the IRQ line, so we correct
	   that here as well. */
	if (sum == 0  || sum == 6*0xff) {
		printk(" EEPROM not present,");
		for (i = 0; i < 5; i++)
			dev->dev_addr[i] = last_phys_addr[i];
		dev->dev_addr[i] = last_phys_addr[i] + 1;
#if defined(__i386__)		/* Patch up x86 BIOS bug. */
		if (last_irq)
			irq = last_irq;
#endif
	}

	for (i = 0; i < 6; i++)
		printk("%c%2.2X", i ? ':' : ' ', last_phys_addr[i] = dev->dev_addr[i]);
	printk(", IRQ %d.\n", irq);
	last_irq = irq;

	/* We do a request_region() only to register /proc/ioports info. */
	/* Note that proper size is tulip_tbl[chip_idx].chip_name, but... */
	request_region(ioaddr, tulip_tbl[chip_idx].io_size, dev->name);

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
	if ( (chip_idx == DC21143  &&  chip_rev == 65) ||
	     (chip_idx == X3201_3) )
		tp->csr0 &= ~0x01000000;
	else if (chip_idx == AX88140)
		tp->csr0 |= 0x2000;

#ifdef TULIP_FULL_DUPLEX
	tp->full_duplex = 1;
	tp->full_duplex_lock = 1;
#endif
#ifdef TULIP_DEFAULT_MEDIA
	tp->default_port = TULIP_DEFAULT_MEDIA;
#endif
#ifdef TULIP_NO_MEDIA_SWITCH
	tp->medialock = 1;
#endif

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

	/* This is logically part of probe1(), but too complex to write inline. */
	if (tulip_tbl[chip_idx].flags & HAS_MEDIA_TABLE)
		parse_eeprom(dev);

	if (media_cap[tp->default_port] & MediaIsMII) {
		u16 media2advert[] = { 0x20, 0x40, 0x03e0, 0x60, 0x80, 0x100, 0x200 };
		tp->to_advertise = media2advert[tp->default_port - 9];
	} else
		tp->to_advertise = 0x03e1;

	if ((tulip_tbl[chip_idx].flags & ALWAYS_CHECK_MII) ||
		(tp->mtable  &&  tp->mtable->has_mii) ||
		( ! tp->mtable  &&  (tulip_tbl[chip_idx].flags & HAS_MII))) {
		int phy, phy_idx;
		if (tp->mtable  &&  tp->mtable->has_mii) {
			for (i = 0; i < tp->mtable->leafcount; i++)
				if (tp->mtable->mleaf[i].media == 11) {
					tp->cur_index = i;
					tp->saved_if_port = dev->if_port;
					select_media(dev, 1);
					dev->if_port = tp->saved_if_port;
					break;
				}
		}
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
				printk(KERN_INFO "%s:  MII transceiver #%d "
					   "config %4.4x status %4.4x advertising %4.4x.\n",
					   dev->name, phy, mii_reg0, mii_status, mii_advert);
				/* Fixup for DLink with miswired PHY. */
				if (mii_advert != reg4) {
					printk(KERN_DEBUG "%s:  Advertising %4.4x on PHY %d,"
						   " previously advertising %4.4x.\n",
						   dev->name, reg4, phy, mii_advert);
					mdio_write(dev, phy, 4, reg4);
				}
				/* Enable autonegotiation: some boards default to off. */
				mdio_write(dev, phy, 0, mii_reg0 |
						   (tp->full_duplex ? 0x1100 : 0x1000) |
						   (media_cap[tp->default_port]&MediaIs100 ? 0x2000:0));
			}
		}
		tp->mii_cnt = phy_idx;
		if (tp->mtable  &&  tp->mtable->has_mii  &&  phy_idx == 0) {
			printk(KERN_INFO "%s: ***WARNING***: No MII transceiver found!\n",
				   dev->name);
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
	case DC21041:
		outl(0x00000000, ioaddr + CSR13);
		outl(0xFFFFFFFF, ioaddr + CSR14);
		outl(0x00000008, ioaddr + CSR15); /* Listen on AUI also. */
		outl_CSR6(inl(ioaddr + CSR6) | 0x0200, ioaddr, chip_idx);
		outl(0x0000EF05, ioaddr + CSR13);
		break;
	case DC21040:
		outl(0x00000000, ioaddr + CSR13);
		outl(0x00000004, ioaddr + CSR13);
		break;
	case DC21140: default:
		if (tp->mtable)
			outl(tp->mtable->csr12dir | 0x100, ioaddr + CSR12);
		break;
	case DC21142:
	case PNIC2:
		if (tp->mii_cnt  ||  media_cap[dev->if_port] & MediaIsMII) {
			outl_CSR6(0x82020000, ioaddr, chip_idx);
			outl(0x0000, ioaddr + CSR13);
			outl(0x0000, ioaddr + CSR14);
			outl_CSR6(0x820E0000, ioaddr, chip_idx);
		} else {
			outl_CSR6(0x82420200, ioaddr, chip_idx);
			outl(0x0001, ioaddr + CSR13);
			outl(0x0003FFFF, ioaddr + CSR14);
			outl(0x0008, ioaddr + CSR15);
			outl(0x0001, ioaddr + CSR13);
			outl(0x1301, ioaddr + CSR12); /* Start NWay. */
		}
		break;
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
	case LC82C168:
		if ( ! tp->mii_cnt) {
			outl_CSR6(0x00420000, ioaddr, chip_idx);
			outl(0x30, ioaddr + CSR12);
			outl(0x0001F078, ioaddr + 0xB8);
			outl(0x0201F078, ioaddr + 0xB8); /* Turn on autonegotiation. */
		}
		break;
	case MX98713: case COMPEX9881:
		outl_CSR6(0x00000000, ioaddr, chip_idx);
		outl(0x000711C0, ioaddr + CSR14); /* Turn on NWay. */
		outl(0x00000001, ioaddr + CSR13);
		break;
	case MX98715: case MX98725:
		outl_CSR6(0x01a80000, ioaddr, chip_idx);
		outl(0xFFFFFFFF, ioaddr + CSR14);
		outl(0x00001000, ioaddr + CSR12);
		break;
	case COMET:
		/* No initialization necessary. */
		break;
	}

	return dev;
}

/* Serial EEPROM section. */
/* The main routine to parse the very complicated SROM structure.
   Search www.digital.com for "21X4 SROM" to get details.
   This code is very complex, and will require changes to support
   additional cards, so I'll be verbose about what is going on.
   */

/* Known cards that have old-style EEPROMs. */
static struct fixups {
  char *name;
  unsigned char addr0, addr1, addr2;
  u16 newtable[32];				/* Max length below. */
} eeprom_fixups[] = {
  {"Asante", 0, 0, 0x94, {0x1e00, 0x0000, 0x0800, 0x0100, 0x018c,
						  0x0000, 0x0000, 0xe078, 0x0001, 0x0050, 0x0018 }},
  {"SMC9332DST", 0, 0, 0xC0, { 0x1e00, 0x0000, 0x0800, 0x021f,
							   0x0000, 0x009E, /* 10baseT */
							   0x0903, 0x006D, /* 100baseTx */ }},
  {"Cogent EM100", 0, 0, 0x92, { 0x1e00, 0x0000, 0x0800, 0x033f,
								 0x0107, 0x8021, /* 100baseFx */
								 0x0108, 0x8021, /* 100baseFx-FD */
								 0x0103, 0x006D, /* 100baseTx */ }},
  {"Maxtech NX-110", 0, 0, 0xE8, { 0x1e00, 0x0000, 0x0800, 0x0313,
							   0x1001, 0x009E, /* 10base2, CSR12 0x10*/
							   0x0000, 0x009E, /* 10baseT */
							   0x0303, 0x006D, /* 100baseTx, CSR12 0x03 */ }},
  {"Accton EN1207", 0, 0, 0xE8, { 0x1e00, 0x0000, 0x0800, 0x031F,
							0x1B01, 0x0000, /* 10base2,   CSR12 0x1B */
							0x1B03, 0x006D, /* 100baseTx, CSR12 0x1B */
							0x0B00, 0x009E, /* 10baseT,   CSR12 0x0B */
   }},
  {0, 0, 0, 0, {}}};

static const char * block_name[] = {"21140 non-MII", "21140 MII PHY",
 "21142 Serial PHY", "21142 MII PHY", "21143 SYM PHY", "21143 reset method"};

#if defined(__i386__)			/* AKA get_unaligned() */
#define get_u16(ptr) (*(u16 *)(ptr))
#else
#define get_u16(ptr) (((u8*)(ptr))[0] + (((u8*)(ptr))[1]<<8))
#endif

static void parse_eeprom(struct net_device *dev)
{
	/* The last media info list parsed, for multiport boards.  */
	static struct mediatable *last_mediatable;
	static unsigned char *last_ee_data;
	static int controller_index;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	long ioaddr = dev->base_addr;
	unsigned char *ee_data = tp->eeprom;
	int i;
#ifdef CARDBUS
	int chip_rev = tp->revision;
#endif

	tp->mtable = 0;
	for (i = 0; i < EEPROM_SIZE/2; i++)
		((u16 *)ee_data)[i] =
			le16_to_cpu(read_eeprom(ioaddr, i, EEPROM_ADDRLEN));

	/* Detect an old-style (SA only) EEPROM layout:
	   memcmp(eedata, eedata+16, 8). */
	for (i = 0; i < 8; i ++)
		if (ee_data[i] != ee_data[16+i])
			break;
	if (i >= 8) {
		if (ee_data[0] == 0xff) {
			if (last_mediatable) {
				controller_index++;
				printk(KERN_INFO "%s:  Controller %d of multiport board.\n",
					   dev->name, controller_index);
				tp->mtable = last_mediatable;
				ee_data = last_ee_data;
				goto subsequent_board;
			} else
				printk(KERN_INFO "%s:  Missing EEPROM, this interface may "
					   "not work correctly!\n",
			   dev->name);
			return;
		}
	  /* Do a fix-up based on the vendor half of the station address prefix. */
	  for (i = 0; eeprom_fixups[i].name; i++) {
		if (dev->dev_addr[0] == eeprom_fixups[i].addr0
			&&  dev->dev_addr[1] == eeprom_fixups[i].addr1
			&&  dev->dev_addr[2] == eeprom_fixups[i].addr2) {
		  if (dev->dev_addr[2] == 0xE8  &&  ee_data[0x1a] == 0x55)
			  i++;			/* An Accton EN1207, not an outlaw Maxtech. */
		  memcpy(ee_data + 26, eeprom_fixups[i].newtable,
				 sizeof(eeprom_fixups[i].newtable));
		  printk(KERN_INFO "%s: Old format EEPROM on '%s' board.  Using"
				 " substitute media control info.\n",
				 dev->name, eeprom_fixups[i].name);
		  break;
		}
	  }
	  if (eeprom_fixups[i].name == NULL) { /* No fixup found. */
		  printk(KERN_INFO "%s: Old style EEPROM with no media selection "
				 "information.\n",
			   dev->name);
		return;
	  }
	}

	controller_index = 0;
	if (ee_data[19] > 1) {		/* Multiport board. */
		last_ee_data = ee_data;
	}
subsequent_board:

	if (ee_data[27] == 0) {		/* No valid media table. */
	} else if (tp->chip_id == DC21041) {
		unsigned char *p = (void *)ee_data + ee_data[27 + controller_index*3];
		short media;
		int count;

		media = get_u16(p);
		p += 2;
		count = *p++;

		printk(KERN_INFO "%s:21041 Media information at %d, default media "
			   "%4.4x (%s).\n", dev->name, ee_data[27], media,
			   media & 0x0800 ? "Autosense" : medianame[media & 15]);
		for (i = 0; i < count; i++) {
			unsigned char media_code = *p++;
			u16 csrvals[3];
			int idx;
			for (idx = 0; idx < 3; idx++) {
				csrvals[idx] = get_u16(p);
				p += 2;
			}
			if (media_code & 0x40) {
				printk(KERN_INFO "%s:  21041 media %2.2x (%s),"
					   " csr13 %4.4x csr14 %4.4x csr15 %4.4x.\n",
					   dev->name, media_code & 15, medianame[media_code & 15],
					   csrvals[0], csrvals[1], csrvals[2]);
			} else
				printk(KERN_INFO "%s:  21041 media #%d, %s.\n",
					   dev->name, media_code & 15, medianame[media_code & 15]);
		}
	} else {
		unsigned char *p = (void *)ee_data + ee_data[27];
		unsigned char csr12dir = 0;
		int count;
		struct mediatable *mtable;
		u16 media = get_u16(p);

		p += 2;
		if (tulip_tbl[tp->chip_id].flags & CSR12_IN_SROM)
			csr12dir = *p++;
		count = *p++;
		mtable = (struct mediatable *)
			kmalloc(sizeof(struct mediatable) + count*sizeof(struct medialeaf),
					GFP_KERNEL);
		if (mtable == NULL)
			return;				/* Horrible, impossible failure. */
		last_mediatable = tp->mtable = mtable;
		mtable->defaultmedia = media;
		mtable->leafcount = count;
		mtable->csr12dir = csr12dir;
		mtable->has_nonmii = mtable->has_mii = mtable->has_reset = 0;
		mtable->csr15dir = mtable->csr15val = 0;

		printk(KERN_INFO "%s:  EEPROM default media type %s.\n", dev->name,
			   media & 0x0800 ? "Autosense" : medianame[media & 15]);
		for (i = 0; i < count; i++) {
			struct medialeaf *leaf = &mtable->mleaf[i];

			if ((p[0] & 0x80) == 0) { /* 21140 Compact block. */
				leaf->type = 0;
				leaf->media = p[0] & 0x3f;
				leaf->leafdata = p;
				if ((p[2] & 0x61) == 0x01)	/* Bogus, but Znyx boards do it. */
					mtable->has_mii = 1;
				p += 4;
			} else {
				leaf->type = p[1];
				if (p[1] == 0x05) {
					mtable->has_reset = i;
					leaf->media = p[2] & 0x0f;
				} else if (p[1] & 1) {
					mtable->has_mii = 1;
					leaf->media = 11;
				} else {
					mtable->has_nonmii = 1;
					leaf->media = p[2] & 0x0f;
					if (p[1] == 2) {
						if (leaf->media == 0) {
							mtable->csr15dir = get_unaligned((u16*)&p[3])<<16;
							mtable->csr15val = get_unaligned((u16*)&p[5])<<16;
						} else if (leaf->media == 0x40) {
							u32 base15 = get_unaligned((u16*)&p[7]);
							mtable->csr15dir =
								(get_unaligned((u16*)&p[9])<<16) + base15;
							mtable->csr15val =
								(get_unaligned((u16*)&p[11])<<16) + base15;
						}
					}
				}
				leaf->leafdata = p + 2;
				p += (p[0] & 0x3f) + 1;
			}
			if (tulip_debug > 1  &&  leaf->media == 11) {
				unsigned char *bp = leaf->leafdata;
				printk(KERN_INFO "%s:  MII interface PHY %d, setup/reset "
					   "sequences %d/%d long, capabilities %2.2x %2.2x.\n",
					   dev->name, bp[0], bp[1], bp[1 + bp[1]*2],
					   bp[5 + bp[2 + bp[1]*2]*2], bp[4 + bp[2 + bp[1]*2]*2]);
			}
			printk(KERN_INFO "%s:  Index #%d - Media %s (#%d) described "
				   "by a %s (%d) block.\n",
				   dev->name, i, medianame[leaf->media], leaf->media,
				   block_name[leaf->type], leaf->type);
		}
	}
}
/* Reading a serial EEPROM is a "bit" grungy, but we work our way through:->.*/

/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x02	/* EEPROM shift clock. */
#define EE_CS			0x01	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x04	/* EEPROM chip data in. */
#define EE_WRITE_0		0x01
#define EE_WRITE_1		0x05
#define EE_DATA_READ	0x08	/* EEPROM chip data out. */
#define EE_ENB			(0x4800 | EE_CS)

/* Delay between EEPROM clock transitions.
   Even at 33Mhz current PCI implementations don't overrun the EEPROM clock.
   We add a bus turn-around to insure that this remains true. */
#define eeprom_delay()	inl(ee_addr)

/* The EEPROM commands include the alway-set leading bit. */
#define EE_WRITE_CMD	(5 << addr_len)
#define EE_READ_CMD		(6 << addr_len)
#define EE_ERASE_CMD	(7 << addr_len)

static int read_eeprom(long ioaddr, int location, int addr_len)
{
	int i;
	unsigned short retval = 0;
	long ee_addr = ioaddr + CSR9;
	int read_cmd = location | EE_READ_CMD;

	outl(EE_ENB & ~EE_CS, ee_addr);
	outl(EE_ENB, ee_addr);

	/* Shift the read command bits out. */
	for (i = 4 + addr_len; i >= 0; i--) {
		short dataval = (read_cmd & (1 << i)) ? EE_DATA_WRITE : 0;
		outl(EE_ENB | dataval, ee_addr);
		eeprom_delay();
		outl(EE_ENB | dataval | EE_SHIFT_CLK, ee_addr);
		eeprom_delay();
	}
	outl(EE_ENB, ee_addr);

	for (i = 16; i > 0; i--) {
		outl(EE_ENB | EE_SHIFT_CLK, ee_addr);
		eeprom_delay();
		retval = (retval << 1) | ((inl(ee_addr) & EE_DATA_READ) ? 1 : 0);
		outl(EE_ENB, ee_addr);
		eeprom_delay();
	}

	/* Terminate the EEPROM access. */
	outl(EE_ENB & ~EE_CS, ee_addr);
	return retval;
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
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int i;
	int read_cmd = (0xf6 << 10) | (phy_id << 5) | location;
	int retval = 0;
	long ioaddr = dev->base_addr;
	long mdio_addr = ioaddr + CSR9;

	if (tp->chip_id == LC82C168) {
		int i = 1000;
		outl(0x60020000 + (phy_id<<23) + (location<<18), ioaddr + 0xA0);
		inl(ioaddr + 0xA0);
		inl(ioaddr + 0xA0);
		while (--i > 0)
			if ( ! ((retval = inl(ioaddr + 0xA0)) & 0x80000000))
				return retval & 0xffff;
		return 0xffff;
	}

	if (tp->chip_id == COMET) {
		if (phy_id == 1) {
			if (location < 7)
				return inl(ioaddr + 0xB4 + (location<<2));
			else if (location == 17)
				return inl(ioaddr + 0xD0);
			else if (location >= 29 && location <= 31)
				return inl(ioaddr + 0xD4 + ((location-29)<<2));
		}
		return 0xffff;
	}

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
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int i;
	int cmd = (0x5002 << 16) | (phy_id << 23) | (location<<18) | value;
	long ioaddr = dev->base_addr;
	long mdio_addr = ioaddr + CSR9;

	if (tp->chip_id == LC82C168) {
		int i = 1000;
		outl(cmd, ioaddr + 0xA0);
		do
			if ( ! (inl(ioaddr + 0xA0) & 0x80000000))
				break;
		while (--i > 0);
		return;
	}

	if (tp->chip_id == COMET) {
		if (phy_id != 1)
			return;
		if (location < 7)
			outl(value, ioaddr + 0xB4 + (location<<2));
		else if (location == 17)
			outl(value, ioaddr + 0xD0);
		else if (location >= 29 && location <= 31)
			outl(value, ioaddr + 0xD4 + ((location-29)<<2));
		return;
	}

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
	if (tp->mii_cnt  ||  (tp->mtable  &&  tp->mtable->has_mii))
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

	if (tulip_tbl[tp->chip_id].flags & MC_HASH_ONLY) {
		u32 addr_low = cpu_to_le32(get_unaligned((u32 *)dev->dev_addr));
		u32 addr_high = cpu_to_le32(get_unaligned((u16 *)(dev->dev_addr+4)));
		if (tp->chip_id == AX88140) {
			outl(0, ioaddr + CSR13);
			outl(addr_low,  ioaddr + CSR14);
			outl(1, ioaddr + CSR13);
			outl(addr_high, ioaddr + CSR14);
		} else if (tp->chip_id == COMET) {
			outl(addr_low,  ioaddr + 0xA4);
			outl(addr_high, ioaddr + 0xA8);
			outl(0, ioaddr + 0xAC);
			outl(0, ioaddr + 0xB0);
		}
	} else if (tp->chip_id != X3201_3) {
		/* This is set_rx_mode(), but without starting the transmitter. */
		u16 *eaddrs = (u16 *)dev->dev_addr;
		u16 *setup_frm = &tp->setup_frame[15*6];

		/* 21140 bug: you must add the broadcast address. */
		memset(tp->setup_frame, 0xff, 96*sizeof(u16));
		/* Fill the final entry of the table with our physical address. */
		*setup_frm++ = eaddrs[0]; *setup_frm++ = eaddrs[0];
		*setup_frm++ = eaddrs[1]; *setup_frm++ = eaddrs[1];
		*setup_frm++ = eaddrs[2]; *setup_frm++ = eaddrs[2];
		/* Put the setup frame on the Tx list. */
		tp->tx_ring[0].length = 0x08000000 | 192;
		tp->tx_ring[0].buffer1 = virt_to_bus(tp->setup_frame);
		tp->tx_ring[0].status = DescOwned;

		tp->cur_tx++;
	} else { /* X3201_3 */
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
		tp->tx_ring[0].length = 0x08000000 | 192;
		/* Lie about the address of our setup frame to make the */
		/* chip happy */
		tp->tx_ring[0].buffer1 = virt_to_bus(tp->setup_frame);
		tp->tx_ring[0].status = DescOwned;

		tp->cur_tx++;
	}
	outl(virt_to_bus(tp->rx_ring), ioaddr + CSR3);
	outl(virt_to_bus(tp->tx_ring), ioaddr + CSR4);

	tp->saved_if_port = dev->if_port;
	if (dev->if_port == 0)
		dev->if_port = tp->default_port;
	if (tp->chip_id == DC21041  &&  dev->if_port > 4)
		/* Invalid: Select initial TP, autosense, autonegotiate.  */
		dev->if_port = 4;

	/* Allow selecting a default media. */
	i = 0;
	if (tp->mtable == NULL)
		goto media_picked;
	if (dev->if_port) {
		int looking_for = media_cap[dev->if_port] & MediaIsMII ? 11 :
			(dev->if_port == 12 ? 0 : dev->if_port);
		for (i = 0; i < tp->mtable->leafcount; i++)
			if (tp->mtable->mleaf[i].media == looking_for) {
				printk(KERN_INFO "%s: Using user-specified media %s.\n",
					   dev->name, medianame[dev->if_port]);
				goto media_picked;
			}
	}
	if ((tp->mtable->defaultmedia & 0x0800) == 0) {
		int looking_for = tp->mtable->defaultmedia & 15;
		for (i = 0; i < tp->mtable->leafcount; i++)
			if (tp->mtable->mleaf[i].media == looking_for) {
				printk(KERN_INFO "%s: Using EEPROM-set media %s.\n",
					   dev->name, medianame[looking_for]);
				goto media_picked;
			}
	}
	/* Start sensing first non-full-duplex media. */
	for (i = tp->mtable->leafcount - 1;
		 (media_cap[tp->mtable->mleaf[i].media] & MediaAlwaysFD) && i > 0; i--)
		;
media_picked:

	tp->csr6 = 0;
	tp->cur_index = i;
	if (dev->if_port == 0  &&  tp->chip_id == DC21142) {
		if (tp->mii_cnt) {
			select_media(dev, 1);
			if (tulip_debug > 1)
				printk(KERN_INFO "%s: Using MII transceiver %d, status "
					   "%4.4x.\n",
					   dev->name, tp->phys[0], mdio_read(dev, tp->phys[0], 1));
			outl_CSR6(0x82020000, ioaddr, tp->chip_id);
			tp->csr6 = 0x820E0000;
			dev->if_port = 11;
			outl(0x0000, ioaddr + CSR13);
			outl(0x0000, ioaddr + CSR14);
		} else
			t21142_start_nway(dev);
	} else if ((tp->chip_id == LC82C168  || tp->chip_id == PNIC2)
			   &&  tp->mii_cnt && ! tp->medialock) {
		dev->if_port = 11;
		tp->csr6 = 0x814C0000 | (tp->full_duplex ? 0x0200 : 0);
		outl(0x0001, ioaddr + CSR15);
	} else if ((tp->chip_id == MX98713 || tp->chip_id == COMPEX9881)
			   && ! tp->medialock) {
		dev->if_port = 0;
		tp->csr6 = 0x01880000 | (tp->full_duplex ? 0x0200 : 0);
		outl(0x0f370000 | inw(ioaddr + 0x80), ioaddr + 0x80);
	} else if (tp->chip_id == MX98715 || tp->chip_id == MX98725) {
		/* Provided by BOLO, Macronix - 12/10/1998. */
		dev->if_port = 0;
		tp->csr6 = 0x01880200;
		outl(0x0f370000 | inw(ioaddr + 0x80), ioaddr + 0x80);
		outl(0x11000 | inw(ioaddr + 0xa0), ioaddr + 0xa0);
	} else if (tp->chip_id == DC21143  &&
			   media_cap[dev->if_port] & MediaIsMII) {
		/* We must reset the media CSRs when we force-select MII mode. */
		outl(0x0000, ioaddr + CSR13);
		outl(0x0000, ioaddr + CSR14);
		outl(0x0008, ioaddr + CSR15);
	} else if (tp->chip_id == X3201_3) {
		outl(0x0008, ioaddr + CSR15);
		udelay(5);
		outl(0xa8050000, ioaddr + CSR15);
		udelay(5);
		outl(0xa00f0000, ioaddr + CSR15); 
		udelay(5);
		tp->csr6  = 0x32400000;
	} else if (tp->chip_id == COMET) {
		dev->if_port = 0;
		tp->csr6 = 0x00040000;
	} else
		select_media(dev, 1);

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

/* Set up the transceiver control registers for the selected media type. */
static void select_media(struct net_device *dev, int startup)
{
	long ioaddr = dev->base_addr;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	struct mediatable *mtable = tp->mtable;
	u32 new_csr6;
	int i;

	if (mtable) {
		struct medialeaf *mleaf = &mtable->mleaf[tp->cur_index];
		unsigned char *p = mleaf->leafdata;
		switch (mleaf->type) {
		case 0:					/* 21140 non-MII xcvr. */
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s: Using a 21140 non-MII transceiver"
					   " with control setting %2.2x.\n",
					   dev->name, p[1]);
			dev->if_port = p[0];
			if (startup)
				outl(mtable->csr12dir | 0x100, ioaddr + CSR12);
			outl(p[1], ioaddr + CSR12);
			new_csr6 = 0x02000000 | ((p[2] & 0x71) << 18);
			break;
		case 2: case 4: {
			u16 setup[5];
			u32 csr13val, csr14val, csr15dir, csr15val;
			for (i = 0; i < 5; i++)
				setup[i] = get_u16(&p[i*2 + 1]);

			dev->if_port = p[0] & 15;
			if (media_cap[dev->if_port] & MediaAlwaysFD)
				tp->full_duplex = 1;

			if (startup && mtable->has_reset) {
				struct medialeaf *rleaf = &mtable->mleaf[mtable->has_reset];
				unsigned char *rst = rleaf->leafdata;
				if (tulip_debug > 1)
					printk(KERN_DEBUG "%s: Resetting the transceiver.\n",
						   dev->name);
				for (i = 0; i < rst[0]; i++)
					outl(get_u16(rst + 1 + (i<<1)) << 16, ioaddr + CSR15);
			}
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s: 21143 non-MII %s transceiver control "
					   "%4.4x/%4.4x.\n",
					   dev->name, medianame[dev->if_port], setup[0], setup[1]);
			if (p[0] & 0x40) {	/* SIA (CSR13-15) setup values are provided. */
				csr13val = setup[0];
				csr14val = setup[1];
				csr15dir = (setup[3]<<16) | setup[2];
				csr15val = (setup[4]<<16) | setup[2];
				outl(0, ioaddr + CSR13);
				outl(csr14val, ioaddr + CSR14);
				outl(csr15dir, ioaddr + CSR15);	/* Direction */
				outl(csr15val, ioaddr + CSR15);	/* Data */
				outl(csr13val, ioaddr + CSR13);
			} else {
				csr13val = 1;
				csr14val = 0x0003FF7F;
				csr15dir = (setup[0]<<16) | 0x0008;
				csr15val = (setup[1]<<16) | 0x0008;
				if (dev->if_port <= 4)
					csr14val = t21142_csr14[dev->if_port];
				if (startup) {
					outl(0, ioaddr + CSR13);
					outl(csr14val, ioaddr + CSR14);
				}
				outl(csr15dir, ioaddr + CSR15);	/* Direction */
				outl(csr15val, ioaddr + CSR15);	/* Data */
				if (startup) outl(csr13val, ioaddr + CSR13);
			}
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s:  Setting CSR15 to %8.8x/%8.8x.\n",
					   dev->name, csr15dir, csr15val);
			if (mleaf->type == 4)
				new_csr6 = 0x82020000 | ((setup[2] & 0x71) << 18);
			else
				new_csr6 = 0x82420000;
			break;
		}
		case 1: case 3: {
			int phy_num = p[0];
			int init_length = p[1];
			u16 *misc_info;
			u16 to_advertise;

			dev->if_port = 11;
			new_csr6 = 0x020E0000;
			if (mleaf->type == 3) {	/* 21142 */
				u16 *init_sequence = (u16*)(p+2);
				u16 *reset_sequence = &((u16*)(p+3))[init_length];
				int reset_length = p[2 + init_length*2];
				misc_info = reset_sequence + reset_length;
				if (startup)
					for (i = 0; i < reset_length; i++)
						outl(get_u16(&reset_sequence[i]) << 16, ioaddr + CSR15);
				for (i = 0; i < init_length; i++)
					outl(get_u16(&init_sequence[i]) << 16, ioaddr + CSR15);
			} else {
				u8 *init_sequence = p + 2;
				u8 *reset_sequence = p + 3 + init_length;
				int reset_length = p[2 + init_length];
				misc_info = (u16*)(reset_sequence + reset_length);
				if (startup) {
					outl(mtable->csr12dir | 0x100, ioaddr + CSR12);
					for (i = 0; i < reset_length; i++)
						outl(reset_sequence[i], ioaddr + CSR12);
				}
				for (i = 0; i < init_length; i++)
					outl(init_sequence[i], ioaddr + CSR12);
			}
			to_advertise = (get_u16(&misc_info[1]) & tp->to_advertise) | 1;
			tp->advertising[phy_num] = to_advertise;
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s:  Advertising %4.4x on PHY %d (%d).\n",
					   dev->name, to_advertise, phy_num, tp->phys[phy_num]);
			/* Bogus: put in by a committee?  */
			mdio_write(dev, tp->phys[phy_num], 4, to_advertise);
			break;
		}
		default:
			printk(KERN_DEBUG "%s:  Invalid media table selection %d.\n",
					   dev->name, mleaf->type);
			new_csr6 = 0x020E0000;
		}
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: Using media type %s, CSR12 is %2.2x.\n",
				   dev->name, medianame[dev->if_port],
				   inl(ioaddr + CSR12) & 0xff);
	} else if (tp->chip_id == DC21041) {
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: 21041 using media %s, CSR12 is %4.4x.\n",
				   dev->name, medianame[dev->if_port & 15],
				   inl(ioaddr + CSR12) & 0xffff);
		outl(0x00000000, ioaddr + CSR13); /* Reset the serial interface */
		outl(t21041_csr14[dev->if_port], ioaddr + CSR14);
		outl(t21041_csr15[dev->if_port], ioaddr + CSR15);
		outl(t21041_csr13[dev->if_port], ioaddr + CSR13);
		new_csr6 = 0x80020000;
	} else if (tp->chip_id == LC82C168 || tp->chip_id == PNIC2) {
		if (startup && ! tp->medialock)
			dev->if_port = tp->mii_cnt ? 11 : 0;
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: PNIC PHY status is %3.3x, CSR12 %4.4x,"
				   " media %s.\n",
				   dev->name, inl(ioaddr + 0xB8), inl(ioaddr + CSR12),
				   medianame[dev->if_port]);
		if (tp->mii_cnt) {
			new_csr6 = 0x810C0000;
			outl(0x0001, ioaddr + CSR15);
			outl(0x0201B07A, ioaddr + 0xB8);
		} else if (startup) {
			/* Start with 10mbps to do autonegotiation. */
			outl(0x32, ioaddr + CSR12);
			new_csr6 = 0x00420000;
			outl(0x0001B078, ioaddr + 0xB8);
			outl(0x0201B078, ioaddr + 0xB8);
		} else if (dev->if_port == 3  ||  dev->if_port == 5) {
			outl(0x33, ioaddr + CSR12);
			new_csr6 = 0x01860000;
			if (startup)
				outl(0x0201F868, ioaddr + 0xB8); /* Trigger autonegotiation. */
			else
				outl(0x1F868, ioaddr + 0xB8);
		} else {
			outl(0x32, ioaddr + CSR12);
			new_csr6 = 0x00420000;
			outl(0x1F078, ioaddr + 0xB8);
		}
	} else if (tp->chip_id == DC21040) {					/* 21040 */
		/* Turn on the xcvr interface. */
		int csr12 = inl(ioaddr + CSR12);
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: 21040 media type is %s, CSR12 is %2.2x.\n",
				   dev->name, medianame[dev->if_port], csr12);
		if (media_cap[dev->if_port] & MediaAlwaysFD)
			tp->full_duplex = 1;
		new_csr6 = 0x20000;
		/* Set the full duplux match frame. */
		outl(FULL_DUPLEX_MAGIC, ioaddr + CSR11);
		outl(0x00000000, ioaddr + CSR13); /* Reset the serial interface */
		if (t21040_csr13[dev->if_port] & 8) {
			outl(0x0705, ioaddr + CSR14);
			outl(0x0006, ioaddr + CSR15);
		} else {
			outl(0xffff, ioaddr + CSR14);
			outl(0x0000, ioaddr + CSR15);
		}
		outl(0x8f01 | t21040_csr13[dev->if_port], ioaddr + CSR13);
	} else if (tp->chip_id == X3201_3) {					/* Xircom */
		if (tp->default_port == 0)
			dev->if_port = tp->mii_cnt ? 11 : 3;
/* Someone is on crack, the Xircom only does MII, no Fx */
/*		if (media_cap[dev->if_port] & MediaIsMII) {
			new_csr6 = 0x020E0000;
		} else if (media_cap[dev->if_port] & MediaIsFx) {
			new_csr6 = 0x028600000;
		} else
			new_csr6 = 0x038600000;*/
		new_csr6 = 0x324c0000;
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: Xircom CardBus Adapter: "
				   "%s transceiver, CSR12 %2.2x.\n",
				   dev->name, medianame[dev->if_port],
				   inl(ioaddr + CSR12));
	} else {					/* Unknown chip type with no media table. */
		if (tp->default_port == 0)
			dev->if_port = tp->mii_cnt ? 11 : 3;
		if (media_cap[dev->if_port] & MediaIsMII) {
			new_csr6 = 0x020E0000;
		} else if (media_cap[dev->if_port] & MediaIsFx) {
			new_csr6 = 0x028600000;
		} else
			new_csr6 = 0x038600000;
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: No media description table, assuming "
				   "%s transceiver, CSR12 %2.2x.\n",
				   dev->name, medianame[dev->if_port],
				   inl(ioaddr + CSR12));
	}

	tp->csr6 = new_csr6 | (tp->csr6 & 0xfdff) | (tp->full_duplex ? 0x0200 : 0);
	return;
}

/*
  Check the MII negotiated duplex, and change the CSR6 setting if
  required.
  Return 0 if everything is OK.
  Return < 0 if the transceiver is missing or has no link beat.
  */
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
	switch (tp->chip_id) {
	case DC21040:
		if (!tp->medialock  &&  csr12 & 0x0002) { /* Network error */
			printk(KERN_INFO "%s: No link beat found.\n",
				   dev->name);
			dev->if_port = (dev->if_port == 2 ? 0 : 2);
			select_media(dev, 0);
			dev->trans_start = jiffies;
		}
		break;
	case DC21041:
		if (tulip_debug > 2)
			printk(KERN_DEBUG "%s: 21041 media tick  CSR12 %8.8x.\n",
				   dev->name, csr12);
		switch (dev->if_port) {
		case 0: case 3: case 4:
		  if (csr12 & 0x0004) { /*LnkFail */
			/* 10baseT is dead.  Check for activity on alternate port. */
			tp->mediasense = 1;
			if (csr12 & 0x0200)
				dev->if_port = 2;
			else
				dev->if_port = 1;
			printk(KERN_INFO "%s: No 21041 10baseT link beat, Media switched to %s.\n",
				   dev->name, medianame[dev->if_port]);
			outl(0, ioaddr + CSR13); /* Reset */
			outl(t21041_csr14[dev->if_port], ioaddr + CSR14);
			outl(t21041_csr15[dev->if_port], ioaddr + CSR15);
			outl(t21041_csr13[dev->if_port], ioaddr + CSR13);
			next_tick = 10*HZ;			/* 2.4 sec. */
		  } else
			next_tick = 30*HZ;
		  break;
		case 1:					/* 10base2 */
		case 2:					/* AUI */
			if (csr12 & 0x0100) {
				next_tick = (30*HZ);			/* 30 sec. */
				tp->mediasense = 0;
			} else if ((csr12 & 0x0004) == 0) {
				printk(KERN_INFO "%s: 21041 media switched to 10baseT.\n",
					   dev->name);
				dev->if_port = 0;
				select_media(dev, 0);
				next_tick = (24*HZ)/10;				/* 2.4 sec. */
			} else if (tp->mediasense || (csr12 & 0x0002)) {
				dev->if_port = 3 - dev->if_port; /* Swap ports. */
				select_media(dev, 0);
				next_tick = 20*HZ;
			} else {
				next_tick = 20*HZ;
			}
			break;
		}
		break;
	case DC21140:  case DC21142: case MX98713: case COMPEX9881: default: {
		struct medialeaf *mleaf;
		unsigned char *p;
		if (tp->mtable == NULL) {	/* No EEPROM info, use generic code. */
			/* Not much that can be done.
			   Assume this a generic MII or SYM transceiver. */
			next_tick = 60*HZ;
			if (tulip_debug > 2)
				printk(KERN_DEBUG "%s: network media monitor CSR6 %8.8x "
					   "CSR12 0x%2.2x.\n",
					   dev->name, inl(ioaddr + CSR6), csr12 & 0xff);
			break;
		}
		mleaf = &tp->mtable->mleaf[tp->cur_index];
		p = mleaf->leafdata;
		switch (mleaf->type) {
		case 0: case 4: {
			/* Type 0 serial or 4 SYM transceiver.  Check the link beat bit. */
			int offset = mleaf->type == 4 ? 5 : 2;
			s8 bitnum = p[offset];
			if (p[offset+1] & 0x80) {
				if (tulip_debug > 1)
					printk(KERN_DEBUG"%s: Transceiver monitor tick "
						   "CSR12=%#2.2x, no media sense.\n",
						   dev->name, csr12);
				if (mleaf->type == 4) {
					if (mleaf->media == 3 && (csr12 & 0x02))
						goto select_next_media;
				}
				break;
			}
			if (tulip_debug > 2)
				printk(KERN_DEBUG "%s: Transceiver monitor tick: CSR12=%#2.2x"
					   " bit %d is %d, expecting %d.\n",
					   dev->name, csr12, (bitnum >> 1) & 7,
					   (csr12 & (1 << ((bitnum >> 1) & 7))) != 0,
					   (bitnum >= 0));
			/* Check that the specified bit has the proper value. */
			if ((bitnum < 0) !=
				((csr12 & (1 << ((bitnum >> 1) & 7))) != 0)) {
				if (tulip_debug > 1)
					printk(KERN_DEBUG "%s: Link beat detected for %s.\n", dev->name,
						   medianame[mleaf->media]);
				if ((p[2] & 0x61) == 0x01)	/* Bogus Znyx board. */
					goto actually_mii;
				break;
			}
			if (tp->medialock)
				break;
	  select_next_media:
			if (--tp->cur_index < 0) {
				/* We start again, but should instead look for default. */
				tp->cur_index = tp->mtable->leafcount - 1;
			}
			dev->if_port = tp->mtable->mleaf[tp->cur_index].media;
			if (media_cap[dev->if_port] & MediaIsFD)
				goto select_next_media; /* Skip FD entries. */
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s: No link beat on media %s,"
					   " trying transceiver type %s.\n",
					   dev->name, medianame[mleaf->media & 15],
					   medianame[tp->mtable->mleaf[tp->cur_index].media]);
			select_media(dev, 0);
			/* Restart the transmit process. */
			outl_CSR6(tp->csr6 | 0x0002, ioaddr, tp->chip_id);
			outl_CSR6(tp->csr6 | 0x2002, ioaddr, tp->chip_id);
			next_tick = (24*HZ)/10;
			break;
		}
		case 1:  case 3:		/* 21140, 21142 MII */
		actually_mii:
			check_duplex(dev);
			next_tick = 60*HZ;
			break;
		case 2:					/* 21142 serial block has no link beat. */
		default:
			break;
		}
	}
	break;
	}
	tp->timer.expires = RUN_AT(next_tick);
	add_timer(&tp->timer);
}

/* Handle the 21143 uniquely: do autoselect with NWay, not the EEPROM list
   of available transceivers.  */
static void t21142_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int csr12 = inl(ioaddr + CSR12);
	int next_tick = 60*HZ;
	int new_csr6 = 0;

	if ((tulip_debug > 2) && !(media_cap[dev->if_port] & MediaIsMII))
		printk(KERN_INFO"%s: 21143 negotiation status %8.8x, %s.\n",
			   dev->name, csr12, medianame[dev->if_port]);
	if (media_cap[dev->if_port] & MediaIsMII) {
		check_duplex(dev);
		next_tick = 60*HZ;
	} else if (tp->nwayset) {
		/* Don't screw up a negotiated session! */
		if (tulip_debug > 1)
			printk(KERN_INFO"%s: Using NWay-set %s media, csr12 %8.8x.\n",
				   dev->name, medianame[dev->if_port], csr12);
	} else if (tp->medialock) {
			;
	} else if (dev->if_port == 3) {
		if (csr12 & 2) {	/* No 100mbps link beat, revert to 10mbps. */
			if (tulip_debug > 1)
				printk(KERN_INFO"%s: No 21143 100baseTx link beat, %8.8x, "
					   "trying NWay.\n", dev->name, csr12);
			t21142_start_nway(dev);
			next_tick = 3*HZ;
		}
	} else if (((csr12 & 0x7000) != 0x5000)
			   && tp->chip_id != X3201_3) {
		/* Negotiation failed.  Search media types. */
		if (tulip_debug > 1)
			printk(KERN_INFO"%s: 21143 negotiation failed, status %8.8x.\n",
				   dev->name, csr12);
		if (!(csr12 & 4)) {		/* 10mbps link beat good. */
			new_csr6 = 0x82420000;
			dev->if_port = 0;
			outl(0, ioaddr + CSR13);
			outl(0x0003FFFF, ioaddr + CSR14);
			outw(t21142_csr15[dev->if_port], ioaddr + CSR15);
			outl(t21142_csr13[dev->if_port], ioaddr + CSR13);
		} else {
			/* Select 100mbps port to check for link beat. */
			new_csr6 = 0x83860000;
			dev->if_port = 3;
			outl(0, ioaddr + CSR13);
			outl(0x0003FF7F, ioaddr + CSR14);
			outw(8, ioaddr + CSR15);
			outl(1, ioaddr + CSR13);
		}
		if (tulip_debug > 1)
			printk(KERN_INFO"%s: Testing new 21143 media %s.\n",
				   dev->name, medianame[dev->if_port]);
		if (new_csr6 != (tp->csr6 & ~0x00D5)) {
			tp->csr6 &= 0x00D5;
			tp->csr6 |= new_csr6;
			outl(0x0301, ioaddr + CSR12);
			outl_CSR6(tp->csr6 | 0x0002, ioaddr, tp->chip_id);
			outl_CSR6(tp->csr6 | 0x2002, ioaddr, tp->chip_id);
		}
		next_tick = 3*HZ;
	}
	if (tp->cur_tx - tp->dirty_tx > 0  &&
		jiffies - dev->trans_start > TX_TIMEOUT) {
		printk(KERN_WARNING "%s: Tx hung, %d vs. %d.\n",
			   dev->name, tp->cur_tx, tp->dirty_tx);
		tulip_tx_timeout(dev);
	}

	tp->timer.expires = RUN_AT(next_tick);
	add_timer(&tp->timer);
}

static void t21142_start_nway(struct net_device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int csr14 = ((tp->to_advertise & 0x0180) << 9)  |
		((tp->to_advertise&0x0020)<<1) | 0xffbf;

	dev->if_port = 0;
	tp->nway = tp->mediasense = 1;
	tp->nwayset = tp->lpar = 0;
	if (debug > 1)
		printk(KERN_DEBUG "%s: Restarting 21143 autonegotiation, %8.8x.\n",
			   dev->name, csr14);
	outl(0x0001, ioaddr + CSR13);
	outl(csr14, ioaddr + CSR14);
	tp->csr6 = 0x82420000 | (tp->to_advertise & 0x0040 ? 0x0200 : 0);
	outl_CSR6(tp->csr6, ioaddr, tp->chip_id);
	if (tp->mtable  &&  tp->mtable->csr15dir) {
		outl(tp->mtable->csr15dir, ioaddr + CSR15);
		outl(tp->mtable->csr15val, ioaddr + CSR15);
	} else
		outw(0x0008, ioaddr + CSR15);
	outl(0x1301, ioaddr + CSR12); 		/* Trigger NWAY. */
}

static void t21142_lnk_change(struct net_device *dev, int csr5)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int csr12 = inl(ioaddr + CSR12);

	if (tulip_debug > 1)
		printk(KERN_INFO"%s: 21143 link status interrupt %8.8x, CSR5 %x, "
			   "%8.8x.\n", dev->name, csr12, csr5, inl(ioaddr + CSR14));

	/* If NWay finished and we have a negotiated partner capability. */
	if (tp->nway  &&  !tp->nwayset  &&  (csr12 & 0x7000) == 0x5000) {
		int setup_done = 0;
		tp->lpar = csr12 >> 16;
		tp->nwayset = 1;
		if (csr12 & 0x01000000)			dev->if_port = 5;
		else if (csr12 & 0x00800000)	dev->if_port = 3;
		else if (csr12 & 0x00400000)	dev->if_port = 4;
		else if (csr12 & 0x00200000)	dev->if_port = 0;
		else {
			tp->nwayset = 0;
			if ( ! (csr12 & 2))			dev->if_port = 3;
			else if ( ! (csr12 & 4))	dev->if_port = 0;
		}
		tp->full_duplex = (media_cap[tp->default_port] & MediaAlwaysFD) ? 1:0;

		if (tulip_debug > 1) {
			if (tp->nwayset)
				printk(KERN_INFO "%s: Switching to %s based on link partner "
					   "advertisement %4.4x.\n",
					   dev->name, medianame[dev->if_port], tp->lpar);
			else
				printk(KERN_INFO "%s: Switching to %s based on link beat "
					   "status of %4.4x.\n",
					   dev->name, medianame[dev->if_port], csr12);
		}

		if (tp->mtable) {
			int i;
			for (i = 0; i < tp->mtable->leafcount; i++)
				if (tp->mtable->mleaf[i].media == dev->if_port) {
					tp->cur_index = i;
					select_media(dev, 0);
					setup_done = 1;
					break;
				}
		}
		if ( ! setup_done) {
			tp->csr6 = dev->if_port & 1 ? 0x83860000 : 0x82420000;
			if (tp->full_duplex)
				tp->csr6 |= 0x0200;
			outw(0x0000, ioaddr + CSR13);
			outw(0x0000, ioaddr + CSR14);
		}
		outl_CSR6(tp->csr6 | 0x0000, ioaddr, tp->chip_id);
		if (debug > 2)
			printk(KERN_DEBUG "%s:  Restarting Tx and Rx, CSR5 is %8.8x.\n",
				   dev->name, inl(ioaddr + CSR5));
		outl_CSR6(tp->csr6 | 0x2002, ioaddr, tp->chip_id);
	} else if ((tp->nwayset  &&  (csr5 & 0x08000000)
				&& (dev->if_port == 3  ||  dev->if_port == 5)
				&& (csr12 & 2) == 2) ||
			   (tp->nway && (csr5 & (TPLnkFail)))) {
		/* Link blew? Maybe restart NWay. */
		del_timer(&tp->timer);
		t21142_start_nway(dev);
		tp->timer.expires = RUN_AT(3*HZ);
		add_timer(&tp->timer);
	} else if (dev->if_port == 3  ||  dev->if_port == 5) {
		if (tulip_debug > 1)
			printk(KERN_INFO"%s: 21143 %s link beat %s.\n",
				   dev->name, medianame[dev->if_port],
				   (csr12 & 2) ? "failed" : "good");
		if ((csr12 & 2)  &&  ! tp->medialock) {
			del_timer(&tp->timer);
			t21142_start_nway(dev);
			tp->timer.expires = RUN_AT(3*HZ);
			add_timer(&tp->timer);
		}
	} else if (dev->if_port == 0  ||  dev->if_port == 4) {
		if ((csr12 & 4) == 0)
			printk(KERN_INFO"%s: 21143 10baseT link beat good.\n",
				   dev->name);
	} else if (!(csr12 & 4)) {		/* 10mbps link beat good. */
		if (tulip_debug)
			printk(KERN_INFO"%s: 21143 10mbps sensed media.\n",
				   dev->name);
		dev->if_port = 0;
	} else if (tp->nwayset) {
		if (tulip_debug)
			printk(KERN_INFO"%s: 21143 using NWay-set %s, csr6 %8.8x.\n",
				   dev->name, medianame[dev->if_port], tp->csr6);
	} else {		/* 100mbps link beat good. */
		if (tulip_debug)
			printk(KERN_INFO"%s: 21143 100baseTx sensed media.\n",
				   dev->name);
		dev->if_port = 3;
		tp->csr6 = 0x83860000;
		outl(0x0003FF7F, ioaddr + CSR14);
		outl(0x0301, ioaddr + CSR12);
		outl_CSR6(tp->csr6 | 0x0002, ioaddr, tp->chip_id);
		outl_CSR6(tp->csr6 | 0x2002, ioaddr, tp->chip_id);
	}
}

static void mxic_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 60*HZ;

	if (tulip_debug > 3) {
		printk(KERN_INFO"%s: MXIC negotiation status %8.8x.\n", dev->name,
			   inl(ioaddr + CSR12));
	}
	if (next_tick) {
		tp->timer.expires = RUN_AT(next_tick);
		add_timer(&tp->timer);
	}
}

static void pnic_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int csr12 = inl(ioaddr + CSR12);
	int next_tick = 60*HZ;
	int new_csr6 = tp->csr6 & ~0x40C40200;

	if (media_cap[dev->if_port] & MediaIsMII) {
		int negotiated = mdio_read(dev, tp->phys[0], 5) & tp->advertising[0];

		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: PNIC negotiated capability %8.8x, "
				   "CSR5 %8.8x.\n",
				   dev->name, negotiated, inl(ioaddr + CSR5));

		if (negotiated & 0x0380) 				/* 10 vs 100mbps */
			new_csr6 |= 0x810E0000;
		else
			new_csr6 |= 0x814E0000;
		if (((negotiated & 0x0300) == 0x0100)			/* Duplex */
			|| (negotiated & 0x00C0) == 0x0040
			|| tp->full_duplex_lock) {
			tp->full_duplex = 1;
			new_csr6 |= 0x0200;
		}
		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: PNIC MII PHY status %4.4x, Link "
				   "partner report %4.4x, csr6 %8.8x/%8.8x.\n",
			   dev->name, mdio_read(dev, tp->phys[0], 1), negotiated,
				   tp->csr6, inl(ioaddr + CSR6));
	} else {
		int phy_reg = inl(ioaddr + 0xB8);
		int csr5 = inl(ioaddr + CSR5);

		if (tulip_debug > 1)
			printk(KERN_DEBUG "%s: PNIC PHY status %8.8x, CSR5 %8.8x.\n",
				   dev->name, phy_reg, csr5);

		if (phy_reg & 0x04000000) {	/* Remote link fault */
			/*outl(0x0201F078, ioaddr + 0xB8);*/
			next_tick = 3*HZ;
		}
		if (inl(ioaddr + CSR5) & TPLnkFail) { /* 100baseTx link beat */
			if (tulip_debug > 1)
				printk(KERN_DEBUG "%s: %s link beat failed, CSR12 %4.4x, "
					   "CSR5 %8.8x, PHY %3.3x.\n",
					   dev->name, medianame[dev->if_port], csr12,
					   inl(ioaddr + CSR5), inl(ioaddr + 0xB8));
			if (tp->medialock) {
			} else if (dev->if_port == 0) {
				dev->if_port = 3;
				outl(0x33, ioaddr + CSR12);
				new_csr6 = 0x01860000;
				outl(0x1F868, ioaddr + 0xB8);
			} else {
				dev->if_port = 0;
				outl(0x32, ioaddr + CSR12);
				new_csr6 = 0x00420000;
				outl(0x1F078, ioaddr + 0xB8);
			}
			new_csr6 |= (tp->csr6 & 0xfdff);
			next_tick = 3*HZ;
		} else
			new_csr6 = tp->csr6;
		if (tp->full_duplex_lock  ||  (phy_reg & 0x30000000) != 0) {
			tp->full_duplex = 1;
			new_csr6 |= 0x00000200;
		}
	}
	if (tp->csr6 != new_csr6) {
		tp->csr6 = new_csr6;
		outl_CSR6(tp->csr6 | 0x0002, ioaddr, tp->chip_id);	/* Restart Tx */
		outl_CSR6(tp->csr6 | 0x2002, ioaddr, tp->chip_id);
		dev->trans_start = jiffies;
		if (tulip_debug > 1)
			printk(KERN_INFO "%s: Changing PNIC configuration to %s-duplex, "
				   "CSR6 %8.8x.\n",
				   dev->name, tp->full_duplex ? "full" : "half", new_csr6);
	}
	tp->timer.expires = RUN_AT(next_tick);
	add_timer(&tp->timer);
}

static void comet_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	long ioaddr = dev->base_addr;
	int next_tick = 60*HZ;

	if (tulip_debug > 1)
		printk(KERN_DEBUG "%s: Comet link status %4.4x partner capability "
			   "%4.4x.\n",
			   dev->name, inl(ioaddr + 0xB8), inl(ioaddr + 0xC8));
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
	} else if (tp->chip_id == DC21040) {
		if ( !tp->medialock  &&  inl(ioaddr + CSR12) & 0x0002) {
			dev->if_port = (dev->if_port == 2 ? 0 : 2);
			printk(KERN_INFO "%s: transmit timed out, switching to "
				   "%s.\n",
				   dev->name, medianame[dev->if_port]);
			select_media(dev, 0);
		}
		dev->trans_start = jiffies;
		return;
	} else if (tp->chip_id == DC21041) {
		int csr12 = inl(ioaddr + CSR12);

		printk(KERN_WARNING "%s: 21041 transmit timed out, status %8.8x, "
			   "CSR12 %8.8x, CSR13 %8.8x, CSR14 %8.8x, resetting...\n",
			   dev->name, inl(ioaddr + CSR5), csr12,
			   inl(ioaddr + CSR13), inl(ioaddr + CSR14));
		tp->mediasense = 1;
		if ( ! tp->medialock) {
			if (dev->if_port == 1 || dev->if_port == 2)
				if (csr12 & 0x0004) {
					dev->if_port = 2 - dev->if_port;
				} else
					dev->if_port = 0;
			else
				dev->if_port = 1;
			select_media(dev, 0);
		}
	} else if (tp->chip_id == DC21140 || tp->chip_id == DC21142
			   || tp->chip_id == MX98713 || tp->chip_id == COMPEX9881) {
		printk(KERN_WARNING "%s: 21140 transmit timed out, status %8.8x, "
			   "SIA %8.8x %8.8x %8.8x %8.8x, resetting...\n",
			   dev->name, inl(ioaddr + CSR5), inl(ioaddr + CSR12),
			   inl(ioaddr + CSR13), inl(ioaddr + CSR14), inl(ioaddr + CSR15));
		if ( ! tp->medialock  &&  tp->mtable) {
			do
				--tp->cur_index;
			while (tp->cur_index >= 0
				   && (media_cap[tp->mtable->mleaf[tp->cur_index].media]
					   & MediaIsFD));
			if (--tp->cur_index < 0) {
				/* We start again, but should instead look for default. */
				tp->cur_index = tp->mtable->leafcount - 1;
			}
			select_media(dev, 0);
			printk(KERN_WARNING "%s: transmit timed out, switching to %s "
				   "media.\n", dev->name, medianame[dev->if_port]);
		}
	} else {
		printk(KERN_WARNING "%s: Transmit timed out, status %8.8x, CSR12 "
			   "%8.8x, resetting...\n",
			   dev->name, inl(ioaddr + CSR5), inl(ioaddr + CSR12));
		dev->if_port = 0;
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
#endif CARDBUS
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
			if (csr5 & (TPLnkPass | TPLnkFail | 0x08000000)) {
				if ( tp->chip_id == DC21142)
					t21142_lnk_change(dev, csr5);
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
	/* 21040 -- Leave the card in 10baseT state. */
	if (tp->chip_id == DC21040)
		outl(0x00000004, ioaddr + CSR13);

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
	long ioaddr = dev->base_addr;
	u16 *data = (u16 *)&rq->ifr_data;
	int phy = tp->phys[0] & 0x1f;
	long flags;

	switch(cmd) {
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		if (tp->mii_cnt)
			data[0] = phy;
		else if (tp->chip_id == DC21142)	/* 21142 pseudo-MII */
			data[0] = 32;
		else if (tp->chip_id == PNIC2)
			data[0] = 32;
		else if (tp->chip_id == COMET)
			data[0] = 1;
		else
			return -ENODEV;
		return 0;
	case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
		if (data[0] == 32  &&
			(tp->chip_id == DC21142 || tp->chip_id == PNIC2)) {
			int csr12 = inl(ioaddr + CSR12);
			int csr14 = inl(ioaddr + CSR14);
			switch (data[1]) {
			case 0: {
				data[3] = (csr14<<5) & 0x1000;
				break; }
			case 1:
				data[3] = 0x7848 + ((csr12&0x7000) == 0x5000 ? 0x20 : 0)
					+ (csr12&0x06 ? 0x04 : 0);
				break;
			case 4: {
				data[3] = ((csr14>>9)&0x0380) +
					((inl(ioaddr + CSR6)>>3)&0x0040) +((csr14>>1)&0x20) + 1;
				break;
			}
			case 5: data[3] = csr12 >> 16; break;
			default: data[3] = 0; break;
			}
		} else {
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
		if (data[0] == 32  &&  tp->chip_id == DC21142) {
			if (data[1] == 5)
				tp->to_advertise = data[2];
		} else {
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
	} else	if (tulip_tbl[tp->chip_id].flags & MC_HASH_ONLY) {
		/* Some work-alikes have only a 64-entry hash filter table. */
		/* Should verify correctness on big-endian/__powerpc__ */
		struct dev_mc_list *mclist;
		int i;
		u32 mc_filter[2];		 /* Multicast hash filter */
		if (dev->mc_count > 64) {		/* Arbitrary non-effective limit. */
			tp->csr6 |= 0x0080;
			csr6 |= 0x0080;
		} else {
			mc_filter[1] = mc_filter[0] = 0;
			for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
				 i++, mclist = mclist->next)
				set_bit(ether_crc(ETH_ALEN, mclist->dmi_addr)>>26, mc_filter);
			if (tp->chip_id == AX88140) {
				outl(2, ioaddr + CSR13);
				outl(mc_filter[0], ioaddr + CSR14);
				outl(3, ioaddr + CSR13);
				outl(mc_filter[1], ioaddr + CSR14);
			} else if (tp->chip_id == COMET) { /* Has a simple hash filter. */
				outl(mc_filter[0], ioaddr + 0xAC);
				outl(mc_filter[1], ioaddr + 0xB0);
			}
		}
	} else {
		u16 *eaddrs, *setup_frm = tp->setup_frame;
		struct dev_mc_list *mclist;
		u32 tx_flags = 0x08000000 | 192;
		int i;

		/* Note that only the low-address shortword of setup_frame is valid!
		   The values are doubled for big-endian architectures. */
		if ((dev->mc_count > 14) || ((dev->mc_count > 6) && (tp->chip_id == X3201_3))) { /* Must use a multicast hash table. */
			u16 hash_table[32];
			tx_flags = 0x08400000 | 192;		/* Use hash filter. */
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
		} else if(tp->chip_id != X3201_3) {
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
		} else {
			/* fill the first two table entries with our address */
			eaddrs = (u16 *)dev->dev_addr;
			for(i=0; i<2; i++) {
				*setup_frm++ = eaddrs[0]; *setup_frm++ = eaddrs[0];
				*setup_frm++ = eaddrs[1]; *setup_frm++ = eaddrs[1];
				*setup_frm++ = eaddrs[2]; *setup_frm++ = eaddrs[2];
			}
			/* Double fill each entry to accomodate chips that */
			/* don't like to parse these correctly */
			for (i=0, mclist=dev->mc_list; i<dev->mc_count;
				 i++, mclist=mclist->next) {
				eaddrs = (u16 *)mclist->dmi_addr;
				*setup_frm++ = eaddrs[0]; *setup_frm++ = eaddrs[0];
				*setup_frm++ = eaddrs[1]; *setup_frm++ = eaddrs[1];
				*setup_frm++ = eaddrs[2]; *setup_frm++ = eaddrs[2];
				*setup_frm++ = eaddrs[0]; *setup_frm++ = eaddrs[0];
				*setup_frm++ = eaddrs[1]; *setup_frm++ = eaddrs[1];
				*setup_frm++ = eaddrs[2]; *setup_frm++ = eaddrs[2];
			}
			i=((i+1)*2);
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
#if 0 /* these entries conflict with regular tulip driver */
  { 0x1011, 0x0002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DC21040 },
  { 0x1011, 0x0014, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DC21041 },
  { 0x1011, 0x0009, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DC21140 },
  { 0x1011, 0x0019, PCI_ANY_ID, PCI_ANY_ID, 0, 0, DC21142 },
  { 0x11AD, 0x0002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, LC82C168 },
  { 0x10d9, 0x0512, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MX98713 },
  { 0x10d9, 0x0531, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MX98715 },
  { 0x10d9, 0x0531, PCI_ANY_ID, PCI_ANY_ID, 0, 0, MX98725 },
  { 0x125B, 0x1400, PCI_ANY_ID, PCI_ANY_ID, 0, 0, AX88140 },
  { 0x11AD, 0xc115, PCI_ANY_ID, PCI_ANY_ID, 0, 0, PNIC2 },
  { 0x1317, 0x0981, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMET },
  { 0x11F6, 0x9881, PCI_ANY_ID, PCI_ANY_ID, 0, 0, COMPEX9881 },
#endif
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
	dev = tulip_probe1(pdev, NULL,
			   pci_resource_start (pdev, 0), pdev->irq,
			   id->driver_data, board_idx++);
	if (dev) {
		pdev->driver_data = dev;
		return 0;
	}
	return -ENODEV;
}

static void tulip_suspend(struct pci_dev *pdev)
{
	struct net_device *dev = pdev->driver_data;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	printk(KERN_INFO "tulip_suspend(%s)\n", dev->name);
	if (tp->open) tulip_down(dev);
}

static void tulip_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pdev->driver_data;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	printk(KERN_INFO "tulip_resume(%s)\n", dev->name);
	if (tp->open) tulip_up(dev);
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
