#ifndef __LINUX_USB_UHCI_H
#define __LINUX_USB_UHCI_H

/*  $Id: usb-uhci-hcd.h,v 1.1 2002/05/14 20:36:57 acher Exp $ */

#ifndef CONFIG_PCI
#error "UHCI needs the CONFIG_PCI option!"
#endif

#define MODNAME "usb-uhci-hcd"
#define UHCI_LATENCY_TIMER 0

static __inline__ void uhci_wait_ms(unsigned int ms)
{
	if(!in_interrupt())
	{
		current->state = TASK_UNINTERRUPTIBLE;
		schedule_timeout(1 + ms * HZ / 1000);
	}
	else
		mdelay(ms);
}

/* Command register */
#define   USBCMD		0
#define   USBCMD_RS		0x0001	/* Run/Stop */
#define   USBCMD_HCRESET	0x0002	/* Host reset */
#define   USBCMD_GRESET		0x0004	/* Global reset */
#define   USBCMD_EGSM		0x0008	/* Global Suspend Mode */
#define   USBCMD_FGR		0x0010	/* Force Global Resume */
#define   USBCMD_SWDBG		0x0020	/* SW Debug mode */
#define   USBCMD_CF		0x0040	/* Config Flag (sw only) */
#define   USBCMD_MAXP		0x0080	/* Max Packet (0 = 32, 1 = 64) */

/* Status register */
#define   USBSTS		2
#define   USBSTS_USBINT		0x0001	/* Interrupt due to IOC */
#define   USBSTS_ERROR		0x0002	/* Interrupt due to error */
#define   USBSTS_RD		0x0004	/* Resume Detect */
#define   USBSTS_HSE		0x0008	/* Host System Error - basically PCI problems */
#define   USBSTS_HCPE		0x0010	/* Host Controller Process Error - the scripts were buggy */
#define   USBSTS_HCH		0x0020	/* HC Halted */

/* Interrupt enable register */
#define   USBINTR		4
#define   USBINTR_TIMEOUT	0x0001	/* Timeout/CRC error enable */
#define   USBINTR_RESUME	0x0002	/* Resume interrupt enable */
#define   USBINTR_IOC		0x0004	/* Interrupt On Complete enable */
#define   USBINTR_SP		0x0008	/* Short packet interrupt enable */

#define USBFRNUM	6
#define USBFLBASEADD	8
#define USBSOF		12

/* USB port status and control registers */
#define   USBPORTSC1	16
#define   USBPORTSC2	18
#define   USBPORTSC_CCS		0x0001	/* Current Connect Status ("device present") */
#define   USBPORTSC_CSC		0x0002	/* Connect Status Change */
#define   USBPORTSC_PE		0x0004	/* Port Enable */
#define   USBPORTSC_PEC		0x0008	/* Port Enable Change */
#define   USBPORTSC_LS		0x0030	/* Line Status */
#define   USBPORTSC_RD		0x0040	/* Resume Detect */
#define   USBPORTSC_LSDA	0x0100	/* Low Speed Device Attached */
#define   USBPORTSC_PR		0x0200	/* Port Reset */
#define   USBPORTSC_SUSP	0x1000	/* Suspend */

/* Legacy support register */
#define USBLEGSUP 0xc0
#define USBLEGSUP_DEFAULT 0x2000	/* only PIRQ enable set */

#define UHCI_NULL_DATA_SIZE	0x7ff	/* for UHCI controller TD */
#define UHCI_PID 		0xff	/* PID MASK */

#define UHCI_PTR_BITS		0x000F
#define UHCI_PTR_TERM		0x0001
#define UHCI_PTR_QH		0x0002
#define UHCI_PTR_DEPTH		0x0004

#define UHCI_NUMFRAMES		1024	/* in the frame list [array] */
#define UHCI_MAX_SOF_NUMBER	2047	/* in an SOF packet */
#define CAN_SCHEDULE_FRAMES	1000	/* how far future frames can be scheduled */

