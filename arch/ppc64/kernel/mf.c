/*
  * mf.c
  * Copyright (C) 2001 Troy D. Armstrong  IBM Corporation
  *
  * This modules exists as an interface between a Linux secondary partition
  * running on an iSeries and the primary partition's Virtual Service
  * Processor (VSP) object.  The VSP has final authority over powering on/off
  * all partitions in the iSeries.  It also provides miscellaneous low-level
  * machine facility type operations.
  *
  * 
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation; either version 2 of the License, or
  * (at your option) any later version.
  * 
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  * 
  * You should have received a copy of the GNU General Public License
  * along with this program; if not, write to the Free Software
  * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
  */

#include <asm/iSeries/mf.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <asm/iSeries/HvLpConfig.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/nvram.h>
#include <asm/time.h>
#include <asm/iSeries/ItSpCommArea.h>
#include <asm/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/bcd.h>
#include <asm/iSeries/vio.h>

/*
 * This is the structure layout for the Machine Facilites LPAR event
 * flows.
 */
union safe_cast {
	u64 ptr_as_u64;
	void *ptr;
};

struct VspCmdData {
	union safe_cast token;
	u16 cmd;
	HvLpIndex lp_index;
	u8 result_code;
	u32 reserved;
	union {
		u64 state;	/* GetStateOut */
		u64 ipl_type;	/* GetIplTypeOut, Function02SelectIplTypeIn */
		u64 ipl_mode;	/* GetIplModeOut, Function02SelectIplModeIn */
		u64 page[4];	/* GetSrcHistoryIn */
		u64 flag;	/* GetAutoIplWhenPrimaryIplsOut,
				   SetAutoIplWhenPrimaryIplsIn,
				   WhiteButtonPowerOffIn,
				   Function08FastPowerOffIn,
				   IsSpcnRackPowerIncompleteOut */
		struct {
			u64 token;
			u64 address_type;
			u64 side;
			u32 length;
			u32 offset;
		} kern;		/* SetKernelImageIn, GetKernelImageIn,
				   SetKernelCmdLineIn, GetKernelCmdLineIn */
		u32 length_out;	/* GetKernelImageOut, GetKernelCmdLineOut */
		u8 reserved[80];
	} sub_data;
};

struct VspRspData {
	struct completion com;
	struct VspCmdData *response;
};

struct AllocData {
	u16 size;
	u16 type;
	u32 count;
	u16 reserved1;
	u8 reserved2;
	HvLpIndex target_lp;
};

struct CeMsgData;

typedef void (*CeMsgCompleteHandler)(void *token, struct CeMsgData *vspCmdRsp);

struct CeMsgCompleteData {
	CeMsgCompleteHandler handler;
	void *token;
};

struct CeMsgData {
	u8 ce_msg[12];
	char reserved[4];
	struct CeMsgCompleteData *completion;
};

struct IoMFLpEvent {
	struct HvLpEvent hp_lp_event;
	u16 subtype_result_code;
	u16 reserved1;
	u32 reserved2;
	union {
		struct AllocData alloc;
		struct CeMsgData ce_msg;
		struct VspCmdData vsp_cmd;
	} data;
};

#define subtype_data(a, b, c, d)	\
		(((a) << 24) + ((b) << 16) + ((c) << 8) + (d))

/*
 * All outgoing event traffic is kept on a FIFO queue.  The first
 * pointer points to the one that is outstanding, and all new
 * requests get stuck on the end.  Also, we keep a certain number of
 * preallocated pending events so that we can operate very early in
 * the boot up sequence (before kmalloc is ready).
 */
struct pending_event {
	struct pending_event *next;
	struct IoMFLpEvent event;
	MFCompleteHandler hdlr;
	char dma_data[72];
	unsigned dma_data_length;
	unsigned remote_address;
};
static spinlock_t pending_event_spinlock;
static struct pending_event *pending_event_head;
static struct pending_event *pending_event_tail;
static struct pending_event *pending_event_avail;
static struct pending_event pending_event_prealloc[16];

/*
 * Put a pending event onto the available queue, so it can get reused.
 * Attention! You must have the pending_event_spinlock before calling!
 */
static void free_pending_event(struct pending_event *ev)
{
	if (ev != NULL) {
		ev->next = pending_event_avail;
		pending_event_avail = ev;
	}
}

/*
 * Enqueue the outbound event onto the stack.  If the queue was
 * empty to begin with, we must also issue it via the Hypervisor
 * interface.  There is a section of code below that will touch
 * the first stack pointer without the protection of the pending_event_spinlock.
 * This is OK, because we know that nobody else will be modifying
 * the first pointer when we do this.
 */
