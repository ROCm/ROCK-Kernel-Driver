
/*
 * snull.h -- definitions for the network module
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 */

/* version dependencies have been confined to a separate file */

#define SGI_MFE		(MACE_BASE+MACE_ENET)
/*		(0xBF280000)*/

/* Tunable parameters */
#define TX_RING_ENTRIES 64	/* 64-512?*/

#define RX_RING_ENTRIES 16 /* Do not change */
/* Internal constants */
#define TX_RING_BUFFER_SIZE	(TX_RING_ENTRIES*sizeof(tx_packet))
#define RX_BUFFER_SIZE 1546 /* ethenet packet size */
#define METH_RX_BUFF_SIZE 4096
#define RX_BUFFER_OFFSET (sizeof(rx_status_vector)+2) /* staus vector + 2 bytes of padding */
#define RX_BUCKET_SIZE 256



/* For more detailed explanations of what each field menas,
   see Nick's great comments to #defines below (or docs, if
   you are lucky enough toget hold of them :)*/

/* tx status vector is written over tx command header upon
   dma completion. */

typedef struct tx_status_vector {
	u64		sent:1; /* always set to 1...*/
	u64		pad0:34;/* always set to 0 */
	u64		flags:9;			/*I'm too lazy to specify each one separately at the moment*/
	u64		col_retry_cnt:4;	/*collision retry count*/
	u64		len:16;				/*Transmit length in bytes*/
} tx_status_vector;

/*
 * Each packet is 128 bytes long.
 * It consists of header, 0-3 concatination
 * buffer pointers and up to 120 data bytes.
 */
typedef struct tx_packet_hdr {
	u64		pad1:36; /*should be filled with 0 */
	u64		cat_ptr3_valid:1,	/*Concatination pointer valid flags*/
			cat_ptr2_valid:1,
			cat_ptr1_valid:1;
	u64		tx_int_flag:1;		/*Generate TX intrrupt when packet has been sent*/
	u64		term_dma_flag:1;	/*Terminate transmit DMA on transmit abort conditions*/
	u64		data_offset:7;		/*Starting byte offset in ring data block*/
	u64		data_len:16;		/*Length of valid data in bytes-1*/
} tx_packet_hdr;
typedef union tx_cat_ptr {
	struct {
		u64		pad2:16; /* should be 0 */
		u64		len:16;				/*length of buffer data - 1*/
		u64		start_addr:29;		/*Physical starting address*/
		u64		pad1:3; /* should be zero */
	} form;
	u64 raw;
} tx_cat_ptr;

typedef struct tx_packet {
	union {
		tx_packet_hdr header;
		tx_status_vector res;
		u64 raw;
	}header;
	union {
		tx_cat_ptr cat_buf[3];
		char dt[120];
	} data;
} tx_packet;

typedef union rx_status_vector {
	struct {
		u64		pad1:1;/*fill it with ones*/
		u64		pad2:15;/*fill with 0*/
		u64		ip_chk_sum:16;
		u64		seq_num:5;
		u64		mac_addr_match:1;
		u64		mcast_addr_match:1;
		u64		carrier_event_seen:1;
		u64		bad_packet:1;
		u64		long_event_seen:1;
		u64		invalid_preamble:1;
		u64		broadcast:1;
		u64		multicast:1;
		u64		crc_error:1;
		u64		huh:1;/*???*/
		u64		rx_code_violation:1;
		u64		rx_len:16;
	} parsed;
	u64 raw;
} rx_status_vector;

typedef struct rx_packet {
	rx_status_vector status;
        u64 pad[3]; /* For whatever reason, there needs to be 4 double-word offset */
        u16 pad2;
	char buf[METH_RX_BUFF_SIZE-sizeof(rx_status_vector)-3*sizeof(u64)-sizeof(u16)];/* data */
} rx_packet;