/* for TD <status> */
#define TD_CTRL_SPD		(1 << 29)	/* Short Packet Detect */
#define TD_CTRL_C_ERR_MASK	(3 << 27)	/* Error Counter bits */
#define TD_CTRL_LS		(1 << 26)	/* Low Speed Device */
#define TD_CTRL_IOS		(1 << 25)	/* Isochronous Select */
#define TD_CTRL_IOC		(1 << 24)	/* Interrupt on Complete */
#define TD_CTRL_ACTIVE		(1 << 23)	/* TD Active */
#define TD_CTRL_STALLED		(1 << 22)	/* TD Stalled */
#define TD_CTRL_DBUFERR		(1 << 21)	/* Data Buffer Error */
#define TD_CTRL_BABBLE		(1 << 20)	/* Babble Detected */
#define TD_CTRL_NAK		(1 << 19)	/* NAK Received */
#define TD_CTRL_CRCTIMEO	(1 << 18)	/* CRC/Time Out Error */
#define TD_CTRL_BITSTUFF	(1 << 17)	/* Bit Stuff Error */
#define TD_CTRL_ACTLEN_MASK	0x7ff	/* actual length, encoded as n - 1 */

#define TD_CTRL_ANY_ERROR	(TD_CTRL_STALLED | TD_CTRL_DBUFERR | \
				 TD_CTRL_BABBLE | TD_CTRL_CRCTIME | TD_CTRL_BITSTUFF)

#define uhci_status_bits(ctrl_sts)	(ctrl_sts & 0xFE0000)
#define uhci_actual_length(desc)	((le32_to_cpu(desc->hw.td.status) + 1) & TD_CTRL_ACTLEN_MASK)	/* 1-based */

/* for TD <flags>: */
#define UHCI_TD_REMOVE		0x0001	/* Remove when done */

/* for TD <info>: (a.k.a. Token) */
#define TD_TOKEN_TOGGLE		19

#define uhci_maxlen(token)	((token) >> 21)
#define uhci_toggle(token)	(((token) >> TD_TOKEN_TOGGLE) & 1)
#define uhci_endpoint(token)	(((token) >> 15) & 0xf)
#define uhci_devaddr(token)	(((token) >> 8) & 0x7f)
#define uhci_devep(token)	(((token) >> 8) & 0x7ff)
#define uhci_packetid(token)	((token) & 0xff)
#define uhci_packetout(token)	(uhci_packetid(token) != USB_PID_IN)
#define uhci_packetin(token)	(uhci_packetid(token) == USB_PID_IN)

#define uhci_do_toggle(urb)     usb_dotoggle (urb->dev, usb_pipeendpoint (urb->pipe), usb_pipeout (urb->pipe))
#define uhci_get_toggle(urb)    usb_gettoggle (urb->dev, usb_pipeendpoint (urb->pipe), usb_pipeout (urb->pipe))

/* ------------------------------------------------------------------------------------
   TD/QH-structures
   ------------------------------------------------------------------------------------ */

typedef enum {
	TD_TYPE, QH_TYPE
} uhci_desc_type_t;

typedef struct {
	__u32 link;
	__u32 status;
	__u32 info;
	__u32 buffer;
} uhci_td_t, *puhci_td_t;

typedef struct {
	__u32 head;
	__u32 element;		/* Queue element pointer */
} uhci_qh_t, *puhci_qh_t;

typedef struct {
	union {
		uhci_td_t 	td;
		uhci_qh_t 	qh;
	} hw;
	uhci_desc_type_t 	type;
	dma_addr_t 		dma_addr;
	struct list_head 	horizontal;
	struct list_head 	vertical;
	struct list_head 	desc_list;
	int 			last_used;
} uhci_desc_t, *puhci_desc_t;