static int signal_event(struct pending_event *ev)
{
	int rc = 0;
	unsigned long flags;
	int go = 1;
	struct pending_event *ev1;
	HvLpEvent_Rc hvRc;

	/* enqueue the event */
	if (ev != NULL) {
		ev->next = NULL;
		spin_lock_irqsave(&pending_event_spinlock, flags);
		if (pending_event_head == NULL)
			pending_event_head = ev;
		else {
			go = 0;
			pending_event_tail->next = ev;
		}
		pending_event_tail = ev;
		spin_unlock_irqrestore(&pending_event_spinlock, flags);
	}

	/* send the event */
	while (go) {
		go = 0;

		/* any DMA data to send beforehand? */
		if (pending_event_head->dma_data_length > 0)
			HvCallEvent_dmaToSp(pending_event_head->dma_data,
					pending_event_head->remote_address,
					pending_event_head->dma_data_length,
					HvLpDma_Direction_LocalToRemote);

		hvRc = HvCallEvent_signalLpEvent(
				&pending_event_head->event.hp_lp_event);
		if (hvRc != HvLpEvent_Rc_Good) {
			printk(KERN_ERR "mf.c: HvCallEvent_signalLpEvent() failed with %d\n",
					(int)hvRc);

			spin_lock_irqsave(&pending_event_spinlock, flags);
			ev1 = pending_event_head;
			pending_event_head = pending_event_head->next;
			if (pending_event_head != NULL)
				go = 1;
			spin_unlock_irqrestore(&pending_event_spinlock, flags);

			if (ev1 == ev)
				rc = -EIO;
			else if (ev1->hdlr != NULL) {
				union safe_cast mySafeCast;

				mySafeCast.ptr_as_u64 = ev1->event.hp_lp_event.xCorrelationToken;
				(*ev1->hdlr)(mySafeCast.ptr, -EIO);
			}

			spin_lock_irqsave(&pending_event_spinlock, flags);
			free_pending_event(ev1);
			spin_unlock_irqrestore(&pending_event_spinlock, flags);
		}
	}

	return rc;
}

/*
 * Allocate a new pending_event structure, and initialize it.
 */
static struct pending_event *new_pending_event(void)
{
	struct pending_event *ev = NULL;
	HvLpIndex primaryLp = HvLpConfig_getPrimaryLpIndex();
	unsigned long flags;
	struct HvLpEvent *hev;

	spin_lock_irqsave(&pending_event_spinlock, flags);
	if (pending_event_avail != NULL) {
		ev = pending_event_avail;
		pending_event_avail = pending_event_avail->next;
	}
	spin_unlock_irqrestore(&pending_event_spinlock, flags);
	if (ev == NULL)
		ev = kmalloc(sizeof(struct pending_event),GFP_ATOMIC);
	if (ev == NULL) {
		printk(KERN_ERR "mf.c: unable to kmalloc %ld bytes\n",
				sizeof(struct pending_event));
		return NULL;
	}
	memset(ev, 0, sizeof(struct pending_event));
	hev = &ev->event.hp_lp_event;
	hev->xFlags.xValid = 1;
	hev->xFlags.xAckType = HvLpEvent_AckType_ImmediateAck;
	hev->xFlags.xAckInd = HvLpEvent_AckInd_DoAck;
	hev->xFlags.xFunction = HvLpEvent_Function_Int;
	hev->xType = HvLpEvent_Type_MachineFac;
	hev->xSourceLp = HvLpConfig_getLpIndex();
	hev->xTargetLp = primaryLp;
	hev->xSizeMinus1 = sizeof(ev->event)-1;
	hev->xRc = HvLpEvent_Rc_Good;
	hev->xSourceInstanceId = HvCallEvent_getSourceLpInstanceId(primaryLp,
			HvLpEvent_Type_MachineFac);
	hev->xTargetInstanceId = HvCallEvent_getTargetLpInstanceId(primaryLp,
			HvLpEvent_Type_MachineFac);

	return ev;
}

static int signal_vsp_instruction(struct VspCmdData *vspCmd)
{
	struct pending_event *ev = new_pending_event();
	int rc;
	struct VspRspData response;

	if (ev == NULL)
		return -ENOMEM;

	init_completion(&response.com);
	response.response = vspCmd;
	ev->event.hp_lp_event.xSubtype = 6;
	ev->event.hp_lp_event.x.xSubtypeData =
		subtype_data('M', 'F',  'V',  'I');
	ev->event.data.vsp_cmd.token.ptr = &response;
	ev->event.data.vsp_cmd.cmd = vspCmd->cmd;
	ev->event.data.vsp_cmd.lp_index = HvLpConfig_getLpIndex();
	ev->event.data.vsp_cmd.result_code = 0xFF;
	ev->event.data.vsp_cmd.reserved = 0;
	memcpy(&(ev->event.data.vsp_cmd.sub_data),
			&(vspCmd->sub_data), sizeof(vspCmd->sub_data));
	mb();

	rc = signal_event(ev);
	if (rc == 0)
		wait_for_completion(&response.com);
	return rc;
}


