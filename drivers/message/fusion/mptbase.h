/*
 *  linux/drivers/message/fusion/mptbase.h
 *      High performance SCSI + LAN / Fibre Channel device drivers.
 *      For use with PCI chip/adapter(s):
 *          LSIFC9xx/LSI409xx Fibre Channel
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Credits:
 *     (see mptbase.c)
 *
 *  Copyright (c) 1999-2001 LSI Logic Corporation
 *  Originally By: Steven J. Ralston
 *  (mailto:Steve.Ralston@lsil.com)
 *
 *  $Id: mptbase.h,v 1.46.2.2.2.2 2001/09/18 03:22:29 sralston Exp $
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

#ifndef MPTBASE_H_INCLUDED
#define MPTBASE_H_INCLUDED
/*{-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#include "linux_compat.h"	/* linux-2.2.x (vs. -2.4.x) tweaks */

#include "lsi/mpi_type.h"
#include "lsi/mpi.h"		/* Fusion MPI(nterface) basic defs */
#include "lsi/mpi_ioc.h"	/* Fusion MPT IOC(ontroller) defs */
#include "lsi/mpi_cnfg.h"	/* IOC configuration support */
#include "lsi/mpi_init.h"	/* SCSI Host (initiator) protocol support */
#include "lsi/mpi_lan.h"	/* LAN over FC protocol support */

#include "lsi/mpi_fc.h"		/* Fibre Channel (lowlevel) support */
#include "lsi/mpi_targ.h"	/* SCSI/FCP Target protcol support */
#include "lsi/fc_log.h"

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#ifndef MODULEAUTHOR
#define MODULEAUTHOR	"LSI Logic Corporation"
#endif

#ifndef COPYRIGHT
#define COPYRIGHT	"Copyright (c) 1999-2001 " MODULEAUTHOR
#endif

#define MPT_LINUX_VERSION_COMMON	"1.02.02"
#define MPT_LINUX_PACKAGE_NAME		"@(#)mptlinux-1.02.02"
#define WHAT_MAGIC_STRING		"@" "(" "#" ")"

#define show_mptmod_ver(s,ver)  \
	printk(KERN_INFO "%s %s\n", s, ver);

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Fusion MPT(linux) driver configurable stuff...
 */
#define MPT_MAX_ADAPTERS		16
#define MPT_MAX_PROTOCOL_DRIVERS	8
#define MPT_MAX_FC_DEVICES		255

#define MPT_MISCDEV_BASENAME		"mptctl"
#define MPT_MISCDEV_PATHNAME		"/dev/" MPT_MISCDEV_BASENAME

#define MPT_PROCFS_MPTBASEDIR		"mpt"
						/* chg it to "driver/fusion" ? */
#define MPT_PROCFS_SUMMARY_NODE		MPT_PROCFS_MPTBASEDIR "/summary"
#define MPT_PROCFS_SUMMARY_PATHNAME	"/proc/" MPT_PROCFS_SUMMARY_NODE
#define MPT_FW_REV_MAGIC_ID_STRING	"FwRev="

#ifdef __KERNEL__	/* { */
#define  MPT_MAX_REQ_DEPTH		1023
#define  MPT_REQ_DEPTH			256
#define  MPT_MIN_REQ_DEPTH		128

#define  MPT_MAX_REPLY_DEPTH		MPT_MAX_REQ_DEPTH
#define  MPT_REPLY_DEPTH		128
#define  MPT_MIN_REPLY_DEPTH		8
#define  MPT_MAX_REPLIES_PER_ISR	32

#define  MPT_MAX_FRAME_SIZE		128
#define  MPT_REQ_SIZE			128
#define  MPT_REPLY_SIZE			128

#define  MPT_SG_BUCKETS_PER_HUNK	1

#ifdef MODULE
#define  MPT_REQ_DEPTH_RANGE_STR	__MODULE_STRING(MPT_MIN_REQ_DEPTH) "-" __MODULE_STRING(MPT_MAX_REQ_DEPTH)
#define  MPT_REPLY_DEPTH_RANGE_STR	__MODULE_STRING(MPT_MIN_REPLY_DEPTH) "-" __MODULE_STRING(MPT_MAX_REPLY_DEPTH)
#define  MPT_REPLY_SIZE_RANGE_STR	__MODULE_STRING(MPT_MIN_REPLY_SIZE) "-" __MODULE_STRING(MPT_MAX_FRAME_SIZE)
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  MPT protocol driver defs...
 */
