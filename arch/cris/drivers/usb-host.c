/*
 * usb-host.c: ETRAX 100LX USB Host Controller Driver (HCD)
 *
 * Copyright (c) 2001 Axis Communications AB.
 *
 * $Id: usb-host.c,v 1.11 2001/09/26 11:52:16 bjornw Exp $
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/list.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/svinto.h>

#include <linux/usb.h>
#include "usb-host.h"

#define ETRAX_USB_HC_IRQ USB_HC_IRQ_NBR
#define ETRAX_USB_RX_IRQ USB_DMA_RX_IRQ_NBR
#define ETRAX_USB_TX_IRQ USB_DMA_TX_IRQ_NBR

static const char *usb_hcd_version = "$Revision: 1.11 $";

#undef KERN_DEBUG
#define KERN_DEBUG ""

#undef USB_DEBUG_RH
#undef USB_DEBUG_EP
#undef USB_DEBUG_DESC
#undef USB_DEBUG_TRACE
#undef USB_DEBUG_CTRL
#undef USB_DEBUG_BULK
#undef USB_DEBUG_INTR

#ifdef USB_DEBUG_RH
#define dbg_rh(format, arg...) printk(KERN_DEBUG __FILE__ ": (RH) " format "\n" , ## arg)
#else
#define dbg_rh(format, arg...) do {} while (0)
#endif

#ifdef USB_DEBUG_EP
#define dbg_ep(format, arg...) printk(KERN_DEBUG __FILE__ ": (EP) " format "\n" , ## arg)
#else
#define dbg_ep(format, arg...) do {} while (0)
#endif

#ifdef USB_DEBUG_CTRL
#define dbg_ctrl(format, arg...) printk(KERN_DEBUG __FILE__ ": (CTRL) " format "\n" , ## arg)
#else
#define dbg_ctrl(format, arg...) do {} while (0)
#endif

#ifdef USB_DEBUG_BULK
#define dbg_bulk(format, arg...) printk(KERN_DEBUG __FILE__ ": (BULK) " format "\n" , ## arg)
#else
#define dbg_bulk(format, arg...) do {} while (0)
#endif

#ifdef USB_DEBUG_INTR
#define dbg_intr(format, arg...) printk(KERN_DEBUG __FILE__ ": (INTR) " format "\n" , ## arg)
#else
#define dbg_intr(format, arg...) do {} while (0)
#endif

#ifdef USB_DEBUG_TRACE
#define DBFENTER (printk(KERN_DEBUG __FILE__ ": Entering : " __FUNCTION__ "\n"))
#define DBFEXIT  (printk(KERN_DEBUG __FILE__ ": Exiting  : " __FUNCTION__ "\n"))
#else
#define DBFENTER (NULL)
#define DBFEXIT  (NULL)
#endif

/*-------------------------------------------------------------------
 Virtual Root Hub
 -------------------------------------------------------------------*/

static __u8 root_hub_dev_des[] =
{
	0x12,  /*  __u8  bLength; */
	0x01,  /*  __u8  bDescriptorType; Device */
	0x00,  /*  __u16 bcdUSB; v1.0 */
	0x01,
	0x09,  /*  __u8  bDeviceClass; HUB_CLASSCODE */
	0x00,  /*  __u8  bDeviceSubClass; */
	0x00,  /*  __u8  bDeviceProtocol; */
	0x08,  /*  __u8  bMaxPacketSize0; 8 Bytes */
	0x00,  /*  __u16 idVendor; */
	0x00,
	0x00,  /*  __u16 idProduct; */
	0x00,
	0x00,  /*  __u16 bcdDevice; */
	0x00,
	0x00,  /*  __u8  iManufacturer; */
	0x02,  /*  __u8  iProduct; */
	0x01,  /*  __u8  iSerialNumber; */
	0x01   /*  __u8  bNumConfigurations; */
};

/* Configuration descriptor */
static __u8 root_hub_config_des[] =
{
	0x09,  /*  __u8  bLength; */
	0x02,  /*  __u8  bDescriptorType; Configuration */
	0x19,  /*  __u16 wTotalLength; */
	0x00,
	0x01,  /*  __u8  bNumInterfaces; */
	0x01,  /*  __u8  bConfigurationValue; */
	0x00,  /*  __u8  iConfiguration; */
	0x40,  /*  __u8  bmAttributes; Bit 7: Bus-powered */
	0x00,  /*  __u8  MaxPower; */

     /* interface */
	0x09,  /*  __u8  if_bLength; */
	0x04,  /*  __u8  if_bDescriptorType; Interface */
	0x00,  /*  __u8  if_bInterfaceNumber; */
	0x00,  /*  __u8  if_bAlternateSetting; */
	0x01,  /*  __u8  if_bNumEndpoints; */
	0x09,  /*  __u8  if_bInterfaceClass; HUB_CLASSCODE */
	0x00,  /*  __u8  if_bInterfaceSubClass; */
	0x00,  /*  __u8  if_bInterfaceProtocol; */
	0x00,  /*  __u8  if_iInterface; */

     /* endpoint */
	0x07,  /*  __u8  ep_bLength; */
	0x05,  /*  __u8  ep_bDescriptorType; Endpoint */
	0x81,  /*  __u8  ep_bEndpointAddress; IN Endpoint 1 */
	0x03,  /*  __u8  ep_bmAttributes; Interrupt */
	0x08,  /*  __u16 ep_wMaxPacketSize; 8 Bytes */
	0x00,
	0xff   /*  __u8  ep_bInterval; 255 ms */
};

static __u8 root_hub_hub_des[] =
{
	0x09,  /*  __u8  bLength; */
	0x29,  /*  __u8  bDescriptorType; Hub-descriptor */
	0x02,  /*  __u8  bNbrPorts; */
	0x00,  /* __u16  wHubCharacteristics; */
	0x00,
	0x01,  /*  __u8  bPwrOn2pwrGood; 2ms */
	0x00,  /*  __u8  bHubContrCurrent; 0 mA */
	0x00,  /*  __u8  DeviceRemovable; *** 7 Ports max *** */
	0xff   /*  __u8  PortPwrCtrlMask; *** 7 ports max *** */
};


#define OK(x) len = (x); dbg_rh("OK(%d): line: %d", x, __LINE__); break
#define CHECK_ALIGN(x) if (((__u32)(x)) & 0x00000003) \
{panic("Alignment check (DWORD) failed at %s:%s:%d\n", __FILE__, __FUNCTION__, __LINE__);}

static submit_urb_count = 0;

//#define ETRAX_USB_INTR_IRQ
//#define ETRAX_USB_INTR_ERROR_FATAL

#define RX_BUF_SIZE        32768
#define RX_DESC_BUF_SIZE   64
#define NBR_OF_RX_DESC     (RX_BUF_SIZE / RX_DESC_BUF_SIZE)

#define NBR_OF_EP_DESC     32

#define MAX_INTR_INTERVAL 128

static __u32 ep_usage_bitmask;
static __u32 ep_really_active;

static unsigned char RxBuf[RX_BUF_SIZE];
static USB_IN_Desc_t RxDescList[NBR_OF_RX_DESC] __attribute__ ((aligned (4)));

static volatile USB_IN_Desc_t *myNextRxDesc;
static volatile USB_IN_Desc_t *myLastRxDesc;
static volatile USB_IN_Desc_t *myPrevRxDesc;

static USB_EP_Desc_t TxCtrlEPList[NBR_OF_EP_DESC] __attribute__ ((aligned (4)));
static USB_EP_Desc_t TxBulkEPList[NBR_OF_EP_DESC] __attribute__ ((aligned (4)));

static USB_EP_Desc_t TxIntrEPList[MAX_INTR_INTERVAL] __attribute__ ((aligned (4)));
static USB_SB_Desc_t TxIntrSB_zout __attribute__ ((aligned (4)));

static struct urb *URB_List[NBR_OF_EP_DESC];
static kmem_cache_t *usb_desc_cache;
static struct usb_bus *etrax_usb_bus;

static void dump_urb (struct urb *urb);
static void init_rx_buffers(void);
static int etrax_rh_unlink_urb (struct urb *urb);
static void etrax_rh_send_irq(struct urb *urb);
static void etrax_rh_init_int_timer(struct urb *urb);
static void etrax_rh_int_timer_do(unsigned long ptr);

static void etrax_usb_setup_epid(char epid, char devnum, char endpoint,
				 char packsize, char slow);
static int etrax_usb_lookup_epid(unsigned char devnum, char endpoint, char slow, int maxp);
static int etrax_usb_allocate_epid(void);
static void etrax_usb_free_epid(char epid);
static void cleanup_sb(USB_SB_Desc_t *sb);

static int etrax_usb_do_ctrl_hw_add(struct urb *urb, char epid, char maxlen, int mem_flags);
static int etrax_usb_do_bulk_hw_add(struct urb *urb, char epid, char maxlen, int mem_flags);

static int etrax_usb_submit_ctrl_urb(struct urb *urb, int mem_flags);

static int etrax_usb_submit_urb(struct urb *urb, int mem_flags);
static int etrax_usb_unlink_urb(struct urb *urb);
static int etrax_usb_get_frame_number(struct usb_device *usb_dev);
static int etrax_usb_allocate_dev(struct usb_device *usb_dev);
static int etrax_usb_deallocate_dev(struct usb_device *usb_dev);

static void etrax_usb_tx_interrupt(int irq, void *vhc, struct pt_regs *regs);
static void etrax_usb_rx_interrupt(int irq, void *vhc, struct pt_regs *regs);
static void etrax_usb_hc_intr_top_half(int irq, void *vhc, struct pt_regs *regs);

static int etrax_rh_submit_urb (struct urb *urb);

static int etrax_usb_hc_init(void);
static void etrax_usb_hc_cleanup(void);

static struct usb_operations etrax_usb_device_operations =
{
	etrax_usb_allocate_dev,
	etrax_usb_deallocate_dev,
	etrax_usb_get_frame_number,
	etrax_usb_submit_urb,
	etrax_usb_unlink_urb
};

#ifdef USB_DEBUG_DESC
static void dump_urb(struct urb *urb)
{
	printk("\nurb                   :0x%08X\n", urb);
	printk("next                  :0x%08X\n", urb->next);
	printk("dev                   :0x%08X\n", urb->dev);
	printk("pipe                  :0x%08X\n", urb->pipe);
	printk("status                :%d\n", urb->status);
	printk("transfer_flags        :0x%08X\n", urb->transfer_flags);
	printk("transfer_buffer       :0x%08X\n", urb->transfer_buffer);
	printk("transfer_buffer_length:%d\n", urb->transfer_buffer_length);
	printk("actual_length         :%d\n", urb->actual_length);
	printk("setup_packet          :0x%08X\n", urb->setup_packet);
	printk("start_frame           :%d\n", urb->start_frame);
	printk("number_of_packets     :%d\n", urb->number_of_packets);
	printk("interval              :%d\n", urb->interval);
	printk("error_count           :%d\n", urb->error_count);
	printk("context               :0x%08X\n", urb->context);
	printk("complete              :0x%08X\n\n", urb->complete);
}

static void dump_in_desc(USB_IN_Desc_t *in)
{
	printk("\nUSB_IN_Desc at 0x%08X\n", in);
	printk("  sw_len  : 0x%04X (%d)\n", in->sw_len, in->sw_len);
	printk("  command : 0x%04X\n", in->command);
	printk("  next    : 0x%08X\n", in->next);
	printk("  buf     : 0x%08X\n", in->buf);
	printk("  hw_len  : 0x%04X (%d)\n", in->hw_len, in->hw_len);
	printk("  status  : 0x%04X\n\n", in->status);
}

static void dump_sb_desc(USB_SB_Desc_t *sb)
{
	printk("\nUSB_SB_Desc at 0x%08X\n", sb);
	printk("  sw_len  : 0x%04X (%d)\n", sb->sw_len, sb->sw_len);
	printk("  command : 0x%04X\n", sb->command);
	printk("  next    : 0x%08X\n", sb->next);
	printk("  buf     : 0x%08X\n\n", sb->buf);
}


