/*
	drivers/net/tulip/tulip.h

	Copyright 2000  The Linux Kernel Team
	Written/copyright 1994-1999 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

*/

#ifndef __NET_TULIP_H__
#define __NET_TULIP_H__

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <asm/io.h>



/* undefine, or define to various debugging levels (>4 == obscene levels) */
#undef TULIP_DEBUG


#ifdef TULIP_DEBUG
/* note: prints function name for you */
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif




struct tulip_chip_table {
	char *chip_name;
	int io_size;
	int valid_intrs;	/* CSR7 interrupt enable settings */
	int flags;
	void (*media_timer) (unsigned long data);
};


enum tbl_flag {
	HAS_MII = 1,
	HAS_MEDIA_TABLE = 2,
	CSR12_IN_SROM = 4,
	ALWAYS_CHECK_MII = 8,
	HAS_ACPI = 0x10,
	MC_HASH_ONLY = 0x20,	/* Hash-only multicast filter. */
	HAS_PNICNWAY = 0x80,
	HAS_NWAY = 0x40,	/* Uses internal NWay xcvr. */
	HAS_INTR_MITIGATION = 0x100,
	IS_ASIX = 0x200,
	HAS_8023X = 0x400,
};


/* chip types.  careful!  order is VERY IMPORTANT here, as these
 * are used throughout the driver as indices into arrays */
/* Note 21142 == 21143. */
enum chips {
	DC21040 = 0,
	DC21041 = 1,
	DC21140 = 2,
	DC21142 = 3, DC21143 = 3,
	LC82C168,
	MX98713,
	MX98715,
	MX98725,
	AX88140,
	PNIC2,
	COMET,
	COMPEX9881,
	I21145,
	DM910X,
};


enum MediaIs {
	MediaIsFD = 1,
	MediaAlwaysFD = 2,
	MediaIsMII = 4,
	MediaIsFx = 8,
	MediaIs100 = 16
};


/* Offsets to the Command and Status Registers, "CSRs".  All accesses
   must be longword instructions and quadword aligned. */
enum tulip_offsets {
	CSR0 = 0,
	CSR1 = 0x08,
	CSR2 = 0x10,
	CSR3 = 0x18,
	CSR4 = 0x20,
	CSR5 = 0x28,
	CSR6 = 0x30,
	CSR7 = 0x38,
	CSR8 = 0x40,
	CSR9 = 0x48,
	CSR10 = 0x50,
	CSR11 = 0x58,
	CSR12 = 0x60,
	CSR13 = 0x68,
	CSR14 = 0x70,
	CSR15 = 0x78
};


/* The bits in the CSR5 status registers, mostly interrupt sources. */
enum status_bits {
	TimerInt = 0x800,
	SytemError = 0x2000,
	TPLnkFail = 0x1000,
	TPLnkPass = 0x10,
	NormalIntr = 0x10000,
	AbnormalIntr = 0x8000,
	RxJabber = 0x200,
	RxDied = 0x100,
	RxNoBuf = 0x80,
	RxIntr = 0x40,
	TxFIFOUnderflow = 0x20,
	TxJabber = 0x08,
	TxNoBuf = 0x04,
	TxDied = 0x02,
	TxIntr = 0x01,
};


/* The Tulip Rx and Tx buffer descriptors. */
struct tulip_rx_desc {
	s32 status;
	s32 length;
	u32 buffer1;
	u32 buffer2;
};


struct tulip_tx_desc {
	s32 status;
	s32 length;
	u32 buffer1;
	u32 buffer2;		/* We use only buffer 1.  */
};


enum desc_status_bits {
	DescOwned = 0x80000000,
	RxDescFatalErr = 0x8000,
	RxWholePkt = 0x0300,
};


enum t21041_csr13_bits {
	csr13_eng = (0xEF0<<4), /* for eng. purposes only, hardcode at EF0h */
	csr13_aui = (1<<3), /* clear to force 10bT, set to force AUI/BNC */
	csr13_cac = (1<<2), /* CSR13/14/15 autoconfiguration */
	csr13_srl = (1<<0), /* When reset, resets all SIA functions, machines */

	csr13_mask_auibnc = (csr13_eng | csr13_aui | csr13_cac | csr13_srl),
	csr13_mask_10bt = (csr13_eng | csr13_cac | csr13_srl),
};

enum t21143_csr6_bits {
	csr6_sc = (1<<31),
	csr6_ra = (1<<30),
	csr6_ign_dest_msb = (1<<26),
	csr6_mbo = (1<<25),
	csr6_scr = (1<<24),  /* scramble mode flag: can't be set */
	csr6_pcs = (1<<23),  /* Enables PCS functions (symbol mode requires csr6_ps be set) default is set */
	csr6_ttm = (1<<22),  /* Transmit Threshold Mode, set for 10baseT, 0 for 100BaseTX */
	csr6_sf = (1<<21),   /* Store and forward. If set ignores TR bits */
	csr6_hbd = (1<<19),  /* Heart beat disable. Disables SQE function in 10baseT */
	csr6_ps = (1<<18),   /* Port Select. 0 (defualt) = 10baseT, 1 = 100baseTX: can't be set */
	csr6_ca = (1<<17),   /* Collision Offset Enable. If set uses special algorithm in low collision situations */
	csr6_trh = (1<<15),  /* Transmit Threshold high bit */
	csr6_trl = (1<<14),  /* Transmit Threshold low bit */

