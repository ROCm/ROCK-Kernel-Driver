/*
 *  linux/drivers/message/fusion/mptscsih.c
 *      High performance SCSI / Fibre Channel SCSI Host device driver.
 *      For use with PCI chip/adapter(s):
 *          LSIFC9xx/LSI409xx Fibre Channel
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Credits:
 *      This driver would not exist if not for Alan Cox's development
 *      of the linux i2o driver.
 *
 *      A huge debt of gratitude is owed to David S. Miller (DaveM)
 *      for fixing much of the stupid and broken stuff in the early
 *      driver while porting to sparc64 platform.  THANK YOU!
 *
 *      (see mptbase.c)
 *
 *  Copyright (c) 1999-2001 LSI Logic Corporation
 *  Original author: Steven J. Ralston
 *  (mailto:Steve.Ralston@lsil.com)
 *
 *  $Id: mptscsih.c,v 1.29.4.1 2001/09/18 03:22:30 sralston Exp $
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

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kdev_t.h>
#include <linux/blkdev.h>
#include <linux/blk.h>		/* for io_request_lock (spinlock) decl */
#include "../../scsi/scsi.h"
#include "../../scsi/hosts.h"
#include "../../scsi/sd.h"

#include "mptbase.h"
#include "mptscsih.h"
#include "isense.h"

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#define my_NAME		"Fusion MPT SCSI Host driver"
#define my_VERSION	MPT_LINUX_VERSION_COMMON
#define MYNAM		"mptscsih"

MODULE_AUTHOR(MODULEAUTHOR);
MODULE_DESCRIPTION(my_NAME);
MODULE_LICENSE("GPL");


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

typedef struct _BIG_SENSE_BUF {
	u8		data[256];
} BIG_SENSE_BUF;

typedef struct _MPT_SCSI_HOST {
	MPT_ADAPTER		 *ioc;
	int			  port;
	struct scsi_cmnd	**ScsiLookup;
	u8			 *SgHunks;
	dma_addr_t		  SgHunksDMA;
	u32			  qtag_tick;
} MPT_SCSI_HOST;

typedef struct _MPT_SCSI_DEV {
	struct _MPT_SCSI_DEV	 *forw;
	struct _MPT_SCSI_DEV	 *back;
	MPT_ADAPTER		 *ioc;
	int			  sense_sz;
	BIG_SENSE_BUF		  CachedSense;
	unsigned long		  io_cnt;
	unsigned long		  read_cnt;
} MPT_SCSI_DEV;

/*
 *  Other private/forward protos...
 */

static int	mptscsih_io_done(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *r);
static void	mptscsih_report_queue_full(Scsi_Cmnd *sc, SCSIIOReply_t *pScsiReply, SCSIIORequest_t *pScsiReq);
static int	mptscsih_taskmgmt_complete(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *r);
static int	mptscsih_io_direction(Scsi_Cmnd *cmd);
static void	copy_sense_data(Scsi_Cmnd *sc, MPT_SCSI_HOST *hd, MPT_FRAME_HDR *mf, SCSIIOReply_t *pScsiReply);
static u32	SCPNT_TO_MSGCTX(Scsi_Cmnd *sc);

static int	mptscsih_ioc_reset(MPT_ADAPTER *ioc, int post_reset);
static int	mptscsih_event_process(MPT_ADAPTER *ioc, EventNotificationReply_t *pEvReply);


static int	mpt_scsi_hosts = 0;
static atomic_t	queue_depth;

static int	ScsiDoneCtx = -1;
static int	ScsiTaskCtx = -1;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,28)
static struct proc_dir_entry proc_mpt_scsihost =
{
	low_ino:	PROC_SCSI_MPT,
	namelen:	8,
	name:		"mptscsih",
	mode:		S_IFDIR | S_IRUGO | S_IXUGO,
	nlink:		2,
};
#endif

#define SNS_LEN(scp)  sizeof((scp)->sense_buffer)

#ifndef MPT_SCSI_USE_NEW_EH
/*
 *  Stuff to handle single-threading SCSI TaskMgmt
 *  (abort/reset) requests...
 */
static spinlock_t mpt_scsih_taskQ_lock = SPIN_LOCK_UNLOCKED;
static MPT_Q_TRACKER mpt_scsih_taskQ = {
	(MPT_FRAME_HDR*) &mpt_scsih_taskQ,
	(MPT_FRAME_HDR*) &mpt_scsih_taskQ
};
static int mpt_scsih_taskQ_cnt = 0;
static int mpt_scsih_taskQ_bh_active = 0;
static MPT_FRAME_HDR *mpt_scsih_active_taskmgmt_mf = NULL;
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_io_done - Main SCSI IO callback routine registered to
 *	Fusion MPT (base) driver
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@mf: Pointer to original MPT request frame
 *	@r: Pointer to MPT reply frame (NULL if TurboReply)
 *
 *	This routine is called from mpt.c::mpt_interrupt() at the completion
 *	of any SCSI IO request.
 *	This routine is registered with the Fusion MPT (base) driver at driver
 *	load/init time via the mpt_register() API call.
 *
 *	Returns 1 indicating alloc'd request frame ptr should be freed.
 */
static int
mptscsih_io_done(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *r)
{
	Scsi_Cmnd	*sc;
	MPT_SCSI_HOST	*hd;
	MPT_SCSI_DEV	*mpt_sdev = NULL;
	u16		 req_idx;

	if ((mf == NULL) ||
	    (mf >= MPT_INDEX_2_MFPTR(ioc, ioc->req_depth))) {
		printk(KERN_ERR MYNAM ": ERROR! NULL or BAD req frame ptr (=%p)!\n", mf);
		return 1;
	}

	hd = (MPT_SCSI_HOST *) ioc->sh->hostdata;
	req_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);
	sc = hd->ScsiLookup[req_idx];
	hd->ScsiLookup[req_idx] = NULL;

	dmfprintk((KERN_INFO MYNAM ": ScsiDone (req:sc:reply=%p:%p:%p)\n", mf, sc, r));

	atomic_dec(&queue_depth);

	/*
	 *  Check for {1st} {IO} completion to "new" device.
	 *  How do we know it's a new device?
	 *  If we haven't set SDpnt->hostdata I guess...
	 */
	if (sc && sc->device) {
		mpt_sdev = (MPT_SCSI_DEV*)sc->device->hostdata;
		if (!mpt_sdev) {
			dprintk((KERN_INFO MYNAM ": *NEW* SCSI device (%d:%d:%d)!\n",
					   sc->device->id, sc->device->lun, sc->device->channel));
			if ((sc->device->hostdata = kmalloc(sizeof(MPT_SCSI_DEV), GFP_ATOMIC)) == NULL) {
				printk(KERN_ERR MYNAM ": ERROR - kmalloc(%d) FAILED!\n", (int)sizeof(MPT_SCSI_DEV));
			} else {
				memset(sc->device->hostdata, 0, sizeof(MPT_SCSI_DEV));
				mpt_sdev = (MPT_SCSI_DEV *) sc->device->hostdata;
				mpt_sdev->ioc = ioc;
			}
		} else {
			if (++mpt_sdev->io_cnt && mptscsih_io_direction(sc) < 0) {
				if (++mpt_sdev->read_cnt == 3) {
					dprintk((KERN_INFO MYNAM ": 3rd DATA_IN, CDB[0]=%02x\n",
							sc->cmnd[0]));
				}
			}
#if 0
			if (mpt_sdev->sense_sz) {
				/*
				 *  Completion of first IO down this path
				 *  *should* invalidate device SenseData...
				 */
				mpt_sdev->sense_sz = 0;
			}
#endif
		}
	}

#if 0
{
	MPT_FRAME_HDR	*mf_chk;

	/* This, I imagine, is a costly check, but...
	 *  If abort/reset active, check to see if this is a IO
	 *  that completed while ABORT/RESET for it is waiting
	 *  on our taskQ!
	 */
	if (! Q_IS_EMPTY(&mpt_scsih_taskQ)) {
		/* If ABORT for this IO is queued, zap it! */
		mf_chk = search_taskQ(1,sc,MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK);
		if (mf_chk != NULL) {
			sc->result = DID_ABORT << 16;
			spin_lock_irqsave(&io_request_lock, flags);
			sc->scsi_done(sc);
			spin_unlock_irqrestore(&io_request_lock, flags);
			return 1;
		}
	}
}
#endif

	if (r != NULL && sc != NULL) {
		SCSIIOReply_t	*pScsiReply;
		SCSIIORequest_t *pScsiReq;
		u16		 status;

		pScsiReply = (SCSIIOReply_t *) r;
		pScsiReq = (SCSIIORequest_t *) mf;

		status = le16_to_cpu(pScsiReply->IOCStatus) & MPI_IOCSTATUS_MASK;

		dprintk((KERN_NOTICE MYNAM ": Uh-Oh!  (req:sc:reply=%p:%p:%p)\n", mf, sc, r));
		dprintk((KERN_NOTICE "  IOCStatus=%04xh, SCSIState=%02xh"
				     ", SCSIStatus=%02xh, IOCLogInfo=%08xh\n",
				     status, pScsiReply->SCSIState, pScsiReply->SCSIStatus,
				     le32_to_cpu(pScsiReply->IOCLogInfo)));

		/*
		 *  Look for + dump FCP ResponseInfo[]!
		 */
		if (pScsiReply->SCSIState & MPI_SCSI_STATE_RESPONSE_INFO_VALID) {
			dprintk((KERN_NOTICE "  FCP_ResponseInfo=%08xh\n",
					     le32_to_cpu(pScsiReply->ResponseInfo)));
		}

		switch(status) {
		case MPI_IOCSTATUS_BUSY:			/* 0x0002 */
			/*sc->result = DID_BUS_BUSY << 16;*/		/* YIKES! - Seems to
									 * kill linux interrupt
									 * handler
									 */
			sc->result = STS_BUSY;				/* Try SCSI BUSY! */
			break;

		case MPI_IOCSTATUS_SCSI_RECOVERED_ERROR:	/* 0x0040 */
			/*  Not real sure here...  */
			sc->result = DID_OK << 16;
			break;

		case MPI_IOCSTATUS_SCSI_INVALID_BUS:		/* 0x0041 */
		case MPI_IOCSTATUS_SCSI_INVALID_TARGETID:	/* 0x0042 */
			sc->result = DID_BAD_TARGET << 16;
			break;

		case MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE:	/* 0x0043 */
			/*  Spoof to SCSI Selection Timeout!  */
			sc->result = DID_NO_CONNECT << 16;
			break;

		case MPI_IOCSTATUS_SCSI_DATA_UNDERRUN:		/* 0x0045 */
			/*
			 *  YIKES!  I just discovered that SCSI IO which
			 *  returns check condition, SenseKey=05 (ILLEGAL REQUEST)
			 *  and ASC/ASCQ=94/01 (LSI Logic RAID vendor specific),
			 *  comes down this path!
			 *  Do upfront check for valid SenseData and give it
			 *  precedence!
			 */
			if (pScsiReply->SCSIState & MPI_SCSI_STATE_AUTOSENSE_VALID) {
				copy_sense_data(sc, hd, mf, pScsiReply);
				sc->result = pScsiReply->SCSIStatus;
				break;
			}

			dprintk((KERN_NOTICE MYNAM ": sc->underflow={report ERR if < %02xh bytes xfer'd}\n", sc->underflow));
			dprintk((KERN_NOTICE MYNAM ": ActBytesXferd=%02xh\n", le32_to_cpu(pScsiReply->TransferCount)));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
			sc->resid = sc->request_bufflen - le32_to_cpu(pScsiReply->TransferCount);
			dprintk((KERN_NOTICE MYNAM ": SET sc->resid=%02xh\n", sc->resid));
#endif

			if (pScsiReq->CDB[0] == INQUIRY) {
				sc->result = (DID_OK << 16);
				break;
			}

			/* workaround attempts... */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
			if (sc->resid >= 0x200) {
				/* GRRRRR...
				 *   //sc->result = DID_SOFT_ERROR << 16;
				 * Try spoofing to BUSY
				 */
				sc->result = STS_BUSY;
			} else {
				sc->result = 0;
			}
#else
			sc->result = 0;
#endif
			break;

		case MPI_IOCSTATUS_SCSI_TASK_TERMINATED:	/* 0x0048 */
			sc->result = DID_ABORT << 16;
			break;

		case MPI_IOCSTATUS_SCSI_IOC_TERMINATED:		/* 0x004B */
		case MPI_IOCSTATUS_SCSI_EXT_TERMINATED:		/* 0x004C */
			sc->result = DID_RESET << 16;
			break;

		case MPI_IOCSTATUS_SUCCESS:			/* 0x0000 */
			sc->result = pScsiReply->SCSIStatus;

			if (pScsiReply->SCSIState & MPI_SCSI_STATE_AUTOSENSE_VALID) {
				copy_sense_data(sc, hd, mf, pScsiReply);

				/*  If running agains circa 200003dd 909 MPT f/w,
				 *  may get this (AUTOSENSE_VALID) for actual TASK_SET_FULL
				 *  (QUEUE_FULL) returned from device!	--> get 0x0000?128
				 *  and with SenseBytes set to 0.
				 */
				if (pScsiReply->SCSIStatus == MPI_SCSI_STATUS_TASK_SET_FULL)
					mptscsih_report_queue_full(sc, pScsiReply, pScsiReq);
			}
			else if (pScsiReply->SCSIState & (MPI_SCSI_STATE_AUTOSENSE_FAILED | MPI_SCSI_STATE_NO_SCSI_STATUS)) {
				/*
				 *  What to do?
				 */
				sc->result = DID_SOFT_ERROR << 16;
			}
			else if (pScsiReply->SCSIState & MPI_SCSI_STATE_TERMINATED) {
				/*  Not real sure here either...  */
				sc->result = DID_ABORT << 16;
			}

			if (sc->result == MPI_SCSI_STATUS_TASK_SET_FULL)
				mptscsih_report_queue_full(sc, pScsiReply, pScsiReq);

			break;

		case MPI_IOCSTATUS_INVALID_FUNCTION:		/* 0x0001 */
		case MPI_IOCSTATUS_INVALID_SGL:			/* 0x0003 */
		case MPI_IOCSTATUS_INTERNAL_ERROR:		/* 0x0004 */
		case MPI_IOCSTATUS_RESERVED:			/* 0x0005 */
		case MPI_IOCSTATUS_INSUFFICIENT_RESOURCES:	/* 0x0006 */
		case MPI_IOCSTATUS_INVALID_FIELD:		/* 0x0007 */
		case MPI_IOCSTATUS_INVALID_STATE:		/* 0x0008 */
		case MPI_IOCSTATUS_SCSI_DATA_OVERRUN:		/* 0x0044 */
		case MPI_IOCSTATUS_SCSI_IO_DATA_ERROR:		/* 0x0046 */
		case MPI_IOCSTATUS_SCSI_PROTOCOL_ERROR:		/* 0x0047 */
		case MPI_IOCSTATUS_SCSI_RESIDUAL_MISMATCH:	/* 0x0049 */
		case MPI_IOCSTATUS_SCSI_TASK_MGMT_FAILED:	/* 0x004A */
		default:
			/*
			 *  What to do?
			 */
			sc->result = DID_SOFT_ERROR << 16;
			break;

		}	/* switch(status) */

		dprintk((KERN_NOTICE MYNAM ": sc->result set to %08xh\n", sc->result));
	}

	if (sc != NULL) {
		unsigned long flags;

		/* Unmap the DMA buffers, if any. */
		if (sc->use_sg) {
			pci_unmap_sg(ioc->pcidev,
				     (struct scatterlist *) sc->request_buffer,
				     sc->use_sg,
				     scsi_to_pci_dma_dir(sc->sc_data_direction));
		} else if (sc->request_bufflen) {
			pci_unmap_single(ioc->pcidev,
					 (dma_addr_t)((long)sc->SCp.ptr),
					 sc->request_bufflen,
					 scsi_to_pci_dma_dir(sc->sc_data_direction));
		}

		spin_lock_irqsave(&io_request_lock, flags);
		sc->scsi_done(sc);
		spin_unlock_irqrestore(&io_request_lock, flags);
	}

	return 1;
}