static void dump_ep_desc(USB_EP_Desc_t *ep)
{
	printk("\nUSB_EP_Desc at 0x%08X\n", ep);
	printk("  hw_len  : 0x%04X (%d)\n", ep->hw_len, ep->hw_len);
	printk("  command : 0x%08X\n", ep->command);
	printk("  sub     : 0x%08X\n", ep->sub);
	printk("  nep     : 0x%08X\n\n", ep->nep);
}


#else
#define dump_urb(...)     (NULL)
#define dump_ep_desc(...) (NULL)
#define dump_sb_desc(...) (NULL)
#define dump_in_desc(...) (NULL)
#endif

static void init_rx_buffers(void)
{
	int i;
	
	DBFENTER;
	
	for (i = 0; i < (NBR_OF_RX_DESC - 1); i++) {
		RxDescList[i].sw_len = RX_DESC_BUF_SIZE;
		RxDescList[i].command = 0;
		RxDescList[i].next = virt_to_phys(&RxDescList[i + 1]);
		RxDescList[i].buf = virt_to_phys(RxBuf + (i * RX_DESC_BUF_SIZE));
		RxDescList[i].hw_len = 0;
		RxDescList[i].status = 0;
	}
	
	RxDescList[i].sw_len = RX_DESC_BUF_SIZE;
	RxDescList[i].command = IO_STATE(USB_IN_command, eol, yes);
	RxDescList[i].next = virt_to_phys(&RxDescList[0]);
	RxDescList[i].buf = virt_to_phys(RxBuf + (i * RX_DESC_BUF_SIZE));
	RxDescList[i].hw_len = 0;
	RxDescList[i].status = 0;

	myNextRxDesc = &RxDescList[0];
	myLastRxDesc = &RxDescList[NBR_OF_RX_DESC - 1];
	myPrevRxDesc = &RxDescList[NBR_OF_RX_DESC - 1];

	*R_DMA_CH9_FIRST = virt_to_phys(myNextRxDesc);
	*R_DMA_CH9_CMD = IO_STATE(R_DMA_CH9_CMD, cmd, start);
	
	DBFEXIT;
}

static void init_tx_ctrl_ep(void)
{
	int i;
	
	DBFENTER;
	
	for (i = 0; i < (NBR_OF_EP_DESC - 1); i++) {
		TxCtrlEPList[i].hw_len = 0;
		TxCtrlEPList[i].command = IO_FIELD(USB_EP_command, epid, i);
		TxCtrlEPList[i].sub = 0;
		TxCtrlEPList[i].nep = virt_to_phys(&TxCtrlEPList[i + 1]);
	}
	
	TxCtrlEPList[i].hw_len = 0;
	TxCtrlEPList[i].command = IO_STATE(USB_EP_command, eol, yes) |
		IO_FIELD(USB_EP_command, epid, i);

	TxCtrlEPList[i].sub = 0;
	TxCtrlEPList[i].nep = virt_to_phys(&TxCtrlEPList[0]);
	
	*R_DMA_CH8_SUB1_EP = virt_to_phys(&TxCtrlEPList[0]);
	*R_DMA_CH8_SUB1_CMD = IO_STATE(R_DMA_CH8_SUB1_CMD, cmd, start);
	
	DBFEXIT;
}

static void init_tx_bulk_ep(void)
{
	int i;
	
	DBFENTER;
	
	for (i = 0; i < (NBR_OF_EP_DESC - 1); i++) {
		TxBulkEPList[i].hw_len = 0;
		TxBulkEPList[i].command = IO_FIELD(USB_EP_command, epid, i);
		TxBulkEPList[i].sub = 0;
		TxBulkEPList[i].nep = virt_to_phys(&TxBulkEPList[i + 1]);
	}
	
	TxBulkEPList[i].hw_len = 0;
	TxBulkEPList[i].command = IO_STATE(USB_EP_command, eol, yes) |
		IO_FIELD(USB_EP_command, epid, i);

	TxBulkEPList[i].sub = 0;
	TxBulkEPList[i].nep = virt_to_phys(&TxBulkEPList[0]);
	
	*R_DMA_CH8_SUB0_EP = virt_to_phys(&TxBulkEPList[0]);
	*R_DMA_CH8_SUB0_CMD = IO_STATE(R_DMA_CH8_SUB0_CMD, cmd, start);
	
	DBFEXIT;
}

static void init_tx_intr_ep(void)
{
	int i;

	DBFENTER;

	TxIntrSB_zout.sw_len = 0;
	TxIntrSB_zout.next = 0;
	TxIntrSB_zout.buf = 0;
	TxIntrSB_zout.command = IO_FIELD(USB_SB_command, rem, 0) |
		IO_STATE(USB_SB_command, tt, zout) |
		IO_STATE(USB_SB_command, full, yes) |
		IO_STATE(USB_SB_command, eot, yes) |
		IO_STATE(USB_SB_command, eol, yes);

	for (i = 0; i < (MAX_INTR_INTERVAL - 1); i++) {
		TxIntrEPList[i].hw_len = 0;
		TxIntrEPList[i].command = IO_STATE(USB_EP_command, eof, yes) |
			IO_STATE(USB_EP_command, enable, yes) |
			IO_FIELD(USB_EP_command, epid, 0);
		TxIntrEPList[i].sub = virt_to_phys(&TxIntrSB_zout);
		TxIntrEPList[i].nep = virt_to_phys(&TxIntrEPList[i + 1]);
	}
	
	TxIntrEPList[i].hw_len = 0;
	TxIntrEPList[i].command =
		IO_STATE(USB_EP_command, eof, yes) |
		IO_STATE(USB_EP_command, enable, yes) |
		IO_FIELD(USB_EP_command, epid, 0);
	TxIntrEPList[i].sub = virt_to_phys(&TxIntrSB_zout);
	TxIntrEPList[i].nep = virt_to_phys(&TxIntrEPList[0]);

	*R_DMA_CH8_SUB2_EP = virt_to_phys(&TxIntrEPList[0]);
	*R_DMA_CH8_SUB2_CMD = IO_STATE(R_DMA_CH8_SUB2_CMD, cmd, start);
	
	DBFEXIT;
}


static int etrax_usb_unlink_intr_urb(struct urb *urb)
{
	struct usb_device *usb_dev = urb->dev;
	etrax_hc_t *hc = usb_dev->bus->hcpriv;

	USB_EP_Desc_t *tmp_ep;
	USB_EP_Desc_t *first_ep;
	
	USB_EP_Desc_t *ep_desc;
	USB_SB_Desc_t *sb_desc;
	
	char epid;
	char devnum;
	char endpoint;
	char slow;
	int maxlen;
	int i;
	
	etrax_urb_priv_t *urb_priv;
	unsigned long flags;
	
	DBFENTER;

	devnum = usb_pipedevice(urb->pipe);
	endpoint = usb_pipeendpoint(urb->pipe);
	slow = usb_pipeslow(urb->pipe);
	maxlen = usb_maxpacket(urb->dev, urb->pipe,
			       usb_pipeout(urb->pipe));

	epid = etrax_usb_lookup_epid(devnum, endpoint, slow, maxlen);
	if (epid == -1) {
		err("Trying to unlink urb that is not in traffic queue!!");
		return -1;  /* fix this */
	}

	*R_DMA_CH8_SUB2_CMD = IO_STATE(R_DMA_CH8_SUB2_CMD, cmd, stop);
	/* Somehow wait for the DMA to finish current activities */
	i = jiffies + 100;
	while (time_before(jiffies, i))
		;
	
	first_ep = &TxIntrEPList[0];
	tmp_ep = first_ep;
	
	do {
		if (IO_EXTRACT(USB_EP_command, epid, ((USB_EP_Desc_t *)phys_to_virt(tmp_ep->nep))->command)
		    == epid) {
			/* Unlink it !!! */
			dbg_intr("Found urb to unlink for epid %d", epid);
			
			ep_desc = phys_to_virt(tmp_ep->nep);
			tmp_ep->nep = ep_desc->nep;
			kmem_cache_free(usb_desc_cache, phys_to_virt(ep_desc->sub));
			kmem_cache_free(usb_desc_cache, ep_desc);
		}

		tmp_ep = phys_to_virt(tmp_ep->nep);
		
	} while (tmp_ep != first_ep);

	/* We should really try to move the EP register to an EP that is not removed
	   instead of restarting, but this will work too */
	*R_DMA_CH8_SUB2_EP = virt_to_phys(&TxIntrEPList[0]);
	*R_DMA_CH8_SUB2_CMD = IO_STATE(R_DMA_CH8_SUB2_CMD, cmd, start);

	clear_bit(epid, (void *)&ep_really_active);
	URB_List[epid] = NULL;
	etrax_usb_free_epid(epid);
	
	DBFEXIT;

	return 0;
}

void etrax_usb_do_intr_recover(int epid)
{
	USB_EP_Desc_t *first_ep, *tmp_ep;
	
	first_ep = (USB_EP_Desc_t *)phys_to_virt(*R_DMA_CH8_SUB2_EP);
	tmp_ep = first_ep;

	do {
		if (IO_EXTRACT(USB_EP_command, epid, tmp_ep->command) == epid &&
		    !(tmp_ep->command & IO_MASK(USB_EP_command, enable))) {
			tmp_ep->command |= IO_STATE(USB_EP_command, enable, yes);
		}
		
		tmp_ep = (USB_EP_Desc_t *)phys_to_virt(tmp_ep->nep);
		
	} while (tmp_ep != first_ep);
}