/*
 * Send a 12-byte CE message to the primary partition VSP object
 */
static int signal_ce_msg(char *ce_msg, struct CeMsgCompleteData *completion)
{
	struct pending_event *ev = new_pending_event();

	if (ev == NULL)
		return -ENOMEM;

	ev->event.hp_lp_event.xSubtype = 0;
	ev->event.hp_lp_event.x.xSubtypeData =
		subtype_data('M',  'F',  'C',  'E');
	memcpy(ev->event.data.ce_msg.ce_msg, ce_msg, 12);
	ev->event.data.ce_msg.completion = completion;
	return signal_event(ev);
}

/*
 * Send a 12-byte CE message and DMA data to the primary partition VSP object
 */
static int dma_and_signal_ce_msg(char *ce_msg,
		struct CeMsgCompleteData *completion, void *dma_data,
		unsigned dma_data_length, unsigned remote_address)
{
	struct pending_event *ev = new_pending_event();

	if (ev == NULL)
		return -ENOMEM;

	ev->event.hp_lp_event.xSubtype = 0;
	ev->event.hp_lp_event.x.xSubtypeData =
		subtype_data('M', 'F', 'C', 'E');
	memcpy(ev->event.data.ce_msg.ce_msg, ce_msg, 12);
	ev->event.data.ce_msg.completion = completion;
	memcpy(ev->dma_data, dma_data, dma_data_length);
	ev->dma_data_length = dma_data_length;
	ev->remote_address = remote_address;
	return signal_event(ev);
}

/*
 * Initiate a nice (hopefully) shutdown of Linux.  We simply are
 * going to try and send the init process a SIGINT signal.  If
 * this fails (why?), we'll simply force it off in a not-so-nice
 * manner.
 */
static int shutdown(void)
{
	int rc = kill_proc(1, SIGINT, 1);

	if (rc) {
		printk(KERN_ALERT "mf.c: SIGINT to init failed (%d), "
				"hard shutdown commencing\n", rc);
		mf_powerOff();
	} else
		printk(KERN_INFO "mf.c: init has been successfully notified "
				"to proceed with shutdown\n");
	return rc;
}

/*
 * The primary partition VSP object is sending us a new
 * event flow.  Handle it...
 */
static void intReceived(struct IoMFLpEvent *event)
{
	int freeIt = 0;
	struct pending_event *two = NULL;

	/* ack the interrupt */
	event->hp_lp_event.xRc = HvLpEvent_Rc_Good;
	HvCallEvent_ackLpEvent(&event->hp_lp_event);

	/* process interrupt */
	switch (event->hp_lp_event.xSubtype) {
	case 0:	/* CE message */
		switch (event->data.ce_msg.ce_msg[3]) {
		case 0x5B:	/* power control notification */
			if ((event->data.ce_msg.ce_msg[5] & 0x20) != 0) {
				printk(KERN_INFO "mf.c: Commencing partition shutdown\n");
				if (shutdown() == 0)
					signal_ce_msg("\x00\x00\x00\xDB\x00\x00\x00\x00\x00\x00\x00\x00", NULL);
			}
			break;
		case 0xC0:	/* get time */
			if ((pending_event_head == NULL) ||
			    (pending_event_head->event.data.ce_msg.ce_msg[3]
			     != 0x40))
				break;
			freeIt = 1;
			if (pending_event_head->event.data.ce_msg.completion != 0) {
				CeMsgCompleteHandler handler = pending_event_head->event.data.ce_msg.completion->handler;
				void *token = pending_event_head->event.data.ce_msg.completion->token;

				if (handler != NULL)
					(*handler)(token, &(event->data.ce_msg));
			}
			break;
		}

		/* remove from queue */
		if (freeIt == 1) {
			unsigned long flags;

			spin_lock_irqsave(&pending_event_spinlock, flags);
			if (pending_event_head != NULL) {
				struct pending_event *oldHead =
					pending_event_head;

				pending_event_head = pending_event_head->next;
				two = pending_event_head;
				free_pending_event(oldHead);
			}
			spin_unlock_irqrestore(&pending_event_spinlock, flags);
		}

		/* send next waiting event */
		if (two != NULL)
			signal_event(NULL);
		break;
	case 1:	/* IT sys shutdown */
		printk(KERN_INFO "mf.c: Commencing system shutdown\n");
		shutdown();
		break;
	}
}