#ifndef MPT_SCSI_USE_NEW_EH
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	search_taskQ - Search SCSI task mgmt request queue for specific
 *			request type
 *	@remove: (Boolean) Should request be removed if found?
 *	@sc: Pointer to Scsi_Cmnd structure
 *	@task_type: Task type to search for
 *
 *	Returns pointer to MPT request frame if found, or %NULL if request
 *	was not found.
 */
static MPT_FRAME_HDR *
search_taskQ(int remove, Scsi_Cmnd *sc, u8 task_type)
{
	MPT_FRAME_HDR *mf = NULL;
	unsigned long flags;
	int count = 0;
	int list_sz;

	dslprintk((KERN_INFO MYNAM ": spinlock#1\n"));
	spin_lock_irqsave(&mpt_scsih_taskQ_lock, flags);
	list_sz = mpt_scsih_taskQ_cnt;
	if (! Q_IS_EMPTY(&mpt_scsih_taskQ)) {
		mf = mpt_scsih_taskQ.head;
		do {
			count++;
			if (mf->u.frame.linkage.argp1 == sc &&
			    mf->u.frame.linkage.arg1 == task_type) {
				if (remove) {
					Q_DEL_ITEM(&mf->u.frame.linkage);
					mpt_scsih_taskQ_cnt--;
				}
				break;
			}
		} while ((mf = mf->u.frame.linkage.forw) != (MPT_FRAME_HDR*)&mpt_scsih_taskQ);
		if (mf == (MPT_FRAME_HDR*)&mpt_scsih_taskQ) {
			mf = NULL;
		}
	}
	spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);

	if (list_sz) {
		dprintk((KERN_INFO MYNAM ": search_taskQ(%d,%p,%d) results=%p (%sFOUND%s)!\n",
				   remove, sc, task_type,
				   mf,
				   mf ? "" : "NOT_",
				   (mf && remove) ? "+REMOVED" : "" ));
		dprintk((KERN_INFO MYNAM ": (searched thru %d of %d items on taskQ)\n",
				   count,
				   list_sz ));
	}

	return mf;
}

#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

/*
 *  Hack!  I'd like to report if a device is returning QUEUE_FULL
 *  but maybe not each and every time...
 */
static long last_queue_full = 0;

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_report_queue_full - Report QUEUE_FULL status returned
 *	from a SCSI target device.
 *	@sc: Pointer to Scsi_Cmnd structure
 *	@pScsiReply: Pointer to SCSIIOReply_t
 *	@pScsiReq: Pointer to original SCSI request
 *
 *	This routine periodically reports QUEUE_FULL status returned from a
 *	SCSI target device.  It reports this to the console via kernel
 *	printk() API call, not more than once every 10 seconds.
 */