static int etrax_usb_submit_intr_urb(struct urb *urb, mem_flags)
{
	USB_EP_Desc_t *tmp_ep;
	USB_EP_Desc_t *first_ep;
	
	USB_SB_Desc_t *sb_desc;
	
	char epid;
	char devnum;
	char endpoint;
	char maxlen;
	char slow;
	int interval;
	int i;
	
	etrax_urb_priv_t *urb_priv;
	unsigned long flags;
	
	DBFENTER;

	devnum = usb_pipedevice(urb->pipe);
	endpoint = usb_pipeendpoint(urb->pipe);
	maxlen = usb_maxpacket(urb->dev, urb->pipe,
			       usb_pipeout(urb->pipe));

	slow = usb_pipeslow(urb->pipe);
	interval = urb->interval;

	dbg_intr("Intr traffic for dev %d, endpoint %d, maxlen %d, slow %d",
		 devnum, endpoint, maxlen, slow);
	
	epid = etrax_usb_lookup_epid(devnum, endpoint, slow, maxlen);
	if (epid == -1) {
		epid = etrax_usb_allocate_epid();
		if (epid == -1) {
			/* We're out of endpoints, return some error */
			err("We're out of endpoints");
			return -ENOMEM;
		}
		/* Now we have to fill in this ep */
		etrax_usb_setup_epid(epid, devnum, endpoint, maxlen, slow);
	}
	/* Ok, now we got valid endpoint, lets insert some traffic */

	urb_priv = (etrax_urb_priv_t *)kmalloc(sizeof(etrax_urb_priv_t), mem_flags);
	urb_priv->first_sb = 0;
	urb_priv->rx_offset = 0;
	urb_priv->eot = 0;
	INIT_LIST_HEAD(&urb_priv->ep_in_list);
	urb->hcpriv = urb_priv;

	/* This is safe since there cannot be any other URB's for this epid */
	URB_List[epid] = urb;
#if 0
	first_ep = (USB_EP_Desc_t *)phys_to_virt(*R_DMA_CH8_SUB2_EP);
#else
	first_ep = &TxIntrEPList[0];
#endif

	/* Round of the interval to 2^n, it is obvious that this code favours
	   smaller numbers, but that is actually a good thing */
	for (i = 0; interval; i++) {
		interval = interval >> 1;
	}

	urb->interval = interval = 1 << (i - 1);

	dbg_intr("Interval rounded to %d", interval);

	tmp_ep = first_ep;
	i = 0;
	do {
		if (tmp_ep->command & IO_MASK(USB_EP_command, eof)) {
			if ((i % interval) == 0) {
				/* Insert the traffic ep after tmp_ep */
				USB_EP_Desc_t *traffic_ep;
				USB_SB_Desc_t *traffic_sb;

				traffic_ep = (USB_EP_Desc_t *)
					kmem_cache_alloc(usb_desc_cache, mem_flags);
				traffic_sb = (USB_SB_Desc_t *)
					kmem_cache_alloc(usb_desc_cache, mem_flags);

				traffic_ep->hw_len = 0;
				traffic_ep->command = IO_FIELD(USB_EP_command, epid, epid) |
					IO_STATE(USB_EP_command, enable, yes);
				traffic_ep->sub = virt_to_phys(traffic_sb);

				if (usb_pipein(urb->pipe)) {
					traffic_sb->sw_len = urb->transfer_buffer_length ?
						(urb->transfer_buffer_length - 1) / maxlen + 1 : 0;
					traffic_sb->next = 0;
					traffic_sb->buf = 0;
					traffic_sb->command = IO_FIELD(USB_SB_command, rem,
								       urb->transfer_buffer_length % maxlen) |
						IO_STATE(USB_SB_command, tt, in) |
						IO_STATE(USB_SB_command, eot, yes) |
						IO_STATE(USB_SB_command, eol, yes);
					
				} else if (usb_pipeout(urb->pipe)) {
					traffic_sb->sw_len = urb->transfer_buffer_length;
					traffic_sb->next = 0;
					traffic_sb->buf = virt_to_phys(urb->transfer_buffer);
					traffic_sb->command = IO_FIELD(USB_SB_command, rem, 0) |
						IO_STATE(USB_SB_command, tt, out) |
						IO_STATE(USB_SB_command, eot, yes) |
						IO_STATE(USB_SB_command, eol, yes) |
						IO_STATE(USB_SB_command, full, yes);
				}

				traffic_ep->nep = tmp_ep->nep;
				tmp_ep->nep = virt_to_phys(traffic_ep);
				dbg_intr("One ep sucessfully inserted");
			}
			i++;
		}
		tmp_ep = (USB_EP_Desc_t *)phys_to_virt(tmp_ep->nep);
	} while (tmp_ep != first_ep);

	set_bit(epid, (void *)&ep_really_active);
	
	*R_DMA_CH8_SUB2_CMD = IO_STATE(R_DMA_CH8_SUB2_CMD, cmd, start);

	DBFEXIT;
	
	return 0;
}


static int handle_intr_transfer_attn(char epid, int status)
{
	struct urb *old_urb;

	DBFENTER;

	old_urb = URB_List[epid];
	
	/* if (status == 0 && IN) find data and copy to urb */
	if (status == 0 && usb_pipein(old_urb->pipe)) {
		unsigned long flags;
		etrax_urb_priv_t *urb_priv;
		struct list_head *entry;
		struct in_chunk *in;

		urb_priv = (etrax_urb_priv_t *)old_urb->hcpriv;
		
		save_flags(flags);
		cli();

		list_for_each(entry, &urb_priv->ep_in_list) {
			in = list_entry(entry, struct in_chunk, list);
			memcpy(old_urb->transfer_buffer, in->data, in->length);
			old_urb->actual_length = in->length;
			old_urb->status = status;
			
			if (old_urb->complete) {
				old_urb->complete(old_urb);
			}
			
			list_del(entry);
			kfree(in->data);
			kfree(in);
		}		
		
		restore_flags(flags);

	} else if (status != 0) {
		warn("Some sort of error for INTR EP !!!!");
#ifdef ETRAX_USB_INTR_ERROR_FATAL
		/* This means that an INTR error is fatal for that endpoint */
		etrax_usb_unlink_intr_urb(old_urb);
		old_urb->status = status;
		if (old_urb->complete) {
			old_urb->complete(old_urb);
		}
#else
		/* In this case we reenable the disabled endpoint(s) */
		etrax_usb_do_intr_recover(epid);
#endif	
	}
	
	DBFEXIT;
}

static int etrax_rh_unlink_urb (struct urb *urb)
{
	etrax_hc_t *hc;
	
	DBFENTER;
	
	hc = urb->dev->bus->hcpriv;
	
	if (hc->rh.urb == urb) {
		hc->rh.send = 0;
		del_timer(&hc->rh.rh_int_timer);
	}
	
	DBFEXIT;
	return 0;
}

static void etrax_rh_send_irq(struct urb *urb)
{
	__u16 data = 0;
	etrax_hc_t *hc = urb->dev->bus->hcpriv;
//	static prev_wPortStatus_1 = 0;
//	static prev_wPortStatus_2 = 0;
	
/*	DBFENTER; */
	
	
/*
  dbg_rh("R_USB_FM_NUMBER   : 0x%08X", *R_USB_FM_NUMBER);
  dbg_rh("R_USB_FM_REMAINING: 0x%08X", *R_USB_FM_REMAINING);
*/
	
	data |= (hc->rh.wPortChange_1) ? (1 << 1) : 0;
	data |= (hc->rh.wPortChange_2) ? (1 << 2) : 0;

	*((__u16 *)urb->transfer_buffer) = cpu_to_le16(data);
	urb->actual_length = 1;
	urb->status = 0;

	
	if (data && hc->rh.send && urb->complete) {
		dbg_rh("wPortChange_1: 0x%04X", hc->rh.wPortChange_1); 
		dbg_rh("wPortChange_2: 0x%04X", hc->rh.wPortChange_2);

		urb->complete(urb);
	}
  
/*	DBFEXIT; */
}

static void etrax_rh_init_int_timer(struct urb *urb)
{
	etrax_hc_t *hc;
	
/*	DBFENTER; */
	
	hc = urb->dev->bus->hcpriv;
	hc->rh.interval = urb->interval;
	init_timer(&hc->rh.rh_int_timer);
	hc->rh.rh_int_timer.function = etrax_rh_int_timer_do;
	hc->rh.rh_int_timer.data = (unsigned long)urb;
	hc->rh.rh_int_timer.expires = jiffies + ((HZ * hc->rh.interval) / 1000);
	add_timer(&hc->rh.rh_int_timer);
	
/*	DBFEXIT; */
}

static void etrax_rh_int_timer_do(unsigned long ptr)
{
	struct urb *urb;
	etrax_hc_t *hc;
	
/*	DBFENTER; */
	
	urb = (struct urb *)ptr;
	hc = urb->dev->bus->hcpriv;
	
	if (hc->rh.send) {
		etrax_rh_send_irq(urb);
	}
	
	etrax_rh_init_int_timer(urb);
	
/*	DBFEXIT; */
}

static void etrax_usb_setup_epid(char epid, char devnum, char endpoint, char packsize, char slow)
{
	unsigned long flags;
	
	DBFENTER;

	save_flags(flags);
	cli();
	
	if (test_bit(epid, (void *)&ep_usage_bitmask)) {
		restore_flags(flags);

		warn("Trying to setup used epid %d", epid);
		DBFEXIT;
		return;
	}
	
	set_bit(epid, (void *)&ep_usage_bitmask);
	dbg_ep("Setting up ep_id %d with devnum %d, endpoint %d and max_len %d",
	       epid, devnum, endpoint, packsize);
	
	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid);
	nop();
	*R_USB_EPT_DATA = IO_STATE(R_USB_EPT_DATA, valid, yes) |
		IO_FIELD(R_USB_EPT_DATA, ep, endpoint) |
		IO_FIELD(R_USB_EPT_DATA, dev, devnum) |
		IO_FIELD(R_USB_EPT_DATA, max_len, packsize) |
		IO_FIELD(R_USB_EPT_DATA, low_speed, slow);

	restore_flags(flags);

	DBFEXIT;
}

static void etrax_usb_free_epid(char epid)
{
	unsigned long flags;
	
	DBFENTER;

	if (!test_bit(epid, (void *)&ep_usage_bitmask)) {
		warn("Trying to free unused epid %d", epid);
		DBFEXIT;
		return;
	}

	save_flags(flags);
	cli();

	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid);
	nop();
	while (*R_USB_EPT_DATA & IO_MASK(R_USB_EPT_DATA, hold))
		printk("+");
	*R_USB_EPT_DATA = 0;
	clear_bit(epid, (void *)&ep_usage_bitmask);

	restore_flags(flags);

	dbg_ep("epid: %d freed", epid);
	
	DBFEXIT;
}


static int etrax_usb_lookup_epid(unsigned char devnum, char endpoint, char slow, int maxp)
{
	int i;
	unsigned long flags;
	__u32 data;
	
	DBFENTER;

	save_flags(flags);
	
	/* Skip first ep_id since it is reserved when intr. or iso traffic is used */
	for (i = 0; i < NBR_OF_EP_DESC; i++) {
		if (test_bit(i, (void *)&ep_usage_bitmask)) {
			*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, i);
			nop();
			data = *R_USB_EPT_DATA;
			if ((IO_MASK(R_USB_EPT_DATA, valid) & data) &&
			    (IO_EXTRACT(R_USB_EPT_DATA, dev, data) == devnum) &&
			    (IO_EXTRACT(R_USB_EPT_DATA, ep, data) == endpoint) &&
			    (IO_EXTRACT(R_USB_EPT_DATA, low_speed, data) == slow) &&
			    (IO_EXTRACT(R_USB_EPT_DATA, max_len, data) == maxp)) {
				restore_flags(flags);
	
				dbg_ep("Found ep_id %d for devnum %d, endpoint %d",
				       i, devnum, endpoint);
				DBFEXIT;
				return i;
			}
		}
	}

	restore_flags(flags);
	
	dbg_ep("Found no ep_id for devnum %d, endpoint %d",
	       devnum, endpoint);
	DBFEXIT;
	return -1;
}

static int etrax_usb_allocate_epid(void)
{
	int i;
	
	DBFENTER;

	for (i = 0; i < NBR_OF_EP_DESC; i++) {
		if (!test_bit(i, (void *)&ep_usage_bitmask)) {
			dbg_ep("Found free ep_id at %d", i);
			DBFEXIT;
			return i;
		}
	}

	dbg_ep("Found no free ep_id's");
	DBFEXIT;
	return -1;
}

static int etrax_usb_submit_bulk_urb(struct urb *urb, int mem_flags)
{
	char epid;
	char devnum;
	char endpoint;
	char maxlen;
	char slow;

	struct urb *tmp_urb;
	
	etrax_urb_priv_t *urb_priv;
	unsigned long flags;
	
	DBFENTER;

	devnum = usb_pipedevice(urb->pipe);
	endpoint = usb_pipeendpoint(urb->pipe);
	maxlen = usb_maxpacket(urb->dev, urb->pipe,
			       usb_pipeout(urb->pipe));
	slow = usb_pipeslow(urb->pipe);
	
	epid = etrax_usb_lookup_epid(devnum, endpoint, slow, maxlen);
	if (epid == -1) {
		epid = etrax_usb_allocate_epid();
		if (epid == -1) {
			/* We're out of endpoints, return some error */
			err("We're out of endpoints");
			return -ENOMEM;
		}
		/* Now we have to fill in this ep */
		etrax_usb_setup_epid(epid, devnum, endpoint, maxlen, slow);
	}
	/* Ok, now we got valid endpoint, lets insert some traffic */

	urb->status = -EINPROGRESS;

	save_flags(flags);
	cli();
	
	if (URB_List[epid]) {
		/* Find end of list and add */
		for (tmp_urb = URB_List[epid]; tmp_urb->next; tmp_urb = tmp_urb->next)
			dump_urb(tmp_urb);

		tmp_urb->next = urb;
		restore_flags(flags);
	} else {
		/* If this is the first URB, add the URB and do HW add */
		URB_List[epid] = urb;
		restore_flags(flags);
		etrax_usb_do_bulk_hw_add(urb, epid, maxlen, mem_flags);
	}

	DBFEXIT;

	return 0;
}

