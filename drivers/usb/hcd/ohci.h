/*
 * OHCI HCD (Host Controller Driver) for USB.
 * 
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2001 David Brownell <dbrownell@users.sourceforge.net>
 * 
 * This file is licenced under GPL
 * $Id: ohci.h,v 1.5 2002/01/19 00:24:01 dbrownell Exp $
 */
 
static const int cc_to_error [16] = { 

/* map OHCI status to errno values */ 
	/* No  Error  */               0,
	/* CRC Error  */               -EILSEQ,
	/* Bit Stuff  */               -EPROTO,
	/* Data Togg  */               -EILSEQ,
	/* Stall      */               -EPIPE,
	/* DevNotResp */               -ETIMEDOUT,
	/* PIDCheck   */               -EPROTO,
	/* UnExpPID   */               -EPROTO,
	/* DataOver   */               -EOVERFLOW,
	/* DataUnder  */               -EREMOTEIO,
	/* (for hw)   */               -EIO,
	/* (for hw)   */               -EIO,
	/* BufferOver */               -ECOMM,
	/* BuffUnder  */               -ENOSR,
	/* (for HCD)  */               -EALREADY,
	/* (for HCD)  */               -EALREADY 
};


/* ED States */

#define ED_NEW 		0x00		/* unused, no dummy td */
#define ED_UNLINK 	0x01		/* dummy td, maybe linked to hc */
#define ED_OPER		0x02		/* dummy td, _is_ linked to hc */

#define ED_URB_DEL  	0x08		/* masked in */

/* usb_ohci_ed */
struct ed {
	/* first fields are hardware-specified */
	__u32 hwINFO;       
	__u32 hwTailP;
	__u32 hwHeadP;
	__u32 hwNextED;

	struct ed * ed_prev;  
	__u8 int_period;
	__u8 int_branch;
	__u8 int_load; 
	__u8 int_interval;
	__u8 state;			// ED_{NEW,UNLINK,OPER}
	__u8 type; 
	__u16 last_iso;
	struct ed * ed_rm_list;

	dma_addr_t dma;
	__u32 unused [3];
} __attribute((aligned(16)));

 
/* TD info field */
#define TD_CC       0xf0000000
#define TD_CC_GET(td_p) ((td_p >>28) & 0x0f)
#define TD_CC_SET(td_p, cc) (td_p) = ((td_p) & 0x0fffffff) | (((cc) & 0x0f) << 28)
#define TD_EC       0x0C000000
#define TD_T        0x03000000
#define TD_T_DATA0  0x02000000
#define TD_T_DATA1  0x03000000
#define TD_T_TOGGLE 0x00000000
#define TD_R        0x00040000
#define TD_DI       0x00E00000
#define TD_DI_SET(X) (((X) & 0x07)<< 21)
#define TD_DP       0x00180000
#define TD_DP_SETUP 0x00000000
#define TD_DP_IN    0x00100000
#define TD_DP_OUT   0x00080000

#define TD_ISO	    0x00010000
#define TD_DEL      0x00020000

/* CC Codes */
#define TD_CC_NOERROR      0x00
#define TD_CC_CRC          0x01
#define TD_CC_BITSTUFFING  0x02
#define TD_CC_DATATOGGLEM  0x03
#define TD_CC_STALL        0x04
#define TD_DEVNOTRESP      0x05
#define TD_PIDCHECKFAIL    0x06
#define TD_UNEXPECTEDPID   0x07
#define TD_DATAOVERRUN     0x08
#define TD_DATAUNDERRUN    0x09
    /* 0x0A, 0x0B reserved for hardware */
#define TD_BUFFEROVERRUN   0x0C
#define TD_BUFFERUNDERRUN  0x0D
    /* 0x0E, 0x0F reserved for HCD */
#define TD_NOTACCESSED     0x0F


#define MAXPSW 1

struct td {
	/* first hardware fields are in all tds */
	__u32		hwINFO;
  	__u32		hwCBP;		/* Current Buffer Pointer */
  	__u32		hwNextTD;	/* Next TD Pointer */
  	__u32		hwBE;		/* Memory Buffer End Pointer */

  	__u16		hwPSW [MAXPSW];	/* PSW is only for ISO */

  	__u8		unused;
  	__u8		index;
  	struct ed	*ed;
  	struct td	*next_dl_td;
  	struct urb	*urb;

	dma_addr_t	td_dma;
	dma_addr_t	data_dma;
	__u32		unused2 [2];
} __attribute((aligned(32)));		/* iso needs 32 */

#define OHCI_ED_SKIP	(1 << 14)