typedef struct {
	struct list_head 	desc_list;	// list pointer to all corresponding TDs/QHs associated with this request
	struct list_head 	urb_list;
	struct urb              *urb;  // urb to which this data belongs
	dma_addr_t 		setup_packet_dma;
	dma_addr_t 		transfer_buffer_dma;
	unsigned long 		started;
	struct urb		*next_queued_urb;	// next queued urb for this EP
	struct urb 		*prev_queued_urb;
	uhci_desc_t		*bottom_qh;
	uhci_desc_t		*next_qh;       	// next helper QH
	char 			use_loop;
	char 			flags;
} urb_priv_t, *purb_priv_t;

struct uhci_hcd {
	unsigned int 		maxports;
	int 			running;
	int 			apm_state;

	struct uhci_hcd 	*next;	// chain of uhci device contexts

	spinlock_t 		urb_list_lock;	// lock to keep consistency 
	spinlock_t 		qh_lock;
	spinlock_t 		td_lock;
	atomic_t 		avoid_bulk;
	
	__u32 			*framelist;
	dma_addr_t 		framelist_dma;
	uhci_desc_t 		**iso_td;
	uhci_desc_t 		*int_chain[8];
	uhci_desc_t 		*ls_control_chain;
	uhci_desc_t 		*control_chain;
	uhci_desc_t 		*bulk_chain;
	uhci_desc_t 		*chain_end;
	uhci_desc_t 		*td1ms;
	uhci_desc_t 		*td32ms;
	uhci_desc_t 		*td128ms;

	struct list_head 	urb_list;	// list of all pending urbs
	struct list_head 	free_desc_qh;   // Cool down pool for QH
	struct list_head 	free_desc_td;   // Cool down pool for ISO/INT-TDs
	struct list_head 	urb_unlinked;	// list of all unlinked  urbs
	struct pci_dev 		*uhci_pci;
	struct pci_pool 	*desc_pool;

	int 			unlink_urb_done;
	int 			loop_usage;            // URBs using bandwidth reclamation
	long 			timeout_check;
	int 			timeout_urbs;
	long 			last_error_time;          // last error output in uhci_interrupt()
	
	long                    last_hcd_irq;
	int                     reanimations;
	int                     need_init;

	// Framework state
	struct usb_hcd          hcd;
};

#define hcd_to_uhci(hcd_ptr) list_entry(hcd_ptr, struct uhci_hcd, hcd)

#define MAKE_TD_ADDR(a) ((a)->dma_addr&~UHCI_PTR_QH)
#define MAKE_QH_ADDR(a) ((a)->dma_addr|UHCI_PTR_QH)
#define UHCI_GET_CURRENT_FRAME(uhci) (inw ((int)(uhci->hcd.regs) + USBFRNUM))

#define CLEAN_TRANSFER_NO_DELETION 0
#define CLEAN_TRANSFER_REGULAR 1
#define CLEAN_TRANSFER_DELETION_MARK 2

#define CLEAN_NOT_FORCED 0
#define CLEAN_FORCED 1

#define PROCESS_ISO_REGULAR 0
#define PROCESS_ISO_FORCE 1

#define PROCESS_INT_REGULAR 0
#define PROCESS_INT_REMOVE 1

#define UNLINK_ASYNC_STORE_URB 0
#define UNLINK_ASYNC_DONT_STORE 1

#define is_td_active(desc) (desc->hw.td.status & cpu_to_le32(TD_CTRL_ACTIVE))

#define set_qh_head(desc,val) (desc)->hw.qh.head=cpu_to_le32(val)
#define set_qh_element(desc,val) (desc)->hw.qh.element=cpu_to_le32(val)
#define set_td_link(desc,val) (desc)->hw.td.link=cpu_to_le32(val)
#define set_td_ioc(desc) (desc)->hw.td.status |= cpu_to_le32(TD_CTRL_IOC)
#define clr_td_ioc(desc) (desc)->hw.td.status &= cpu_to_le32(~TD_CTRL_IOC)


#endif