static int etrax_usb_do_bulk_hw_add(struct urb *urb, char epid, char maxlen, int mem_flags)
{
	USB_SB_Desc_t *sb_desc_1;

	etrax_urb_priv_t *urb_priv;

	unsigned long flags;
	__u32 r_usb_ept_data;

	DBFENTER;

	urb_priv = kmalloc(sizeof(etrax_urb_priv_t), mem_flags);
	sb_desc_1 = (USB_SB_Desc_t*)kmem_cache_alloc(usb_desc_cache, mem_flags);

	if (usb_pipeout(urb->pipe)) {

		dbg_bulk("Bulk transfer for epid %d is OUT", epid);
		dbg_bulk("transfer_buffer_length == %d", urb->transfer_buffer_length);
		dbg_bulk("actual_length == %d", urb->actual_length);
	
		if (urb->transfer_buffer_length > 0xffff) {
			panic(__FILE__ __FUNCTION__ ":urb->transfer_buffer_length > 0xffff\n");
		}

		sb_desc_1->sw_len = urb->transfer_buffer_length;  /* was actual_length */
		sb_desc_1->command = IO_FIELD(USB_SB_command, rem, 0) |
			IO_STATE(USB_SB_command, tt, out) |

#if 0
			IO_STATE(USB_SB_command, full, no) |
#else
			IO_STATE(USB_SB_command, full, yes) |
#endif

			IO_STATE(USB_SB_command, eot, yes) |
			IO_STATE(USB_SB_command, eol, yes);
		
		dbg_bulk("transfer_buffer is at 0x%08X", urb->transfer_buffer);
		
		sb_desc_1->buf = virt_to_phys(urb->transfer_buffer);
		sb_desc_1->next = 0;
		
	} else if (usb_pipein(urb->pipe)) {

		dbg_bulk("Transfer for epid %d is IN", epid);
		dbg_bulk("transfer_buffer_length = %d", urb->transfer_buffer_length);
		dbg_bulk("rem is calculated to %d", urb->transfer_buffer_length % maxlen);
		
		sb_desc_1->sw_len = urb->transfer_buffer_length ?
			(urb->transfer_buffer_length - 1) / maxlen + 1 : 0;
		dbg_bulk("sw_len got %d", sb_desc_1->sw_len);
		dbg_bulk("transfer_buffer is at 0x%08X", urb->transfer_buffer);
		
		sb_desc_1->command =
			IO_FIELD(USB_SB_command, rem,
				 urb->transfer_buffer_length % maxlen) |
			IO_STATE(USB_SB_command, tt, in) |
			IO_STATE(USB_SB_command, eot, yes) |
			IO_STATE(USB_SB_command, eol, yes);
		
		sb_desc_1->buf = 0;
		sb_desc_1->next = 0;

		urb_priv->rx_offset = 0;
		urb_priv->eot = 0;
	}
	
	urb_priv->first_sb = sb_desc_1;
	
	urb->hcpriv = (void *)urb_priv;
	
	/* Reset toggle bits and reset error count, remeber to di and ei */
	/* Warning: it is possible that this locking doesn't work with bottom-halves */

	save_flags(flags);
	cli();

	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid); nop();
	if (*R_USB_EPT_DATA & IO_MASK(R_USB_EPT_DATA, hold)) {
		panic("Hold was set in %s\n", __FUNCTION__);
	}

	*R_USB_EPT_DATA &=
		~(IO_MASK(R_USB_EPT_DATA, error_count_in) |
		  IO_MASK(R_USB_EPT_DATA, error_count_out));
	
	if (usb_pipeout(urb->pipe)) {
		char toggle =
		usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe), usb_pipeout(urb->pipe));
		*R_USB_EPT_DATA &= ~IO_MASK(R_USB_EPT_DATA, t_out);
		*R_USB_EPT_DATA |= IO_FIELD(R_USB_EPT_DATA, t_out, toggle);
	} else {
		char toggle =
		usb_gettoggle(urb->dev, usb_pipeendpoint(urb->pipe), usb_pipeout(urb->pipe));
		*R_USB_EPT_DATA &= ~IO_MASK(R_USB_EPT_DATA, t_in);
		*R_USB_EPT_DATA |= IO_FIELD(R_USB_EPT_DATA, t_in, toggle);
	}
		
	/* Enable the EP descr. */

	set_bit(epid, (void *)&ep_really_active);
	
	TxBulkEPList[epid].sub = virt_to_phys(sb_desc_1);
	TxBulkEPList[epid].hw_len = 0;
	TxBulkEPList[epid].command |= IO_STATE(USB_EP_command, enable, yes);

	restore_flags(flags);

	if (!(*R_DMA_CH8_SUB0_CMD & IO_MASK(R_DMA_CH8_SUB0_CMD, cmd))) {
		*R_DMA_CH8_SUB0_CMD = IO_STATE(R_DMA_CH8_SUB0_CMD, cmd, start);
		
	}

	DBFEXIT;
}

static int handle_bulk_transfer_attn(char epid, int status)
{
	struct urb *old_urb;
	etrax_urb_priv_t *hc_priv;
	unsigned long flags;

	DBFENTER;

	clear_bit(epid, (void *)&ep_really_active);
	
	old_urb = URB_List[epid];
	URB_List[epid] = old_urb->next;

	/* if (status == 0 && IN) find data and copy to urb */
	if (status == 0 && usb_pipein(old_urb->pipe)) {
		etrax_urb_priv_t *urb_priv;
		
		urb_priv = (etrax_urb_priv_t *)old_urb->hcpriv;
		save_flags(flags);
		cli();
		if (urb_priv->eot == 1) {
			old_urb->actual_length = urb_priv->rx_offset;
		} else {
			if (urb_priv->rx_offset == 0) {
				status = 0;
			} else {
				status = -EPROTO;
			}
			
			old_urb->actual_length = 0;
			err("(BULK) No eot set in IN data!!! rx_offset is: %d", urb_priv->rx_offset);
		}
		
		restore_flags(flags);
	}

	save_flags(flags);
	cli();
		
	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid); nop();
	if (usb_pipeout(old_urb->pipe)) {
		char toggle =
		IO_EXTRACT(R_USB_EPT_DATA, t_out, *R_USB_EPT_DATA);
		usb_settoggle(old_urb->dev, usb_pipeendpoint(old_urb->pipe),
			      usb_pipeout(old_urb->pipe), toggle);
	} else {
		char toggle =
		IO_EXTRACT(R_USB_EPT_DATA, t_in, *R_USB_EPT_DATA);
		usb_settoggle(old_urb->dev, usb_pipeendpoint(old_urb->pipe),
			      usb_pipeout(old_urb->pipe), toggle);
	}
	restore_flags(flags);
	
	/* If there are any more URB's in the list we'd better start sending */
	if (URB_List[epid]) {
		etrax_usb_do_bulk_hw_add(URB_List[epid], epid,
					 usb_maxpacket(URB_List[epid]->dev, URB_List[epid]->pipe,
						       usb_pipeout(URB_List[epid]->pipe)),
					 GFP_KERNEL);
	}
#if 1
	else {
		/* This means that this EP is now free, deconfigure it */
		etrax_usb_free_epid(epid);
	}
#endif
	
	/* Remember to free the SB's */
	hc_priv = (etrax_urb_priv_t *)old_urb->hcpriv;
	cleanup_sb(hc_priv->first_sb);
	kfree(hc_priv);

	old_urb->status = status;
	if (old_urb->complete) {
		old_urb->complete(old_urb);
	}

	DBFEXIT;
}

/* ---------------------------------------------------------------------------- */

static int etrax_usb_submit_ctrl_urb(struct urb *urb, int mem_flags)
{
	char epid;
	char devnum;
	char endpoint;
	char maxlen;
	char slow;

	struct urb *tmp_urb;
	
	etrax_urb_priv_t *urb_priv;
	unsigned long flags;
	
	DBFENTER;

	devnum = usb_pipedevice(urb->pipe);
	endpoint = usb_pipeendpoint(urb->pipe);
	maxlen = usb_maxpacket(urb->dev, urb->pipe,
			       usb_pipeout(urb->pipe));
	slow = usb_pipeslow(urb->pipe);
	
	epid = etrax_usb_lookup_epid(devnum, endpoint, slow, maxlen);
	if (epid == -1) {
		epid = etrax_usb_allocate_epid();
		if (epid == -1) {
			/* We're out of endpoints, return some error */
			err("We're out of endpoints");
			return -ENOMEM;
		}
		/* Now we have to fill in this ep */
		etrax_usb_setup_epid(epid, devnum, endpoint, maxlen, slow);
	}
	/* Ok, now we got valid endpoint, lets insert some traffic */

	urb->status = -EINPROGRESS;

	save_flags(flags);
	cli();
	
	if (URB_List[epid]) {
		/* Find end of list and add */
		for (tmp_urb = URB_List[epid]; tmp_urb->next; tmp_urb = tmp_urb->next)
			dump_urb(tmp_urb);

		tmp_urb->next = urb;
		restore_flags(flags);
	} else {
		/* If this is the first URB, add the URB and do HW add */
		URB_List[epid] = urb;
		restore_flags(flags);
		etrax_usb_do_ctrl_hw_add(urb, epid, maxlen, mem_flags);
	}

	DBFEXIT;

	return 0;
}