/*
 * The primary partition VSP object is acknowledging the receipt
 * of a flow we sent to them.  If there are other flows queued
 * up, we must send another one now...
 */
static void ackReceived(struct IoMFLpEvent *event)
{
	unsigned long flags;
	struct pending_event * two = NULL;
	unsigned long freeIt = 0;

	/* handle current event */
	if (pending_event_head != NULL) {
		switch (event->hp_lp_event.xSubtype) {
		case 0:     /* CE msg */
			if (event->data.ce_msg.ce_msg[3] == 0x40) {
				if (event->data.ce_msg.ce_msg[2] != 0) {
					freeIt = 1;
					if (pending_event_head->event.data.ce_msg.completion
							!= 0) {
						CeMsgCompleteHandler handler = pending_event_head->event.data.ce_msg.completion->handler;
						void *token = pending_event_head->event.data.ce_msg.completion->token;

						if (handler != NULL)
							(*handler)(token, &(event->data.ce_msg));
					}
				}
			} else
				freeIt = 1;
			break;
		case 4:	/* allocate */
		case 5:	/* deallocate */
			if (pending_event_head->hdlr != NULL) {
				union safe_cast mySafeCast;

				mySafeCast.ptr_as_u64 = event->hp_lp_event.xCorrelationToken;
				(*pending_event_head->hdlr)(mySafeCast.ptr, event->data.alloc.count);
			}
			freeIt = 1;
			break;
		case 6:
			{
				struct VspRspData *rsp = (struct VspRspData *)event->data.vsp_cmd.token.ptr;

				if (rsp != NULL) {
					if (rsp->response != NULL)
						memcpy(rsp->response, &(event->data.vsp_cmd), sizeof(event->data.vsp_cmd));
					complete(&rsp->com);
				} else
					printk(KERN_ERR "mf.c: no rsp\n");
				freeIt = 1;
			}
			break;
		}
	}
	else
		printk(KERN_ERR "mf.c: stack empty for receiving ack\n");

	/* remove from queue */
	spin_lock_irqsave(&pending_event_spinlock, flags);
	if ((pending_event_head != NULL) && (freeIt == 1)) {
		struct pending_event *oldHead = pending_event_head;

		pending_event_head = pending_event_head->next;
		two = pending_event_head;
		free_pending_event(oldHead);
	} 
	spin_unlock_irqrestore(&pending_event_spinlock, flags);

	/* send next waiting event */
	if (two != NULL)
		signal_event(NULL);
}

/*
 * This is the generic event handler we are registering with
 * the Hypervisor.  Ensure the flows are for us, and then
 * parse it enough to know if it is an interrupt or an
 * acknowledge.
 */
static void hvHandler(struct HvLpEvent *event, struct pt_regs *regs)
{
	if ((event != NULL) && (event->xType == HvLpEvent_Type_MachineFac)) {
		switch(event->xFlags.xFunction) {
		case HvLpEvent_Function_Ack:
			ackReceived((struct IoMFLpEvent *)event);
			break;
		case HvLpEvent_Function_Int:
			intReceived((struct IoMFLpEvent *)event);
			break;
		default:
			printk(KERN_ERR "mf.c: non ack/int event received\n");
			break;
		}
	} else
		printk(KERN_ERR "mf.c: alien event received\n");
}

/*
 * Global kernel interface to allocate and seed events into the
 * Hypervisor.
 */
void mf_allocateLpEvents(HvLpIndex targetLp, HvLpEvent_Type type,
		unsigned size, unsigned count, MFCompleteHandler hdlr,
		void *userToken)
{
	struct pending_event *ev = new_pending_event();
	int rc;

	if (ev == NULL) {
		rc = -ENOMEM;
	} else {
		union safe_cast mine;

		mine.ptr = userToken;
		ev->event.hp_lp_event.xSubtype = 4;
		ev->event.hp_lp_event.xCorrelationToken = mine.ptr_as_u64;
		ev->event.hp_lp_event.x.xSubtypeData =
			subtype_data('M', 'F', 'M', 'A');
		ev->event.data.alloc.target_lp = targetLp;
		ev->event.data.alloc.type = type;
		ev->event.data.alloc.size = size;
		ev->event.data.alloc.count = count;
		ev->hdlr = hdlr;
		rc = signal_event(ev);
	}
	if ((rc != 0) && (hdlr != NULL))
		(*hdlr)(userToken, rc);
}
EXPORT_SYMBOL(mf_allocateLpEvents);