static void
mptscsih_report_queue_full(Scsi_Cmnd *sc, SCSIIOReply_t *pScsiReply, SCSIIORequest_t *pScsiReq)
{
	long time = jiffies;

	if (time - last_queue_full > 10 * HZ) {
		printk(KERN_WARNING MYNAM ": Device reported QUEUE_FULL!  SCSI bus:target:lun = %d:%d:%d\n",
				0, sc->target, sc->lun);
		last_queue_full = time;
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int BeenHereDoneThat = 0;

/*  SCSI fops start here...  */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_detect - Register MPT adapter(s) as SCSI host(s) with
 *	linux scsi mid-layer.
 *	@tpnt: Pointer to Scsi_Host_Template structure
 *
 *	(linux Scsi_Host_Template.detect routine)
 *
 *	Returns number of SCSI host adapters that were successfully
 *	registered with the linux scsi mid-layer via the scsi_register()
 *	API call.
 */
int
mptscsih_detect(Scsi_Host_Template *tpnt)
{
	struct Scsi_Host	*sh = NULL;
	MPT_SCSI_HOST		*hd = NULL;
	MPT_ADAPTER		*this;
	unsigned long		 flags;
	int			 sz;
	u8			*mem;

	if (! BeenHereDoneThat++) {
		show_mptmod_ver(my_NAME, my_VERSION);

		if ((ScsiDoneCtx = mpt_register(mptscsih_io_done, MPTSCSIH_DRIVER)) <= 0) {
			printk(KERN_ERR MYNAM ": Failed to register callback1 with MPT base driver\n");
			return mpt_scsi_hosts;
		}
		if ((ScsiTaskCtx = mpt_register(mptscsih_taskmgmt_complete, MPTSCSIH_DRIVER)) <= 0) {
			printk(KERN_ERR MYNAM ": Failed to register callback2 with MPT base driver\n");
			return mpt_scsi_hosts;
		}

#ifndef MPT_SCSI_USE_NEW_EH
		Q_INIT(&mpt_scsih_taskQ, MPT_FRAME_HDR);
		spin_lock_init(&mpt_scsih_taskQ_lock);
#endif

		if (mpt_event_register(ScsiDoneCtx, mptscsih_event_process) == 0) {
			dprintk((KERN_INFO MYNAM ": Registered for IOC event notifications\n"));
		} else {
			/* FIXME! */
		}

		if (mpt_reset_register(ScsiDoneCtx, mptscsih_ioc_reset) == 0) {
			dprintk((KERN_INFO MYNAM ": Registered for IOC reset notifications\n"));
		} else {
			/* FIXME! */
		}
	}

	dprintk((KERN_INFO MYNAM ": mpt_scsih_detect()\n"));

	this = mpt_adapter_find_first();
	while (this != NULL) {
		/* FIXME!  Multi-port (aka FC929) support...
		 * for (i = 0; i < this->facts.NumberOfPorts; i++)
		 */

		/* 20010215 -sralston
		 *  Added sanity check on SCSI Initiator-mode enabled
		 *  for this MPT adapter.
		 */
		if (!(this->pfacts[0].ProtocolFlags & MPI_PORTFACTS_PROTOCOL_INITIATOR)) {
			printk(KERN_ERR MYNAM ": Skipping %s because SCSI Initiator mode is NOT enabled!\n",
					this->name);
			this = mpt_adapter_find_next(this);
			continue;
		}

		/* 20010202 -sralston
		 *  Added sanity check on readiness of the MPT adapter.
		 */
		if (this->last_state != MPI_IOC_STATE_OPERATIONAL) {
			printk(KERN_ERR MYNAM ": ERROR - Skipping %s because it's not operational!\n",
					this->name);
			this = mpt_adapter_find_next(this);
			continue;
		}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
		tpnt->proc_dir = &proc_mpt_scsihost;
#endif
		sh = scsi_register(tpnt, sizeof(MPT_SCSI_HOST));
		if (sh != NULL) {
			save_flags(flags);
			cli();
			sh->io_port = 0;
			sh->n_io_port = 0;
			sh->irq = 0;

			/* Yikes!  This is important!
			 * Otherwise, by default, linux only scans target IDs 0-7!
			 *
			 * BUG FIX!  20010618 -sralston & pdelaney
			 * FC919 testing was encountering "duplicate" FC devices,
			 * as it turns out because the 919 was returning 512
			 * for PortFacts.MaxDevices, causing a wraparound effect
			 * in SCSI IO requests.  So instead of using:
			 *     sh->max_id = this->pfacts[0].MaxDevices - 1
			 * we'll use a definitive max here.
			 */
			sh->max_id = MPT_MAX_FC_DEVICES;

			sh->this_id = this->pfacts[0].PortSCSIID;

			restore_flags(flags);

			hd = (MPT_SCSI_HOST *) sh->hostdata;
			hd->ioc = this;
			hd->port = 0;		/* FIXME! */

			/* SCSI needs Scsi_Cmnd lookup table!
			 * (with size equal to req_depth*PtrSz!)
			 */
			sz = hd->ioc->req_depth * sizeof(void *);
			mem = kmalloc(sz, GFP_KERNEL);
			if (mem == NULL)
				return mpt_scsi_hosts;

			memset(mem, 0, sz);
			hd->ScsiLookup = (struct scsi_cmnd **) mem;

			dprintk((KERN_INFO MYNAM ": ScsiLookup @ %p, sz=%d\n",
				 hd->ScsiLookup, sz));

			/* SCSI also needs SG buckets/hunk management!
			 * (with size equal to N * req_sz * req_depth!)
			 * (where N is number of SG buckets per hunk)
			 */
			sz = MPT_SG_BUCKETS_PER_HUNK * hd->ioc->req_sz * hd->ioc->req_depth;
			mem = pci_alloc_consistent(hd->ioc->pcidev, sz,
						   &hd->SgHunksDMA);
			if (mem == NULL)
				return mpt_scsi_hosts;

			memset(mem, 0, sz);
			hd->SgHunks = (u8*)mem;

			dprintk((KERN_INFO MYNAM ": SgHunks    @ %p(%08x), sz=%d\n",
				 hd->SgHunks, hd->SgHunksDMA, sz));

			hd->qtag_tick = jiffies;

			this->sh = sh;
			mpt_scsi_hosts++;
		}
		this = mpt_adapter_find_next(this);
	}

	return mpt_scsi_hosts;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
    static char *info_kbuf = NULL;
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_release - Unregister SCSI host from linux scsi mid-layer
 *	@host: Pointer to Scsi_Host structure
 *
 *	(linux Scsi_Host_Template.release routine)
 *	This routine releases all resources associated with the SCSI host
 *	adapter.
 *
 *	Returns 0 for success.
 */
int
mptscsih_release(struct Scsi_Host *host)
{
	MPT_SCSI_HOST	*hd;
#ifndef MPT_SCSI_USE_NEW_EH
	unsigned long	 flags;

	spin_lock_irqsave(&mpt_scsih_taskQ_lock, flags);
	if (mpt_scsih_taskQ_bh_active) {
		int count = 10 * HZ;

		dprintk((KERN_INFO MYNAM ": Info: Zapping TaskMgmt thread!\n"));

		/* Zap the taskQ! */
		Q_INIT(&mpt_scsih_taskQ, MPT_FRAME_HDR);
		spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);

		while(mpt_scsih_taskQ_bh_active && --count) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}
		if (!count)
			printk(KERN_ERR MYNAM ": ERROR! TaskMgmt thread still active!\n");
	}
	spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);
#endif

	hd = (MPT_SCSI_HOST *) host->hostdata;
	if (hd != NULL) {
		int sz1, sz2;

		sz1 = sz2 = 0;
		if (hd->ScsiLookup != NULL) {
			sz1 = hd->ioc->req_depth * sizeof(void *);
			kfree(hd->ScsiLookup);
			hd->ScsiLookup = NULL;
		}

		if (hd->SgHunks != NULL) {

			sz2 = MPT_SG_BUCKETS_PER_HUNK * hd->ioc->req_sz * hd->ioc->req_depth;
			pci_free_consistent(hd->ioc->pcidev, sz2,
					    hd->SgHunks, hd->SgHunksDMA);
			hd->SgHunks = NULL;
		}
		dprintk((KERN_INFO MYNAM ": Free'd ScsiLookup (%d) and SgHunks (%d) memory\n", sz1, sz2));
	}

	if (mpt_scsi_hosts) {
		if (--mpt_scsi_hosts == 0) {
#if 0
			mptscsih_flush_pending();
#endif
			mpt_reset_deregister(ScsiDoneCtx);
			dprintk((KERN_INFO MYNAM ": Deregistered for IOC reset notifications\n"));

			mpt_event_deregister(ScsiDoneCtx);
			dprintk((KERN_INFO MYNAM ": Deregistered for IOC event notifications\n"));

			mpt_deregister(ScsiDoneCtx);
			mpt_deregister(ScsiTaskCtx);

			if (info_kbuf != NULL)
				kfree(info_kbuf);
		}
	}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_info - Return information about MPT adapter
 *	@SChost: Pointer to Scsi_Host structure
 *
 *	(linux Scsi_Host_Template.info routine)
 *
 *	Returns pointer to buffer where information was written.
 */
const char *
mptscsih_info(struct Scsi_Host *SChost)
{
	MPT_SCSI_HOST *h;
	int size = 0;

	if (info_kbuf == NULL)
		if ((info_kbuf = kmalloc(0x1000 /* 4Kb */, GFP_KERNEL)) == NULL)
			return info_kbuf;

	h = (MPT_SCSI_HOST *)SChost->hostdata;
	info_kbuf[0] = '\0';
	mpt_print_ioc_summary(h->ioc, info_kbuf, &size, 0, 0);
	info_kbuf[size-1] = '\0';

	return info_kbuf;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
	static int max_qd = 1;
#ifdef MPT_DEBUG
	static int max_sges = 0;
	static int max_xfer = 0;
#endif
#if 0
	static int max_num_sges = 0;
	static int max_sgent_len = 0;
#endif
#if 0
static int index_log[128];
static int index_ent = 0;
static __inline__ void ADD_INDEX_LOG(int req_ent)
{
	int i = index_ent++;

	index_log[i & (128 - 1)] = req_ent;
}
#else
#define ADD_INDEX_LOG(req_ent)	do { } while(0)
#endif
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_qcmd - Primary Fusion MPT SCSI initiator IO start routine.
 *	@SCpnt: Pointer to Scsi_Cmnd structure
 *	@done: Pointer SCSI mid-layer IO completion function
 *
 *	(linux Scsi_Host_Template.queuecommand routine)
 *	This is the primary SCSI IO start routine.  Create a MPI SCSIIORequest
 *	from a linux Scsi_Cmnd request and send it to the IOC.
 *
 *	Returns 0. (rtn value discarded by linux scsi mid-layer)
 */
int
mptscsih_qcmd(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
{
	struct Scsi_Host	*host;
	MPT_SCSI_HOST		*hd;
	MPT_FRAME_HDR		*mf;
	SCSIIORequest_t		*pScsiReq;
	int	 datadir;
	u32	 len;
	u32	 sgdir;
	u32	 scsictl;
	u32	 scsidir;
	u32	 qtag;
	u32	*mptr;
	int	 sge_spill1;
	int	 frm_sz;
	int	 sges_left;
	u32	 chain_offset;
	int	 my_idx;
	int	 i;

	dmfprintk((KERN_INFO MYNAM "_qcmd: SCpnt=%p, done()=%p\n",
		    SCpnt, done));

	host = SCpnt->host;
	hd = (MPT_SCSI_HOST *) host->hostdata;
	
#if 0
	if (host->host_busy >= 60) {
		MPT_ADAPTER *ioc = hd->ioc;
		u16 pci_command, pci_status;

		/* The IOC is probably hung, investigate status. */
		printk("MPI: IOC probably hung IOCSTAT[%08x] INTSTAT[%08x] REPLYFIFO[%08x]\n",
		       readl(&ioc->chip.fc9xx->DoorbellValue),
		       readl(&ioc->chip.fc9xx->IntStatus),
		       readl(&ioc->chip.fc9xx->ReplyFifo));
		pci_read_config_word(ioc->pcidev, PCI_COMMAND, &pci_command);
		pci_read_config_word(ioc->pcidev, PCI_STATUS, &pci_status);
		printk("MPI: PCI command[%04x] status[%04x]\n", pci_command, pci_status);
		{
			/* DUMP req index logger. */
			int begin, end;

			begin = (index_ent - 65) & (128 - 1);
			end = index_ent & (128 - 1);
			printk("MPI: REQ_INDEX_HIST[");
			while (begin != end) {
				printk("(%04x)", index_log[begin]);
				begin = (begin + 1) & (128 - 1);
			}
			printk("\n");
		}
		sti();
		while(1)
			barrier();
	}
#endif

	SCpnt->scsi_done = done;

	/* 20000617 -sralston
	 *  GRRRRR...  Shouldn't have to do this but...
	 *  Do explicit check for REQUEST_SENSE and cached SenseData.
	 *  If yes, return cached SenseData.
	 */
#ifdef MPT_SCSI_CACHE_AUTOSENSE
	{
		MPT_SCSI_DEV	*mpt_sdev;

		mpt_sdev = (MPT_SCSI_DEV *) SCpnt->device->hostdata;
		if (mpt_sdev && SCpnt->cmnd[0] == REQUEST_SENSE) {
			u8 *dest = NULL;

			if (!SCpnt->use_sg)
				dest = SCpnt->request_buffer;
			else {
				struct scatterlist *sg = (struct scatterlist *) SCpnt->request_buffer;
				if (sg)
					dest = (u8 *) (unsigned long)sg_dma_address(sg);
			}

			if (dest && mpt_sdev->sense_sz) {
				memcpy(dest, mpt_sdev->CachedSense.data, mpt_sdev->sense_sz);
#ifdef MPT_DEBUG
				{
					int  i;
					u8  *sb;

					sb = mpt_sdev->CachedSense.data;
					if (sb && ((sb[0] & 0x70) == 0x70)) {
						printk(KERN_WARNING MYNAM ": Returning last cached SCSI (hex) SenseData:\n");
						printk(KERN_WARNING " ");
						for (i = 0; i < (8 + sb[7]); i++)
							printk("%s%02x", i == 13 ? "-" : " ", sb[i]);
						printk("\n");
					}
				}
#endif
			}
			SCpnt->resid = SCpnt->request_bufflen - mpt_sdev->sense_sz;
			SCpnt->result = 0;
/*			spin_lock(&io_request_lock);	*/
			SCpnt->scsi_done(SCpnt);
/*			spin_unlock(&io_request_lock);	*/
			return 0;
		}
	}
#endif

	if ((mf = mpt_get_msg_frame(ScsiDoneCtx, hd->ioc->id)) == NULL) {
/*		SCpnt->result = DID_SOFT_ERROR << 16;	*/
		SCpnt->result = STS_BUSY;
		SCpnt->scsi_done(SCpnt);
/*		return 1;				*/
		return 0;
	}
	pScsiReq = (SCSIIORequest_t *) mf;

	my_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);

	ADD_INDEX_LOG(my_idx);

	/* Map the data portion, if any. */
	sges_left = SCpnt->use_sg;
	if (sges_left) {
		sges_left = pci_map_sg(hd->ioc->pcidev,
				       (struct scatterlist *) SCpnt->request_buffer,
				       sges_left,
				       scsi_to_pci_dma_dir(SCpnt->sc_data_direction));
	} else if (SCpnt->request_bufflen) {
		dma_addr_t buf_dma_addr;

		buf_dma_addr = pci_map_single(hd->ioc->pcidev,
					      SCpnt->request_buffer,
					      SCpnt->request_bufflen,
					      scsi_to_pci_dma_dir(SCpnt->sc_data_direction));

		/* We hide it here for later unmap. */
		SCpnt->SCp.ptr = (char *)(unsigned long) buf_dma_addr;
	}

	/*
	 *  Put together a MPT SCSI request...
	 */

	/* Assume SimpleQ, NO DATA XFER for now */

	len = SCpnt->request_bufflen;
	sgdir = 0x00000000;		/* SGL IN  (host<--ioc) */
	scsidir = MPI_SCSIIO_CONTROL_NODATATRANSFER;

	/*
	 *  The scsi layer should be handling this stuff
	 *  (In 2.3.x it does -DaveM)
	 */

	/*  BUG FIX!  19991030 -sralston
	 *    TUR's being issued with scsictl=0x02000000 (DATA_IN)!
	 *    Seems we may receive a buffer (len>0) even when there
	 *    will be no data transfer!  GRRRRR...
	 */
	datadir = mptscsih_io_direction(SCpnt);
	if (datadir < 0) {
		scsidir = MPI_SCSIIO_CONTROL_READ;	/* DATA IN  (host<--ioc<--dev) */
	} else if (datadir > 0) {
		sgdir	= 0x04000000;			/* SGL OUT  (host-->ioc) */
		scsidir = MPI_SCSIIO_CONTROL_WRITE;	/* DATA OUT (host-->ioc-->dev) */
	} else {
		len = 0;
	}

	qtag = MPI_SCSIIO_CONTROL_SIMPLEQ;

	/*
	 *  Attach tags to the devices
	 */
	if (SCpnt->device->tagged_supported) {
		/*
		 *  Some drives are too stupid to handle fairness issues
		 *  with tagged queueing. We throw in the odd ordered
		 *  tag to stop them starving themselves.
		 */
		if ((jiffies - hd->qtag_tick) > (5*HZ)) {
			qtag = MPI_SCSIIO_CONTROL_ORDEREDQ;
			hd->qtag_tick = jiffies;

#if 0
			/* These are ALWAYS zero!
			 * (Because this is a place for the device driver to dynamically
			 *  assign tag numbers any way it sees fit.  That's why -DaveM)
			 */
			dprintk((KERN_DEBUG MYNAM ": sc->device->current_tag = %08x\n",
					SCpnt->device->current_tag));
			dprintk((KERN_DEBUG MYNAM ": sc->tag                 = %08x\n",
					SCpnt->tag));
#endif
		}
#if 0
		else {
			/* Hmmm...  I always see value of 0 here,
			 *  of which {HEAD_OF, ORDERED, SIMPLE} are NOT!  -sralston
			 * (Because this is a place for the device driver to dynamically
			 *  assign tag numbers any way it sees fit.  That's why -DaveM)
			 *
			 * if (SCpnt->tag == HEAD_OF_QUEUE_TAG)
			 */
			if (SCpnt->device->current_tag == HEAD_OF_QUEUE_TAG)
				qtag = MPI_SCSIIO_CONTROL_HEADOFQ;
			else if (SCpnt->tag == ORDERED_QUEUE_TAG)
				qtag = MPI_SCSIIO_CONTROL_ORDEREDQ;
		}
#endif
	}

	scsictl = scsidir | qtag;

	frm_sz = hd->ioc->req_sz;

	/* Ack!
	 * sge_spill1 = 9;
	 */
	sge_spill1 = (frm_sz - (sizeof(SCSIIORequest_t) - sizeof(SGEIOUnion_t) + sizeof(SGEChain32_t))) / 8;
	/*  spill1: for req_sz == 128 (128-48==80, 80/8==10 SGEs max, first time!), --> use 9
	 *  spill1: for req_sz ==  96 ( 96-48==48, 48/8== 6 SGEs max, first time!), --> use 5
	 */
	dsgprintk((KERN_INFO MYNAM ": SG: %x     spill1 = %d\n",
		   my_idx, sge_spill1));

#ifdef MPT_DEBUG
	if (sges_left > max_sges) {
		max_sges = sges_left;
		dprintk((KERN_INFO MYNAM ": MPT_MaxSges = %d\n", max_sges));
	}
#endif
#if 0
	if (sges_left > max_num_sges) {
		max_num_sges = sges_left;
		printk(KERN_INFO MYNAM ": MPT_MaxNumSges = %d\n", max_num_sges);
	}
#endif

	dsgprintk((KERN_INFO MYNAM ": SG: %x     sges_left = %d (initially)\n",
		   my_idx, sges_left));

	chain_offset = 0;
	if (sges_left > (sge_spill1+1)) {
#if 0
		chain_offset = 0x1E;
#endif
		chain_offset = (frm_sz - 8) / 4;
	}

	pScsiReq->TargetID = SCpnt->target;
	pScsiReq->Bus = hd->port;
	pScsiReq->ChainOffset = chain_offset;
	pScsiReq->Function = MPI_FUNCTION_SCSI_IO_REQUEST;
	pScsiReq->CDBLength = SCpnt->cmd_len;

/* We have 256 bytes alloc'd per IO; let's use it. */
/*	pScsiReq->SenseBufferLength = SNS_LEN(SCpnt);	*/
	pScsiReq->SenseBufferLength = 255;

	pScsiReq->Reserved = 0;
	pScsiReq->MsgFlags = 0;
	pScsiReq->LUN[0] = 0;
	pScsiReq->LUN[1] = SCpnt->lun;
	pScsiReq->LUN[2] = 0;
	pScsiReq->LUN[3] = 0;
	pScsiReq->LUN[4] = 0;
	pScsiReq->LUN[5] = 0;
	pScsiReq->LUN[6] = 0;
	pScsiReq->LUN[7] = 0;
	pScsiReq->Control = cpu_to_le32(scsictl);

	/*
	 *  Write SCSI CDB into the message
	 */
	for (i = 0; i < 12; i++)
		pScsiReq->CDB[i] = SCpnt->cmnd[i];
	for (i = 12; i < 16; i++)
		pScsiReq->CDB[i] = 0;

	/* DataLength */
	pScsiReq->DataLength = cpu_to_le32(len);

	/* SenseBuffer low address */
	pScsiReq->SenseBufferLowAddr = cpu_to_le32(hd->ioc->sense_buf_pool_dma + (my_idx * 256));

	mptr = (u32 *) &pScsiReq->SGL;

	/*
	 *  Now fill in the SGList...
	 *  NOTES: For 128 byte req_sz, we can hold up to 10 simple SGE's
	 *  in the remaining request frame.  We -could- do unlimited chains
	 *  but each chain buffer can only be req_sz bytes in size, and
	 *  we lose one SGE whenever we chain.
	 *  For 128 req_sz, we can hold up to 16 SGE's per chain buffer.
	 *  For practical reasons, limit ourselves to 1 overflow chain buffer;
	 *  giving us 9 + 16 == 25 SGE's max.
	 *  At 4 Kb per SGE, that yields 100 Kb max transfer.
	 *
	 *  (This code needs to be completely changed when/if 64-bit DMA
	 *   addressing is used, since we will be able to fit much less than
	 *   10 embedded SG entries. -DaveM)
	 */
	if (sges_left) {
		struct scatterlist *sg = (struct scatterlist *) SCpnt->request_buffer;
		u32  v1, v2;
		int  sge_spill2;
		int  sge_cur_spill;
		int  sgCnt;
		u8  *pSgBucket;
		int  chain_sz;

		len = 0;

		/*	sge_spill2 = 15;
		 *  spill2: for req_sz == 128 (128/8==16 SGEs max, first time!), --> use 15
		 *  spill2: for req_sz ==  96 ( 96/8==12 SGEs max, first time!), --> use 11
		 */
		sge_spill2 = frm_sz / 8 - 1;
		dsgprintk((KERN_INFO MYNAM ": SG: %x     spill2 = %d\n",
			   my_idx, sge_spill2));

		pSgBucket = NULL;
		sgCnt = 0;
		sge_cur_spill = sge_spill1;
		while (sges_left) {
#if 0
			if (sg_dma_len(sg) > max_sgent_len) {
				max_sgent_len = sg_dma_len(sg);
				printk(KERN_INFO MYNAM ": MPT_MaxSgentLen = %d\n", max_sgent_len);
			}
#endif
			/* Write one simple SGE */
			v1 = sgdir | 0x10000000 | sg_dma_len(sg);
			len += sg_dma_len(sg);
			v2 = sg_dma_address(sg);
			dsgprintk((KERN_INFO MYNAM ": SG: %x     Writing SGE @%p: %08x %08x, sges_left=%d\n",
				   my_idx, mptr, v1, v2, sges_left));
			*mptr++ = cpu_to_le32(v1);
			*mptr++ = cpu_to_le32(v2);
			sg++;
			sgCnt++;

			if (--sges_left == 0) {
				/* re-write 1st word of previous SGE with SIMPLE,
				 * LE, EOB, and EOL bits!
				 */
				v1 = 0xD1000000 | sgdir | sg_dma_len(sg-1);
				dsgprintk((KERN_INFO MYNAM ": SG: %x (re)Writing SGE @%p: %08x (VERY LAST SGE!)\n",
					   my_idx, mptr-2, v1));
				*(mptr - 2) = cpu_to_le32(v1);
			} else {
				if ((sges_left > 1) && ((sgCnt % sge_cur_spill) == 0)) {
					dsgprintk((KERN_INFO MYNAM ": SG: %x     SG spill at modulo 0!\n",
						   my_idx));

					/* Fixup previous SGE with LE bit! */
					v1 = sgdir | 0x90000000 | sg_dma_len(sg-1);
					dsgprintk((KERN_INFO MYNAM ": SG: %x (re)Writing SGE @%p: %08x (LAST BUCKET SGE!)\n",
						   my_idx, mptr-2, v1));
					*(mptr - 2) = cpu_to_le32(v1);

					chain_offset = 0;
					/* Going to need another chain? */
					if (sges_left > (sge_spill2+1)) {
#if 0
						chain_offset = 0x1E;
#endif
						chain_offset = (frm_sz - 8) / 4;
						chain_sz = frm_sz;
					} else {
						chain_sz = sges_left * 8;
					}

					/* write chain SGE at mptr. */
					v1 = 0x30000000 | chain_offset<<16 | chain_sz;
					if (pSgBucket == NULL) {
						pSgBucket = hd->SgHunks
							+ (my_idx * frm_sz * MPT_SG_BUCKETS_PER_HUNK);
					} else {
						pSgBucket += frm_sz;
					}
					v2 = (hd->SgHunksDMA +
					      ((u8 *)pSgBucket - (u8 *)hd->SgHunks));
					dsgprintk((KERN_INFO MYNAM ": SG: %x     Writing SGE @%p: %08x %08x (CHAIN!)\n",
						   my_idx, mptr, v1, v2));
					*(mptr++) = cpu_to_le32(v1);
					*(mptr) = cpu_to_le32(v2);

					mptr = (u32 *) pSgBucket;
					sgCnt = 0;
					sge_cur_spill = sge_spill2;
				}
			}
		}
	} else {
		dsgprintk((KERN_INFO MYNAM ": SG: non-SG for %p, len=%d\n",
			   SCpnt, SCpnt->request_bufflen));

		if (len > 0) {
			dma_addr_t buf_dma_addr;

			buf_dma_addr = (dma_addr_t) (unsigned long)SCpnt->SCp.ptr;
			*(mptr++) = cpu_to_le32(0xD1000000|sgdir|SCpnt->request_bufflen);
			*(mptr++) = cpu_to_le32(buf_dma_addr);
		}
	}

#ifdef MPT_DEBUG
	/* if (SCpnt->request_bufflen > max_xfer) */
	if (len > max_xfer) {
		max_xfer = len;
		dprintk((KERN_INFO MYNAM ": MPT_MaxXfer = %d\n", max_xfer));
	}
#endif

	hd->ScsiLookup[my_idx] = SCpnt;

	/* Main banana... */
	mpt_put_msg_frame(ScsiDoneCtx, hd->ioc->id, mf);

	atomic_inc(&queue_depth);
	if (atomic_read(&queue_depth) > max_qd) {
		max_qd = atomic_read(&queue_depth);
		dprintk((KERN_INFO MYNAM ": Queue depth now %d.\n", max_qd));
	}

	dmfprintk((KERN_INFO MYNAM ": Issued SCSI cmd (%p)\n", SCpnt));

	return 0;
}

#ifdef MPT_SCSI_USE_NEW_EH		/* { */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
    mptscsih_abort
    Returns: 0=SUCCESS, else FAILED
*/
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_abort - Abort linux Scsi_Cmnd routine, new_eh variant
 *	@SCpnt: Pointer to Scsi_Cmnd structure, IO to be aborted
 *
 *	(linux Scsi_Host_Template.eh_abort_handler routine)
 *
 *	Returns SUCCESS or FAILED.  
 */
int
mptscsih_abort(Scsi_Cmnd * SCpnt)
{
	MPT_FRAME_HDR	*mf;
	SCSITaskMgmt_t	*pScsiTm;
	MPT_SCSI_HOST	*hd;
	u32		*msg;
	u32		 ctx2abort;
	int		 i;
	unsigned long	 flags;

	printk(KERN_WARNING MYNAM ": Attempting _ABORT SCSI IO (=%p)\n", SCpnt);
	printk(KERN_WARNING MYNAM ": IOs outstanding = %d\n", atomic_read(&queue_depth));

	hd = (MPT_SCSI_HOST *) SCpnt->host->hostdata;

	if ((mf = mpt_get_msg_frame(ScsiTaskCtx, hd->ioc->id)) == NULL) {
/*		SCpnt->result = DID_SOFT_ERROR << 16;	*/
		SCpnt->result = STS_BUSY;
		SCpnt->scsi_done(SCpnt);
		return FAILED;
	}

	pScsiTm = (SCSITaskMgmt_t *) mf;
	msg = (u32 *) mf;

	pScsiTm->TargetID = SCpnt->target;
	pScsiTm->Bus = hd->port;
	pScsiTm->ChainOffset = 0;
	pScsiTm->Function = MPI_FUNCTION_SCSI_TASK_MGMT;

	pScsiTm->Reserved = 0;
	pScsiTm->TaskType = MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK;
	pScsiTm->Reserved1 = 0;
	pScsiTm->MsgFlags = 0;

	for (i = 0; i < 8; i++) {
		u8 val = 0;
		if (i == 1)
			val = SCpnt->lun;
		pScsiTm->LUN[i] = val;
	}

	for (i = 0; i < 7; i++)
		pScsiTm->Reserved2[i] = 0;

	/* Most important!  Set TaskMsgContext to SCpnt's MsgContext!
	 * (the IO to be ABORT'd)
	 *
	 * NOTE: Since we do not byteswap MsgContext, we do not
	 *	 swap it here either.  It is an opaque cookie to
	 *	 the controller, so it does not matter. -DaveM
	 */
	ctx2abort = SCPNT_TO_MSGCTX(SCpnt);
	if (ctx2abort == -1) {
		printk(KERN_ERR MYNAM ": ERROR - ScsiLookup fail(#2) for SCpnt=%p\n", SCpnt);
		SCpnt->result = DID_SOFT_ERROR << 16;
		spin_lock_irqsave(&io_request_lock, flags);
		SCpnt->scsi_done(SCpnt);
		spin_unlock_irqrestore(&io_request_lock, flags);
		mpt_free_msg_frame(ScsiTaskCtx, hd->ioc->id, mf);
	} else {
		dprintk((KERN_INFO MYNAM ":DbG: ctx2abort = %08x\n", ctx2abort));
		pScsiTm->TaskMsgContext = ctx2abort;


		/* MPI v0.10 requires SCSITaskMgmt requests be sent via Doorbell/handshake
			mpt_put_msg_frame(hd->ioc->id, mf);
		*/
		if ((i = mpt_send_handshake_request(ScsiTaskCtx, hd->ioc->id,
					sizeof(SCSITaskMgmt_t), msg))
		    != 0) {
			printk(KERN_WARNING MYNAM
					": WARNING[2] - IOC error (%d) processing TaskMgmt request (mf=%p:sc=%p)\n",
					i, mf, SCpnt);
			SCpnt->result = DID_SOFT_ERROR << 16;
			spin_lock_irqsave(&io_request_lock, flags);
			SCpnt->scsi_done(SCpnt);
			spin_unlock_irqrestore(&io_request_lock, flags);
			mpt_free_msg_frame(ScsiTaskCtx, hd->ioc->id, mf);
		}
	}

	//return SUCCESS;
	return FAILED;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_dev_reset - Perform a SCSI TARGET_RESET!  new_eh variant
 *	@SCpnt: Pointer to Scsi_Cmnd structure, IO which reset is due to
 *
 *	(linux Scsi_Host_Template.eh_dev_reset_handler routine)
 *
 *	Returns SUCCESS or FAILED.
 */
int
mptscsih_dev_reset(Scsi_Cmnd * SCpnt)
{
	MPT_FRAME_HDR	*mf;
	SCSITaskMgmt_t	*pScsiTm;
	MPT_SCSI_HOST	*hd;
	u32		*msg;
	int		 i;
	unsigned long	 flags;

	printk(KERN_WARNING MYNAM ": Attempting _TARGET_RESET (%p)\n", SCpnt);
	printk(KERN_WARNING MYNAM ": IOs outstanding = %d\n", atomic_read(&queue_depth));

	hd = (MPT_SCSI_HOST *) SCpnt->host->hostdata;

	if ((mf = mpt_get_msg_frame(ScsiTaskCtx, hd->ioc->id)) == NULL) {
/*		SCpnt->result = DID_SOFT_ERROR << 16;	*/
		SCpnt->result = STS_BUSY;
		SCpnt->scsi_done(SCpnt);
		return FAILED;
	}

	pScsiTm = (SCSITaskMgmt_t *) mf;
	msg = (u32*)mf;

	pScsiTm->TargetID = SCpnt->target;
	pScsiTm->Bus = hd->port;
	pScsiTm->ChainOffset = 0;
	pScsiTm->Function = MPI_FUNCTION_SCSI_TASK_MGMT;

	pScsiTm->Reserved = 0;
	pScsiTm->TaskType = MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	pScsiTm->Reserved1 = 0;
	pScsiTm->MsgFlags = 0;

	/* _TARGET_RESET goes to LUN 0 always! */
	for (i = 0; i < 8; i++)
		pScsiTm->LUN[i] = 0;

	/* Control: No data direction, set task mgmt bit? */
	for (i = 0; i < 7; i++)
		pScsiTm->Reserved2[i] = 0;

	pScsiTm->TaskMsgContext = cpu_to_le32(0);

/* MPI v0.10 requires SCSITaskMgmt requests be sent via Doorbell/handshake
	mpt_put_msg_frame(hd->ioc->id, mf);
*/
/* FIXME!  Check return status! */
	if ((i = mpt_send_handshake_request(ScsiTaskCtx, hd->ioc->id,
				sizeof(SCSITaskMgmt_t), msg))
	    != 0) {
		printk(KERN_WARNING MYNAM
				": WARNING[3] - IOC error (%d) processing TaskMgmt request (mf=%p:sc=%p)\n",
				i, mf, SCpnt);
		SCpnt->result = DID_SOFT_ERROR << 16;
		spin_lock_irqsave(&io_request_lock, flags);
		SCpnt->scsi_done(SCpnt);
		spin_unlock_irqrestore(&io_request_lock, flags);
		mpt_free_msg_frame(ScsiTaskCtx, hd->ioc->id, mf);
	}

	//return SUCCESS;
	return FAILED;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_bus_reset - Perform a SCSI BUS_RESET!	new_eh variant
 *	@SCpnt: Pointer to Scsi_Cmnd structure, IO which reset is due to
 *
 *	(linux Scsi_Host_Template.eh_bus_reset_handler routine)
 *
 *	Returns SUCCESS or FAILED.
 */
int
mptscsih_bus_reset(Scsi_Cmnd * SCpnt)
{
	MPT_FRAME_HDR	*mf;
	SCSITaskMgmt_t	*pScsiTm;
	MPT_SCSI_HOST	*hd;
	u32		*msg;
	int		 i;
	unsigned long	 flags;

	printk(KERN_WARNING MYNAM ": Attempting _BUS_RESET (%p)\n", SCpnt);
	printk(KERN_WARNING MYNAM ": IOs outstanding = %d\n", atomic_read(&queue_depth));

	hd = (MPT_SCSI_HOST *) SCpnt->host->hostdata;

	if ((mf = mpt_get_msg_frame(ScsiTaskCtx, hd->ioc->id)) == NULL) {
/*		SCpnt->result = DID_SOFT_ERROR << 16;	*/
		SCpnt->result = STS_BUSY;
		SCpnt->scsi_done(SCpnt);
		return FAILED;
	}

	pScsiTm = (SCSITaskMgmt_t *) mf;
	msg = (u32 *) mf;

	pScsiTm->TargetID = SCpnt->target;
	pScsiTm->Bus = hd->port;
	pScsiTm->ChainOffset = 0;
	pScsiTm->Function = MPI_FUNCTION_SCSI_TASK_MGMT;

	pScsiTm->Reserved = 0;
	pScsiTm->TaskType = MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS;
	pScsiTm->Reserved1 = 0;
	pScsiTm->MsgFlags = 0;

	for (i = 0; i < 8; i++)
		pScsiTm->LUN[i] = 0;

	/* Control: No data direction, set task mgmt bit? */
	for (i = 0; i < 7; i++)
		pScsiTm->Reserved2[i] = 0;

	pScsiTm->TaskMsgContext = cpu_to_le32(0);

/* MPI v0.10 requires SCSITaskMgmt requests be sent via Doorbell/handshake
	mpt_put_msg_frame(hd->ioc->id, mf);
*/
/* FIXME!  Check return status! */
	if ((i = mpt_send_handshake_request(ScsiTaskCtx, hd->ioc->id,
				sizeof(SCSITaskMgmt_t), msg))
	    != 0) {
		printk(KERN_WARNING MYNAM
				": WARNING[4] - IOC error (%d) processing TaskMgmt request (mf=%p:sc=%p)\n",
				i, mf, SCpnt);
		SCpnt->result = DID_SOFT_ERROR << 16;
		spin_lock_irqsave(&io_request_lock, flags);
		SCpnt->scsi_done(SCpnt);
		spin_unlock_irqrestore(&io_request_lock, flags);
		mpt_free_msg_frame(ScsiTaskCtx, hd->ioc->id, mf);
	}

	return SUCCESS;
}

#if 0	/* { */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_host_reset - Perform a SCSI host adapter RESET!
 *	new_eh variant
 *	@SCpnt: Pointer to Scsi_Cmnd structure, IO which reset is due to
 *
 *	(linux Scsi_Host_Template.eh_host_reset_handler routine)
 *
 *	Returns SUCCESS or FAILED.
 */
int
mptscsih_host_reset(Scsi_Cmnd * SCpnt)
{
	return FAILED;
}
#endif	/* } */

#else		/* MPT_SCSI old EH stuff... */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_old_abort - Abort linux Scsi_Cmnd routine
 *	@SCpnt: Pointer to Scsi_Cmnd structure, IO to be aborted
 *
 *	(linux Scsi_Host_Template.abort routine)
 *
 *	Returns SCSI_ABORT_{SUCCESS,BUSY,PENDING}.
 */
int
mptscsih_old_abort(Scsi_Cmnd *SCpnt)
{
	MPT_SCSI_HOST		*hd;
	MPT_FRAME_HDR		*mf;
	struct tq_struct	*ptaskfoo;
	unsigned long		 flags;

	printk(KERN_WARNING MYNAM ": Scheduling _ABORT SCSI IO (=%p)\n", SCpnt);
	printk(KERN_WARNING MYNAM ": IOs outstanding = %d\n", atomic_read(&queue_depth));

	if ((hd = (MPT_SCSI_HOST *) SCpnt->host->hostdata) == NULL) {
		SCpnt->result = DID_ABORT << 16;
		SCpnt->scsi_done(SCpnt);
		return SCSI_ABORT_SUCCESS;
	}

	/*
	 *  Check to see if there's already an ABORT queued for this guy.
	 */
	mf = search_taskQ(0,SCpnt,MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK);
	if (mf != NULL) {
		return SCSI_ABORT_PENDING;
	}

	if ((mf = mpt_get_msg_frame(ScsiTaskCtx, hd->ioc->id)) == NULL) {
/*		SCpnt->result = DID_SOFT_ERROR << 16;	*/
		SCpnt->result = STS_BUSY;
		SCpnt->scsi_done(SCpnt);
		return SCSI_ABORT_BUSY;
	}

	/*
	 *  Add ourselves to (end of) mpt_scsih_taskQ.
	 *  Check to see if our _bh is running.  If NOT, schedule it.
	 */
	dslprintk((KERN_INFO MYNAM ": spinlock#2\n"));
	spin_lock_irqsave(&mpt_scsih_taskQ_lock, flags);
	Q_ADD_TAIL(&mpt_scsih_taskQ, &mf->u.frame.linkage, MPT_FRAME_HDR);
	mpt_scsih_taskQ_cnt++;
	/* Yikes - linkage! */
/*	SCpnt->host_scribble = (unsigned char *)mf;	*/
	mf->u.frame.linkage.arg1 = MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK;
	mf->u.frame.linkage.argp1 = SCpnt;
	if (! mpt_scsih_taskQ_bh_active) {
		mpt_scsih_taskQ_bh_active = 1;
		/*
		 *  Oh how cute, no alloc/free/mgmt needed if we use
		 *  (bottom/unused portion of) MPT request frame.
		 */
		ptaskfoo = (struct tq_struct *) ((u8*)mf + hd->ioc->req_sz - sizeof(*ptaskfoo));
		ptaskfoo->sync = 0;
		ptaskfoo->routine = mptscsih_taskmgmt_bh;
		ptaskfoo->data = SCpnt;

		SCHEDULE_TASK(ptaskfoo);
	}
	spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);

	return SCSI_ABORT_PENDING;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_old_reset - Perform a SCSI BUS_RESET!
 *	@SCpnt: Pointer to Scsi_Cmnd structure, IO which reset is due to
 *	@reset_flags: (not used?)
 *
 *	(linux Scsi_Host_Template.reset routine)
 *
 *	Returns SCSI_RESET_{SUCCESS,PUNT,PENDING}.
 */
int
mptscsih_old_reset(Scsi_Cmnd *SCpnt, unsigned int reset_flags)
{
	MPT_SCSI_HOST		*hd;
	MPT_FRAME_HDR		*mf;
	struct tq_struct	*ptaskfoo;
	unsigned long		 flags;

	printk(KERN_WARNING MYNAM ": Scheduling _BUS_RESET (=%p)\n", SCpnt);
	printk(KERN_WARNING MYNAM ": IOs outstanding = %d\n", atomic_read(&queue_depth));

	if ((hd = (MPT_SCSI_HOST *) SCpnt->host->hostdata) == NULL) {
		SCpnt->result = DID_RESET << 16;
		SCpnt->scsi_done(SCpnt);
		return SCSI_RESET_SUCCESS;
	}

	/*
	 *  Check to see if there's already a BUS_RESET queued for this guy.
	 */
	mf = search_taskQ(0,SCpnt,MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS);
	if (mf != NULL) {
		return SCSI_RESET_PENDING;
	}

	if ((mf = mpt_get_msg_frame(ScsiTaskCtx, hd->ioc->id)) == NULL) {
/*		SCpnt->result = DID_SOFT_ERROR << 16;	*/
		SCpnt->result = STS_BUSY;
		SCpnt->scsi_done(SCpnt);
		return SCSI_RESET_PUNT;
	}

	/*
	 *  Add ourselves to (end of) mpt_scsih_taskQ.
	 *  Check to see if our _bh is running.  If NOT, schedule it.
	 */
	dslprintk((KERN_INFO MYNAM ": spinlock#3\n"));
	spin_lock_irqsave(&mpt_scsih_taskQ_lock, flags);
	Q_ADD_TAIL(&mpt_scsih_taskQ, &mf->u.frame.linkage, MPT_FRAME_HDR);
	mpt_scsih_taskQ_cnt++;
	/* Yikes - linkage! */
/*	SCpnt->host_scribble = (unsigned char *)mf;	*/
	mf->u.frame.linkage.arg1 = MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS;
	mf->u.frame.linkage.argp1 = SCpnt;
	if (! mpt_scsih_taskQ_bh_active) {
		mpt_scsih_taskQ_bh_active = 1;
		/*
		 *  Oh how cute, no alloc/free/mgmt needed if we use
		 *  (bottom/unused portion of) MPT request frame.
		 */
		ptaskfoo = (struct tq_struct *) ((u8*)mf + hd->ioc->req_sz - sizeof(*ptaskfoo));
		ptaskfoo->sync = 0;
		ptaskfoo->routine = mptscsih_taskmgmt_bh;
		ptaskfoo->data = SCpnt;

		SCHEDULE_TASK(ptaskfoo);
	}
	spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);

	return SCSI_RESET_PENDING;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	mptscsih_taskmgmt_bh - SCSI task mgmt bottom half handler
 *	@sc: (unused)
 *
 *	This routine (thread) is active whenever there are any outstanding
 *	SCSI task management requests for a SCSI host adapter.
 *	IMPORTANT!  This routine is scheduled therefore should never be
 *	running in ISR context.  i.e., it's safe to sleep here.
 */
void
mptscsih_taskmgmt_bh(void *sc)
{
	Scsi_Cmnd	*SCpnt;
	MPT_FRAME_HDR	*mf;
	SCSITaskMgmt_t	*pScsiTm;
	MPT_SCSI_HOST	*hd;
	u32		 ctx2abort = 0;
	int		 i;
	unsigned long	 flags;
	u8		 task_type;

	dslprintk((KERN_INFO MYNAM ": spinlock#4\n"));
	spin_lock_irqsave(&mpt_scsih_taskQ_lock, flags);
	mpt_scsih_taskQ_bh_active = 1;
	spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);

	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/4);

		/*
		 *  We MUST remove item from taskQ *before* we format the
		 *  frame as a SCSITaskMgmt request and send it down to the IOC.
		 */
		dslprintk((KERN_INFO MYNAM ": spinlock#5\n"));
		spin_lock_irqsave(&mpt_scsih_taskQ_lock, flags);
		if (Q_IS_EMPTY(&mpt_scsih_taskQ)) {
			spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);
			break;
		}
		mf = mpt_scsih_taskQ.head;
		Q_DEL_ITEM(&mf->u.frame.linkage);
		mpt_scsih_taskQ_cnt--;
		mpt_scsih_active_taskmgmt_mf = mf;
		spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);

		SCpnt = (Scsi_Cmnd*)mf->u.frame.linkage.argp1;
		if (SCpnt == NULL) {
			printk(KERN_ERR MYNAM ": ERROR - TaskMgmt has NULL SCpnt! (%p:%p)\n", mf, SCpnt);
			continue;
		}
		hd = (MPT_SCSI_HOST *) SCpnt->host->hostdata;
		pScsiTm = (SCSITaskMgmt_t *) mf;

		for (i = 0; i < 8; i++) {
			pScsiTm->LUN[i] = 0;
		}

		task_type = mf->u.frame.linkage.arg1;
		if (task_type == MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK) {
			printk(KERN_WARNING MYNAM ": Attempting _ABORT SCSI IO! (mf=%p:sc=%p)\n",
					mf, SCpnt);

			/* Most important!  Set TaskMsgContext to SCpnt's MsgContext!
			 * (the IO to be ABORT'd)
			 *
			 * NOTE: Since we do not byteswap MsgContext, we do not
			 *	 swap it here either.  It is an opaque cookie to
			 *	 the controller, so it does not matter. -DaveM
			 */
			ctx2abort = SCPNT_TO_MSGCTX(SCpnt);
			if (ctx2abort == -1) {
				printk(KERN_ERR MYNAM ": ERROR - ScsiLookup fail(#1) for SCpnt=%p\n", SCpnt);
				SCpnt->result = DID_SOFT_ERROR << 16;
				spin_lock_irqsave(&io_request_lock, flags);
				SCpnt->scsi_done(SCpnt);
				spin_unlock_irqrestore(&io_request_lock, flags);
				mpt_free_msg_frame(ScsiTaskCtx, hd->ioc->id, mf);
				continue;
			}
			pScsiTm->LUN[1] = SCpnt->lun;
		}
		else if (task_type == MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS)
		{
			printk(KERN_WARNING MYNAM ": Attempting _BUS_RESET! (against SCSI IO mf=%p:sc=%p)\n", mf, SCpnt);
		}
#if 0
		else if (task_type == MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET) {}
		else if (task_type == MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET) {}
#endif

		printk(KERN_WARNING MYNAM ": IOs outstanding = %d\n", atomic_read(&queue_depth));

		pScsiTm->TargetID = SCpnt->target;
		pScsiTm->Bus = hd->port;
		pScsiTm->ChainOffset = 0;
		pScsiTm->Function = MPI_FUNCTION_SCSI_TASK_MGMT;

		pScsiTm->Reserved = 0;
		pScsiTm->TaskType = task_type;
		pScsiTm->Reserved1 = 0;
		pScsiTm->MsgFlags = 0;

		for (i = 0; i < 7; i++)
			pScsiTm->Reserved2[i] = 0;

		dprintk((KERN_INFO MYNAM ":DbG: ctx2abort = %08x\n", ctx2abort));
		pScsiTm->TaskMsgContext = ctx2abort;

		/* Control: No data direction, set task mgmt bit? */

		/*
		 *  As of MPI v0.10 this request can NOT be sent (normally)
		 *  via FIFOs.	So we can't:
		 *		mpt_put_msg_frame(ScsiTaskCtx, hd->ioc->id, mf);
		 *  SCSITaskMgmt requests MUST be sent ONLY via
		 *  Doorbell/handshake now.   :-(
		 */
		if ((i = mpt_send_handshake_request(ScsiTaskCtx, hd->ioc->id,
					sizeof(SCSITaskMgmt_t), (u32*) mf))
		    != 0) {
			printk(KERN_WARNING MYNAM ": WARNING[1] - IOC error (%d) processing TaskMgmt request (mf=%p:sc=%p)\n", i, mf, SCpnt);
			SCpnt->result = DID_SOFT_ERROR << 16;
			spin_lock_irqsave(&io_request_lock, flags);
			SCpnt->scsi_done(SCpnt);
			spin_unlock_irqrestore(&io_request_lock, flags);
			mpt_free_msg_frame(ScsiTaskCtx, hd->ioc->id, mf);
		} else {
			/* Spin-Wait for TaskMgmt complete!!! */
			while (mpt_scsih_active_taskmgmt_mf != NULL) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(HZ/4);
			}
		}
	}

	dslprintk((KERN_INFO MYNAM ": spinlock#6\n"));
	spin_lock_irqsave(&mpt_scsih_taskQ_lock, flags);
	mpt_scsih_taskQ_bh_active = 0;
	spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);

	return;
}

