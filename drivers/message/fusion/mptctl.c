/*
 *  linux/drivers/message/fusion/mptctl.c
 *      Fusion MPT misc device (ioctl) driver.
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
 *      A big THANKS to Eddie C. Dost for fixing the ioctl path
 *      and most importantly f/w download on sparc64 platform!
 *      (plus Eddie's other helpful hints and insights)
 *
 *      Thanks to Arnaldo Carvalho de Melo for finding and patching
 *      a potential memory leak in mpt_ioctl_do_fw_download(),
 *      and for some kmalloc insight:-)
 *
 *      (see also mptbase.c)
 *
 *  Copyright (c) 1999-2001 LSI Logic Corporation
 *  Originally By: Steven J. Ralston, Noah Romer
 *  (mailto:Steve.Ralston@lsil.com)
 *
 *  $Id: mptctl.c,v 1.25.4.1 2001/08/24 20:07:06 sralston Exp $
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

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/proc_fs.h>

#define COPYRIGHT	"Copyright (c) 1999-2001 LSI Logic Corporation"
#define MODULEAUTHOR	"Steven J. Ralston, Noah Romer"
#include "mptbase.h"

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#define my_NAME		"Fusion MPT misc device (ioctl) driver"
#define my_VERSION	MPT_LINUX_VERSION_COMMON
#define MYNAM		"mptctl"

EXPORT_NO_SYMBOLS;
MODULE_AUTHOR(MODULEAUTHOR);
MODULE_DESCRIPTION(my_NAME);
MODULE_LICENSE("GPL");


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

static int mptctl_id = -1;
static int rwperf_reset = 0;
static struct semaphore mptctl_syscall_sem_ioc[MPT_MAX_ADAPTERS];

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

static int mpt_ioctl_rwperf(unsigned long arg);
static int mpt_ioctl_rwperf_status(unsigned long arg);
static int mpt_ioctl_rwperf_reset(unsigned long arg);
static int mpt_ioctl_fw_download(unsigned long arg);
static int mpt_ioctl_do_fw_download(int ioc, char *ufwbuf, size_t fwlen);
static int mpt_ioctl_scsi_cmd(unsigned long arg);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 * Scatter gather list (SGL) sizes and limits...
 */
//#define MAX_SCSI_FRAGS	9
#define MAX_FRAGS_SPILL1	9
#define MAX_FRAGS_SPILL2	15
#define FRAGS_PER_BUCKET	(MAX_FRAGS_SPILL2 + 1)

//#define MAX_CHAIN_FRAGS	64
//#define MAX_CHAIN_FRAGS	(15+15+15+16)
#define MAX_CHAIN_FRAGS		(4 * MAX_FRAGS_SPILL2 + 1)

//  Define max sg LIST bytes ( == (#frags + #chains) * 8 bytes each)
//  Works out to: 592d bytes!     (9+1)*8 + 4*(15+1)*8
//                  ^----------------- 80 + 512
#define MAX_SGL_BYTES		((MAX_FRAGS_SPILL1 + 1 + (4 * FRAGS_PER_BUCKET)) * 8)

/* linux only seems to ever give 128kB MAX contiguous (GFP_USER) mem bytes */
#define MAX_KMALLOC_SZ		(128*1024)

struct buflist {
	u8	*kptr;
	int	 len;
};

#define myMAX_TARGETS	(1<<4)
#define myMAX_LUNS	(1<<3)
#define myMAX_T_MASK	(myMAX_TARGETS-1)
#define myMAX_L_MASK	(myMAX_LUNS-1)
static u8  DevInUse[myMAX_TARGETS][myMAX_LUNS] = {{0,0}};
static u32 DevIosCount[myMAX_TARGETS][myMAX_LUNS] = {{0,0}};

static u32 fwReplyBuffer[16];
static pMPIDefaultReply_t ReplyMsg = NULL;

/* some private forw protos */
static SGESimple32_t *kbuf_alloc_2_sgl( int bytes, u32 dir, int *frags,
		struct buflist **blp, dma_addr_t *sglbuf_dma, MPT_ADAPTER *ioc);
static void kfree_sgl( SGESimple32_t *sgl, dma_addr_t sgl_dma,
		struct buflist *buflist, MPT_ADAPTER *ioc);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/**
 *	mptctl_syscall_down - Down the MPT adapter syscall semaphore.
 *	@ioc: Pointer to MPT adapter
 *	@nonblock: boolean, non-zero if O_NONBLOCK is set
 *
 *	All of the mptctl commands can potentially sleep, which is illegal
 *	with a spinlock held, thus we perform mutual exclusion here.
 *
 *	Returns negative errno on error, or zero for success.
 */