typedef enum {
	MPTBASE_DRIVER,		/* MPT base class */
	MPTCTL_DRIVER,		/* MPT ioctl class */
	MPTSCSIH_DRIVER,	/* MPT SCSI host (initiator) class */
	MPTLAN_DRIVER,		/* MPT LAN class */
	MPTSTM_DRIVER,		/* MPT SCSI target mode class */
	MPTUNKNOWN_DRIVER
} MPT_DRIVER_CLASS;

/*
 *  MPT adapter / port / bus / device info structures...
 */

typedef union _MPT_FRAME_TRACKER {
	struct {
		struct _MPT_FRAME_HDR	*forw;
		struct _MPT_FRAME_HDR	*back;
		u32			 arg1;
		void			*argp1;
	} linkage;
	/*
	 * NOTE: On non-32-bit systems, where pointers are LARGE,
	 * using the linkage pointers destroys our sacred MsgContext
	 * field contents.  But we don't care anymore because these
	 * are now reset in mpt_put_msg_frame() just prior to sending
	 * a request off to the IOC.
	 */
	struct {
		u32 __hdr[2];
		/*
		 * The following _MUST_ match the location of the
		 * MsgContext field in the MPT message headers.
		 */
		union {
			u32		 MsgContext;
			struct {
				u16	 req_idx;	/* Request index */
				u8	 cb_idx;	/* callback function index */
				u8	 rsvd;
			} fld;
		} msgctxu;
	} hwhdr;
} MPT_FRAME_TRACKER;

/*
 *  We might want to view/access a frame as:
 *    1) generic request header
 *    2) SCSIIORequest
 *    3) SCSIIOReply
 *    4) MPIDefaultReply
 *    5) frame tracker
 */
typedef struct _MPT_FRAME_HDR {
	union {
		MPIHeader_t		hdr;
		SCSIIORequest_t		scsireq;
		SCSIIOReply_t		sreply;
		MPIDefaultReply_t	reply;
		MPT_FRAME_TRACKER	frame;
	} u;
} MPT_FRAME_HDR;

typedef struct _MPT_Q_TRACKER {
	MPT_FRAME_HDR	*head;
	MPT_FRAME_HDR	*tail;
} MPT_Q_TRACKER;


typedef struct _MPT_SGL_HDR {
	SGESimple32_t	 sge[1];
} MPT_SGL_HDR;

typedef struct _MPT_SGL64_HDR {
	SGESimple64_t	 sge[1];
} MPT_SGL64_HDR;


typedef struct _Q_ITEM {
	struct _Q_ITEM	*forw;
	struct _Q_ITEM	*back;
} Q_ITEM;

typedef struct _Q_TRACKER {
	struct _Q_ITEM	*head;
	struct _Q_ITEM	*tail;
} Q_TRACKER;


/*
 *  Chip-specific stuff...
 */

typedef enum {
	FC909 = 0x0909,
	FC919 = 0x0919,
	FC929 = 0x0929,
	C1030 = 0x1030,
	FCUNK = 0xFBAD
} CHIP_TYPE;

/*
 *  System interface register set
 */

typedef struct _SYSIF_REGS
{
	u32	Doorbell;	/* 00     System<->IOC Doorbell reg  */
	u32	WriteSequence;	/* 04     Write Sequence register    */
	u32	Diagnostic;	/* 08     Diagnostic register        */
	u32	TestBase;	/* 0C     Test Base Address          */
	u32	Reserved1[8];	/* 10-2F  reserved for future use    */
	u32	IntStatus;	/* 30     Interrupt Status           */
	u32	IntMask;	/* 34     Interrupt Mask             */
	u32	Reserved2[2];	/* 38-3F  reserved for future use    */
	u32	RequestFifo;	/* 40     Request Post/Free FIFO     */
	u32	ReplyFifo;	/* 44     Reply   Post/Free FIFO     */
	u32	Reserved3[2];	/* 48-4F  reserved for future use    */
	u32	HostIndex;	/* 50     Host Index register        */
	u32	Reserved4[15];	/* 54-8F                             */
	u32	Fubar;		/* 90     For Fubar usage            */
	u32	Reserved5[27];	/* 94-FF                             */
} SYSIF_REGS;

/*
 * NOTE: Use MPI_{DOORBELL,WRITESEQ,DIAG}_xxx defs in lsi/mpi.h
 * in conjunction with SYSIF_REGS accesses!
 */


