/*
 *  linux/drivers/message/fusion/mptbase.c
 *      High performance SCSI + LAN / Fibre Channel device drivers.
 *      This is the Fusion MPT base driver which supports multiple
 *      (SCSI + LAN) specialized protocol drivers.
 *      For use with PCI chip/adapter(s):
 *          LSIFC9xx/LSI409xx Fibre Channel
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Credits:
 *      There are lots of people not mentioned below that deserve credit
 *      and thanks but won't get it here - sorry in advance that you
 *      got overlooked.
 *
 *      This driver would not exist if not for Alan Cox's development
 *      of the linux i2o driver.
 *
 *      A special thanks to Noah Romer (LSI Logic) for tons of work
 *      and tough debugging on the LAN driver, especially early on;-)
 *      And to Roger Hickerson (LSI Logic) for tirelessly supporting
 *      this driver project.
 *
 *      All manner of help from Stephen Shirron (LSI Logic):
 *      low-level FC analysis, debug + various fixes in FCxx firmware,
 *      initial port to alpha platform, various driver code optimizations,
 *      being a faithful sounding board on all sorts of issues & ideas,
 *      etc.
 *
 *      A huge debt of gratitude is owed to David S. Miller (DaveM)
 *      for fixing much of the stupid and broken stuff in the early
 *      driver while porting to sparc64 platform.  THANK YOU!
 *
 *      Special thanks goes to the I2O LAN driver people at the
 *      University of Helsinki, who, unbeknownst to them, provided
 *      the inspiration and initial structure for this driver.
 *
 *      A really huge debt of gratitude is owed to Eddie C. Dost
 *      for gobs of hard work fixing and optimizing LAN code.
 *      THANK YOU!
 *
 *  Copyright (c) 1999-2001 LSI Logic Corporation
 *  Originally By: Steven J. Ralston
 *  (mailto:Steve.Ralston@lsil.com)
 *
 *  $Id: mptbase.c,v 1.53.4.3 2001/09/18 03:54:54 sralston Exp $
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    NO WARRANTY
    THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
    LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
    solely responsible for determining the appropriateness of using and
    distributing the Program and assumes all risks associated with its
    exercise of rights under this Agreement, including but not limited to
    the risks and costs of program errors, damage to or loss of data,
    programs or equipment, and unavailability or interruption of operations.

    DISCLAIMER OF LIABILITY
    NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
    HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#include <linux/config.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <asm/io.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include "mptbase.h"

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#define my_NAME		"Fusion MPT base driver"
#define my_VERSION	MPT_LINUX_VERSION_COMMON
#define MYNAM		"mptbase"

MODULE_AUTHOR(MODULEAUTHOR);
MODULE_DESCRIPTION(my_NAME);
MODULE_LICENSE("GPL");


/*
 *  cmd line parameters
 */
MODULE_PARM(PortIo, "0-1i");
MODULE_PARM_DESC(PortIo, "[0]=Use mmap, 1=Use port io");
MODULE_PARM(HardReset, "0-1i");
MODULE_PARM_DESC(HardReset, "0=Disable HardReset, [1]=Enable HardReset");
static int PortIo = 0;
static int HardReset = 1;

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Public data...
 */
int mpt_lan_index = 0;
int mpt_stm_index = 0;

void *mpt_v_ASCQ_TablePtr = NULL;
const char **mpt_ScsiOpcodesPtr = NULL;
int mpt_ASCQ_TableSz = 0;

#define WHOINIT_UNKNOWN		0xAA

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Private data...
 */
					/* Adapter lookup table */
static MPT_ADAPTER		*mpt_adapters[MPT_MAX_ADAPTERS] = {0};
static MPT_ADAPTER_TRACKER	 MptAdapters;
					/* Callback lookup table */
static MPT_CALLBACK		 MptCallbacks[MPT_MAX_PROTOCOL_DRIVERS];
					/* Protocol driver class lookup table */
static int	 		 MptDriverClass[MPT_MAX_PROTOCOL_DRIVERS];
					/* Event handler lookup table */
static MPT_EVHANDLER		 MptEvHandlers[MPT_MAX_PROTOCOL_DRIVERS];
					/* Reset handler lookup table */
static MPT_RESETHANDLER		 MptResetHandlers[MPT_MAX_PROTOCOL_DRIVERS];

static int	FusionInitCalled = 0;
static int	mpt_base_index = -1;

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Forward protos...
 */
static void	mpt_interrupt(int irq, void *bus_id, struct pt_regs *r);
static int	mpt_base_reply(MPT_ADAPTER *ioc, MPT_FRAME_HDR *req, MPT_FRAME_HDR *reply);

static int 	mpt_do_ioc_recovery(MPT_ADAPTER *ioc, u32 reason);
static int 	mpt_adapter_install(struct pci_dev *pdev);
static void	mpt_detect_929_bound_ports(MPT_ADAPTER *this, struct pci_dev *pdev);
static void	mpt_adapter_disable(MPT_ADAPTER *ioc, int freeup);
static void	mpt_adapter_dispose(MPT_ADAPTER *ioc);

static void	MptDisplayIocCapabilities(MPT_ADAPTER *ioc);
static int	MakeIocReady(MPT_ADAPTER *ioc, int force);
static u32	GetIocState(MPT_ADAPTER *ioc, int cooked);
static int	GetIocFacts(MPT_ADAPTER *ioc);
static int	GetPortFacts(MPT_ADAPTER *ioc, int portnum);
static int	SendIocInit(MPT_ADAPTER *ioc);
static int	SendPortEnable(MPT_ADAPTER *ioc, int portnum);
static int	mpt_fc9x9_reset(MPT_ADAPTER *ioc, int ignore);
static int	KickStart(MPT_ADAPTER *ioc, int ignore);
static int	SendIocReset(MPT_ADAPTER *ioc, u8 reset_type);
static int	PrimeIocFifos(MPT_ADAPTER *ioc);
static int	HandShakeReqAndReply(MPT_ADAPTER *ioc, int reqBytes, u32 *req, int replyBytes, u16 *u16reply, int maxwait);
static int	WaitForDoorbellAck(MPT_ADAPTER *ioc, int howlong);
static int	WaitForDoorbellInt(MPT_ADAPTER *ioc, int howlong);
static int	WaitForDoorbellReply(MPT_ADAPTER *ioc, int howlong);
static int	GetLanConfigPages(MPT_ADAPTER *ioc);
static int	SendEventNotification(MPT_ADAPTER *ioc, u8 EvSwitch);
static int	SendEventAck(MPT_ADAPTER *ioc, EventNotificationReply_t *evnp);

static int	procmpt_create(void);
#ifdef CONFIG_PROC_FS
static int	procmpt_destroy(void);
#endif
static int	procmpt_read_summary(char *page, char **start, off_t off, int count, int *eof, void *data);
static int	procmpt_read_dbg(char *page, char **start, off_t off, int count, int *eof, void *data);
/*static int	procmpt_info(char *buf, char **start, off_t offset, int len);*/

static int	ProcessEventNotification(MPT_ADAPTER *ioc, EventNotificationReply_t *evReply, int *evHandlers);
static void	mpt_fc_log_info(MPT_ADAPTER *ioc, u32 log_info);
static void	mpt_sp_log_info(MPT_ADAPTER *ioc, u32 log_info);

static struct proc_dir_entry	*procmpt_root_dir = NULL;

int		fusion_init(void);
static void	fusion_exit(void);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* 20000207 -sralston
 *  GRRRRR...  IOSpace (port i/o) register access (for the 909) is back!
 * 20000517 -sralston
 *  Let's trying going back to default mmap register access...
 */

static inline u32 CHIPREG_READ32(volatile u32 *a)
{
	if (PortIo)
		return inl((unsigned long)a);
	else
		return readl(a);
}

static inline void CHIPREG_WRITE32(volatile u32 *a, u32 v)
{
	if (PortIo)
		outl(v, (unsigned long)a);
	else
		writel(v, a);
}


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_interrupt - MPT adapter (IOC) specific interrupt handler.
 *	@irq: irq number (not used)
 *	@bus_id: bus identifier cookie == pointer to MPT_ADAPTER structure
 *	@r: pt_regs pointer (not used)
 *
 *	This routine is registered via the request_irq() kernel API call,
 *	and handles all interrupts generated from a specific MPT adapter
 *	(also referred to as a IO Controller or IOC).
 *	This routine must clear the interrupt from the adapter and does
 *	so by reading the reply FIFO.  Multiple replies may be processed
 *	per single call to this routine; up to MPT_MAX_REPLIES_PER_ISR
 *	which is currently set to 32 in mptbase.h.
 *
 *	This routine handles register-level access of the adapter but
 *	dispatches (calls) a protocol-specific callback routine to handle
 *	the protocol-specific details of the MPT request completion.
 */