static int etrax_usb_do_ctrl_hw_add(struct urb *urb, char epid, char maxlen, int mem_flags)
{
	USB_SB_Desc_t *sb_desc_1;
	USB_SB_Desc_t *sb_desc_2;
	USB_SB_Desc_t *sb_desc_3;

	etrax_urb_priv_t *urb_priv;

	unsigned long flags;
	__u32 r_usb_ept_data;
	

	DBFENTER;

	urb_priv = kmalloc(sizeof(etrax_urb_priv_t), mem_flags);
	sb_desc_1 = (USB_SB_Desc_t*)kmem_cache_alloc(usb_desc_cache, mem_flags);
	sb_desc_2 = (USB_SB_Desc_t*)kmem_cache_alloc(usb_desc_cache, mem_flags);

	if (!(sb_desc_1 && sb_desc_2)) {
		panic("kmem_cache_alloc in ctrl_hw_add gave NULL pointers !!!\n");
	}
	
	sb_desc_1->sw_len = 8;
	sb_desc_1->command = IO_FIELD(USB_SB_command, rem, 0) |
		IO_STATE(USB_SB_command, tt, setup) |
		IO_STATE(USB_SB_command, full, yes) |
		IO_STATE(USB_SB_command, eot, yes);
	
	sb_desc_1->buf = virt_to_phys(urb->setup_packet);
	sb_desc_1->next = virt_to_phys(sb_desc_2);
	dump_sb_desc(sb_desc_1);

	if (usb_pipeout(urb->pipe)) {
		dbg_ctrl("Transfer for epid %d is OUT", epid);

		/* If this Control OUT transfer has an optional data stage we add an OUT token
		   before the mandatory IN (status) token, hence the reordered SB list */
		
		if (urb->transfer_buffer) {
			dbg_ctrl("This OUT transfer has an extra data stage");
			sb_desc_3 = (USB_SB_Desc_t*)kmem_cache_alloc(usb_desc_cache, mem_flags);

			sb_desc_1->next = virt_to_phys(sb_desc_3);
			
			sb_desc_3->sw_len = urb->transfer_buffer_length;
			sb_desc_3->command = IO_STATE(USB_SB_command, tt, out) |
				IO_STATE(USB_SB_command, full, yes) |
				IO_STATE(USB_SB_command, eot, yes);
			sb_desc_3->buf = virt_to_phys(urb->transfer_buffer);
			sb_desc_3->next = virt_to_phys(sb_desc_2);
		}
		
		sb_desc_2->sw_len = 1;
		sb_desc_2->command = IO_FIELD(USB_SB_command, rem, 0) |
			IO_STATE(USB_SB_command, tt, in) |
			IO_STATE(USB_SB_command, eot, yes) |
			IO_STATE(USB_SB_command, eol, yes);

		sb_desc_2->buf = 0;
		sb_desc_2->next = 0;
		dump_sb_desc(sb_desc_2);
		
	} else if (usb_pipein(urb->pipe)) {

		dbg_ctrl("Transfer for epid %d is IN", epid);
		dbg_ctrl("transfer_buffer_length = %d", urb->transfer_buffer_length);
		dbg_ctrl("rem is calculated to %d", urb->transfer_buffer_length % maxlen);

		sb_desc_3 = (USB_SB_Desc_t*)kmem_cache_alloc(usb_desc_cache, mem_flags);
		
		sb_desc_2->sw_len = urb->transfer_buffer_length ?
			(urb->transfer_buffer_length - 1) / maxlen + 1 : 0;
		dbg_ctrl("sw_len got %d", sb_desc_2->sw_len);
		
		sb_desc_2->command =
			IO_FIELD(USB_SB_command, rem,
				 urb->transfer_buffer_length % maxlen) |
			IO_STATE(USB_SB_command, tt, in) |
			IO_STATE(USB_SB_command, eot, yes);
		
		sb_desc_2->buf = 0;
		sb_desc_2->next = virt_to_phys(sb_desc_3);
		dump_sb_desc(sb_desc_2);

		sb_desc_3->sw_len = 1;
		sb_desc_3->command = IO_FIELD(USB_SB_command, rem, 0) |
			IO_STATE(USB_SB_command, tt, zout) |
			IO_STATE(USB_SB_command, full, yes) |
			IO_STATE(USB_SB_command, eot, yes) |
			IO_STATE(USB_SB_command, eol, yes);
				
		sb_desc_3->buf = 0;
		sb_desc_3->next = 0;
		dump_sb_desc(sb_desc_3);

		urb_priv->rx_offset = 0;
		urb_priv->eot = 0;
	}
	
	urb_priv->first_sb = sb_desc_1;
	
	urb->hcpriv = (void *)urb_priv;
	
	/* Reset toggle bits and reset error count, remeber to di and ei */
	/* Warning: it is possible that this locking doesn't work with bottom-halves */

	save_flags(flags);
	cli();

	*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid); nop();
	if (*R_USB_EPT_DATA & IO_MASK(R_USB_EPT_DATA, hold)) {
		panic("Hold was set in %s\n", __FUNCTION__);
	}
	

	*R_USB_EPT_DATA &=
		~(IO_MASK(R_USB_EPT_DATA, error_count_in) |
		  IO_MASK(R_USB_EPT_DATA, error_count_out) |
		  IO_MASK(R_USB_EPT_DATA, t_in) |
		  IO_MASK(R_USB_EPT_DATA, t_out));

	/* Enable the EP descr. */

	set_bit(epid, (void *)&ep_really_active);
	
	TxCtrlEPList[epid].sub = virt_to_phys(sb_desc_1);
	TxCtrlEPList[epid].hw_len = 0;
	TxCtrlEPList[epid].command |= IO_STATE(USB_EP_command, enable, yes);

	restore_flags(flags);

	dump_ep_desc(&TxCtrlEPList[epid]);

	if (!(*R_DMA_CH8_SUB1_CMD & IO_MASK(R_DMA_CH8_SUB1_CMD, cmd))) {
		*R_DMA_CH8_SUB1_CMD = IO_STATE(R_DMA_CH8_SUB1_CMD, cmd, start);
		
	}
	
	DBFEXIT;
}

static int etrax_usb_submit_urb(struct urb *urb, int mem_flags)
{
	etrax_hc_t *hc;
	int rval = -EINVAL;
	
	DBFENTER;

	dump_urb(urb);
	submit_urb_count++;
	
	hc = (etrax_hc_t*) urb->dev->bus->hcpriv;
	
	if (usb_pipedevice(urb->pipe) == hc->rh.devnum) {
		/* This request if for the Virtual Root Hub */
		rval = etrax_rh_submit_urb(urb);
		
	} else if (usb_pipetype(urb->pipe) == PIPE_CONTROL) {
		rval = etrax_usb_submit_ctrl_urb(urb, mem_flags);

	} else if (usb_pipetype(urb->pipe) == PIPE_BULK) {
		rval = etrax_usb_submit_bulk_urb(urb, mem_flags);

	} else if (usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {
		int bustime;

		if (urb->bandwidth == 0) {
			bustime = usb_check_bandwidth(urb->dev, urb);
			if (bustime < 0) {
				rval = bustime;
			} else {
				usb_claim_bandwidth(urb->dev, urb, bustime, 0);
				rval = etrax_usb_submit_intr_urb(urb, mem_flags);
			}
			
		}
	} else if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		warn("Isochronous traffic is not supported !!!");
		rval = -EINVAL;
	}

	DBFEXIT;

	return rval;
}

static int etrax_usb_unlink_urb(struct urb *urb)
{
	etrax_hc_t *hc = urb->dev->bus->hcpriv;
	int epid;
	int pos;
	int devnum, endpoint, slow, maxlen;
	etrax_urb_priv_t *hc_priv;
	unsigned long flags;
	
	DBFENTER;
	dump_urb(urb);
	devnum = usb_pipedevice(urb->pipe);
	endpoint = usb_pipeendpoint(urb->pipe);
	slow = usb_pipeslow(urb->pipe);
	maxlen = usb_maxpacket(urb->dev, urb->pipe,
			       usb_pipeout(urb->pipe));

	epid = etrax_usb_lookup_epid(devnum, endpoint, slow, maxlen);

	if (epid == -1)
		return 0;
	
	
	if (usb_pipedevice(urb->pipe) == hc->rh.devnum) {
		int ret;
		ret =  etrax_rh_unlink_urb(urb);
		DBFEXIT;
		return ret;
	} else if (usb_pipetype(URB_List[epid]->pipe) == PIPE_INTERRUPT) {
		int ret;
		ret = etrax_usb_unlink_intr_urb(urb);
		urb->status = -ENOENT;
		if (urb->complete) {
			urb->complete(urb);
		}
		DBFEXIT;
		return ret;
	}

	info("Unlink of BULK or CTRL");

	save_flags(flags);
	cli();
	
	for (epid = 0; epid < 32; epid++) {
		struct urb *u = URB_List[epid];
		pos = 0;

		for (; u; u = u->next) {
			pos++;
			if (u == urb) {
				info("Found urb at epid %d, pos %d", epid, pos);

				if (pos == 1) {
					if (usb_pipetype(u->pipe) == PIPE_CONTROL) {
						if (TxCtrlEPList[epid].command & IO_MASK(USB_EP_command, enable)) {
							/* The EP was enabled, disable it and wait */
							TxCtrlEPList[epid].command &= ~IO_MASK(USB_EP_command, enable);
							while (*R_DMA_CH8_SUB1_EP == virt_to_phys(&TxCtrlEPList[epid]));
						}
						
					} else if (usb_pipetype(u->pipe) == PIPE_BULK) {
						if (TxBulkEPList[epid].command & IO_MASK(USB_EP_command, enable)) {
							TxBulkEPList[epid].command &= ~IO_MASK(USB_EP_command, enable);
							while (*R_DMA_CH8_SUB0_EP == virt_to_phys(&TxBulkEPList[epid]));
						}
					}

					URB_List[epid] = u->next;
				
				} else {
					struct urb *up;
					for (up = URB_List[epid]; up->next != u; up = up->next);
					up->next = u->next;
				}
				u->status = -ENOENT;
				if (u->complete) {
					u->complete(u);
				}
				
				hc_priv = (etrax_urb_priv_t *)u->hcpriv;
				cleanup_sb(hc_priv->first_sb);
				kfree(hc_priv);
			}
		}
	}

	restore_flags(flags);
		
	DBFEXIT;
	return 0;
}

static int etrax_usb_get_frame_number(struct usb_device *usb_dev)
{
	DBFENTER;
	DBFEXIT;
	return (*R_USB_FM_NUMBER);
}

static int etrax_usb_allocate_dev(struct usb_device *usb_dev)
{  
	DBFENTER;
	DBFEXIT;
	return 0;
}

static int etrax_usb_deallocate_dev(struct usb_device *usb_dev)
{
	DBFENTER;
	DBFEXIT;
	return 0;
}

static void etrax_usb_tx_interrupt(int irq, void *vhc, struct pt_regs *regs)
{
	etrax_hc_t *hc = (etrax_hc_t *)vhc;
	int epid;
	char eol;
	struct urb *urb;
	USB_EP_Desc_t *tmp_ep;
	USB_SB_Desc_t *tmp_sb;
	
	DBFENTER;

	if (*R_IRQ_READ2 & IO_MASK(R_IRQ_READ2, dma8_sub0_descr)) {
		info("dma8_sub0_descr (BULK) intr.");
		*R_DMA_CH8_SUB0_CLR_INTR = IO_STATE(R_DMA_CH8_SUB0_CLR_INTR, clr_descr, do);
	}
	if (*R_IRQ_READ2 & IO_MASK(R_IRQ_READ2, dma8_sub1_descr)) {
		info("dma8_sub1_descr (CTRL) intr.");
		*R_DMA_CH8_SUB1_CLR_INTR = IO_STATE(R_DMA_CH8_SUB1_CLR_INTR, clr_descr, do);
	}
	if (*R_IRQ_READ2 & IO_MASK(R_IRQ_READ2, dma8_sub2_descr)) {
		info("dma8_sub2_descr (INT) intr.");
		*R_DMA_CH8_SUB2_CLR_INTR = IO_STATE(R_DMA_CH8_SUB2_CLR_INTR, clr_descr, do);
	}
	if (*R_IRQ_READ2 & IO_MASK(R_IRQ_READ2, dma8_sub3_descr)) {
		info("dma8_sub3_descr (ISO) intr.");
		*R_DMA_CH8_SUB3_CLR_INTR = IO_STATE(R_DMA_CH8_SUB3_CLR_INTR, clr_descr, do);
	}
	
	DBFEXIT;
}