/*
 * Global kernel interface to unseed and deallocate events already in
 * Hypervisor.
 */
void mf_deallocateLpEvents(HvLpIndex targetLp, HvLpEvent_Type type,
		unsigned count, MFCompleteHandler hdlr, void *userToken)
{
	struct pending_event *ev = new_pending_event();
	int rc;

	if (ev == NULL)
		rc = -ENOMEM;
	else {
		union safe_cast mine;

		mine.ptr = userToken;
		ev->event.hp_lp_event.xSubtype = 5;
		ev->event.hp_lp_event.xCorrelationToken = mine.ptr_as_u64;
		ev->event.hp_lp_event.x.xSubtypeData =
			subtype_data('M', 'F', 'M', 'D');
		ev->event.data.alloc.target_lp = targetLp;
		ev->event.data.alloc.type = type;
		ev->event.data.alloc.count = count;
		ev->hdlr = hdlr;
		rc = signal_event(ev);
	}
	if ((rc != 0) && (hdlr != NULL))
		(*hdlr)(userToken, rc);
}
EXPORT_SYMBOL(mf_deallocateLpEvents);

/*
 * Global kernel interface to tell the VSP object in the primary
 * partition to power this partition off.
 */
void mf_powerOff(void)
{
	printk(KERN_INFO "mf.c: Down it goes...\n");
	signal_ce_msg("\x00\x00\x00\x4D\x00\x00\x00\x00\x00\x00\x00\x00", NULL);
	for (;;);
}

/*
 * Global kernel interface to tell the VSP object in the primary
 * partition to reboot this partition.
 */
void mf_reboot(void)
{
	printk(KERN_INFO "mf.c: Preparing to bounce...\n");
	signal_ce_msg("\x00\x00\x00\x4E\x00\x00\x00\x00\x00\x00\x00\x00", NULL);
	for (;;);
}

/*
 * Display a single word SRC onto the VSP control panel.
 */
void mf_displaySrc(u32 word)
{
	u8 ce[12];

	memcpy(ce, "\x00\x00\x00\x4A\x00\x00\x00\x01\x00\x00\x00\x00", 12);
	ce[8] = word >> 24;
	ce[9] = word >> 16;
	ce[10] = word >> 8;
	ce[11] = word;
	signal_ce_msg(ce, NULL);
}

/*
 * Display a single word SRC of the form "PROGXXXX" on the VSP control panel.
 */
void mf_displayProgress(u16 value)
{
	u8 ce[12];
	u8 src[72];

	memcpy(ce, "\x00\x00\x04\x4A\x00\x00\x00\x48\x00\x00\x00\x00", 12);
	memcpy(src, "\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
		"\x00\x00\x00\x00PROGxxxx                        ",
		72);
	src[6] = value >> 8;
	src[7] = value & 255;
	src[44] = "0123456789ABCDEF"[(value >> 12) & 15];
	src[45] = "0123456789ABCDEF"[(value >> 8) & 15];
	src[46] = "0123456789ABCDEF"[(value >> 4) & 15];
	src[47] = "0123456789ABCDEF"[value & 15];
	dma_and_signal_ce_msg(ce, NULL, src, sizeof(src), 9 * 64 * 1024);
}

/*
 * Clear the VSP control panel.  Used to "erase" an SRC that was
 * previously displayed.
 */
void mf_clearSrc(void)
{
	signal_ce_msg("\x00\x00\x00\x4B\x00\x00\x00\x00\x00\x00\x00\x00", NULL);
}

/*
 * Initialization code here.
 */
void mf_init(void)
{
	int i;

	/* initialize */
	spin_lock_init(&pending_event_spinlock);
	for (i = 0;
	     i < sizeof(pending_event_prealloc) / sizeof(*pending_event_prealloc);
	     ++i)
		free_pending_event(&pending_event_prealloc[i]);
	HvLpEvent_registerHandler(HvLpEvent_Type_MachineFac, &hvHandler);

	/* virtual continue ack */
	signal_ce_msg("\x00\x00\x00\x57\x00\x00\x00\x00\x00\x00\x00\x00", NULL);

	/* initialization complete */
	printk(KERN_NOTICE "mf.c: iSeries Linux LPAR Machine Facilities initialized\n");
}