	/***************************************************************
	 * This table shows transmit threshold values based on media   *
	 * and these two registers (from PNIC1 & 2 docs) Note: this is *
	 * all meaningless if sf is set.                               *
	 ***************************************************************/

	/***********************************
	 * (trh,trl) * 100BaseTX * 10BaseT *
	 ***********************************
	 *   (0,0)   *     128   *    72   *
	 *   (0,1)   *     256   *    96   *
	 *   (1,0)   *     512   *   128   *
	 *   (1,1)   *    1024   *   160   *
	 ***********************************/

	csr6_st = (1<<13),   /* Transmit conrol: 1 = transmit, 0 = stop */
	csr6_fc = (1<<12),   /* Forces a collision in next transmission (for testing in loopback mode) */
	csr6_om_int_loop = (1<<10), /* internal (FIFO) loopback flag */
	csr6_om_ext_loop = (1<<11), /* external (PMD) loopback flag */
	/* set both and you get (PHY) loopback */
	csr6_fd = (1<<9),    /* Full duplex mode, disables hearbeat, no loopback */
	csr6_pm = (1<<7),    /* Pass All Multicast */
	csr6_pr = (1<<6),    /* Promiscuous mode */
	csr6_sb = (1<<5),    /* Start(1)/Stop(0) backoff counter */
	csr6_if = (1<<4),    /* Inverse Filtering, rejects only addresses in address table: can't be set */
	csr6_pb = (1<<3),    /* Pass Bad Frames, (1) causes even bad frames to be passed on */
	csr6_ho = (1<<2),    /* Hash-only filtering mode: can't be set */
	csr6_sr = (1<<1),    /* Start(1)/Stop(0) Receive */
	csr6_hp = (1<<0),    /* Hash/Perfect Receive Filtering Mode: can't be set */

	csr6_mask_capture = (csr6_sc | csr6_ca),
	csr6_mask_defstate = (csr6_mask_capture | csr6_mbo),
	csr6_mask_hdcap = (csr6_mask_defstate | csr6_hbd | csr6_ps),
	csr6_mask_hdcaptt = (csr6_mask_hdcap  | csr6_trh | csr6_trl),
	csr6_mask_fullcap = (csr6_mask_hdcaptt | csr6_fd),
	csr6_mask_fullpromisc = (csr6_pr | csr6_pm),
	csr6_mask_filters = (csr6_hp | csr6_ho | csr6_if),
	csr6_mask_100bt = (csr6_scr | csr6_pcs | csr6_hbd),
};


/* Keep the ring sizes a power of two for efficiency.
   Making the Tx ring too large decreases the effectiveness of channel
   bonding and packet priority.
   There are no ill effects from too-large receive rings. */
#define TX_RING_SIZE	16
#define RX_RING_SIZE	32


#define PKT_BUF_SZ		1536	/* Size of each temporary Rx buffer. */


/* Ring-wrap flag in length field, use for last ring entry.
	0x01000000 means chain on buffer2 address,
	0x02000000 means use the ring start address in CSR2/3.
   Note: Some work-alike chips do not function correctly in chained mode.
   The ASIX chip works only in chained mode.
   Thus we indicates ring mode, but always write the 'next' field for
   chained mode as well.
*/
#define DESC_RING_WRAP 0x02000000


/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x02	/* EEPROM shift clock. */
#define EE_CS			0x01	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x04	/* Data from the Tulip to EEPROM. */
#define EE_WRITE_0		0x01
#define EE_WRITE_1		0x05
#define EE_DATA_READ	0x08	/* Data from the EEPROM chip. */
#define EE_ENB			(0x4800 | EE_CS)

/* Delay between EEPROM clock transitions.
   Even at 33Mhz current PCI implementations don't overrun the EEPROM clock.
   We add a bus turn-around to insure that this remains true. */
#define eeprom_delay()	inl(ee_addr)

/* The EEPROM commands include the alway-set leading bit. */
#define EE_READ_CMD		(6)

#define EEPROM_SIZE 128 	/* 2 << EEPROM_ADDRLEN */


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


#define RUN_AT(x) (jiffies + (x))


#if defined(__i386__)			/* AKA get_unaligned() */
#define get_u16(ptr) (*(u16 *)(ptr))
#else
#define get_u16(ptr) (((u8*)(ptr))[0] + (((u8*)(ptr))[1]<<8))
#endif

struct medialeaf {
	u8 type;
	u8 media;
	unsigned char *leafdata;
};