typedef struct _MPT_ADAPTER
{
	struct _MPT_ADAPTER	*forw;
	struct _MPT_ADAPTER	*back;
	int			 id;		/* Unique adapter id {0,1,2,...} */
	int			 pci_irq;
	char			 name[32];	/* "iocN"             */
	char			*prod_name;	/* "LSIFC9x9"         */
	u32			 mem_phys;	/* == f4020000 (mmap) */
	volatile SYSIF_REGS	*chip;		/* == c8817000 (mmap) */
	CHIP_TYPE		 chip_type;
	int			 mem_size;
	int			 alloc_total;
	u32			 last_state;
	int			 active;
	int			 sod_reset;
	unsigned long		 last_kickstart;
	u8			*reply_alloc;		/* Reply frames alloc ptr */
	dma_addr_t		 reply_alloc_dma;
	MPT_FRAME_HDR		*reply_frames;		/* Reply frames - rounded up! */
	dma_addr_t		 reply_frames_dma;
	int			 reply_depth;
	int			 reply_sz;
		/* We (host driver) get to manage our own RequestQueue! */
	u8			*req_alloc;		/* Request frames alloc ptr */
	dma_addr_t		 req_alloc_dma;
	MPT_FRAME_HDR		*req_frames;		/* Request msg frames for PULL mode! */
	dma_addr_t		 req_frames_dma;
	int			 req_depth;
	int			 req_sz;
	MPT_Q_TRACKER		 FreeQ;
	spinlock_t		 FreeQlock;
		/* Pool of SCSI sense buffers for commands coming from
		 * the SCSI mid-layer.  We have one 256 byte sense buffer
		 * for each REQ entry.
		 */
	u8			*sense_buf_pool;
	dma_addr_t		 sense_buf_pool_dma;
	struct pci_dev		*pcidev;
/*	atomic_t		 userCnt;	*/
	u8			*memmap;
	int			 mtrr_reg;
	struct Scsi_Host	*sh;
	struct proc_dir_entry	*ioc_dentry;
	struct _MPT_ADAPTER	*alt_ioc;
	int			 hs_reply_idx;
	u32			 hs_req[MPT_MAX_FRAME_SIZE/sizeof(u32)];
	u16			 hs_reply[MPT_MAX_FRAME_SIZE/sizeof(u16)];
	IOCFactsReply_t		 facts;
	PortFactsReply_t	 pfacts[2];
	LANPage0_t		 lan_cnfg_page0;
	LANPage1_t		 lan_cnfg_page1;
	u8			 FirstWhoInit;
	u8			 pad1[3];
} MPT_ADAPTER;


typedef struct _MPT_ADAPTER_TRACKER {
	MPT_ADAPTER	*head;
	MPT_ADAPTER	*tail;
} MPT_ADAPTER_TRACKER;

/*
 *  New return value convention:
 *    1 = Ok to free associated request frame
 *    0 = not Ok ...
 */
typedef int (*MPT_CALLBACK)(MPT_ADAPTER *ioc, MPT_FRAME_HDR *req, MPT_FRAME_HDR *reply);

typedef int (*MPT_EVHANDLER)(MPT_ADAPTER *ioc, EventNotificationReply_t *evReply);
typedef int (*MPT_RESETHANDLER)(MPT_ADAPTER *ioc, int reset_phase);
/* reset_phase defs */
#define MPT_IOC_PRE_RESET		0
#define MPT_IOC_POST_RESET		1

/*
 * Invent MPT host event (super-set of MPI Events)
 * Fitted to 1030's 64-byte [max] request frame size
 */
typedef struct _MPT_HOST_EVENT {
	EventNotificationReply_t	 MpiEvent;	/* 8 32-bit words! */
	u32				 pad[6];
	void				*next;
} MPT_HOST_EVENT;

#define MPT_HOSTEVENT_IOC_BRINGUP	0x91
#define MPT_HOSTEVENT_IOC_RECOVER	0x92

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *  Funky (private) macros...
 */
#ifdef MPT_DEBUG
#define dprintk(x)  printk x
#else
#define dprintk(x)
#endif

#ifdef MPT_DEBUG_HANDSHAKE
#define dhsprintk(x)  printk x
#else
#define dhsprintk(x)
#endif

#if defined(MPT_DEBUG) || defined(MPT_DEBUG_MSG_FRAME)
#define dmfprintk(x)  printk x
#else
#define dmfprintk(x)
#endif

#ifdef MPT_DEBUG_IRQ
#define dirqprintk(x)  printk x
#else
#define dirqprintk(x)
#endif

#ifdef MPT_DEBUG_EVENTS
#define deventprintk(x)  printk x
#else
#define deventprintk(x)
#endif

#ifdef MPT_DEBUG_SPINLOCK
#define dslprintk(x)  printk x
#else
#define dslprintk(x)
#endif