void mf_setSide(char side)
{
	u64 newSide;
	struct VspCmdData myVspCmd;

	memset(&myVspCmd, 0, sizeof(myVspCmd));
	switch (side) {
	case 'A':	newSide = 0;
			break;
	case 'B':	newSide = 1;
			break;
	case 'C':	newSide = 2; 
			break;
	default:	newSide = 3;
			break;
	}
	myVspCmd.sub_data.ipl_type = newSide;
	myVspCmd.cmd = 10;

	(void)signal_vsp_instruction(&myVspCmd);
}

char mf_getSide(void)
{
	char returnValue = ' ';
	int rc = 0;
	struct VspCmdData myVspCmd;

	memset(&myVspCmd, 0, sizeof(myVspCmd));
	myVspCmd.cmd = 2;
	myVspCmd.sub_data.ipl_type = 0;
	mb();
	rc = signal_vsp_instruction(&myVspCmd);

	if (rc != 0)
		return returnValue;

	if (myVspCmd.result_code == 0) {
		switch (myVspCmd.sub_data.ipl_type) {
		case 0:	returnValue = 'A';
			break;
		case 1:	returnValue = 'B';
			break;
		case 2:	returnValue = 'C';
			break;
		default:	returnValue = 'D';
			break;
		}
	}
	return returnValue;
}

void mf_getSrcHistory(char *buffer, int size)
{
#if 0
	struct IplTypeReturnStuff returnStuff;
	struct pending_event *ev = new_pending_event();
	int rc = 0;
	char *pages[4];

	pages[0] = kmalloc(4096, GFP_ATOMIC);
	pages[1] = kmalloc(4096, GFP_ATOMIC);
	pages[2] = kmalloc(4096, GFP_ATOMIC);
	pages[3] = kmalloc(4096, GFP_ATOMIC);
	if ((ev == NULL) || (pages[0] == NULL) || (pages[1] == NULL)
			 || (pages[2] == NULL) || (pages[3] == NULL))
		return -ENOMEM;

	returnStuff.xType = 0;
	returnStuff.xRc = 0;
	returnStuff.xDone = 0;
	ev->event.hp_lp_event.xSubtype = 6;
	ev->event.hp_lp_event.x.xSubtypeData =
		subtype_data('M', 'F', 'V', 'I');
	ev->event.data.vsp_cmd.xEvent = &returnStuff;
	ev->event.data.vsp_cmd.cmd = 4;
	ev->event.data.vsp_cmd.lp_index = HvLpConfig_getLpIndex();
	ev->event.data.vsp_cmd.result_code = 0xFF;
	ev->event.data.vsp_cmd.reserved = 0;
	ev->event.data.vsp_cmd.sub_data.page[0] = ISERIES_HV_ADDR(pages[0]);
	ev->event.data.vsp_cmd.sub_data.page[1] = ISERIES_HV_ADDR(pages[1]);
	ev->event.data.vsp_cmd.sub_data.page[2] = ISERIES_HV_ADDR(pages[2]);
	ev->event.data.vsp_cmd.sub_data.page[3] = ISERIES_HV_ADDR(pages[3]);
	mb();
	if (signal_event(ev) != 0)
		return;

 	while (returnStuff.xDone != 1)
 		udelay(10);
 	if (returnStuff.xRc == 0)
 		memcpy(buffer, pages[0], size);
	kfree(pages[0]);
	kfree(pages[1]);
	kfree(pages[2]);
	kfree(pages[3]);
#endif
}

void mf_setCmdLine(const char *cmdline, int size, u64 side)
{
	struct VspCmdData myVspCmd;
	dma_addr_t dma_addr = 0;
	char *page = dma_alloc_coherent(iSeries_vio_dev, size, &dma_addr,
			GFP_ATOMIC);

	if (page == NULL) {
		printk(KERN_ERR "mf.c: couldn't allocate memory to set command line\n");
		return;
	}

	copy_from_user(page, cmdline, size);

	memset(&myVspCmd, 0, sizeof(myVspCmd));
	myVspCmd.cmd = 31;
	myVspCmd.sub_data.kern.token = dma_addr;
	myVspCmd.sub_data.kern.address_type = HvLpDma_AddressType_TceIndex;
	myVspCmd.sub_data.kern.side = side;
	myVspCmd.sub_data.kern.length = size;
	mb();
	(void)signal_vsp_instruction(&myVspCmd);

	dma_free_coherent(iSeries_vio_dev, size, page, dma_addr);
}