typedef struct meth_regs {
	u64		mac_ctrl;		/*0x00,rw,31:0*/
	u64		int_flags;		/*0x08,rw,30:0*/
	u64		dma_ctrl;		/*0x10,rw,15:0*/
	u64		timer;			/*0x18,rw,5:0*/
	u64		int_tx;			/*0x20,wo,0:0*/
	u64		int_rx;			/*0x28,wo,9:4*/
	struct {
		u32 tx_info_pad;
		u32 rptr:16,wptr:16;
	}		tx_info;		/*0x30,rw,31:0*/
	u64		tx_info_al;		/*0x38,rw,31:0*/
	struct {
		u32	rx_buff_pad1;
		u32	rx_buff_pad2:8,
			wptr:8,
			rptr:8,
			depth:8;
	}		rx_buff;		/*0x40,ro,23:0*/
	u64		rx_buff_al1;	/*0x48,ro,23:0*/
	u64		rx_buff_al2;	/*0x50,ro,23:0*/
	u64		int_update;		/*0x58,wo,31:0*/
	u32		phy_data_pad;
	u32		phy_data;		/*0x60,rw,16:0*/
	u32		phy_reg_pad;
	u32		phy_registers;	/*0x68,rw,9:0*/
	u64		phy_trans_go;	/*0x70,wo,0:0*/
	u64		backoff_seed;	/*0x78,wo,10:0*/
	u64		imq_reserved[4];/*0x80,ro,64:0(x4)*/
	/*===================================*/
	u64		mac_addr;		/*0xA0,rw,47:0, I think it's MAC address, but I'm not sure*/
	u64		mcast_addr;		/*0xA8,rw,47:0, This seems like secondary MAC address*/
	u64		mcast_filter;	/*0xB0,rw,63:0*/
	u64		tx_ring_base;	/*0xB8,rw,31:13*/
	/* Following are read-only debugging info register */
	u64		tx_pkt1_hdr;	/*0xC0,ro,63:0*/
	u64		tx_pkt1_ptr[3];	/*0xC8,ro,63:0(x3)*/
	u64		tx_pkt2_hdr;	/*0xE0,ro,63:0*/
	u64		tx_pkt2_ptr[3];	/*0xE8,ro,63:0(x3)*/
	/*===================================*/
	u32		rx_pad;
	u32		rx_fifo;
	u64		reserved[31];
}meth_regs;

	/* Bits in METH_MAC */

#define SGI_MAC_RESET		BIT(0)	/* 0: MAC110 active in run mode, 1: Global reset signal to MAC110 core is active */
#define METH_PHY_FDX		BIT(1) /* 0: Disable full duplex, 1: Enable full duplex */
#define METH_PHY_LOOP	BIT(2) /* 0: Normal operation, follows 10/100mbit and M10T/MII select, 1: loops internal MII bus */
				       /*    selects ignored */
#define METH_100MBIT		BIT(3) /* 0: 10meg mode, 1: 100meg mode */
#define METH_PHY_MII		BIT(4) /* 0: MII selected, 1: SIA selected */
				       /*   Note: when loopback is set this bit becomes collision control.  Setting this bit will */
				       /*         cause a collision to be reported. */

				       /* Bits 5 and 6 are used to determine the the Destination address filter mode */
#define METH_ACCEPT_MY 0			/* 00: Accept PHY address only */
#define METH_ACCEPT_MCAST 0x20	/* 01: Accept physical, broadcast, and multicast filter matches only */
#define METH_ACCEPT_AMCAST 0x40	/* 10: Accept physical, broadcast, and all multicast packets */
#define METH_PROMISC 0x60		/* 11: Promiscious mode */

#define METH_PHY_LINK_FAIL	BIT(7) /* 0: Link failure detection disabled, 1: Hardware scans for link failure in PHY */

#define METH_MAC_IPG	0x1ffff00

#define METH_DEFAULT_IPG ((17<<15) | (11<<22) | (21<<8))
						/* 0x172e5c00 */ /* 23, 23, 23 */ /*0x54A9500 *//*21,21,21*/
				       /* Bits 8 through 14 are used to determine Inter-Packet Gap between "Back to Back" packets */
				       /* The gap depends on the clock speed of the link, 80ns per increment for 100baseT, 800ns  */
				       /* per increment for 10BaseT */

				       /* Bits 15 through 21 are used to determine IPGR1 */

				       /* Bits 22 through 28 are used to determine IPGR2 */

#define METH_REV_SHIFT 29       /* Bits 29 through 31 are used to determine the revision */
				       /* 000: Inital revision */
				       /* 001: First revision, Improved TX concatenation */


/* DMA control bits */
#define METH_RX_OFFSET_SHIFT 12 /* Bits 12:14 of DMA control register indicate starting offset of packet data for RX operation */
#define METH_RX_DEPTH_SHIFT 4 /* Bits 8:4 define RX fifo depth -- when # of RX fifo entries != depth, interrupt is generted */