static void etrax_usb_rx_interrupt(int irq, void *vhc, struct pt_regs *regs)
{
	int epid = 0;
	struct urb *urb;
	etrax_urb_priv_t *urb_priv;
		
	*R_DMA_CH9_CLR_INTR = IO_STATE(R_DMA_CH9_CLR_INTR, clr_eop, do);

	while (myNextRxDesc->status & IO_MASK(USB_IN_status, eop)) {
		if (myNextRxDesc->status & IO_MASK(USB_IN_status, nodata)) {

			goto skip_out;
		}

		if (myNextRxDesc->status & IO_MASK(USB_IN_status, error)) {
			
			goto skip_out;
		}
		
		epid = IO_EXTRACT(USB_IN_status, epid, myNextRxDesc->status);

		urb = URB_List[epid];

		if (urb && usb_pipein(urb->pipe)) {
			urb_priv = (etrax_urb_priv_t *)urb->hcpriv;

			if (usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {
				struct in_chunk *in;
				dbg_intr("Packet for epid %d in rx buffers", epid);
				in = kmalloc(sizeof(struct in_chunk), GFP_ATOMIC);
				in->length = myNextRxDesc->hw_len;
				in->data = kmalloc(in->length, GFP_ATOMIC);
				memcpy(in->data, phys_to_virt(myNextRxDesc->buf), in->length);
				list_add_tail(&in->list, &urb_priv->ep_in_list);
#ifndef ETRAX_USB_INTR_IRQ
				etrax_usb_hc_intr_top_half(irq, vhc, regs);
#endif
				
			} else {
				if ((urb_priv->rx_offset + myNextRxDesc->hw_len) >
				    urb->transfer_buffer_length) {
					err("Packet (epid: %d) in RX buffer was bigger "
					    "than the URB has room for !!!", epid);
					goto skip_out;
				}
				
				memcpy(urb->transfer_buffer + urb_priv->rx_offset,
				       phys_to_virt(myNextRxDesc->buf), myNextRxDesc->hw_len);
				
				urb_priv->rx_offset += myNextRxDesc->hw_len;
			}
			
			if (myNextRxDesc->status & IO_MASK(USB_IN_status, eot)) {
				urb_priv->eot = 1;
			}
			
		} else {
			err("This is almost fatal, inpacket for epid %d which does not exist "
			    " or is out !!!\nURB was at 0x%08X", epid, urb);
			
			goto skip_out;
		}

	skip_out:
		myPrevRxDesc = myNextRxDesc;
		myPrevRxDesc->command |= IO_MASK(USB_IN_command, eol);
		myLastRxDesc->command &= ~IO_MASK(USB_IN_command, eol);
		myLastRxDesc = myPrevRxDesc;

		myNextRxDesc->status = 0;
		myNextRxDesc = phys_to_virt(myNextRxDesc->next);
	}
}



static void cleanup_sb(USB_SB_Desc_t *sb)
{
	USB_SB_Desc_t *next_sb;
	
	DBFENTER;

	if (sb == NULL) {
		err("cleanup_sb was given a NULL pointer");
		return;
	}

	while (!(sb->command & IO_MASK(USB_SB_command, eol))) {
		next_sb = (USB_SB_Desc_t *)phys_to_virt(sb->next);
		kmem_cache_free(usb_desc_cache, sb);
		sb = next_sb;
	}

	kmem_cache_free(usb_desc_cache, sb);

	DBFEXIT;

}

static int handle_control_transfer_attn(char epid, int status)
{
	struct urb *old_urb;
	etrax_urb_priv_t *hc_priv;	

	DBFENTER;

	clear_bit(epid, (void *)&ep_really_active);
	
	old_urb = URB_List[epid];
	URB_List[epid] = old_urb->next;
	
	/* if (status == 0 && IN) find data and copy to urb */
	if (status == 0 && usb_pipein(old_urb->pipe)) {
		unsigned long flags;
		etrax_urb_priv_t *urb_priv;

		urb_priv = (etrax_urb_priv_t *)old_urb->hcpriv;
		save_flags(flags);
		cli();
		if (urb_priv->eot == 1) {
			old_urb->actual_length = urb_priv->rx_offset;
			dbg_ctrl("urb_priv->rx_offset: %d in handle_control_attn", urb_priv->rx_offset);
		} else {
			status = -EPROTO;
			old_urb->actual_length = 0;
			err("(CTRL) No eot set in IN data!!! rx_offset: %d", urb_priv->rx_offset);
		}

		restore_flags(flags);
	}
	
	/* If there are any more URB's in the list we'd better start sending */
	if (URB_List[epid]) {
		etrax_usb_do_ctrl_hw_add(URB_List[epid], epid,
					 usb_maxpacket(URB_List[epid]->dev, URB_List[epid]->pipe,
						       usb_pipeout(URB_List[epid]->pipe)),
					 GFP_KERNEL);
	}
#if 1
	else {
		/* This means that this EP is now free, deconfigure it */
		etrax_usb_free_epid(epid);
	}
#endif
	
	/* Remember to free the SB's */
	hc_priv = (etrax_urb_priv_t *)old_urb->hcpriv;
	cleanup_sb(hc_priv->first_sb);
	kfree(hc_priv);

	old_urb->status = status;
	if (old_urb->complete) {
		old_urb->complete(old_urb);
	}

	DBFEXIT;
}



static void etrax_usb_hc_intr_bottom_half(void *data)
{
	struct usb_reg_context *reg = (struct usb_reg_context *)data;
	struct urb *old_urb;
	
	int error_code;
	int epid;

	__u32 r_usb_ept_data;

	etrax_hc_t *hc = reg->hc; 
	__u16 r_usb_rh_port_status_1;
	__u16 r_usb_rh_port_status_2;
	
	DBFENTER;

	if (reg->r_usb_irq_mask_read & IO_MASK(R_USB_IRQ_MASK_READ, port_status)) {

		/*
		  The Etrax RH does not include a wPortChange register, so this has
		  to be handled in software. See section 11.16.2.6.2 in USB 1.1 spec
		  for details.
		*/
		
		r_usb_rh_port_status_1 = reg->r_usb_rh_port_status_1;
		r_usb_rh_port_status_2 = reg->r_usb_rh_port_status_2;
		
		dbg_rh("port_status pending");
		dbg_rh("r_usb_rh_port_status_1: 0x%04X", r_usb_rh_port_status_1);
		dbg_rh("r_usb_rh_port_status_2: 0x%04X", r_usb_rh_port_status_2);

		/* C_PORT_CONNECTION is set on any transition */
		hc->rh.wPortChange_1 |=
			((r_usb_rh_port_status_1 & (1 << RH_PORT_CONNECTION)) !=
			 (hc->rh.prev_wPortStatus_1 & (1 << RH_PORT_CONNECTION))) ?
			(1 << RH_PORT_CONNECTION) : 0;
		
		hc->rh.wPortChange_2 |=
			((r_usb_rh_port_status_2 & (1 << RH_PORT_CONNECTION)) !=
			 (hc->rh.prev_wPortStatus_2 & (1 << RH_PORT_CONNECTION))) ?
			(1 << RH_PORT_CONNECTION) : 0;

		/* C_PORT_ENABLE is _only_ set on a one to zero transition */
		hc->rh.wPortChange_1 |=
			((hc->rh.prev_wPortStatus_1 & (1 << RH_PORT_ENABLE))
			 && !(r_usb_rh_port_status_1 & (1 << RH_PORT_ENABLE))) ?
			(1 << RH_PORT_ENABLE) : 0;
		
		hc->rh.wPortChange_2 |=
			((hc->rh.prev_wPortStatus_2 & (1 << RH_PORT_ENABLE))
			 && !(r_usb_rh_port_status_2 & (1 << RH_PORT_ENABLE))) ?
			(1 << RH_PORT_ENABLE) : 0;
		
		/* C_PORT_SUSPEND seems difficult, lets ignore it.. (for now) */
		
		/* C_PORT_RESET is _only_ set on a transition from the resetting state
		   to the enabled state */
		hc->rh.wPortChange_1 |=
			((hc->rh.prev_wPortStatus_1 & (1 << RH_PORT_RESET))
			 && (r_usb_rh_port_status_1 & (1 << RH_PORT_ENABLE))) ?
			(1 << RH_PORT_RESET) : 0;
		
		hc->rh.wPortChange_2 |=
			((hc->rh.prev_wPortStatus_2 & (1 << RH_PORT_RESET))
			 && (r_usb_rh_port_status_2 & (1 << RH_PORT_ENABLE))) ?
			(1 << RH_PORT_RESET) : 0;
		
		hc->rh.prev_wPortStatus_1 = r_usb_rh_port_status_1;
		hc->rh.prev_wPortStatus_2 = r_usb_rh_port_status_2;	
	}

	for (epid = 0; epid < 32; epid++) {

		unsigned long flags;

		save_flags(flags);
		cli();

		*R_USB_EPT_INDEX = IO_FIELD(R_USB_EPT_INDEX, value, epid); nop();
		r_usb_ept_data = *R_USB_EPT_DATA;

		restore_flags(flags);

		if (r_usb_ept_data & IO_MASK(R_USB_EPT_DATA, hold)) {
			warn("Was hold for epid %d", epid);
			continue;
		}

		if (!(r_usb_ept_data & IO_MASK(R_USB_EPT_DATA, valid))) {
			continue;
		}
		
		
		if (test_bit(epid, (void *)&reg->r_usb_epid_attn)) {

			if (URB_List[epid] == NULL) {
				err("R_USB_EPT_DATA is 0x%08X", r_usb_ept_data);
				err("submit urb has been called %d times..", submit_urb_count);
				err("EPID_ATTN for epid %d, with NULL entry in list", epid);
				return;
			}
			
			dbg_ep("r_usb_ept_data [%d] == 0x%08X", epid,
			       r_usb_ept_data);
			
			error_code = IO_EXTRACT(R_USB_EPT_DATA, error_code,
						r_usb_ept_data);
			
			if (error_code == IO_STATE_VALUE(R_USB_EPT_DATA, error_code, no_error)) {
				/* no_error means that this urb was sucessfully sent or that we have
				   some undefinde error*/
				
				if (IO_EXTRACT(R_USB_EPT_DATA, error_count_out, r_usb_ept_data) == 3 ||
				    IO_EXTRACT(R_USB_EPT_DATA, error_count_in, r_usb_ept_data) == 3) {
				/* Actually there were transmission errors */
					warn("Undefined error for endpoint %d", epid);
					if (usb_pipetype(URB_List[epid]->pipe) == PIPE_CONTROL) {
						handle_control_transfer_attn(epid, -EPROTO);
					} else if (usb_pipetype(URB_List[epid]->pipe) == PIPE_BULK) {
						handle_bulk_transfer_attn(epid, -EPROTO);
					} else if (usb_pipetype(URB_List[epid]->pipe) == PIPE_INTERRUPT) {
						handle_intr_transfer_attn(epid, -EPROTO);
					}
							   
				} else {

					if (reg->r_usb_status & IO_MASK(R_USB_STATUS, perror)) {
						if (usb_pipetype(URB_List[epid]->pipe) == PIPE_INTERRUPT) {
							etrax_usb_do_intr_recover(epid);
						} else {
							panic("Epid attention for epid %d (none INTR), with no errors and no "
							      "exessive retry r_usb_status is 0x%02X\n",
							      epid, reg->r_usb_status);
						}
						
					} else if (reg->r_usb_status & IO_MASK(R_USB_STATUS, ourun)) {
						panic("Epid attention for epid %d, with no errors and no "
						      "exessive retry r_usb_status is 0x%02X\n",
						      epid, reg->r_usb_status);
						
					}
					
					warn("Epid attention for epid %d, with no errors and no "
					     "exessive retry r_usb_status is 0x%02X",
					     epid, reg->r_usb_status);
					warn("OUT error count: %d", IO_EXTRACT(R_USB_EPT_DATA, error_count_out,
									       r_usb_ept_data));
					warn("IN  error count: %d", IO_EXTRACT(R_USB_EPT_DATA, error_count_in,
									       r_usb_ept_data));
					

				}
				
			} else if (error_code == IO_STATE_VALUE(R_USB_EPT_DATA, error_code, stall)) {
				warn("Stall for endpoint %d", epid);
				if (usb_pipetype(URB_List[epid]->pipe) == PIPE_CONTROL) {
					handle_control_transfer_attn(epid, -EPIPE);
				} else if (usb_pipetype(URB_List[epid]->pipe) == PIPE_BULK) {
					handle_bulk_transfer_attn(epid, -EPIPE);
				} else if (usb_pipetype(URB_List[epid]->pipe) == PIPE_INTERRUPT) {
					handle_intr_transfer_attn(epid, -EPIPE);
				}
				
				
			} else if (error_code == IO_STATE_VALUE(R_USB_EPT_DATA, error_code, bus_error)) {
				panic("USB bus error for endpoint %d\n", epid);
				
			} else if (error_code == IO_STATE_VALUE(R_USB_EPT_DATA, error_code, buffer_error)) {
				warn("Buffer error for endpoint %d", epid);

				if (usb_pipetype(URB_List[epid]->pipe) == PIPE_CONTROL) {
					handle_control_transfer_attn(epid, -EPROTO);
				} else if (usb_pipetype(URB_List[epid]->pipe) == PIPE_BULK) {
					handle_bulk_transfer_attn(epid, -EPROTO);
				} else if (usb_pipetype(URB_List[epid]->pipe) == PIPE_INTERRUPT) {
					handle_intr_transfer_attn(epid, -EPROTO);
				}

			}
		} else if (test_bit(epid, (void *)&ep_really_active)) {
			/* Should really be else if (testbit(really active)) */

			if (usb_pipetype(URB_List[epid]->pipe) == PIPE_CONTROL) {

				if (!(TxCtrlEPList[epid].command & IO_MASK(USB_EP_command, enable))) {
					/* Now we have to verify that this CTRL endpoint got disabled
					   cause it reached end of list with no error */
					
					if (IO_EXTRACT(R_USB_EPT_DATA, error_code, r_usb_ept_data) ==
					    IO_STATE_VALUE(R_USB_EPT_DATA, error_code, no_error)) {
						/*
						  This means that the endpoint has no error, is disabled
						  and had inserted traffic,
						  i.e. transfer sucessfully completed
						*/
						dbg_ctrl("Last SB for CTRL %d sent sucessfully", epid);
						handle_control_transfer_attn(epid, 0);
					}
				}
				
			} else if (usb_pipetype(URB_List[epid]->pipe) == PIPE_BULK) {
				if (!(TxBulkEPList[epid].command & IO_MASK(USB_EP_command, enable))) {
					/* Now we have to verify that this BULK endpoint go disabled
					   cause it reached end of list with no error */

					if (IO_EXTRACT(R_USB_EPT_DATA, error_code, r_usb_ept_data) ==
					    IO_STATE_VALUE(R_USB_EPT_DATA, error_code, no_error)) {
						/*
						  This means that the endpoint has no error, is disabled
						  and had inserted traffic,
						  i.e. transfer sucessfully completed
						*/
						dbg_bulk("Last SB for BULK %d sent sucessfully", epid);
						handle_bulk_transfer_attn(epid, 0);
					}
				}
			} else if (usb_pipetype(URB_List[epid]->pipe) == PIPE_INTERRUPT) {
				handle_intr_transfer_attn(epid, 0);
			}
		}
		
	}	

	kfree(reg);

	DBFEXIT;
}


static void etrax_usb_hc_intr_top_half(int irq, void *vhc, struct pt_regs *regs)
{
	struct usb_reg_context *reg;

	DBFENTER;

	reg = (struct usb_reg_context *)kmalloc(sizeof(struct usb_reg_context), GFP_ATOMIC);

	if (!(reg)) {
		panic("kmalloc failed in top_half\n");
	}

	reg->hc = (etrax_hc_t *)vhc;
	reg->r_usb_irq_mask_read    = *R_USB_IRQ_MASK_READ;
	reg->r_usb_status           = *R_USB_STATUS;

#if 0
	if (reg->r_usb_status & IO_MASK(R_USB_STATUS, perror)) {
		panic("r_usb_status said perror\n");
	}
	if (reg->r_usb_status & IO_MASK(R_USB_STATUS, ourun)) {
		panic("r_usb_status said ourun !!!\n");
	}
#endif
	
	reg->r_usb_epid_attn        = *R_USB_EPID_ATTN;

	reg->r_usb_rh_port_status_1 = *R_USB_RH_PORT_STATUS_1;
	reg->r_usb_rh_port_status_2 = *R_USB_RH_PORT_STATUS_2;

	reg->usb_bh.sync = 0;
	reg->usb_bh.routine = etrax_usb_hc_intr_bottom_half;
	reg->usb_bh.data = reg;

	queue_task(&reg->usb_bh, &tq_immediate);
	mark_bh(IMMEDIATE_BH);

	DBFEXIT;
}

static int etrax_rh_submit_urb(struct urb *urb)
{
	struct usb_device *usb_dev = urb->dev;
	etrax_hc_t *hc = usb_dev->bus->hcpriv;
	unsigned int pipe = urb->pipe;
	struct usb_ctrlrequest *cmd = (struct usb_ctrlrequest *) urb->setup_packet;
	void *data = urb->transfer_buffer;
	int leni = urb->transfer_buffer_length;
	int len = 0;
	int status = 0;
	int stat = 0;
	int i;

	__u16 cstatus;

	__u16 bmRType_bReq;
	__u16 wValue;
	__u16 wIndex;
	__u16 wLength;

	DBFENTER;

	if (usb_pipetype (pipe) == PIPE_INTERRUPT) {
		dbg_rh("Root-Hub submit IRQ: every %d ms", urb->interval);
		hc->rh.urb = urb;
		hc->rh.send = 1;
		hc->rh.interval = urb->interval;
		etrax_rh_init_int_timer(urb);
		DBFEXIT;
		
		return 0;
	}

	bmRType_bReq = cmd->bRequestType | cmd->bRequest << 8;
	wValue = le16_to_cpu(cmd->wValue);
	wIndex = le16_to_cpu(cmd->wIndex);
	wLength = le16_to_cpu(cmd->wLength);

	dbg_rh("bmRType_bReq : 0x%04X (%d)", bmRType_bReq, bmRType_bReq);
	dbg_rh("wValue       : 0x%04X (%d)", wValue, wValue);
	dbg_rh("wIndex       : 0x%04X (%d)", wIndex, wIndex);
	dbg_rh("wLength      : 0x%04X (%d)", wLength, wLength);
	
	switch (bmRType_bReq) {
		
		/* Request Destination:
		   without flags: Device, 
		   RH_INTERFACE: interface, 
		   RH_ENDPOINT: endpoint,
		   RH_CLASS means HUB here, 
		   RH_OTHER | RH_CLASS  almost ever means HUB_PORT here 
		 */

	case RH_GET_STATUS:
		*(__u16 *) data = cpu_to_le16 (1);
		OK (2);
		
	case RH_GET_STATUS | RH_INTERFACE:
		*(__u16 *) data = cpu_to_le16 (0);
		OK (2);
		
	case RH_GET_STATUS | RH_ENDPOINT:
		*(__u16 *) data = cpu_to_le16 (0);
		OK (2);
		
	case RH_GET_STATUS | RH_CLASS:
		*(__u32 *) data = cpu_to_le32 (0);
		OK (4);		/* hub power ** */
		
	case RH_GET_STATUS | RH_OTHER | RH_CLASS:
		if (wIndex == 1) {
			*((__u16*)data) = cpu_to_le16(hc->rh.prev_wPortStatus_1);
			*((__u16*)data + 1) = cpu_to_le16(hc->rh.wPortChange_1);
		}
		else if (wIndex == 2) {
			*((__u16*)data) = cpu_to_le16(hc->rh.prev_wPortStatus_2);
			*((__u16*)data + 1) = cpu_to_le16(hc->rh.wPortChange_2);
		}
		else {
			dbg_rh("RH_GET_STATUS whith invalid wIndex !!");
			OK(0);
		}
		
		OK(4);

	case RH_CLEAR_FEATURE | RH_ENDPOINT:
		switch (wValue) {
		case (RH_ENDPOINT_STALL):
			OK (0);
		}
		break;

	case RH_CLEAR_FEATURE | RH_CLASS:
		switch (wValue) {
		case (RH_C_HUB_OVER_CURRENT):
			OK (0);	/* hub power over current ** */
		}
		break;

	case RH_CLEAR_FEATURE | RH_OTHER | RH_CLASS:
		switch (wValue) {
		case (RH_PORT_ENABLE):
			if (wIndex == 1) {

				dbg_rh("trying to do disable of port 1");

				*R_USB_PORT1_DISABLE = IO_STATE(R_USB_PORT1_DISABLE, disable, yes);
				while (hc->rh.prev_wPortStatus_1 &
				       IO_STATE(R_USB_RH_PORT_STATUS_1, enabled, yes));
				*R_USB_PORT1_DISABLE = IO_STATE(R_USB_PORT1_DISABLE, disable, no);
				dbg_rh("Port 1 is disabled");

			} else if (wIndex == 2) {

				dbg_rh("trying to do disable of port 2");
				
				*R_USB_PORT2_DISABLE = IO_STATE(R_USB_PORT2_DISABLE, disable, yes);
				while (hc->rh.prev_wPortStatus_2 &
				       IO_STATE(R_USB_RH_PORT_STATUS_2, enabled, yes));
				*R_USB_PORT2_DISABLE = IO_STATE(R_USB_PORT2_DISABLE, disable, no);
				dbg_rh("Port 2 is disabled");

			} else {
				dbg_rh("RH_CLEAR_FEATURE->RH_PORT_ENABLE "
				       "with invalid wIndex == %d!!", wIndex);
			}			
				
			OK (0);
		case (RH_PORT_SUSPEND):
			/* Opposite to suspend should be resume, so well do a resume */
			if (wIndex == 1) {
				*R_USB_COMMAND =
					IO_STATE(R_USB_COMMAND, port_sel, port1) |
					IO_STATE(R_USB_COMMAND, port_cmd, resume)|
					IO_STATE(R_USB_COMMAND, ctrl_cmd, nop);
			} else if (wIndex == 2) {
				*R_USB_COMMAND =
					IO_STATE(R_USB_COMMAND, port_sel, port2) |
					IO_STATE(R_USB_COMMAND, port_cmd, resume)|
					IO_STATE(R_USB_COMMAND, ctrl_cmd, nop);
			} else {
				dbg_rh("RH_CLEAR_FEATURE->RH_PORT_SUSPEND "
				       "with invalid wIndex == %d!!", wIndex);
			}
			
			OK (0);
		case (RH_PORT_POWER):
			OK (0);	/* port power ** */
		case (RH_C_PORT_CONNECTION):
			
			if (wIndex == 1) {
				hc->rh.wPortChange_1 &= ~(1 << RH_PORT_CONNECTION);
			}
			else if (wIndex == 2) {
				hc->rh.wPortChange_2 &= ~(1 << RH_PORT_CONNECTION);
			}
			else {
				dbg_rh("RH_CLEAR_FEATURE->RH_C_PORT_CONNECTION "
				       "with invalid wIndex == %d!!", wIndex);
			}

			OK (0);
		case (RH_C_PORT_ENABLE):
			if (wIndex == 1) {
				hc->rh.wPortChange_1 &= ~(1 << RH_PORT_ENABLE);
			}
			else if (wIndex == 2) {
				hc->rh.wPortChange_2 &= ~(1 << RH_PORT_ENABLE);
			}
			else {
				dbg_rh("RH_CLEAR_FEATURE->RH_C_PORT_ENABLE "
				       "with invalid wIndex == %d!!", wIndex);
			}
			OK (0);
		case (RH_C_PORT_SUSPEND):
/*** WR_RH_PORTSTAT(RH_PS_PSSC); */
			OK (0);
		case (RH_C_PORT_OVER_CURRENT):
			OK (0);	/* port power over current ** */
		case (RH_C_PORT_RESET):
			if (wIndex == 1) {
				hc->rh.wPortChange_1 &= ~(1 << RH_PORT_RESET);
			}
			else if (wIndex == 2) {
				dbg_rh("This is wPortChange before clear: 0x%04X", hc->rh.wPortChange_2);
				
				hc->rh.wPortChange_2 &= ~(1 << RH_PORT_RESET);
				dbg_rh("This is wPortChange after clear: 0x%04X", hc->rh.wPortChange_2);
			} else {
				dbg_rh("RH_CLEAR_FEATURE->RH_C_PORT_RESET "
				       "with invalid index == %d!!", wIndex);
			}

			OK (0);
		
		}
		break;
		
	case RH_SET_FEATURE | RH_OTHER | RH_CLASS:
		switch (wValue) {
		case (RH_PORT_SUSPEND):
			if (wIndex == 1) {
				*R_USB_COMMAND =
					IO_STATE(R_USB_COMMAND, port_sel, port1) |
					IO_STATE(R_USB_COMMAND, port_cmd, suspend) |
					IO_STATE(R_USB_COMMAND, ctrl_cmd, nop);
			} else if (wIndex == 2) {
				*R_USB_COMMAND =
					IO_STATE(R_USB_COMMAND, port_sel, port2) |
					IO_STATE(R_USB_COMMAND, port_cmd, suspend) |
					IO_STATE(R_USB_COMMAND, ctrl_cmd, nop);
			} else {
				dbg_rh("RH_SET_FEATURE->RH_C_PORT_SUSPEND "
				       "with invalid wIndex == %d!!", wIndex);
			}			

			OK (0);
		case (RH_PORT_RESET):
			if (wIndex == 1) {
				int port1_retry;
				
			port1_redo:
				dbg_rh("Doing reset of port 1");

				*R_USB_COMMAND =
					IO_STATE(R_USB_COMMAND, port_cmd, reset) |
					IO_STATE(R_USB_COMMAND, port_sel, port1);

				/* We must once again wait at least 10ms for the device to recover */

				port1_retry = 0;
				while (!((*((volatile __u16 *)&hc->rh.prev_wPortStatus_1)) &
					 IO_STATE(R_USB_RH_PORT_STATUS_1,
						  enabled, yes))) {
					printk(""); if (port1_retry++ >= 10000) {goto port1_redo;}
				}
				
				/* This only seems to work if we use printk,
				   not even schedule() works !!! WHY ?? */

				udelay(15000);
			}
			else if (wIndex == 2) {
				int port2_retry;
				
			port2_redo:
				dbg_rh("Doing reset of port 2");
				
				*R_USB_COMMAND =
					IO_STATE(R_USB_COMMAND, port_cmd, reset) |
					IO_STATE(R_USB_COMMAND, port_sel, port2);

				/* We must once again wait at least 10ms for the device to recover */

				port2_retry = 0;
				while (!((*((volatile __u16 *)&hc->rh.prev_wPortStatus_2)) &
					 IO_STATE(R_USB_RH_PORT_STATUS_2,
						  enabled, yes))) {
					printk(""); if (port2_retry++ >= 10000) {goto port2_redo;}
				}
				
				/* This only seems to work if we use printk,
				   not even schedule() works !!! WHY ?? */

				udelay(15000);
			}

			/* Try to bring the HC into running state */
			*R_USB_COMMAND =
				IO_STATE(R_USB_COMMAND, ctrl_cmd, host_run);
			
			nop(); while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));
			
			dbg_rh("...Done");
			OK(0);

		case (RH_PORT_POWER):
			OK (0);	/* port power ** */
		case (RH_PORT_ENABLE):
			/* There is no rh port enable command in the Etrax USB interface!!!! */
			OK (0);

		}
		break;
		
	case RH_SET_ADDRESS:
		hc->rh.devnum = wValue;
		dbg_rh("RH address set to: %d", hc->rh.devnum);
		OK (0);
		
	case RH_GET_DESCRIPTOR:
		switch ((wValue & 0xff00) >> 8) {
		case (0x01):	/* device descriptor */
			len = min_t(unsigned int, leni, min_t(unsigned int, sizeof (root_hub_dev_des), wLength));
			memcpy (data, root_hub_dev_des, len);
			OK (len);
		case (0x02):	/* configuration descriptor */
			len = min_t(unsigned int, leni, min_t(unsigned int, sizeof (root_hub_config_des), wLength));
			memcpy (data, root_hub_config_des, len);
			OK (len);
		case (0x03):	/* string descriptors */
			len = usb_root_hub_string (wValue & 0xff,
						   0xff, "ETRAX 100LX",
						   data, wLength);
			if (len > 0) {
				OK(min_t(int, leni, len));
			} else 
				stat = -EPIPE;
		}
		break;
		
	case RH_GET_DESCRIPTOR | RH_CLASS:
		root_hub_hub_des[2] = hc->rh.numports;
		len = min_t(unsigned int, leni, min_t(unsigned int, sizeof (root_hub_hub_des), wLength));
		memcpy (data, root_hub_hub_des, len);
		OK (len);
		
	case RH_GET_CONFIGURATION:
		*(__u8 *) data = 0x01;
		OK (1);
		
	case RH_SET_CONFIGURATION:
		OK (0);
		
	default:
		stat = -EPIPE;
	}

	urb->actual_length = len;
	urb->status = stat;
	urb->dev=NULL;
	if (urb->complete) {
		urb->complete (urb);
	}
	DBFEXIT;
	
	return 0;
}