int mf_getCmdLine(char *cmdline, int *size, u64 side)
{
	struct VspCmdData myVspCmd;
	int rc;
	int len = *size;
	dma_addr_t dma_addr;

	dma_addr = dma_map_single(iSeries_vio_dev, cmdline, len,
			DMA_FROM_DEVICE);
	memset(cmdline, 0, len);
	memset(&myVspCmd, 0, sizeof(myVspCmd));
	myVspCmd.cmd = 33;
	myVspCmd.sub_data.kern.token = dma_addr;
	myVspCmd.sub_data.kern.address_type = HvLpDma_AddressType_TceIndex;
	myVspCmd.sub_data.kern.side = side;
	myVspCmd.sub_data.kern.length = len;
	mb();
	rc = signal_vsp_instruction(&myVspCmd);

	if (rc == 0) {
		if (myVspCmd.result_code == 0)
			len = myVspCmd.sub_data.length_out;
#if 0
		else
			memcpy(cmdline, "Bad cmdline", 11);
#endif
	}

	dma_unmap_single(iSeries_vio_dev, dma_addr, *size, DMA_FROM_DEVICE);

	return len;
}


int mf_setVmlinuxChunk(const char *buffer, int size, int offset, u64 side)
{
	struct VspCmdData myVspCmd;
	int rc;
	dma_addr_t dma_addr = 0;
	char *page = dma_alloc_coherent(iSeries_vio_dev, size, &dma_addr,
			GFP_ATOMIC);

	if (page == NULL) {
		printk(KERN_ERR "mf.c: couldn't allocate memory to set vmlinux chunk\n");
		return -ENOMEM;
	}

	copy_from_user(page, buffer, size);
	memset(&myVspCmd, 0, sizeof(myVspCmd));

	myVspCmd.cmd = 30;
	myVspCmd.sub_data.kern.token = dma_addr;
	myVspCmd.sub_data.kern.address_type = HvLpDma_AddressType_TceIndex;
	myVspCmd.sub_data.kern.side = side;
	myVspCmd.sub_data.kern.offset = offset;
	myVspCmd.sub_data.kern.length = size;
	mb();
	rc = signal_vsp_instruction(&myVspCmd);
	if (rc == 0) {
		if (myVspCmd.result_code == 0)
			rc = 0;
		else
			rc = -ENOMEM;
	}

	dma_free_coherent(iSeries_vio_dev, size, page, dma_addr);

	return rc;
}

int mf_getVmlinuxChunk(char *buffer, int *size, int offset, u64 side)
{
	struct VspCmdData myVspCmd;
	int rc;
	int len = *size;
	dma_addr_t dma_addr;

	dma_addr = dma_map_single(iSeries_vio_dev, buffer, len,
			DMA_FROM_DEVICE);
	memset(buffer, 0, len);
	memset(&myVspCmd, 0, sizeof(myVspCmd));
	myVspCmd.cmd = 32;
	myVspCmd.sub_data.kern.token = dma_addr;
	myVspCmd.sub_data.kern.address_type = HvLpDma_AddressType_TceIndex;
	myVspCmd.sub_data.kern.side = side;
	myVspCmd.sub_data.kern.offset = offset;
	myVspCmd.sub_data.kern.length = len;
	mb();
	rc = signal_vsp_instruction(&myVspCmd);
	if (rc == 0) {
		if (myVspCmd.result_code == 0)
			*size = myVspCmd.sub_data.length_out;
		else
			rc = -ENOMEM;
	}

	dma_unmap_single(iSeries_vio_dev, dma_addr, len, DMA_FROM_DEVICE);

	return rc;
}

int mf_setRtcTime(unsigned long time)
{
	struct rtc_time tm;

	to_tm(time, &tm);

	return mf_setRtc(&tm);
}

struct RtcTimeData {
	struct completion com;
	struct CeMsgData xCeMsg;
	int xRc;
};

void getRtcTimeComplete(void * token, struct CeMsgData *ceMsg)
{
	struct RtcTimeData *rtc = (struct RtcTimeData *)token;

	memcpy(&(rtc->xCeMsg), ceMsg, sizeof(rtc->xCeMsg));
	rtc->xRc = 0;
	complete(&rtc->com);
}

static unsigned long lastsec = 1;