#endif		/* } */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptscsih_taskmgmt_complete - Callback routine, gets registered to
 *	Fusion MPT base	driver
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@mf: Pointer to SCSI task mgmt request frame
 *	@r: Pointer to SCSI task mgmt reply frame
 *
 *	This routine is called from mptbase.c::mpt_interrupt() at the completion
 *	of any SCSI task management request.
 *	This routine is registered with the MPT (base) driver at driver
 *	load/init time via the mpt_register() API call.
 *
 *	Returns 1 indicating alloc'd request frame ptr should be freed.
 */
static int
mptscsih_taskmgmt_complete(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, MPT_FRAME_HDR *r)
{
	SCSITaskMgmtReply_t	*pScsiTmReply;
	SCSITaskMgmt_t		*pScsiTmReq;
	u8			 tmType;
#ifndef MPT_SCSI_USE_NEW_EH
	unsigned long		 flags;
#endif

	dprintk((KERN_INFO MYNAM ": SCSI TaskMgmt completed mf=%p, r=%p\n",
		 mf, r));

#ifndef MPT_SCSI_USE_NEW_EH
	dslprintk((KERN_INFO MYNAM ": spinlock#7\n"));
	spin_lock_irqsave(&mpt_scsih_taskQ_lock, flags);
	/* It better be the active one! */
	if (mf != mpt_scsih_active_taskmgmt_mf) {
		printk(KERN_ERR MYNAM ": ERROR! Non-active TaskMgmt (=%p) completed!\n", mf);
		mpt_scsih_active_taskmgmt_mf = NULL;
		spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);
		return 1;
	}