/*
 * The HCCA (Host Controller Communications Area) is a 256 byte
 * structure defined in the OHCI spec. The host controller is
 * told the base address of it.  It must be 256-byte aligned.
 */
#define NUM_INTS 32	/* part of the OHCI standard */
struct ohci_hcca {
	__u32	int_table [NUM_INTS];	/* Interrupt ED table */
	__u16	frame_no;		/* current frame number */
	__u16	pad1;			/* set to 0 on each frame_no change */
	__u32	done_head;		/* info returned for an interrupt */
	u8	reserved_for_hc [116];
} __attribute((aligned(256)));

  
/*
 * Maximum number of root hub ports.  
 */
#define MAX_ROOT_PORTS	15	/* maximum OHCI root hub ports */

/*
 * This is the structure of the OHCI controller's memory mapped I/O
 * region.  This is Memory Mapped I/O.  You must use the readl() and
 * writel() macros defined in asm/io.h to access these!!
 */
struct ohci_regs {
	/* control and status registers */
	__u32	revision;
	__u32	control;
	__u32	cmdstatus;
	__u32	intrstatus;
	__u32	intrenable;
	__u32	intrdisable;

	/* memory pointers */
	__u32	hcca;
	__u32	ed_periodcurrent;
	__u32	ed_controlhead;
	__u32	ed_controlcurrent;
	__u32	ed_bulkhead;
	__u32	ed_bulkcurrent;
	__u32	donehead;

	/* frame counters */
	__u32	fminterval;
	__u32	fmremaining;
	__u32	fmnumber;
	__u32	periodicstart;
	__u32	lsthresh;

	/* Root hub ports */
	struct	ohci_roothub_regs {
		__u32	a;
		__u32	b;
		__u32	status;
		__u32	portstatus [MAX_ROOT_PORTS];
	} roothub;

	/* and some optional registers for legacy compatibility */
} __attribute((aligned(32)));


/* OHCI CONTROL AND STATUS REGISTER MASKS */

/*
 * HcControl (control) register masks
 */
#define OHCI_CTRL_CBSR	(3 << 0)	/* control/bulk service ratio */
#define OHCI_CTRL_PLE	(1 << 2)	/* periodic list enable */
#define OHCI_CTRL_IE	(1 << 3)	/* isochronous enable */
#define OHCI_CTRL_CLE	(1 << 4)	/* control list enable */
#define OHCI_CTRL_BLE	(1 << 5)	/* bulk list enable */
#define OHCI_CTRL_HCFS	(3 << 6)	/* host controller functional state */
#define OHCI_CTRL_IR	(1 << 8)	/* interrupt routing */
#define OHCI_CTRL_RWC	(1 << 9)	/* remote wakeup connected */
#define OHCI_CTRL_RWE	(1 << 10)	/* remote wakeup enable */

/* pre-shifted values for HCFS */
#	define OHCI_USB_RESET	(0 << 6)
#	define OHCI_USB_RESUME	(1 << 6)
#	define OHCI_USB_OPER	(2 << 6)
#	define OHCI_USB_SUSPEND	(3 << 6)

/*
 * HcCommandStatus (cmdstatus) register masks
 */
#define OHCI_HCR	(1 << 0)	/* host controller reset */
#define OHCI_CLF  	(1 << 1)	/* control list filled */
#define OHCI_BLF  	(1 << 2)	/* bulk list filled */
#define OHCI_OCR  	(1 << 3)	/* ownership change request */
#define OHCI_SOC  	(3 << 16)	/* scheduling overrun count */

/*
 * masks used with interrupt registers:
 * HcInterruptStatus (intrstatus)
 * HcInterruptEnable (intrenable)
 * HcInterruptDisable (intrdisable)
 */
#define OHCI_INTR_SO	(1 << 0)	/* scheduling overrun */
#define OHCI_INTR_WDH	(1 << 1)	/* writeback of done_head */
#define OHCI_INTR_SF	(1 << 2)	/* start frame */
#define OHCI_INTR_RD	(1 << 3)	/* resume detect */
#define OHCI_INTR_UE	(1 << 4)	/* unrecoverable error */
#define OHCI_INTR_FNO	(1 << 5)	/* frame number overflow */
#define OHCI_INTR_RHSC	(1 << 6)	/* root hub status change */
#define OHCI_INTR_OC	(1 << 30)	/* ownership change */
#define OHCI_INTR_MIE	(1 << 31)	/* master interrupt enable */


/* OHCI ROOT HUB REGISTER MASKS */
 