#define METH_DMA_TX_EN BIT(1) /* enable TX DMA */
#define METH_DMA_TX_INT_EN BIT(0) /* enable TX Buffer Empty interrupt */
#define METH_DMA_RX_EN BIT(15) /* Enable RX */
#define METH_DMA_RX_INT_EN BIT(9) /* Enable interrupt on RX packet */


/* RX status bits */

#define METH_RX_ST_RCV_CODE_VIOLATION BIT(16)
#define METH_RX_ST_DRBL_NBL BIT(17)
#define METH_RX_ST_CRC_ERR BIT(18)
#define METH_RX_ST_MCAST_PKT BIT(19)
#define METH_RX_ST_BCAST_PKT BIT(20)
#define METH_RX_ST_INV_PREAMBLE_CTX BIT(21)
#define METH_RX_ST_LONG_EVT_SEEN BIT(22)
#define METH_RX_ST_BAD_PACKET BIT(23)
#define METH_RX_ST_CARRIER_EVT_SEEN BIT(24)
#define METH_RX_ST_MCAST_FILTER_MATCH BIT(25)
#define METH_RX_ST_PHYS_ADDR_MATCH BIT(26)

#define METH_RX_STATUS_ERRORS \
	( \
	METH_RX_ST_RCV_CODE_VIOLATION| \
	METH_RX_ST_CRC_ERR| \
	METH_RX_ST_INV_PREAMBLE_CTX| \
	METH_RX_ST_LONG_EVT_SEEN| \
	METH_RX_ST_BAD_PACKET| \
	METH_RX_ST_CARRIER_EVT_SEEN \
	)
	/* Bits in METH_INT */
	/* Write _1_ to corresponding bit to clear */
#define METH_INT_TX_EMPTY	BIT(0)	/* 0: No interrupt pending, 1: The TX ring buffer is empty */
#define METH_INT_TX_PKT		BIT(1)	/* 0: No interrupt pending */
					      	/* 1: A TX message had the INT request bit set, the packet has been sent. */
#define METH_INT_TX_LINK_FAIL	BIT(2)	/* 0: No interrupt pending, 1: PHY has reported a link failure */
#define METH_INT_MEM_ERROR	BIT(3)	/* 0: No interrupt pending */
						/* 1: A memory error occurred durring DMA, DMA stopped, Fatal */
#define METH_INT_TX_ABORT		BIT(4)	/* 0: No interrupt pending, 1: The TX aborted operation, DMA stopped, FATAL */
#define METH_INT_RX_THRESHOLD	BIT(5)	/* 0: No interrupt pending, 1: Selected receive threshold condition Valid */
#define METH_INT_RX_UNDERFLOW	BIT(6)	/* 0: No interrupt pending, 1: FIFO was empty, packet could not be queued */
#define METH_INT_RX_OVERFLOW		BIT(7)	/* 0: No interrupt pending, 1: DMA FIFO Overflow, DMA stopped, FATAL */

#define METH_INT_RX_RPTR_MASK 0x0001F00		/* Bits 8 through 12 alias of RX read-pointer */

						/* Bits 13 through 15 are always 0. */

#define METH_INT_TX_RPTR_MASK 0x1FF0000	        /* Bits 16 through 24 alias of TX read-pointer */

#define METH_INT_SEQ_MASK    0x2E000000	        /* Bits 25 through 29 are the starting seq number for the message at the */
						/* top of the queue */

#define METH_ERRORS ( \
	METH_INT_RX_OVERFLOW|	\
	METH_INT_RX_UNDERFLOW|	\
	METH_INT_MEM_ERROR|			\
	METH_INT_TX_ABORT)

#define METH_INT_MCAST_HASH		BIT(30) /* If RX DMA is enabled the hash select logic output is latched here */

/* TX status bits */
#define METH_TX_STATUS_DONE BIT(23) /* Packet was transmitted successfully */

/* Tx command header bits */
#define METH_TX_CMD_INT_EN BIT(24) /* Generate TX interrupt when packet is sent */

/* Phy MDIO interface busy flag */
#define MDIO_BUSY    BIT(16)
#define MDIO_DATA_MASK 0xFFFF
/* PHY defines */
#define PHY_QS6612X    0x0181441    /* Quality TX */
#define PHY_ICS1889    0x0015F41    /* ICS FX */
#define PHY_ICS1890    0x0015F42    /* ICS TX */
#define PHY_DP83840    0x20005C0    /* National TX */