#ifdef MPT_DEBUG
	if ((mf == NULL) ||
	    (mf >= MPT_INDEX_2_MFPTR(ioc, ioc->req_depth))) {
		printk(KERN_ERR MYNAM ": ERROR! NULL or BAD TaskMgmt ptr (=%p)!\n", mf);
		mpt_scsih_active_taskmgmt_mf = NULL;
		spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);
		return 1;
	}
#endif
	spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);
#endif

	if (r != NULL) {
		pScsiTmReply = (SCSITaskMgmtReply_t*)r;
		pScsiTmReq = (SCSITaskMgmt_t*)mf;

		/* Figure out if this was ABORT_TASK, TARGET_RESET, or BUS_RESET! */
		tmType = pScsiTmReq->TaskType;

		dprintk((KERN_INFO MYNAM ": TaskType = %d\n", tmType));
		dprintk((KERN_INFO MYNAM ": TerminationCount = %d\n",
			 le32_to_cpu(pScsiTmReply->TerminationCount)));

		/* Error?  (anything non-zero?) */
		if (*(u32 *)&pScsiTmReply->Reserved2[0]) {
			dprintk((KERN_INFO MYNAM ": SCSI TaskMgmt (%d) - Oops!\n", tmType));
			dprintk((KERN_INFO MYNAM ": IOCStatus = %04xh\n",
				 le16_to_cpu(pScsiTmReply->IOCStatus)));
			dprintk((KERN_INFO MYNAM ": IOCLogInfo = %08xh\n",
				 le32_to_cpu(pScsiTmReply->IOCLogInfo)));
		} else {
			dprintk((KERN_INFO MYNAM ": SCSI TaskMgmt (%d) SUCCESS!\n", tmType));
		}
	}