static int __init etrax_usb_hc_init(void)
{
	static etrax_hc_t *hc;
	struct usb_bus *bus;
	struct usb_device *usb_rh;
	
	DBFENTER;

	info("ETRAX 100LX USB-HCD %s (c) 2001 Axis Communications AB\n", usb_hcd_version);
	
	hc = kmalloc(sizeof(etrax_hc_t), GFP_KERNEL);

	/* We use kmem_cache_* to make sure that all DMA desc. are dword aligned */
	usb_desc_cache = kmem_cache_create("usb_desc_cache", sizeof(USB_EP_Desc_t), 0, 0, 0, 0);
	if (!usb_desc_cache) {
		panic("USB Desc Cache allocation failed !!!\n");
	}
	
	etrax_usb_bus = bus = usb_alloc_bus(&etrax_usb_device_operations);
	hc->bus = bus;
	bus->hcpriv = hc;

	/* Initalize RH to the default address.
	   And make sure that we have no status change indication */
	hc->rh.numports = 2;  /* The RH has two ports */
	hc->rh.devnum = 0;
	hc->rh.wPortChange_1 = 0;
	hc->rh.wPortChange_2 = 0;

	/* Also initate the previous values to zero */
	hc->rh.prev_wPortStatus_1 = 0;
	hc->rh.prev_wPortStatus_2 = 0;

	/* Initialize the intr-traffic flags */
	hc->intr.sleeping = 0;
	hc->intr.wq = NULL;

	/* Initially all ep's are free except ep 0 */
	ep_usage_bitmask = 0;
	set_bit(0, (void *)&ep_usage_bitmask);
	ep_really_active = 0;

	memset(URB_List, 0, sizeof(URB_List));

	/* This code should really be moved */

	if (request_dma(USB_TX_DMA_NBR, "ETRAX 100LX built-in USB (Tx)")) {
		err("Could not allocate DMA ch 8 for USB");
		etrax_usb_hc_cleanup();
		DBFEXIT;
		return -1;
	}
	
	if (request_dma(USB_RX_DMA_NBR, "ETRAX 100LX built-in USB (Rx)")) {
		err("Could not allocate DMA ch 9 for USB");
		etrax_usb_hc_cleanup();
		DBFEXIT;
		return -1;
	}	
#if 0  /* Moved to head.S */
	*R_GEN_CONFIG = genconfig_shadow =
		(genconfig_shadow & ~(IO_MASK(R_GEN_CONFIG, usb1) |
				      IO_MASK(R_GEN_CONFIG, usb2) |
				      IO_MASK(R_GEN_CONFIG, dma8) |
				      IO_MASK(R_GEN_CONFIG, dma9))) |
		IO_STATE(R_GEN_CONFIG, dma8, usb) |
		IO_STATE(R_GEN_CONFIG, dma9, usb)
#ifdef CONFIG_ETRAX_USB_HOST_PORT1
		| IO_STATE(R_GEN_CONFIG, usb1, select)
#endif
#ifdef CONFIG_ETRAX_USB_HOST_PORT2
		| IO_STATE(R_GEN_CONFIG, usb2, select)
#endif
	;
#endif
	
	usb_register_bus(hc->bus);

	/* We may have to set more bits, but these are the obvious ones */
	*R_IRQ_MASK2_SET =
		IO_STATE(R_IRQ_MASK2_SET, dma8_sub0_descr, set) |
		IO_STATE(R_IRQ_MASK2_SET, dma8_sub1_descr, set) |
		IO_STATE(R_IRQ_MASK2_SET, dma8_sub2_descr, set) |
		IO_STATE(R_IRQ_MASK2_SET, dma8_sub3_descr, set);

	*R_IRQ_MASK2_SET =
		IO_STATE(R_IRQ_MASK2_SET, dma9_eop, set) |
		IO_STATE(R_IRQ_MASK2_SET, dma9_descr, set);

	*R_USB_IRQ_MASK_SET = 
		IO_STATE(R_USB_IRQ_MASK_SET, ctl_status, set) |
		IO_STATE(R_USB_IRQ_MASK_SET, ctl_eot, set) |
		IO_STATE(R_USB_IRQ_MASK_SET, bulk_eot, set) |
#ifdef ETRAX_USB_INTR_IRQ
		IO_STATE(R_USB_IRQ_MASK_SET, intr_eot, set) |
#endif
		IO_STATE(R_USB_IRQ_MASK_SET, epid_attn, set) |
		IO_STATE(R_USB_IRQ_MASK_SET, port_status, set);
	
	if (request_irq(ETRAX_USB_HC_IRQ, etrax_usb_hc_intr_top_half, 0,
			"ETRAX 100LX built-in USB (HC)", hc)) {
		err("Could not allocate IRQ %d for USB", ETRAX_USB_HC_IRQ);
		etrax_usb_hc_cleanup();
		DBFEXIT;
		return -1;
	}
	
	if (request_irq(ETRAX_USB_RX_IRQ, etrax_usb_rx_interrupt, 0,
			"ETRAX 100LX built-in USB (Rx)", hc)) {
		err("Could not allocate IRQ %d for USB", ETRAX_USB_RX_IRQ);
		etrax_usb_hc_cleanup();
		DBFEXIT;
		return -1;
	}
	
	if (request_irq(ETRAX_USB_TX_IRQ, etrax_usb_tx_interrupt, 0,
			"ETRAX 100LX built-in USB (Tx)", hc)) {
		err("Could not allocate IRQ %d for USB", ETRAX_USB_TX_IRQ);
		etrax_usb_hc_cleanup();
		DBFEXIT;
		return -1;
	}

	/* Reset the USB interface (configures as HC) */
	*R_USB_COMMAND =
		IO_STATE(R_USB_COMMAND, ctrl_cmd, reset) |
		IO_STATE(R_USB_COMMAND, port_cmd, reset);
	
	nop(); while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));