struct mediatable {
	u16 defaultmedia;
	u8 leafcount;
	u8 csr12dir;		/* General purpose pin directions. */
	unsigned has_mii:1;
	unsigned has_nonmii:1;
	unsigned has_reset:6;
	u32 csr15dir;
	u32 csr15val;		/* 21143 NWay setting. */
	struct medialeaf mleaf[0];
};


struct mediainfo {
	struct mediainfo *next;
	int info_type;
	int index;
	unsigned char *info;
};

struct ring_info {
	struct sk_buff	*skb;
	dma_addr_t	mapping;
};


struct tulip_private {
	const char *product_name;
	struct net_device *next_module;
	struct tulip_rx_desc *rx_ring;
	struct tulip_tx_desc *tx_ring;
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct ring_info tx_buffers[TX_RING_SIZE];
	/* The addresses of receive-in-place skbuffs. */
	struct ring_info rx_buffers[RX_RING_SIZE];
	u16 setup_frame[96];	/* Pseudo-Tx frame to init address table. */
	int chip_id;
	int revision;
	int flags;
	struct net_device_stats stats;
	struct timer_list timer;	/* Media selection timer. */
	spinlock_t lock;
	unsigned int cur_rx, cur_tx;	/* The next free ring entry */
	unsigned int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */
	unsigned int full_duplex:1;	/* Full-duplex operation requested. */
	unsigned int full_duplex_lock:1;
	unsigned int fake_addr:1;	/* Multiport board faked address. */
	unsigned int default_port:4;	/* Last dev->if_port value. */
	unsigned int media2:4;	/* Secondary monitored media port. */
	unsigned int medialock:1;	/* Don't sense media type. */
	unsigned int mediasense:1;	/* Media sensing in progress. */
	unsigned int nway:1, nwayset:1;		/* 21143 internal NWay. */
	unsigned int csr0;	/* CSR0 setting. */
	unsigned int csr6;	/* Current CSR6 control settings. */
	unsigned char eeprom[EEPROM_SIZE];	/* Serial EEPROM contents. */
	void (*link_change) (struct net_device * dev, int csr5);
	u16 to_advertise;	/* NWay capabilities advertised.  */
	u16 lpar;		/* 21143 Link partner ability. */
	u16 advertising[4];
	signed char phys[4], mii_cnt;	/* MII device addresses. */
	struct mediatable *mtable;
	int cur_index;		/* Current media index. */
	int saved_if_port;
	struct pci_dev *pdev;
	int ttimer;
	int susp_rx;
	unsigned long nir;
	unsigned long base_addr;
	int pad0, pad1;		/* Used for 8-byte alignment */
};


struct eeprom_fixup {
	char *name;
	unsigned char addr0;
	unsigned char addr1;
	unsigned char addr2;
	u16 newtable[32];	/* Max length below. */
};


/* 21142.c */
extern u16 t21142_csr14[];
void t21142_timer(unsigned long data);
void t21142_start_nway(struct net_device *dev);
void t21142_lnk_change(struct net_device *dev, int csr5);

/* eeprom.c */
void tulip_parse_eeprom(struct net_device *dev);
int tulip_read_eeprom(long ioaddr, int location, int addr_len);

/* interrupt.c */
extern unsigned int tulip_max_interrupt_work;
extern int tulip_rx_copybreak;
void tulip_interrupt(int irq, void *dev_instance, struct pt_regs *regs);

/* media.c */
int tulip_mdio_read(struct net_device *dev, int phy_id, int location);
void tulip_mdio_write(struct net_device *dev, int phy_id, int location, int value);
void tulip_select_media(struct net_device *dev, int startup);
int tulip_check_duplex(struct net_device *dev);

/* pnic.c */
void pnic_do_nway(struct net_device *dev);
void pnic_lnk_change(struct net_device *dev, int csr5);
void pnic_timer(unsigned long data);

/* timer.c */
void tulip_timer(unsigned long data);
void mxic_timer(unsigned long data);
void comet_timer(unsigned long data);

/* tulip_core.c */
extern int tulip_debug;
extern const char * const medianame[];
extern const char tulip_media_cap[];
extern struct tulip_chip_table tulip_tbl[];
extern u8 t21040_csr13[];
extern u16 t21041_csr13[];
extern u16 t21041_csr14[];
extern u16 t21041_csr15[];


static inline void tulip_outl_csr (struct tulip_private *tp, u32 newValue, enum tulip_offsets offset)
{
	outl (newValue, tp->base_addr + offset);
}

static inline void tulip_stop_rxtx(struct tulip_private *tp, u32 csr6mask)
{
	tulip_outl_csr(tp, csr6mask & ~(csr6_st | csr6_sr), CSR6);
}

static inline void tulip_restart_rxtx(struct tulip_private *tp, u32 csr6mask)
{
	tulip_outl_csr(tp, csr6mask | csr6_sr, CSR6);
	tulip_outl_csr(tp, csr6mask | csr6_st | csr6_sr, CSR6);
}

#endif /* __NET_TULIP_H__ */