#ifndef MPT_SCSI_USE_NEW_EH
	/*
	 *  Signal to _bh thread that we finished.
	 */
	dslprintk((KERN_INFO MYNAM ": spinlock#8\n"));
	spin_lock_irqsave(&mpt_scsih_taskQ_lock, flags);
	mpt_scsih_active_taskmgmt_mf = NULL;
	spin_unlock_irqrestore(&mpt_scsih_taskQ_lock, flags);
#endif

	return 1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	This is anyones guess quite frankly.
 */

int
mptscsih_bios_param(Disk * disk, kdev_t dev, int *ip)
{
	int size;

	size = disk->capacity;
	ip[0] = 64;				/* heads			*/
	ip[1] = 32;				/* sectors			*/
	if ((ip[2] = size >> 11) > 1024) {	/* cylinders, test for big disk */
		ip[0] = 255;			/* heads			*/
		ip[1] = 63;			/* sectors			*/
		ip[2] = size / (255 * 63);	/* cylinders			*/
	}
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Private routines...
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* 19991030 -sralston
 *  Return absolute SCSI data direction:
 *     1 = _DATA_OUT
 *     0 = _DIR_NONE
 *    -1 = _DATA_IN
 */
static int
mptscsih_io_direction(Scsi_Cmnd *cmd)
{
	switch (cmd->cmnd[0]) {
	/*  _DATA_OUT commands	*/
	case WRITE_6:		case WRITE_10:		case WRITE_12:
	case WRITE_LONG:	case WRITE_SAME:	case WRITE_BUFFER:
	case WRITE_VERIFY:	case WRITE_VERIFY_12:
	case COMPARE:		case COPY:		case COPY_VERIFY:
	case SEARCH_EQUAL:	case SEARCH_HIGH:	case SEARCH_LOW:
	case SEARCH_EQUAL_12:	case SEARCH_HIGH_12:	case SEARCH_LOW_12:
	case MODE_SELECT:	case MODE_SELECT_10:	case LOG_SELECT:
	case SEND_DIAGNOSTIC:	case CHANGE_DEFINITION: case UPDATE_BLOCK:
	case SET_WINDOW:	case MEDIUM_SCAN:	case SEND_VOLUME_TAG:
	case REASSIGN_BLOCKS:
	case PERSISTENT_RESERVE_OUT:
	case 0xea:
		return 1;

	/*  No data transfer commands  */
	case SEEK_6:		case SEEK_10:
	case RESERVE:		case RELEASE:
	case TEST_UNIT_READY:
	case START_STOP:
	case ALLOW_MEDIUM_REMOVAL:
		return 0;

	/*  Conditional data transfer commands	*/
	case FORMAT_UNIT:
		if (cmd->cmnd[1] & 0x10)	/* FmtData (data out phase)? */
			return 1;
		else
			return 0;

	case VERIFY:
		if (cmd->cmnd[1] & 0x02)	/* VERIFY:BYTCHK (data out phase)? */
			return 1;
		else
			return 0;

	case RESERVE_10:
		if (cmd->cmnd[1] & 0x03)	/* RESERSE:{LongID|Extent} (data out phase)? */
			return 1;
		else
			return 0;

#if 0
	case REZERO_UNIT:	/* (or REWIND) */
	case SPACE:
	case ERASE:		case ERASE_10:
	case SYNCHRONIZE_CACHE:
	case LOCK_UNLOCK_CACHE:
#endif

	/*  Must be data _IN!  */
	default:
		return -1;
	}
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static void
copy_sense_data(Scsi_Cmnd *sc, MPT_SCSI_HOST *hd, MPT_FRAME_HDR *mf, SCSIIOReply_t *pScsiReply)
{
	MPT_SCSI_DEV	*mpt_sdev = NULL;
	u32		 sense_count = le32_to_cpu(pScsiReply->SenseCount);
	char		 devFoo[32];
	IO_Info_t	 thisIo;

	if (sc && sc->device)
		mpt_sdev = (MPT_SCSI_DEV*) sc->device->hostdata;

	if (sense_count) {
		u8 *sense_data;
		int req_index;

		/* Copy the sense received into the scsi command block. */
		req_index = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);
		sense_data = ((u8 *)hd->ioc->sense_buf_pool + (req_index * 256));
		memcpy(sc->sense_buffer, sense_data, SNS_LEN(sc));
		/* Cache SenseData for this SCSI device! */
		if (mpt_sdev) {
			memcpy(mpt_sdev->CachedSense.data, sense_data, sense_count);
			mpt_sdev->sense_sz = sense_count;
		}
	} else {
		dprintk((KERN_INFO MYNAM ": Hmmm... SenseData len=0! (?)\n"));
	}


	thisIo.cdbPtr = sc->cmnd;
	thisIo.sensePtr = sc->sense_buffer;
	thisIo.SCSIStatus = pScsiReply->SCSIStatus;
	thisIo.DoDisplay = 1;
	sprintf(devFoo, "ioc%d,scsi%d:%d", hd->ioc->id, sc->target, sc->lun);
	thisIo.DevIDStr = devFoo;
/* fubar */
	thisIo.dataPtr = NULL;
	thisIo.inqPtr = NULL;
	if (sc->device) {
		thisIo.inqPtr = sc->device->vendor-8;		/* FIXME!!! */
	}
	(void) mpt_ScsiHost_ErrorReport(&thisIo);

	return;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static u32
SCPNT_TO_MSGCTX(Scsi_Cmnd *sc)
{
	MPT_SCSI_HOST *hd;
	MPT_FRAME_HDR *mf;
	int i;

	hd = (MPT_SCSI_HOST *) sc->host->hostdata;

	for (i = 0; i < hd->ioc->req_depth; i++) {
		if (hd->ScsiLookup[i] == sc) {
			mf = MPT_INDEX_2_MFPTR(hd->ioc, i);
			return mf->u.frame.hwhdr.msgctxu.MsgContext;
		}
	}

	return -1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

/* see mptscsih.h */

#ifdef MPT_SCSIHOST_NEED_ENTRY_EXIT_HOOKUPS
	static Scsi_Host_Template driver_template = MPT_SCSIHOST;
#	include "../../scsi/scsi_module.c"
#endif


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mptscsih_ioc_reset(MPT_ADAPTER *ioc, int reset_phase)
{
	dprintk((KERN_INFO MYNAM ": IOC %s_reset routed to SCSI host driver!\n",
			reset_phase==MPT_IOC_PRE_RESET ? "pre" : "post"));

	if (reset_phase == MPT_IOC_PRE_RESET) {
		/* FIXME! Do pre-reset cleanup */
	} else {
		/* FIXME! Do post-reset cleanup */
	}

	return 1;		/* currently means nothing really */
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mptscsih_event_process(MPT_ADAPTER *ioc, EventNotificationReply_t *pEvReply)
{
	u8 event = le32_to_cpu(pEvReply->Event) & 0xFF;

	dprintk((KERN_INFO MYNAM ": MPT event (=%02Xh) routed to SCSI host driver!\n", event));

	switch (event) {
	case MPI_EVENT_UNIT_ATTENTION:			/* 03 */
		/* FIXME! */
		break;
	case MPI_EVENT_IOC_BUS_RESET:			/* 04 */
		/* FIXME! */
		break;
	case MPI_EVENT_EXT_BUS_RESET:			/* 05 */
		/* FIXME! */
		break;
	case MPI_EVENT_LOGOUT:				/* 09 */
		/* FIXME! */
		break;

		/*
		 *  CHECKME! Don't think we need to do
		 *  anything for these, but...
		 */
	case MPI_EVENT_RESCAN:				/* 06 */
	case MPI_EVENT_LINK_STATUS_CHANGE:		/* 07 */
	case MPI_EVENT_LOOP_STATE_CHANGE:		/* 08 */
		/*
		 *  CHECKME!  Falling thru...
		 */

	case MPI_EVENT_NONE:				/* 00 */
	case MPI_EVENT_LOG_DATA:			/* 01 */
	case MPI_EVENT_STATE_CHANGE:			/* 02 */
	case MPI_EVENT_EVENT_CHANGE:			/* 0A */
	default:
		dprintk((KERN_INFO MYNAM ": Ignoring event (=%02Xh)\n", event));
		break;
	}

	return 1;		/* currently means nothing really */
}

#if 0		/* { */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *	scsiherr.c - Fusion MPT SCSI Host driver error handling/reporting.
 *
 *	drivers/message/fusion/scsiherr.c
 */

//extern const char	**mpt_ScsiOpcodesPtr;	/* needed by mptscsih.c */
//extern ASCQ_Table_t	 *mpt_ASCQ_TablePtr;
//extern int		  mpt_ASCQ_TableSz;

/*  Lie!  */
#define MYNAM	"mptscsih"

#endif		/* } */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Private data...
 */
static ASCQ_Table_t *mptscsih_ASCQ_TablePtr;

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/* old symsense.c stuff... */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 * Private data...
 * To protect ourselves against those that would pass us bogus pointers
 */
static u8 dummyInqData[SCSI_STD_INQUIRY_BYTES]
    = { 0x1F, 0x00, 0x00, 0x00,
	0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static u8 dummySenseData[SCSI_STD_SENSE_BYTES]
    = { 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00 };
static u8 dummyCDB[16]
    = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static u8 dummyScsiData[16]
    = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

#if 0
static const char *PeripheralDeviceTypeString[32] = {
	"Direct-access",		/* 00h */
	"Sequential-access",		/* 01h */
	"Printer",			/* 02h */
	"Processor",			/* 03h */
			/*"Write-Once-Read-Multiple",*/	/* 04h */
	"WORM",				/* 04h */
	"CD-ROM",			/* 05h */
	"Scanner",			/* 06h */
	"Optical memory",		/* 07h */
	"Media Changer",		/* 08h */
	"Communications",		/* 09h */
	"(Graphics arts pre-press)",	/* 0Ah */
	"(Graphics arts pre-press)",	/* 0Bh */
	"Array controller",		/* 0Ch */
	"Enclosure services",		/* 0Dh */
	"Simplified direct-access",	/* 0Eh */
	"Reserved-0Fh",			/* 0Fh */
	"Reserved-10h",			/* 10h */
	"Reserved-11h",			/* 11h */
	"Reserved-12h",			/* 12h */
	"Reserved-13h",			/* 13h */
	"Reserved-14h",			/* 14h */
	"Reserved-15h",			/* 15h */
	"Reserved-16h",			/* 16h */
	"Reserved-17h",			/* 17h */
	"Reserved-18h",			/* 18h */
	"Reserved-19h",			/* 19h */
	"Reserved-1Ah",			/* 1Ah */
	"Reserved-1Bh",			/* 1Bh */
	"Reserved-1Ch",			/* 1Ch */
	"Reserved-1Dh",			/* 1Dh */
	"Reserved-1Eh",			/* 1Eh */
	"Unknown"			/* 1Fh */
};
#endif

static char *ScsiStatusString[] = {
	"GOOD",					/* 00h */
	NULL,					/* 01h */
	"CHECK CONDITION",			/* 02h */
	NULL,					/* 03h */
	"CONDITION MET",			/* 04h */
	NULL,					/* 05h */
	NULL,					/* 06h */
	NULL,					/* 07h */
	"BUSY",					/* 08h */
	NULL,					/* 09h */
	NULL,					/* 0Ah */
	NULL,					/* 0Bh */
	NULL,					/* 0Ch */
	NULL,					/* 0Dh */
	NULL,					/* 0Eh */
	NULL,					/* 0Fh */
	"INTERMEDIATE",				/* 10h */
	NULL,					/* 11h */
	NULL,					/* 12h */
	NULL,					/* 13h */
	"INTERMEDIATE-CONDITION MET",		/* 14h */
	NULL,					/* 15h */
	NULL,					/* 16h */
	NULL,					/* 17h */
	"RESERVATION CONFLICT",			/* 18h */
	NULL,					/* 19h */
	NULL,					/* 1Ah */
	NULL,					/* 1Bh */
	NULL,					/* 1Ch */
	NULL,					/* 1Dh */
	NULL,					/* 1Eh */
	NULL,					/* 1Fh */
	NULL,					/* 20h */
	NULL,					/* 21h */
	"COMMAND TERMINATED",			/* 22h */
	NULL,					/* 23h */
	NULL,					/* 24h */
	NULL,					/* 25h */
	NULL,					/* 26h */
	NULL,					/* 27h */
	"TASK SET FULL",			/* 28h */
	NULL,					/* 29h */
	NULL,					/* 2Ah */
	NULL,					/* 2Bh */
	NULL,					/* 2Ch */
	NULL,					/* 2Dh */
	NULL,					/* 2Eh */
	NULL,					/* 2Fh */
	"ACA ACTIVE",				/* 30h */
	NULL
};

static const char *ScsiCommonOpString[] = {
	"TEST UNIT READY",			/* 00h */
	"REZERO UNIT (REWIND)",			/* 01h */
	NULL,					/* 02h */
	"REQUEST_SENSE",			/* 03h */
	"FORMAT UNIT (MEDIUM)",			/* 04h */
	"READ BLOCK LIMITS",			/* 05h */
	NULL,					/* 06h */
	"REASSIGN BLOCKS",			/* 07h */
	"READ(6)",				/* 08h */
	NULL,					/* 09h */
	"WRITE(6)",				/* 0Ah */
	"SEEK(6)",				/* 0Bh */
	NULL,					/* 0Ch */
	NULL,					/* 0Dh */
	NULL,					/* 0Eh */
	"READ REVERSE",				/* 0Fh */
	"WRITE_FILEMARKS",			/* 10h */
	"SPACE(6)",				/* 11h */
	"INQUIRY",				/* 12h */
	NULL
};

static const char *SenseKeyString[] = {
	"NO SENSE",				/* 0h */
	"RECOVERED ERROR",			/* 1h */
	"NOT READY",				/* 2h */
	"MEDIUM ERROR",				/* 3h */
	"HARDWARE ERROR",			/* 4h */
	"ILLEGAL REQUEST",			/* 5h */
	"UNIT ATTENTION",			/* 6h */
	"DATA PROTECT",				/* 7h */
	"BLANK CHECK",				/* 8h */
	"VENDOR-SPECIFIC",			/* 9h */
	"ABORTED COPY",				/* Ah */
	"ABORTED COMMAND",			/* Bh */
	"EQUAL (obsolete)",			/* Ch */
	"VOLUME OVERFLOW",			/* Dh */
	"MISCOMPARE",				/* Eh */
	"RESERVED",				/* Fh */
	NULL
};

#define SPECIAL_ASCQ(c,q) \
	(((c) == 0x40 && (q) != 0x00) || ((c) == 0x4D) || ((c) == 0x70))

#if 0
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Sense_Key_Specific() - If Sense_Key_Specific_Valid bit is set,
 *			   then print additional information via
 *			   a call to SDMS_SystemAlert().
 *
 *  Return: nothing
 */
static void Sense_Key_Specific(IO_Info_t *ioop, char *msg1)
{
	u8	*sd;
	u8	 BadValue;
	u8	 SenseKey;
	int	 Offset;
	int	 len = strlen(msg1);

	sd = ioop->sensePtr;
	if (SD_Additional_Sense_Length(sd) < 8)
		return;

	SenseKey = SD_Sense_Key(sd);

	if (SD_Sense_Key_Specific_Valid(sd)) {
		if (SenseKey == SK_ILLEGAL_REQUEST) {
			Offset = SD_Bad_Byte(sd);
			if (SD_Was_Illegal_Request(sd)) {
				BadValue = ioop->cdbPtr[Offset];
				len += sprintf(msg1+len, "\n  Illegal CDB value=%02Xh found at CDB ",
						BadValue);
		} else {
			BadValue = ioop->dataPtr[Offset];
			len += sprintf(msg1+len, "\n  Illegal DATA value=%02Xh found at DATA ",
					BadValue);
		}
		len += sprintf(msg1+len, "byte=%02Xh", Offset);
		if (SD_SKS_Bit_Pointer_Valid(sd))
			len += sprintf(msg1+len, "/bit=%1Xh", SD_SKS_Bit_Pointer(sd));
		} else if ((SenseKey == SK_RECOVERED_ERROR) ||
			   (SenseKey == SK_HARDWARE_ERROR) ||
			   (SenseKey == SK_MEDIUM_ERROR)) {
			len += sprintf(msg1+len, "\n  Recovery algorithm Actual_Retry_Count=%02Xh",
			SD_Actual_Retry_Count(sd));
		}
	}
}
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int dump_cdb(char *foo, unsigned char *cdb)
{
	int i, grpCode, cdbLen;
	int l = 0;

	grpCode = cdb[0] >> 5;
	if (grpCode < 1)
		cdbLen = 6;
	else if (grpCode < 3)
		cdbLen = 10;
	else if (grpCode == 5)
		cdbLen = 12;
	else
		cdbLen = 16;

	for (i=0; i < cdbLen; i++)
		l += sprintf(foo+l, " %02X", cdb[i]);

	return l;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int dump_sd(char *foo, unsigned char *sd)
{
	int snsLen = 8 + SD_Additional_Sense_Length(sd);
	int l = 0;
	int i;

	for (i=0; i < MIN(snsLen,18); i++)
		l += sprintf(foo+l, " %02X", sd[i]);
	l += sprintf(foo+l, "%s", snsLen>18 ? " ..." : "");

	return l;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*  Do ASC/ASCQ lookup/grindage to English readable string(s)  */
static const char * ascq_set_strings_4max(
		u8 ASC, u8 ASCQ,
		const char **s1, const char **s2, const char **s3, const char **s4)
{
	static const char *asc_04_part1_string = "LOGICAL UNIT ";
	static const char *asc_04_part2a_string = "NOT READY, ";
	static const char *asc_04_part2b_string = "IS ";
	static const char *asc_04_ascq_NN_part3_strings[] = {	/* ASC ASCQ (hex) */
	  "CAUSE NOT REPORTABLE",				/* 04 00 */
	  "IN PROCESS OF BECOMING READY",			/* 04 01 */
	  "INITIALIZING CMD. REQUIRED",				/* 04 02 */
	  "MANUAL INTERVENTION REQUIRED",			/* 04 03 */
	  /* Add	" IN PROGRESS" to all the following... */
	  "FORMAT",						/* 04 04 */
	  "REBUILD",						/* 04 05 */
	  "RECALCULATION",					/* 04 06 */
	  "OPERATION",						/* 04 07 */
	  "LONG WRITE",						/* 04 08 */
	  "SELF-TEST",						/* 04 09 */
	  NULL
	};
	static char *asc_04_part4_string = " IN PROGRESS";

	static char *asc_29_ascq_NN_strings[] = {		/* ASC ASCQ (hex) */
	  "POWER ON, RESET, OR BUS DEVICE RESET OCCURRED",	/* 29 00 */
	  "POWER ON OCCURRED",					/* 29 01 */
	  "SCSI BUS RESET OCCURRED",				/* 29 02 */
	  "BUS DEVICE RESET FUNCTION OCCURRED",			/* 29 03 */
	  "DEVICE INTERNAL RESET",				/* 29 04 */
	  "TRANSCEIVER MODE CHANGED TO SINGLE-ENDED",		/* 29 05 */
	  "TRANSCEIVER MODE CHANGED TO LVD",			/* 29 06 */
	  NULL
	};
	static char *ascq_vendor_uniq = "(Vendor Unique)";
	static char *ascq_noone = "(no matching ASC/ASCQ description found)";
	int idx;

	*s1 = *s2 = *s3 = *s4 = "";		/* set'em all to the empty "" string */

	/* CHECKME! Need lock/sem?
	 *  Update and examine for isense module presense.
	 */
	mptscsih_ASCQ_TablePtr = (ASCQ_Table_t *)mpt_v_ASCQ_TablePtr;

	if (mptscsih_ASCQ_TablePtr == NULL) {
		/* 2nd chances... */
		if (ASC == 0x04 && (ASCQ < sizeof(asc_04_ascq_NN_part3_strings)/sizeof(char*)-1)) {
			*s1 = asc_04_part1_string;
			*s2 = (ASCQ == 0x01) ? asc_04_part2b_string : asc_04_part2a_string;
			*s3 = asc_04_ascq_NN_part3_strings[ASCQ];
			/* check for " IN PROGRESS" ones */
			if (ASCQ >= 0x04)
				*s4 = asc_04_part4_string;
		} else if (ASC == 0x29 && (ASCQ < sizeof(asc_29_ascq_NN_strings)/sizeof(char*)-1))
			*s1 = asc_29_ascq_NN_strings[ASCQ];
		/*
		 *	else { leave all *s[1-4] values pointing to the empty "" string }
		 */
		return *s1;
	}

	/*
	 *  Need to check ASC here; if it is "special," then
	 *  the ASCQ is variable, and indicates failed component number.
	 *  We must treat the ASCQ as a "don't care" while searching the
	 *  mptscsih_ASCQ_Table[] by masking it off, and then restoring it later
	 *  on when we actually need to identify the failed component.
	 */
	if (SPECIAL_ASCQ(ASC,ASCQ))
		ASCQ = 0xFF;

	/*  OK, now search mptscsih_ASCQ_Table[] for a matching entry  */
	for (idx = 0; mptscsih_ASCQ_TablePtr && idx < mpt_ASCQ_TableSz; idx++)
		if ((ASC == mptscsih_ASCQ_TablePtr[idx].ASC) && (ASCQ == mptscsih_ASCQ_TablePtr[idx].ASCQ))
			return (*s1 = mptscsih_ASCQ_TablePtr[idx].Description);

	if ((ASC >= 0x80) || (ASCQ >= 0x80))
		*s1 = ascq_vendor_uniq;
	else
		*s1 = ascq_noone;

	return *s1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  SCSI Error Report; desired output format...
 *---
SCSI Error Report =-=-=-=-=-=-=-=-=-=-=-=-=-= (ioc0,scsi0:0)
  SCSI_Status=02h (CHECK CONDITION)
  Original_CDB[]: 00 00 00 00 00 00 - TestUnitReady
  SenseData[12h]: 70 00 06 00 00 00 00 0A 00 00 00 00 29 00 03 00 00 00
  SenseKey=6h (UNIT ATTENTION); FRU=03h
  ASC/ASCQ=29h/00h, "POWER ON, RESET, OR BUS DEVICE RESET OCCURRED"
 *---
 */

int mpt_ScsiHost_ErrorReport(IO_Info_t *ioop)
{
	char		 foo[512];
	char		 buf2[32];
	char		*statstr;
	const char	*opstr;
	int		 sk		= SD_Sense_Key(ioop->sensePtr);
	const char	*skstr		= SenseKeyString[sk];
	unsigned char	 asc		= SD_ASC(ioop->sensePtr);
	unsigned char	 ascq		= SD_ASCQ(ioop->sensePtr);
	int		 l;

	/*
	 *  More quiet mode.
	 *  Filter out common, repetitive, warning-type errors...  like:
	 *    POWER ON (06,29/00 or 06,29/01),
	 *    SPINNING UP (02,04/01),
	 *    LOGICAL UNIT NOT SUPPORTED (05,25/00), etc.
	 */
	if (	(sk==SK_UNIT_ATTENTION	&& asc==0x29 && (ascq==0x00 || ascq==0x01))
	     || (sk==SK_NOT_READY	&& asc==0x04 && ascq==0x01)
	     || (sk==SK_ILLEGAL_REQUEST && asc==0x25 && ascq==0x00)
	   )
	{
		/* Do nothing! */
		return 0;
	}

	/*
	 *  Protect ourselves...
	 */
	if (ioop->cdbPtr == NULL)
		ioop->cdbPtr = dummyCDB;
	if (ioop->sensePtr == NULL)
		ioop->sensePtr = dummySenseData;
	if (ioop->inqPtr == NULL)
		ioop->inqPtr = dummyInqData;
	if (ioop->dataPtr == NULL)
		ioop->dataPtr = dummyScsiData;

	statstr = NULL;
	if ((ioop->SCSIStatus >= sizeof(ScsiStatusString)/sizeof(char*)-1) ||
	    ((statstr = (char*)ScsiStatusString[ioop->SCSIStatus]) == NULL)) {
		(void) sprintf(buf2, "Bad-Reserved-%02Xh", ioop->SCSIStatus);
		statstr = buf2;
	}

	opstr = NULL;
	if (1+ioop->cdbPtr[0] <= sizeof(ScsiCommonOpString)/sizeof(char*))
		opstr = ScsiCommonOpString[ioop->cdbPtr[0]];
	else if (mpt_ScsiOpcodesPtr)
		opstr = mpt_ScsiOpcodesPtr[ioop->cdbPtr[0]];

	l = sprintf(foo, "SCSI Error Report =-=-= (%s)\n"
	  "  SCSI_Status=%02Xh (%s)\n"
	  "  Original_CDB[]:",
			ioop->DevIDStr,
			ioop->SCSIStatus,
			statstr);
	l += dump_cdb(foo+l, ioop->cdbPtr);
	if (opstr)
		l += sprintf(foo+l, " - \"%s\"", opstr);
	l += sprintf(foo+l, "\n  SenseData[%02Xh]:", 8+SD_Additional_Sense_Length(ioop->sensePtr));
	l += dump_sd(foo+l, ioop->sensePtr);
	l += sprintf(foo+l, "\n  SenseKey=%Xh (%s); FRU=%02Xh\n  ASC/ASCQ=%02Xh/%02Xh",
			sk, skstr, SD_FRU(ioop->sensePtr), asc, ascq );

	{
		const char	*x1, *x2, *x3, *x4;
		x1 = x2 = x3 = x4 = "";
		x1 = ascq_set_strings_4max(asc, ascq, &x1, &x2, &x3, &x4);
		if (x1 != NULL) {
			if (x1[0] != '(')
				l += sprintf(foo+l, " \"%s%s%s%s\"", x1,x2,x3,x4);
			else
				l += sprintf(foo+l, " %s%s%s%s", x1,x2,x3,x4);
		}
	}

#if 0
	if (SPECIAL_ASCQ(asc,ascq))
		l += sprintf(foo+l, " (%02Xh)", ascq);
#endif

	PrintF(("%s\n", foo));

	return l;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