/* roothub.portstatus [i] bits */
#define RH_PS_CCS            0x00000001   	/* current connect status */
#define RH_PS_PES            0x00000002   	/* port enable status*/
#define RH_PS_PSS            0x00000004   	/* port suspend status */
#define RH_PS_POCI           0x00000008   	/* port over current indicator */
#define RH_PS_PRS            0x00000010  	/* port reset status */
#define RH_PS_PPS            0x00000100   	/* port power status */
#define RH_PS_LSDA           0x00000200    	/* low speed device attached */
#define RH_PS_CSC            0x00010000 	/* connect status change */
#define RH_PS_PESC           0x00020000   	/* port enable status change */
#define RH_PS_PSSC           0x00040000    	/* port suspend status change */
#define RH_PS_OCIC           0x00080000    	/* over current indicator change */
#define RH_PS_PRSC           0x00100000   	/* port reset status change */

/* roothub.status bits */
#define RH_HS_LPS	     0x00000001		/* local power status */
#define RH_HS_OCI	     0x00000002		/* over current indicator */
#define RH_HS_DRWE	     0x00008000		/* device remote wakeup enable */
#define RH_HS_LPSC	     0x00010000		/* local power status change */
#define RH_HS_OCIC	     0x00020000		/* over current indicator change */
#define RH_HS_CRWE	     0x80000000		/* clear remote wakeup enable */

/* roothub.b masks */
#define RH_B_DR		0x0000ffff		/* device removable flags */
#define RH_B_PPCM	0xffff0000		/* port power control mask */

/* roothub.a masks */
#define	RH_A_NDP	(0xff << 0)		/* number of downstream ports */
#define	RH_A_PSM	(1 << 8)		/* power switching mode */
#define	RH_A_NPS	(1 << 9)		/* no power switching */
#define	RH_A_DT		(1 << 10)		/* device type (mbz) */
#define	RH_A_OCPM	(1 << 11)		/* over current protection mode */
#define	RH_A_NOCP	(1 << 12)		/* no over current protection */
#define	RH_A_POTPGT	(0xff << 24)		/* power on to power good time */


/* urb */
typedef struct urb_priv
{
	struct ed		*ed;
	__u16			length;		// # tds in this request
	__u16			td_cnt;		// tds already serviced
	int   			state;
	struct td		*td [0];	// all TDs in this request

} urb_priv_t;

#define URB_DEL 1


/* Hash struct used for TD/ED hashing */
struct hash_t {
	void		*virt;
	dma_addr_t	dma;
	struct hash_t	*next; // chaining for collision cases
};

/* List of TD/ED hash entries */
struct hash_list_t {
	struct hash_t	*head;
	struct hash_t	*tail;
};

#define TD_HASH_SIZE    64    /* power'o'two */
#define ED_HASH_SIZE    64    /* power'o'two */

#define TD_HASH_FUNC(td_dma) ((td_dma ^ (td_dma >> 5)) % TD_HASH_SIZE)
#define ED_HASH_FUNC(ed_dma) ((ed_dma ^ (ed_dma >> 5)) % ED_HASH_SIZE)


/*
 * This is the full ohci controller description
 *
 * Note how the "proper" USB information is just
 * a subset of what the full implementation needs. (Linus)
 */

struct ohci_hcd {
	spinlock_t		lock;

	/*
	 * I/O memory used to communicate with the HC (uncached);
	 */
	struct ohci_regs	*regs;

	/*
	 * main memory used to communicate with the HC (uncached)
	 */
	struct ohci_hcca	*hcca;
	dma_addr_t		hcca_dma;

	struct ed		*ed_rm_list [2];	/* to be removed */

	struct ed		*ed_bulktail;		/* last in bulk list */
	struct ed		*ed_controltail;	/* last in ctrl list */
 	struct ed		*ed_isotail;		/* last in iso list */

#ifdef CONFIG_PCI
	struct pci_pool		*td_cache;
	struct pci_pool		*ed_cache;
	struct hash_list_t	td_hash [TD_HASH_SIZE];
	struct hash_list_t	ed_hash [ED_HASH_SIZE];
#endif

	/*
	 * driver state
	 */
	int			disabled;	/* e.g. got a UE, we're hung */
	int			sleeping;
	int			ohci_int_load [NUM_INTS];
	u32 			hc_control;	/* copy of hc control reg */
	struct usb_device	*dev [128];

	unsigned long		flags;		/* for HC bugs */
#define	OHCI_QUIRK_AMD756	0x01			/* erratum #4 */

	/*
	 * framework state
	 */
	struct usb_hcd		hcd;
};

#define hcd_to_ohci(hcd_ptr) list_entry(hcd_ptr, struct ohci_hcd, hcd)