static void
mpt_interrupt(int irq, void *bus_id, struct pt_regs *r)
{
	MPT_ADAPTER	*ioc;
	MPT_FRAME_HDR	*mf;
	MPT_FRAME_HDR	*mr;
	u32		 pa;
	u32		*m;
	int		 req_idx;
	int		 cb_idx;
	int		 type;
	int		 freeme;
	int		 count = 0;

	ioc = bus_id;

	/*
	 *  Drain the reply FIFO!
	 *
	 * NOTES: I've seen up to 10 replies processed in this loop, so far...
	 * Update: I've seen up to 9182 replies processed in this loop! ??
	 * Update: Limit ourselves to processing max of N replies
	 *	(bottom of loop).
	 */
	while (1) {

		if ((pa = CHIPREG_READ32(&ioc->chip->ReplyFifo)) == 0xFFFFFFFF)
			return;

		cb_idx = 0;
		freeme = 0;

		/*
		 *  Check for non-TURBO reply!
		 */
		if (pa & MPI_ADDRESS_REPLY_A_BIT) {
			dma_addr_t reply_dma_addr;
			u16 ioc_stat;

			/* non-TURBO reply!  Hmmm, something may be up...
			 *  Newest turbo reply mechanism; get address
			 *  via left shift 1 (get rid of MPI_ADDRESS_REPLY_A_BIT)!
			 */
			reply_dma_addr = (pa = (pa << 1));

			/* Map DMA address of reply header to cpu address. */
			m = (u32 *) ((u8 *)ioc->reply_frames +
					(reply_dma_addr - ioc->reply_frames_dma));

			mr = (MPT_FRAME_HDR *) m;
			req_idx = le16_to_cpu(mr->u.frame.hwhdr.msgctxu.fld.req_idx);
			cb_idx = mr->u.frame.hwhdr.msgctxu.fld.cb_idx;
			mf = MPT_INDEX_2_MFPTR(ioc, req_idx);

			dprintk((KERN_INFO MYNAM ": %s: Got non-TURBO reply=%p\n",
					ioc->name, mr));
			DBG_DUMP_REPLY_FRAME(mr)

			/* NEW!  20010301 -sralston
			 *  Check/log IOC log info
			 */
			ioc_stat = le16_to_cpu(mr->u.reply.IOCStatus);
	 		if (ioc_stat & MPI_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE) {
				u32	 log_info = le32_to_cpu(mr->u.reply.IOCLogInfo);
				if ((int)ioc->chip_type <= (int)FC929)
					mpt_fc_log_info(ioc, log_info);
				else
					mpt_sp_log_info(ioc, log_info);
			}
		} else {
			/*
			 *  Process turbo (context) reply...
			 */
			dirqprintk((KERN_INFO MYNAM ": %s: Got TURBO reply(=%08x)\n", ioc->name, pa));
			type = (pa >> MPI_CONTEXT_REPLY_TYPE_SHIFT);
			if (type == MPI_CONTEXT_REPLY_TYPE_SCSI_TARGET) {
				cb_idx = mpt_stm_index;
				mf = NULL;
				mr = (MPT_FRAME_HDR *) CAST_U32_TO_PTR(pa);
			} else if (type == MPI_CONTEXT_REPLY_TYPE_LAN) {
				cb_idx = mpt_lan_index;
				/*
				 * BUG FIX!  20001218 -sralston
				 *  Blind set of mf to NULL here was fatal
				 *  after lan_reply says "freeme"
				 *  Fix sort of combined with an optimization here;
				 *  added explicit check for case where lan_reply
				 *  was just returning 1 and doing nothing else.
				 *  For this case skip the callback, but set up
				 *  proper mf value first here:-)
				 */
				if ((pa & 0x58000000) == 0x58000000) {
					req_idx = pa & 0x0000FFFF;
					mf = MPT_INDEX_2_MFPTR(ioc, req_idx);
					freeme = 1;
					/*
					 *  IMPORTANT!  Invalidate the callback!
					 */
					cb_idx = 0;
				} else {
					mf = NULL;
				}
				mr = (MPT_FRAME_HDR *) CAST_U32_TO_PTR(pa);
			} else {
				req_idx = pa & 0x0000FFFF;
				cb_idx = (pa & 0x00FF0000) >> 16;
				mf = MPT_INDEX_2_MFPTR(ioc, req_idx);
				mr = NULL;
			}
			pa = 0;					/* No reply flush! */
		}

		/*  Check for (valid) IO callback!  */
		if (cb_idx) {
			/*  Do the callback!  */
			freeme = (*(MptCallbacks[cb_idx]))(ioc, mf, mr);
		}

		if (pa) {
			/*  Flush (non-TURBO) reply with a WRITE!  */
			CHIPREG_WRITE32(&ioc->chip->ReplyFifo, pa);
		}

		if (freeme) {
			unsigned long flags;

			/*  Put Request back on FreeQ!  */
			spin_lock_irqsave(&ioc->FreeQlock, flags);
			Q_ADD_TAIL(&ioc->FreeQ, &mf->u.frame.linkage, MPT_FRAME_HDR);
			spin_unlock_irqrestore(&ioc->FreeQlock, flags);
		}

		count++;
		dirqprintk((KERN_INFO MYNAM ": %s: ISR processed frame #%d\n", ioc->name, count));
		mb();

		if (count >= MPT_MAX_REPLIES_PER_ISR) {
			dirqprintk((KERN_INFO MYNAM ": %s: ISR processed %d replies.",
					ioc->name, count));
			dirqprintk((" Giving this ISR a break!\n"));
			return;
		}

	}	/* drain reply FIFO */
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mpt_base_reply - MPT base driver's callback routine; all base driver
 *	"internal" request/reply processing is routed here.
 *	Currently used for EventNotification and EventAck handling.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@mf: Pointer to original MPT request frame
 *	@reply: Pointer to MPT reply frame (NULL if TurboReply)
 *
 *	Returns 1 indicating original alloc'd request frame ptr
 *	should be freed, or 0 if it shouldn't.
 */
static int
mpt_base_reply(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *reply)
{
	int freereq = 1;
	u8 func;

	dprintk((KERN_INFO MYNAM ": %s: mpt_base_reply() called\n", ioc->name));

	if ((mf == NULL) ||
	    (mf >= MPT_INDEX_2_MFPTR(ioc, ioc->req_depth))) {
		printk(KERN_ERR MYNAM ": %s: ERROR - NULL or BAD request frame ptr! (=%p)\n",
				ioc->name, mf);
		return 1;
	}

	if (reply == NULL) {
		dprintk((KERN_ERR MYNAM ": %s: ERROR - Unexpected NULL Event (turbo?) reply!\n",
				ioc->name));
		return 1;
	}

	if (!(reply->u.hdr.MsgFlags & MPI_MSGFLAGS_CONTINUATION_REPLY)) {
		dmfprintk((KERN_INFO MYNAM ": Original request frame (@%p) header\n", mf));
		DBG_DUMP_REQUEST_FRAME_HDR(mf)
	}

	func = reply->u.hdr.Function;
	dprintk((KERN_INFO MYNAM ": %s: mpt_base_reply, Function=%02Xh\n",
			ioc->name, func));

	if (func == MPI_FUNCTION_EVENT_NOTIFICATION) {
		EventNotificationReply_t *pEvReply = (EventNotificationReply_t *) reply;
		int evHandlers = 0;
		int results;

		results = ProcessEventNotification(ioc, pEvReply, &evHandlers);
		if (results != evHandlers) {
			/* CHECKME! Any special handling needed here? */
			dprintk((KERN_WARNING MYNAM ": %s: Hmmm... Called %d event handlers, sum results = %d\n",
					ioc->name, evHandlers, results));
		}

		/*
		 *  Hmmm...  It seems that EventNotificationReply is an exception
		 *  to the rule of one reply per request.
		 */
		if (pEvReply->MsgFlags & MPI_MSGFLAGS_CONTINUATION_REPLY)
			freereq = 0;
#ifdef CONFIG_PROC_FS
//		LogEvent(ioc, pEvReply);
#endif
	} else if (func == MPI_FUNCTION_EVENT_ACK) {
		dprintk((KERN_INFO MYNAM ": %s: mpt_base_reply, EventAck reply received\n",
				ioc->name));
	} else {
		printk(KERN_ERR MYNAM ": %s: ERROR - Unexpected msg function (=%02Xh) reply received!\n",
				ioc->name, func);
	}

	/*
	 *  Conditionally tell caller to free the original
	 *  EventNotification/EventAck/unexpected request frame!
	 */
	return freereq;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_register - Register protocol-specific main callback handler.
 *	@cbfunc: callback function pointer
 *	@dclass: Protocol driver's class (%MPT_DRIVER_CLASS enum value)
 *
 *	This routine is called by a protocol-specific driver (SCSI host,
 *	LAN, SCSI target) to register it's reply callback routine.  Each
 *	protocol-specific driver must do this before it will be able to
 *	use any IOC resources, such as obtaining request frames.
 *
 *	NOTES: The SCSI protocol driver currently calls this routine twice
 *	in order to register separate callbacks; one for "normal" SCSI IO
 *	and another for MptScsiTaskMgmt requests.
 *
 *	Returns a positive integer valued "handle" in the
 *	range (and S.O.D. order) {7,6,...,1} if successful.
 *	Any non-positive return value (including zero!) should be considered
 *	an error by the caller.
 */
int
mpt_register(MPT_CALLBACK cbfunc, MPT_DRIVER_CLASS dclass)
{
	int r = -1;
	int i;

#ifndef MODULE
	/*
	 *  Handle possibility of the mptscsih_detect() routine getting
	 *  called *before* fusion_init!
	 */
	if (!FusionInitCalled) {
		dprintk((KERN_INFO MYNAM ": Hmmm, calling fusion_init from mpt_register!\n"));
		/*
		 *  NOTE! We'll get recursion here, as fusion_init()
		 *  calls mpt_register()!
		 */
		fusion_init();
		FusionInitCalled++;
	}
#endif

	/*
	 *  Search for empty callback slot in this order: {7,6,...,1}
	 *  (slot/handle 0 is reserved!)
	 */
	for (i = MPT_MAX_PROTOCOL_DRIVERS-1; i; i--) {
		if (MptCallbacks[i] == NULL) {
			MptCallbacks[i] = cbfunc;
			MptDriverClass[i] = dclass;
			MptEvHandlers[i] = NULL;
			r = i;
			if (cbfunc != mpt_base_reply) {
				MOD_INC_USE_COUNT;
			}
			break;
		}
	}

	return r;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_deregister - Deregister a protocol drivers resources.
 *	@cb_idx: previously registered callback handle
 *
 *	Each protocol-specific driver should call this routine when it's
 *	module is unloaded.
 */
void
mpt_deregister(int cb_idx)
{
	if (cb_idx && (cb_idx < MPT_MAX_PROTOCOL_DRIVERS)) {
		MptCallbacks[cb_idx] = NULL;
		MptDriverClass[cb_idx] = MPTUNKNOWN_DRIVER;
		MptEvHandlers[cb_idx] = NULL;
		if (cb_idx != mpt_base_index) {
			MOD_DEC_USE_COUNT;
		}
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_event_register - Register protocol-specific event callback
 *	handler.
 *	@cb_idx: previously registered (via mpt_register) callback handle
 *	@ev_cbfunc: callback function
 *
 *	This routine can be called by one or more protocol-specific drivers
 *	if/when they choose to be notified of MPT events.
 *
 *	Returns 0 for success.
 */
int
mpt_event_register(int cb_idx, MPT_EVHANDLER ev_cbfunc)
{
	if (cb_idx < 1 || cb_idx >= MPT_MAX_PROTOCOL_DRIVERS)
		return -1;

	MptEvHandlers[cb_idx] = ev_cbfunc;
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_event_deregister - Deregister protocol-specific event callback
 *	handler.
 *	@cb_idx: previously registered callback handle
 *
 *	Each protocol-specific driver should call this routine
 *	when it does not (or can no longer) handle events,
 *	or when it's module is unloaded.
 */
void
mpt_event_deregister(int cb_idx)
{
	if (cb_idx < 1 || cb_idx >= MPT_MAX_PROTOCOL_DRIVERS)
		return;

	MptEvHandlers[cb_idx] = NULL;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_reset_register - Register protocol-specific IOC reset handler.
 *	@cb_idx: previously registered (via mpt_register) callback handle
 *	@reset_func: reset function
 *
 *	This routine can be called by one or more protocol-specific drivers
 *	if/when they choose to be notified of IOC resets.
 *
 *	Returns 0 for success.
 */
int
mpt_reset_register(int cb_idx, MPT_RESETHANDLER reset_func)
{
	if (cb_idx < 1 || cb_idx >= MPT_MAX_PROTOCOL_DRIVERS)
		return -1;

	MptResetHandlers[cb_idx] = reset_func;
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_reset_deregister - Deregister protocol-specific IOC reset handler.
 *	@cb_idx: previously registered callback handle
 *
 *	Each protocol-specific driver should call this routine
 *	when it does not (or can no longer) handle IOC reset handling,
 *	or when it's module is unloaded.
 */
void
mpt_reset_deregister(int cb_idx)
{
	if (cb_idx < 1 || cb_idx >= MPT_MAX_PROTOCOL_DRIVERS)
		return;

	MptResetHandlers[cb_idx] = NULL;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_get_msg_frame - Obtain a MPT request frame from the pool (of 1024)
 *	allocated per MPT adapter.
 *	@handle: Handle of registered MPT protocol driver
 *	@iocid: IOC unique identifier (integer)
 *
 *	Returns pointer to a MPT request frame or %NULL if none are available.
 */
MPT_FRAME_HDR*
mpt_get_msg_frame(int handle, int iocid)
{
	MPT_FRAME_HDR *mf = NULL;
	MPT_ADAPTER *iocp;
	unsigned long flags;

	/* validate handle and ioc identifier */
	iocp = mpt_adapters[iocid];
	spin_lock_irqsave(&iocp->FreeQlock, flags);
	if (! Q_IS_EMPTY(&iocp->FreeQ)) {
		int req_offset;

		mf = iocp->FreeQ.head;
		Q_DEL_ITEM(&mf->u.frame.linkage);
		mf->u.frame.hwhdr.msgctxu.fld.cb_idx = handle;	/* byte */
		req_offset = (u8 *)mf - (u8 *)iocp->req_frames;
								/* u16! */
		mf->u.frame.hwhdr.msgctxu.fld.req_idx =
				cpu_to_le16(req_offset / iocp->req_sz);
		mf->u.frame.hwhdr.msgctxu.fld.rsvd = 0;
	}
	spin_unlock_irqrestore(&iocp->FreeQlock, flags);
	dmfprintk((KERN_INFO MYNAM ": %s: mpt_get_msg_frame(%d,%d), got mf=%p\n",
			iocp->name, handle, iocid, mf));
	return mf;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_put_msg_frame - Send a protocol specific MPT request frame
 *	to a IOC.
 *	@handle: Handle of registered MPT protocol driver
 *	@iocid: IOC unique identifier (integer)
 *	@mf: Pointer to MPT request frame
 *
 *	This routine posts a MPT request frame to the request post FIFO of a
 *	specific MPT adapter.
 */
void
mpt_put_msg_frame(int handle, int iocid, MPT_FRAME_HDR *mf)
{
	MPT_ADAPTER *iocp;

	iocp = mpt_adapters[iocid];
	if (iocp != NULL) {
		dma_addr_t mf_dma_addr;
		int req_offset;

		/* ensure values are reset properly! */
		mf->u.frame.hwhdr.msgctxu.fld.cb_idx = handle;		/* byte */
		req_offset = (u8 *)mf - (u8 *)iocp->req_frames;
									/* u16! */
		mf->u.frame.hwhdr.msgctxu.fld.req_idx = cpu_to_le16(req_offset / iocp->req_sz);
		mf->u.frame.hwhdr.msgctxu.fld.rsvd = 0;

#ifdef MPT_DEBUG_MSG_FRAME
		{
			u32	*m = mf->u.frame.hwhdr.__hdr;
			int	 i, n;

			printk(KERN_INFO MYNAM ": %s: About to Put msg frame @ %p:\n" KERN_INFO " ",
					iocp->name, m);
			n = iocp->req_sz/4 - 1;
			while (m[n] == 0)
				n--;
			for (i=0; i<=n; i++) {
				if (i && ((i%8)==0))
					printk("\n" KERN_INFO " ");
				printk(" %08x", le32_to_cpu(m[i]));
			}
			printk("\n");
		}
#endif

		mf_dma_addr = iocp->req_frames_dma + req_offset;
		CHIPREG_WRITE32(&iocp->chip->RequestFifo, mf_dma_addr);
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_free_msg_frame - Place MPT request frame back on FreeQ.
 *	@handle: Handle of registered MPT protocol driver
 *	@iocid: IOC unique identifier (integer)
 *	@mf: Pointer to MPT request frame
 *
 *	This routine places a MPT request frame back on the MPT adapter's
 *	FreeQ.
 */
void
mpt_free_msg_frame(int handle, int iocid, MPT_FRAME_HDR *mf)
{
	MPT_ADAPTER *iocp;
	unsigned long flags;

	iocp = mpt_adapters[iocid];
	if (iocp != NULL) {
		/*  Put Request back on FreeQ!  */
		spin_lock_irqsave(&iocp->FreeQlock, flags);
		Q_ADD_TAIL(&iocp->FreeQ, &mf->u.frame.linkage, MPT_FRAME_HDR);
		spin_unlock_irqrestore(&iocp->FreeQlock, flags);
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_send_handshake_request - Send MPT request via doorbell
 *	handshake method.
 *	@handle: Handle of registered MPT protocol driver
 *	@iocid: IOC unique identifier (integer)
 *	@reqBytes: Size of the request in bytes
 *	@req: Pointer to MPT request frame
 *
 *	This routine is used exclusively by mptscsih to send MptScsiTaskMgmt
 *	requests since they are required to be sent via doorbell handshake.
 *
 *	NOTE: It is the callers responsibility to byte-swap fields in the
 *	request which are greater than 1 byte in size.
 *
 *	Returns 0 for success, non-zero for failure.
 */
int
mpt_send_handshake_request(int handle, int iocid, int reqBytes, u32 *req)
{
	MPT_ADAPTER	*iocp;
	int		 r = 0;

	iocp = mpt_adapters[iocid];
	if (iocp != NULL) {
		u8		*req_as_bytes;
		u32		 ioc_raw_state;
		int		 i;

		/* YIKES!  We already know something is amiss.
		 * Do upfront check on IOC state.
		 */
		ioc_raw_state = GetIocState(iocp, 0);
		if ((ioc_raw_state & MPI_DOORBELL_ACTIVE) ||
		    ((ioc_raw_state & MPI_IOC_STATE_MASK) != MPI_IOC_STATE_OPERATIONAL)) {
			printk(KERN_WARNING MYNAM ": %s: Bad IOC state (%08x) WARNING!\n",
					iocp->name, ioc_raw_state);
			if ((r = mpt_do_ioc_recovery(iocp, MPT_HOSTEVENT_IOC_RECOVER)) != 0) {
				printk(KERN_WARNING MYNAM ": WARNING - (%d) Cannot recover %s\n",
						r, iocp->name);
				return r;
			}
		}

		/*
		 * Emulate what mpt_put_msg_frame() does /wrt to sanity
		 * setting cb_idx/req_idx.  But ONLY if this request
		 * is in proper (pre-alloc'd) request buffer range...
		 */
		i = MFPTR_2_MPT_INDEX(iocp,(MPT_FRAME_HDR*)req);
		if (reqBytes >= 12 && i >= 0 && i < iocp->req_depth) {
			MPT_FRAME_HDR *mf = (MPT_FRAME_HDR*)req;
			mf->u.frame.hwhdr.msgctxu.fld.req_idx = cpu_to_le16(i);
			mf->u.frame.hwhdr.msgctxu.fld.cb_idx = handle;
		}

		/* Make sure there are no doorbells */
		CHIPREG_WRITE32(&iocp->chip->IntStatus, 0);

		CHIPREG_WRITE32(&iocp->chip->Doorbell,
				((MPI_FUNCTION_HANDSHAKE<<MPI_DOORBELL_FUNCTION_SHIFT) |
				 ((reqBytes/4)<<MPI_DOORBELL_ADD_DWORDS_SHIFT)));

		/* Wait for IOC doorbell int */
		if ((i = WaitForDoorbellInt(iocp, 2)) < 0) {
			return i;
		}

		dhsprintk((KERN_INFO MYNAM ": %s: mpt_send_handshake_request start, WaitCnt=%d\n",
				iocp->name, i));

		CHIPREG_WRITE32(&iocp->chip->IntStatus, 0);

		if ((r = WaitForDoorbellAck(iocp, 1)) < 0) {
			return -2;
		}

		/* Send request via doorbell handshake */
		req_as_bytes = (u8 *) req;
		for (i = 0; i < reqBytes/4; i++) {
			u32 word;

			word = ((req_as_bytes[(i*4) + 0] <<  0) |
				(req_as_bytes[(i*4) + 1] <<  8) |
				(req_as_bytes[(i*4) + 2] << 16) |
				(req_as_bytes[(i*4) + 3] << 24));
			CHIPREG_WRITE32(&iocp->chip->Doorbell, word);
			if ((r = WaitForDoorbellAck(iocp, 1)) < 0) {
				r = -3;
				break;
			}
		}

		if ((r = WaitForDoorbellInt(iocp, 2)) >= 0)
			r = 0;
		else
			r = -4;

		/* Make sure there are no doorbells */
		CHIPREG_WRITE32(&iocp->chip->IntStatus, 0);
	}

	return r;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_adapter_find_first - Find first MPT adapter pointer.
 *
 *	Returns first MPT adapter pointer or %NULL if no MPT adapters
 *	are present.
 */
MPT_ADAPTER *
mpt_adapter_find_first(void)
{
	MPT_ADAPTER *this = NULL;

	if (! Q_IS_EMPTY(&MptAdapters))
		this = MptAdapters.head;

	return this;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 * 	mpt_adapter_find_next - Find next MPT adapter pointer.
 * 	@prev: Pointer to previous MPT adapter
 *
 *	Returns next MPT adapter pointer or %NULL if there are no more.
 */
MPT_ADAPTER *
mpt_adapter_find_next(MPT_ADAPTER *prev)
{
	MPT_ADAPTER *next = NULL;

	if (prev && (prev->forw != (MPT_ADAPTER*)&MptAdapters.head))
		next = prev->forw;

	return next;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_pci_scan - Scan PCI devices for MPT adapters.
 *
 *	Returns count of MPT adapters found, keying off of PCI vendor and
 *	device_id's.
 */
int __init
mpt_pci_scan(void)
{
	struct pci_dev *pdev;
	struct pci_dev *pdev2;
	int found = 0;
	int count = 0;
	int r;

	dprintk((KERN_INFO MYNAM ": Checking for MPT adapters...\n"));

	/*
	 *  NOTE: The 929 (I believe) will appear as 2 separate PCI devices,
	 *  one for each channel.
	 */
	pci_for_each_dev(pdev) {
		pdev2 = NULL;
		if (pdev->vendor != 0x1000)
			continue;

		if ((pdev->device != MPI_MANUFACTPAGE_DEVICEID_FC909) &&
		    (pdev->device != MPI_MANUFACTPAGE_DEVICEID_FC929) &&
		    (pdev->device != MPI_MANUFACTPAGE_DEVICEID_FC919) &&
#if 0
		    /* FIXME! C103x family */
		    (pdev->device != MPI_MANUFACTPAGE_DEVID_53C1030) &&
		    (pdev->device != MPI_MANUFACTPAGE_DEVID_53C1030_ZC) &&
		    (pdev->device != MPI_MANUFACTPAGE_DEVID_53C1035) &&
#endif
		    1) {
			dprintk((KERN_INFO MYNAM ": Skipping LSI device=%04xh\n", pdev->device));
			continue;
		}

		/* GRRRRR
		 * 929 dual function devices may be presented in Func 1,0 order,
		 * but we'd really really rather have them in Func 0,1 order.
		 * Do some kind of look ahead here...
		 */
		if (pdev->devfn & 1) {
			pdev2 = pci_peek_next_dev(pdev);
			if (pdev2 && (pdev2->vendor == 0x1000) &&
			    (PCI_SLOT(pdev2->devfn) == PCI_SLOT(pdev->devfn)) &&
			    (pdev2->device == MPI_MANUFACTPAGE_DEVICEID_FC929) &&
			    (pdev2->bus->number == pdev->bus->number) &&
			    !(pdev2->devfn & 1)) {
				dprintk((KERN_INFO MYNAM ": MPT adapter found: PCI bus/dfn=%02x/%02xh, class=%08x, id=%xh\n",
			 		pdev2->bus->number, pdev2->devfn, pdev2->class, pdev2->device));
				found++;
				if ((r = mpt_adapter_install(pdev2)) == 0)
					count++;
			} else {
				pdev2 = NULL;
			}
		}

		dprintk((KERN_INFO MYNAM ": MPT adapter found: PCI bus/dfn=%02x/%02xh, class=%08x, id=%xh\n",
			 pdev->bus->number, pdev->devfn, pdev->class, pdev->device));
		found++;
		if ((r = mpt_adapter_install(pdev)) == 0)
			count++;

		if (pdev2)
			pdev = pdev2;
	}

	printk(KERN_INFO MYNAM ": %d MPT adapter%s found, %d installed.\n",
		 found, (found==1) ? "" : "s", count);

	if (!found || !count) {
		fusion_exit();
		return -ENODEV;
	}

#ifdef CONFIG_PROC_FS
	if (procmpt_create() != 0)
		printk(KERN_WARNING MYNAM ": WARNING! - %s creation failed!\n",
				MPT_PROCFS_MPTBASEDIR);
#endif

	return count;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_verify_adapter - Given a unique IOC identifier, set pointer to
 *	the associated MPT adapter structure.
 *	@iocid: IOC unique identifier (integer)
 *	@iocpp: Pointer to pointer to IOC adapter
 *
 *	Returns iocid and sets iocpp.
 */
int
mpt_verify_adapter(int iocid, MPT_ADAPTER **iocpp)
{
	MPT_ADAPTER *p;

	*iocpp = NULL;
	if (iocid >= MPT_MAX_ADAPTERS)
		return -1;

	p = mpt_adapters[iocid];
	if (p == NULL)
		return -1;

	*iocpp = p;
	return iocid;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_adapter_install - Install a PCI intelligent MPT adapter.
 *	@pdev: Pointer to pci_dev structure
 *
 *	This routine performs all the steps necessary to bring the IOC of
 *	a MPT adapter to a OPERATIONAL state.  This includes registering
 *	memory regions, registering the interrupt, and allocating request
 *	and reply memory pools.
 *
 *	This routine also pre-fetches the LAN MAC address of a Fibre Channel
 *	MPT adapter.
 *
 *	Returns 0 for success, non-zero for failure.
 *
 *	TODO: Add support for polled controllers
 */
static int __init
mpt_adapter_install(struct pci_dev *pdev)
{
	MPT_ADAPTER	*ioc;
	char		*myname;
	u8		*mem;
	unsigned long	 mem_phys;
	unsigned long	 port;
	u32		 msize;
	u32		 psize;
	int		 i;
	int		 r = -ENODEV;
	int		 len;

	ioc = kmalloc(sizeof(MPT_ADAPTER), GFP_KERNEL);
	if (ioc == NULL) {
		printk(KERN_ERR MYNAM ": ERROR - Insufficient memory to add adapter!\n");
		return -ENOMEM;
	}
	memset(ioc, 0, sizeof(*ioc));
	ioc->req_sz = MPT_REQ_SIZE;			/* avoid div by zero! */
	ioc->alloc_total = sizeof(MPT_ADAPTER);

	ioc->pcidev = pdev;

	/* Find lookup slot. */
	for (i=0; i < MPT_MAX_ADAPTERS; i++) {
		if (mpt_adapters[i] == NULL) {
			ioc->id = i;		/* Assign adapter unique id (lookup) */
			break;
		}
	}
	if (i == MPT_MAX_ADAPTERS) {
		printk(KERN_ERR MYNAM ": ERROR - mpt_adapters[%d] table overflow!\n", i);
		kfree(ioc);
		return -ENFILE;
	}

	mem_phys = msize = 0;
	port = psize = 0;
	for (i=0; i < DEVICE_COUNT_RESOURCE; i++) {
		if (pdev->PCI_BASEADDR_FLAGS(i) & PCI_BASE_ADDRESS_SPACE_IO) {
			/* Get I/O space! */
			port = pdev->PCI_BASEADDR_START(i);
			psize = PCI_BASEADDR_SIZE(pdev,i);
		} else {
			/* Get memmap */
			mem_phys = pdev->PCI_BASEADDR_START(i);
			msize = PCI_BASEADDR_SIZE(pdev,i);
			break;
		}
	}
	ioc->mem_size = msize;

	if (i == DEVICE_COUNT_RESOURCE) {
		printk(KERN_ERR MYNAM ": ERROR - MPT adapter has no memory regions defined!\n");
		kfree(ioc);
		return -EINVAL;
	}

	dprintk((KERN_INFO MYNAM ": MPT adapter @ %lx, msize=%dd bytes\n", mem_phys, msize));
	dprintk((KERN_INFO MYNAM ": (port i/o @ %lx, psize=%dd bytes)\n", port, psize));
	dprintk((KERN_INFO MYNAM ": Using %s register access method\n", PortIo ? "PortIo" : "MemMap"));

	mem = NULL;
	if (! PortIo) {
		/* Get logical ptr for PciMem0 space */
		/*mem = ioremap(mem_phys, msize);*/
		mem = ioremap(mem_phys, 0x100);
		if (mem == NULL) {
			printk(KERN_ERR MYNAM ": ERROR - Unable to map adapter memory!\n");
			kfree(ioc);
			return -EINVAL;
		}
		ioc->memmap = mem;
	}
	dprintk((KERN_INFO MYNAM ": mem = %p, mem_phys = %lx\n", mem, mem_phys));

	if (PortIo) {
		u8 *pmem = (u8*)port;
		ioc->mem_phys = port;
		ioc->chip = (SYSIF_REGS*)pmem;
	} else {
		ioc->mem_phys = mem_phys;
		ioc->chip = (SYSIF_REGS*)mem;
	}

	ioc->chip_type = FCUNK;
	if (pdev->device == MPI_MANUFACTPAGE_DEVICEID_FC909) {
		ioc->chip_type = FC909;
		ioc->prod_name = "LSIFC909";
	}
	else if (pdev->device == MPI_MANUFACTPAGE_DEVICEID_FC929) {
		ioc->chip_type = FC929;
		ioc->prod_name = "LSIFC929";
	}
	else if (pdev->device == MPI_MANUFACTPAGE_DEVICEID_FC919) {
		ioc->chip_type = FC919;
		ioc->prod_name = "LSIFC919";
	}
#if 0
	else if (pdev->device == MPI_MANUFACTPAGE_DEVICEID_53C1030) {
		ioc->chip_type = C1030;
		ioc->prod_name = "LSI53C1030";
	}
#endif

	myname = "iocN";
	len = strlen(myname);
	memcpy(ioc->name, myname, len+1);
	ioc->name[len-1] = '0' + ioc->id;

	Q_INIT(&ioc->FreeQ, MPT_FRAME_HDR);
	spin_lock_init(&ioc->FreeQlock);

	/* Disable all! */
	CHIPREG_WRITE32(&ioc->chip->IntMask, 0xFFFFFFFF);
	ioc->active = 0;
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);

	ioc->pci_irq = -1;
	if (pdev->irq) {
		r = request_irq(pdev->irq, mpt_interrupt, SA_SHIRQ, ioc->name, ioc);

		if (r < 0) {
			printk(KERN_ERR MYNAM ": %s: ERROR - Unable to allocate interrupt %d!\n",
					ioc->name, pdev->irq);
			iounmap(mem);
			kfree(ioc);
			return -EBUSY;
		}

		ioc->pci_irq = pdev->irq;

		pci_set_master(pdev);			/* ?? */

		dprintk((KERN_INFO MYNAM ": %s installed at interrupt %d\n", ioc->name, pdev->irq));
	}

	/* tack onto tail of our MPT adapter list */
	Q_ADD_TAIL(&MptAdapters, ioc, MPT_ADAPTER);

	/* Set lookup ptr. */
	mpt_adapters[ioc->id] = ioc;

	/* NEW!  20010220 -sralston
	 * Check for "929 bound ports" to reduce redundant resets.
	 */
	if (ioc->chip_type == FC929)
		mpt_detect_929_bound_ports(ioc, pdev);

	if ((r = mpt_do_ioc_recovery(ioc, MPT_HOSTEVENT_IOC_BRINGUP)) != 0) {
		printk(KERN_WARNING MYNAM ": WARNING - %s did not initialize properly! (%d)\n",
				ioc->name, r);
	}

	return r;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_do_ioc_recovery - Initialize or recover MPT adapter.
 *	@ioc: Pointer to MPT adapter structure
 *	@reason: Event word / reason
 *
 *	This routine performs all the steps necessary to bring the IOC
 *	to a OPERATIONAL state.
 *
 *	This routine also pre-fetches the LAN MAC address of a Fibre Channel
 *	MPT adapter.
 *
 *	Returns 0 for success.
 */
static int
mpt_do_ioc_recovery(MPT_ADAPTER *ioc, u32 reason)
{
	int	 hard_reset_done = 0;
	int	 alt_ioc_ready = 0;
	int	 hard;
	int	 r;
	int	 i;
	int	 handlers;

	printk(KERN_INFO MYNAM ": Initiating %s %s\n",
			ioc->name, reason==MPT_HOSTEVENT_IOC_BRINGUP ? "bringup" : "recovery");

	/* Disable reply interrupts */
	CHIPREG_WRITE32(&ioc->chip->IntMask, 0xFFFFFFFF);
	ioc->active = 0;
	/* NOTE: Access to IOC's request FreeQ is now blocked! */

// FIXME? Cleanup all IOC requests here! (or below?)
// But watch out for event associated request?

	hard = HardReset;
	if (ioc->alt_ioc && (reason == MPT_HOSTEVENT_IOC_BRINGUP))
		hard = 0;

	if ((hard_reset_done = MakeIocReady(ioc, hard)) < 0) {
		printk(KERN_WARNING MYNAM ": %s NOT READY WARNING!\n",
				ioc->name);
		return -1;
	}

// NEW!
#if 0						// Kiss-of-death!?!
	if (ioc->alt_ioc) {
// Grrr... Hold off any alt-IOC interrupts (and events) while
// handshaking to <this> IOC, needed because?
		/* Disable alt-IOC's reply interrupts for a bit ... */
		alt_ioc_intmask = CHIPREG_READ32(&ioc->alt_ioc->chip->IntMask);
		CHIPREG_WRITE32(&ioc->alt_ioc->chip->IntMask, 0xFFFFFFFF);
		ioc->alt_ioc->active = 0;
		/* NOTE: Access to alt-IOC's request FreeQ is now blocked! */
	}
#endif

	if (hard_reset_done && ioc->alt_ioc) {
		if ((r = MakeIocReady(ioc->alt_ioc, 0)) == 0)
			alt_ioc_ready = 1;
		else
			printk(KERN_WARNING MYNAM ": alt-%s: (%d) Not ready WARNING!\n",
					ioc->alt_ioc->name, r);
	}

	if (reason == MPT_HOSTEVENT_IOC_BRINGUP) {
		/* Get IOC facts! */
		if ((r = GetIocFacts(ioc)) != 0)
			return -2;
		MptDisplayIocCapabilities(ioc);
	}

	/*
	 * Call each currently registered protocol IOC reset handler
	 * with pre-reset indication.
	 * NOTE: If we're doing _IOC_BRINGUP, there can be no
	 * MptResetHandlers[] registered yet.
	 */
	if (hard_reset_done) {
		r = handlers = 0;
		for (i=MPT_MAX_PROTOCOL_DRIVERS-1; i; i--) {
			if (MptResetHandlers[i]) {
				dprintk((KERN_INFO MYNAM ": %s: Calling IOC pre_reset handler #%d\n",
						ioc->name, i));
				r += (*(MptResetHandlers[i]))(ioc, MPT_IOC_PRE_RESET);
				handlers++;

				if (alt_ioc_ready) {
					dprintk((KERN_INFO MYNAM ": %s: Calling alt-IOC pre_reset handler #%d\n",
							ioc->alt_ioc->name, i));
					r += (*(MptResetHandlers[i]))(ioc->alt_ioc, MPT_IOC_PRE_RESET);
					handlers++;
				}
			}
		}
		/* FIXME?  Examine results here? */
	}

	// May need to check/upload firmware & data here!

	if ((r = SendIocInit(ioc)) != 0)
		return -3;
// NEW!
	if (alt_ioc_ready) {
		if ((r = SendIocInit(ioc->alt_ioc)) != 0) {
			alt_ioc_ready = 0;
			printk(KERN_WARNING MYNAM ": alt-%s: (%d) init failure WARNING!\n",
					ioc->alt_ioc->name, r);
		}
	}

	/*
	 * Call each currently registered protocol IOC reset handler
	 * with post-reset indication.
	 * NOTE: If we're doing _IOC_BRINGUP, there can be no
	 * MptResetHandlers[] registered yet.
	 */
	if (hard_reset_done) {
		r = handlers = 0;
		for (i=MPT_MAX_PROTOCOL_DRIVERS-1; i; i--) {
			if (MptResetHandlers[i]) {
				dprintk((KERN_INFO MYNAM ": %s: Calling IOC post_reset handler #%d\n",
						ioc->name, i));
				r += (*(MptResetHandlers[i]))(ioc, MPT_IOC_POST_RESET);
				handlers++;

				if (alt_ioc_ready) {
					dprintk((KERN_INFO MYNAM ": %s: Calling alt-IOC post_reset handler #%d\n",
							ioc->alt_ioc->name, i));
					r += (*(MptResetHandlers[i]))(ioc->alt_ioc, MPT_IOC_POST_RESET);
					handlers++;
				}
			}
		}
		/* FIXME?  Examine results here? */
	}

	/*
	 * Prime reply & request queues!
	 * (mucho alloc's)
	 */
	if ((r = PrimeIocFifos(ioc)) != 0)
		return -4;
// NEW!
	if (alt_ioc_ready && ((r = PrimeIocFifos(ioc->alt_ioc)) != 0)) {
		printk(KERN_WARNING MYNAM ": alt-%s: (%d) FIFO mgmt alloc WARNING!\n",
				ioc->alt_ioc->name, r);
	}

// FIXME! Cleanup all IOC (and alt-IOC?) requests here!

	if ((ioc->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_LAN) &&
	    (ioc->lan_cnfg_page0.Header.PageLength == 0)) {
		/*
		 *  Pre-fetch the ports LAN MAC address!
		 *  (LANPage1_t stuff)
		 */
		(void) GetLanConfigPages(ioc);
#ifdef MPT_DEBUG
		{
			u8 *a = (u8*)&ioc->lan_cnfg_page1.HardwareAddressLow;
			dprintk((KERN_INFO MYNAM ": %s: LanAddr = %02X:%02X:%02X:%02X:%02X:%02X\n",
					ioc->name, a[5], a[4], a[3], a[2], a[1], a[0] ));
		}
#endif
	}

	/* Enable! (reply interrupt) */
	CHIPREG_WRITE32(&ioc->chip->IntMask, ~(MPI_HIM_RIM));
	ioc->active = 1;

// NEW!
#if 0						// Kiss-of-death!?!
	if (alt_ioc_ready && (r==0)) {
		/* (re)Enable alt-IOC! (reply interrupt) */
		dprintk((KERN_INFO MYNAM ": alt-%s reply irq re-enabled\n",
				ioc->alt_ioc->name));
		CHIPREG_WRITE32(&ioc->alt_ioc->chip->IntMask, ~(MPI_HIM_RIM));
		ioc->alt_ioc->active = 1;
	}
#endif

	/* NEW!  20010120 -sralston
	 *  Enable MPT base driver management of EventNotification
	 *  and EventAck handling.
	 */
	if (!ioc->facts.EventState)
		(void) SendEventNotification(ioc, 1);	/* 1=Enable EventNotification */
// NEW!
// FIXME!?!
//	if (ioc->alt_ioc && alt_ioc_ready && !ioc->alt_ioc->facts.EventState) {
//		(void) SendEventNotification(ioc->alt_ioc, 1);	/* 1=Enable EventNotification */
//	}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mpt_detect_929_bound_ports - Search for PCI bus/dev_function
 *	which matches PCI bus/dev_function (+/-1) for newly discovered 929.
 *	@ioc: Pointer to MPT adapter structure
 *	@pdev: Pointer to (struct pci_dev) structure
 *
 *	If match on PCI dev_function +/-1 is found, bind the two MPT adapters
 *	using alt_ioc pointer fields in their %MPT_ADAPTER structures.
 */
static void
mpt_detect_929_bound_ports(MPT_ADAPTER *ioc, struct pci_dev *pdev)
{
	MPT_ADAPTER *ioc_srch = mpt_adapter_find_first();
	unsigned int match_lo, match_hi;

	match_lo = pdev->devfn-1;
	match_hi = pdev->devfn+1;
	dprintk((KERN_INFO MYNAM ": %s: PCI bus/devfn=%x/%x, searching for devfn match on %x or %x\n",
			ioc->name, pdev->bus->number, pdev->devfn, match_lo, match_hi));

	while (ioc_srch != NULL) {
		struct pci_dev *_pcidev = ioc_srch->pcidev;

		if ( (_pcidev->device == MPI_MANUFACTPAGE_DEVICEID_FC929) &&
		     (_pcidev->bus->number == pdev->bus->number) &&
		     (_pcidev->devfn == match_lo || _pcidev->devfn == match_hi) ) {
			/* Paranoia checks */
			if (ioc->alt_ioc != NULL) {
				printk(KERN_WARNING MYNAM ": Oops, already bound (%s <==> %s)!\n",
						ioc->name, ioc->alt_ioc->name);
				break;
			} else if (ioc_srch->alt_ioc != NULL) {
				printk(KERN_WARNING MYNAM ": Oops, already bound (%s <==> %s)!\n",
						ioc_srch->name, ioc_srch->alt_ioc->name);
				break;
			}
			dprintk((KERN_INFO MYNAM ": FOUND! binding %s <==> %s\n",
					ioc->name, ioc_srch->name));
			ioc_srch->alt_ioc = ioc;
			ioc->alt_ioc = ioc_srch;
			ioc->sod_reset = ioc->alt_ioc->sod_reset;
			ioc->last_kickstart = ioc->alt_ioc->last_kickstart;
			break;
		}
		ioc_srch = mpt_adapter_find_next(ioc_srch);
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mpt_adapter_disable - Disable misbehaving MPT adapter.
 *	@this: Pointer to MPT adapter structure
 *	@free: Free up alloc'd reply, request, etc.
 */
static void
mpt_adapter_disable(MPT_ADAPTER *this, int freeup)
{
	if (this != NULL) {
		int sz;
		u32 state;

		/* Disable the FW */
		state = GetIocState(this, 1);
		if (state == MPI_IOC_STATE_OPERATIONAL) {
			if (SendIocReset(this, MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET) != 0)
				(void) KickStart(this, 1);
		}

		/* Disable adapter interrupts! */
		CHIPREG_WRITE32(&this->chip->IntMask, 0xFFFFFFFF);
		this->active = 0;
		/* Clear any lingering interrupt */
		CHIPREG_WRITE32(&this->chip->IntStatus, 0);

		if (freeup && this->reply_alloc != NULL) {
			sz = (this->reply_sz * this->reply_depth) + 128;
			pci_free_consistent(this->pcidev, sz,
					this->reply_alloc, this->reply_alloc_dma);
			this->reply_frames = NULL;
			this->reply_alloc = NULL;
			this->alloc_total -= sz;
		}

		if (freeup && this->req_alloc != NULL) {
			sz = (this->req_sz * this->req_depth) + 128;
			/*
			 *  Rounding UP to nearest 4-kB boundary here...
			 */
			sz = ((sz + 0x1000UL - 1UL) / 0x1000) * 0x1000;
			pci_free_consistent(this->pcidev, sz,
					this->req_alloc, this->req_alloc_dma);
			this->req_frames = NULL;
			this->req_alloc = NULL;
			this->alloc_total -= sz;
		}

		if (freeup && this->sense_buf_pool != NULL) {
			sz = (this->req_depth * 256);
			pci_free_consistent(this->pcidev, sz,
					this->sense_buf_pool, this->sense_buf_pool_dma);
			this->sense_buf_pool = NULL;
			this->alloc_total -= sz;
		}
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mpt_adapter_dispose - Free all resources associated with a MPT
 *	adapter.
 *	@this: Pointer to MPT adapter structure
 *
 *	This routine unregisters h/w resources and frees all alloc'd memory
 *	associated with a MPT adapter structure.
 */
static void
mpt_adapter_dispose(MPT_ADAPTER *this)
{
	if (this != NULL) {
		int sz_first, sz_last;

		sz_first = this->alloc_total;

		mpt_adapter_disable(this, 1);

		if (this->pci_irq != -1) {
			free_irq(this->pci_irq, this);
			this->pci_irq = -1;
		}

		if (this->memmap != NULL)
			iounmap((u8 *) this->memmap);

#if defined(CONFIG_MTRR) && 0
		if (this->mtrr_reg > 0) {
			mtrr_del(this->mtrr_reg, 0, 0);
			dprintk((KERN_INFO MYNAM ": %s: MTRR region de-registered\n", this->name));
		}
#endif

		/*  Zap the adapter lookup ptr!  */
		mpt_adapters[this->id] = NULL;

		sz_last = this->alloc_total;
		dprintk((KERN_INFO MYNAM ": %s: free'd %d of %d bytes\n",
				this->name, sz_first-sz_last+(int)sizeof(*this), sz_first));
		kfree(this);
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	MptDisplayIocCapabilities - Disply IOC's capacilities.
 *	@ioc: Pointer to MPT adapter structure
 */
static void
MptDisplayIocCapabilities(MPT_ADAPTER *ioc)
{
	int i = 0;

	printk(KERN_INFO "%s: ", ioc->name);
	if (ioc->prod_name && strlen(ioc->prod_name) > 3)
		printk("%s: ", ioc->prod_name+3);
	printk("Capabilities={");

	if (ioc->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_INITIATOR) {
		printk("Initiator");
		i++;
	}

	if (ioc->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_TARGET) {
		printk("%sTarget", i ? "," : "");
		i++;
	}

	if (ioc->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_LAN) {
		printk("%sLAN", i ? "," : "");
		i++;
	}

#if 0
	/*
	 *  This would probably evoke more questions than it's worth
	 */
	if (ioc->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_TARGET) {
		printk("%sLogBusAddr", i ? "," : "");
		i++;
	}
#endif

	printk("}\n");
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	MakeIocReady - Get IOC to a READY state, using KickStart if needed.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@kick: Force hard KickStart of IOC
 *
 *	Returns 0 for already-READY, 1 for hard reset success,
 *	else negative for failure.
 */
static int
MakeIocReady(MPT_ADAPTER *ioc, int force)
{
	u32	 ioc_state;
	int	 statefault = 0;
	int 	 cntdn;
	int	 hard_reset_done = 0;
	int	 r;
	int	 i;

	/* Get current [raw] IOC state  */
	ioc_state = GetIocState(ioc, 0);
	dhsprintk((KERN_INFO MYNAM "::MakeIocReady, %s [raw] state=%08x\n", ioc->name, ioc_state));

	/*
	 *	Check to see if IOC got left/stuck in doorbell handshake
	 *	grip of death.  If so, hard reset the IOC.
	 */
	if (ioc_state & MPI_DOORBELL_ACTIVE) {
		statefault = 1;
		printk(KERN_WARNING MYNAM ": %s: Uh-oh, unexpected doorbell active!\n",
				ioc->name);
	}

	/* Is it already READY? */
	if (!statefault && (ioc_state & MPI_IOC_STATE_MASK) == MPI_IOC_STATE_READY)
		return 0;

	/*
	 *	Check to see if IOC is in FAULT state.
	 */
	if ((ioc_state & MPI_IOC_STATE_MASK) == MPI_IOC_STATE_FAULT) {
		statefault = 2;
		printk(KERN_WARNING MYNAM ": %s: Uh-oh, IOC is in FAULT state!!!\n",
				ioc->name);
		printk(KERN_WARNING "           FAULT code = %04xh\n",
				ioc_state & MPI_DOORBELL_DATA_MASK);
	}

	/*
	 *	Hmmm...  Did it get left operational?
	 */
	if ((ioc_state & MPI_IOC_STATE_MASK) == MPI_IOC_STATE_OPERATIONAL) {
		statefault = 3;
		dprintk((KERN_WARNING MYNAM ": %s: Hmmm... IOC operational unexpected\n",
				ioc->name));
	}

	hard_reset_done = KickStart(ioc, statefault||force);
	if (hard_reset_done < 0)
		return -1;

	/*
	 *  Loop here waiting for IOC to come READY.
	 */
	i = 0;
	cntdn = HZ * 15;
	while ((ioc_state = GetIocState(ioc, 1)) != MPI_IOC_STATE_READY) {
		if (ioc_state == MPI_IOC_STATE_OPERATIONAL) {
			/*
			 *  BIOS or previous driver load left IOC in OP state.
			 *  Reset messaging FIFOs.
			 */
			if ((r = SendIocReset(ioc, MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET)) != 0) {
				printk(KERN_ERR MYNAM ": %s: ERROR - IOC msg unit reset failed!\n", ioc->name);
				return -2;
			}
		} else if (ioc_state == MPI_IOC_STATE_RESET) {
			/*
			 *  Something is wrong.  Try to get IOC back
			 *  to a known state.
			 */
			if ((r = SendIocReset(ioc, MPI_FUNCTION_IO_UNIT_RESET)) != 0) {
				printk(KERN_ERR MYNAM ": %s: ERROR - IO unit reset failed!\n", ioc->name);
				return -3;
			}
		}

		i++; cntdn--;
		if (!cntdn) {
			printk(KERN_ERR MYNAM ": %s: ERROR - Wait IOC_READY state timeout(%d)!\n",
					ioc->name, (i+5)/HZ);
			return -ETIME;
		}

		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
	}

	if (statefault < 3) {
		printk(KERN_WARNING MYNAM ": %s: Whew!  Recovered from %s\n",
				ioc->name,
				statefault==1 ? "stuck handshake" : "IOC FAULT");
	}

	return hard_reset_done;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	GetIocState - Get the current state of a MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@cooked: Request raw or cooked IOC state
 *
 *	Returns all IOC Doorbell register bits if cooked==0, else just the
 *	Doorbell bits in MPI_IOC_STATE_MASK.
 */
static u32
GetIocState(MPT_ADAPTER *ioc, int cooked)
{
	u32 s, sc;

	/*  Get!  */
	s = CHIPREG_READ32(&ioc->chip->Doorbell);
	dprintk((KERN_INFO MYNAM ": %s: raw state = %08x\n", ioc->name, s));
	sc = s & MPI_IOC_STATE_MASK;

	/*  Save!  */
	ioc->last_state = sc;

	return cooked ? sc : s;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	GetIocFacts - Send IOCFacts request to MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
GetIocFacts(MPT_ADAPTER *ioc)
{
	IOCFacts_t		 get_facts;
	IOCFactsReply_t		*facts;
	int			 r;
	int			 req_sz;
	int			 reply_sz;
	u32			 status;

	/* IOC *must* NOT be in RESET state! */
	if (ioc->last_state == MPI_IOC_STATE_RESET) {
		printk(KERN_ERR MYNAM ": ERROR - Can't get IOCFacts, %s NOT READY! (%08x)\n",
				ioc->name,
				ioc->last_state );
		return -44;
	}

	facts = &ioc->facts;

	/* Destination (reply area)... */
	reply_sz = sizeof(*facts);
	memset(facts, 0, reply_sz);

	/* Request area (get_facts on the stack right now!) */
	req_sz = sizeof(get_facts);
	memset(&get_facts, 0, req_sz);

	get_facts.Function = MPI_FUNCTION_IOC_FACTS;
	/* Assert: All other get_facts fields are zero! */

	dprintk((KERN_INFO MYNAM ": %s: Sending get IocFacts request\n", ioc->name));

	/* No non-zero fields in the get_facts request are greater than
	 * 1 byte in size, so we can just fire it off as is.
	 */
	r = HandShakeReqAndReply(ioc,
			req_sz, (u32*)&get_facts,
			reply_sz, (u16*)facts, 3);
	if (r != 0)
		return r;

	/*
	 * Now byte swap (GRRR) the necessary fields before any further
	 * inspection of reply contents.
	 *
	 * But need to do some sanity checks on MsgLength (byte) field
	 * to make sure we don't zero IOC's req_sz!
	 */
	/* Did we get a valid reply? */
	if (facts->MsgLength > offsetof(IOCFactsReply_t, RequestFrameSize)/sizeof(u32)) {
		/*
		 * If not been here, done that, save off first WhoInit value
		 */
		if (ioc->FirstWhoInit == WHOINIT_UNKNOWN)
			ioc->FirstWhoInit = facts->WhoInit;

		facts->MsgVersion = le16_to_cpu(facts->MsgVersion);
		facts->MsgContext = le32_to_cpu(facts->MsgContext);
		facts->IOCStatus = le16_to_cpu(facts->IOCStatus);
		facts->IOCLogInfo = le32_to_cpu(facts->IOCLogInfo);
		status = facts->IOCStatus & MPI_IOCSTATUS_MASK;
		/* CHECKME! IOCStatus, IOCLogInfo */

		facts->ReplyQueueDepth = le16_to_cpu(facts->ReplyQueueDepth);
		facts->RequestFrameSize = le16_to_cpu(facts->RequestFrameSize);
		facts->FWVersion = le16_to_cpu(facts->FWVersion);
		facts->ProductID = le16_to_cpu(facts->ProductID);
		facts->CurrentHostMfaHighAddr =
				le32_to_cpu(facts->CurrentHostMfaHighAddr);
		facts->GlobalCredits = le16_to_cpu(facts->GlobalCredits);
		facts->CurrentSenseBufferHighAddr =
				le32_to_cpu(facts->CurrentSenseBufferHighAddr);
		facts->CurReplyFrameSize =
				le16_to_cpu(facts->CurReplyFrameSize);

		/*
		 * Handle NEW (!) IOCFactsReply fields in MPI-1.01.xx
		 * Older MPI-1.00.xx struct had 13 dwords, and enlarged
		 * to 14 in MPI-1.01.0x.
		 */
		if (facts->MsgLength >= sizeof(IOCFactsReply_t)/sizeof(u32) && facts->MsgVersion > 0x0100) {
			facts->FWImageSize = le32_to_cpu(facts->FWImageSize);
			facts->DataImageSize = le32_to_cpu(facts->DataImageSize);
		}

		if (facts->RequestFrameSize) {
			/*
			 * Set values for this IOC's REQUEST queue size & depth...
			 */
			ioc->req_sz = MIN(MPT_REQ_SIZE, facts->RequestFrameSize * 4);

			/*
			 *  Set values for this IOC's REPLY queue size & depth...
			 *
			 * BUG? FIX?  20000516 -nromer & sralston 
			 *  GRRR...  The following did not translate well from MPI v0.09:
			 *	ioc->reply_sz = MIN(MPT_REPLY_SIZE, facts->ReplySize * 4);
			 *  to 0.10:
			 *	ioc->reply_sz = MIN(MPT_REPLY_SIZE, facts->BlockSize * 4);
			 *  Was trying to minimally optimize to smallest possible reply size
			 *  (and greatly reduce kmalloc size).  But LAN may need larger reply?
			 *
			 *  So for now, just set reply size to request size.  FIXME?
			 */
			ioc->reply_sz = ioc->req_sz;
		} else {
			/*  Something is wrong!  */
			printk(KERN_ERR MYNAM ": %s: ERROR - IOC reported invalid 0 request size!\n",
					ioc->name);
			ioc->req_sz = MPT_REQ_SIZE;
			ioc->reply_sz = MPT_REPLY_SIZE;
			return -55;
		}
		ioc->req_depth = MIN(MPT_REQ_DEPTH, facts->GlobalCredits);
		ioc->reply_depth = MIN(MPT_REPLY_DEPTH, facts->ReplyQueueDepth);

		dprintk((KERN_INFO MYNAM ": %s: reply_sz=%3d, reply_depth=%4d\n",
				ioc->name, ioc->reply_sz, ioc->reply_depth));
		dprintk((KERN_INFO MYNAM ": %s: req_sz  =%3d, req_depth  =%4d\n",
				ioc->name, ioc->req_sz, ioc->req_depth));

		/* Get port facts! */
		if ( (r = GetPortFacts(ioc, 0)) != 0 )
			return r;
	} else {
		printk(KERN_ERR MYNAM ": %s: ERROR - Invalid IOC facts reply!\n",
				ioc->name);
		return -66;
	}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	GetPortFacts - Send PortFacts request to MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@portnum: Port number
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
GetPortFacts(MPT_ADAPTER *ioc, int portnum)
{
	PortFacts_t		 get_pfacts;
	PortFactsReply_t	*pfacts;
	int			 i;
	int			 req_sz;
	int			 reply_sz;

	/* IOC *must* NOT be in RESET state! */
	if (ioc->last_state == MPI_IOC_STATE_RESET) {
		printk(KERN_ERR MYNAM ": ERROR - Can't get PortFacts, %s NOT READY! (%08x)\n",
				ioc->name,
				ioc->last_state );
		return -4;
	}

	pfacts = &ioc->pfacts[portnum];

	/* Destination (reply area)...  */
	reply_sz = sizeof(*pfacts);
	memset(pfacts, 0, reply_sz);

	/* Request area (get_pfacts on the stack right now!) */
	req_sz = sizeof(get_pfacts);
	memset(&get_pfacts, 0, req_sz);

	get_pfacts.Function = MPI_FUNCTION_PORT_FACTS;
	get_pfacts.PortNumber = portnum;
	/* Assert: All other get_pfacts fields are zero! */

	dprintk((KERN_INFO MYNAM ": %s: Sending get PortFacts(%d) request\n",
			ioc->name, portnum));

	/* No non-zero fields in the get_pfacts request are greater than
	 * 1 byte in size, so we can just fire it off as is.
	 */
	i = HandShakeReqAndReply(ioc, req_sz, (u32*)&get_pfacts,
				reply_sz, (u16*)pfacts, 3);
	if (i != 0)
		return i;

	/* Did we get a valid reply? */

	/* Now byte swap the necessary fields in the response. */
	pfacts->MsgContext = le32_to_cpu(pfacts->MsgContext);
	pfacts->IOCStatus = le16_to_cpu(pfacts->IOCStatus);
	pfacts->IOCLogInfo = le32_to_cpu(pfacts->IOCLogInfo);
	pfacts->MaxDevices = le16_to_cpu(pfacts->MaxDevices);
	pfacts->PortSCSIID = le16_to_cpu(pfacts->PortSCSIID);
	pfacts->ProtocolFlags = le16_to_cpu(pfacts->ProtocolFlags);
	pfacts->MaxPostedCmdBuffers = le16_to_cpu(pfacts->MaxPostedCmdBuffers);
	pfacts->MaxPersistentIDs = le16_to_cpu(pfacts->MaxPersistentIDs);
	pfacts->MaxLanBuckets = le16_to_cpu(pfacts->MaxLanBuckets);

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	SendIocInit - Send IOCInit request to MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 *	Send IOCInit followed by PortEnable to bring IOC to OPERATIONAL state.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
SendIocInit(MPT_ADAPTER *ioc)
{
	IOCInit_t		 ioc_init;
	MPIDefaultReply_t	 init_reply;
	u32			 state;
	int			 r;
	int			 count;
	int			 cntdn;

	memset(&ioc_init, 0, sizeof(ioc_init));
	memset(&init_reply, 0, sizeof(init_reply));

	ioc_init.WhoInit = MPI_WHOINIT_HOST_DRIVER;
/*	ioc_init.ChainOffset = 0;			*/
	ioc_init.Function = MPI_FUNCTION_IOC_INIT;
/*	ioc_init.Flags = 0;				*/

	/*ioc_init.MaxDevices = 16;*/
	ioc_init.MaxDevices = 255;
/*	ioc_init.MaxBuses = 16;				*/
	ioc_init.MaxBuses = 1;

/*	ioc_init.MsgFlags = 0;				*/
/*	ioc_init.MsgContext = cpu_to_le32(0x00000000);	*/
	ioc_init.ReplyFrameSize = cpu_to_le16(ioc->reply_sz);	/* in BYTES */
	ioc_init.HostMfaHighAddr = cpu_to_le32(0);	/* Say we 32-bit! for now */

	dprintk((KERN_INFO MYNAM ": %s: Sending IOCInit (req @ %p)\n", ioc->name, &ioc_init));

	r = HandShakeReqAndReply(ioc, sizeof(IOCInit_t), (u32*)&ioc_init,
			sizeof(MPIDefaultReply_t), (u16*)&init_reply, 10);
	if (r != 0)
		return r;

	/* No need to byte swap the multibyte fields in the reply
	 * since we don't even look at it's contents.
	 */

	if ((r = SendPortEnable(ioc, 0)) != 0)
		return r;

	/* YIKES!  SUPER IMPORTANT!!!
	 *  Poll IocState until _OPERATIONAL while IOC is doing
	 *  LoopInit and TargetDiscovery!
	 */
	count = 0;
	cntdn = HZ * 60;					/* chg'd from 30 to 60 seconds */
	state = GetIocState(ioc, 1);
	while (state != MPI_IOC_STATE_OPERATIONAL && --cntdn) {
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);

		if (!cntdn) {
			printk(KERN_ERR MYNAM ": %s: ERROR - Wait IOC_OP state timeout(%d)!\n",
					ioc->name, (count+5)/HZ);
			return -9;
		}

		state = GetIocState(ioc, 1);
		count++;
	}
	dhsprintk((KERN_INFO MYNAM ": %s: INFO - Wait IOC_OPERATIONAL state (cnt=%d)\n",
			ioc->name, count));

	return r;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	SendPortEnable - Send PortEnable request to MPT adapter port.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@portnum: Port number to enable
 *
 *	Send PortEnable to bring IOC to OPERATIONAL state.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
SendPortEnable(MPT_ADAPTER *ioc, int portnum)
{
	PortEnable_t		 port_enable;
	MPIDefaultReply_t	 reply_buf;
	int	 i;
	int	 req_sz;
	int	 reply_sz;

	/*  Destination...  */
	reply_sz = sizeof(MPIDefaultReply_t);
	memset(&reply_buf, 0, reply_sz);

	req_sz = sizeof(PortEnable_t);
	memset(&port_enable, 0, req_sz);

	port_enable.Function = MPI_FUNCTION_PORT_ENABLE;
	port_enable.PortNumber = portnum;
/*	port_enable.ChainOffset = 0;		*/
/*	port_enable.MsgFlags = 0;		*/
/*	port_enable.MsgContext = 0;		*/

	dprintk((KERN_INFO MYNAM ": %s: Sending Port(%d)Enable (req @ %p)\n",
			ioc->name, portnum, &port_enable));

	i = HandShakeReqAndReply(ioc, req_sz, (u32*)&port_enable,
			reply_sz, (u16*)&reply_buf, 65);
	if (i != 0)
		return i;

	/* We do not even look at the reply, so we need not
	 * swap the multi-byte fields.
	 */

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	KickStart - Perform hard reset of MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@force: Force hard reset
 *
 *	This routine places MPT adapter in diagnostic mode via the
 *	WriteSequence register, and then performs a hard reset of adapter
 *	via the Diagnostic register.
 *
 *	Returns 0 for soft reset success, 1 for hard reset success,
 *	else a negative value for failure.
 */
static int
KickStart(MPT_ADAPTER *ioc, int force)
{
	int hard_reset_done = 0;
	u32 ioc_state;
	int cnt = 0;

	dprintk((KERN_WARNING MYNAM ": KickStarting %s!\n", ioc->name));

	hard_reset_done = mpt_fc9x9_reset(ioc, force);
#if 0
	if (ioc->chip_type == FC909 || ioc->chip-type == FC919) {
		hard_reset_done = mpt_fc9x9_reset(ioc, force);
	} else if (ioc->chip_type == FC929) {
		unsigned long delta;

		delta = jiffies - ioc->last_kickstart;
		dprintk((KERN_INFO MYNAM ": %s: 929 KickStart, last=%ld, delta = %ld\n",
				ioc->name, ioc->last_kickstart, delta));
		if ((ioc->sod_reset == 0) || (delta >= 10*HZ))
			hard_reset_done = mpt_fc9x9_reset(ioc, ignore);
		else {
			dprintk((KERN_INFO MYNAM ": %s: Skipping KickStart (delta=%ld)!\n",
					ioc->name, delta));
			return 0;
		}
	/* TODO! Add C1030!
	} else if (ioc->chip_type == C1030) {
	 */
	} else {
		printk(KERN_ERR MYNAM ": %s: ERROR - Bad chip_type (0x%x)\n",
				ioc->name, ioc->chip_type);
		return -5;
	}
#endif

	if (hard_reset_done < 0)
		return hard_reset_done;

	dprintk((KERN_INFO MYNAM ": %s: Diagnostic reset successful\n",
			ioc->name));

	for (cnt=0; cnt<HZ*20; cnt++) {
		if ((ioc_state = GetIocState(ioc, 1)) == MPI_IOC_STATE_READY) {
			dprintk((KERN_INFO MYNAM ": %s: KickStart successful! (cnt=%d)\n",
					ioc->name, cnt));
			return hard_reset_done;
		}
		/* udelay(10000) ? */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
	}

	printk(KERN_ERR MYNAM ": %s: ERROR - Failed to come READY after reset!\n",
			ioc->name);
	return -1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mpt_fc9x9_reset - Perform hard reset of FC9x9 adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 *	This routine places FC9x9 adapter in diagnostic mode via the
 *	WriteSequence register, and then performs a hard reset of adapter
 *	via the Diagnostic register.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
mpt_fc9x9_reset(MPT_ADAPTER *ioc, int ignore)
{
	u32 diag0val;
	int hard_reset_done = 0;

	/* Use "Diagnostic reset" method! (only thing available!) */

	diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
#ifdef MPT_DEBUG
{
	u32 diag1val = 0;
	if (ioc->alt_ioc)
		diag1val = CHIPREG_READ32(&ioc->alt_ioc->chip->Diagnostic);
	dprintk((KERN_INFO MYNAM ": %s: DBG1: diag0=%08x, diag1=%08x\n",
			ioc->name, diag0val, diag1val));
}
#endif
	if (diag0val & MPI_DIAG_DRWE) {
		dprintk((KERN_INFO MYNAM ": %s: DiagWriteEn bit already set\n",
				ioc->name));
	} else {
		/* Write magic sequence to WriteSequence register */
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_1ST_KEY_VALUE);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_2ND_KEY_VALUE);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_3RD_KEY_VALUE);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_4TH_KEY_VALUE);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_5TH_KEY_VALUE);
		dprintk((KERN_INFO MYNAM ": %s: Wrote magic DiagWriteEn sequence [spot#1]\n",
				ioc->name));
	}

	diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
#ifdef MPT_DEBUG
{
	u32 diag1val = 0;
	if (ioc->alt_ioc)
		diag1val = CHIPREG_READ32(&ioc->alt_ioc->chip->Diagnostic);
	dprintk((KERN_INFO MYNAM ": %s: DbG2: diag0=%08x, diag1=%08x\n",
			ioc->name, diag0val, diag1val));
}
#endif
	if (!ignore && (diag0val & MPI_DIAG_RESET_HISTORY)) {
		dprintk((KERN_INFO MYNAM ": %s: Skipping due to ResetHistory bit set!\n",
				ioc->name));
	} else {
		/*
		 * Now hit the reset bit in the Diagnostic register
		 * (THE BIG HAMMER!)
		 */
		CHIPREG_WRITE32(&ioc->chip->Diagnostic, MPI_DIAG_RESET_ADAPTER);
		hard_reset_done = 1;
		dprintk((KERN_INFO MYNAM ": %s: Diagnostic reset performed\n",
				ioc->name));

		/* want udelay(100) */
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);

		/* Write magic sequence to WriteSequence register */
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_1ST_KEY_VALUE);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_2ND_KEY_VALUE);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_3RD_KEY_VALUE);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_4TH_KEY_VALUE);
		CHIPREG_WRITE32(&ioc->chip->WriteSequence, MPI_WRSEQ_5TH_KEY_VALUE);
		dprintk((KERN_INFO MYNAM ": %s: Wrote magic DiagWriteEn sequence [spot#2]\n",
				ioc->name));
	}

	/* Clear RESET_HISTORY bit! */
	CHIPREG_WRITE32(&ioc->chip->Diagnostic, 0x0);

	diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
#ifdef MPT_DEBUG
{
	u32 diag1val = 0;
	if (ioc->alt_ioc)
		diag1val = CHIPREG_READ32(&ioc->alt_ioc->chip->Diagnostic);
	dprintk((KERN_INFO MYNAM ": %s: DbG3: diag0=%08x, diag1=%08x\n",
			ioc->name, diag0val, diag1val));
}
#endif
	if (diag0val & MPI_DIAG_RESET_HISTORY) {
		printk(KERN_WARNING MYNAM ": %s: WARNING - ResetHistory bit failed to clear!\n",
				ioc->name);
	}

	diag0val = CHIPREG_READ32(&ioc->chip->Diagnostic);
#ifdef MPT_DEBUG
{
	u32 diag1val = 0;
	if (ioc->alt_ioc)
		diag1val = CHIPREG_READ32(&ioc->alt_ioc->chip->Diagnostic);
	dprintk((KERN_INFO MYNAM ": %s: DbG4: diag0=%08x, diag1=%08x\n",
			ioc->name, diag0val, diag1val));
}
#endif
	if (diag0val & (MPI_DIAG_FLASH_BAD_SIG | MPI_DIAG_RESET_ADAPTER | MPI_DIAG_DISABLE_ARM)) {
		printk(KERN_ERR MYNAM ": %s: ERROR - Diagnostic reset FAILED! (%02xh)\n",
				ioc->name, diag0val);
		return -3;
	}

	/*
	 * Reset flag that says we've enabled event notification
	 */
	ioc->facts.EventState = 0;

	/* NEW!  20010220 -sralston
	 * Try to avoid redundant resets of the 929.
	 */
	ioc->sod_reset++;
	ioc->last_kickstart = jiffies;
	if (ioc->alt_ioc) {
		ioc->alt_ioc->sod_reset = ioc->sod_reset;
		ioc->alt_ioc->last_kickstart = ioc->last_kickstart;
	}

	return hard_reset_done;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	SendIocReset - Send IOCReset request to MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@reset_type: reset type, expected values are
 *	%MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET or %MPI_FUNCTION_IO_UNIT_RESET
 *
 *	Send IOCReset request to the MPT adapter.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
SendIocReset(MPT_ADAPTER *ioc, u8 reset_type)
{
	int r;

	dprintk((KERN_WARNING MYNAM ": %s: Sending IOC reset(0x%02x)!\n",
			ioc->name, reset_type));
	CHIPREG_WRITE32(&ioc->chip->Doorbell, reset_type<<MPI_DOORBELL_FUNCTION_SHIFT);
	if ((r = WaitForDoorbellAck(ioc, 2)) < 0)
		return r;

	/* TODO!
	 *  Cleanup all event stuff for this IOC; re-issue EventNotification
	 *  request if needed.
	 */
	if (ioc->facts.Function)
		ioc->facts.EventState = 0;

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	PrimeIocFifos - Initialize IOC request and reply FIFOs.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 *	This routine allocates memory for the MPT reply and request frame
 *	pools, and primes the IOC reply FIFO with reply frames.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
PrimeIocFifos(MPT_ADAPTER *ioc)
{
	MPT_FRAME_HDR *mf;
	unsigned long b;
	dma_addr_t aligned_mem_dma;
	u8 *mem, *aligned_mem;
	int i, sz;

	/*  Prime reply FIFO...  */

	if (ioc->reply_frames == NULL) {
		sz = (ioc->reply_sz * ioc->reply_depth) + 128;
		mem = pci_alloc_consistent(ioc->pcidev, sz, &ioc->reply_alloc_dma);
		if (mem == NULL)
			goto out_fail;

		memset(mem, 0, sz);
		ioc->alloc_total += sz;
		ioc->reply_alloc = mem;
		dprintk((KERN_INFO MYNAM ": %s.reply_alloc  @ %p[%08x], sz=%d bytes\n",
			 ioc->name, mem, ioc->reply_alloc_dma, sz));

		b = (unsigned long) mem;
		b = (b + (0x80UL - 1UL)) & ~(0x80UL - 1UL); /* round up to 128-byte boundary */
		aligned_mem = (u8 *) b;
		ioc->reply_frames = (MPT_FRAME_HDR *) aligned_mem;
		ioc->reply_frames_dma =
			(ioc->reply_alloc_dma + (aligned_mem - mem));
		aligned_mem_dma = ioc->reply_frames_dma;
		dprintk((KERN_INFO MYNAM ": %s.reply_frames @ %p[%08x]\n",
			 ioc->name, aligned_mem, aligned_mem_dma));

		for (i = 0; i < ioc->reply_depth; i++) {
			/*  Write each address to the IOC!  */
			CHIPREG_WRITE32(&ioc->chip->ReplyFifo, aligned_mem_dma);
			aligned_mem_dma += ioc->reply_sz;
		}
	}


	/*  Request FIFO - WE manage this!  */

	if (ioc->req_frames == NULL) {
		sz = (ioc->req_sz * ioc->req_depth) + 128;
		/*
		 *  Rounding UP to nearest 4-kB boundary here...
		 */
		sz = ((sz + 0x1000UL - 1UL) / 0x1000) * 0x1000;

		mem = pci_alloc_consistent(ioc->pcidev, sz, &ioc->req_alloc_dma);
		if (mem == NULL)
			goto out_fail;

		memset(mem, 0, sz);
		ioc->alloc_total += sz;
		ioc->req_alloc = mem;
		dprintk((KERN_INFO MYNAM ": %s.req_alloc    @ %p[%08x], sz=%d bytes\n",
			 ioc->name, mem, ioc->req_alloc_dma, sz));

		b = (unsigned long) mem;
		b = (b + (0x80UL - 1UL)) & ~(0x80UL - 1UL); /* round up to 128-byte boundary */
		aligned_mem = (u8 *) b;
		ioc->req_frames = (MPT_FRAME_HDR *) aligned_mem;
		ioc->req_frames_dma =
			(ioc->req_alloc_dma + (aligned_mem - mem));
		aligned_mem_dma = ioc->req_frames_dma;

		dprintk((KERN_INFO MYNAM ": %s.req_frames   @ %p[%08x]\n",
			 ioc->name, aligned_mem, aligned_mem_dma));

		for (i = 0; i < ioc->req_depth; i++) {
			mf = (MPT_FRAME_HDR *) aligned_mem;

			/*  Queue REQUESTs *internally*!  */
			Q_ADD_TAIL(&ioc->FreeQ.head, &mf->u.frame.linkage, MPT_FRAME_HDR);
			aligned_mem += ioc->req_sz;
		}

#if defined(CONFIG_MTRR) && 0
		/*
		 *  Enable Write Combining MTRR for IOC's memory region.
		 *  (at least as much as we can; "size and base must be
		 *  multiples of 4 kiB"
		 */
		ioc->mtrr_reg = mtrr_add(ioc->req_alloc_dma,
					 sz,
					 MTRR_TYPE_WRCOMB, 1);
		dprintk((KERN_INFO MYNAM ": %s: MTRR region registered (base:size=%08x:%x)\n",
				ioc->name, ioc->req_alloc_dma,
				sz ));
#endif

	}

	if (ioc->sense_buf_pool == NULL) {
		sz = (ioc->req_depth * 256);
		ioc->sense_buf_pool =
				pci_alloc_consistent(ioc->pcidev, sz, &ioc->sense_buf_pool_dma);
		if (ioc->sense_buf_pool == NULL)
			goto out_fail;

		ioc->alloc_total += sz;
	}

	return 0;

out_fail:
	if (ioc->reply_alloc != NULL) {
		sz = (ioc->reply_sz * ioc->reply_depth) + 128;
		pci_free_consistent(ioc->pcidev,
				sz,
				ioc->reply_alloc, ioc->reply_alloc_dma);
		ioc->reply_frames = NULL;
		ioc->reply_alloc = NULL;
		ioc->alloc_total -= sz;
	}
	if (ioc->req_alloc != NULL) {
		sz = (ioc->req_sz * ioc->req_depth) + 128;
		/*
		 *  Rounding UP to nearest 4-kB boundary here...
		 */
		sz = ((sz + 0x1000UL - 1UL) / 0x1000) * 0x1000;
		pci_free_consistent(ioc->pcidev,
				sz,
				ioc->req_alloc, ioc->req_alloc_dma);
#if defined(CONFIG_MTRR) && 0
		if (ioc->mtrr_reg > 0) {
			mtrr_del(ioc->mtrr_reg, 0, 0);
			dprintk((KERN_INFO MYNAM ": %s: MTRR region de-registered\n",
					ioc->name));
		}
#endif
		ioc->req_frames = NULL;
		ioc->req_alloc = NULL;
		ioc->alloc_total -= sz;
	}
	if (ioc->sense_buf_pool != NULL) {
		sz = (ioc->req_depth * 256);
		pci_free_consistent(ioc->pcidev,
				sz,
				ioc->sense_buf_pool, ioc->sense_buf_pool_dma);
		ioc->sense_buf_pool = NULL;
	}
	return -1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	HandShakeReqAndReply - Send MPT request to and receive reply from
 *	IOC via doorbell handshake method.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@reqBytes: Size of the request in bytes
 *	@req: Pointer to MPT request frame
 *	@replyBytes: Expected size of the reply in bytes
 *	@u16reply: Pointer to area where reply should be written
 *	@maxwait: Max wait time for a reply (in seconds)
 *
 *	NOTES: It is the callers responsibility to byte-swap fields in the
 *	request which are greater than 1 byte in size.  It is also the
 *	callers responsibility to byte-swap response fields which are
 *	greater than 1 byte in size.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
HandShakeReqAndReply(MPT_ADAPTER *ioc, int reqBytes, u32 *req, int replyBytes, u16 *u16reply, int maxwait)
{
	MPIDefaultReply_t *mptReply;
	int failcnt = 0;
	int t;

	/*
	 * Get ready to cache a handshake reply
	 */
	ioc->hs_reply_idx = 0;
	mptReply = (MPIDefaultReply_t *) ioc->hs_reply;
	mptReply->MsgLength = 0;

	/*
	 * Make sure there are no doorbells (WRITE 0 to IntStatus reg),
	 * then tell IOC that we want to handshake a request of N words.
	 * (WRITE u32val to Doorbell reg).
	 */
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);
	CHIPREG_WRITE32(&ioc->chip->Doorbell,
			((MPI_FUNCTION_HANDSHAKE<<MPI_DOORBELL_FUNCTION_SHIFT) |
			 ((reqBytes/4)<<MPI_DOORBELL_ADD_DWORDS_SHIFT)));

	/*
	 * Wait for IOC's doorbell handshake int
	 */
	if ((t = WaitForDoorbellInt(ioc, 2)) < 0)
		failcnt++;

	dhsprintk((KERN_INFO MYNAM ": %s: HandShake request start, WaitCnt=%d%s\n",
			ioc->name, t, failcnt ? " - MISSING DOORBELL HANDSHAKE!" : ""));

	/*
	 * Clear doorbell int (WRITE 0 to IntStatus reg),
	 * then wait for IOC to ACKnowledge that it's ready for
	 * our handshake request.
	 */
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);
	if (!failcnt && (t = WaitForDoorbellAck(ioc, 2)) < 0)
		failcnt++;

	if (!failcnt) {
		int	 i;
		u8	*req_as_bytes = (u8 *) req;

		/*
		 * Stuff request words via doorbell handshake,
		 * with ACK from IOC for each.
		 */
		for (i = 0; !failcnt && i < reqBytes/4; i++) {
			u32 word = ((req_as_bytes[(i*4) + 0] <<  0) |
				    (req_as_bytes[(i*4) + 1] <<  8) |
				    (req_as_bytes[(i*4) + 2] << 16) |
				    (req_as_bytes[(i*4) + 3] << 24));

			CHIPREG_WRITE32(&ioc->chip->Doorbell, word);
			if ((t = WaitForDoorbellAck(ioc, 2)) < 0)
				failcnt++;
		}

		dmfprintk((KERN_INFO MYNAM ": Handshake request frame (@%p) header\n", req));
		DBG_DUMP_REQUEST_FRAME_HDR(req)

		dhsprintk((KERN_INFO MYNAM ": %s: HandShake request post done, WaitCnt=%d%s\n",
				ioc->name, t, failcnt ? " - MISSING DOORBELL ACK!" : ""));

		/*
		 * Wait for completion of doorbell handshake reply from the IOC
		 */
		if (!failcnt && (t = WaitForDoorbellReply(ioc, maxwait)) < 0)
			failcnt++;

		/*
		 * Copy out the cached reply...
		 */
		for(i=0; i < MIN(replyBytes/2,mptReply->MsgLength*2); i++)
			u16reply[i] = ioc->hs_reply[i];
	} else {
		return -99;
	}

	return -failcnt;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	WaitForDoorbellAck - Wait for IOC to clear the IOP_DOORBELL_STATUS bit
 *	in it's IntStatus register.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@howlong: How long to wait (in seconds)
 *
 *	This routine waits (up to ~2 seconds max) for IOC doorbell
 *	handshake ACKnowledge.
 *
 *	Returns a negative value on failure, else wait loop count.
 */
static int
WaitForDoorbellAck(MPT_ADAPTER *ioc, int howlong)
{
	int cntdn = HZ * howlong;
	int count = 0;
	u32 intstat;

	while (--cntdn) {
		intstat = CHIPREG_READ32(&ioc->chip->IntStatus);
		if (! (intstat & MPI_HIS_IOP_DOORBELL_STATUS))
			break;
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
		count++;
	}

	if (cntdn) {
		dhsprintk((KERN_INFO MYNAM ": %s: WaitForDoorbell ACK (cnt=%d)\n",
				ioc->name, count));
		return count;
	}

	printk(KERN_ERR MYNAM ": %s: ERROR - Doorbell ACK timeout(%d)!\n",
			ioc->name, (count+5)/HZ);
	return -1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	WaitForDoorbellInt - Wait for IOC to set the HIS_DOORBELL_INTERRUPT bit
 *	in it's IntStatus register.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@howlong: How long to wait (in seconds)
 *
 *	This routine waits (up to ~2 seconds max) for IOC doorbell interrupt.
 *
 *	Returns a negative value on failure, else wait loop count.
 */
static int
WaitForDoorbellInt(MPT_ADAPTER *ioc, int howlong)
{
	int cntdn = HZ * howlong;
	int count = 0;
	u32 intstat;

	while (--cntdn) {
		intstat = CHIPREG_READ32(&ioc->chip->IntStatus);
		if (intstat & MPI_HIS_DOORBELL_INTERRUPT)
			break;
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(1);
		count++;
	}

	if (cntdn) {
		dhsprintk((KERN_INFO MYNAM ": %s: WaitForDoorbell INT (cnt=%d)\n",
				ioc->name, count));
		return count;
	}

	printk(KERN_ERR MYNAM ": %s: ERROR - Doorbell INT timeout(%d)!\n",
			ioc->name, (count+5)/HZ);
	return -1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	WaitForDoorbellReply - Wait for and capture a IOC handshake reply.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@howlong: How long to wait (in seconds)
 *
 *	This routine polls the IOC for a handshake reply, 16 bits at a time.
 *	Reply is cached to IOC private area large enough to hold a maximum
 *	of 128 bytes of reply data.
 *
 *	Returns a negative value on failure, else size of reply in WORDS.
 */
static int
WaitForDoorbellReply(MPT_ADAPTER *ioc, int howlong)
{
	int u16cnt = 0;
	int failcnt = 0;
	int t;
	u16 *hs_reply = ioc->hs_reply;
 	volatile MPIDefaultReply_t *mptReply = (MPIDefaultReply_t *) ioc->hs_reply;
	u16 hword;

	hs_reply[0] = hs_reply[1] = hs_reply[7] = 0;

	/*
	 * Get first two u16's so we can look at IOC's intended reply MsgLength
	 */
	u16cnt=0;
	if ((t = WaitForDoorbellInt(ioc, howlong)) < 0) {
		failcnt++;
	} else {
		hs_reply[u16cnt++] = le16_to_cpu(CHIPREG_READ32(&ioc->chip->Doorbell) & 0x0000FFFF);
		CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);
		if ((t = WaitForDoorbellInt(ioc, 2)) < 0)
			failcnt++;
		else {
			hs_reply[u16cnt++] = le16_to_cpu(CHIPREG_READ32(&ioc->chip->Doorbell) & 0x0000FFFF);
			CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);
		}
	}

	dhsprintk((KERN_INFO MYNAM ": %s: First handshake reply word=%08x%s\n",
			ioc->name, le32_to_cpu(*(u32 *)hs_reply),
			failcnt ? " - MISSING DOORBELL HANDSHAKE!" : ""));

	/*
	 * If no error (and IOC said MsgLength is > 0), piece together
	 * reply 16 bits at a time.
	 */
	for (u16cnt=2; !failcnt && u16cnt < (2 * mptReply->MsgLength); u16cnt++) {
		if ((t = WaitForDoorbellInt(ioc, 2)) < 0)
			failcnt++;
		hword = le16_to_cpu(CHIPREG_READ32(&ioc->chip->Doorbell) & 0x0000FFFF);
		/* don't overflow our IOC hs_reply[] buffer! */
		if (u16cnt < sizeof(ioc->hs_reply) / sizeof(ioc->hs_reply[0]))
			hs_reply[u16cnt] = hword;
		CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);
	}

	if (!failcnt && (t = WaitForDoorbellInt(ioc, 2)) < 0)
		failcnt++;
	CHIPREG_WRITE32(&ioc->chip->IntStatus, 0);

	if (failcnt) {
		printk(KERN_ERR MYNAM ": %s: ERROR - Handshake reply failure!\n",
				ioc->name);
		return -failcnt;
	}
#if 0
	else if (u16cnt != (2 * mptReply->MsgLength)) {
		return -101;
	}
	else if ((mptReply->IOCStatus & MPI_IOCSTATUS_MASK) != MPI_IOCSTATUS_SUCCESS) {
		return -102;
	}
#endif

	dmfprintk((KERN_INFO MYNAM ": %s: Got Handshake reply:\n", ioc->name));
	DBG_DUMP_REPLY_FRAME(mptReply)

	dhsprintk((KERN_INFO MYNAM ": %s: WaitForDoorbell REPLY (sz=%d)\n",
			ioc->name, u16cnt/2));
	return u16cnt/2;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	GetLanConfigPages - Fetch LANConfig pages.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
GetLanConfigPages(MPT_ADAPTER *ioc)
{
	Config_t		 config_req;
	ConfigReply_t		 config_reply;
	LANPage0_t		*page0;
	dma_addr_t		 page0_dma;
	LANPage1_t		*page1;
	dma_addr_t		 page1_dma;
	int			 i;
	int			 req_sz;
	int			 reply_sz;
	int			 data_sz;

/* LANPage0 */
	/*  Immediate destination (reply area)...  */
	reply_sz = sizeof(config_reply);
	memset(&config_reply, 0, reply_sz);

	/*  Ultimate destination...  */
	page0 = &ioc->lan_cnfg_page0;
	data_sz = sizeof(*page0);
	memset(page0, 0, data_sz);

	/*  Request area (config_req on the stack right now!)  */
	req_sz = sizeof(config_req);
	memset(&config_req, 0, req_sz);
	config_req.Function = MPI_FUNCTION_CONFIG;
	config_req.Action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	/*	config_req.Header.PageVersion = 0;	*/
	/*	config_req.Header.PageLength = 0;	*/
	config_req.Header.PageNumber = 0;
	config_req.Header.PageType = MPI_CONFIG_PAGETYPE_LAN;
	/*	config_req.PageAddress = 0;		*/
	config_req.PageBufferSGE.u.Simple.FlagsLength = cpu_to_le32(
			((MPI_SGE_FLAGS_LAST_ELEMENT |
			  MPI_SGE_FLAGS_END_OF_BUFFER |
			  MPI_SGE_FLAGS_END_OF_LIST |
			  MPI_SGE_FLAGS_SIMPLE_ELEMENT |
			  MPI_SGE_FLAGS_SYSTEM_ADDRESS |
			  MPI_SGE_FLAGS_32_BIT_ADDRESSING |
			  MPI_SGE_FLAGS_32_BIT_CONTEXT) << MPI_SGE_FLAGS_SHIFT) |
			(u32)data_sz
	);
	page0_dma = pci_map_single(ioc->pcidev, page0, data_sz, PCI_DMA_FROMDEVICE);
	config_req.PageBufferSGE.u.Simple.u.Address32 = cpu_to_le32(page0_dma);

	dprintk((KERN_INFO MYNAM ": %s: Sending Config request LAN_PAGE_0\n",
			ioc->name));

	i = HandShakeReqAndReply(ioc, req_sz, (u32*)&config_req,
				reply_sz, (u16*)&config_reply, 3);
	pci_unmap_single(ioc->pcidev, page0_dma, data_sz, PCI_DMA_FROMDEVICE);
	if (i != 0)
		return i;

	/*  Now byte swap the necessary LANPage0 fields  */

/* LANPage1 */
	/*  Immediate destination (reply area)...  */
	reply_sz = sizeof(config_reply);
	memset(&config_reply, 0, reply_sz);

	/*  Ultimate destination...  */
	page1 = &ioc->lan_cnfg_page1;
	data_sz = sizeof(*page1);
	memset(page1, 0, data_sz);

	/*  Request area (config_req on the stack right now!)  */
	req_sz = sizeof(config_req);
	memset(&config_req, 0, req_sz);
	config_req.Function = MPI_FUNCTION_CONFIG;
	config_req.Action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	/*	config_req.Header.PageVersion = 0;	*/
	/*	config_req.Header.PageLength = 0;	*/
	config_req.Header.PageNumber = 1;
	config_req.Header.PageType = MPI_CONFIG_PAGETYPE_LAN;
	/*	config_req.PageAddress = 0;		*/
	config_req.PageBufferSGE.u.Simple.FlagsLength = cpu_to_le32(
			((MPI_SGE_FLAGS_LAST_ELEMENT |
			  MPI_SGE_FLAGS_END_OF_BUFFER |
			  MPI_SGE_FLAGS_END_OF_LIST |
			  MPI_SGE_FLAGS_SIMPLE_ELEMENT |
			  MPI_SGE_FLAGS_SYSTEM_ADDRESS |
			  MPI_SGE_FLAGS_32_BIT_ADDRESSING |
			  MPI_SGE_FLAGS_32_BIT_CONTEXT) << MPI_SGE_FLAGS_SHIFT) |
			(u32)data_sz
	);
	page1_dma = pci_map_single(ioc->pcidev, page1, data_sz, PCI_DMA_FROMDEVICE);
	config_req.PageBufferSGE.u.Simple.u.Address32 = cpu_to_le32(page1_dma);

	dprintk((KERN_INFO MYNAM ": %s: Sending Config request LAN_PAGE_1\n",
			ioc->name));

	i = HandShakeReqAndReply(ioc, req_sz, (u32*)&config_req,
				reply_sz, (u16*)&config_reply, 3);
	pci_unmap_single(ioc->pcidev, page1_dma, data_sz, PCI_DMA_FROMDEVICE);
	if (i != 0)
		return i;

	/*  Now byte swap the necessary LANPage1 fields  */

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	SendEventNotification - Send EventNotification (on or off) request
 *	to MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@EvSwitch: Event switch flags
 */
static int
SendEventNotification(MPT_ADAPTER *ioc, u8 EvSwitch)
{
	EventNotification_t	*evnp;

	evnp = (EventNotification_t *) mpt_get_msg_frame(mpt_base_index, ioc->id);
	if (evnp == NULL) {
		dprintk((KERN_WARNING MYNAM ": %s: WARNING - Unable to allocate a event request frame!\n",
				ioc->name));
		return 0;
	}
	memset(evnp, 0, sizeof(*evnp));

	dprintk((KERN_INFO MYNAM ": %s: Sending EventNotification(%d)\n", ioc->name, EvSwitch));

	evnp->Function = MPI_FUNCTION_EVENT_NOTIFICATION;
	evnp->ChainOffset = 0;
	evnp->MsgFlags = 0;
	evnp->Switch = EvSwitch;

	mpt_put_msg_frame(mpt_base_index, ioc->id, (MPT_FRAME_HDR *)evnp);

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	SendEventAck - Send EventAck request to MPT adapter.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@evnp: Pointer to original EventNotification request
 */
static int
SendEventAck(MPT_ADAPTER *ioc, EventNotificationReply_t *evnp)
{
	EventAck_t	*pAck;

	if ((pAck = (EventAck_t *) mpt_get_msg_frame(mpt_base_index, ioc->id)) == NULL) {
		printk(KERN_WARNING MYNAM ": %s: WARNING - Unable to allocate event ACK request frame!\n",
				ioc->name);
		return -1;
	}
	memset(pAck, 0, sizeof(*pAck));

	dprintk((KERN_INFO MYNAM ": %s: Sending EventAck\n", ioc->name));

	pAck->Function     = MPI_FUNCTION_EVENT_ACK;
	pAck->ChainOffset  = 0;
	pAck->MsgFlags     = 0;
	pAck->Event        = evnp->Event;
	pAck->EventContext = evnp->EventContext;

	mpt_put_msg_frame(mpt_base_index, ioc->id, (MPT_FRAME_HDR *)pAck);

	return 0;
}

#ifdef CONFIG_PROC_FS		/* { */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	procfs (%MPT_PROCFS_MPTBASEDIR/...) support stuff...
 */

#define PROC_MPT_READ_RETURN(page,start,off,count,eof,len) \
{ \
	len -= off;			\
	if (len < count) {		\
		*eof = 1;		\
		if (len <= 0)		\
			return 0;	\
	} else				\
		len = count;		\
	*start = page + off;		\
	return len;			\
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	procmpt_create - Create %MPT_PROCFS_MPTBASEDIR entries.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
procmpt_create(void)
{
	MPT_ADAPTER *ioc;
	struct proc_dir_entry *ent;
	int errcnt = 0;

	/*
	 * 	BEWARE: If/when MPT_PROCFS_MPTBASEDIR changes from "mpt"
	 * 	(single level) to multi level (e.g. "driver/message/fusion")
	 * 	something here needs to change.  -sralston
	 */
	procmpt_root_dir = CREATE_PROCDIR_ENTRY(MPT_PROCFS_MPTBASEDIR, NULL);
	if (procmpt_root_dir == NULL)
		return -ENOTDIR;

	if ((ioc = mpt_adapter_find_first()) != NULL) {
		ent = create_proc_read_entry(MPT_PROCFS_SUMMARY_NODE, 0, NULL, procmpt_read_summary, NULL);
		if (ent == NULL) {
			printk(KERN_WARNING MYNAM ": WARNING - Could not create %s entry!\n",
					MPT_PROCFS_SUMMARY_PATHNAME);
			errcnt++;
		}
	}

	while (ioc != NULL) {
		char pname[32];
		int namelen;
		/*
		 *  Create "/proc/mpt/iocN" subdirectory entry for each MPT adapter.
		 */
		namelen = sprintf(pname, MPT_PROCFS_MPTBASEDIR "/%s", ioc->name);
		if ((ent = CREATE_PROCDIR_ENTRY(pname, NULL)) != NULL) {
			/*
			 *  And populate it with: "summary" and "dbg" file entries.
			 */
			(void) sprintf(pname+namelen, "/summary");
			ent = create_proc_read_entry(pname, 0, NULL, procmpt_read_summary, ioc);
			if (ent == NULL) {
				errcnt++;
				printk(KERN_WARNING MYNAM ": %s: WARNING - Could not create /proc/%s entry!\n",
						ioc->name, pname);
			}
//#ifdef MPT_DEBUG
			/* DEBUG aid! */
			(void) sprintf(pname+namelen, "/dbg");
			ent = create_proc_read_entry(pname, 0, NULL, procmpt_read_dbg, ioc);
			if (ent == NULL) {
				errcnt++;
				printk(KERN_WARNING MYNAM ": %s: WARNING - Could not create /proc/%s entry!\n",
						ioc->name, pname);
			}
//#endif
		} else {
			errcnt++;
			printk(KERN_WARNING MYNAM ": %s: WARNING - Could not create /proc/%s entry!\n",
					ioc->name, pname);

		}

		ioc = mpt_adapter_find_next(ioc);
	}

	if (errcnt) {
//		remove_proc_entry("mpt", 0);
		return -ENOTDIR;
	}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	procmpt_destroy - Tear down %MPT_PROCFS_MPTBASEDIR entries.
 *
 *	Returns 0 for success, non-zero for failure.
 */
static int
procmpt_destroy(void)
{
	MPT_ADAPTER *ioc;

	if (!procmpt_root_dir)
		return 0;

	/*
	 * 	BEWARE: If/when MPT_PROCFS_MPTBASEDIR changes from "mpt"
	 * 	(single level) to multi level (e.g. "driver/message/fusion")
	 * 	something here needs to change.  -sralston
	 */

	ioc = mpt_adapter_find_first();
	if (ioc != NULL) {
		remove_proc_entry(MPT_PROCFS_SUMMARY_NODE, 0);
	}

	while (ioc != NULL) {
		char pname[32];
		int namelen;
		/*
		 *  Tear down each "/proc/mpt/iocN" subdirectory.
		 */
		namelen = sprintf(pname, MPT_PROCFS_MPTBASEDIR "/%s", ioc->name);
		(void) sprintf(pname+namelen, "/summary");
		remove_proc_entry(pname, 0);
//#ifdef MPT_DEBUG
		(void) sprintf(pname+namelen, "/dbg");
		remove_proc_entry(pname, 0);
//#endif
		(void) sprintf(pname, MPT_PROCFS_MPTBASEDIR "/%s", ioc->name);
		remove_proc_entry(pname, 0);

		ioc = mpt_adapter_find_next(ioc);
	}

	if (atomic_read((atomic_t *)&procmpt_root_dir->count) == 0) {
		remove_proc_entry(MPT_PROCFS_MPTBASEDIR, 0);
		procmpt_root_dir = NULL;
		return 0;
	}

	return -1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	procmpt_read_summary - Handle read request from /proc/mpt/summary
 *	or from /proc/mpt/iocN/summary.
 *	@page: Pointer to area to write information
 *	@start: Pointer to start pointer
 *	@off: Offset to start writing
 *	@count: 
 *	@eof: Pointer to EOF integer
 *	@data: Pointer 
 *
 *	Returns numbers of characters written to process performing the read.
 */
static int
procmpt_read_summary(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	MPT_ADAPTER *ioc;
	char *out = page;
	int len;

	if (data == NULL)
		ioc = mpt_adapter_find_first();
	else
		ioc = data;

// Too verbose!
//	out += sprintf(out, "Attached Fusion MPT I/O Controllers:%s\n", ioc ? "" : " none");

	while (ioc) {
		int	more = 0;

// Too verbose!
//		mpt_print_ioc_facts(ioc, out, &more, 0);
		mpt_print_ioc_summary(ioc, out, &more, 0, 1);

		out += more;
		if ((out-page) >= count) {
			break;
		}

		if (data == NULL)
			ioc = mpt_adapter_find_next(ioc);
		else
			ioc = NULL;				/* force exit for iocN */
	}
	len = out - page;

	PROC_MPT_READ_RETURN(page,start,off,count,eof,len);
}

// debug aid!
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	procmpt_read_dbg - Handle read request from /proc/mpt/iocN/dbg.
 *	@page: Pointer to area to write information
 *	@start: Pointer to start pointer
 *	@off: Offset to start writing
 *	@count: 
 *	@eof: Pointer to EOF integer
 *	@data: Pointer 
 *
 *	Returns numbers of characters written to process performing the read.
 */
static int
procmpt_read_dbg(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	MPT_ADAPTER *ioc;
	char *out = page;
	int len;

	ioc = data;

	while (ioc) {
		int	more = 0;

		mpt_print_ioc_facts(ioc, out, &more, 0);

		out += more;
		if ((out-page) >= count) {
			break;
		}
		ioc = NULL;
	}
	len = out - page;

	PROC_MPT_READ_RETURN(page,start,off,count,eof,len);
}
#endif		/* CONFIG_PROC_FS } */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static void
mpt_get_fw_exp_ver(char *buf, MPT_ADAPTER *ioc)
{
	if ((ioc->facts.FWVersion & 0xF000) == 0xE000)
		sprintf(buf, " (Exp %02d%02d)",
			(ioc->facts.FWVersion & 0x0F00) >> 8,	/* Month */
			ioc->facts.FWVersion & 0x001F);		/* Day */
	else
		buf[0] ='\0';

	/* insider hack! */
	if (ioc->facts.FWVersion & 0x0080) {
		strcat(buf, " [MDBG]");
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_print_ioc_summary - Write ASCII summary of IOC to a buffer.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@buffer: Pointer to buffer where IOC summary info should be written
 *	@size: Pointer to number of bytes we wrote (set by this routine)
 *	@len: Offset at which to start writing in buffer
 *	@showlan: Display LAN stuff?
 *
 * 	This routine writes (english readable) ASCII text, which represents
 * 	a summary of IOC information, to a buffer.
 */
void
mpt_print_ioc_summary(MPT_ADAPTER *ioc, char *buffer, int *size, int len, int showlan)
{
	char expVer[32];
	int y;

	mpt_get_fw_exp_ver(expVer, ioc);

	/*
	 *  Shorter summary of attached ioc's...
	 */
	y = sprintf(buffer+len, "%s: %s, %s%04xh%s, Ports=%d, MaxQ=%d",
			ioc->name,
			ioc->prod_name,
			MPT_FW_REV_MAGIC_ID_STRING,	/* "FwRev=" or somesuch */
			ioc->facts.FWVersion,
			expVer,
			ioc->facts.NumberOfPorts,
			ioc->req_depth);

	if (showlan && (ioc->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_LAN)) {
		u8 *a = (u8*)&ioc->lan_cnfg_page1.HardwareAddressLow;
		y += sprintf(buffer+len+y, ", LanAddr=%02X:%02X:%02X:%02X:%02X:%02X",
			a[5], a[4], a[3], a[2], a[1], a[0]);
	}

	if (ioc->pci_irq < 100)
		y += sprintf(buffer+len+y, ", IRQ=%d", ioc->pci_irq);

	if (!ioc->active)
		y += sprintf(buffer+len+y, " (disabled)");

	y += sprintf(buffer+len+y, "\n");

	*size = y;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_print_ioc_facts - Write ASCII summary of IOC facts to a buffer.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@buffer: Pointer to buffer where IOC facts should be written
 *	@size: Pointer to number of bytes we wrote (set by this routine)
 *	@len: Offset at which to start writing in buffer
 *
 * 	This routine writes (english readable) ASCII text, which represents
 * 	a summary of the IOC facts, to a buffer.
 */
void
mpt_print_ioc_facts(MPT_ADAPTER *ioc, char *buffer, int *size, int len)
{
	char expVer[32];
	char iocName[16];
	int sz;
	int y;
	int p;

	mpt_get_fw_exp_ver(expVer, ioc);

	strcpy(iocName, ioc->name);
	y = sprintf(buffer+len, "%s:\n", iocName);

	y += sprintf(buffer+len+y, "  ProductID = 0x%04x\n", ioc->facts.ProductID);
	for (p=0; p < ioc->facts.NumberOfPorts; p++) {
		y += sprintf(buffer+len+y, "  PortNumber = %d (of %d)\n",
			p+1,
			ioc->facts.NumberOfPorts);
		if (ioc->pfacts[p].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_LAN) {
			u8 *a = (u8*)&ioc->lan_cnfg_page1.HardwareAddressLow;
			y += sprintf(buffer+len+y, "  LanAddr = 0x%02x:%02x:%02x:%02x:%02x:%02x\n",
				a[5], a[4], a[3], a[2], a[1], a[0]);
		}
	}
	y += sprintf(buffer+len+y, "  FWVersion = 0x%04x%s\n", ioc->facts.FWVersion, expVer);
	y += sprintf(buffer+len+y, "  MsgVersion = 0x%04x\n", ioc->facts.MsgVersion);
	y += sprintf(buffer+len+y, "  FirstWhoInit = 0x%02x\n", ioc->FirstWhoInit);
	y += sprintf(buffer+len+y, "  EventState = 0x%02x\n", ioc->facts.EventState);
	y += sprintf(buffer+len+y, "  CurrentHostMfaHighAddr = 0x%08x\n",
		 	ioc->facts.CurrentHostMfaHighAddr);
	y += sprintf(buffer+len+y, "  CurrentSenseBufferHighAddr = 0x%08x\n",
			ioc->facts.CurrentSenseBufferHighAddr);
	y += sprintf(buffer+len+y, "  MaxChainDepth = 0x%02x frames\n", ioc->facts.MaxChainDepth);
	y += sprintf(buffer+len+y, "  MinBlockSize = 0x%02x bytes\n", 4*ioc->facts.BlockSize);

	y += sprintf(buffer+len+y, "  RequestFrames @ 0x%p (Dma @ 0x%08x)\n",
					ioc->req_alloc, ioc->req_alloc_dma);
	/*
	 *  Rounding UP to nearest 4-kB boundary here...
	 */
	sz = (ioc->req_sz * ioc->req_depth) + 128;
	sz = ((sz + 0x1000UL - 1UL) / 0x1000) * 0x1000;
	y += sprintf(buffer+len+y, "    {CurReqSz=%d} x {CurReqDepth=%d} = %d bytes ^= 0x%x\n",
					ioc->req_sz, ioc->req_depth, ioc->req_sz*ioc->req_depth, sz);
	y += sprintf(buffer+len+y, "    {MaxReqSz=%d}   {MaxReqDepth=%d}\n",
					4*ioc->facts.RequestFrameSize,
					ioc->facts.GlobalCredits);

	y += sprintf(buffer+len+y, "  ReplyFrames   @ 0x%p (Dma @ 0x%08x)\n",
					ioc->reply_alloc, ioc->reply_alloc_dma);
	sz = (ioc->reply_sz * ioc->reply_depth) + 128;
	y += sprintf(buffer+len+y, "    {CurRepSz=%d} x {CurRepDepth=%d} = %d bytes ^= 0x%x\n",
					ioc->reply_sz, ioc->reply_depth, ioc->reply_sz*ioc->reply_depth, sz);
	y += sprintf(buffer+len+y, "    {MaxRepSz=%d}   {MaxRepDepth=%d}\n",
					ioc->facts.CurReplyFrameSize,
					ioc->facts.ReplyQueueDepth);

	*size = y;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static char *
EventDescriptionStr(u8 event, u32 evData0)
{
	char *ds = NULL;

	switch(event) {
	case MPI_EVENT_NONE:
		ds = "None";
		break;
	case MPI_EVENT_LOG_DATA:
		ds = "Log Data";
		break;
	case MPI_EVENT_STATE_CHANGE:
		ds = "State Change";
		break;
	case MPI_EVENT_UNIT_ATTENTION:
		ds = "Unit Attention";
		break;
	case MPI_EVENT_IOC_BUS_RESET:
		ds = "IOC Bus Reset";
		break;
	case MPI_EVENT_EXT_BUS_RESET:
		ds = "External Bus Reset";
		break;
	case MPI_EVENT_RESCAN:
		ds = "Bus Rescan Event"; 
		/* Ok, do we need to do anything here? As far as
		   I can tell, this is when a new device gets added
		   to the loop. */
		break;
	case MPI_EVENT_LINK_STATUS_CHANGE:
		if (evData0 == MPI_EVENT_LINK_STATUS_FAILURE)
			ds = "Link Status(FAILURE) Change";
		else
			ds = "Link Status(ACTIVE) Change";
		break;
	case MPI_EVENT_LOOP_STATE_CHANGE:
		if (evData0 == MPI_EVENT_LOOP_STATE_CHANGE_LIP)
			ds = "Loop State(LIP) Change";
		else if (evData0 == MPI_EVENT_LOOP_STATE_CHANGE_LPE)
			ds = "Loop State(LPE) Change";			/* ??? */
		else
			ds = "Loop State(LPB) Change";			/* ??? */
		break;
	case MPI_EVENT_LOGOUT:
		ds = "Logout";
		break;
	case MPI_EVENT_EVENT_CHANGE:
		if (evData0)
			ds = "Events(ON) Change";
		else
			ds = "Events(OFF) Change";
		break;
	/*
	 *  MPT base "custom" events may be added here...
	 */
	default:
		ds = "Unknown";
		break;
	}
	return ds;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	ProcessEventNotification - Route a received EventNotificationReply to
 *	all currently regeistered event handlers.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@pEventReply: Pointer to EventNotification reply frame
 *	@evHandlers: Pointer to integer, number of event handlers
 *
 *	Returns sum of event handlers return values.
 */
static int
ProcessEventNotification(MPT_ADAPTER *ioc, EventNotificationReply_t *pEventReply, int *evHandlers)
{
	u16 evDataLen;
	u32 evData0 = 0;
//	u32 evCtx;
	int i;
	int r = 0;
	int handlers = 0;
	char *evStr;
	u8 event;

	/*
	 *  Do platform normalization of values
	 */
	event = le32_to_cpu(pEventReply->Event) & 0xFF;
//	evCtx = le32_to_cpu(pEventReply->EventContext);
	evDataLen = le16_to_cpu(pEventReply->EventDataLength);
	if (evDataLen) {
		evData0 = le32_to_cpu(pEventReply->Data[0]);
	}

	evStr = EventDescriptionStr(event, evData0);
	dprintk((KERN_INFO MYNAM ": %s: MPT event (%s=%02Xh) detected!\n",
			ioc->name,
			evStr,
			event));

#if defined(MPT_DEBUG) || defined(MPT_DEBUG_EVENTS)
	printk(KERN_INFO MYNAM ": Event data:\n" KERN_INFO);
	for (i = 0; i < evDataLen; i++)
		printk(" %08x", le32_to_cpu(pEventReply->Data[i]));
	printk("\n");
#endif

	/*
	 *  Do general / base driver event processing
	 */
	switch(event) {
	case MPI_EVENT_NONE:			/* 00 */
	case MPI_EVENT_LOG_DATA:		/* 01 */
	case MPI_EVENT_STATE_CHANGE:		/* 02 */
	case MPI_EVENT_UNIT_ATTENTION:		/* 03 */
	case MPI_EVENT_IOC_BUS_RESET:		/* 04 */
	case MPI_EVENT_EXT_BUS_RESET:		/* 05 */
	case MPI_EVENT_RESCAN:			/* 06 */
	case MPI_EVENT_LINK_STATUS_CHANGE:	/* 07 */
	case MPI_EVENT_LOOP_STATE_CHANGE:	/* 08 */
	case MPI_EVENT_LOGOUT:			/* 09 */
	default:
		break;
	case MPI_EVENT_EVENT_CHANGE:		/* 0A */
		if (evDataLen) {
			u8 evState = evData0 & 0xFF;

			/* CHECKME! What if evState unexpectedly says OFF (0)? */

			/* Update EventState field in cached IocFacts */
			if (ioc->facts.Function) {
				ioc->facts.EventState = evState;
			}
		}
		break;
	}

	/*
	 *  Call each currently registered protocol event handler.
	 */
	for (i=MPT_MAX_PROTOCOL_DRIVERS-1; i; i--) {
		if (MptEvHandlers[i]) {
			dprintk((KERN_INFO MYNAM ": %s: Routing Event to event handler #%d\n",
					ioc->name, i));
			r += (*(MptEvHandlers[i]))(ioc, pEventReply);
			handlers++;
		}
	}
	/* FIXME?  Examine results here? */

	/*
	 *  If needed, send (a single) EventAck.
	 */
	if (pEventReply->AckRequired == MPI_EVENT_NOTIFICATION_ACK_REQUIRED) {
		if ((i = SendEventAck(ioc, pEventReply)) != 0) {
		}
	}

	*evHandlers = handlers;
	return r;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mpt_fc_log_info - Log information returned from Fibre Channel IOC.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@log_info: U32 LogInfo reply word from the IOC
 *
 *	Refer to lsi/fc_log.h.
 */
static void
mpt_fc_log_info(MPT_ADAPTER *ioc, u32 log_info)
{
	static char *subcl_str[8] = {
		"FCP Initiator", "FCP Target", "LAN", "MPI Message Layer",
		"FC Link", "Context Manager", "Invalid Field Offset", "State Change Info"
	};
	char *desc = "unknown";
	u8 subcl = (log_info >> 24) & 0x7;
	u32 SubCl = log_info & 0x27000000;

	switch(log_info) {
/* FCP Initiator */
    	case MPI_IOCLOGINFO_FC_INIT_ERROR_OUT_OF_ORDER_FRAME:
		desc = "Received an out of order frame - unsupported";
		break;
	case MPI_IOCLOGINFO_FC_INIT_ERROR_BAD_START_OF_FRAME:
		desc = "Bad start of frame primative";
		break;
	case MPI_IOCLOGINFO_FC_INIT_ERROR_BAD_END_OF_FRAME:
		desc = "Bad end of frame primative";
		break;
	case MPI_IOCLOGINFO_FC_INIT_ERROR_OVER_RUN:
		desc = "Receiver hardware detected overrun";
		break;
	case MPI_IOCLOGINFO_FC_INIT_ERROR_RX_OTHER:
		desc = "Other errors caught by IOC which require retries";
		break;
	case MPI_IOCLOGINFO_FC_INIT_ERROR_SUBPROC_DEAD:
		desc = "Main processor could not initialize sub-processor";
		break;
/* FC Target */
	case MPI_IOCLOGINFO_FC_TARGET_NO_PDISC:
		desc = "Not sent because we are waiting for a PDISC from the initiator";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_NO_LOGIN:
		desc = "Not sent because we are not logged in to the remote node";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_DOAR_KILLED_BY_LIP:
		desc = "Data Out, Auto Response, not sent due to a LIP";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_DIAR_KILLED_BY_LIP:
		desc = "Data In, Auto Response, not sent due to a LIP";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_DIAR_MISSING_DATA:
		desc = "Data In, Auto Response, missing data frames";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_DONR_KILLED_BY_LIP:
		desc = "Data Out, No Response, not sent due to a LIP";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_WRSP_KILLED_BY_LIP:
		desc = "Auto-response after a write not sent due to a LIP";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_DINR_KILLED_BY_LIP:
		desc = "Data In, No Response, not completed due to a LIP";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_DINR_MISSING_DATA:
		desc = "Data In, No Response, missing data frames";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_MRSP_KILLED_BY_LIP:
		desc = "Manual Response not sent due to a LIP";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_NO_CLASS_3:
		desc = "Not sent because remote node does not support Class 3";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_LOGIN_NOT_VALID:
		desc = "Not sent because login to remote node not validated";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_FROM_OUTBOUND:
		desc = "Cleared from the outbound after a logout";
		break;
	case MPI_IOCLOGINFO_FC_TARGET_WAITING_FOR_DATA_IN:
		desc = "Cleared waiting for data after a logout";
		break;
/* LAN */
	case MPI_IOCLOGINFO_FC_LAN_TRANS_SGL_MISSING:
		desc = "Transaction Context Sgl Missing";
		break;
	case MPI_IOCLOGINFO_FC_LAN_TRANS_WRONG_PLACE:
		desc = "Transaction Context found before an EOB";
		break;
	case MPI_IOCLOGINFO_FC_LAN_TRANS_RES_BITS_SET:
		desc = "Transaction Context value has reserved bits set";
		break;
	case MPI_IOCLOGINFO_FC_LAN_WRONG_SGL_FLAG:
		desc = "Invalid SGL Flags";
		break;
/* FC Link */
	case MPI_IOCLOGINFO_FC_LINK_LOOP_INIT_TIMEOUT:
		desc = "Loop initialization timed out";
		break;
	case MPI_IOCLOGINFO_FC_LINK_ALREADY_INITIALIZED:
		desc = "Another system controller already initialized the loop";
		break;
	case MPI_IOCLOGINFO_FC_LINK_LINK_NOT_ESTABLISHED:
		desc = "Not synchronized to signal or still negotiating (possible cable problem)";
		break;
	case MPI_IOCLOGINFO_FC_LINK_CRC_ERROR:
		desc = "CRC check detected error on received frame";
		break;
	}

	printk(KERN_INFO MYNAM ": %s: LogInfo(0x%08x): SubCl={%s}",
			ioc->name, log_info, subcl_str[subcl]);
	if (SubCl == MPI_IOCLOGINFO_FC_INVALID_FIELD_BYTE_OFFSET)
		printk(", byte_offset=%d\n", log_info & MPI_IOCLOGINFO_FC_INVALID_FIELD_MAX_OFFSET);
	else if (SubCl == MPI_IOCLOGINFO_FC_STATE_CHANGE)
		printk("\n");		/* StateChg in LogInfo & 0x00FFFFFF, above */
	else
		printk("\n" KERN_INFO " %s\n", desc);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mpt_sp_log_info - Log information returned from SCSI Parallel IOC.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@mr: Pointer to MPT reply frame
 *	@log_info: U32 LogInfo word from the IOC
 *
 *	Refer to lsi/sp_log.h.
 */
static void
mpt_sp_log_info(MPT_ADAPTER *ioc, u32 log_info)
{
	/* FIXME! */
	printk(KERN_INFO MYNAM ": %s: LogInfo(0x%08x)\n", ioc->name, log_info);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_register_ascqops_strings - Register SCSI ASC/ASCQ and SCSI
 *	OpCode strings from the (optional) isense module.
 *	@ascqTable: Pointer to ASCQ_Table_t structure
 *	@ascqtbl_sz: Number of entries in ASCQ_Table
 *	@opsTable: Pointer to array of SCSI OpCode strings (char pointers)
 *
 *	Specialized driver registration routine for the isense driver.
 */
int
mpt_register_ascqops_strings(/*ASCQ_Table_t*/void *ascqTable, int ascqtbl_sz, const char **opsTable)
{
	int r = 0;

	if (ascqTable && ascqtbl_sz && opsTable) {
		mpt_v_ASCQ_TablePtr = ascqTable;
		mpt_ASCQ_TableSz = ascqtbl_sz;
		mpt_ScsiOpcodesPtr = opsTable;
		printk(KERN_INFO MYNAM ": English readable SCSI-3 strings enabled:-)\n");
		r = 1;
	}
	MOD_INC_USE_COUNT;
	return r;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mpt_deregister_ascqops_strings - Deregister SCSI ASC/ASCQ and SCSI
 *	OpCode strings from the isense driver.
 *
 *	Specialized driver deregistration routine for the isense driver.
 */
void
mpt_deregister_ascqops_strings(void)
{
	mpt_v_ASCQ_TablePtr = NULL;
	mpt_ASCQ_TableSz = 0;
	mpt_ScsiOpcodesPtr = NULL;
	printk(KERN_INFO MYNAM ": English readable SCSI-3 strings disabled)-:\n");
	MOD_DEC_USE_COUNT;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

EXPORT_SYMBOL(mpt_register);
EXPORT_SYMBOL(mpt_deregister);
EXPORT_SYMBOL(mpt_event_register);
EXPORT_SYMBOL(mpt_event_deregister);
EXPORT_SYMBOL(mpt_reset_register);
EXPORT_SYMBOL(mpt_reset_deregister);
EXPORT_SYMBOL(mpt_get_msg_frame);
EXPORT_SYMBOL(mpt_put_msg_frame);
EXPORT_SYMBOL(mpt_free_msg_frame);
EXPORT_SYMBOL(mpt_send_handshake_request);
EXPORT_SYMBOL(mpt_adapter_find_first);
EXPORT_SYMBOL(mpt_adapter_find_next);
EXPORT_SYMBOL(mpt_verify_adapter);
EXPORT_SYMBOL(mpt_print_ioc_summary);
EXPORT_SYMBOL(mpt_lan_index);
EXPORT_SYMBOL(mpt_stm_index);

EXPORT_SYMBOL(mpt_register_ascqops_strings);
EXPORT_SYMBOL(mpt_deregister_ascqops_strings);
EXPORT_SYMBOL(mpt_v_ASCQ_TablePtr);
EXPORT_SYMBOL(mpt_ASCQ_TableSz);
EXPORT_SYMBOL(mpt_ScsiOpcodesPtr);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	fusion_init - Fusion MPT base driver initialization routine.
 *
 *	Returns 0 for success, non-zero for failure.
 */
int __init fusion_init(void)
{
	int i;

	if (FusionInitCalled++) {
		dprintk((KERN_INFO MYNAM ": INFO - Driver late-init entry point called\n"));
		return 0;
	}

	show_mptmod_ver(my_NAME, my_VERSION);
	printk(KERN_INFO COPYRIGHT "\n");

	Q_INIT(&MptAdapters, MPT_ADAPTER);			/* set to empty */
	for (i = 0; i < MPT_MAX_PROTOCOL_DRIVERS; i++) {
		MptCallbacks[i] = NULL;
		MptDriverClass[i] = MPTUNKNOWN_DRIVER;
		MptEvHandlers[i] = NULL;
		MptResetHandlers[i] = NULL;
	}

	/* NEW!  20010120 -sralston
	 *  Register ourselves (mptbase) in order to facilitate
	 *  EventNotification handling.
	 */
	mpt_base_index = mpt_register(mpt_base_reply, MPTBASE_DRIVER);

	if ((i = mpt_pci_scan()) < 0)
		return i;

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	fusion_exit - Perform driver unload cleanup.
 *
 *	This routine frees all resources associated with each MPT adapter
 *	and removes all %MPT_PROCFS_MPTBASEDIR entries.
 */
static void fusion_exit(void)
{
	MPT_ADAPTER *this;

	dprintk((KERN_INFO MYNAM ": fusion_exit() called!\n"));

	/* Whups?  20010120 -sralston
	 *  Moved this *above* removal of all MptAdapters!
	 */
#ifdef CONFIG_PROC_FS
	procmpt_destroy();
#endif

	while (! Q_IS_EMPTY(&MptAdapters)) {
		this = MptAdapters.head;
		Q_DEL_ITEM(this);
		mpt_adapter_dispose(this);
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

module_init(fusion_init);
module_exit(fusion_exit);