int mf_getRtcTime(unsigned long *time)
{
	u32 dataWord1 = *((u32 *)(&xSpCommArea.xBcdTimeAtIplStart));
	u32 dataWord2 = *(((u32 *)&(xSpCommArea.xBcdTimeAtIplStart)) + 1);
	int year = 1970;
	int year1 = (dataWord1 >> 24) & 0x000000FF;
	int year2 = (dataWord1 >> 16) & 0x000000FF;
	int sec = (dataWord1 >> 8) & 0x000000FF;
	int min = dataWord1 & 0x000000FF;
	int hour = (dataWord2 >> 24) & 0x000000FF;
	int day = (dataWord2 >> 8) & 0x000000FF;
	int mon = dataWord2 & 0x000000FF;

	BCD_TO_BIN(sec);
	BCD_TO_BIN(min);
	BCD_TO_BIN(hour);
	BCD_TO_BIN(day);
	BCD_TO_BIN(mon);
	BCD_TO_BIN(year1);
	BCD_TO_BIN(year2);
	year = year1 * 100 + year2;

	*time = mktime(year, mon, day, hour, min, sec);
	*time += (jiffies / HZ);
    
	/*
	 * Now THIS is a nasty hack!
	 * It ensures that the first two calls to mf_getRtcTime get different
	 * answers.  That way the loop in init_time (time.c) will not think
	 * the clock is stuck.
	 */
	if (lastsec) {
		*time -= lastsec;
		--lastsec;
	}
	return 0;
}

int mf_getRtc(struct rtc_time *tm)
{
	struct CeMsgCompleteData ceComplete;
	struct RtcTimeData rtcData;
	int rc;

	memset(&ceComplete, 0, sizeof(ceComplete));
	memset(&rtcData, 0, sizeof(rtcData));
	init_completion(&rtcData.com);
	ceComplete.handler = &getRtcTimeComplete;
	ceComplete.token = (void *)&rtcData;
	rc = signal_ce_msg("\x00\x00\x00\x40\x00\x00\x00\x00\x00\x00\x00\x00",
			&ceComplete);
	if (rc == 0) {
		wait_for_completion(&rtcData.com);

		if (rtcData.xRc == 0) {
			if ((rtcData.xCeMsg.ce_msg[2] == 0xa9) ||
			    (rtcData.xCeMsg.ce_msg[2] == 0xaf)) {
				/* TOD clock is not set */
				tm->tm_sec = 1;
				tm->tm_min = 1;
				tm->tm_hour = 1;
				tm->tm_mday = 10;
				tm->tm_mon = 8;
				tm->tm_year = 71;
				mf_setRtc(tm);
			}
			{
				u32 dataWord1 = *((u32 *)(rtcData.xCeMsg.ce_msg+4));
				u32 dataWord2 = *((u32 *)(rtcData.xCeMsg.ce_msg+8));
				u8 year = (dataWord1 >> 16) & 0x000000FF;
				u8 sec = (dataWord1 >> 8) & 0x000000FF;
				u8 min = dataWord1 & 0x000000FF;
				u8 hour = (dataWord2 >> 24) & 0x000000FF;
				u8 day = (dataWord2 >> 8) & 0x000000FF;
				u8 mon = dataWord2 & 0x000000FF;

				BCD_TO_BIN(sec);
				BCD_TO_BIN(min);
				BCD_TO_BIN(hour);
				BCD_TO_BIN(day);
				BCD_TO_BIN(mon);
				BCD_TO_BIN(year);

				if (year <= 69)
					year += 100;
	    
				tm->tm_sec = sec;
				tm->tm_min = min;
				tm->tm_hour = hour;
				tm->tm_mday = day;
				tm->tm_mon = mon;
				tm->tm_year = year;
			}
		} else {
			rc = rtcData.xRc;
			tm->tm_sec = 0;
			tm->tm_min = 0;
			tm->tm_hour = 0;
			tm->tm_mday = 15;
			tm->tm_mon = 5;
			tm->tm_year = 52;

		}
		tm->tm_wday = 0;
		tm->tm_yday = 0;
		tm->tm_isdst = 0;
	}

	return rc;
}

int mf_setRtc(struct rtc_time * tm)
{
	char ceTime[12] = "\x00\x00\x00\x41\x00\x00\x00\x00\x00\x00\x00\x00";
	u8 day, mon, hour, min, sec, y1, y2;
	unsigned year;
    
	year = 1900 + tm->tm_year;
	y1 = year / 100;
	y2 = year % 100;
    
	sec = tm->tm_sec;
	min = tm->tm_min;
	hour = tm->tm_hour;
	day = tm->tm_mday;
	mon = tm->tm_mon + 1;
	    
	BIN_TO_BCD(sec);
	BIN_TO_BCD(min);
	BIN_TO_BCD(hour);
	BIN_TO_BCD(mon);
	BIN_TO_BCD(day);
	BIN_TO_BCD(y1);
	BIN_TO_BCD(y2);

	ceTime[4] = y1;
	ceTime[5] = y2;
	ceTime[6] = sec;
	ceTime[7] = min;
	ceTime[8] = hour;
	ceTime[10] = day;
	ceTime[11] = mon;
   
	return signal_ce_msg(ceTime, NULL);
}