#ifdef MPT_DEBUG_SG
#define dsgprintk(x)  printk x
#else
#define dsgprintk(x)
#endif


#define MPT_INDEX_2_MFPTR(ioc,idx) \
	(MPT_FRAME_HDR*)( (u8*)(ioc)->req_frames + (ioc)->req_sz * (idx) )

#define MFPTR_2_MPT_INDEX(ioc,mf) \
	(int)( ((u8*)mf - (u8*)(ioc)->req_frames) / (ioc)->req_sz )

#define Q_INIT(q,type)  (q)->head = (q)->tail = (type*)(q)
#define Q_IS_EMPTY(q)   ((Q_ITEM*)(q)->head == (Q_ITEM*)(q))

#define Q_ADD_TAIL(qt,i,type) { \
	Q_TRACKER	*_qt = (Q_TRACKER*)(qt); \
	Q_ITEM		*oldTail = _qt->tail; \
	(i)->forw = (type*)_qt; \
	(i)->back = (type*)oldTail; \
	oldTail->forw = (Q_ITEM*)(i); \
	_qt->tail = (Q_ITEM*)(i); \
}

#define Q_ADD_HEAD(qt,i,type) { \
	Q_TRACKER	*_qt = (Q_TRACKER*)(qt); \
	Q_ITEM		*oldHead = _qt->head; \
	(i)->forw = (type*)oldHead; \
	(i)->back = (type*)_qt; \
	oldHead->back = (Q_ITEM*)(i); \
	_qt->head = (Q_ITEM*)(i); \
}

#define Q_DEL_ITEM(i) { \
	Q_ITEM  *_forw = (Q_ITEM*)(i)->forw; \
	Q_ITEM  *_back = (Q_ITEM*)(i)->back; \
	_back->forw = _forw; \
	_forw->back = _back; \
}


#define SWAB4(value) \
	(u32)(   (((value) & 0x000000ff) << 24) \
	       | (((value) & 0x0000ff00) << 8)  \
	       | (((value) & 0x00ff0000) >> 8)  \
	       | (((value) & 0xff000000) >> 24) )


#if defined(MPT_DEBUG) || defined(MPT_DEBUG_MSG_FRAME)
#define DBG_DUMP_REPLY_FRAME(mfp) \
	{	u32 *m = (u32 *)(mfp);					\
		int  i, n = (le32_to_cpu(m[0]) & 0x00FF0000) >> 16;	\
		printk(KERN_INFO " ");					\
		for (i=0; i<n; i++)					\
			printk(" %08x", le32_to_cpu(m[i]));		\
		printk("\n");						\
	}
#define DBG_DUMP_REQUEST_FRAME_HDR(mfp) \
	{	int  i, n = 3;						\
		u32 *m = (u32 *)(mfp);					\
		printk(KERN_INFO " ");					\
		for (i=0; i<n; i++)					\
			printk(" %08x", le32_to_cpu(m[i]));		\
		printk("\n");						\
	}
#else
#define DBG_DUMP_REPLY_FRAME(mfp)
#define DBG_DUMP_REQUEST_FRAME_HDR(mfp)
#endif


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#endif		/* } __KERNEL__ */


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

/*
 *  MPT Control IOCTLs and structures
 */
#define MPT_MAGIC_NUMBER	'm'
#define MPTRWPERF		_IOWR(MPT_MAGIC_NUMBER,0,struct mpt_raw_r_w)
#define MPTRWPERF_CHK		_IOR(MPT_MAGIC_NUMBER,13,struct mpt_raw_r_w)
#define MPTRWPERF_RESET		_IOR(MPT_MAGIC_NUMBER,14,struct mpt_raw_r_w)
#define MPTFWDOWNLOAD		_IOWR(MPT_MAGIC_NUMBER,15,struct mpt_fw_xfer)
#define MPTSCSICMD		_IOWR(MPT_MAGIC_NUMBER,16,struct mpt_scsi_cmd)

/*
 *  Define something *vague* enough that caller doesn't
 *  really need to know anything about device parameters
 *  (blk_size, capacity, etc.)
 */