#if 1
	/* Initate PSTART to all unallocatable bit times */
	*R_USB_FM_PSTART = IO_FIELD(R_USB_FM_PSTART, value, 10000);
#endif

#ifdef CONFIG_ETRAX_USB_HOST_PORT1
	*R_USB_PORT1_DISABLE = IO_STATE(R_USB_PORT1_DISABLE, disable, no);
#endif
	
#ifdef CONFIG_ETRAX_USB_HOST_PORT2
	*R_USB_PORT2_DISABLE = IO_STATE(R_USB_PORT2_DISABLE, disable, no);
#endif
	
	*R_USB_COMMAND =
		IO_STATE(R_USB_COMMAND, ctrl_cmd, host_config) |
		IO_STATE(R_USB_COMMAND, port_cmd, reset);
	
	nop(); while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));

	*R_USB_COMMAND =
		IO_STATE(R_USB_COMMAND, port_sel, port1) |
		IO_STATE(R_USB_COMMAND, port_cmd, reset);

	nop(); while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));

	/* Here we must wait at least 10ms so the device has time to recover */
	udelay(15000);

	init_rx_buffers();
	init_tx_bulk_ep();
	init_tx_ctrl_ep();
	init_tx_intr_ep();

	/* This works. It seems like the host_run command only has effect when a device is connected,
	 i.e. it has to be done when a interrup */
	*R_USB_COMMAND =
		IO_STATE(R_USB_COMMAND, ctrl_cmd, host_run);
	
	nop(); while (*R_USB_COMMAND & IO_MASK(R_USB_COMMAND, busy));

	usb_rh = usb_alloc_dev(NULL, hc->bus);
	hc->bus->root_hub = usb_rh;
	usb_connect(usb_rh);
	usb_new_device(usb_rh);

	DBFEXIT;
	
	return 0;
}

static void etrax_usb_hc_cleanup(void)
{
	DBFENTER;
	
	free_irq(ETRAX_USB_HC_IRQ, NULL);
	free_irq(ETRAX_USB_RX_IRQ, NULL);
	free_irq(ETRAX_USB_TX_IRQ, NULL);

	free_dma(USB_TX_DMA_NBR);
	free_dma(USB_RX_DMA_NBR);
	usb_deregister_bus(etrax_usb_bus);

	DBFEXIT;
}

module_init(etrax_usb_hc_init);
module_exit(etrax_usb_hc_cleanup);