static inline int
mptctl_syscall_down(MPT_ADAPTER *ioc, int nonblock)
{
	dprintk((KERN_INFO MYNAM "::mpt_syscall_down(%p,%d) called\n", ioc, nonblock));

	if (nonblock) {
		if (down_trylock(&mptctl_syscall_sem_ioc[ioc->id]))
			return -EAGAIN;
	} else {
		if (down_interruptible(&mptctl_syscall_sem_ioc[ioc->id]))
			return -ERESTARTSYS;
	}
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  This is the callback for any message we have posted. The message itself
 *  will be returned to the message pool when we return from the IRQ
 *
 *  This runs in irq context so be short and sweet.
 */
static int
mptctl_reply(MPT_ADAPTER *ioc, MPT_FRAME_HDR *req, MPT_FRAME_HDR *reply)
{
	u8 targ;

	//dprintk((KERN_DEBUG MYNAM ": Got mptctl_reply()!\n"));

	if (req && req->u.hdr.Function == MPI_FUNCTION_SCSI_IO_REQUEST) {
		targ = req->u.scsireq.TargetID & myMAX_T_MASK;
		DevIosCount[targ][0]--;
	} else if (reply && req && req->u.hdr.Function == MPI_FUNCTION_FW_DOWNLOAD) {
		// NOTE: Expects/requires non-Turbo reply!
		dprintk((KERN_INFO MYNAM ": Caching MPI_FUNCTION_FW_DOWNLOAD reply!\n"));
		memcpy(fwReplyBuffer, reply, MIN(sizeof(fwReplyBuffer), 4*reply->u.reply.MsgLength));
		ReplyMsg = (pMPIDefaultReply_t) fwReplyBuffer;
	}

	return 1;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  struct file_operations functionality. 
 *  Members:
 *	llseek, write, read, ioctl, open, release
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,9)
static loff_t
mptctl_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}
#define no_llseek mptctl_llseek
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static ssize_t
mptctl_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	printk(KERN_ERR MYNAM ": ioctl WRITE not yet supported\n");
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static ssize_t
mptctl_read(struct file *file, char *buf, size_t count, loff_t *ptr)
{
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  MPT ioctl handler
 */
static int
mpt_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct mpt_ioctl_sanity	*usanity = (struct mpt_ioctl_sanity *) arg;
	struct mpt_ioctl_sanity	 ksanity;
	int iocnum;
	unsigned iocnumX;
	int nonblock = (file->f_flags & O_NONBLOCK);
	int ret;
	MPT_ADAPTER *iocp = NULL;

	dprintk((KERN_INFO MYNAM "::mpt_ioctl() called\n"));

	if (copy_from_user(&ksanity, usanity, sizeof(ksanity))) {
		printk(KERN_ERR "%s::mpt_ioctl() @%d - "
				"Unable to copy mpt_ioctl_sanity data @ %p\n",
				__FILE__, __LINE__, (void*)usanity);
		return -EFAULT;
	}
	ret = -ENXIO;				/* (-6) No such device or address */

	/* Verify intended MPT adapter */
	iocnumX = ksanity.iocnum & 0xFF;
	if (((iocnum = mpt_verify_adapter(iocnumX, &iocp)) < 0) ||
	    (iocp == NULL)) {
		printk(KERN_ERR "%s::mpt_ioctl() @%d - ioc%d not found!\n",
				__FILE__, __LINE__, iocnumX);
		return -ENODEV;
	}

	if ((ret = mptctl_syscall_down(iocp, nonblock)) != 0)
		return ret;

	dprintk((KERN_INFO MYNAM "::mpt_ioctl() - Using %s\n", iocp->name));

	switch(cmd) {
	case MPTRWPERF:
		ret = mpt_ioctl_rwperf(arg);
		break;
	case MPTRWPERF_CHK:
		ret = mpt_ioctl_rwperf_status(arg);
		break;
	case MPTRWPERF_RESET:
		ret = mpt_ioctl_rwperf_reset(arg);
		break;
	case MPTFWDOWNLOAD:
		ret = mpt_ioctl_fw_download(arg);
		break;
	case MPTSCSICMD:
		ret = mpt_ioctl_scsi_cmd(arg);
		break;
	default:
		ret = -EINVAL;
	}

	up(&mptctl_syscall_sem_ioc[iocp->id]);

	return ret;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int mptctl_open(struct inode *inode, struct file *file)
{
	/*
	 * Should support multiple management users
	 */
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int mptctl_release(struct inode *inode, struct file *file)
{
	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_ioctl_fw_download(unsigned long arg)
{
	struct mpt_fw_xfer	*ufwdl = (struct mpt_fw_xfer *) arg;
	struct mpt_fw_xfer	 kfwdl;

	dprintk((KERN_INFO "mpt_ioctl_fwdl called. mptctl_id = %xh\n", mptctl_id)); //tc
	if (copy_from_user(&kfwdl, ufwdl, sizeof(struct mpt_fw_xfer))) {
		printk(KERN_ERR "%s@%d::_ioctl_fwdl - "
				"Unable to copy mpt_fw_xfer struct @ %p\n",
				__FILE__, __LINE__, (void*)ufwdl);
		return -EFAULT;
	}

	return mpt_ioctl_do_fw_download(kfwdl.iocnum, kfwdl.bufp, kfwdl.fwlen);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 * MPT FW Download
 */
static int
mpt_ioctl_do_fw_download(int ioc, char *ufwbuf, size_t fwlen)
{
	FWDownload_t		*dlmsg;
	MPT_FRAME_HDR		*mf;
	MPT_ADAPTER		*iocp;
//	char			*fwbuf;
//	dma_addr_t		 fwbuf_dma;
	FWDownloadTCSGE_t	*fwVoodoo;
//	SGEAllUnion_t		*fwSgl;
	int			 ret;

	SGESimple32_t	*sgl;
	SGESimple32_t	*sgOut, *sgIn;
	dma_addr_t	 sgl_dma;
	struct buflist	*buflist = NULL;
	struct buflist	*bl = NULL;
	int		 numfrags = 0;
	int		 maxfrags;
	int		 n = 0;
	u32		 sgdir;
	u32		 nib;
	int		 fw_bytes_copied = 0;
	u16		 iocstat;
	int		 i;

	dprintk((KERN_INFO "mpt_ioctl_do_fwdl called. mptctl_id = %xh.\n", mptctl_id));

	dprintk((KERN_INFO "DbG: kfwdl.bufp  = %p\n", ufwbuf));
	dprintk((KERN_INFO "DbG: kfwdl.fwlen = %d\n", (int)fwlen));
	dprintk((KERN_INFO "DbG: kfwdl.ioc   = %04xh\n", ioc));

	if ((ioc = mpt_verify_adapter(ioc, &iocp)) < 0) {
		printk("%s@%d::_ioctl_fwdl - ioc%d not found!\n",
				__FILE__, __LINE__, ioc);
		return -ENXIO; /* (-6) No such device or address */
	}

	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL)
		return -EAGAIN;
	dlmsg = (FWDownload_t*) mf;
	fwVoodoo = (FWDownloadTCSGE_t *) &dlmsg->SGL;
	sgOut = (SGESimple32_t *) (fwVoodoo + 1);

	/*
	 * Construct f/w download request
	 */
	dlmsg->ImageType = MPI_FW_DOWNLOAD_ITYPE_FW;
	dlmsg->Reserved = 0;
	dlmsg->ChainOffset = 0;
	dlmsg->Function = MPI_FUNCTION_FW_DOWNLOAD;
	dlmsg->Reserved1[0] = dlmsg->Reserved1[1] = dlmsg->Reserved1[2] = 0;
	dlmsg->MsgFlags = 0;

	fwVoodoo->Reserved = 0;
	fwVoodoo->ContextSize = 0;
	fwVoodoo->DetailsLength = 12;
	fwVoodoo->Flags = MPI_SGE_FLAGS_TRANSACTION_ELEMENT;
	fwVoodoo->Reserved1 = 0;
	fwVoodoo->ImageOffset = 0;
	fwVoodoo->ImageSize = cpu_to_le32(fwlen);

	/*
	 * Need to kmalloc area(s) for holding firmware image bytes.
	 * But we need to do it piece meal, using a proper
	 * scatter gather list (with 128kB MAX hunks).
	 * 
	 * A practical limit here might be # of sg hunks that fit into
	 * a single IOC request frame; 12 or 8 (see below), so:
	 * For FC9xx: 12 x 128kB == 1.5 mB (max)
	 * For C1030:  8 x 128kB == 1   mB (max)
	 * We could support chaining, but things get ugly(ier:)
	 */
	sgdir = 0x04000000;		/* IOC will READ from sys mem */
	if ((sgl = kbuf_alloc_2_sgl(fwlen, sgdir, &numfrags, &buflist, &sgl_dma, iocp)) == NULL)
		return -ENOMEM;

	/*
	 * We should only need SGL with 2 simple_32bit entries (up to 256 kB)
	 * for FC9xx f/w image, but calculate max number of sge hunks
	 * we can fit into a request frame, and limit ourselves to that.
	 * (currently no chain support)
	 * For FC9xx: (128-12-16)/8 = 12.5 = 12
	 * For C1030:  (96-12-16)/8 =  8.5 =  8
	 */
	maxfrags = (iocp->req_sz - sizeof(MPIHeader_t) - sizeof(FWDownloadTCSGE_t)) / sizeof(SGESimple32_t);
	if (numfrags > maxfrags) {
		ret = -EMLINK;
		goto fwdl_out;
	}

	dprintk((KERN_INFO "DbG: sgl buffer  = %p, sgfrags = %d\n", sgl, numfrags));

	/*
	 * Parse SG list, copying sgl itself,
	 * plus f/w image hunks from user space as we go...
	 */
	ret = -EFAULT;
	sgIn = sgl;
	bl = buflist;
	for (i=0; i < numfrags; i++) {
		nib = (le32_to_cpu(sgIn->FlagsLength) & 0xF0000000) >> 28;
		/* skip ignore/chain. */
		if (nib == 0 || nib == 3) {
			;
		} else if (sgIn->Address) {
			*sgOut = *sgIn;
			n++;
			if (copy_from_user(bl->kptr, ufwbuf+fw_bytes_copied, bl->len)) {
				printk(KERN_ERR "%s@%d::_ioctl_fwdl - "
						"Unable to copy f/w buffer hunk#%d @ %p\n",
						__FILE__, __LINE__, n, (void*)ufwbuf);
				goto fwdl_out;
			}
			fw_bytes_copied += bl->len;
		}
		sgIn++;
		bl++;
		sgOut++;
	}

#ifdef MPT_DEBUG
	{
		u32 *m = (u32 *)mf;
		printk(KERN_INFO MYNAM ": F/W download request:\n" KERN_INFO " ");
		for (i=0; i < 7+numfrags*2; i++)
			printk(" %08x", le32_to_cpu(m[i]));
		printk("\n");
	}
#endif

	/*
	 * Finally, perform firmware download.
	 */
	ReplyMsg = NULL;
	mpt_put_msg_frame(mptctl_id, ioc, mf);

	/*
	 *  Wait until the reply has been received
	 */
	{
		int	 foo = 0;

		while (ReplyMsg == NULL) {
			if (!(foo%1000000)) {
				dprintk((KERN_INFO "DbG::_do_fwdl: "
					   "In ReplyMsg loop - iteration %d\n",
					   foo)); //tc
			}
			ret = -ETIME;
			if (++foo > 60000000)
				goto fwdl_out;
			mb();
			schedule();
			barrier();
		}
	}

	if (sgl)
        	kfree_sgl(sgl, sgl_dma, buflist, iocp);

	iocstat = le16_to_cpu(ReplyMsg->IOCStatus) & MPI_IOCSTATUS_MASK;
	if (iocstat == MPI_IOCSTATUS_SUCCESS) {
		printk(KERN_INFO MYNAM ": F/W update successfully sent to %s!\n", iocp->name);
		return 0;
	} else if (iocstat == MPI_IOCSTATUS_INVALID_FUNCTION) {
		printk(KERN_WARNING MYNAM ": ?Hmmm...  %s says it doesn't support F/W download!?!\n",
				iocp->name);
		printk(KERN_WARNING MYNAM ": (time to go bang on somebodies door)\n");
		return -EBADRQC;
	} else if (iocstat == MPI_IOCSTATUS_BUSY) {
		printk(KERN_WARNING MYNAM ": Warning!  %s says: IOC_BUSY!\n", iocp->name);
		printk(KERN_WARNING MYNAM ": (try again later?)\n");
		return -EBUSY;
	} else {
		printk(KERN_WARNING MYNAM "::ioctl_fwdl() ERROR!  %s returned [bad] status = %04xh\n",
				    iocp->name, iocstat);
		printk(KERN_WARNING MYNAM ": (bad VooDoo)\n");
		return -ENOMSG;
	}
	return 0;

fwdl_out:
        kfree_sgl(sgl, sgl_dma, buflist, iocp);
	return ret;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  NEW rwperf (read/write performance) stuff starts here...
 */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static SGESimple32_t *
kbuf_alloc_2_sgl(int bytes, u32 sgdir, int *frags,
		 struct buflist **blp, dma_addr_t *sglbuf_dma, MPT_ADAPTER *ioc)
{
	SGESimple32_t	*sglbuf = NULL;
	struct buflist	*buflist = NULL;
	int		 numfrags = 0;
	int		 fragcnt = 0;
	int		 alloc_sz = MIN(bytes,MAX_KMALLOC_SZ);	// avoid kernel warning msg!
	int		 bytes_allocd = 0;
	int		 this_alloc;
	SGESimple32_t	*sgl;
	u32		 pa;					// phys addr
	SGEChain32_t	*last_chain = NULL;
	SGEChain32_t	*old_chain = NULL;
	int		 chaincnt = 0;
	int		 i, buflist_ent;
	int		 sg_spill = MAX_FRAGS_SPILL1;
	int		 dir;

	*frags = 0;
	*blp = NULL;
	i = MAX_SGL_BYTES / 8;
	buflist = kmalloc(i, GFP_USER);
	if (buflist == NULL)
		return NULL;
	memset(buflist, 0, i);
	buflist_ent = 0;

	sglbuf = pci_alloc_consistent(ioc->pcidev, MAX_SGL_BYTES, sglbuf_dma);
	if (sglbuf == NULL)
		goto free_and_fail;

	if (sgdir & 0x04000000)
		dir = PCI_DMA_TODEVICE;
	else
		dir = PCI_DMA_FROMDEVICE;

	sgl = sglbuf;
	while (bytes_allocd < bytes) {
		this_alloc = MIN(alloc_sz, bytes-bytes_allocd);
		buflist[buflist_ent].len = this_alloc;
		buflist[buflist_ent].kptr = pci_alloc_consistent(ioc->pcidev,
								 this_alloc,
								 &pa);
		if (buflist[buflist_ent].kptr == NULL) {
			alloc_sz = alloc_sz / 2;
			if (alloc_sz == 0) {
				printk(KERN_WARNING MYNAM "-SG: No can do - "
						    "not enough memory!   :-(\n");
				printk(KERN_WARNING MYNAM "-SG: (freeing %d frags)\n",
						    numfrags);
				goto free_and_fail;
			}
			continue;
		} else {
			dma_addr_t dma_addr;

			bytes_allocd += this_alloc;

			/* Write one SIMPLE sge */
			sgl->FlagsLength = cpu_to_le32(0x10000000|sgdir|this_alloc);
			dma_addr = pci_map_single(ioc->pcidev, buflist[buflist_ent].kptr, this_alloc, dir);
			sgl->Address = cpu_to_le32(dma_addr);

			fragcnt++;
			numfrags++;
			sgl++;
			buflist_ent++;
		}

		if (bytes_allocd >= bytes)
			break;

		/* Need to chain? */
		if (fragcnt == sg_spill) {
			dma_addr_t chain_link;

			if (last_chain != NULL)
				last_chain->NextChainOffset = 0x1E;

			fragcnt = 0;
			sg_spill = MAX_FRAGS_SPILL2;

			/* fixup previous SIMPLE sge */
			sgl[-1].FlagsLength |= cpu_to_le32(0x80000000);

			chain_link = (*sglbuf_dma) +
				((u8 *)(sgl+1) - (u8 *)sglbuf);

			/* Write one CHAIN sge */
			sgl->FlagsLength = cpu_to_le32(0x30000080);
			sgl->Address = cpu_to_le32(chain_link);

			old_chain = last_chain;
			last_chain = (SGEChain32_t*)sgl;
			chaincnt++;
			numfrags++;
			sgl++;
		}

		/* overflow check... */
		if (numfrags*8 > MAX_SGL_BYTES) {
			/* GRRRRR... */
			printk(KERN_WARNING MYNAM "-SG: No can do - "
					    "too many SG frags!   :-(\n");
			printk(KERN_WARNING MYNAM "-SG: (freeing %d frags)\n",
					    numfrags);
			goto free_and_fail;
		}
	}

	/* Last sge fixup: set LE+eol+eob bits */
	sgl[-1].FlagsLength |= cpu_to_le32(0xC1000000);

	/* Chain fixup needed? */
	if (last_chain != NULL && fragcnt < 16)
		last_chain->Length = cpu_to_le16(fragcnt * 8);

	*frags = numfrags;
	*blp = buflist;

	dprintk((KERN_INFO MYNAM "-SG: kbuf_alloc_2_sgl() - "
			   "%d SG frags generated!  (%d CHAIN%s)\n",
			   numfrags, chaincnt, chaincnt>1?"s":""));

	dprintk((KERN_INFO MYNAM "-SG: kbuf_alloc_2_sgl() - "
			   "last (big) alloc_sz=%d\n",
			   alloc_sz));

	return sglbuf;

free_and_fail:
	if (sglbuf != NULL) {
		int i;

		for (i = 0; i < numfrags; i++) {
			dma_addr_t dma_addr;
			u8 *kptr;
			int len;

			if ((le32_to_cpu(sglbuf[i].FlagsLength) >> 24) == 0x30)
				continue;

			dma_addr = le32_to_cpu(sglbuf[i].Address);
			kptr = buflist[i].kptr;
			len = buflist[i].len;

			pci_free_consistent(ioc->pcidev, len, kptr, dma_addr);
		}
		pci_free_consistent(ioc->pcidev, MAX_SGL_BYTES, sglbuf, *sglbuf_dma);
	}
	kfree(buflist);
	return NULL;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static void
kfree_sgl(SGESimple32_t *sgl, dma_addr_t sgl_dma, struct buflist *buflist, MPT_ADAPTER *ioc)
{
	SGESimple32_t	*sg = sgl;
	struct buflist	*bl = buflist;
	u32		 nib;
	int		 dir;
	int		 n = 0;

	if (le32_to_cpu(sg->FlagsLength) & 0x04000000)
		dir = PCI_DMA_TODEVICE;
	else
		dir = PCI_DMA_FROMDEVICE;

	nib = (le32_to_cpu(sg->FlagsLength) & 0xF0000000) >> 28;
	while (! (nib & 0x4)) { /* eob */
		/* skip ignore/chain. */
		if (nib == 0 || nib == 3) {
			;
		} else if (sg->Address) {
			dma_addr_t dma_addr;
			void *kptr;
			int len;

			dma_addr = le32_to_cpu(sg->Address);
			kptr = bl->kptr;
			len = bl->len;
			pci_unmap_single(ioc->pcidev, dma_addr, len, dir);
			pci_free_consistent(ioc->pcidev, len, kptr, dma_addr);
			n++;
		}
		sg++;
		bl++;
		nib = (le32_to_cpu(sg->FlagsLength) & 0xF0000000) >> 28;
	}

	/* we're at eob! */
	if (sg->Address) {
		dma_addr_t dma_addr;
		void *kptr;
		int len;

		dma_addr = le32_to_cpu(sg->Address);
		kptr = bl->kptr;
		len = bl->len;
		pci_unmap_single(ioc->pcidev, dma_addr, len, dir);
		pci_free_consistent(ioc->pcidev, len, kptr, dma_addr);
		n++;
	}

	pci_free_consistent(ioc->pcidev, MAX_SGL_BYTES, sgl, sgl_dma);
	kfree(buflist);
	dprintk((KERN_INFO MYNAM "-SG: Free'd 1 SGL buf + %d kbufs!\n", n));
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_ioctl_rwperf_init(struct mpt_raw_r_w *dest, unsigned long src,
		      char *caller, MPT_ADAPTER **iocpp)
{
	char	*myname = "_rwperf_init()";
	int	 ioc;

	/* get copy of structure passed from user space */
	if (copy_from_user(dest, (void*)src, sizeof(*dest))) {
		printk(KERN_ERR MYNAM "::%s() @%d - Can't copy mpt_raw_r_w data @ %p\n",
				myname, __LINE__, (void*)src);
		return -EFAULT;				/* (-14) Bad address */
	} else {
		dprintk((KERN_INFO MYNAM "-perf: PerfInfo.{ioc,targ,qd,iters,nblks}"
				   ": %d %d %d %d %d\n",
				   dest->iocnum, dest->target,
				   (int)dest->qdepth, dest->iters, dest->nblks ));
		dprintk((KERN_INFO MYNAM "-perf: PerfInfo.{cache,skip,range,rdwr,seqran}"
				   ": %d %d %d %d %d\n",
				   dest->cache_sz, dest->skip, dest->range,
				   dest->rdwr, dest->seqran ));

		/* Get the MPT adapter id. */
		if ((ioc = mpt_verify_adapter(dest->iocnum, iocpp)) < 0) {
			printk(KERN_ERR MYNAM "::%s() @%d - ioc%d not found!\n",
					myname, __LINE__, dest->iocnum);
			return -ENXIO;			/* (-6) No such device or address */
		} else {
			dprintk((MYNAM "-perf: %s using mpt/ioc%x, target %02xh\n",
					caller, dest->iocnum, dest->target));
		}
	}

	return ioc;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

/*  Treat first N blocks of disk as sacred!  */
#define SACRED_BLOCKS	100

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_ioctl_rwperf(unsigned long arg)
{
	struct mpt_raw_r_w	 kPerfInfo;
				/* NOTE: local copy, on stack==KERNEL_SPACE! */
	u8		 target, targetM;
	u8		 lun, lunM;
	u8		 scsiop;
	int		 qdepth;
	int		 iters;
	int		 cache_sz;
	u32		 xferbytes;
	u32		 scsidir;
	u32		 qtag;
	u32		 scsictl;
	u32		 sgdir;
	u32		 blkno;
	u32		 sbphys;
	SGESimple32_t	*sgl;
	dma_addr_t	 sgl_dma;
	struct buflist	*buflist;
	SGESimple32_t	*sgOut, *sgIn;
	int		 numfrags;
	u32		*msg;
	int		 i;
	int		 ioc;
	MPT_FRAME_HDR	*mf;
	MPT_ADAPTER	*iocp;
	int		 sgfragcpycnt;
	int		 blklo, blkhi;
	u8		 nextchainoffset;
	u8		*SenseBuf;
	dma_addr_t	 SenseBufDMA;
	char		*myname = "_rwperf()";

    dprintk((KERN_INFO "%s - starting...\n", myname));

    /* Validate target device */
    if ((ioc = mpt_ioctl_rwperf_init(&kPerfInfo, arg, myname, &iocp)) < 0)
        return ioc;

    /* Allocate DMA'able memory for the sense buffer. */
    SenseBuf = pci_alloc_consistent(iocp->pcidev, 256, &SenseBufDMA);

    /* set perf parameters from input */
    target = kPerfInfo.target & 0x0FF;
    targetM = target & myMAX_T_MASK;
    lun = kPerfInfo.lun & 0x1F;			// LUN=31 max
    lunM = lun & myMAX_L_MASK;
    qdepth = kPerfInfo.qdepth;
    iters = kPerfInfo.iters;
    xferbytes = ((u32)kPerfInfo.nblks)<<9;

    DevInUse[targetM][lunM] = 1;
    DevIosCount[targetM][lunM] = 0;

    cache_sz = kPerfInfo.cache_sz * 1024;	// CacheSz in kB!

    /* ToDo: */
    /* get capacity (?) */


    // pre-build, one time, everything we can for speed in the loops below...

    scsiop = 0x28;				// default to SCSI READ!
    scsidir = MPI_SCSIIO_CONTROL_READ;		// DATA IN  (host<--ioc<--dev)
						// 02000000
    qtag = MPI_SCSIIO_CONTROL_SIMPLEQ;		// 00000000

    if (xferbytes == 0) {
        // Do 0-byte READ!!!
        //  IMPORTANT!  Need to set no SCSI DIR for this!
        scsidir = MPI_SCSIIO_CONTROL_NODATATRANSFER;
    }

    scsictl = scsidir | qtag;

    /*
     *  Set sgdir for DMA transfer.
     */
//    sgdir   = 0x04000000;		// SCSI WRITE
    sgdir = 0x00000000;			// SCSI READ

    if ((sgl = kbuf_alloc_2_sgl(MAX(512,xferbytes), sgdir, &numfrags, &buflist, &sgl_dma, iocp)) == NULL)
        return -ENOMEM;

    sgfragcpycnt = MIN(10,numfrags);
    nextchainoffset = 0;
    if (numfrags > 10)
        nextchainoffset = 0x1E;

    sbphys = SenseBufDMA;

    rwperf_reset = 0;

//    do {	// target-loop

        blkno = SACRED_BLOCKS;		// Treat first N blocks as sacred!
					// FIXME!  Skip option
        blklo = blkno;
        blkhi = blkno;

        do {    // inner-loop

            while ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
                mb();
                schedule();
                barrier();
            }
            msg = (u32*)mf;

            /* Start piecing the SCSIIORequest together */
            msg[0] = 0x00000000 | nextchainoffset<<16 | target;
            msg[1] = 0x0000FF0A;				// 255 sense bytes, 10-byte CDB!
            msg[3] = lun << 8;
            msg[4] = 0;
            msg[5] = scsictl;

            // 16 bytes of CDB @ msg[6,7,8,9] are below...

            msg[6] = (   ((blkno & 0xFF000000) >> 8)
                       | ((blkno & 0x00FF0000) << 8)
                       | scsiop );
            msg[7] = (   (((u32)kPerfInfo.nblks & 0x0000FF00) << 16)
                       | ((blkno & 0x000000FF) << 8)
                       | ((blkno & 0x0000FF00) >> 8) );
            msg[8] = (kPerfInfo.nblks & 0x00FF);
            msg[9] = 0;

            msg[10] = xferbytes;

//            msg[11] = 0xD0000100;
//            msg[12] = sbphys;
//            msg[13] = 0;
            msg[11] = sbphys;

            // Copy the SGL...
            if (xferbytes) {
                sgOut = (SGESimple32_t*)&msg[12];
                sgIn  = sgl;
                for (i=0; i < sgfragcpycnt; i++)
                    *sgOut++ = *sgIn++;
            }

            // fubar!  QueueDepth issue!!!
            while (    !rwperf_reset
                    && (DevIosCount[targetM][lunM] >= MIN(qdepth,64)) )
            {
                mb();
                schedule();
                barrier();
            }

//            blkno += kPerfInfo.nblks;
// EXP Stuff!
// Try optimizing to certain cache size for the target!
// by keeping blkno within cache range if at all possible
#if 0
            if (    cache_sz
                 && ((2 * kPerfInfo.nblks) <= (cache_sz>>9))
                 && ((blkno + kPerfInfo.nblks) > ((cache_sz>>9) + SACRED_BLOCKS)) )
                blkno = SACRED_BLOCKS;
            else
                blkno += kPerfInfo.nblks;
#endif
// Ok, cheat!
            if (cache_sz && ((blkno + kPerfInfo.nblks) > ((cache_sz>>9) + SACRED_BLOCKS)) )
                   blkno = SACRED_BLOCKS;
            else
                blkno += kPerfInfo.nblks;

            if (blkno > blkhi)
                blkhi = blkno;

            DevIosCount[targetM][lunM]++;

            /*
             *  Finally, post the request
             */
            mpt_put_msg_frame(mptctl_id, ioc, mf);


            /* let linux breath! */
            mb();
            schedule();
            barrier();

            //dprintk((KERN_DEBUG MYNAM "-perf: inner-loop, cnt=%d\n", iters));

        } while ((--iters > 0) && !rwperf_reset);

        dprintk((KERN_INFO MYNAM "-perf: DbG: blklo=%d, blkhi=%d\n", blklo, blkhi));
        dprintk((KERN_INFO MYNAM "-perf: target-loop, thisTarget=%d\n", target));

//        //  TEMPORARY!
//        target = 0;

//    } while (target);


    if (DevIosCount[targetM][lunM]) {
        dprintk((KERN_INFO "  DbG: DevIosCount[%d][%d]=%d\n",
                targetM, lunM, DevIosCount[targetM][lunM]));
    }

    while (DevIosCount[targetM][lunM]) {
        //dprintk((KERN_DEBUG "  DbG: Waiting... DevIosCount[%d][%d]=%d\n",
        //        targetM, lunM, DevIosCount[targetM][lunM]));
        mb();
        schedule();
        barrier();
    }
    DevInUse[targetM][lunM] = 0;

    pci_free_consistent(iocp->pcidev, 256, SenseBuf, SenseBufDMA);

    if (sgl)
        kfree_sgl(sgl, sgl_dma, buflist, iocp);

    dprintk((KERN_INFO "  *** done ***\n"));

    return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_ioctl_rwperf_status(unsigned long arg)
{
	struct mpt_raw_r_w	 kPerfInfo;
				/* NOTE: local copy, on stack==KERNEL_SPACE! */
	MPT_ADAPTER	*iocp;
	int		 ioc;
//	u8		 targ;
//	u8		 lun;
	int		 T, L;
	char		*myname = "_rwperf_status()";


	dprintk((KERN_INFO "%s - starting...\n", myname));

	/* Get a pointer to the MPT adapter. */
	if ((ioc = mpt_ioctl_rwperf_init(&kPerfInfo, arg, myname, &iocp)) < 0)
		return ioc;

	/* set perf parameters from input */
//	targ = kPerfInfo.target & 0xFF;
//	lun = kPerfInfo.lun & 0x1F;

	for (T=0; T < myMAX_TARGETS; T++)
		for (L=0; L < myMAX_LUNS; L++)
			if (DevIosCount[T][L]) {
				printk(KERN_INFO "%s: ioc%d->00:%02x:%02x"
						 ", IosCnt=%d\n",
						 myname, ioc, T, L, DevIosCount[T][L] );
			}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_ioctl_rwperf_reset(unsigned long arg)
{
	struct mpt_raw_r_w	 kPerfInfo;
				/* NOTE: local copy, on stack==KERNEL_SPACE! */
	MPT_ADAPTER	*iocp;
	int		 ioc;
//	u8		 targ;
//	u8		 lun;
	int		 T, L;
	int		 i;
	char		*myname = "_rwperf_reset()";

	dprintk((KERN_INFO "%s - starting...\n", myname));

	/* Get MPT adapter id. */
	if ((ioc = mpt_ioctl_rwperf_init(&kPerfInfo, arg, myname, &iocp)) < 0)
		return ioc;

	/* set perf parameters from input */
//	targ = kPerfInfo.target & 0xFF;
//	lun = kPerfInfo.lun & 0x1F;

	rwperf_reset = 1;
	for (i=0; i < 1000000; i++) {
		mb();
		schedule();
		barrier();
	}
	rwperf_reset = 0;

	for (T=0; T < myMAX_TARGETS; T++)
		for (L=0; L < myMAX_LUNS; L++)
			if (DevIosCount[T][L]) {
				printk(KERN_INFO "%s: ioc%d->00:%02x:%02x, "
						 "IosCnt RESET! (from %d to 0)\n",
						 myname, ioc, T, L, DevIosCount[T][L] );
				DevIosCount[T][L] = 0;
				DevInUse[T][L] = 0;
			}

	return 0;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
mpt_ioctl_scsi_cmd(unsigned long arg)
{
	return -ENOSYS;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,51)
#define	owner_THIS_MODULE  owner:		THIS_MODULE,
#else
#define	owner_THIS_MODULE
#endif

static struct file_operations mptctl_fops = {
	owner_THIS_MODULE
	llseek:		no_llseek,
	read:		mptctl_read,
	write:		mptctl_write,
	ioctl:		mpt_ioctl,
	open:		mptctl_open,
	release:	mptctl_release,
};

static struct miscdevice mptctl_miscdev = {
	MPT_MINOR,
	MYNAM,
	&mptctl_fops
};

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#if defined(__sparc__) && defined(__sparc_v9__)		/*{*/

/* The dynamic ioctl32 compat. registry only exists in >2.3.x sparc64 kernels */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)		/*{*/
extern int register_ioctl32_conversion(unsigned int cmd,
				       int (*handler)(unsigned int,
						      unsigned int,
						      unsigned long,
						      struct file *));
int unregister_ioctl32_conversion(unsigned int cmd);

struct mpt_fw_xfer32 {
	unsigned int iocnum;
	unsigned int fwlen;
	u32 bufp;
};

#define MPTFWDOWNLOAD32     _IOWR(MPT_MAGIC_NUMBER,15,struct mpt_fw_xfer32)

extern asmlinkage int sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
static int
sparc32_mptfwxfer_ioctl(unsigned int fd, unsigned int cmd,
			unsigned long arg, struct file *filp)
{
	struct mpt_fw_xfer32 kfw32;
	struct mpt_fw_xfer kfw;
	MPT_ADAPTER *iocp = NULL;
	int iocnum, iocnumX;
	int nonblock = (filp->f_flags & O_NONBLOCK);
	int ret;

	dprintk((KERN_INFO MYNAM "::sparc32_mptfwxfer_ioctl() called\n"));

	if (copy_from_user(&kfw32, (char *)arg, sizeof(kfw32)))
		return -EFAULT;

	/* Verify intended MPT adapter */
	iocnumX = kfw32.iocnum & 0xFF;
	if (((iocnum = mpt_verify_adapter(iocnumX, &iocp)) < 0) ||
	    (iocp == NULL)) {
		printk(KERN_ERR MYNAM "::sparc32_mptfwxfer_ioctl @%d - ioc%d not found!\n",
				__LINE__, iocnumX);
		return -ENODEV;
	}

	if ((ret = mptctl_syscall_down(iocp, nonblock)) != 0)
		return ret;

	kfw.iocnum = iocnum;
	kfw.fwlen = kfw32.fwlen;
	kfw.bufp = (void *)(unsigned long)kfw32.bufp;

	ret = mpt_ioctl_do_fw_download(kfw.iocnum, kfw.bufp, kfw.fwlen);

	up(&mptctl_syscall_sem_ioc[iocp->id]);

	return ret;
}

#endif		/*} linux >= 2.3.x */
#endif		/*} sparc */

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
int __init mptctl_init(void)
{
	int err;
	int i;
	int where = 1;

	show_mptmod_ver(my_NAME, my_VERSION);

	for (i=0; i<MPT_MAX_ADAPTERS; i++) {
		sema_init(&mptctl_syscall_sem_ioc[i], 1);
	}

#if defined(__sparc__) && defined(__sparc_v9__)		/*{*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)		/*{*/
	err = register_ioctl32_conversion(MPTRWPERF, NULL);
	if (++where && err) goto out_fail;
	err = register_ioctl32_conversion(MPTRWPERF_CHK, NULL);
	if (++where && err) goto out_fail;
	err = register_ioctl32_conversion(MPTRWPERF_RESET, NULL);
	if (++where && err) goto out_fail;
	err = register_ioctl32_conversion(MPTFWDOWNLOAD32, sparc32_mptfwxfer_ioctl);
	if (++where && err) goto out_fail;
#endif		/*} linux >= 2.3.x */
#endif		/*} sparc */

	if (misc_register(&mptctl_miscdev) == -1) {
		printk(KERN_ERR MYNAM ": Can't register misc device [minor=%d].\n", MPT_MINOR);
		err = -EBUSY;
		goto out_fail;
	}
	printk(KERN_INFO MYNAM ": Registered with Fusion MPT base driver\n");
	printk(KERN_INFO MYNAM ": /dev/%s @ (major,minor=%d,%d)\n",
			 mptctl_miscdev.name, MISC_MAJOR, mptctl_miscdev.minor);

	/*
	 *  Install our handler
	 */
	++where;
	if ((mptctl_id = mpt_register(mptctl_reply, MPTCTL_DRIVER)) <= 0) {
		printk(KERN_ERR MYNAM ": ERROR: Failed to register with Fusion MPT base driver\n");
		misc_deregister(&mptctl_miscdev);
		err = -EBUSY;
		goto out_fail;
	}

	return 0;

out_fail:

#if defined(__sparc__) && defined(__sparc_v9__)		/*{*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)		/*{*/
	printk(KERN_ERR MYNAM ": ERROR: Failed to register ioctl32_conversion!"
			" (%d:err=%d)\n", where, err);
	unregister_ioctl32_conversion(MPTRWPERF);
	unregister_ioctl32_conversion(MPTRWPERF_CHK);
	unregister_ioctl32_conversion(MPTRWPERF_RESET);
	unregister_ioctl32_conversion(MPTFWDOWNLOAD32);
#endif		/*} linux >= 2.3.x */
#endif		/*} sparc */

	return err;
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
void mptctl_exit(void)
{

#if defined(__sparc__) && defined(__sparc_v9__)		/*{*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)		/*{*/
	unregister_ioctl32_conversion(MPTRWPERF);
	unregister_ioctl32_conversion(MPTRWPERF_CHK);
	unregister_ioctl32_conversion(MPTRWPERF_RESET);
	unregister_ioctl32_conversion(MPTFWDOWNLOAD32);
#endif		/*} linux >= 2.3.x */
#endif		/*} sparc */

	misc_deregister(&mptctl_miscdev);
	printk(KERN_INFO MYNAM ": /dev/%s @ (major,minor=%d,%d)\n",
			 mptctl_miscdev.name, MISC_MAJOR, mptctl_miscdev.minor);
	printk(KERN_INFO MYNAM ": Deregistered from Fusion MPT base driver\n");

	mpt_deregister(mptctl_id);
}

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

module_init(mptctl_init);
module_exit(mptctl_exit);