struct mpt_raw_r_w {
	unsigned int	 iocnum;	/* IOC unit number */
	unsigned int	 port;		/* IOC port number */
	unsigned int	 target;	/* SCSI Target */
	unsigned int	 lun;		/* SCSI LUN */
	unsigned int	 iters;		/* N iterations */
	unsigned short	 nblks;		/* number of blocks per IO */
	unsigned short	 qdepth;	/* max Q depth on this device */
	unsigned char	 range;		/* 0-100% of FULL disk capacity, 0=use (nblks X iters) */
	unsigned char	 skip;		/* % of disk to skip */
	unsigned char	 rdwr;		/* 0-100%, 0=pure ReaDs, 100=pure WRites */
	unsigned char	 seqran;	/* 0-100%, 0=pure SEQential, 100=pure RANdom */
	unsigned int	 cache_sz;	/* In Kb!  Optimize hits to N Kb cache size */
};

struct mpt_fw_xfer {
	unsigned int	 iocnum;	/* IOC unit number */
/*	u8		 flags;*/	/* Message flags - bit field */
	unsigned int	 fwlen;
	void		*bufp;		/* Pointer to firmware buffer */
};

struct mpt_scsi_cmd {
	unsigned int	 iocnum;	/* IOC unit number */
	unsigned int	 port;		/* IOC port number */
	unsigned int	 target;	/* SCSI Target */
	unsigned int	 lun;		/* SCSI LUN */
	SCSIIORequest_t	 scsi_req;
	SCSIIOReply_t	 scsi_reply;
};

struct mpt_ioctl_sanity {
	unsigned int	 iocnum;
};

#ifdef __KERNEL__	/* { */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

/*
 *  Public entry points...
 */
extern int	 mpt_register(MPT_CALLBACK cbfunc, MPT_DRIVER_CLASS dclass);
extern void	 mpt_deregister(int cb_idx);
extern int	 mpt_event_register(int cb_idx, MPT_EVHANDLER ev_cbfunc);
extern void	 mpt_event_deregister(int cb_idx);
extern int	 mpt_reset_register(int cb_idx, MPT_RESETHANDLER reset_func);
extern void	 mpt_reset_deregister(int cb_idx);
extern int	 mpt_register_ascqops_strings(/*ASCQ_Table_t*/void *ascqTable, int ascqtbl_sz, const char **opsTable);
extern void	 mpt_deregister_ascqops_strings(void);
extern MPT_FRAME_HDR	*mpt_get_msg_frame(int handle, int iocid);
extern void	 mpt_free_msg_frame(int handle, int iocid, MPT_FRAME_HDR *mf);
extern void	 mpt_put_msg_frame(int handle, int iocid, MPT_FRAME_HDR *mf);
extern int	 mpt_send_handshake_request(int handle, int iocid, int reqBytes, u32 *req);
extern int	 mpt_verify_adapter(int iocid, MPT_ADAPTER **iocpp);
extern MPT_ADAPTER	*mpt_adapter_find_first(void);
extern MPT_ADAPTER	*mpt_adapter_find_next(MPT_ADAPTER *prev);
extern void	 mpt_print_ioc_summary(MPT_ADAPTER *ioc, char *buf, int *size, int len, int showlan);
extern void	 mpt_print_ioc_facts(MPT_ADAPTER *ioc, char *buf, int *size, int len);

/*
 *  Public data decl's...
 */
extern int		  mpt_lan_index;	/* needed by mptlan.c */
extern int		  mpt_stm_index;	/* needed by mptstm.c */

extern void		 *mpt_v_ASCQ_TablePtr;
extern const char	**mpt_ScsiOpcodesPtr;
extern int		  mpt_ASCQ_TableSz;

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#endif		/* } __KERNEL__ */

/*
 *  More (public) macros...
 */
#ifndef MIN
#define MIN(a, b)   (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)   (((a) > (b)) ? (a) : (b))
#endif

#ifndef offsetof
#define offsetof(t, m)	((size_t) (&((t *)0)->m))
#endif

#if defined(__alpha__) || defined(__sparc_v9__)
#define CAST_U32_TO_PTR(x)	((void *)(u64)x)
#define CAST_PTR_TO_U32(x)	((u32)(u64)x)
#else
#define CAST_U32_TO_PTR(x)	((void *)x)
#define CAST_PTR_TO_U32(x)	((u32)x)
#endif

#define MPT_PROTOCOL_FLAGS_c_c_c_c(pflags) \
	((pflags) & MPI_PORTFACTS_PROTOCOL_INITIATOR)	? 'I' : 'i',	\
	((pflags) & MPI_PORTFACTS_PROTOCOL_TARGET)	? 'T' : 't',	\
	((pflags) & MPI_PORTFACTS_PROTOCOL_LAN)		? 'L' : 'l',	\
	((pflags) & MPI_PORTFACTS_PROTOCOL_LOGBUSADDR)	? 'B' : 'b'

/*}-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
#endif

