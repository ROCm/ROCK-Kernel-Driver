/*
 * dc395x.c
 *
 * Device Driver for Tekram DC395(U/UW/F), DC315(U)
 * PCI SCSI Bus Master Host Adapter
 * (SCSI chip set used Tekram ASIC TRM-S1040)
 *
 * Authors:
 *  C.L. Huang <ching@tekram.com.tw>
 *  Erich Chen <erich@tekram.com.tw>
 *  (C) Copyright 1995-1999 Tekram Technology Co., Ltd.
 *
 *  Kurt Garloff <garloff@suse.de>
 *  (C) 1999-2000 Kurt Garloff
 *
 *  Oliver Neukum <oliver@neukum.name>
 *  Ali Akcaagac <aliakc@web.de>
 *  Jamie Lenehan <lenehan@twibble.org>
 *  (C) 2003
 *
 * License: GNU GPL
 *
 *************************************************************************
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ************************************************************************
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/blk.h>
#include <asm/io.h>
#include "scsi.h"
#include "hosts.h"
#include "dc395x.h"
#include <scsi/scsicam.h>	/* needed for scsicam_bios_param */
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/spinlock.h>

/*---------------------------------------------------------------------------
                                  Features
 ---------------------------------------------------------------------------*/
/*
 * Set to disable parts of the driver
 */
/*#define DC395x_NO_DISCONNECT*/
/*#define DC395x_NO_TAGQ*/
/*#define DC395x_NO_SYNC*/
/*#define DC395x_NO_WIDE*/

/*---------------------------------------------------------------------------
                                  Debugging
 ---------------------------------------------------------------------------*/
/*
 * Types of debugging that can be enabled and disabled
 */
#define DBG_KG		0x0001
#define DBG_0		0x0002
#define DBG_1		0x0004
#define DBG_DCB		0x0008
#define DBG_PARSE	0x0010		/* debug command line parsing */
#define DBG_SGPARANOIA	0x0020
#define DBG_FIFO	0x0040
#define DBG_PIO		0x0080
#define DBG_RECURSION	0x0100		/* check for excessive recursion */
#define DBG_MALLOC	0x0200		/* report on memory allocations */
#define DBG_TRACE	0x0400
#define DBG_TRACEALL	0x0800


/*
 * Set set of things to output debugging for.
 * Undefine to remove all debugging
 */
/*#define DEBUG_MASK (DBG_0|DBG_1|DBG_DCB|DBG_PARSE|DBG_SGPARANOIA|DBG_FIFO|DBG_PIO|DBG_TRACE|DBG_TRACEALL)*/
/*#define  DEBUG_MASK	DBG_0*/


/*
 * Output a kernel mesage at the specified level and append the
 * driver name and a ": " to the start of the message
 */
#define dprintkl(level, format, arg...)  \
    printk(level DC395X_NAME ": " format , ## arg)


#ifdef DEBUG_MASK
/*
 * print a debug message - this is formated with KERN_DEBUG, then the
 * driver name followed by a ": " and then the message is output. 
 * This also checks that the specified debug level is enabled before
 * outputing the message
 */
#define dprintkdbg(type, format, arg...) \
	do { \
		if ((type) & (DEBUG_MASK)) \
			dprintkl(KERN_DEBUG , format , ## arg); \
	} while (0)

/*
 * Check if the specified type of debugging is enabled
 */
#define debug_enabled(type)	((DEBUG_MASK) & (type))

#else
/*
 * No debugging. Do nothing
 */
#define dprintkdbg(type, format, arg...) \
	do {} while (0)
#define debug_enabled(type)	(0)

#endif


/*
 * The recursion debugging just counts entries into the driver and
 * prints out a messge if it exceeds a certain limit. This variable
 * hold the count.
 */
#if debug_enabled(DBG_RECURSION)
static int dbg_in_driver = 0;
#endif


/*
 * Memory allocation debugging
 * Just reports when memory is allocated and/or released.
 */
#if debug_enabled(DBG_MALLOC)
inline void *dc395x_kmalloc(size_t sz, int fl)
{
	void *ptr = kmalloc(sz, fl);
	dprintkl(KERN_DEBUG, "Alloc %i bytes @ %p w/ fl %08x\n", sz, ptr, fl);
	return ptr;
}
inline void dc395x_kfree(const void *adr)
{
	dprintkl(KERN_DEBUG, "Free mem @ %p\n", adr);
	kfree(adr);
}
#else
#define dc395x_kmalloc(sz, fl)	kmalloc(sz, fl)
#define dc395x_kfree(adr) kfree(adr)
#endif


/*
 * Debug/trace stuff
 */
#if debug_enabled(DBG_TRACEALL)
# define TRACEOUTALL(x...) printk ( x)
#else
# define TRACEOUTALL(x...) do {} while (0)
#endif

#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
# define DEBUGTRACEBUFSZ 512
char DC395x_tracebuf[64];
char DC395x_traceoverflow[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
# define TRACEPRINTF(x...) \
	do { \
		int ln = sprintf(DC395x_tracebuf, x); \
		if (pSRB->debugpos + ln >= DEBUGTRACEBUFSZ) { \
			pSRB->debugtrace[pSRB->debugpos] = 0; \
			pSRB->debugpos = DEBUGTRACEBUFSZ/5; \
			pSRB->debugtrace[pSRB->debugpos++] = '>'; \
		} \
		sprintf(pSRB->debugtrace + pSRB->debugpos, "%s", DC395x_tracebuf); \
		pSRB->debugpos += ln - 1; \
	} while (0)
# define TRACEOUT(x...) printk (x)
#else
# define TRACEPRINTF(x...) do {} while (0)
# define TRACEOUT(x...) do {} while (0)
#endif


/*---------------------------------------------------------------------------
 ---------------------------------------------------------------------------*/


#ifndef PCI_VENDOR_ID_TEKRAM
#define PCI_VENDOR_ID_TEKRAM                    0x1DE1	/* Vendor ID    */
#endif
#ifndef PCI_DEVICE_ID_TEKRAM_TRMS1040
#define PCI_DEVICE_ID_TEKRAM_TRMS1040           0x0391	/* Device ID    */
#endif



#define DC395x_LOCK_IO(dev)   spin_lock_irqsave(((struct Scsi_Host *)dev)->host_lock, flags)
#define DC395x_UNLOCK_IO(dev) spin_unlock_irqrestore(((struct Scsi_Host *)dev)->host_lock, flags)

#define DC395x_ACB_INITLOCK(pACB)               spin_lock_init(&pACB->smp_lock)
#define DC395x_ACB_LOCK(pACB,acb_flags)	        if (!pACB->lock_level_count[cpuid]) { spin_lock_irqsave(&pACB->smp_lock,acb_flags); pACB->lock_level_count[cpuid]++; } else { pACB->lock_level_count[cpuid]++; }
#define DC395x_ACB_UNLOCK(pACB,acb_flags)       if (--pACB->lock_level_count[cpuid] == 0) { spin_unlock_irqrestore(&pACB->smp_lock,acb_flags); }

#define DC395x_SMP_IO_LOCK(dev,irq_flags)       spin_lock_irqsave(((struct Scsi_Host*)dev)->host_lock,irq_flags)
#define DC395x_SMP_IO_UNLOCK(dev,irq_flags)     spin_unlock_irqrestore(((struct Scsi_Host*)dev)->host_lock,irq_flags)
#define DC395x_SCSI_DONE_ACB_LOCK	        spin_lock(&(pACB->smp_lock))
#define DC395x_SCSI_DONE_ACB_UNLOCK	        spin_unlock(&(pACB->smp_lock))


#define DC395x_read8(address)                   (u8)(inb(pACB->IOPortBase + (address)))
#define DC395x_read8_(address, base)            (u8)(inb((USHORT)(base) + (address)))
#define DC395x_read16(address)                  (u16)(inw(pACB->IOPortBase + (address)))
#define DC395x_read32(address)                  (u32)(inl(pACB->IOPortBase + (address)))
#define DC395x_write8(address,value)            outb((value), pACB->IOPortBase + (address))
#define DC395x_write8_(address,value,base)      outb((value), (USHORT)(base) + (address))
#define DC395x_write16(address,value)           outw((value), pACB->IOPortBase + (address))
#define DC395x_write32(address,value)           outl((value), pACB->IOPortBase + (address))


#define BUS_ADDR(sg)		sg_dma_address(&(sg))
#define CPU_ADDR(sg)		(page_address((sg).page)+(sg).offset)
#define PAGE_ADDRESS(sg)	page_address((sg)->page)
#define SET_DIR(dir,pcmd)	dir = scsi_to_pci_dma_dir((pcmd)->sc_data_direction)

/* cmd->result */
#define RES_TARGET		0x000000FF	/* Target State */
#define RES_TARGET_LNX  STATUS_MASK	/* Only official ... */
#define RES_ENDMSG		0x0000FF00	/* End Message */
#define RES_DID			0x00FF0000	/* DID_ codes */
#define RES_DRV			0xFF000000	/* DRIVER_ codes */

#define MK_RES(drv,did,msg,tgt) ((int)(drv)<<24 | (int)(did)<<16 | (int)(msg)<<8 | (int)(tgt))
#define MK_RES_LNX(drv,did,msg,tgt) ((int)(drv)<<24 | (int)(did)<<16 | (int)(msg)<<8 | (int)(tgt)<<1)

#define SET_RES_TARGET(who,tgt) { who &= ~RES_TARGET; who |= (int)(tgt); }
#define SET_RES_TARGET_LNX(who,tgt) { who &= ~RES_TARGET_LNX; who |= (int)(tgt) << 1; }
#define SET_RES_MSG(who,msg) { who &= ~RES_ENDMSG; who |= (int)(msg) << 8; }
#define SET_RES_DID(who,did) { who &= ~RES_DID; who |= (int)(did) << 16; }
#define SET_RES_DRV(who,drv) { who &= ~RES_DRV; who |= (int)(drv) << 24; }

/*
**************************************************************************
*/
#define NO_IRQ 255
#define TAG_NONE 255

struct SGentry {
	u32 address;		/* bus! address */
	u32 length;
};


/*-----------------------------------------------------------------------
  SCSI Request Block
  -----------------------------------------------------------------------*/
struct ScsiReqBlk {
	struct ScsiReqBlk *pNextSRB;
	struct DeviceCtlBlk *pSRBDCB;

	/* HW scatter list (up to 64 entries) */
	struct SGentry *SegmentX;
	Scsi_Cmnd *pcmd;

	/* Offset 0x20/0x10 */
	unsigned char *virt_addr;	/* set by DC395x_update_SGlist */

	u32 SRBTotalXferLength;
	u32 Xferred;		/* Backup for the already xferred len */

	u32 SRBSGBusAddr;	/* bus address of DC395x scatterlist */

	u16 SRBState;
	u8 SRBSGCount;
	u8 SRBSGIndex;

	/* Offset 0x38/0x24 */
	u8 MsgInBuf[6];
	u8 MsgOutBuf[6];

	u8 AdaptStatus;
	u8 TargetStatus;
	u8 MsgCnt;
	u8 EndMessage;

	/* Offset 0x48/0x34 */
	u8 *pMsgPtr;

	u8 TagNumber;
	u8 SRBStatus;
	u8 RetryCnt;
	u8 SRBFlag;

	u8 ScsiPhase;
	u8 padding;
	u16 debugpos;
	/* Offset 0x58/0x40 */
#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
	char *debugtrace;
	/* Offset 0x60/0x44 */
#endif
};


/*-----------------------------------------------------------------------
  Device Control Block
  -----------------------------------------------------------------------*/
struct DeviceCtlBlk {
	struct DeviceCtlBlk *pNextDCB;
	struct AdapterCtlBlk *pDCBACB;

	struct ScsiReqBlk *pGoingSRB;
	struct ScsiReqBlk *pGoingLast;

/* 0x10: */
	struct ScsiReqBlk *pWaitingSRB;
	struct ScsiReqBlk *pWaitLast;

	struct ScsiReqBlk *pActiveSRB;
	u32 TagMask;

/* 0x20: */
	u16 MaxCommand;
	u8 AdaptIndex;		/* UnitInfo struc start        */
	u8 UnitIndex;		/* nth Unit on this card       */

	u16 GoingSRBCnt;
	u16 WaitSRBCnt;
	u8 TargetID;		/* SCSI Target ID  (SCSI Only) */
	u8 TargetLUN;		/* SCSI Log.  Unit (SCSI Only) */
	u8 IdentifyMsg;
	u8 DevMode;

/* 0x2c: */
/*    u8	AdpMode;*/
	u8 Inquiry7;		/* To store Inquiry flags */
	u8 SyncMode;		/* 0:async mode */
	u8 MinNegoPeriod;	/* for nego. */
	u8 SyncPeriod;		/* for reg.  */

	u8 SyncOffset;		/* for reg. and nego.(low nibble) */
	u8 UnitCtrlFlag;
	u8 DCBFlag;
	u8 DevType;
	u8 init_TCQ_flag;

	unsigned long last_derated;	/* last time, when features were turned off in abort */
/* 0x38: */
	/* u8       Reserved2[3];    for dword alignment */
};

/*-----------------------------------------------------------------------
  Adapter Control Block
  -----------------------------------------------------------------------*/
struct AdapterCtlBlk {
	struct Scsi_Host *pScsiHost;
	struct AdapterCtlBlk *pNextACB;

	u16 IOPortBase;
	u16 Revxx1;

	struct DeviceCtlBlk *pLinkDCB;
	struct DeviceCtlBlk *pLastDCB;
	struct DeviceCtlBlk *pDCBRunRobin;

	struct DeviceCtlBlk *pActiveDCB;

	struct ScsiReqBlk *pFreeSRB;
	struct ScsiReqBlk *pTmpSRB;
	struct timer_list Waiting_Timer;
	struct timer_list SelTO_Timer;

	u16 SRBCount;
	u16 AdapterIndex;	/* nth Adapter this driver */

	u32 QueryCnt;
	Scsi_Cmnd *pQueryHead;
	Scsi_Cmnd *pQueryTail;

	u8 msgin123[4];

	u8 status;
	u8 DCBCnt;
	u8 sel_timeout;
	u8 dummy;

	u8 IRQLevel;
	u8 TagMaxNum;
	u8 ACBFlag;
	u8 Gmode2;

	u8 Config;
	u8 LUNchk;
	u8 scan_devices;
	u8 HostID_Bit;

	u8 DCBmap[DC395x_MAX_SCSI_ID];
	struct DeviceCtlBlk *children[DC395x_MAX_SCSI_ID][32];

	u32 Cmds;
	u32 SelLost;
	u32 SelConn;
	u32 CmdInQ;
	u32 CmdOutOfSRB;

	/*struct DeviceCtlBlk       DCB_array[DC395x_MAX_DCB]; *//*  +74h, Len=3E8  */
	struct pci_dev *pdev;

	u8 MsgLen;
	u8 DeviceCnt;

	struct ScsiReqBlk SRB_array[DC395x_MAX_SRB_CNT];
	struct ScsiReqBlk TmpSRB;
};


/*
 * The SEEPROM structure for TRM_S1040 
 */
struct NVRamTarget {
	u8 NvmTarCfg0;		/* Target configuration byte 0  */
	u8 NvmTarPeriod;	/* Target period                */
	u8 NvmTarCfg2;		/* Target configuration byte 2  */
	u8 NvmTarCfg3;		/* Target configuration byte 3  */
};


struct NvRamType {
	u8 NvramSubVendorID[2];	/* 0,1  Sub Vendor ID   */
	u8 NvramSubSysID[2];	/* 2,3  Sub System ID   */
	u8 NvramSubClass;	/* 4    Sub Class       */
	u8 NvramVendorID[2];	/* 5,6  Vendor ID       */
	u8 NvramDeviceID[2];	/* 7,8  Device ID       */
	u8 NvramReserved;	/* 9    Reserved        */
	struct NVRamTarget NvramTarget[DC395x_MAX_SCSI_ID];
						/** 10,11,12,13
						 ** 14,15,16,17
						 ** ....
						 ** ....
						 ** 70,71,72,73
						 */
	u8 NvramScsiId;		/* 74 Host Adapter SCSI ID      */
	u8 NvramChannelCfg;	/* 75 Channel configuration     */
	u8 NvramDelayTime;	/* 76 Power on delay time       */
	u8 NvramMaxTag;		/* 77 Maximum tags              */
	u8 NvramReserved0;	/* 78  */
	u8 NvramBootTarget;	/* 79  */
	u8 NvramBootLun;	/* 80  */
	u8 NvramReserved1;	/* 81  */
	u16 Reserved[22];	/* 82,..125 */
	u16 NvramCheckSum;	/* 126,127 */
};

/*------------------------------------------------------------------------------*/

void DC395x_DataOutPhase0(struct AdapterCtlBlk *pACB,
			  struct ScsiReqBlk *pSRB, u16 * pscsi_status);
void DC395x_DataInPhase0(struct AdapterCtlBlk *pACB,
			 struct ScsiReqBlk *pSRB, u16 * pscsi_status);
static void DC395x_CommandPhase0(struct AdapterCtlBlk *pACB,
				 struct ScsiReqBlk *pSRB,
				 u16 * pscsi_status);
static void DC395x_StatusPhase0(struct AdapterCtlBlk *pACB,
				struct ScsiReqBlk *pSRB,
				u16 * pscsi_status);
static void DC395x_MsgOutPhase0(struct AdapterCtlBlk *pACB,
				struct ScsiReqBlk *pSRB,
				u16 * pscsi_status);
void DC395x_MsgInPhase0(struct AdapterCtlBlk *pACB,
			struct ScsiReqBlk *pSRB, u16 * pscsi_status);
static void DC395x_DataOutPhase1(struct AdapterCtlBlk *pACB,
				 struct ScsiReqBlk *pSRB,
				 u16 * pscsi_status);
static void DC395x_DataInPhase1(struct AdapterCtlBlk *pACB,
				struct ScsiReqBlk *pSRB,
				u16 * pscsi_status);
static void DC395x_CommandPhase1(struct AdapterCtlBlk *pACB,
				 struct ScsiReqBlk *pSRB,
				 u16 * pscsi_status);
static void DC395x_StatusPhase1(struct AdapterCtlBlk *pACB,
				struct ScsiReqBlk *pSRB,
				u16 * pscsi_status);
static void DC395x_MsgOutPhase1(struct AdapterCtlBlk *pACB,
				struct ScsiReqBlk *pSRB,
				u16 * pscsi_status);
static void DC395x_MsgInPhase1(struct AdapterCtlBlk *pACB,
			       struct ScsiReqBlk *pSRB,
			       u16 * pscsi_status);
static void DC395x_Nop0(struct AdapterCtlBlk *pACB,
			struct ScsiReqBlk *pSRB, u16 * pscsi_status);
static void DC395x_Nop1(struct AdapterCtlBlk *pACB,
			struct ScsiReqBlk *pSRB, u16 * pscsi_status);
static void DC395x_basic_config(struct AdapterCtlBlk *pACB);
static void DC395x_cleanup_after_transfer(struct AdapterCtlBlk *pACB,
					  struct ScsiReqBlk *pSRB);
static void DC395x_ResetSCSIBus(struct AdapterCtlBlk *pACB);
void DC395x_DataIO_transfer(struct AdapterCtlBlk *pACB,
			    struct ScsiReqBlk *pSRB, u16 ioDir);
void DC395x_Disconnect(struct AdapterCtlBlk *pACB);
void DC395x_Reselect(struct AdapterCtlBlk *pACB);
u8 DC395x_StartSCSI(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB,
		    struct ScsiReqBlk *pSRB);
static void DC395x_BuildSRB(Scsi_Cmnd * pcmd, struct DeviceCtlBlk *pDCB,
			    struct ScsiReqBlk *pSRB);
void DC395x_DoingSRB_Done(struct AdapterCtlBlk *pACB, u8 did_code,
			  Scsi_Cmnd * pcmd, u8 force);
static void DC395x_ScsiRstDetect(struct AdapterCtlBlk *pACB);
static void DC395x_pci_unmap(struct AdapterCtlBlk *pACB,
			     struct ScsiReqBlk *pSRB);
static void DC395x_pci_unmap_sense(struct AdapterCtlBlk *pACB,
				   struct ScsiReqBlk *pSRB);
static inline void DC395x_EnableMsgOut_Abort(struct AdapterCtlBlk *pACB,
					     struct ScsiReqBlk *pSRB);
void DC395x_SRBdone(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB,
		    struct ScsiReqBlk *pSRB);
static void DC395x_RequestSense(struct AdapterCtlBlk *pACB,
				struct DeviceCtlBlk *pDCB,
				struct ScsiReqBlk *pSRB);
static inline void DC395x_SetXferRate(struct AdapterCtlBlk *pACB,
				      struct DeviceCtlBlk *pDCB);
void DC395x_initDCB(struct AdapterCtlBlk *pACB,
		    struct DeviceCtlBlk **ppDCB, u8 target, u8 lun);
static void DC395x_remove_dev(struct AdapterCtlBlk *pACB,
			      struct DeviceCtlBlk *pDCB);


static struct AdapterCtlBlk *DC395x_pACB_start = NULL;
static struct AdapterCtlBlk *DC395x_pACB_current = NULL;
static u16 DC395x_adapterCnt = 0;
static u16 DC395x_CurrSyncOffset = 0;

static char DC395x_monitor_next_IRQ = 0;

/* 
 * DC395x_stateV = (void *)DC395x_SCSI_phase0[phase]
 */
static void *DC395x_SCSI_phase0[] = {
	DC395x_DataOutPhase0,	/* phase:0 */
	DC395x_DataInPhase0,	/* phase:1 */
	DC395x_CommandPhase0,	/* phase:2 */
	DC395x_StatusPhase0,	/* phase:3 */
	DC395x_Nop0,		/* phase:4 PH_BUS_FREE .. initial phase */
	DC395x_Nop0,		/* phase:5 PH_BUS_FREE .. initial phase */
	DC395x_MsgOutPhase0,	/* phase:6 */
	DC395x_MsgInPhase0,	/* phase:7 */
};

/*
 * DC395x_stateV = (void *)DC395x_SCSI_phase1[phase]
 */
static void *DC395x_SCSI_phase1[] = {
	DC395x_DataOutPhase1,	/* phase:0 */
	DC395x_DataInPhase1,	/* phase:1 */
	DC395x_CommandPhase1,	/* phase:2 */
	DC395x_StatusPhase1,	/* phase:3 */
	DC395x_Nop1,		/* phase:4 PH_BUS_FREE .. initial phase */
	DC395x_Nop1,		/* phase:5 PH_BUS_FREE .. initial phase */
	DC395x_MsgOutPhase1,	/* phase:6 */
	DC395x_MsgInPhase1,	/* phase:7 */
};

struct NvRamType dc395x_trm_eepromBuf[DC395x_MAX_ADAPTER_NUM];
/*
 *Fast20:	000	 50ns, 20.0 MHz
 *		001	 75ns, 13.3 MHz
 *		010	100ns, 10.0 MHz
 *		011	125ns,  8.0 MHz
 *		100	150ns,  6.6 MHz
 *		101	175ns,  5.7 MHz
 *		110	200ns,  5.0 MHz
 *		111	250ns,  4.0 MHz
 *
 *Fast40(LVDS):	000	 25ns, 40.0 MHz
 *		001	 50ns, 20.0 MHz
 *		010	 75ns, 13.3 MHz
 *		011	100ns, 10.0 MHz
 *		100	125ns,  8.0 MHz
 *		101	150ns,  6.6 MHz
 *		110	175ns,  5.7 MHz
 *		111	200ns,  5.0 MHz
 */
/*static u8	dc395x_clock_period[] = {12,19,25,31,37,44,50,62};*/

/* real period:48ns,76ns,100ns,124ns,148ns,176ns,200ns,248ns */
static u8 dc395x_clock_period[] = { 12, 18, 25, 31, 37, 43, 50, 62 };
static u16 dc395x_clock_speed[] = { 200, 133, 100, 80, 67, 58, 50, 40 };
/* real period:48ns,72ns,100ns,124ns,148ns,172ns,200ns,248ns */



/*---------------------------------------------------------------------------
                                Configuration
  ---------------------------------------------------------------------------*/

/*
 * Command line parameters are stored in a structure below.
 * These are the index's into the strcuture for the various
 * command line options.
 */
#define CFG_ADAPTER_ID		0
#define CFG_MAX_SPEED		1
#define CFG_DEV_MODE		2
#define CFG_ADAPTER_MODE	3
#define CFG_TAGS		4
#define CFG_RESET_DELAY		5

#define CFG_NUM			6	/* number of configuration items */


/*
 * Value used to indicate that a command line override
 * hasn't been used to modify the value.
 */
#define CFG_PARAM_UNSET -1


/*
 * Hold command line parameters.
 */
struct dc395x_config_data {
	int value;		/* value of this setting */
	int min;		/* minimum value */
	int max;		/* maximum value */
	int def;		/* default value */
	int safe;		/* safe value */
};
struct dc395x_config_data __initdata cfg_data[] = {
	{ /* adapter id */
		CFG_PARAM_UNSET,
		0,
		15,
		7,
		7
	},
	{ /* max speed */
		CFG_PARAM_UNSET,
		  0,
		  7,
		  1,	/* 13.3Mhz */
		  4,	/*  6.7Hmz */
	},
	{ /* dev mode */
		CFG_PARAM_UNSET,
		0,
		0x3f,
		NTC_DO_PARITY_CHK | NTC_DO_DISCONNECT | NTC_DO_SYNC_NEGO |
			NTC_DO_WIDE_NEGO | NTC_DO_TAG_QUEUEING |
			NTC_DO_SEND_START,
		NTC_DO_PARITY_CHK | NTC_DO_SEND_START
	},
	{ /* adapter mode */
		CFG_PARAM_UNSET,
		0,
		0x2f,
#ifdef CONFIG_SCSI_MULTI_LUN
			NAC_SCANLUN |
#endif
		NAC_GT2DRIVES | NAC_GREATER_1G | NAC_POWERON_SCSI_RESET
			/*| NAC_ACTIVE_NEG*/,
		NAC_GT2DRIVES | NAC_GREATER_1G | NAC_POWERON_SCSI_RESET | 0x08
	},
	{ /* tags */
		CFG_PARAM_UNSET,
		0,
		5,
		3,	/* 16 tags (??) */
		2,
	},
	{ /* reset delay */
		CFG_PARAM_UNSET,
		0,
		180,
		1,	/* 1 second */
		10,	/* 10 seconds */
	}
};


/*
 * Safe settings. If set to zero the the BIOS/default values with command line
 * overrides will be used. If set to 1 then safe and slow settings will be used.
 */
static int dc395x_safe = 0;
module_param_named(safe, dc395x_safe, bool, 0);
MODULE_PARM_DESC(safe, "Use safe and slow settings only. Default: false");


module_param_named(adapter_id, cfg_data[CFG_ADAPTER_ID].value, int, 0);
MODULE_PARM_DESC(adapter_id, "Adapter SCSI ID. Default 7 (0-15)");

module_param_named(max_speed, cfg_data[CFG_MAX_SPEED].value, int, 0);
MODULE_PARM_DESC(max_speed, "Maximum bus speed. Default 1 (0-7) Speeds: 0=20, 1=13.3, 2=10, 3=8, 4=6.7, 5=5.8, 6=5, 7=4 Mhz");

module_param_named(dev_mode, cfg_data[CFG_DEV_MODE].value, int, 0);
MODULE_PARM_DESC(dev_mode, "Device mode.");

module_param_named(adapter_mode, cfg_data[CFG_ADAPTER_MODE].value, int, 0);
MODULE_PARM_DESC(adapter_mode, "Adapter mode.");

module_param_named(tags, cfg_data[CFG_TAGS].value, int, 0);
MODULE_PARM_DESC(tags, "Number of tags (1<<x). Default 3 (0-5)");

module_param_named(reset_delay, cfg_data[CFG_RESET_DELAY].value, int, 0);
MODULE_PARM_DESC(reset_delay, "Reset delay in seconds. Default 1 (0-180)");


/**
 * set_safe_settings - if the safe parameter is set then
 * set all values to the safe and slow values.
 **/
static
void __init set_safe_settings(void)
{
	if (dc395x_safe)
	{
		int i;

		dprintkl(KERN_INFO, "Using sage settings.\n");
		for (i = 0; i < CFG_NUM; i++)
		{
			cfg_data[i].value = cfg_data[i].safe;
		}
	}
}


/**
 * fix_settings - reset any boot parmeters which are out of range
 * back to the default values.
 **/
static
void __init fix_settings(void)
{
	int i;

	dprintkdbg(DBG_PARSE, "setup %08x %08x %08x %08x %08x %08x\n",
		    cfg_data[CFG_ADAPTER_ID].value,
		    cfg_data[CFG_MAX_SPEED].value,
		    cfg_data[CFG_DEV_MODE].value,
		    cfg_data[CFG_ADAPTER_MODE].value,
		    cfg_data[CFG_TAGS].value,
		    cfg_data[CFG_RESET_DELAY].value);
	for (i = 0; i < CFG_NUM; i++)
	{
		if (cfg_data[i].value < cfg_data[i].min ||
			cfg_data[i].value > cfg_data[i].max)
		{
			cfg_data[i].value = cfg_data[i].def;
		}
	}
}



/*
 * Mapping from the eeprom value (index into this array) to the
 * the number of actual seconds that the delay should be for.
 */
static
char __initdata eeprom_index_to_delay_map[] = { 1, 3, 5, 10, 16, 30, 60, 120 };


/**
 * eeprom_index_to_delay - Take the eeprom delay setting and convert it
 * into a number of seconds.
 */
static void __init eeprom_index_to_delay(struct NvRamType *eeprom)
{
	eeprom->NvramDelayTime = eeprom_index_to_delay_map[eeprom->NvramDelayTime];
}


/**
 * delay_to_eeprom_index - Take a delay in seconds and return the closest
 * eeprom index which will delay for at least that amount of seconds.
 */
static int __init delay_to_eeprom_index(int delay)
{
	u8 idx = 0;
	while (idx < 7 && eeprom_index_to_delay_map[idx] < delay) {
		idx++;
	}
	return idx;
}


/* Overrride BIOS values with the set ones */
static void __init DC395x_EEprom_Override(struct NvRamType *eeprom)
{
	u8 id;

	/* Adapter Settings */
	if (cfg_data[CFG_ADAPTER_ID].value != CFG_PARAM_UNSET) {
		eeprom->NvramScsiId =
		    (u8)cfg_data[CFG_ADAPTER_ID].value;
	}
	if (cfg_data[CFG_ADAPTER_MODE].value != CFG_PARAM_UNSET) {
		eeprom->NvramChannelCfg =
		    (u8)cfg_data[CFG_ADAPTER_MODE].value;
	}
	if (cfg_data[CFG_RESET_DELAY].value != CFG_PARAM_UNSET) {
		eeprom->NvramDelayTime =
		    delay_to_eeprom_index(cfg_data[CFG_RESET_DELAY].value);
	}
	if (cfg_data[CFG_TAGS].value != CFG_PARAM_UNSET) {
		eeprom->NvramMaxTag = (u8)cfg_data[CFG_TAGS].value;
	}

	/* Device Settings */
	for (id = 0; id < DC395x_MAX_SCSI_ID; id++) {
		if (cfg_data[CFG_DEV_MODE].value != CFG_PARAM_UNSET) {
			eeprom->NvramTarget[id].NvmTarCfg0 =
			    (u8)cfg_data[CFG_DEV_MODE].value;
		}
		if (cfg_data[CFG_MAX_SPEED].value != CFG_PARAM_UNSET) {
			eeprom->NvramTarget[id].NvmTarPeriod =
			    (u8)cfg_data[CFG_MAX_SPEED].value;
		}
	}
}


/*
 * Queueing philosphy:
 * There are a couple of lists:
 * - Query: Contains the Scsi Commands not yet turned into SRBs (per ACB)
 *   (Note: For new EH, it is unecessary!)
 * - Waiting: Contains a list of SRBs not yet sent (per DCB)
 * - Free: List of free SRB slots
 * 
 * If there are no waiting commands for the DCB, the new one is sent to the bus
 * otherwise the oldest one is taken from the Waiting list and the new one is 
 * queued to the Waiting List
 * 
 * Lists are managed using two pointers and eventually a counter
 */

/* Nomen est omen ... */
static inline void
DC395x_freetag(struct DeviceCtlBlk *pDCB, struct ScsiReqBlk *pSRB)
{
	if (pSRB->TagNumber < 255) {
		pDCB->TagMask &= ~(1 << pSRB->TagNumber);	/* free tag mask */
		pSRB->TagNumber = 255;
	}
}


/* Find cmd in SRB list */
inline static struct ScsiReqBlk *DC395x_find_cmd(Scsi_Cmnd * pcmd,
						 struct ScsiReqBlk *start)
{
	struct ScsiReqBlk *psrb = start;
	if (!start)
		return 0;
	do {
		if (psrb->pcmd == pcmd)
			return psrb;
		psrb = psrb->pNextSRB;
	} while (psrb && psrb != start);
	return 0;
}


/* Append to Query List */
static void
DC395x_Query_append(Scsi_Cmnd * cmd, struct AdapterCtlBlk *pACB)
{
	dprintkdbg(DBG_0, "Append cmd %li to Query\n", cmd->pid);

	cmd->host_scribble = NULL;

	if (!pACB->QueryCnt)
		pACB->pQueryHead = cmd;
	else
		pACB->pQueryTail->host_scribble = (void *) cmd;

	pACB->pQueryTail = cmd;
	pACB->QueryCnt++;
	pACB->CmdOutOfSRB++;
}


/* Return next cmd from Query list */
static Scsi_Cmnd *DC395x_Query_get(struct AdapterCtlBlk *pACB)
{
	Scsi_Cmnd *pcmd;

	pcmd = pACB->pQueryHead;
	if (!pcmd)
		return pcmd;
	dprintkdbg(DBG_0, "Get cmd %li from Query\n", pcmd->pid);
	pACB->pQueryHead = (void *) pcmd->host_scribble;
	pcmd->host_scribble = NULL;
	if (!pACB->pQueryHead)
		pACB->pQueryTail = NULL;
	pACB->QueryCnt--;
	return pcmd;
}


/* Return next free SRB */
static __inline__ struct ScsiReqBlk *DC395x_Free_get(struct AdapterCtlBlk
						     *pACB)
{
	struct ScsiReqBlk *pSRB;

	/*DC395x_Free_integrity (pACB); */
	pSRB = pACB->pFreeSRB;
	if (!pSRB)
		dprintkl(KERN_ERR, "Out of Free SRBs :-(\n");
	if (pSRB) {
		pACB->pFreeSRB = pSRB->pNextSRB;
		pSRB->pNextSRB = NULL;
	}

	return pSRB;
}


/* Insert SRB oin top of free list */
static __inline__ void
DC395x_Free_insert(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB)
{
	dprintkdbg(DBG_0, "Free SRB %p\n", pSRB);
	pSRB->pNextSRB = pACB->pFreeSRB;
	pACB->pFreeSRB = pSRB;
}


/* Inserts a SRB to the top of the Waiting list */
static __inline__ void
DC395x_Waiting_insert(struct DeviceCtlBlk *pDCB, struct ScsiReqBlk *pSRB)
{
	dprintkdbg(DBG_0, "Insert pSRB %p cmd %li to Waiting\n", pSRB, pSRB->pcmd->pid);
	pSRB->pNextSRB = pDCB->pWaitingSRB;
	if (!pDCB->pWaitingSRB)
		pDCB->pWaitLast = pSRB;
	pDCB->pWaitingSRB = pSRB;
	pDCB->WaitSRBCnt++;
}


/* Queue SRB to waiting list */
static __inline__ void
DC395x_Waiting_append(struct DeviceCtlBlk *pDCB, struct ScsiReqBlk *pSRB)
{
	dprintkdbg(DBG_0, "Append pSRB %p cmd %li to Waiting\n", pSRB, pSRB->pcmd->pid);
	if (pDCB->pWaitingSRB)
		pDCB->pWaitLast->pNextSRB = pSRB;
	else
		pDCB->pWaitingSRB = pSRB;

	pDCB->pWaitLast = pSRB;
	/* No next one in waiting list */
	pSRB->pNextSRB = NULL;
	pDCB->WaitSRBCnt++;
	/*pDCB->pDCBACB->CmdInQ++; */
}


static __inline__ void
DC395x_Going_append(struct DeviceCtlBlk *pDCB, struct ScsiReqBlk *pSRB)
{
	dprintkdbg(DBG_0, "Append SRB %p to Going\n", pSRB);
	/* Append to the list of Going commands */
	if (pDCB->pGoingSRB)
		pDCB->pGoingLast->pNextSRB = pSRB;
	else
		pDCB->pGoingSRB = pSRB;

	pDCB->pGoingLast = pSRB;
	/* No next one in sent list */
	pSRB->pNextSRB = NULL;
	pDCB->GoingSRBCnt++;
}


/* Find predecessor SRB */
inline static struct ScsiReqBlk *DC395x_find_SRBpre(struct ScsiReqBlk
						    *pSRB,
						    struct ScsiReqBlk
						    *start)
{
	struct ScsiReqBlk *srb = start;
	if (!start)
		return 0;
	do {
		if (srb->pNextSRB == pSRB)
			return srb;
		srb = srb->pNextSRB;
	} while (srb && srb != start);
	return 0;
}


/* Remove SRB from SRB queue */
inline static struct ScsiReqBlk *DC395x_rmv_SRB(struct ScsiReqBlk *pSRB,
						struct ScsiReqBlk *pre)
{
	if (pre->pNextSRB != pSRB)
		pre = DC395x_find_SRBpre(pSRB, pre);
	if (!pre) {
		dprintkl(KERN_ERR, "Internal ERROR: SRB to rmv not found in Q!\n");
		return 0;
	}
	pre->pNextSRB = pSRB->pNextSRB;
	/*pSRB->pNextSRB = 0; */
	return pre;
}


/* Remove SRB from Going queue */
static void
DC395x_Going_remove(struct DeviceCtlBlk *pDCB, struct ScsiReqBlk *pSRB,
		    struct ScsiReqBlk *hint)
{
	struct ScsiReqBlk *pre = 0;
	dprintkdbg(DBG_0, "Remove SRB %p from Going\n", pSRB);
	if (!pSRB)
		dprintkl(KERN_ERR, "Going_remove %p!\n", pSRB);
	if (pSRB == pDCB->pGoingSRB)
		pDCB->pGoingSRB = pSRB->pNextSRB;
	else if (hint && hint->pNextSRB == pSRB)
		pre = DC395x_rmv_SRB(pSRB, hint);
	else
		pre = DC395x_rmv_SRB(pSRB, pDCB->pGoingSRB);
	if (pSRB == pDCB->pGoingLast)
		pDCB->pGoingLast = pre;
	pDCB->GoingSRBCnt--;
}


/* Remove SRB from Waiting queue */
static void
DC395x_Waiting_remove(struct DeviceCtlBlk *pDCB, struct ScsiReqBlk *pSRB,
		      struct ScsiReqBlk *hint)
{
	struct ScsiReqBlk *pre = 0;
	dprintkdbg(DBG_0, "Remove SRB %p from Waiting\n", pSRB);
	if (!pSRB)
		dprintkl(KERN_ERR, "Waiting_remove %p!\n", pSRB);
	if (pSRB == pDCB->pWaitingSRB)
		pDCB->pWaitingSRB = pSRB->pNextSRB;
	else if (hint && hint->pNextSRB == pSRB)
		pre = DC395x_rmv_SRB(pSRB, hint);
	else
		pre = DC395x_rmv_SRB(pSRB, pDCB->pWaitingSRB);
	if (pSRB == pDCB->pWaitLast)
		pDCB->pWaitLast = pre;
	pDCB->WaitSRBCnt--;
}


/* Moves SRB from Going list to the top of Waiting list */
static void
DC395x_Going_to_Waiting(struct DeviceCtlBlk *pDCB, struct ScsiReqBlk *pSRB)
{
	dprintkdbg(DBG_0, "Going_to_Waiting (SRB %p) pid = %li\n", pSRB, pSRB->pcmd->pid);
	/* Remove SRB from Going */
	DC395x_Going_remove(pDCB, pSRB, 0);
	TRACEPRINTF("GtW *");
	/* Insert on top of Waiting */
	DC395x_Waiting_insert(pDCB, pSRB);
	/* Tag Mask must be freed elsewhere ! (KG, 99/06/18) */
}


/* Moves first SRB from Waiting list to Going list */
static __inline__ void
DC395x_Waiting_to_Going(struct DeviceCtlBlk *pDCB, struct ScsiReqBlk *pSRB)
{
	/* Remove from waiting list */
	dprintkdbg(DBG_0, "Remove SRB %p from head of Waiting\n", pSRB);
	DC395x_Waiting_remove(pDCB, pSRB, 0);
	TRACEPRINTF("WtG *");
	DC395x_Going_append(pDCB, pSRB);
}


void DC395x_waiting_timed_out(unsigned long ptr);
/* Sets the timer to wake us up */
static void
DC395x_waiting_timer(struct AdapterCtlBlk *pACB, unsigned long to)
{
	if (timer_pending(&pACB->Waiting_Timer))
		return;
	init_timer(&pACB->Waiting_Timer);
	pACB->Waiting_Timer.function = DC395x_waiting_timed_out;
	pACB->Waiting_Timer.data = (unsigned long) pACB;
	if (time_before
	    (jiffies + to, pACB->pScsiHost->last_reset - HZ / 2))
		pACB->Waiting_Timer.expires =
		    pACB->pScsiHost->last_reset - HZ / 2 + 1;
	else
		pACB->Waiting_Timer.expires = jiffies + to + 1;
	add_timer(&pACB->Waiting_Timer);
}


/* Send the next command from the waiting list to the bus */
void DC395x_Waiting_process(struct AdapterCtlBlk *pACB)
{
	struct DeviceCtlBlk *ptr;
	struct DeviceCtlBlk *ptr1;
	struct ScsiReqBlk *pSRB;

	if ((pACB->pActiveDCB)
	    || (pACB->ACBFlag & (RESET_DETECT + RESET_DONE + RESET_DEV)))
		return;
	if (timer_pending(&pACB->Waiting_Timer))
		del_timer(&pACB->Waiting_Timer);
	ptr = pACB->pDCBRunRobin;
	if (!ptr) {		/* This can happen! */
		ptr = pACB->pLinkDCB;
		pACB->pDCBRunRobin = ptr;
	}
	ptr1 = ptr;
	if (!ptr1)
		return;
	do {
		/* Make sure, the next another device gets scheduled ... */
		pACB->pDCBRunRobin = ptr1->pNextDCB;
		if (!(pSRB = ptr1->pWaitingSRB)
		    || (ptr1->MaxCommand <= ptr1->GoingSRBCnt))
			ptr1 = ptr1->pNextDCB;
		else {
			/* Try to send to the bus */
			if (!DC395x_StartSCSI(pACB, ptr1, pSRB))
				DC395x_Waiting_to_Going(ptr1, pSRB);
			else
				DC395x_waiting_timer(pACB, HZ / 50);
			break;
		}
	} while (ptr1 != ptr);
	return;
}


/* Wake up waiting queue */
void DC395x_waiting_timed_out(unsigned long ptr)
{
	unsigned long flags;
	struct AdapterCtlBlk *pACB = (struct AdapterCtlBlk *) ptr;
	dprintkdbg(DBG_KG, "Debug: Waiting queue woken up by timer.\n");
	DC395x_LOCK_IO(pACB->pScsiHost);
	DC395x_Waiting_process(pACB);
	DC395x_UNLOCK_IO(pACB->pScsiHost);
}


/* Get the DCB for a given ID/LUN combination */
static inline struct DeviceCtlBlk *DC395x_findDCB(struct AdapterCtlBlk
						  *pACB, u8 id, u8 lun)
{
	return pACB->children[id][lun];
}


/***********************************************************************
 * Function: static void DC395x_SendSRB (struct AdapterCtlBlk* pACB, struct ScsiReqBlk* pSRB)
 *
 * Purpose: Send SCSI Request Block (pSRB) to adapter (pACB)
 *
 *            DC395x_queue_command
 *            DC395x_Waiting_process
 *
 ***********************************************************************/
static void
DC395x_SendSRB(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB)
{
	struct DeviceCtlBlk *pDCB;

	pDCB = pSRB->pSRBDCB;
	if ((pDCB->MaxCommand <= pDCB->GoingSRBCnt) || (pACB->pActiveDCB)
	    || (pACB->ACBFlag & (RESET_DETECT + RESET_DONE + RESET_DEV))) {
		DC395x_Waiting_append(pDCB, pSRB);
		DC395x_Waiting_process(pACB);
		return;
	}
#if 0
	if (pDCB->pWaitingSRB) {
		DC395x_Waiting_append(pDCB, pSRB);
		/*      pSRB = GetWaitingSRB(pDCB); *//* non-existent */
		pSRB = pDCB->pWaitingSRB;
		/* Remove from waiting list */
		pDCB->pWaitingSRB = pSRB->pNextSRB;
		pSRB->pNextSRB = NULL;
		if (!pDCB->pWaitingSRB)
			pDCB->pWaitLast = NULL;
	}
#endif

	if (!DC395x_StartSCSI(pACB, pDCB, pSRB))
		DC395x_Going_append(pDCB, pSRB);
	else {
		DC395x_Waiting_insert(pDCB, pSRB);
		DC395x_waiting_timer(pACB, HZ / 50);
	}
}


/*
 *********************************************************************
 *
 * Function: static void DC395x_BuildSRB (Scsi_Cmd *pcmd, struct DeviceCtlBlk* pDCB, struct ScsiReqBlk* pSRB)
 *
 *  Purpose: Prepare SRB for being sent to Device DCB w/ command *pcmd
 *
 *********************************************************************
 */
static void
DC395x_BuildSRB(Scsi_Cmnd * pcmd, struct DeviceCtlBlk *pDCB,
		struct ScsiReqBlk *pSRB)
{
	int i, max;
	struct SGentry *sgp;
	struct scatterlist *sl;
	u32 request_size;
	int dir;

	dprintkdbg(DBG_0, "DC395x_BuildSRB..............\n");
	/*memset (pSRB, 0, sizeof (struct ScsiReqBlk)); */
	pSRB->pSRBDCB = pDCB;
	pSRB->pcmd = pcmd;
	/* Find out about direction */
	dir = scsi_to_pci_dma_dir(pcmd->sc_data_direction);

	if (pcmd->use_sg && dir != PCI_DMA_NONE) {
		unsigned int len = 0;
		/* TODO: In case usg_sg and the no of segments differ, things
		 * will probably go wrong. */
		max = pSRB->SRBSGCount =
		    pci_map_sg(pDCB->pDCBACB->pdev,
			       (struct scatterlist *) pcmd->request_buffer,
			       pcmd->use_sg, dir);
		sgp = pSRB->SegmentX;
		request_size = pcmd->request_bufflen;
		dprintkdbg(DBG_SGPARANOIA, 
		       "BuildSRB: Bufflen = %d, buffer = %p, use_sg = %d\n",
		       pcmd->request_bufflen, pcmd->request_buffer,
		       pcmd->use_sg);
		dprintkdbg(DBG_SGPARANOIA, 
		       "Mapped %i Segments to %i\n", pcmd->use_sg,
		       pSRB->SRBSGCount);
		sl = (struct scatterlist *) pcmd->request_buffer;

		pSRB->virt_addr = page_address(sl->page);
		for (i = 0; i < max; i++) {
			u32 busaddr = (u32) sg_dma_address(&sl[i]);
			u32 seglen = (u32) sl[i].length;
			sgp[i].address = busaddr;
			sgp[i].length = seglen;
			len += seglen;
			dprintkdbg(DBG_SGPARANOIA,
			       "Setting up sgp %d, address = 0x%08x, length = %d, tot len = %d\n",
			       i, busaddr, seglen, len);
		}
		sgp += max - 1;
		/* Fixup for last buffer too big as it is allocated on even page boundaries */
		if (len > request_size) {
#if debug_enabled(DBG_KG) || debug_enabled(DBG_SGPARANOIA)
			dprintkdbg(DBG_KG|DBG_SGPARANOIA,
			       "Fixup SG total length: %d->%d, last seg %d->%d\n",
			       len, request_size, sgp->length,
			       sgp->length - (len - request_size));
#endif
			sgp->length -= (len - request_size);
			len = request_size;
		}
		/* WIDE padding */
		if (pDCB->SyncPeriod & WIDE_SYNC && len % 2) {
			len++;
			sgp->length++;
		}
		pSRB->SRBTotalXferLength = len;	/*? */
		/* Hopefully this does not cross a page boundary ... */
		pSRB->SRBSGBusAddr =
		    pci_map_single(pDCB->pDCBACB->pdev, pSRB->SegmentX,
				   sizeof(struct SGentry) *
				   DC395x_MAX_SG_LISTENTRY,
				   PCI_DMA_TODEVICE);
		dprintkdbg(DBG_SGPARANOIA,
		       "Map SG descriptor list %p (%05x) to %08x\n",
		       pSRB->SegmentX,
		       sizeof(struct SGentry) * DC395x_MAX_SG_LISTENTRY,
		       pSRB->SRBSGBusAddr);
	} else {
		if (pcmd->request_buffer && dir != PCI_DMA_NONE) {
			u32 len = pcmd->request_bufflen;	/* Actual request size */
			pSRB->SRBSGCount = 1;
			pSRB->SegmentX[0].address =
			    pci_map_single(pDCB->pDCBACB->pdev,
					   pcmd->request_buffer, len, dir);
			/* WIDE padding */
			if (pDCB->SyncPeriod & WIDE_SYNC && len % 2)
				len++;
			pSRB->SegmentX[0].length = len;
			pSRB->SRBTotalXferLength = len;
			pSRB->virt_addr = pcmd->request_buffer;
			pSRB->SRBSGBusAddr = 0;
			dprintkdbg(DBG_SGPARANOIA,
			       "BuildSRB: len = %d, buffer = %p, use_sg = %d, map %08x\n",
			       len, pcmd->request_buffer, pcmd->use_sg,
			       pSRB->SegmentX[0].address);
		} else {
			pSRB->SRBSGCount = 0;
			pSRB->SRBTotalXferLength = 0;
			pSRB->SRBSGBusAddr = 0;
			pSRB->virt_addr = 0;
			dprintkdbg(DBG_SGPARANOIA,
			       "BuildSRB: buflen = %d, buffer = %p, use_sg = %d, NOMAP %08x\n",
			       pcmd->bufflen, pcmd->request_buffer,
			       pcmd->use_sg, pSRB->SegmentX[0].address);
		}
	}

	pSRB->SRBSGIndex = 0;
	pSRB->AdaptStatus = 0;
	pSRB->TargetStatus = 0;
	pSRB->MsgCnt = 0;
	pSRB->SRBStatus = 0;
	pSRB->SRBFlag = 0;
	pSRB->SRBState = 0;
	pSRB->RetryCnt = 0;

#if debug_enabled(DBG_TRACE|DBG_TRACEALL) && debug_enabled(DBG_SGPARANOIA)
	if ((unsigned long)pSRB->debugtrace & (DEBUGTRACEBUFSZ - 1)) {
		dprintkdbg(DBG_SGPARANOIA,
			"SRB %i (%p): debugtrace %p corrupt!\n",
		       (pSRB - pDCB->pDCBACB->SRB_array) /
		       sizeof(struct ScsiReqBlk), pSRB, pSRB->debugtrace);
	}
#endif
#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
	pSRB->debugpos = 0;
	pSRB->debugtrace = 0;
#endif
	TRACEPRINTF("pid %li(%li):%02x %02x..(%i-%i) *", pcmd->pid,
		    jiffies, pcmd->cmnd[0], pcmd->cmnd[1],
		    pcmd->device->id, pcmd->device->lun);
	pSRB->TagNumber = TAG_NONE;

	pSRB->ScsiPhase = PH_BUS_FREE;	/* initial phase */
	pSRB->EndMessage = 0;
	return;
}


/* Put cmnd from Query to Waiting list and send next Waiting cmnd */
static void DC395x_Query_to_Waiting(struct AdapterCtlBlk *pACB)
{
	Scsi_Cmnd *pcmd;
	struct ScsiReqBlk *pSRB;
	struct DeviceCtlBlk *pDCB;

	if (pACB->ACBFlag & (RESET_DETECT + RESET_DONE + RESET_DEV))
		return;

	while (pACB->QueryCnt) {
		pSRB = DC395x_Free_get(pACB);
		if (!pSRB)
			return;
		pcmd = DC395x_Query_get(pACB);
		if (!pcmd) {
			DC395x_Free_insert(pACB, pSRB);
			return;
		}		/* should not happen */
		pDCB =
		    DC395x_findDCB(pACB, pcmd->device->id,
				   pcmd->device->lun);
		if (!pDCB) {
			DC395x_Free_insert(pACB, pSRB);
			dprintkl(KERN_ERR, "Command in queue to non-existing device!\n");
			pcmd->result =
			    MK_RES(DRIVER_ERROR, DID_ERROR, 0, 0);
			/*DC395x_UNLOCK_ACB_NI; */
			pcmd->done(pcmd);
			/*DC395x_LOCK_ACB_NI; */
		}
		DC395x_BuildSRB(pcmd, pDCB, pSRB);
		DC395x_Waiting_append(pDCB, pSRB);
	}
}


/***********************************************************************
 * Function : static int DC395x_queue_command (Scsi_Cmnd *cmd,
 *					       void (*done)(Scsi_Cmnd *))
 *
 * Purpose : enqueues a SCSI command
 *
 * Inputs : cmd - SCSI command, done - callback function called on 
 *	    completion, with a pointer to the command descriptor.
 *
 * Returns : (depending on kernel version)
 * 2.0.x: always return 0
 * 2.1.x: old model: (use_new_eh_code == 0): like 2.0.x
 *	  new model: return 0 if successful
 *	  	     return 1 if command cannot be queued (queue full)
 *		     command will be inserted in midlevel queue then ...
 *
 ***********************************************************************/
static int
DC395x_queue_command(Scsi_Cmnd * cmd, void (*done) (Scsi_Cmnd *))
{
	struct DeviceCtlBlk *pDCB;
	struct ScsiReqBlk *pSRB;
	struct AdapterCtlBlk *pACB =
	    (struct AdapterCtlBlk *) cmd->device->host->hostdata;


	dprintkdbg(DBG_0, "Queue Cmd=%02x,Tgt=%d,LUN=%d (pid=%li)\n",
		cmd->cmnd[0], cmd->device->id,
		cmd->device->lun, cmd->pid);

#if debug_enabled(DBG_RECURSION)
	if (dbg_in_driver++ > NORM_REC_LVL) {
		dprintkl(KERN_DEBUG,
			"%i queue_command () recursion? (pid=%li)\n",
			dbg_in_driver, cmd->pid);
	}
#endif

	/* Assume BAD_TARGET; will be cleared later */
	cmd->result = DID_BAD_TARGET << 16;

	if ((cmd->device->id >= pACB->pScsiHost->max_id)
	    || (cmd->device->lun >= pACB->pScsiHost->max_lun)
	    || (cmd->device->lun >31)) {
		/*      dprintkl(KERN_INFO, "Ignore target %d lun %d\n",
		   cmd->device->id, cmd->device->lun); */
#if debug_enabled(DBG_RECURSION)
		dbg_in_driver--
#endif
		/*return 1; */
		done(cmd);
		return 0;
	}

	if (!(pACB->DCBmap[cmd->device->id] & (1 << cmd->device->lun))) {
		dprintkl(KERN_INFO, "Ignore target %02x lun %02x\n", cmd->device->id,
		       cmd->device->lun);
		/*return 1; */
#if debug_enabled(DBG_RECURSION)
		dbg_in_driver--
#endif
		done(cmd);
		return 0;
	} else {
		pDCB =
		    DC395x_findDCB(pACB, cmd->device->id,
				   cmd->device->lun);
		if (!pDCB) {	/* should never happen */
			dprintkl(KERN_ERR, "no DCB failed, target %02x lun %02x\n",
			       cmd->device->id, cmd->device->lun);
			dprintkl(KERN_ERR, "No DCB in queuecommand (2)!\n");
#if debug_enabled(DBG_RECURSION)
			dbg_in_driver--
#endif
			return 1;
		}
	}

	pACB->Cmds++;
	cmd->scsi_done = done;
	cmd->result = 0;

	DC395x_Query_to_Waiting(pACB);

	if (pACB->QueryCnt) {
		/* Unsent commands ? */
		dprintkdbg(DBG_0, "QueryCnt != 0\n");
		DC395x_Query_append(cmd, pACB);
		DC395x_Waiting_process(pACB);
	} else {
		if (pDCB->pWaitingSRB) {
			pSRB = DC395x_Free_get(pACB);
			if (debug_enabled(DBG_0)) {
				if (!pSRB)
					dprintkdbg(DBG_0, "No free SRB but Waiting\n");
				else
					dprintkdbg(DBG_0, "Free SRB w/ Waiting\n");
			}
			if (!pSRB) {
				DC395x_Query_append(cmd, pACB);
			} else {
				DC395x_BuildSRB(cmd, pDCB, pSRB);
				DC395x_Waiting_append(pDCB, pSRB);
			}
			DC395x_Waiting_process(pACB);
		} else {
			pSRB = DC395x_Free_get(pACB);
			if (debug_enabled(DBG_0)) {
				if (!pSRB)
					dprintkdbg(DBG_0, "No free SRB w/o Waiting\n");
				else
					dprintkdbg(DBG_0, "Free SRB w/o Waiting\n");
			}
			if (!pSRB) {
				DC395x_Query_append(cmd, pACB);
				DC395x_Waiting_process(pACB);
			} else {
				DC395x_BuildSRB(cmd, pDCB, pSRB);
				DC395x_SendSRB(pACB, pSRB);
			}
		}
	}

	/*DC395x_ACB_LOCK(pACB,acb_flags); */
	dprintkdbg(DBG_1, "... command (pid %li) queued successfully.\n", cmd->pid);
#if debug_enabled(DBG_RECURSION)
	dbg_in_driver--
#endif
	return 0;
}


/***********************************************************************
 * Function static int DC395x_slave_alloc()
 *
 * Purpose: Allocate DCB
 ***********************************************************************/
static int DC395x_slave_alloc(struct scsi_device *sdp)
{
	struct AdapterCtlBlk *pACB;
	struct DeviceCtlBlk *dummy;

	pACB = (struct AdapterCtlBlk *) sdp->host->hostdata;

	DC395x_initDCB(pACB, &dummy, sdp->id, sdp->lun);

	return dummy ? 0 : -ENOMEM;
}


static void DC395x_slave_destroy(struct scsi_device *sdp)
{
	struct AdapterCtlBlk *ACB;
	struct DeviceCtlBlk *DCB;

	ACB = (struct AdapterCtlBlk *) sdp->host->hostdata;
	DCB = DC395x_findDCB(ACB, sdp->id, sdp->lun);

	DC395x_remove_dev(ACB, DCB);
}


/***********************************************************************
 * Function : static void DC395_updateDCB()
 *
 * Purpose :  Set the configuration dependent DCB parameters
 ***********************************************************************/
void
DC395x_updateDCB(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB)
{
	/* Prevent disconnection of narrow devices if this_id > 7 */
	if (!(pDCB->DevMode & NTC_DO_WIDE_NEGO)
	    && pACB->pScsiHost->this_id > 7)
		pDCB->DevMode &= ~NTC_DO_DISCONNECT;

	/* TagQ w/o DisCn is impossible */
	if (!(pDCB->DevMode & NTC_DO_DISCONNECT))
		pDCB->DevMode &= ~NTC_DO_TAG_QUEUEING;
	pDCB->IdentifyMsg =
	    IDENTIFY((pDCB->DevMode & NTC_DO_DISCONNECT), pDCB->TargetLUN);

	pDCB->SyncMode &=
	    EN_TAG_QUEUEING | SYNC_NEGO_DONE | WIDE_NEGO_DONE
	    /*| EN_ATN_STOP */ ;
	if (pDCB->DevMode & NTC_DO_TAG_QUEUEING) {
		if (pDCB->SyncMode & EN_TAG_QUEUEING)
			pDCB->MaxCommand = pACB->TagMaxNum;
	} else {
		pDCB->SyncMode &= ~EN_TAG_QUEUEING;
		pDCB->MaxCommand = 1;
	}

	if (pDCB->DevMode & NTC_DO_SYNC_NEGO)
		pDCB->SyncMode |= SYNC_NEGO_ENABLE;
	else {
		pDCB->SyncMode &= ~(SYNC_NEGO_DONE | SYNC_NEGO_ENABLE);
		pDCB->SyncOffset &= ~0x0f;
	}

	if (pDCB->DevMode & NTC_DO_WIDE_NEGO
	    && pACB->Config & HCC_WIDE_CARD)
		pDCB->SyncMode |= WIDE_NEGO_ENABLE;
	else {
		pDCB->SyncMode &= ~(WIDE_NEGO_DONE | WIDE_NEGO_ENABLE);
		pDCB->SyncPeriod &= ~WIDE_SYNC;
	}
	/*if (! (pDCB->DevMode & EN_DISCONNECT_)) pDCB->SyncMode &= ~EN_ATN_STOP; */
}


/*
 *********************************************************************
 *
 * Function   : DC395x_bios_param
 * Description: Return the disk geometry for the given SCSI device.
 *********************************************************************
 */
static int
DC395x_bios_param(struct scsi_device *sdev, struct block_device *bdev,
		  sector_t capacity, int *info)
{
#ifdef CONFIG_SCSI_DC395x_TRMS1040_TRADMAP
	int heads, sectors, cylinders;
	struct AdapterCtlBlk *pACB;
	int size = capacity;

	dprintkdbg(DBG_0, "DC395x_bios_param..............\n");
	pACB = (struct AdapterCtlBlk *) sdev->host->hostdata;
	heads = 64;
	sectors = 32;
	cylinders = size / (heads * sectors);

	if ((pACB->Gmode2 & NAC_GREATER_1G) && (cylinders > 1024)) {
		heads = 255;
		sectors = 63;
		cylinders = size / (heads * sectors);
	}
	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;
	return 0;
#else
	return scsicam_bios_param(bdev, capacity, info);
#endif
}


/*
 * DC395x register dump
 */
void
DC395x_dumpinfo(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB,
		struct ScsiReqBlk *pSRB)
{
	u16 pstat;
	struct pci_dev *pdev = pACB->pdev;
	pci_read_config_word(pdev, PCI_STATUS, &pstat);
	if (!pDCB)
		pDCB = pACB->pActiveDCB;
	if (!pSRB && pDCB)
		pSRB = pDCB->pActiveSRB;
	if (pSRB) {
		if (!(pSRB->pcmd))
			dprintkl(KERN_INFO, "dump: SRB %p: cmd %p OOOPS!\n", pSRB,
			       pSRB->pcmd);
		else
			dprintkl(KERN_INFO, "dump: SRB %p: cmd %p pid %li: %02x (%02i-%i)\n",
			       pSRB, pSRB->pcmd, pSRB->pcmd->pid,
			       pSRB->pcmd->cmnd[0], pSRB->pcmd->device->id,
			       pSRB->pcmd->device->lun);
		printk("              SGList %p Cnt %i Idx %i Len %i\n",
		       pSRB->SegmentX, pSRB->SRBSGCount, pSRB->SRBSGIndex,
		       pSRB->SRBTotalXferLength);
		printk
		    ("              State %04x Status %02x Phase %02x (%sconn.)\n",
		     pSRB->SRBState, pSRB->SRBStatus, pSRB->ScsiPhase,
		     (pACB->pActiveDCB) ? "" : "not");
		TRACEOUT("        %s\n", pSRB->debugtrace);
	}
	dprintkl(KERN_INFO, "dump: SCSI block\n");
	printk
	    ("              Status %04x FIFOCnt %02x Signals %02x IRQStat %02x\n",
	     DC395x_read16(TRM_S1040_SCSI_STATUS),
	     DC395x_read8(TRM_S1040_SCSI_FIFOCNT),
	     DC395x_read8(TRM_S1040_SCSI_SIGNAL),
	     DC395x_read8(TRM_S1040_SCSI_INTSTATUS));
	printk
	    ("              Sync %02x Target %02x RSelID %02x SCSICtr %08x\n",
	     DC395x_read8(TRM_S1040_SCSI_SYNC),
	     DC395x_read8(TRM_S1040_SCSI_TARGETID),
	     DC395x_read8(TRM_S1040_SCSI_IDMSG),
	     DC395x_read32(TRM_S1040_SCSI_COUNTER));
	printk
	    ("              IRQEn %02x Config %04x Cfg2 %02x Cmd %02x SelTO %02x\n",
	     DC395x_read8(TRM_S1040_SCSI_INTEN),
	     DC395x_read16(TRM_S1040_SCSI_CONFIG0),
	     DC395x_read8(TRM_S1040_SCSI_CONFIG2),
	     DC395x_read8(TRM_S1040_SCSI_COMMAND),
	     DC395x_read8(TRM_S1040_SCSI_TIMEOUT));
	dprintkl(KERN_INFO, "dump: DMA block\n");
	printk
	    ("              Cmd %04x FIFOCnt %02x FStat %02x IRQStat %02x IRQEn %02x Cfg %04x\n",
	     DC395x_read16(TRM_S1040_DMA_COMMAND),
	     DC395x_read8(TRM_S1040_DMA_FIFOCNT),
	     DC395x_read8(TRM_S1040_DMA_FIFOSTAT),
	     DC395x_read8(TRM_S1040_DMA_STATUS),
	     DC395x_read8(TRM_S1040_DMA_INTEN),
	     DC395x_read16(TRM_S1040_DMA_CONFIG));
	printk("              TCtr %08x CTCtr %08x Addr %08x%08x\n",
	       DC395x_read32(TRM_S1040_DMA_XCNT),
	       DC395x_read32(TRM_S1040_DMA_CXCNT),
	       DC395x_read32(TRM_S1040_DMA_XHIGHADDR),
	       DC395x_read32(TRM_S1040_DMA_XLOWADDR));
	dprintkl(KERN_INFO, "dump: Misc: GCtrl %02x GStat %02x GTmr %02x\n",
	       DC395x_read8(TRM_S1040_GEN_CONTROL),
	       DC395x_read8(TRM_S1040_GEN_STATUS),
	       DC395x_read8(TRM_S1040_GEN_TIMER));
	dprintkl(KERN_INFO, "dump: PCI Status %04x\n", pstat);


}


static inline void DC395x_clrfifo(struct AdapterCtlBlk *pACB, char *txt)
{
#if debug_enabled(DBG_FIFO)
	u8 lines = DC395x_read8(TRM_S1040_SCSI_SIGNAL);
	u8 fifocnt = DC395x_read8(TRM_S1040_SCSI_FIFOCNT);
	if (!(fifocnt & 0x40))
		dprintkdbg(DBG_FIFO,
		       "Clr FIFO (%i bytes) on phase %02x in %s\n",
			fifocnt & 0x3f, lines, txt);
#endif
#if debug_enabled(DBG_TRACE)   
	if (pACB->pActiveDCB && pACB->pActiveDCB->pActiveSRB) {
		struct ScsiReqBlk *pSRB = pACB->pActiveDCB->pActiveSRB;
		TRACEPRINTF("#*");
	}
#endif
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_CLRFIFO);
}


/*
 ********************************************************************
 *
 *		DC395x_reset      DC395x_ScsiRstDetect
 *
 ********************************************************************
 */
static void DC395x_ResetDevParam(struct AdapterCtlBlk *pACB)
{
	struct DeviceCtlBlk *pDCB;
	struct DeviceCtlBlk *pDCBTemp;
	struct NvRamType *eeprom;
	u8 PeriodIndex;
	u16 index;

	dprintkdbg(DBG_0, "DC395x_ResetDevParam..............\n");
	pDCB = pACB->pLinkDCB;
	if (pDCB == NULL)
		return;

	pDCBTemp = pDCB;
	do {
		pDCB->SyncMode &= ~(SYNC_NEGO_DONE + WIDE_NEGO_DONE);
		pDCB->SyncPeriod = 0;
		pDCB->SyncOffset = 0;
		index = pACB->AdapterIndex;
		eeprom = &dc395x_trm_eepromBuf[index];

		pDCB->DevMode =
		    eeprom->NvramTarget[pDCB->TargetID].NvmTarCfg0;
		/*pDCB->AdpMode = eeprom->NvramChannelCfg; */
		PeriodIndex =
		    eeprom->NvramTarget[pDCB->TargetID].
		    NvmTarPeriod & 0x07;
		pDCB->MinNegoPeriod = dc395x_clock_period[PeriodIndex];
		if (!(pDCB->DevMode & NTC_DO_WIDE_NEGO)
		    || !(pACB->Config & HCC_WIDE_CARD))
			pDCB->SyncMode &= ~WIDE_NEGO_ENABLE;

		pDCB = pDCB->pNextDCB;
	}
	while (pDCBTemp != pDCB && pDCB != NULL);
}


/*
 *********************************************************************
 * Function : int DC395x_eh_bus_reset(Scsi_Cmnd *cmd)
 * Purpose  : perform a hard reset on the SCSI bus
 * Inputs   : cmd - some command for this host (for fetching hooks)
 * Returns  : SUCCESS (0x2002) on success, else FAILED (0x2003).
 *********************************************************************
 */
static int DC395x_eh_bus_reset(Scsi_Cmnd * cmd)
{
	struct AdapterCtlBlk *pACB;
	/*u32         acb_flags=0; */

	dprintkl(KERN_INFO, "reset requested!\n");
	pACB = (struct AdapterCtlBlk *) cmd->device->host->hostdata;
	/* mid level guarantees no recursion */
	/*DC395x_ACB_LOCK(pACB,acb_flags); */

	if (timer_pending(&pACB->Waiting_Timer))
		del_timer(&pACB->Waiting_Timer);

	/*
	 * disable interrupt    
	 */
	DC395x_write8(TRM_S1040_DMA_INTEN, 0x00);
	DC395x_write8(TRM_S1040_SCSI_INTEN, 0x00);
	DC395x_write8(TRM_S1040_SCSI_CONTROL, DO_RSTMODULE);
	DC395x_write8(TRM_S1040_DMA_CONTROL, DMARESETMODULE);

	DC395x_ResetSCSIBus(pACB);
	udelay(500);

	/* We may be in serious trouble. Wait some seconds */
	pACB->pScsiHost->last_reset =
	    jiffies + 3 * HZ / 2 +
	    HZ * dc395x_trm_eepromBuf[pACB->AdapterIndex].NvramDelayTime;

	/*
	 * re-enable interrupt      
	 */
	/* Clear SCSI FIFO          */
	DC395x_write8(TRM_S1040_DMA_CONTROL, CLRXFIFO);
	DC395x_clrfifo(pACB, "reset");
	/* Delete pending IRQ */
	DC395x_read8(TRM_S1040_SCSI_INTSTATUS);
	DC395x_basic_config(pACB);

	DC395x_ResetDevParam(pACB);
	DC395x_DoingSRB_Done(pACB, DID_RESET, cmd, 0);

	pACB->pActiveDCB = NULL;

	pACB->ACBFlag = 0;	/* RESET_DETECT, RESET_DONE ,RESET_DEV */
	DC395x_Waiting_process(pACB);

	/*DC395x_ACB_LOCK(pACB,acb_flags); */
	return SUCCESS;
}


/*
 *********************************************************************
 * Function : int DC395x_eh_abort(Scsi_Cmnd *cmd)
 * Purpose  : abort an errant SCSI command
 * Inputs   : cmd - command to be aborted
 * Returns  : SUCCESS (0x2002) on success, else FAILED (0x2003).
 *********************************************************************
 */
static int DC395x_eh_abort(Scsi_Cmnd * cmd)
{
	/*
	 * Look into our command queues: If it has not been sent already,
	 * we remove it and return success. Otherwise fail.
	 * First check the Query Queues, then the Waiting ones
	 */
	struct AdapterCtlBlk *pACB =
	    (struct AdapterCtlBlk *) cmd->device->host->hostdata;
	struct DeviceCtlBlk *pDCB;
	struct ScsiReqBlk *pSRB;
	int cnt = pACB->QueryCnt;
	Scsi_Cmnd *pcmd;
	Scsi_Cmnd *last = 0;
	dprintkl(KERN_INFO, "eh abort: cmd %p (pid %li, %02i-%i) ",
	       cmd, cmd->pid, cmd->device->id, cmd->device->lun);
	for (pcmd = pACB->pQueryHead; cnt--;
	     last = pcmd, pcmd = (Scsi_Cmnd *) pcmd->host_scribble) {
		if (pcmd == cmd) {
			/* unqueue */
			if (last) {
				last->host_scribble = pcmd->host_scribble;
				if (!pcmd->host_scribble)
					pACB->pQueryTail = last;
			} else {
				pACB->pQueryHead =
				    (Scsi_Cmnd *) pcmd->host_scribble;
				if (!pcmd->host_scribble)
					pACB->pQueryTail = 0;
			}
			printk("found in Query queue :-)\n");
			pACB->QueryCnt--;
			cmd->result = DID_ABORT << 16;
			return SUCCESS;
		}
	}
	pDCB = DC395x_findDCB(pACB, cmd->device->id, cmd->device->lun);
	if (!pDCB) {
		printk("no DCB!\n");
		return FAILED;
	}

	pSRB = DC395x_find_cmd(cmd, pDCB->pWaitingSRB);
	if (pSRB) {
		DC395x_Waiting_remove(pDCB, pSRB, 0);
		DC395x_pci_unmap_sense(pACB, pSRB);
		DC395x_pci_unmap(pACB, pSRB);
		DC395x_freetag(pDCB, pSRB);
		DC395x_Free_insert(pACB, pSRB);
		printk("found in waiting queue :-)\n");
		cmd->result = DID_ABORT << 16;
		return SUCCESS;
	}
	pSRB = DC395x_find_cmd(cmd, pDCB->pGoingSRB);
	if (pSRB)
		printk("found in going queue :-(\n");
	else
		printk("not found!\n");
	return FAILED;
}


/*
 * TODO (new EH):
 * int (*eh_device_reset_handler)(Scsi_Cmnd *);
 * int (*eh_host_reset_handler)(Scsi_Cmnd *);
 * 
 * remove Query Queue
 * investigate whether/which commands need to be ffed back to mid-layer
 * in _eh_reset()
 */


/* SDTR */
static void
DC395x_Build_SDTR(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB,
		  struct ScsiReqBlk *pSRB)
{
	u8 *ptr = pSRB->MsgOutBuf + pSRB->MsgCnt;
	if (pSRB->MsgCnt > 1) {
		dprintkl(KERN_INFO,
		       "Build_SDTR: MsgOutBuf BUSY (%i: %02x %02x)\n",
		       pSRB->MsgCnt, pSRB->MsgOutBuf[0],
		       pSRB->MsgOutBuf[1]);
		return;
	}
	if (!(pDCB->DevMode & NTC_DO_SYNC_NEGO)) {
		pDCB->SyncOffset = 0;
		pDCB->MinNegoPeriod = 200 >> 2;
	} else if (pDCB->SyncOffset == 0)
		pDCB->SyncOffset = SYNC_NEGO_OFFSET;

	*ptr++ = MSG_EXTENDED;	/* (01h) */
	*ptr++ = 3;		/* length */
	*ptr++ = EXTENDED_SDTR;	/* (01h) */
	*ptr++ = pDCB->MinNegoPeriod;	/* Transfer period (in 4ns) */
	*ptr++ = pDCB->SyncOffset;	/* Transfer period (max. REQ/ACK dist) */
	pSRB->MsgCnt += 5;
	pSRB->SRBState |= SRB_DO_SYNC_NEGO;
	TRACEPRINTF("S *");
}


/* SDTR */
static void
DC395x_Build_WDTR(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB,
		  struct ScsiReqBlk *pSRB)
{
	u8 wide =
	    ((pDCB->DevMode & NTC_DO_WIDE_NEGO) & (pACB->
						   Config & HCC_WIDE_CARD))
	    ? 1 : 0;
	u8 *ptr = pSRB->MsgOutBuf + pSRB->MsgCnt;
	if (pSRB->MsgCnt > 1) {
		dprintkl(KERN_INFO,
		       "Build_WDTR: MsgOutBuf BUSY (%i: %02x %02x)\n",
		       pSRB->MsgCnt, pSRB->MsgOutBuf[0],
		       pSRB->MsgOutBuf[1]);
		return;
	}
	*ptr++ = MSG_EXTENDED;	/* (01h) */
	*ptr++ = 2;		/* length */
	*ptr++ = EXTENDED_WDTR;	/* (03h) */
	*ptr++ = wide;
	pSRB->MsgCnt += 4;
	pSRB->SRBState |= SRB_DO_WIDE_NEGO;
	TRACEPRINTF("W *");
}


#if 0
/* Timer to work around chip flaw: When selecting and the bus is 
 * busy, we sometimes miss a Selection timeout IRQ */
void DC395x_selection_timeout_missed(unsigned long ptr);
/* Sets the timer to wake us up */
static void DC395x_selto_timer(struct AdapterCtlBlk *pACB)
{
	if (timer_pending(&pACB->SelTO_Timer))
		return;
	init_timer(&pACB->SelTO_Timer);
	pACB->SelTO_Timer.function = DC395x_selection_timeout_missed;
	pACB->SelTO_Timer.data = (unsigned long) pACB;
	if (time_before
	    (jiffies + HZ, pACB->pScsiHost->last_reset + HZ / 2))
		pACB->SelTO_Timer.expires =
		    pACB->pScsiHost->last_reset + HZ / 2 + 1;
	else
		pACB->SelTO_Timer.expires = jiffies + HZ + 1;
	add_timer(&pACB->SelTO_Timer);
}


void DC395x_selection_timeout_missed(unsigned long ptr)
{
	unsigned long flags;
	struct AdapterCtlBlk *pACB = (struct AdapterCtlBlk *) ptr;
	struct ScsiReqBlk *pSRB;
	dprintkl(KERN_DEBUG, "Chip forgot to produce SelTO IRQ!\n");
	if (!pACB->pActiveDCB || !pACB->pActiveDCB->pActiveSRB) {
		dprintkl(KERN_DEBUG, "... but no cmd pending? Oops!\n");
		return;
	}
	DC395x_LOCK_IO(pACB->pScsiHost);
	pSRB = pACB->pActiveDCB->pActiveSRB;
	TRACEPRINTF("N/TO *");
	DC395x_Disconnect(pACB);
	DC395x_UNLOCK_IO(pACB->pScsiHost);
}
#endif


/*
 * scsiio
 *		DC395x_DoWaitingSRB    DC395x_SRBdone 
 *		DC395x_SendSRB         DC395x_RequestSense
 */
u8
DC395x_StartSCSI(struct AdapterCtlBlk * pACB, struct DeviceCtlBlk * pDCB,
		 struct ScsiReqBlk * pSRB)
{
	u16 s_stat2, return_code;
	u8 s_stat, scsicommand, i, identify_message;
	u8 *ptr;

	dprintkdbg(DBG_0, "DC395x_StartSCSI..............\n");
	pSRB->TagNumber = TAG_NONE;	/* pACB->TagMaxNum: had error read in eeprom */

	s_stat = DC395x_read8(TRM_S1040_SCSI_SIGNAL);
	s_stat2 = 0;
	s_stat2 = DC395x_read16(TRM_S1040_SCSI_STATUS);
	TRACEPRINTF("Start %02x *", s_stat);
#if 1
	if (s_stat & 0x20 /* s_stat2 & 0x02000 */ ) {
		dprintkdbg(DBG_KG,
		       "StartSCSI: pid %li(%02i-%i): BUSY %02x %04x\n",
		       pSRB->pcmd->pid, pDCB->TargetID, pDCB->TargetLUN,
		       s_stat, s_stat2);
		/*
		 * Try anyway?
		 *
		 * We could, BUT: Sometimes the TRM_S1040 misses to produce a Selection
		 * Timeout, a Disconnect or a Reselction IRQ, so we would be screwed!
		 * (This is likely to be a bug in the hardware. Obviously, most people
		 *  only have one initiator per SCSI bus.)
		 * Instead let this fail and have the timer make sure the command is 
		 * tried again after a short time
		 */
		TRACEPRINTF("^*");
		/*DC395x_selto_timer (pACB); */
		/*DC395x_monitor_next_IRQ = 1; */
		return 1;
	}
#endif
	if (pACB->pActiveDCB) {
		dprintkl(KERN_DEBUG, "We try to start a SCSI command (%li)!\n",
		       pSRB->pcmd->pid);
		dprintkl(KERN_DEBUG, "While another one (%li) is active!!\n",
		       (pACB->pActiveDCB->pActiveSRB ? pACB->pActiveDCB->
			pActiveSRB->pcmd->pid : 0));
		TRACEOUT(" %s\n", pSRB->debugtrace);
		if (pACB->pActiveDCB->pActiveSRB)
			TRACEOUT(" %s\n",
				 pACB->pActiveDCB->pActiveSRB->debugtrace);
		return 1;
	}
	if (DC395x_read16(TRM_S1040_SCSI_STATUS) & SCSIINTERRUPT) {
		dprintkdbg(DBG_KG,
		       "StartSCSI failed (busy) for pid %li(%02i-%i)\n",
		       pSRB->pcmd->pid, pDCB->TargetID, pDCB->TargetLUN);
		TRACEPRINTF("°*");
		return 1;
	}
	/* Allow starting of SCSI commands half a second before we allow the mid-level
	 * to queue them again after a reset */
	if (time_before(jiffies, pACB->pScsiHost->last_reset - HZ / 2)) {
		dprintkdbg(DBG_KG, 
		       "We were just reset and don't accept commands yet!\n");
		return 1;
	}

	/* Flush FIFO */
	DC395x_clrfifo(pACB, "Start");
	DC395x_write8(TRM_S1040_SCSI_HOSTID, pACB->pScsiHost->this_id);
	DC395x_write8(TRM_S1040_SCSI_TARGETID, pDCB->TargetID);
	DC395x_write8(TRM_S1040_SCSI_SYNC, pDCB->SyncPeriod);
	DC395x_write8(TRM_S1040_SCSI_OFFSET, pDCB->SyncOffset);
	pSRB->ScsiPhase = PH_BUS_FREE;	/* initial phase */

	identify_message = pDCB->IdentifyMsg;
	/*DC395x_TRM_write8(TRM_S1040_SCSI_IDMSG, identify_message); */
	/* Don't allow disconnection for AUTO_REQSENSE: Cont.All.Cond.! */
	if (pSRB->SRBFlag & AUTO_REQSENSE)
		identify_message &= 0xBF;

	if (((pSRB->pcmd->cmnd[0] == INQUIRY)
	     || (pSRB->pcmd->cmnd[0] == REQUEST_SENSE)
	     || (pSRB->SRBFlag & AUTO_REQSENSE))
	    && (((pDCB->SyncMode & WIDE_NEGO_ENABLE)
		 && !(pDCB->SyncMode & WIDE_NEGO_DONE))
		|| ((pDCB->SyncMode & SYNC_NEGO_ENABLE)
		    && !(pDCB->SyncMode & SYNC_NEGO_DONE)))
	    && (pDCB->TargetLUN == 0)) {
		pSRB->MsgOutBuf[0] = identify_message;
		pSRB->MsgCnt = 1;
		scsicommand = SCMD_SEL_ATNSTOP;
		pSRB->SRBState = SRB_MSGOUT;
#ifndef SYNC_FIRST
		if (pDCB->SyncMode & WIDE_NEGO_ENABLE
		    && pDCB->Inquiry7 & SCSI_INQ_WBUS16) {
			DC395x_Build_WDTR(pACB, pDCB, pSRB);
			goto no_cmd;
		}
#endif
		if (pDCB->SyncMode & SYNC_NEGO_ENABLE
		    && pDCB->Inquiry7 & SCSI_INQ_SYNC) {
			DC395x_Build_SDTR(pACB, pDCB, pSRB);
			goto no_cmd;
		}
		if (pDCB->SyncMode & WIDE_NEGO_ENABLE
		    && pDCB->Inquiry7 & SCSI_INQ_WBUS16) {
			DC395x_Build_WDTR(pACB, pDCB, pSRB);
			goto no_cmd;
		}
		pSRB->MsgCnt = 0;
	}
	/* 
	 ** Send identify message   
	 */
	DC395x_write8(TRM_S1040_SCSI_FIFO, identify_message);

	scsicommand = SCMD_SEL_ATN;
	pSRB->SRBState = SRB_START_;
#ifndef DC395x_NO_TAGQ
	if ((pDCB->SyncMode & EN_TAG_QUEUEING)
	    && (identify_message & 0xC0)) {
		/* Send Tag message */
		u32 tag_mask = 1;
		u8 tag_number = 0;
		while (tag_mask & pDCB->TagMask
		       && tag_number <= pDCB->MaxCommand) {
			tag_mask = tag_mask << 1;
			tag_number++;
		}
		if (tag_number >= pDCB->MaxCommand) {
			dprintkl(KERN_WARNING,
			       "Start_SCSI: Out of tags for pid %li (%i-%i)\n",
			       pSRB->pcmd->pid, pSRB->pcmd->device->id,
			       pSRB->pcmd->device->lun);
			pSRB->SRBState = SRB_READY;
			DC395x_write16(TRM_S1040_SCSI_CONTROL,
				       DO_HWRESELECT);
			return 1;
		}
		/* 
		 ** Send Tag id
		 */
		DC395x_write8(TRM_S1040_SCSI_FIFO, MSG_SIMPLE_QTAG);
		DC395x_write8(TRM_S1040_SCSI_FIFO, tag_number);
		pDCB->TagMask |= tag_mask;
		pSRB->TagNumber = tag_number;
		TRACEPRINTF("Tag %i *", tag_number);

		scsicommand = SCMD_SEL_ATN3;
		pSRB->SRBState = SRB_START_;
	}
#endif
/*polling:*/
	/*
	 *          Send CDB ..command block .........                     
	 */
	dprintkdbg(DBG_KG, 
	       "StartSCSI (pid %li) %02x (%i-%i): Tag %i\n",
	       pSRB->pcmd->pid, pSRB->pcmd->cmnd[0],
	       pSRB->pcmd->device->id, pSRB->pcmd->device->lun,
	       pSRB->TagNumber);
	if (pSRB->SRBFlag & AUTO_REQSENSE) {
		DC395x_write8(TRM_S1040_SCSI_FIFO, REQUEST_SENSE);
		DC395x_write8(TRM_S1040_SCSI_FIFO, (pDCB->TargetLUN << 5));
		DC395x_write8(TRM_S1040_SCSI_FIFO, 0);
		DC395x_write8(TRM_S1040_SCSI_FIFO, 0);
		DC395x_write8(TRM_S1040_SCSI_FIFO,
			      sizeof(pSRB->pcmd->sense_buffer));
		DC395x_write8(TRM_S1040_SCSI_FIFO, 0);
	} else {
		ptr = (u8 *) pSRB->pcmd->cmnd;
		for (i = 0; i < pSRB->pcmd->cmd_len; i++)
			DC395x_write8(TRM_S1040_SCSI_FIFO, *ptr++);
	}
      no_cmd:
	DC395x_write16(TRM_S1040_SCSI_CONTROL,
		       DO_HWRESELECT | DO_DATALATCH);
	if (DC395x_read16(TRM_S1040_SCSI_STATUS) & SCSIINTERRUPT) {
		/* 
		 * If DC395x_StartSCSI return 1:
		 * we caught an interrupt (must be reset or reselection ... )
		 * : Let's process it first!
		 */
		dprintkdbg(DBG_0, "Debug: StartSCSI failed (busy) for pid %li(%02i-%i)!\n",
			pSRB->pcmd->pid, pDCB->TargetID, pDCB->TargetLUN);
		/*DC395x_clrfifo (pACB, "Start2"); */
		/*DC395x_write16 (TRM_S1040_SCSI_CONTROL, DO_HWRESELECT | DO_DATALATCH); */
		pSRB->SRBState = SRB_READY;
		DC395x_freetag(pDCB, pSRB);
		pSRB->MsgCnt = 0;
		return_code = 1;
		/* This IRQ should NOT get lost, as we did not acknowledge it */
	} else {
		/* 
		 * If DC395x_StartSCSI returns 0:
		 * we know that the SCSI processor is free
		 */
		pSRB->ScsiPhase = PH_BUS_FREE;	/* initial phase */
		pDCB->pActiveSRB = pSRB;
		pACB->pActiveDCB = pDCB;
		return_code = 0;
		/* it's important for atn stop */
		DC395x_write16(TRM_S1040_SCSI_CONTROL,
			       DO_DATALATCH | DO_HWRESELECT);
		/*
		 ** SCSI command
		 */
		TRACEPRINTF("%02x *", scsicommand);
		DC395x_write8(TRM_S1040_SCSI_COMMAND, scsicommand);
	}
	return return_code;
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_initAdapter
 ********************************************************************
 */

/**
 * dc395x_handle_interrupt - Handle an interrupt that has been confirmed to
 *                           have been triggered for this card.
 *
 * @pACB:	 a pointer to the adpter control block
 * @scsi_status: the status return when we checked the card
 **/
static void dc395x_handle_interrupt(struct AdapterCtlBlk *pACB, u16 scsi_status)
{
	struct DeviceCtlBlk *pDCB;
	struct ScsiReqBlk *pSRB;
	u16 phase;
	u8 scsi_intstatus;
	unsigned long flags;
	void (*DC395x_stateV) (struct AdapterCtlBlk *, struct ScsiReqBlk *,
			       u16 *);

	DC395x_LOCK_IO(pACB->pScsiHost);

	/* This acknowledges the IRQ */
	scsi_intstatus = DC395x_read8(TRM_S1040_SCSI_INTSTATUS);
	if ((scsi_status & 0x2007) == 0x2002)
		dprintkl(KERN_DEBUG, "COP after COP completed? %04x\n",
		       scsi_status);
#if 1				/*def DBG_0 */
	if (DC395x_monitor_next_IRQ) {
		dprintkl(KERN_INFO,
		       "status=%04x intstatus=%02x\n", scsi_status,
		       scsi_intstatus);
		DC395x_monitor_next_IRQ--;
	}
#endif
	/*DC395x_ACB_LOCK(pACB,acb_flags); */
	if (debug_enabled(DBG_KG)) {
		if (scsi_intstatus & INT_SELTIMEOUT)
		dprintkdbg(DBG_KG, "Sel Timeout IRQ\n");
	}
	/*dprintkl(KERN_DEBUG, "DC395x_IRQ: intstatus = %02x ", scsi_intstatus); */

	if (timer_pending(&pACB->SelTO_Timer))
		del_timer(&pACB->SelTO_Timer);

	if (scsi_intstatus & (INT_SELTIMEOUT | INT_DISCONNECT)) {
		DC395x_Disconnect(pACB);	/* bus free interrupt  */
		goto out_unlock;
	}
	if (scsi_intstatus & INT_RESELECTED) {
		DC395x_Reselect(pACB);
		goto out_unlock;
	}
	if (scsi_intstatus & INT_SELECT) {
		dprintkl(KERN_INFO, "Host does not support target mode!\n");
		goto out_unlock;
	}
	if (scsi_intstatus & INT_SCSIRESET) {
		DC395x_ScsiRstDetect(pACB);
		goto out_unlock;
	}
	if (scsi_intstatus & (INT_BUSSERVICE | INT_CMDDONE)) {
		pDCB = pACB->pActiveDCB;
		if (!pDCB) {
			dprintkl(KERN_DEBUG,
			       "Oops: BusService (%04x %02x) w/o ActiveDCB!\n",
			       scsi_status, scsi_intstatus);
			goto out_unlock;
		}
		pSRB = pDCB->pActiveSRB;
		if (pDCB->DCBFlag & ABORT_DEV_) {
			dprintkdbg(DBG_0, "MsgOut Abort Device.....\n");
			DC395x_EnableMsgOut_Abort(pACB, pSRB);
		}
		/*
		 ************************************************************
		 * software sequential machine
		 ************************************************************
		 */
		phase = (u16) pSRB->ScsiPhase;
		/* 
		 * 62037 or 62137
		 * call  DC395x_SCSI_phase0[]... "phase entry"
		 * handle every phase before start transfer
		 */
		/* DC395x_DataOutPhase0,     phase:0 */
		/* DC395x_DataInPhase0,      phase:1 */
		/* DC395x_CommandPhase0,     phase:2 */
		/* DC395x_StatusPhase0,      phase:3 */
		/* DC395x_Nop0,              phase:4 PH_BUS_FREE .. initial phase */
		/* DC395x_Nop0,              phase:5 PH_BUS_FREE .. initial phase */
		/* DC395x_MsgOutPhase0,      phase:6 */
		/* DC395x_MsgInPhase0,       phase:7 */
		DC395x_stateV = (void *) DC395x_SCSI_phase0[phase];
		DC395x_stateV(pACB, pSRB, &scsi_status);
		/* 
		 *$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ 
		 *
		 *        if there were any exception occured
		 *        scsi_status will be modify to bus free phase
		 * new scsi_status transfer out from ... previous DC395x_stateV
		 *
		 *$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$ 
		 */
		pSRB->ScsiPhase = scsi_status & PHASEMASK;
		phase = (u16) scsi_status & PHASEMASK;
		/* 
		 * call  DC395x_SCSI_phase1[]... "phase entry"
		 * handle every phase do transfer
		 */
		/* DC395x_DataOutPhase1,     phase:0 */
		/* DC395x_DataInPhase1,      phase:1 */
		/* DC395x_CommandPhase1,     phase:2 */
		/* DC395x_StatusPhase1,      phase:3 */
		/* DC395x_Nop1,              phase:4 PH_BUS_FREE .. initial phase */
		/* DC395x_Nop1,              phase:5 PH_BUS_FREE .. initial phase */
		/* DC395x_MsgOutPhase1,      phase:6 */
		/* DC395x_MsgInPhase1,       phase:7 */
		DC395x_stateV = (void *) DC395x_SCSI_phase1[phase];
		DC395x_stateV(pACB, pSRB, &scsi_status);
	}
      out_unlock:
	DC395x_UNLOCK_IO(pACB->pScsiHost);
	return;
}

irqreturn_t DC395x_Interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct AdapterCtlBlk *pACB = DC395x_pACB_start;
	u16 scsi_status;
	u8 dma_status;
	irqreturn_t handled = IRQ_NONE;

	dprintkdbg(DBG_0, "DC395x_Interrupt..............\n");
#if debug_enabled(DBG_RECURSION)
        if (dbg_in_driver++ > NORM_REC_LVL) {
		dprintkl(KERN_DEBUG, "%i interrupt recursion?\n", dbg_in_driver);
	}
#endif

	/*
	 * Find which card generated the interrupt. Note that it may have
	 * been something else that we share the interupt with which
	 * actually generated it.
	 *
	 * We'll check the interupt status register of each card that
	 * is on the IRQ that was responsible for this interupt.
	 */
	for (; pACB != NULL; pACB = pACB->pNextACB) {
		if (pACB->IRQLevel != (u8) irq) {
			/* card is not on the irq that triggered */
			continue;
		}

		/*
		 * Ok, we've found a card on the correct irq,
		 * let's check if an interupt is pending
		 */
		scsi_status = DC395x_read16(TRM_S1040_SCSI_STATUS);
		dma_status = DC395x_read8(TRM_S1040_DMA_STATUS);
		if (scsi_status & SCSIINTERRUPT) {
			/* interupt pending - let's process it! */
			dc395x_handle_interrupt(pACB, scsi_status);
			handled = IRQ_HANDLED;
		}
		else if (dma_status & 0x20) {
			/* Error from the DMA engine */
			dprintkl(KERN_INFO, "Interrupt from DMA engine: %02x!\n",
			       dma_status);
#if 0
			dprintkl(KERN_INFO, "This means DMA error! Try to handle ...\n");
			if (pACB->pActiveDCB) {
				pACB->pActiveDCB-> DCBFlag |= ABORT_DEV_;
				if (pACB->pActiveDCB->pActiveSRB)
					DC395x_EnableMsgOut_Abort(pACB, pACB->pActiveDCB->pActiveSRB);
			}
			DC395x_write8(TRM_S1040_DMA_CONTROL, ABORTXFER | CLRXFIFO);
#else
			dprintkl(KERN_INFO, "Ignoring DMA error (probably a bad thing) ...\n");
			pACB = (struct AdapterCtlBlk *)NULL;
#endif
			handled = IRQ_HANDLED;
		}
	}

#if debug_enabled(DBG_RECURSION)
	dbg_in_driver--
#endif
	return handled;
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_MsgOutPhase0: one of DC395x_SCSI_phase0[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase0[phase]
 *			           if phase =6
 ********************************************************************
 */
static void
DC395x_MsgOutPhase0(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		    u16 * pscsi_status)
{
	dprintkdbg(DBG_0, "DC395x_MsgOutPhase0.....\n");
	if (pSRB->SRBState & (SRB_UNEXPECT_RESEL + SRB_ABORT_SENT)) {
		*pscsi_status = PH_BUS_FREE;	/*.. initial phase */
	}
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	pSRB->SRBState &= ~SRB_MSGOUT;
	TRACEPRINTF("MOP0 *");
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_MsgOutPhase1: one of DC395x_SCSI_phase0[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase0[phase]
 *					if phase =6	    
 ********************************************************************
 */
static void
DC395x_MsgOutPhase1(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		    u16 * pscsi_status)
{
	u16 i;
	u8 *ptr;
	struct DeviceCtlBlk *pDCB;

	dprintkdbg(DBG_0, "DC395x_MsgOutPhase1..............\n");
	TRACEPRINTF("MOP1*");
	pDCB = pACB->pActiveDCB;
	DC395x_clrfifo(pACB, "MOP1");
	if (!(pSRB->SRBState & SRB_MSGOUT)) {
		pSRB->SRBState |= SRB_MSGOUT;
		dprintkl(KERN_DEBUG, "Debug: pid %li: MsgOut Phase unexpected.\n", pSRB->pcmd->pid);	/* So what ? */
	}
	if (!pSRB->MsgCnt) {
		dprintkdbg(DBG_0, "Debug: pid %li: NOP Msg (no output message there).\n",
			pSRB->pcmd->pid);
		DC395x_write8(TRM_S1040_SCSI_FIFO, MSG_NOP);
		DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
		DC395x_write8(TRM_S1040_SCSI_COMMAND, SCMD_FIFO_OUT);
		TRACEPRINTF("\\*");
		TRACEOUT(" %s\n", pSRB->debugtrace);
		return;
	}
	ptr = (u8 *) pSRB->MsgOutBuf;
	TRACEPRINTF("(*");
	/*dprintkl(KERN_DEBUG, "Send msg: "); DC395x_printMsg (ptr, pSRB->MsgCnt); */
	/*dprintkl(KERN_DEBUG, "MsgOut: "); */
	for (i = 0; i < pSRB->MsgCnt; i++) {
		TRACEPRINTF("%02x *", *ptr);
		DC395x_write8(TRM_S1040_SCSI_FIFO, *ptr++);
	}
	TRACEPRINTF(")*");
	pSRB->MsgCnt = 0;
	/*printk("\n"); */
	if (			/*(pDCB->DCBFlag & ABORT_DEV_) && */
		   (pSRB->MsgOutBuf[0] == MSG_ABORT))
		pSRB->SRBState = SRB_ABORT_SENT;

	/*1.25 */
	/*DC395x_write16 (TRM_S1040_SCSI_CONTROL, DO_DATALATCH); *//* it's important for atn stop */
	/*
	 ** SCSI command 
	 */
	/*TRACEPRINTF (".*"); */
	DC395x_write8(TRM_S1040_SCSI_COMMAND, SCMD_FIFO_OUT);
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_CommandPhase0: one of DC395x_SCSI_phase0[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase0[phase]
 *				if phase =2 
 ********************************************************************
 */
static void
DC395x_CommandPhase0(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		     u16 * pscsi_status)
{
	TRACEPRINTF("COP0 *");
	/*1.25 */
	/*DC395x_clrfifo (pACB, COP0); */
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_CommandPhase1: one of DC395x_SCSI_phase1[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase1[phase]
 *				if phase =2    	 
 ********************************************************************
 */
static void
DC395x_CommandPhase1(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		     u16 * pscsi_status)
{
	struct DeviceCtlBlk *pDCB;
	u8 *ptr;
	u16 i;

	dprintkdbg(DBG_0, "DC395x_CommandPhase1..............\n");
	TRACEPRINTF("COP1*");
	DC395x_clrfifo(pACB, "COP1");
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_CLRATN);
	if (!(pSRB->SRBFlag & AUTO_REQSENSE)) {
		ptr = (u8 *) pSRB->pcmd->cmnd;
		for (i = 0; i < pSRB->pcmd->cmd_len; i++) {
			DC395x_write8(TRM_S1040_SCSI_FIFO, *ptr);
			ptr++;
		}
	} else {
		DC395x_write8(TRM_S1040_SCSI_FIFO, REQUEST_SENSE);
		pDCB = pACB->pActiveDCB;
		/* target id */
		DC395x_write8(TRM_S1040_SCSI_FIFO, (pDCB->TargetLUN << 5));
		DC395x_write8(TRM_S1040_SCSI_FIFO, 0);
		DC395x_write8(TRM_S1040_SCSI_FIFO, 0);
		DC395x_write8(TRM_S1040_SCSI_FIFO,
			      sizeof(pSRB->pcmd->sense_buffer));
		DC395x_write8(TRM_S1040_SCSI_FIFO, 0);
	}
	pSRB->SRBState |= SRB_COMMAND;
	/* it's important for atn stop */
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
	/* SCSI command */
	TRACEPRINTF(".*");
	DC395x_write8(TRM_S1040_SCSI_COMMAND, SCMD_FIFO_OUT);
}


/* Do sanity checks for S/G list */
static inline void DC395x_check_SG(struct ScsiReqBlk *pSRB)
{
	if (debug_enabled(DBG_SGPARANOIA)) {
		unsigned Length = 0;
		unsigned Idx = pSRB->SRBSGIndex;
		struct SGentry *psge = pSRB->SegmentX + Idx;
		for (; Idx < pSRB->SRBSGCount; psge++, Idx++)
			Length += psge->length;
		if (Length != pSRB->SRBTotalXferLength)
			dprintkdbg(DBG_SGPARANOIA,
			       "Inconsistent SRB S/G lengths (Tot=%i, Count=%i) !!\n",
			       pSRB->SRBTotalXferLength, Length);
	}			       
}


/*
 * Compute the next Scatter Gather list index and adjust its length
 * and address if necessary; also compute virt_addr
 */
void DC395x_update_SGlist(struct ScsiReqBlk *pSRB, u32 Left)
{
	struct SGentry *psge;
	u32 Xferred = 0;
	u8 Idx;
	Scsi_Cmnd *pcmd = pSRB->pcmd;
	struct scatterlist *sg;
	int segment = pcmd->use_sg;

	dprintkdbg(DBG_KG, "Update SG: Total %i, Left %i\n",
	       pSRB->SRBTotalXferLength, Left);
	DC395x_check_SG(pSRB);
	psge = pSRB->SegmentX + pSRB->SRBSGIndex;
	/* data that has already been transferred */
	Xferred = pSRB->SRBTotalXferLength - Left;
	if (pSRB->SRBTotalXferLength != Left) {
		/*DC395x_check_SG_TX (pSRB, Xferred); */
		/* Remaining */
		pSRB->SRBTotalXferLength = Left;
		/* parsing from last time disconnect SGIndex */
		for (Idx = pSRB->SRBSGIndex; Idx < pSRB->SRBSGCount; Idx++) {
			/* Complete SG entries done */
			if (Xferred >= psge->length)
				Xferred -= psge->length;
			/* Partial SG entries done */
			else {
				psge->length -= Xferred;	/* residue data length  */
				psge->address += Xferred;	/* residue data pointer */
				pSRB->SRBSGIndex = Idx;
				pci_dma_sync_single(pSRB->pSRBDCB->
						    pDCBACB->pdev,
						    pSRB->SRBSGBusAddr,
						    sizeof(struct SGentry)
						    *
						    DC395x_MAX_SG_LISTENTRY,
						    PCI_DMA_TODEVICE);
				break;
			}
			psge++;
		}
		DC395x_check_SG(pSRB);
	}
	/* We need the corresponding virtual address sg_to_virt */
	/*dprintkl(KERN_DEBUG, "sg_to_virt: bus %08x -> virt ", psge->address); */
	if (!segment) {
		pSRB->virt_addr += Xferred;
		/*printk("%p\n", pSRB->virt_addr); */
		return;
	}
	/* We have to walk the scatterlist to find it */
	sg = (struct scatterlist *) pcmd->request_buffer;
	while (segment--) {
		/*printk("(%08x)%p ", BUS_ADDR(*sg), PAGE_ADDRESS(sg)); */
		unsigned long mask =
		    ~((unsigned long) sg->length - 1) & PAGE_MASK;
		if ((BUS_ADDR(*sg) & mask) == (psge->address & mask)) {
			pSRB->virt_addr = (PAGE_ADDRESS(sg)
					   + psge->address -
					   (psge->address & PAGE_MASK));
			/*printk("%p\n", pSRB->virt_addr); */
			return;
		}
		++sg;
	}
	dprintkl(KERN_ERR, "sg_to_virt failed!\n");
	pSRB->virt_addr = 0;
}


/* 
 * DC395x_cleanup_after_transfer
 * 
 * Makes sure, DMA and SCSI engine are empty, after the transfer has finished
 * KG: Currently called from  StatusPhase1 ()
 * Should probably also be called from other places
 * Best might be to call it in DataXXPhase0, if new phase will differ 
 */
static void
DC395x_cleanup_after_transfer(struct AdapterCtlBlk *pACB,
			      struct ScsiReqBlk *pSRB)
{
	TRACEPRINTF(" Cln*");
	/*DC395x_write8 (TRM_S1040_DMA_STATUS, FORCEDMACOMP); */
	if (DC395x_read16(TRM_S1040_DMA_COMMAND) & 0x0001) {	/* read */
		if (!(DC395x_read8(TRM_S1040_SCSI_FIFOCNT) & 0x40))
			DC395x_clrfifo(pACB, "ClnIn");

		if (!(DC395x_read8(TRM_S1040_DMA_FIFOSTAT) & 0x80))
			DC395x_write8(TRM_S1040_DMA_CONTROL, CLRXFIFO);
	} else {		/* write */
		if (!(DC395x_read8(TRM_S1040_DMA_FIFOSTAT) & 0x80))
			DC395x_write8(TRM_S1040_DMA_CONTROL, CLRXFIFO);

		if (!(DC395x_read8(TRM_S1040_SCSI_FIFOCNT) & 0x40))
			DC395x_clrfifo(pACB, "ClnOut");

	}
	/*1.25 */
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);
}


/*
 * Those no of bytes will be transfered w/ PIO through the SCSI FIFO
 * Seems to be needed for unknown reasons; could be a hardware bug :-(
 */
#define DC395x_LASTPIO 4
/*
 ********************************************************************
 * scsiio
 *	DC395x_DataOutPhase0: one of DC395x_SCSI_phase0[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase0[phase]
 *				if phase =0 
 ********************************************************************
 */
void
DC395x_DataOutPhase0(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		     u16 * pscsi_status)
{
	u16 scsi_status;
	u32 dLeftCounter = 0;
	struct DeviceCtlBlk *pDCB = pSRB->pSRBDCB;

	dprintkdbg(DBG_0, "DC395x_DataOutPhase0.....\n");
	TRACEPRINTF("DOP0*");
	pDCB = pSRB->pSRBDCB;
	scsi_status = *pscsi_status;

	/*
	 * KG: We need to drain the buffers before we draw any conclusions!
	 * This means telling the DMA to push the rest into SCSI, telling
	 * SCSI to push the rest to the bus.
	 * However, the device might have been the one to stop us (phase
	 * change), and the data in transit just needs to be accounted so
	 * it can be retransmitted.)
	 */
	/* 
	 * KG: Stop DMA engine pushing more data into the SCSI FIFO
	 * If we need more data, the DMA SG list will be freshly set up, anyway
	 */
	dprintkdbg(DBG_PIO, "DOP0: DMA_FCNT: %02x, DMA_FSTAT: %02x, SCSI_FCNT: %02x, CTR %06x, stat %04x, Tot: %06x\n",
	       DC395x_read8(TRM_S1040_DMA_FIFOCNT),
	       DC395x_read8(TRM_S1040_DMA_FIFOSTAT),
	       DC395x_read8(TRM_S1040_SCSI_FIFOCNT),
	       DC395x_read32(TRM_S1040_SCSI_COUNTER), scsi_status,
	       pSRB->SRBTotalXferLength);
	DC395x_write8(TRM_S1040_DMA_CONTROL, STOPDMAXFER | CLRXFIFO);

	if (!(pSRB->SRBState & SRB_XFERPAD)) {
		if (scsi_status & PARITYERROR)
			pSRB->SRBStatus |= PARITY_ERROR;

		/*
		 * KG: Right, we can't just rely on the SCSI_COUNTER, because this
		 * is the no of bytes it got from the DMA engine not the no it 
		 * transferred successfully to the device. (And the difference could
		 * be as much as the FIFO size, I guess ...)
		 */
		if (!(scsi_status & SCSIXFERDONE)) {
			/*
			 * when data transfer from DMA FIFO to SCSI FIFO
			 * if there was some data left in SCSI FIFO
			 */
			dLeftCounter =
			    (u32) (DC395x_read8(TRM_S1040_SCSI_FIFOCNT) &
				   0x1F);
			if (pDCB->SyncPeriod & WIDE_SYNC)
				dLeftCounter <<= 1;

			dprintkdbg(DBG_KG,
			       "Debug: SCSI FIFO contains %i %s in DOP0\n",
			       DC395x_read8(TRM_S1040_SCSI_FIFOCNT),
			       (pDCB->
				SyncPeriod & WIDE_SYNC) ? "words" :
			       "bytes");
			dprintkdbg(DBG_KG,
			       "SCSI FIFOCNT %02x, SCSI CTR %08x\n",
			       DC395x_read8(TRM_S1040_SCSI_FIFOCNT),
			       DC395x_read32(TRM_S1040_SCSI_COUNTER));
			dprintkdbg(DBG_KG,
			       "DMA FIFOCNT %04x, FIFOSTAT %02x, DMA CTR %08x\n",
			       DC395x_read8(TRM_S1040_DMA_FIFOCNT),
			       DC395x_read8(TRM_S1040_DMA_FIFOSTAT),
			       DC395x_read32(TRM_S1040_DMA_CXCNT));

			/*
			 * if WIDE scsi SCSI FIFOCNT unit is word !!!
			 * so need to *= 2
			 */
		}
		/*
		 * calculate all the residue data that not yet tranfered
		 * SCSI transfer counter + left in SCSI FIFO data
		 *
		 * .....TRM_S1040_SCSI_COUNTER (24bits)
		 * The counter always decrement by one for every SCSI byte transfer.
		 * .....TRM_S1040_SCSI_FIFOCNT ( 5bits)
		 * The counter is SCSI FIFO offset counter (in units of bytes or! words)
		 */
		if (pSRB->SRBTotalXferLength > DC395x_LASTPIO)
			dLeftCounter +=
			    DC395x_read32(TRM_S1040_SCSI_COUNTER);
		TRACEPRINTF("%06x *", dLeftCounter);

		/* Is this a good idea? */
		/*DC395x_clrfifo (pACB, "DOP1"); */
		/* KG: What is this supposed to be useful for? WIDE padding stuff? */
		if (dLeftCounter == 1 && pDCB->SyncPeriod & WIDE_SYNC
		    && pSRB->pcmd->request_bufflen % 2) {
			dLeftCounter = 0;
			dprintkl(KERN_INFO, "DOP0: Discard 1 byte. (%02x)\n",
			       scsi_status);
		}
		/*
		 * KG: Oops again. Same thinko as above: The SCSI might have been
		 * faster than the DMA engine, so that it ran out of data.
		 * In that case, we have to do just nothing! 
		 * But: Why the interrupt: No phase change. No XFERCNT_2_ZERO. Or?
		 */
		/*
		 * KG: This is nonsense: We have been WRITING data to the bus
		 * If the SCSI engine has no bytes left, how should the DMA engine?
		 */
		if ((dLeftCounter ==
		     0) /*|| (scsi_status & SCSIXFERCNT_2_ZERO) ) */ ) {
			/*
			 * int ctr = 6000000; u8 TempDMAstatus;
			 * do
			 * {
			 *  TempDMAstatus = DC395x_read8(TRM_S1040_DMA_STATUS);
			 * } while( !(TempDMAstatus & DMAXFERCOMP) && --ctr);
			 * if (ctr < 6000000-1) dprintkl(KERN_DEBUG, "DMA should be complete ... in DOP1\n");
			 * if (!ctr) dprintkl(KERN_ERR, "Deadlock in DataOutPhase0 !!\n");
			 */
			pSRB->SRBTotalXferLength = 0;
		} else {	/* Update SG list         */
			/*
			 * if transfer not yet complete
			 * there were some data residue in SCSI FIFO or
			 * SCSI transfer counter not empty
			 */
			long oldXferred =
			    pSRB->SRBTotalXferLength - dLeftCounter;
			const int diff =
			    (pDCB->SyncPeriod & WIDE_SYNC) ? 2 : 1;
			DC395x_update_SGlist(pSRB, dLeftCounter);
			/* KG: Most ugly hack! Apparently, this works around a chip bug */
			if ((pSRB->SegmentX[pSRB->SRBSGIndex].length ==
			     diff && pSRB->pcmd->use_sg)
			    || ((oldXferred & ~PAGE_MASK) ==
				(PAGE_SIZE - diff))
			    ) {
				dprintkl(KERN_INFO,
				       "Work around chip bug (%i)?\n", diff);
				dLeftCounter =
				    pSRB->SRBTotalXferLength - diff;
				DC395x_update_SGlist(pSRB, dLeftCounter);
				/*pSRB->SRBTotalXferLength -= diff; */
				/*pSRB->virt_addr += diff; */
				/*if (pSRB->pcmd->use_sg) */
				/*      pSRB->SRBSGIndex++; */
			}
		}
	}
#if 0
	if (!(DC395x_read8(TRM_S1040_SCSI_FIFOCNT) & 0x40))
		dprintkl(KERN_DEBUG,
			"DOP0(%li): %i bytes in SCSI FIFO! (Clear!)\n",
			pSRB->pcmd->pid,
			DC395x_read8(TRM_S1040_SCSI_FIFOCNT) & 0x1f);
#endif
	/*DC395x_clrfifo (pACB, "DOP0"); */
	/*DC395x_write8 (TRM_S1040_DMA_CONTROL, CLRXFIFO | ABORTXFER); */
#if 1
	if ((*pscsi_status & PHASEMASK) != PH_DATA_OUT) {
		/*dprintkl(KERN_DEBUG, "Debug: Clean up after Data Out ...\n"); */
		DC395x_cleanup_after_transfer(pACB, pSRB);
	}
#endif
	TRACEPRINTF(".*");
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_DataOutPhase1: one of DC395x_SCSI_phase0[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase0[phase]
 *				if phase =0    
 *		62037
 ********************************************************************
 */
static void
DC395x_DataOutPhase1(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		     u16 * pscsi_status)
{

	dprintkdbg(DBG_0, "DC395x_DataOutPhase1.....\n");
	/*1.25 */
	TRACEPRINTF("DOP1*");
	DC395x_clrfifo(pACB, "DOP1");
	/*
	 ** do prepare befor transfer when data out phase
	 */
	DC395x_DataIO_transfer(pACB, pSRB, XFERDATAOUT);
	TRACEPRINTF(".*");
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_DataInPhase0: one of DC395x_SCSI_phase1[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase1[phase]
 *				if phase =1  
 ********************************************************************
 */
void
DC395x_DataInPhase0(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		    u16 * pscsi_status)
{
	u16 scsi_status;
	u32 dLeftCounter = 0;
	/*struct DeviceCtlBlk*   pDCB = pSRB->pSRBDCB; */
	/*u8 bval; */

	dprintkdbg(DBG_0, "DC395x_DataInPhase0..............\n");
	TRACEPRINTF("DIP0*");
	scsi_status = *pscsi_status;

	/*
	 * KG: DataIn is much more tricky than DataOut. When the device is finished
	 * and switches to another phase, the SCSI engine should be finished too.
	 * But: There might still be bytes left in its FIFO to be fetched by the DMA
	 * engine and transferred to memory.
	 * We should wait for the FIFOs to be emptied by that (is there any way to 
	 * enforce this?) and then stop the DMA engine, because it might think, that
	 * there are more bytes to follow. Yes, the device might disconnect prior to
	 * having all bytes transferred! 
	 * Also we should make sure that all data from the DMA engine buffer's really
	 * made its way to the system memory! Some documentation on this would not
	 * seem to be a bad idea, actually.
	 */
	if (!(pSRB->SRBState & SRB_XFERPAD)) {
		if (scsi_status & PARITYERROR) {
			dprintkl(KERN_INFO,
			       "Parity Error (pid %li, target %02i-%i)\n",
			       pSRB->pcmd->pid, pSRB->pcmd->device->id,
			       pSRB->pcmd->device->lun);
			pSRB->SRBStatus |= PARITY_ERROR;
		}
		/*
		 * KG: We should wait for the DMA FIFO to be empty ...
		 * but: it would be better to wait first for the SCSI FIFO and then the
		 * the DMA FIFO to become empty? How do we know, that the device not already
		 * sent data to the FIFO in a MsgIn phase, eg.?
		 */
		if (!(DC395x_read8(TRM_S1040_DMA_FIFOSTAT) & 0x80)) {
#if 0
			int ctr = 6000000;
			dprintkl(KERN_DEBUG,
			       "DIP0: Wait for DMA FIFO to flush ...\n");
			/*DC395x_write8  (TRM_S1040_DMA_CONTROL, STOPDMAXFER); */
			/*DC395x_write32 (TRM_S1040_SCSI_COUNTER, 7); */
			/*DC395x_write8  (TRM_S1040_SCSI_COMMAND, SCMD_DMA_IN); */
			while (!
			       (DC395x_read16(TRM_S1040_DMA_FIFOSTAT) &
				0x80) && --ctr);
			if (ctr < 6000000 - 1)
				dprintkl(KERN_DEBUG
				       "DIP0: Had to wait for DMA ...\n");
			if (!ctr)
				dprintkl(KERN_ERR,
				       "Deadlock in DIP0 waiting for DMA FIFO empty!!\n");
			/*DC395x_write32 (TRM_S1040_SCSI_COUNTER, 0); */
#endif
			dprintkdbg(DBG_KG, "DIP0: DMA_FIFO: %02x %02x\n",
			       DC395x_read8(TRM_S1040_DMA_FIFOCNT),
			       DC395x_read8(TRM_S1040_DMA_FIFOSTAT));
		}
		/* Now: Check remainig data: The SCSI counters should tell us ... */
		dLeftCounter = DC395x_read32(TRM_S1040_SCSI_COUNTER)
		    + ((DC395x_read8(TRM_S1040_SCSI_FIFOCNT) & 0x1f)
		       << ((pSRB->pSRBDCB->SyncPeriod & WIDE_SYNC) ? 1 :
			   0));

		dprintkdbg(DBG_KG, "SCSI FIFO contains %i %s in DIP0\n",
			  DC395x_read8(TRM_S1040_SCSI_FIFOCNT) & 0x1f,
			  (pSRB->pSRBDCB->
			  SyncPeriod & WIDE_SYNC) ? "words" : "bytes");
		dprintkdbg(DBG_KG, "SCSI FIFOCNT %02x, SCSI CTR %08x\n",
			  DC395x_read8(TRM_S1040_SCSI_FIFOCNT),
			  DC395x_read32(TRM_S1040_SCSI_COUNTER));
		dprintkdbg(DBG_KG, "DMA FIFOCNT %02x,%02x DMA CTR %08x\n",
			  DC395x_read8(TRM_S1040_DMA_FIFOCNT),
			  DC395x_read8(TRM_S1040_DMA_FIFOSTAT),
			  DC395x_read32(TRM_S1040_DMA_CXCNT));
		dprintkdbg(DBG_KG, "Remaining: TotXfer: %i, SCSI FIFO+Ctr: %i\n",
			  pSRB->SRBTotalXferLength, dLeftCounter);
#if DC395x_LASTPIO
		/* KG: Less than or equal to 4 bytes can not be transfered via DMA, it seems. */
		if (dLeftCounter
		    && pSRB->SRBTotalXferLength <= DC395x_LASTPIO) {
			/*u32 addr = (pSRB->SegmentX[pSRB->SRBSGIndex].address); */
			/*DC395x_update_SGlist (pSRB, dLeftCounter); */
			dprintkdbg(DBG_PIO, "DIP0: PIO (%i %s) to %p for remaining %i bytes:",
				  DC395x_read8(TRM_S1040_SCSI_FIFOCNT) &
				  0x1f,
				  (pSRB->pSRBDCB->
				   SyncPeriod & WIDE_SYNC) ? "words" :
				  "bytes", pSRB->virt_addr,
				  pSRB->SRBTotalXferLength);

			if (pSRB->pSRBDCB->SyncPeriod & WIDE_SYNC)
				DC395x_write8(TRM_S1040_SCSI_CONFIG2,
					      CFG2_WIDEFIFO);

			while (DC395x_read8(TRM_S1040_SCSI_FIFOCNT) !=
			       0x40) {
				u8 byte =
				    DC395x_read8(TRM_S1040_SCSI_FIFO);
				*(pSRB->virt_addr)++ = byte;
				if (debug_enabled(DBG_PIO))
					printk(" %02x", byte);
				pSRB->SRBTotalXferLength--;
				dLeftCounter--;
				pSRB->SegmentX[pSRB->SRBSGIndex].length--;
				if (pSRB->SRBTotalXferLength
				    && !pSRB->SegmentX[pSRB->SRBSGIndex].
				    length) {
				    	if (debug_enabled(DBG_PIO))
						printk(" (next segment)");
					pSRB->SRBSGIndex++;
					DC395x_update_SGlist(pSRB,
							     dLeftCounter);
				}
			}
			if (pSRB->pSRBDCB->SyncPeriod & WIDE_SYNC) {
#if 1				/* Read the last byte ... */
				if (pSRB->SRBTotalXferLength > 0) {
					u8 byte =
					    DC395x_read8
					    (TRM_S1040_SCSI_FIFO);
					*(pSRB->virt_addr)++ = byte;
					pSRB->SRBTotalXferLength--;
					if (debug_enabled(DBG_PIO))
						printk(" %02x", byte);
				}
#endif
				DC395x_write8(TRM_S1040_SCSI_CONFIG2, 0);
			}
			/*printk(" %08x", *(u32*)(bus_to_virt (addr))); */
			/*pSRB->SRBTotalXferLength = 0; */
			if (debug_enabled(DBG_PIO))
				printk("\n");
		}
#endif				/* DC395x_LASTPIO */

#if 0
		/*
		 * KG: This was in DATAOUT. Does it also belong here?
		 * Nobody seems to know what counter and fifo_cnt count exactly ...
		 */
		if (!(scsi_status & SCSIXFERDONE)) {
			/*
			 * when data transfer from DMA FIFO to SCSI FIFO
			 * if there was some data left in SCSI FIFO
			 */
			dLeftCounter =
			    (u32) (DC395x_read8(TRM_S1040_SCSI_FIFOCNT) &
				   0x1F);
			if (pSRB->pSRBDCB->SyncPeriod & WIDE_SYNC)
				dLeftCounter <<= 1;
			/*
			 * if WIDE scsi SCSI FIFOCNT unit is word !!!
			 * so need to *= 2
			 * KG: Seems to be correct ...
			 */
		}
#endif
		/*dLeftCounter += DC395x_read32(TRM_S1040_SCSI_COUNTER); */
#if 0
		dprintkl(KERN_DEBUG,
		       "DIP0: ctr=%08x, DMA_FIFO=%02x,%02x SCSI_FIFO=%02x\n",
		       dLeftCounter, DC395x_read8(TRM_S1040_DMA_FIFOCNT),
		       DC395x_read8(TRM_S1040_DMA_FIFOSTAT),
		       DC395x_read8(TRM_S1040_SCSI_FIFOCNT));
		dprintkl(KERN_DEBUG, "DIP0: DMAStat %02x\n",
		       DC395x_read8(TRM_S1040_DMA_STATUS));
#endif

		/* KG: This should not be needed any more! */
		if ((dLeftCounter == 0)
		    || (scsi_status & SCSIXFERCNT_2_ZERO)) {
#if 0
			int ctr = 6000000;
			u8 TempDMAstatus;
			do {
				TempDMAstatus =
				    DC395x_read8(TRM_S1040_DMA_STATUS);
			} while (!(TempDMAstatus & DMAXFERCOMP) && --ctr);
			if (!ctr)
				dprintkl(KERN_ERR,
				       "Deadlock in DataInPhase0 waiting for DMA!!\n");
			pSRB->SRBTotalXferLength = 0;
#endif
#if 0				/*def DBG_KG             */
			dprintkl(KERN_DEBUG,
			       "DIP0: DMA not yet ready: %02x: %i -> %i bytes\n",
			       DC395x_read8(TRM_S1040_DMA_STATUS),
			       pSRB->SRBTotalXferLength, dLeftCounter);
#endif
			pSRB->SRBTotalXferLength = dLeftCounter;
		} else {	/* phase changed */
			/*
			 * parsing the case:
			 * when a transfer not yet complete 
			 * but be disconnected by target
			 * if transfer not yet complete
			 * there were some data residue in SCSI FIFO or
			 * SCSI transfer counter not empty
			 */
			DC395x_update_SGlist(pSRB, dLeftCounter);
		}
	}
	/* KG: The target may decide to disconnect: Empty FIFO before! */
	if ((*pscsi_status & PHASEMASK) != PH_DATA_IN) {
		/*dprintkl(KERN_DEBUG, "Debug: Clean up after Data In  ...\n"); */
		DC395x_cleanup_after_transfer(pACB, pSRB);
	}
#if 0
	/* KG: Make sure, no previous transfers are pending! */
	bval = DC395x_read8(TRM_S1040_SCSI_FIFOCNT);
	if (!(bval & 0x40)) {
		bval &= 0x1f;
		dprintkl(KERN_DEBUG,
		       "DIP0(%li): %i bytes in SCSI FIFO (stat %04x) (left %08x)!!\n",
		       pSRB->pcmd->pid, bval & 0x1f, scsi_status,
		       dLeftCounter);
		if ((dLeftCounter == 0)
		    || (scsi_status & SCSIXFERCNT_2_ZERO)) {
			dprintkl(KERN_DEBUG, "Clear FIFO!\n");
			DC395x_clrfifo(pACB, "DIP0");
		}
	}
#endif
	/*DC395x_write8 (TRM_S1040_DMA_CONTROL, CLRXFIFO | ABORTXFER); */

	/*DC395x_clrfifo (pACB, "DIP0"); */
	/*DC395x_write16 (TRM_S1040_SCSI_CONTROL, DO_DATALATCH); */
	TRACEPRINTF(".*");
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_DataInPhase1: one of DC395x_SCSI_phase0[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase0[phase]
 *				if phase =1 
 ********************************************************************
 */
static void
DC395x_DataInPhase1(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		    u16 * pscsi_status)
{
	dprintkdbg(DBG_0, "DC395x_DataInPhase1.....\n");
	/* FIFO should be cleared, if previous phase was not DataPhase */
	/*DC395x_clrfifo (pACB, "DIP1"); */
	/* Allow data in! */
	/*DC395x_write16 (TRM_S1040_SCSI_CONTROL, DO_DATALATCH); */
	TRACEPRINTF("DIP1:*");
	/*
	 ** do prepare before transfer when data in phase
	 */
	DC395x_DataIO_transfer(pACB, pSRB, XFERDATAIN);
	TRACEPRINTF(".*");
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_DataOutPhase1
 *		DC395x_DataInPhase1
 ********************************************************************
 */
void
DC395x_DataIO_transfer(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		       u16 ioDir)
{
	u8 bval;
	struct DeviceCtlBlk *pDCB;

	dprintkdbg(DBG_0, "DataIO_transfer %c (pid %li): len = %i, SG: %i/%i\n",
	       ((ioDir & DMACMD_DIR) ? 'r' : 'w'), pSRB->pcmd->pid,
	       pSRB->SRBTotalXferLength, pSRB->SRBSGIndex,
	       pSRB->SRBSGCount);
	TRACEPRINTF("%05x(%i/%i)*", pSRB->SRBTotalXferLength,
		    pSRB->SRBSGIndex, pSRB->SRBSGCount);
	pDCB = pSRB->pSRBDCB;
	if (pSRB == pACB->pTmpSRB) {
		dprintkl(KERN_ERR, "Using TmpSRB in DataPhase!\n");
	}
	if (pSRB->SRBSGIndex < pSRB->SRBSGCount) {
		if (pSRB->SRBTotalXferLength > DC395x_LASTPIO) {
			u8 dma_status = DC395x_read8(TRM_S1040_DMA_STATUS);
			/*
			 * KG: What should we do: Use SCSI Cmd 0x90/0x92?
			 * Maybe, even ABORTXFER would be appropriate
			 */
			if (dma_status & XFERPENDING) {
				dprintkl(KERN_DEBUG, "Xfer pending! Expect trouble!!\n");
				DC395x_dumpinfo(pACB, pDCB, pSRB);
				DC395x_write8(TRM_S1040_DMA_CONTROL,
					      CLRXFIFO);
			}
			/*DC395x_clrfifo (pACB, "IO"); */
			/* 
			 * load what physical address of Scatter/Gather list table want to be
			 * transfer 
			 */
			pSRB->SRBState |= SRB_DATA_XFER;
			DC395x_write32(TRM_S1040_DMA_XHIGHADDR, 0);
			if (pSRB->pcmd->use_sg) {	/* with S/G */
				ioDir |= DMACMD_SG;
				DC395x_write32(TRM_S1040_DMA_XLOWADDR,
					       pSRB->SRBSGBusAddr +
					       sizeof(struct SGentry) *
					       pSRB->SRBSGIndex);
				/* load how many bytes in the Scatter/Gather list table */
				DC395x_write32(TRM_S1040_DMA_XCNT,
					       ((u32)
						(pSRB->SRBSGCount -
						 pSRB->SRBSGIndex) << 3));
			} else {	/* without S/G */
				ioDir &= ~DMACMD_SG;
				DC395x_write32(TRM_S1040_DMA_XLOWADDR,
					       pSRB->SegmentX[0].address);
				DC395x_write32(TRM_S1040_DMA_XCNT,
					       pSRB->SegmentX[0].length);
			}
			/* load total transfer length (24bits) max value 16Mbyte */
			DC395x_write32(TRM_S1040_SCSI_COUNTER,
				       pSRB->SRBTotalXferLength);
			DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
			if (ioDir & DMACMD_DIR) {	/* read */
				DC395x_write8(TRM_S1040_SCSI_COMMAND,
					      SCMD_DMA_IN);
				DC395x_write16(TRM_S1040_DMA_COMMAND,
					       ioDir);
			} else {
				DC395x_write16(TRM_S1040_DMA_COMMAND,
					       ioDir);
				DC395x_write8(TRM_S1040_SCSI_COMMAND,
					      SCMD_DMA_OUT);
			}

		}
#if DC395x_LASTPIO
		else if (pSRB->SRBTotalXferLength > 0) {	/* The last four bytes: Do PIO */
			/*DC395x_clrfifo (pACB, "IO"); */
			/* 
			 * load what physical address of Scatter/Gather list table want to be
			 * transfer 
			 */
			pSRB->SRBState |= SRB_DATA_XFER;
			/* load total transfer length (24bits) max value 16Mbyte */
			DC395x_write32(TRM_S1040_SCSI_COUNTER,
				       pSRB->SRBTotalXferLength);
			DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
			if (ioDir & DMACMD_DIR) {	/* read */
				DC395x_write8(TRM_S1040_SCSI_COMMAND,
					      SCMD_FIFO_IN);
			} else {	/* write */
				int ln = pSRB->SRBTotalXferLength;
				if (pSRB->pSRBDCB->SyncPeriod & WIDE_SYNC)
					DC395x_write8
					    (TRM_S1040_SCSI_CONFIG2,
					     CFG2_WIDEFIFO);
				dprintkdbg(DBG_PIO, "DOP1: PIO %i bytes from %p:",
					  pSRB->SRBTotalXferLength,
					  pSRB->virt_addr);
				while (pSRB->SRBTotalXferLength) {
					if (debug_enabled(DBG_PIO))
						printk(" %02x", (unsigned char) *(pSRB->virt_addr));
					DC395x_write8
					    (TRM_S1040_SCSI_FIFO,
					     *(pSRB->virt_addr)++);
					pSRB->SRBTotalXferLength--;
					pSRB->SegmentX[pSRB->SRBSGIndex].
					    length--;
					if (pSRB->SRBTotalXferLength
					    && !pSRB->SegmentX[pSRB->
							       SRBSGIndex].
					    length) {
						if (debug_enabled(DBG_PIO))
							printk(" (next segment)");
						pSRB->SRBSGIndex++;
						DC395x_update_SGlist(pSRB,
								     pSRB->
								     SRBTotalXferLength);
					}
				}
				if (pSRB->pSRBDCB->SyncPeriod & WIDE_SYNC) {
					if (ln % 2) {
						DC395x_write8(TRM_S1040_SCSI_FIFO, 0);
						if (debug_enabled(DBG_PIO))
							printk(" |00");
					}
					DC395x_write8
					    (TRM_S1040_SCSI_CONFIG2, 0);
				}
				/*DC395x_write32(TRM_S1040_SCSI_COUNTER, ln); */
				if (debug_enabled(DBG_PIO))
					printk("\n");
				DC395x_write8(TRM_S1040_SCSI_COMMAND,
						  SCMD_FIFO_OUT);
			}
		}
#endif				/* DC395x_LASTPIO */
		else {		/* xfer pad */

			u8 data = 0, data2 = 0;
			if (pSRB->SRBSGCount) {
				pSRB->AdaptStatus = H_OVER_UNDER_RUN;
				pSRB->SRBStatus |= OVER_RUN;
			}
			/*
			 * KG: despite the fact that we are using 16 bits I/O ops
			 * the SCSI FIFO is only 8 bits according to the docs
			 * (we can set bit 1 in 0x8f to serialize FIFO access ...)
			 */
			if (pDCB->SyncPeriod & WIDE_SYNC) {
				DC395x_write32(TRM_S1040_SCSI_COUNTER, 2);
				DC395x_write8(TRM_S1040_SCSI_CONFIG2,
					      CFG2_WIDEFIFO);
				if (ioDir & DMACMD_DIR) {	/* read */
					data =
					    DC395x_read8
					    (TRM_S1040_SCSI_FIFO);
					data2 =
					    DC395x_read8
					    (TRM_S1040_SCSI_FIFO);
					/*dprintkl(KERN_DEBUG, "DataIO: Xfer pad: %02x %02x\n", data, data2); */
				} else {
					/* Danger, Robinson: If you find KGs scattered over the wide
					 * disk, the driver or chip is to blame :-( */
					DC395x_write8(TRM_S1040_SCSI_FIFO,
						      'K');
					DC395x_write8(TRM_S1040_SCSI_FIFO,
						      'G');
				}
				DC395x_write8(TRM_S1040_SCSI_CONFIG2, 0);
			} else {
				DC395x_write32(TRM_S1040_SCSI_COUNTER, 1);
				/* Danger, Robinson: If you find a collection of Ks on your disk
				 * something broke :-( */
				if (ioDir & DMACMD_DIR) {	/* read */
					data =
					    DC395x_read8
					    (TRM_S1040_SCSI_FIFO);
					/*dprintkl(KERN_DEBUG, "DataIO: Xfer pad: %02x\n", data); */
				} else {
					DC395x_write8(TRM_S1040_SCSI_FIFO,
						      'K');
				}
			}
			pSRB->SRBState |= SRB_XFERPAD;
			DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
			/*
			 * SCSI command 
			 */
			bval =
			    (ioDir & DMACMD_DIR) ? SCMD_FIFO_IN :
			    SCMD_FIFO_OUT;
			DC395x_write8(TRM_S1040_SCSI_COMMAND, bval);
		}
	}
	/*DC395x_monitor_next_IRQ = 2; */
	/*printk(" done\n"); */
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_StatusPhase0: one of DC395x_SCSI_phase0[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase0[phase]
 *				if phase =3  
 ********************************************************************
 */
static void
DC395x_StatusPhase0(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		    u16 * pscsi_status)
{
	dprintkdbg(DBG_0, "StatusPhase0 (pid %li)\n", pSRB->pcmd->pid);
	TRACEPRINTF("STP0 *");
	pSRB->TargetStatus = DC395x_read8(TRM_S1040_SCSI_FIFO);
	pSRB->EndMessage = DC395x_read8(TRM_S1040_SCSI_FIFO);	/* get message */
	pSRB->SRBState = SRB_COMPLETED;
	*pscsi_status = PH_BUS_FREE;	/*.. initial phase */
	/*1.25 */
	/*DC395x_clrfifo (pACB, "STP0"); */
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	/*
	 ** SCSI command 
	 */
	DC395x_write8(TRM_S1040_SCSI_COMMAND, SCMD_MSGACCEPT);
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_StatusPhase1: one of DC395x_SCSI_phase1[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase1[phase]
 *				if phase =3 
 ********************************************************************
 */
static void
DC395x_StatusPhase1(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		    u16 * pscsi_status)
{
	dprintkdbg(DBG_0, "StatusPhase1 (pid=%li)\n", pSRB->pcmd->pid);
	TRACEPRINTF("STP1 *");
	/* Cleanup is now done at the end of DataXXPhase0 */
	/*DC395x_cleanup_after_transfer (pACB, pSRB); */

	pSRB->SRBState = SRB_STATUS;
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	/*
	 * SCSI command 
	 */
	DC395x_write8(TRM_S1040_SCSI_COMMAND, SCMD_COMP);
}

/* Message handling */

#if 0
/* Print received message */
static void DC395x_printMsg(u8 * MsgBuf, u32 len)
{
	int i;
	printk(" %02x", MsgBuf[0]);
	for (i = 1; i < len; i++)
		printk(" %02x", MsgBuf[i]);
	printk("\n");
}
#endif

/* Check if the message is complete */
static inline u8 DC395x_MsgIn_complete(u8 * msgbuf, u32 len)
{
	if (*msgbuf == EXTENDED_MESSAGE) {
		if (len < 2)
			return 0;
		if (len < msgbuf[1] + 2)
			return 0;
	} else if (*msgbuf >= 0x20 && *msgbuf <= 0x2f)	/* two byte messages */
		if (len < 2)
			return 0;
	return 1;
}

#define DC395x_ENABLE_MSGOUT \
 DC395x_write16 (TRM_S1040_SCSI_CONTROL, DO_SETATN); \
 pSRB->SRBState |= SRB_MSGOUT


/* reject_msg */
static inline void
DC395x_MsgIn_reject(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB)
{
	pSRB->MsgOutBuf[0] = MESSAGE_REJECT;
	pSRB->MsgCnt = 1;
	DC395x_ENABLE_MSGOUT;
	pSRB->SRBState &= ~SRB_MSGIN;
	pSRB->SRBState |= SRB_MSGOUT;
	dprintkl(KERN_INFO,
	       "Reject message %02x from %02i-%i\n", pSRB->MsgInBuf[0],
	       pSRB->pSRBDCB->TargetID, pSRB->pSRBDCB->TargetLUN);
	TRACEPRINTF("\\*");
}


/* abort command */
static inline void
DC395x_EnableMsgOut_Abort(struct AdapterCtlBlk *pACB,
			  struct ScsiReqBlk *pSRB)
{
	pSRB->MsgOutBuf[0] = ABORT;
	pSRB->MsgCnt = 1;
	DC395x_ENABLE_MSGOUT;
	pSRB->SRBState &= ~SRB_MSGIN;
	pSRB->SRBState |= SRB_MSGOUT;
	/*
	   if (pSRB->pSRBDCB)
	   pSRB->pSRBDCB->DCBFlag &= ~ABORT_DEV_;
	 */
	TRACEPRINTF("#*");
}


static struct ScsiReqBlk *DC395x_MsgIn_QTag(struct AdapterCtlBlk *pACB,
					    struct DeviceCtlBlk *pDCB,
					    u8 tag)
{
	struct ScsiReqBlk *lastSRB = pDCB->pGoingLast;
	struct ScsiReqBlk *pSRB = pDCB->pGoingSRB;
	dprintkdbg(DBG_0, "QTag Msg (SRB %p): %i\n", pSRB, tag);
	if (!(pDCB->TagMask & (1 << tag)))
		dprintkl(KERN_DEBUG,
		       "MsgIn_QTag: TagMask (%08x) does not reserve tag %i!\n",
		       pDCB->TagMask, tag);

	if (!pSRB)
		goto mingx0;
	while (pSRB) {
		if (pSRB->TagNumber == tag)
			break;
		if (pSRB == lastSRB)
			goto mingx0;
		pSRB = pSRB->pNextSRB;
	}
	dprintkdbg(DBG_0, "pid %li (%i-%i)\n", pSRB->pcmd->pid,
	       pSRB->pSRBDCB->TargetID, pSRB->pSRBDCB->TargetLUN);
	if (pDCB->DCBFlag & ABORT_DEV_) {
		/*pSRB->SRBState = SRB_ABORT_SENT; */
		DC395x_EnableMsgOut_Abort(pACB, pSRB);
	}

	if (!(pSRB->SRBState & SRB_DISCONNECT))
		goto mingx0;

	/* Tag found */
	TRACEPRINTF("[%s]*", pDCB->pActiveSRB->debugtrace);
	TRACEPRINTF("RTag*");
	/* Just for debugging ... */
	lastSRB = pSRB;
	pSRB = pDCB->pActiveSRB;
	TRACEPRINTF("Found.*");
	pSRB = lastSRB;

	memcpy(pSRB->MsgInBuf, pDCB->pActiveSRB->MsgInBuf, pACB->MsgLen);
	pSRB->SRBState |= pDCB->pActiveSRB->SRBState;
	pSRB->SRBState |= SRB_DATA_XFER;
	pDCB->pActiveSRB = pSRB;
	/* How can we make the DORS happy? */
	return pSRB;

      mingx0:
	pSRB = pACB->pTmpSRB;
	pSRB->SRBState = SRB_UNEXPECT_RESEL;
	pDCB->pActiveSRB = pSRB;
	pSRB->MsgOutBuf[0] = MSG_ABORT_TAG;
	pSRB->MsgCnt = 1;
	DC395x_ENABLE_MSGOUT;
	TRACEPRINTF("?*");
	dprintkl(KERN_DEBUG, "Unknown tag received: %i: abort !!\n", tag);
	return pSRB;
}


/* Reprogram registers */
static inline void
DC395x_reprog(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB)
{
	DC395x_write8(TRM_S1040_SCSI_TARGETID, pDCB->TargetID);
	DC395x_write8(TRM_S1040_SCSI_SYNC, pDCB->SyncPeriod);
	DC395x_write8(TRM_S1040_SCSI_OFFSET, pDCB->SyncOffset);
	DC395x_SetXferRate(pACB, pDCB);
}


/* set async transfer mode */
static void
DC395x_MsgIn_set_async(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB)
{
	struct DeviceCtlBlk *pDCB = pSRB->pSRBDCB;
	dprintkl(KERN_DEBUG, "Target %02i: No sync transfers\n", pDCB->TargetID);
	TRACEPRINTF("!S *");
	pDCB->SyncMode &= ~(SYNC_NEGO_ENABLE);
	pDCB->SyncMode |= SYNC_NEGO_DONE;
	/*pDCB->SyncPeriod &= 0; */
	pDCB->SyncOffset = 0;
	pDCB->MinNegoPeriod = 200 >> 2;	/* 200ns <=> 5 MHz */
	pSRB->SRBState &= ~SRB_DO_SYNC_NEGO;
	DC395x_reprog(pACB, pDCB);
	if ((pDCB->SyncMode & WIDE_NEGO_ENABLE)
	    && !(pDCB->SyncMode & WIDE_NEGO_DONE)) {
		DC395x_Build_WDTR(pACB, pDCB, pSRB);
		DC395x_ENABLE_MSGOUT;
		dprintkdbg(DBG_0, "SDTR(rej): Try WDTR anyway ...\n");
	}
}


/* set sync transfer mode */
static void
DC395x_MsgIn_set_sync(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB)
{
	u8 bval;
	int fact;
	struct DeviceCtlBlk *pDCB = pSRB->pSRBDCB;
	/*u8 oldsyncperiod = pDCB->SyncPeriod; */
	/*u8 oldsyncoffset = pDCB->SyncOffset; */

	dprintkdbg(DBG_1, "Target %02i: Sync: %ins (%02i.%01i MHz) Offset %i\n",
	       pDCB->TargetID, pSRB->MsgInBuf[3] << 2,
	       (250 / pSRB->MsgInBuf[3]),
	       ((250 % pSRB->MsgInBuf[3]) * 10) / pSRB->MsgInBuf[3],
	       pSRB->MsgInBuf[4]);

	if (pSRB->MsgInBuf[4] > 15)
		pSRB->MsgInBuf[4] = 15;
	if (!(pDCB->DevMode & NTC_DO_SYNC_NEGO))
		pDCB->SyncOffset = 0;
	else if (pDCB->SyncOffset == 0)
		pDCB->SyncOffset = pSRB->MsgInBuf[4];
	if (pSRB->MsgInBuf[4] > pDCB->SyncOffset)
		pSRB->MsgInBuf[4] = pDCB->SyncOffset;
	else
		pDCB->SyncOffset = pSRB->MsgInBuf[4];
	bval = 0;
	while (bval < 7 && (pSRB->MsgInBuf[3] > dc395x_clock_period[bval]
			    || pDCB->MinNegoPeriod >
			    dc395x_clock_period[bval]))
		bval++;
	if (pSRB->MsgInBuf[3] < dc395x_clock_period[bval])
		dprintkl(KERN_INFO,
		       "Increase sync nego period to %ins\n",
		       dc395x_clock_period[bval] << 2);
	pSRB->MsgInBuf[3] = dc395x_clock_period[bval];
	pDCB->SyncPeriod &= 0xf0;
	pDCB->SyncPeriod |= ALT_SYNC | bval;
	pDCB->MinNegoPeriod = pSRB->MsgInBuf[3];

	if (pDCB->SyncPeriod & WIDE_SYNC)
		fact = 500;
	else
		fact = 250;

	dprintkl(KERN_INFO,
	       "Target %02i: %s Sync: %ins Offset %i (%02i.%01i MB/s)\n",
	       pDCB->TargetID, (fact == 500) ? "Wide16" : "",
	       pDCB->MinNegoPeriod << 2, pDCB->SyncOffset,
	       (fact / pDCB->MinNegoPeriod),
	       ((fact % pDCB->MinNegoPeriod) * 10 +
		pDCB->MinNegoPeriod / 2) / pDCB->MinNegoPeriod);

	TRACEPRINTF("S%i *", pDCB->MinNegoPeriod << 2);
	if (!(pSRB->SRBState & SRB_DO_SYNC_NEGO)) {
		/* Reply with corrected SDTR Message */
		dprintkl(KERN_DEBUG, " .. answer w/  %ins %i\n",
		       pSRB->MsgInBuf[3] << 2, pSRB->MsgInBuf[4]);

		memcpy(pSRB->MsgOutBuf, pSRB->MsgInBuf, 5);
		pSRB->MsgCnt = 5;
		DC395x_ENABLE_MSGOUT;
		pDCB->SyncMode |= SYNC_NEGO_DONE;
	} else {
		if ((pDCB->SyncMode & WIDE_NEGO_ENABLE)
		    && !(pDCB->SyncMode & WIDE_NEGO_DONE)) {
			DC395x_Build_WDTR(pACB, pDCB, pSRB);
			DC395x_ENABLE_MSGOUT;
			dprintkdbg(DBG_0, "SDTR: Also try WDTR ...\n");
		}
	}
	pSRB->SRBState &= ~SRB_DO_SYNC_NEGO;
	pDCB->SyncMode |= SYNC_NEGO_DONE | SYNC_NEGO_ENABLE;

	DC395x_reprog(pACB, pDCB);
}


static inline void
DC395x_MsgIn_set_nowide(struct AdapterCtlBlk *pACB,
			struct ScsiReqBlk *pSRB)
{
	struct DeviceCtlBlk *pDCB = pSRB->pSRBDCB;
	dprintkdbg(DBG_KG, "WDTR got rejected from target %02i\n",
	       pDCB->TargetID);
	TRACEPRINTF("!W *");
	pDCB->SyncPeriod &= ~WIDE_SYNC;
	pDCB->SyncMode &= ~(WIDE_NEGO_ENABLE);
	pDCB->SyncMode |= WIDE_NEGO_DONE;
	pSRB->SRBState &= ~SRB_DO_WIDE_NEGO;
	DC395x_reprog(pACB, pDCB);
	if ((pDCB->SyncMode & SYNC_NEGO_ENABLE)
	    && !(pDCB->SyncMode & SYNC_NEGO_DONE)) {
		DC395x_Build_SDTR(pACB, pDCB, pSRB);
		DC395x_ENABLE_MSGOUT;
		dprintkdbg(DBG_0, "WDTR(rej): Try SDTR anyway ...\n");
	}
}

static void
DC395x_MsgIn_set_wide(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB)
{
	struct DeviceCtlBlk *pDCB = pSRB->pSRBDCB;
	u8 wide = (pDCB->DevMode & NTC_DO_WIDE_NEGO
		   && pACB->Config & HCC_WIDE_CARD) ? 1 : 0;
	if (pSRB->MsgInBuf[3] > wide)
		pSRB->MsgInBuf[3] = wide;
	/* Completed */
	if (!(pSRB->SRBState & SRB_DO_WIDE_NEGO)) {
		dprintkl(KERN_DEBUG,
		       "Target %02i initiates Wide Nego ...\n",
		       pDCB->TargetID);
		memcpy(pSRB->MsgOutBuf, pSRB->MsgInBuf, 4);
		pSRB->MsgCnt = 4;
		pSRB->SRBState |= SRB_DO_WIDE_NEGO;
		DC395x_ENABLE_MSGOUT;
	}

	pDCB->SyncMode |= (WIDE_NEGO_ENABLE | WIDE_NEGO_DONE);
	if (pSRB->MsgInBuf[3] > 0)
		pDCB->SyncPeriod |= WIDE_SYNC;
	else
		pDCB->SyncPeriod &= ~WIDE_SYNC;
	pSRB->SRBState &= ~SRB_DO_WIDE_NEGO;
	TRACEPRINTF("W%i *", (pDCB->SyncPeriod & WIDE_SYNC ? 1 : 0));
	/*pDCB->SyncMode &= ~(WIDE_NEGO_ENABLE+WIDE_NEGO_DONE); */
	dprintkdbg(DBG_KG,
	       "Wide transfers (%i bit) negotiated with target %02i\n",
	       (8 << pSRB->MsgInBuf[3]), pDCB->TargetID);
	DC395x_reprog(pACB, pDCB);
	if ((pDCB->SyncMode & SYNC_NEGO_ENABLE)
	    && !(pDCB->SyncMode & SYNC_NEGO_DONE)) {
		DC395x_Build_SDTR(pACB, pDCB, pSRB);
		DC395x_ENABLE_MSGOUT;
		dprintkdbg(DBG_0, "WDTR: Also try SDTR ...\n");
	}
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_MsgInPhase0: one of DC395x_SCSI_phase0[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase0[phase]
 *				if phase =7   
 *
 * extended message codes:
 *
 *	code	description
 *
 *	02h	Reserved
 *	00h	MODIFY DATA  POINTER
 *	01h	SYNCHRONOUS DATA TRANSFER REQUEST
 *	03h	WIDE DATA TRANSFER REQUEST
 *   04h - 7Fh	Reserved
 *   80h - FFh	Vendor specific
 *  
 ********************************************************************
 */
void
DC395x_MsgInPhase0(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		   u16 * pscsi_status)
{
	struct DeviceCtlBlk *pDCB;

	dprintkdbg(DBG_0, "DC395x_MsgInPhase0..............\n");
	TRACEPRINTF("MIP0*");
	pDCB = pACB->pActiveDCB;

	pSRB->MsgInBuf[pACB->MsgLen++] = DC395x_read8(TRM_S1040_SCSI_FIFO);
	if (DC395x_MsgIn_complete(pSRB->MsgInBuf, pACB->MsgLen)) {
		TRACEPRINTF("(%02x)*", pSRB->MsgInBuf[0]);
		/*dprintkl(KERN_INFO, "MsgIn:"); */
		/*DC395x_printMsg (pSRB->MsgInBuf, pACB->MsgLen); */

		/* Now eval the msg */
		switch (pSRB->MsgInBuf[0]) {
		case DISCONNECT:
			pSRB->SRBState = SRB_DISCONNECT;
			break;

		case SIMPLE_QUEUE_TAG:
		case HEAD_OF_QUEUE_TAG:
		case ORDERED_QUEUE_TAG:
			TRACEPRINTF("(%02x)*", pSRB->MsgInBuf[1]);
			pSRB =
			    DC395x_MsgIn_QTag(pACB, pDCB,
					      pSRB->MsgInBuf[1]);
			break;

		case MESSAGE_REJECT:
			DC395x_write16(TRM_S1040_SCSI_CONTROL,
				       DO_CLRATN | DO_DATALATCH);
			/* A sync nego message was rejected ! */
			if (pSRB->SRBState & SRB_DO_SYNC_NEGO) {
				DC395x_MsgIn_set_async(pACB, pSRB);
				break;
			}
			/* A wide nego message was rejected ! */
			if (pSRB->SRBState & SRB_DO_WIDE_NEGO) {
				DC395x_MsgIn_set_nowide(pACB, pSRB);
				break;
			}
			DC395x_EnableMsgOut_Abort(pACB, pSRB);
			/*pSRB->SRBState |= SRB_ABORT_SENT */
			break;

		case EXTENDED_MESSAGE:
			TRACEPRINTF("(%02x)*", pSRB->MsgInBuf[2]);
			/* SDTR */
			if (pSRB->MsgInBuf[1] == 3
			    && pSRB->MsgInBuf[2] == EXTENDED_SDTR) {
				DC395x_MsgIn_set_sync(pACB, pSRB);
				break;
			}
			/* WDTR */
			if (pSRB->MsgInBuf[1] == 2 && pSRB->MsgInBuf[2] == EXTENDED_WDTR && pSRB->MsgInBuf[3] <= 2) {	/* sanity check ... */
				DC395x_MsgIn_set_wide(pACB, pSRB);
				break;
			}
			DC395x_MsgIn_reject(pACB, pSRB);
			break;

			/* Discard  wide residual */
		case MSG_IGNOREWIDE:
			dprintkdbg(DBG_0, "Ignore Wide Residual!\n");
			/*DC395x_write32 (TRM_S1040_SCSI_COUNTER, 1); */
			/*DC395x_read8 (TRM_S1040_SCSI_FIFO); */
			break;

			/* nothing has to be done */
		case COMMAND_COMPLETE:
			break;

			/*
			 * SAVE POINTER may be ignored as we have the struct ScsiReqBlk* associated with the
			 * scsi command. Thanks, Gérard, for pointing it out.
			 */
		case SAVE_POINTERS:
			dprintkdbg(DBG_0, "SAVE POINTER message received (pid %li: rem.%i) ... ignore :-(\n",
			       pSRB->pcmd->pid, pSRB->SRBTotalXferLength);
			/*pSRB->Saved_Ptr = pSRB->TotalXferredLen; */
			break;
			/* The device might want to restart transfer with a RESTORE */
		case RESTORE_POINTERS:
			dprintkl(KERN_DEBUG,
			       "RESTORE POINTER message received ... ignore :-(\n");
			/*dc395x_restore_ptr (pACB, pSRB); */
			break;
		case ABORT:
			dprintkl(KERN_DEBUG,
			       "ABORT msg received (pid %li %02i-%i)\n",
			       pSRB->pcmd->pid, pDCB->TargetID,
			       pDCB->TargetLUN);
			pDCB->DCBFlag |= ABORT_DEV_;
			DC395x_EnableMsgOut_Abort(pACB, pSRB);
			break;
			/* reject unknown messages */
		default:
			if (pSRB->MsgInBuf[0] & IDENTIFY_BASE) {
				dprintkl(KERN_DEBUG, "Identify Message received?\n");
				/*TRACEOUT (" %s\n", pSRB->debugtrace); */
				pSRB->MsgCnt = 1;
				pSRB->MsgOutBuf[0] = pDCB->IdentifyMsg;
				DC395x_ENABLE_MSGOUT;
				pSRB->SRBState |= SRB_MSGOUT;
				/*break; */
			}
			DC395x_MsgIn_reject(pACB, pSRB);
			TRACEOUT(" %s\n", pSRB->debugtrace);
		}
		TRACEPRINTF(".*");

		/* Clear counter and MsgIn state */
		pSRB->SRBState &= ~SRB_MSGIN;
		pACB->MsgLen = 0;
	}

	/*1.25 */
	if ((*pscsi_status & PHASEMASK) != PH_MSG_IN)
#if 0
		DC395x_clrfifo(pACB, "MIP0_");
#else
		TRACEPRINTF("N/Cln *");
#endif
	*pscsi_status = PH_BUS_FREE;
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important ... you know! */
	DC395x_write8(TRM_S1040_SCSI_COMMAND, SCMD_MSGACCEPT);
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_MsgInPhase1: one of DC395x_SCSI_phase1[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase1[phase]
 *				if phase =7	   
 ********************************************************************
 */
static void
DC395x_MsgInPhase1(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
		   u16 * pscsi_status)
{
	dprintkdbg(DBG_0, "DC395x_MsgInPhase1..............\n");
	TRACEPRINTF("MIP1 *");
	DC395x_clrfifo(pACB, "MIP1");
	DC395x_write32(TRM_S1040_SCSI_COUNTER, 1);
	if (!(pSRB->SRBState & SRB_MSGIN)) {
		pSRB->SRBState &= ~SRB_DISCONNECT;
		pSRB->SRBState |= SRB_MSGIN;
	}
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	/*
	 * SCSI command 
	 */
	DC395x_write8(TRM_S1040_SCSI_COMMAND, SCMD_FIFO_IN);
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_Nop0: one of DC395x_SCSI_phase1[] ,DC395x_SCSI_phase0[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase0[phase]
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase1[phase]
 *				if phase =4 ..PH_BUS_FREE
 ********************************************************************
 */
static void
DC395x_Nop0(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
	    u16 * pscsi_status)
{
	/*TRACEPRINTF("NOP0 *"); */
}


/*
 ********************************************************************
 * scsiio
 *	DC395x_Nop1: one of DC395x_SCSI_phase0[] ,DC395x_SCSI_phase1[] vectors
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase0[phase]
 *	 DC395x_stateV = (void *)DC395x_SCSI_phase1[phase]
 *				if phase =5
 ********************************************************************
 */
static void
DC395x_Nop1(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB,
	    u16 * pscsi_status)
{
	/*TRACEPRINTF("NOP1 *"); */
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_MsgInPhase0
 ********************************************************************
 */
static void
DC395x_SetXferRate(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB)
{
	u8 bval;
	u16 cnt, i;
	struct DeviceCtlBlk *pDCBTemp;

	/*
	 ** set all lun device's  period , offset
	 */
	if (!(pDCB->IdentifyMsg & 0x07)) {
		if (pACB->scan_devices)
			DC395x_CurrSyncOffset = pDCB->SyncOffset;
		else {
			pDCBTemp = pACB->pLinkDCB;
			cnt = pACB->DCBCnt;
			bval = pDCB->TargetID;
			for (i = 0; i < cnt; i++) {
				if (pDCBTemp->TargetID == bval) {
					pDCBTemp->SyncPeriod =
					    pDCB->SyncPeriod;
					pDCBTemp->SyncOffset =
					    pDCB->SyncOffset;
					pDCBTemp->SyncMode =
					    pDCB->SyncMode;
					pDCBTemp->MinNegoPeriod =
					    pDCB->MinNegoPeriod;
				}
				pDCBTemp = pDCBTemp->pNextDCB;
			}
		}
	}
	return;
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_Interrupt
 ********************************************************************
 */
void DC395x_Disconnect(struct AdapterCtlBlk *pACB)
{
	struct DeviceCtlBlk *pDCB;
	struct ScsiReqBlk *pSRB;

	dprintkdbg(DBG_0, "Disconnect (pid=%li)\n", pACB->pActiveDCB->pActiveSRB->pcmd->pid);
	pDCB = pACB->pActiveDCB;
	if (!pDCB) {
		dprintkl(KERN_ERR, "Disc: Exception Disconnect pDCB=NULL !!\n ");
		udelay(500);
		/* Suspend queue for a while */
		pACB->pScsiHost->last_reset =
		    jiffies + HZ / 2 +
		    HZ *
		    dc395x_trm_eepromBuf[pACB->AdapterIndex].
		    NvramDelayTime;
		DC395x_clrfifo(pACB, "DiscEx");
		DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_HWRESELECT);
		return;
	}
	pSRB = pDCB->pActiveSRB;
	pACB->pActiveDCB = 0;
	TRACEPRINTF("DISC *");

	pSRB->ScsiPhase = PH_BUS_FREE;	/* initial phase */
	DC395x_clrfifo(pACB, "Disc");
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_HWRESELECT);
	if (pSRB->SRBState & SRB_UNEXPECT_RESEL) {
		dprintkl(KERN_ERR, "Disc: Unexpected Reselection (%i-%i)\n",
		       pDCB->TargetID, pDCB->TargetLUN);
		pSRB->SRBState = 0;
		DC395x_Waiting_process(pACB);
	} else if (pSRB->SRBState & SRB_ABORT_SENT) {
		/*Scsi_Cmnd* pcmd = pSRB->pcmd; */
		pDCB->DCBFlag &= ~ABORT_DEV_;
		pACB->pScsiHost->last_reset = jiffies + HZ / 2 + 1;
		dprintkl(KERN_ERR, "Disc: SRB_ABORT_SENT!\n");
		DC395x_DoingSRB_Done(pACB, DID_ABORT, pSRB->pcmd, 1);
		DC395x_Query_to_Waiting(pACB);
		DC395x_Waiting_process(pACB);
	} else {
		if ((pSRB->SRBState & (SRB_START_ + SRB_MSGOUT))
		    || !(pSRB->
			 SRBState & (SRB_DISCONNECT + SRB_COMPLETED))) {
			/*
			 * Selection time out 
			 * SRB_START_ || SRB_MSGOUT || (!SRB_DISCONNECT && !SRB_COMPLETED)
			 */
			/* Unexp. Disc / Sel Timeout */
			if (pSRB->SRBState != SRB_START_
			    && pSRB->SRBState != SRB_MSGOUT) {
				pSRB->SRBState = SRB_READY;
				dprintkl(KERN_DEBUG, "Unexpected Disconnection (pid %li)!\n",
				       pSRB->pcmd->pid);
				pSRB->TargetStatus = SCSI_STAT_SEL_TIMEOUT;
				TRACEPRINTF("UnExpD *");
				TRACEOUT("%s\n", pSRB->debugtrace);
				goto disc1;
			} else {
				/* Normal selection timeout */
				TRACEPRINTF("SlTO *");
				dprintkdbg(DBG_KG,
				       "Disc: SelTO (pid=%li) for dev %02i-%i\n",
				       pSRB->pcmd->pid, pDCB->TargetID,
				       pDCB->TargetLUN);
				if (pSRB->RetryCnt++ > DC395x_MAX_RETRIES
				    || pACB->scan_devices) {
					pSRB->TargetStatus =
					    SCSI_STAT_SEL_TIMEOUT;
					goto disc1;
				}
				DC395x_freetag(pDCB, pSRB);
				DC395x_Going_to_Waiting(pDCB, pSRB);
				dprintkdbg(DBG_KG, "Retry pid %li ...\n",
				       pSRB->pcmd->pid);
				DC395x_waiting_timer(pACB, HZ / 20);
			}
		} else if (pSRB->SRBState & SRB_DISCONNECT) {
			u8 bval = DC395x_read8(TRM_S1040_SCSI_SIGNAL);
			/*
			 * SRB_DISCONNECT (This is what we expect!)
			 */
			/* dprintkl(KERN_DEBUG, "DoWaitingSRB (pid=%li)\n", pSRB->pcmd->pid); */
			TRACEPRINTF("+*");
			if (bval & 0x40) {
				dprintkdbg(DBG_0, "Debug: DISC: SCSI bus stat %02x: ACK set! Other controllers?\n",
					bval);
				/* It could come from another initiator, therefore don't do much ! */
				TRACEPRINTF("ACK(%02x) *", bval);
				/*DC395x_dumpinfo (pACB, pDCB, pSRB); */
				/*TRACEOUT (" %s\n", pSRB->debugtrace); */
				/*pDCB->DCBFlag |= ABORT_DEV_; */
				/*DC395x_EnableMsgOut_Abort (pACB, pSRB); */
				/*DC395x_write16 (TRM_S1040_SCSI_CONTROL, DO_CLRFIFO | DO_CLRATN | DO_HWRESELECT); */
			} else
				DC395x_Waiting_process(pACB);
		} else if (pSRB->SRBState & SRB_COMPLETED) {
		      disc1:
			/*
			 ** SRB_COMPLETED
			 */
			DC395x_freetag(pDCB, pSRB);
			pDCB->pActiveSRB = 0;
			pSRB->SRBState = SRB_FREE;
			/*dprintkl(KERN_DEBUG, "done (pid=%li)\n", pSRB->pcmd->pid); */
			DC395x_SRBdone(pACB, pDCB, pSRB);
		}
	}
	return;
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_Reselect
 ********************************************************************
 */
void DC395x_Reselect(struct AdapterCtlBlk *pACB)
{
	struct DeviceCtlBlk *pDCB;
	struct ScsiReqBlk *pSRB = 0;
	u16 RselTarLunId;
	u8 id, lun;
	u8 arblostflag = 0;

	dprintkdbg(DBG_0, "DC395x_Reselect..............\n");

	DC395x_clrfifo(pACB, "Resel");
	/*DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_HWRESELECT | DO_DATALATCH); */
	/* Read Reselected Target ID and LUN */
	RselTarLunId = DC395x_read16(TRM_S1040_SCSI_TARGETID);
	pDCB = pACB->pActiveDCB;
	if (pDCB) {		/* Arbitration lost but Reselection win */
		pSRB = pDCB->pActiveSRB;
		if (!pSRB) {
			dprintkl(KERN_DEBUG, "Arb lost Resel won, but pActiveSRB == 0!\n");
			DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
			return;
		}
		/* Why the if ? */
		if (!(pACB->scan_devices)) {
			dprintkdbg(DBG_KG,
			       "Arb lost but Resel win pid %li (%02i-%i) Rsel %04x Stat %04x\n",
			       pSRB->pcmd->pid, pDCB->TargetID,
			       pDCB->TargetLUN, RselTarLunId,
			       DC395x_read16(TRM_S1040_SCSI_STATUS));
			TRACEPRINTF("ArbLResel!*");
			/*TRACEOUT (" %s\n", pSRB->debugtrace); */
			arblostflag = 1;
			/*pSRB->SRBState |= SRB_DISCONNECT; */

			pSRB->SRBState = SRB_READY;
			DC395x_freetag(pDCB, pSRB);
			DC395x_Going_to_Waiting(pDCB, pSRB);
			DC395x_waiting_timer(pACB, HZ / 20);

			/* return; */
		}
	}
	/* Read Reselected Target Id and LUN */
	if (!(RselTarLunId & (IDENTIFY_BASE << 8)))
		dprintkl(KERN_DEBUG, "Resel expects identify msg! Got %04x!\n",
		       RselTarLunId);
	id = RselTarLunId & 0xff;
	lun = (RselTarLunId >> 8) & 7;
	pDCB = DC395x_findDCB(pACB, id, lun);
	if (!pDCB) {
		dprintkl(KERN_ERR, "Reselect from non existing device (%02i-%i)\n",
		       id, lun);
		DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
		return;
	}

	pACB->pActiveDCB = pDCB;

	if (!(pDCB->DevMode & NTC_DO_DISCONNECT))
		dprintkl(KERN_DEBUG, "Reselection in spite of forbidden disconnection? (%02i-%i)\n",
		       pDCB->TargetID, pDCB->TargetLUN);

	if ((pDCB->SyncMode & EN_TAG_QUEUEING) /*&& !arblostflag */ ) {
		struct ScsiReqBlk *oldSRB = pSRB;
		pSRB = pACB->pTmpSRB;
#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
		pSRB->debugpos = 0;
		pSRB->debugtrace[0] = 0;
#endif
		pDCB->pActiveSRB = pSRB;
		if (oldSRB)
			TRACEPRINTF("ArbLResel(%li):*", oldSRB->pcmd->pid);
		/*if (arblostflag) dprintkl(KERN_DEBUG, "Reselect: Wait for Tag ... \n"); */
	} else {
		/* There can be only one! */
		pSRB = pDCB->pActiveSRB;
		if (pSRB)
			TRACEPRINTF("RSel *");
		if (!pSRB || !(pSRB->SRBState & SRB_DISCONNECT)) {
			/*
			 * abort command
			 */
			dprintkl(KERN_DEBUG,
			       "Reselected w/o disconnected cmds from %02i-%i?\n",
			       pDCB->TargetID, pDCB->TargetLUN);
			pSRB = pACB->pTmpSRB;
			pSRB->SRBState = SRB_UNEXPECT_RESEL;
			pDCB->pActiveSRB = pSRB;
			DC395x_EnableMsgOut_Abort(pACB, pSRB);
		} else {
			if (pDCB->DCBFlag & ABORT_DEV_) {
				/*pSRB->SRBState = SRB_ABORT_SENT; */
				DC395x_EnableMsgOut_Abort(pACB, pSRB);
			} else
				pSRB->SRBState = SRB_DATA_XFER;

		}
		/*if (arblostflag) TRACEOUT (" %s\n", pSRB->debugtrace); */
	}
	pSRB->ScsiPhase = PH_BUS_FREE;	/* initial phase */
	/* 
	 ***********************************************
	 ** Program HA ID, target ID, period and offset
	 ***********************************************
	 */
	DC395x_write8(TRM_S1040_SCSI_HOSTID, pACB->pScsiHost->this_id);	/* host   ID */
	DC395x_write8(TRM_S1040_SCSI_TARGETID, pDCB->TargetID);	/* target ID */
	DC395x_write8(TRM_S1040_SCSI_OFFSET, pDCB->SyncOffset);	/* offset    */
	DC395x_write8(TRM_S1040_SCSI_SYNC, pDCB->SyncPeriod);	/* sync period, wide */
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_DATALATCH);	/* it's important for atn stop */
	/* SCSI command */
	DC395x_write8(TRM_S1040_SCSI_COMMAND, SCMD_MSGACCEPT);
}


/* Dynamic device handling */

/* Remove dev (and DCB) */
static void
DC395x_remove_dev(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB)
{
	struct DeviceCtlBlk *pPrevDCB = pACB->pLinkDCB;

	dprintkdbg(DBG_0, "remove_dev\n");
	if (pDCB->GoingSRBCnt > 1) {
		dprintkdbg(DBG_DCB, "Driver won't free DCB (ID %i, LUN %i): 0x%08x because of SRBCnt %i\n",
			  pDCB->TargetID, pDCB->TargetLUN, (int) pDCB,
			  pDCB->GoingSRBCnt);
		return;
	}
	pACB->DCBmap[pDCB->TargetID] &= ~(1 << pDCB->TargetLUN);
	pACB->children[pDCB->TargetID][pDCB->TargetLUN] = NULL;

	/* The first one */
	if (pDCB == pACB->pLinkDCB) {
		/* The last one */
		if (pACB->pLastDCB == pDCB) {
			pDCB->pNextDCB = 0;
			pACB->pLastDCB = 0;
		}
		pACB->pLinkDCB = pDCB->pNextDCB;
	} else {
		while (pPrevDCB->pNextDCB != pDCB)
			pPrevDCB = pPrevDCB->pNextDCB;
		pPrevDCB->pNextDCB = pDCB->pNextDCB;
		if (pDCB == pACB->pLastDCB)
			pACB->pLastDCB = pPrevDCB;
	}

	dprintkdbg(DBG_DCB, "Driver about to free DCB (ID %i, LUN %i): %p\n",
		  pDCB->TargetID, pDCB->TargetLUN, pDCB);
	if (pDCB == pACB->pActiveDCB)
		pACB->pActiveDCB = 0;
	if (pDCB == pACB->pLinkDCB)
		pACB->pLinkDCB = pDCB->pNextDCB;
	if (pDCB == pACB->pDCBRunRobin)
		pACB->pDCBRunRobin = pDCB->pNextDCB;
	pACB->DCBCnt--;
	dc395x_kfree(pDCB);
	/* pACB->DeviceCnt--; */
}


static inline u8 DC395x_tagq_blacklist(char *name)
{
#ifndef DC395x_NO_TAGQ
#if 0
	u8 i;
	for (i = 0; i < BADDEVCNT; i++)
		if (memcmp(name, DC395x_baddevname1[i], 28) == 0)
			return 1;
#endif
	return 0;
#else
	return 1;
#endif
}


static void
DC395x_disc_tagq_set(struct DeviceCtlBlk *pDCB, struct ScsiInqData *ptr)
{
	/* Check for SCSI format (ANSI and Response data format) */
	if ((ptr->Vers & 0x07) >= 2 || (ptr->RDF & 0x0F) == 2) {
		if ((ptr->Flags & SCSI_INQ_CMDQUEUE)
		    && (pDCB->DevMode & NTC_DO_TAG_QUEUEING) &&
		    /*(pDCB->DevMode & NTC_DO_DISCONNECT) */
		    /* ((pDCB->DevType == TYPE_DISK) 
		       || (pDCB->DevType == TYPE_MOD)) && */
		    !DC395x_tagq_blacklist(((char *) ptr) + 8)) {
			if (pDCB->MaxCommand == 1)
				pDCB->MaxCommand =
				    pDCB->pDCBACB->TagMaxNum;
			pDCB->SyncMode |= EN_TAG_QUEUEING;
			/*pDCB->TagMask = 0; */
		} else
			pDCB->MaxCommand = 1;
	}
}


static void
DC395x_add_dev(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB,
	       struct ScsiInqData *ptr)
{
	u8 bval1 = ptr->DevType & SCSI_DEVTYPE;
	pDCB->DevType = bval1;
	/* if (bval1 == TYPE_DISK || bval1 == TYPE_MOD) */
	DC395x_disc_tagq_set(pDCB, ptr);
}


/* 
 ********************************************************************
 * unmap mapped pci regions from SRB
 ********************************************************************
 */
static void
DC395x_pci_unmap(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB)
{
	int dir;
	Scsi_Cmnd *pcmd = pSRB->pcmd;
	dir = scsi_to_pci_dma_dir(pcmd->sc_data_direction);
	if (pcmd->use_sg && dir != PCI_DMA_NONE) {
		/* unmap DC395x SG list */
		dprintkdbg(DBG_SGPARANOIA,
		       "Unmap SG descriptor list %08x (%05x)\n",
		       pSRB->SRBSGBusAddr,
		       sizeof(struct SGentry) * DC395x_MAX_SG_LISTENTRY);
		pci_unmap_single(pACB->pdev, pSRB->SRBSGBusAddr,
				 sizeof(struct SGentry) *
				 DC395x_MAX_SG_LISTENTRY,
				 PCI_DMA_TODEVICE);
		dprintkdbg(DBG_SGPARANOIA, "Unmap %i SG segments from %p\n",
		       pcmd->use_sg, pcmd->request_buffer);
		/* unmap the sg segments */
		pci_unmap_sg(pACB->pdev,
			     (struct scatterlist *) pcmd->request_buffer,
			     pcmd->use_sg, dir);
	} else if (pcmd->request_buffer && dir != PCI_DMA_NONE) {
		dprintkdbg(DBG_SGPARANOIA, "Unmap buffer at %08x (%05x)\n",
		       pSRB->SegmentX[0].address, pcmd->request_bufflen);
		pci_unmap_single(pACB->pdev, pSRB->SegmentX[0].address,
				 pcmd->request_bufflen, dir);
	}
}


/* 
 ********************************************************************
 * unmap mapped pci sense buffer from SRB
 ********************************************************************
 */
static void
DC395x_pci_unmap_sense(struct AdapterCtlBlk *pACB, struct ScsiReqBlk *pSRB)
{
	if (!(pSRB->SRBFlag & AUTO_REQSENSE))
		return;
	/* Unmap sense buffer */
	dprintkdbg(DBG_SGPARANOIA, "Unmap sense buffer from %08x\n",
	       pSRB->SegmentX[0].address);
	pci_unmap_single(pACB->pdev, pSRB->SegmentX[0].address,
			 pSRB->SegmentX[0].length, PCI_DMA_FROMDEVICE);
	/* Restore SG stuff */
	/*printk ("Auto_ReqSense finished: Restore Counters ...\n"); */
	pSRB->SRBTotalXferLength = pSRB->Xferred;
	pSRB->SegmentX[0].address =
	    pSRB->SegmentX[DC395x_MAX_SG_LISTENTRY - 1].address;
	pSRB->SegmentX[0].length =
	    pSRB->SegmentX[DC395x_MAX_SG_LISTENTRY - 1].length;
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_Disconnected
 *	Complete execution of a SCSI command
 *	Signal completion to the generic SCSI driver  
 ********************************************************************
 */
void
DC395x_SRBdone(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB,
	       struct ScsiReqBlk *pSRB)
{
	u8 tempcnt, status;
	Scsi_Cmnd *pcmd;
	struct ScsiInqData *ptr;
	/*u32              drv_flags=0; */
	int dir;

	pcmd = pSRB->pcmd;
	TRACEPRINTF("DONE *");

	dir = scsi_to_pci_dma_dir(pcmd->sc_data_direction);
	ptr = (struct ScsiInqData *) (pcmd->request_buffer);
	if (pcmd->use_sg)
		ptr =
		    (struct ScsiInqData *) CPU_ADDR(*(struct scatterlist *)
						    ptr);
	dprintkdbg(DBG_SGPARANOIA, 
	       "SRBdone SG=%i (%i/%i), req_buf = %p, adr = %p\n",
	       pcmd->use_sg, pSRB->SRBSGIndex, pSRB->SRBSGCount,
	       pcmd->request_buffer, ptr);
	dprintkdbg(DBG_KG,
	       "SRBdone (pid %li, target %02i-%i): ", pSRB->pcmd->pid,
	       pSRB->pcmd->device->id, pSRB->pcmd->device->lun);
	status = pSRB->TargetStatus;
	if (pSRB->SRBFlag & AUTO_REQSENSE) {
		dprintkdbg(DBG_0, "AUTO_REQSENSE1..............\n");
		DC395x_pci_unmap_sense(pACB, pSRB);
		/*
		 ** target status..........................
		 */
		pSRB->SRBFlag &= ~AUTO_REQSENSE;
		pSRB->AdaptStatus = 0;
		pSRB->TargetStatus = CHECK_CONDITION << 1;
		if (debug_enabled(DBG_KG)) {
			switch (pcmd->sense_buffer[2] & 0x0f) {
			case NOT_READY:
				dprintkl(KERN_DEBUG,
				     "ReqSense: NOT_READY (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i) ",
				     pcmd->cmnd[0], pDCB->TargetID,
				     pDCB->TargetLUN, status, pACB->scan_devices);
				break;
			case UNIT_ATTENTION:
				dprintkl(KERN_DEBUG,
				     "ReqSense: UNIT_ATTENTION (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i) ",
				     pcmd->cmnd[0], pDCB->TargetID,
				     pDCB->TargetLUN, status, pACB->scan_devices);
				break;
			case ILLEGAL_REQUEST:
				dprintkl(KERN_DEBUG,
				     "ReqSense: ILLEGAL_REQUEST (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i) ",
				     pcmd->cmnd[0], pDCB->TargetID,
				     pDCB->TargetLUN, status, pACB->scan_devices);
				break;
			case MEDIUM_ERROR:
				dprintkl(KERN_DEBUG,
				     "ReqSense: MEDIUM_ERROR (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i) ",
				     pcmd->cmnd[0], pDCB->TargetID,
				     pDCB->TargetLUN, status, pACB->scan_devices);
				break;
			case HARDWARE_ERROR:
				dprintkl(KERN_DEBUG,
				     "ReqSense: HARDWARE_ERROR (Cmnd = 0x%02x, Dev = %i-%i, Stat = %i, Scan = %i) ",
				     pcmd->cmnd[0], pDCB->TargetID,
				     pDCB->TargetLUN, status, pACB->scan_devices);
				break;
			}
			if (pcmd->sense_buffer[7] >= 6)
				dprintkl(KERN_DEBUG, 
				     "Sense=%02x, ASC=%02x, ASCQ=%02x (%08x %08x) ",
				     pcmd->sense_buffer[2], pcmd->sense_buffer[12],
				     pcmd->sense_buffer[13],
				     *((unsigned int *) (pcmd->sense_buffer + 3)),
				     *((unsigned int *) (pcmd->sense_buffer + 8)));
			else
				dprintkl(KERN_DEBUG,
				     "Sense=%02x, No ASC/ASCQ (%08x) ",
				     pcmd->sense_buffer[2],
				     *((unsigned int *) (pcmd->sense_buffer + 3)));
		}

		if (status == (CHECK_CONDITION << 1)) {
			pcmd->result = DID_BAD_TARGET << 16;
			goto ckc_e;
		}
		dprintkdbg(DBG_0, "AUTO_REQSENSE2..............\n");

		if ((pSRB->SRBTotalXferLength)
		    && (pSRB->SRBTotalXferLength >= pcmd->underflow))
			pcmd->result =
			    MK_RES_LNX(DRIVER_SENSE, DID_OK,
				       pSRB->EndMessage, CHECK_CONDITION);
		/*SET_RES_DID(pcmd->result,DID_OK) */
		else
			pcmd->result =
			    MK_RES_LNX(DRIVER_SENSE, DID_OK,
				       pSRB->EndMessage, CHECK_CONDITION);

		goto ckc_e;
	}

/*************************************************************/
	if (status) {
		/*
		 * target status..........................
		 */
		if (status_byte(status) == CHECK_CONDITION) {
			DC395x_RequestSense(pACB, pDCB, pSRB);
			return;
		} else if (status_byte(status) == QUEUE_FULL) {
			tempcnt = (u8) pDCB->GoingSRBCnt;
			printk
			    ("\nDC395x:  QUEUE_FULL for dev %02i-%i with %i cmnds\n",
			     pDCB->TargetID, pDCB->TargetLUN, tempcnt);
			if (tempcnt > 1)
				tempcnt--;
			pDCB->MaxCommand = tempcnt;
			DC395x_freetag(pDCB, pSRB);
			DC395x_Going_to_Waiting(pDCB, pSRB);
			DC395x_waiting_timer(pACB, HZ / 20);
			pSRB->AdaptStatus = 0;
			pSRB->TargetStatus = 0;
			return;
		} else if (status == SCSI_STAT_SEL_TIMEOUT) {
			pSRB->AdaptStatus = H_SEL_TIMEOUT;
			pSRB->TargetStatus = 0;
			pcmd->result = DID_NO_CONNECT << 16;
		} else {
			pSRB->AdaptStatus = 0;
			SET_RES_DID(pcmd->result, DID_ERROR);
			SET_RES_MSG(pcmd->result, pSRB->EndMessage);
			SET_RES_TARGET(pcmd->result, status);

		}
	} else {
		/*
		 ** process initiator status..........................
		 */
		status = pSRB->AdaptStatus;
		if (status & H_OVER_UNDER_RUN) {
			pSRB->TargetStatus = 0;
			SET_RES_DID(pcmd->result, DID_OK);
			SET_RES_MSG(pcmd->result, pSRB->EndMessage);
		} else if (pSRB->SRBStatus & PARITY_ERROR) {
			SET_RES_DID(pcmd->result, DID_PARITY);
			SET_RES_MSG(pcmd->result, pSRB->EndMessage);
		} else {	/* No error */

			pSRB->AdaptStatus = 0;
			pSRB->TargetStatus = 0;
			SET_RES_DID(pcmd->result, DID_OK);
		}
	}

	if (dir != PCI_DMA_NONE) {
		if (pcmd->use_sg)
			pci_dma_sync_sg(pACB->pdev,
					(struct scatterlist *) pcmd->
					request_buffer, pcmd->use_sg, dir);
		else if (pcmd->request_buffer)
			pci_dma_sync_single(pACB->pdev,
					    pSRB->SegmentX[0].address,
					    pcmd->request_bufflen, dir);
	}

	if ((pcmd->result & RES_DID) == 0 && pcmd->cmnd[0] == INQUIRY
	    && pcmd->cmnd[2] == 0 && pcmd->request_bufflen >= 8
	    && dir != PCI_DMA_NONE && ptr && (ptr->Vers & 0x07) >= 2)
		pDCB->Inquiry7 = ptr->Flags;
/* Check Error Conditions */
      ckc_e:

	/*if( pSRB->pcmd->cmnd[0] == INQUIRY && */
	/*  (host_byte(pcmd->result) == DID_OK || status_byte(pcmd->result) & CHECK_CONDITION) ) */
	if (pcmd->cmnd[0] == INQUIRY && (pcmd->result == (DID_OK << 16)
					 || status_byte(pcmd->
							result) &
					 CHECK_CONDITION)) {

		if (!pDCB->init_TCQ_flag) {
			DC395x_add_dev(pACB, pDCB, ptr);
			pDCB->init_TCQ_flag = 1;
		}

	}


	/* Here is the info for Doug Gilbert's sg3 ... */
	pcmd->resid = pSRB->SRBTotalXferLength;
	/* This may be interpreted by sb. or not ... */
	pcmd->SCp.this_residual = pSRB->SRBTotalXferLength;
	pcmd->SCp.buffers_residual = 0;
	if (debug_enabled(DBG_KG)) {
		if (pSRB->SRBTotalXferLength)
			dprintkdbg(DBG_KG, "pid %li: %02x (%02i-%i): Missed %i bytes\n",
			     pcmd->pid, pcmd->cmnd[0], pcmd->device->id,
			     pcmd->device->lun, pSRB->SRBTotalXferLength);
	}

	DC395x_Going_remove(pDCB, pSRB, 0);
	/* Add to free list */
	if (pSRB == pACB->pTmpSRB)
		dprintkl(KERN_ERR, "ERROR! Completed Cmnd with TmpSRB!\n");
	else
		DC395x_Free_insert(pACB, pSRB);

	dprintkdbg(DBG_0, "SRBdone: done pid %li\n", pcmd->pid);
	if (debug_enabled(DBG_KG)) {
		printk(" 0x%08x\n", pcmd->result);
	}
	TRACEPRINTF("%08x(%li)*", pcmd->result, jiffies);
	DC395x_pci_unmap(pACB, pSRB);
	/*DC395x_UNLOCK_ACB_NI; */
	pcmd->scsi_done(pcmd);
	/*DC395x_LOCK_ACB_NI; */
	TRACEOUTALL(KERN_INFO " %s\n", pSRB->debugtrace);

	DC395x_Query_to_Waiting(pACB);
	DC395x_Waiting_process(pACB);
	return;
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_reset
 * abort all cmds in our queues
 ********************************************************************
 */
void
DC395x_DoingSRB_Done(struct AdapterCtlBlk *pACB, u8 did_flag,
		     Scsi_Cmnd * cmd, u8 force)
{
	struct DeviceCtlBlk *pDCB;
	struct ScsiReqBlk *pSRB;
	struct ScsiReqBlk *pSRBTemp;
	u16 cnt;
	Scsi_Cmnd *pcmd;

	pDCB = pACB->pLinkDCB;
	if (!pDCB)
		return;
	dprintkl(KERN_INFO, "DC395x_DoingSRB_Done: pids ");
	do {
		/* As the ML may queue cmnds again, cache old values */
		struct ScsiReqBlk *pWaitingSRB = pDCB->pWaitingSRB;
		/*struct ScsiReqBlk* pWaitLast = pDCB->pWaitLast; */
		u16 WaitSRBCnt = pDCB->WaitSRBCnt;
		/* Going queue */
		cnt = pDCB->GoingSRBCnt;
		pSRB = pDCB->pGoingSRB;
		while (cnt--) {
			int result;
			int dir;
			pSRBTemp = pSRB->pNextSRB;
			pcmd = pSRB->pcmd;
			dir = scsi_to_pci_dma_dir(pcmd->sc_data_direction);
			result = MK_RES(0, did_flag, 0, 0);
			/*result = MK_RES(0,DID_RESET,0,0); */
			TRACEPRINTF("Reset(%li):%08x*", jiffies, result);
			printk(" (G)");
#if 1				/*ndef DC395x_DEBUGTRACE */
			printk("%li(%02i-%i) ", pcmd->pid,
			       pcmd->device->id, pcmd->device->lun);
#endif
			TRACEOUT("%s\n", pSRB->debugtrace);
			pDCB->pGoingSRB = pSRBTemp;
			pDCB->GoingSRBCnt--;
			if (!pSRBTemp)
				pDCB->pGoingLast = NULL;
			DC395x_freetag(pDCB, pSRB);
			DC395x_Free_insert(pACB, pSRB);
			pcmd->result = result;
			DC395x_pci_unmap_sense(pACB, pSRB);
			DC395x_pci_unmap(pACB, pSRB);
			if (force) {
				/* For new EH, we normally don't need to give commands back,
				 * as they all complete or all time out */
				/* do we need the aic7xxx hack and conditionally decrease retry ? */
				/*DC395x_SCSI_DONE_ACB_UNLOCK; */
				pcmd->scsi_done(pcmd);
				/*DC395x_SCSI_DONE_ACB_LOCK; */
			}
			pSRB = pSRBTemp;
		}
		if (pDCB->pGoingSRB)
			dprintkl(KERN_DEBUG, 
			       "How could the ML send cmnds to the Going queue? (%02i-%i)!!\n",
			       pDCB->TargetID, pDCB->TargetLUN);
		if (pDCB->TagMask)
			dprintkl(KERN_DEBUG,
			       "TagMask for %02i-%i should be empty, is %08x!\n",
			       pDCB->TargetID, pDCB->TargetLUN,
			       pDCB->TagMask);
		/*pDCB->GoingSRBCnt = 0;; */
		/*pDCB->pGoingSRB = NULL; pDCB->pGoingLast = NULL; */

		/* Waiting queue */
		cnt = WaitSRBCnt;
		pSRB = pWaitingSRB;
		while (cnt--) {
			int result;
			pSRBTemp = pSRB->pNextSRB;
			pcmd = pSRB->pcmd;
			result = MK_RES(0, did_flag, 0, 0);
			TRACEPRINTF("Reset(%li):%08x*", jiffies, result);
			printk(" (W)");
#if 1				/*ndef DC395x_DEBUGTRACE */
			printk("%li(%i-%i)", pcmd->pid, pcmd->device->id,
			       pcmd->device->lun);
#endif
			TRACEOUT("%s\n", pSRB->debugtrace);
			pDCB->pWaitingSRB = pSRBTemp;
			pDCB->WaitSRBCnt--;
			if (!pSRBTemp)
				pDCB->pWaitLast = NULL;
			DC395x_Free_insert(pACB, pSRB);

			pcmd->result = result;
			DC395x_pci_unmap_sense(pACB, pSRB);
			DC395x_pci_unmap(pACB, pSRB);
			if (force) {
				/* For new EH, we normally don't need to give commands back,
				 * as they all complete or all time out */
				/* do we need the aic7xxx hack and conditionally decrease retry ? */
				/*DC395x_SCSI_DONE_ACB_UNLOCK; */
				pcmd->scsi_done(pcmd);
				/*DC395x_SCSI_DONE_ACB_LOCK; */
				pSRB = pSRBTemp;
			}
		}
		if (pDCB->WaitSRBCnt)
			printk
			    ("\nDC395x: Debug: ML queued %i cmnds again to %02i-%i\n",
			     pDCB->WaitSRBCnt, pDCB->TargetID,
			     pDCB->TargetLUN);
		/* The ML could have queued the cmnds again! */
		/*pDCB->WaitSRBCnt = 0;; */
		/*pDCB->pWaitingSRB = NULL; pDCB->pWaitLast = NULL; */
		pDCB->DCBFlag &= ~ABORT_DEV_;
		pDCB = pDCB->pNextDCB;
	}
	while (pDCB != pACB->pLinkDCB && pDCB);
	printk("\n");
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_shutdown   DC395x_reset
 ********************************************************************
 */
static void DC395x_ResetSCSIBus(struct AdapterCtlBlk *pACB)
{
	/*u32  drv_flags=0; */

	dprintkdbg(DBG_0, "DC395x_ResetSCSIBus..............\n");

	/*DC395x_DRV_LOCK(drv_flags); */
	pACB->ACBFlag |= RESET_DEV;	/* RESET_DETECT, RESET_DONE, RESET_DEV */

	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_RSTSCSI);
	while (!(DC395x_read8(TRM_S1040_SCSI_INTSTATUS) & INT_SCSIRESET));

	/*DC395x_DRV_UNLOCK(drv_flags); */
	return;
}


/* Set basic config */
static void DC395x_basic_config(struct AdapterCtlBlk *pACB)
{
	u8 bval;
	u16 wval;
	DC395x_write8(TRM_S1040_SCSI_TIMEOUT, pACB->sel_timeout);
	if (pACB->Config & HCC_PARITY)
		bval = PHASELATCH | INITIATOR | BLOCKRST | PARITYCHECK;
	else
		bval = PHASELATCH | INITIATOR | BLOCKRST;

	DC395x_write8(TRM_S1040_SCSI_CONFIG0, bval);

	/* program configuration 1: Act_Neg (+ Act_Neg_Enh? + Fast_Filter? + DataDis?) */
	DC395x_write8(TRM_S1040_SCSI_CONFIG1, 0x03);	/* was 0x13: default */
	/* program Host ID                  */
	DC395x_write8(TRM_S1040_SCSI_HOSTID, pACB->pScsiHost->this_id);
	/* set ansynchronous transfer       */
	DC395x_write8(TRM_S1040_SCSI_OFFSET, 0x00);
	/* Turn LED control off */
	wval = DC395x_read16(TRM_S1040_GEN_CONTROL) & 0x7F;
	DC395x_write16(TRM_S1040_GEN_CONTROL, wval);
	/* DMA config          */
	wval = DC395x_read16(TRM_S1040_DMA_CONFIG) & ~DMA_FIFO_CTRL;
	wval |=
	    DMA_FIFO_HALF_HALF | DMA_ENHANCE /*| DMA_MEM_MULTI_READ */ ;
	/*dprintkl(KERN_INFO, "DMA_Config: %04x\n", wval); */
	DC395x_write16(TRM_S1040_DMA_CONFIG, wval);
	/* Clear pending interrupt status */
	DC395x_read8(TRM_S1040_SCSI_INTSTATUS);
	/* Enable SCSI interrupt    */
	DC395x_write8(TRM_S1040_SCSI_INTEN, 0x7F);
	DC395x_write8(TRM_S1040_DMA_INTEN, EN_SCSIINTR | EN_DMAXFERERROR
		      /*| EN_DMAXFERABORT | EN_DMAXFERCOMP | EN_FORCEDMACOMP */
		      );
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_Interrupt
 ********************************************************************
 */
static void DC395x_ScsiRstDetect(struct AdapterCtlBlk *pACB)
{
	dprintkl(KERN_INFO, "DC395x_ScsiRstDetect\n");
	/* delay half a second */
	if (timer_pending(&pACB->Waiting_Timer))
		del_timer(&pACB->Waiting_Timer);

	DC395x_write8(TRM_S1040_SCSI_CONTROL, DO_RSTMODULE);
	DC395x_write8(TRM_S1040_DMA_CONTROL, DMARESETMODULE);
	/*DC395x_write8(TRM_S1040_DMA_CONTROL,STOPDMAXFER); */
	udelay(500);
	/* Maybe we locked up the bus? Then lets wait even longer ... */
	pACB->pScsiHost->last_reset =
	    jiffies + 5 * HZ / 2 +
	    HZ * dc395x_trm_eepromBuf[pACB->AdapterIndex].NvramDelayTime;

	DC395x_clrfifo(pACB, "RstDet");
	DC395x_basic_config(pACB);
	/*1.25 */
	/*DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_HWRESELECT); */

	if (pACB->ACBFlag & RESET_DEV) {	/* RESET_DETECT, RESET_DONE, RESET_DEV */
		pACB->ACBFlag |= RESET_DONE;
	} else {
		pACB->ACBFlag |= RESET_DETECT;
		DC395x_ResetDevParam(pACB);
		DC395x_DoingSRB_Done(pACB, DID_RESET, 0, 1);
		/*DC395x_RecoverSRB( pACB ); */
		pACB->pActiveDCB = NULL;
		pACB->ACBFlag = 0;
		DC395x_Waiting_process(pACB);
	}

	return;
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_SRBdone
 ********************************************************************
 */
static void
DC395x_RequestSense(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB,
		    struct ScsiReqBlk *pSRB)
{
	Scsi_Cmnd *pcmd;

	pcmd = pSRB->pcmd;
	dprintkdbg(DBG_KG,
	       "DC395x_RequestSense for pid %li, target %02i-%i\n",
	       pcmd->pid, pcmd->device->id, pcmd->device->lun);
	TRACEPRINTF("RqSn*");
	pSRB->SRBFlag |= AUTO_REQSENSE;
	pSRB->AdaptStatus = 0;
	pSRB->TargetStatus = 0;

	/* KG: Can this prevent crap sense data ? */
	memset(pcmd->sense_buffer, 0, sizeof(pcmd->sense_buffer));

	/* Save some data */
	pSRB->SegmentX[DC395x_MAX_SG_LISTENTRY - 1].address =
	    pSRB->SegmentX[0].address;
	pSRB->SegmentX[DC395x_MAX_SG_LISTENTRY - 1].length =
	    pSRB->SegmentX[0].length;
	pSRB->Xferred = pSRB->SRBTotalXferLength;
	/* pSRB->SegmentX : a one entry of S/G list table */
	pSRB->SRBTotalXferLength = sizeof(pcmd->sense_buffer);
	pSRB->SegmentX[0].length = sizeof(pcmd->sense_buffer);
	/* Map sense buffer */
	pSRB->SegmentX[0].address =
	    pci_map_single(pACB->pdev, pcmd->sense_buffer,
			   sizeof(pcmd->sense_buffer), PCI_DMA_FROMDEVICE);
	dprintkdbg(DBG_SGPARANOIA, "Map sense buffer at %p (%05x) to %08x\n",
	       pcmd->sense_buffer, sizeof(pcmd->sense_buffer),
	       pSRB->SegmentX[0].address);
	pSRB->SRBSGCount = 1;
	pSRB->SRBSGIndex = 0;

	if (DC395x_StartSCSI(pACB, pDCB, pSRB)) {	/* Should only happen, if sb. else grabs the bus */
		dprintkl(KERN_DEBUG,
		       "Request Sense failed for pid %li (%02i-%i)!\n",
		       pSRB->pcmd->pid, pDCB->TargetID, pDCB->TargetLUN);
		TRACEPRINTF("?*");
		DC395x_Going_to_Waiting(pDCB, pSRB);
		DC395x_waiting_timer(pACB, HZ / 100);
	}
	TRACEPRINTF(".*");
}


/*
 *********************************************************************
 *		DC395x_queue_command
 *
 * Function : void DC395x_initDCB
 *  Purpose : initialize the internal structures for a given DCB
 *   Inputs : cmd - pointer to this scsi cmd request block structure
 *********************************************************************
 */
void
DC395x_initDCB(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk **ppDCB,
	       u8 target, u8 lun)
{
	struct NvRamType *eeprom;
	u8 PeriodIndex;
	u16 index;
	struct DeviceCtlBlk *pDCB;
	struct DeviceCtlBlk *pDCB2;

	dprintkdbg(DBG_0, "DC395x_initDCB..............\n");
	pDCB = dc395x_kmalloc(sizeof(struct DeviceCtlBlk), GFP_ATOMIC);
	/*pDCB = DC395x_findDCB (pACB, target, lun); */
	*ppDCB = pDCB;
	pDCB2 = 0;
	if (!pDCB)
		return;

	if (pACB->DCBCnt == 0) {
		pACB->pLinkDCB = pDCB;
		pACB->pDCBRunRobin = pDCB;
	} else {
		pACB->pLastDCB->pNextDCB = pDCB;
	}

	pACB->DCBCnt++;
	pDCB->pNextDCB = pACB->pLinkDCB;
	pACB->pLastDCB = pDCB;

	/* $$$$$$$ */
	pDCB->pDCBACB = pACB;
	pDCB->TargetID = target;
	pDCB->TargetLUN = lun;
	/* $$$$$$$ */
	pDCB->pWaitingSRB = NULL;
	pDCB->pGoingSRB = NULL;
	pDCB->GoingSRBCnt = 0;
	pDCB->WaitSRBCnt = 0;
	pDCB->pActiveSRB = NULL;
	/* $$$$$$$ */
	pDCB->TagMask = 0;
	pDCB->DCBFlag = 0;
	pDCB->MaxCommand = 1;
	pDCB->AdaptIndex = pACB->AdapterIndex;
	/* $$$$$$$ */
	index = pACB->AdapterIndex;
	eeprom = &dc395x_trm_eepromBuf[index];
	pDCB->DevMode = eeprom->NvramTarget[target].NvmTarCfg0;
	/*pDCB->AdpMode = eeprom->NvramChannelCfg; */
	pDCB->Inquiry7 = 0;
	pDCB->SyncMode = 0;
	pDCB->last_derated = pACB->pScsiHost->last_reset - 2;
	/* $$$$$$$ */
	pDCB->SyncPeriod = 0;
	pDCB->SyncOffset = 0;
	PeriodIndex = eeprom->NvramTarget[target].NvmTarPeriod & 0x07;
	pDCB->MinNegoPeriod = dc395x_clock_period[PeriodIndex];

#ifndef DC395x_NO_WIDE
	if ((pDCB->DevMode & NTC_DO_WIDE_NEGO)
	    && (pACB->Config & HCC_WIDE_CARD))
		pDCB->SyncMode |= WIDE_NEGO_ENABLE;
#endif
#ifndef DC395x_NO_SYNC
	if (pDCB->DevMode & NTC_DO_SYNC_NEGO)
		if (!(lun) || DC395x_CurrSyncOffset)
			pDCB->SyncMode |= SYNC_NEGO_ENABLE;
#endif
	/* $$$$$$$ */
#ifndef DC395x_NO_DISCONNECT
	pDCB->IdentifyMsg =
	    IDENTIFY(pDCB->DevMode & NTC_DO_DISCONNECT, lun);
#else
	pDCB->IdentifyMsg = IDENTIFY(0, lun);
#endif
	/* $$$$$$$ */
	if (pDCB->TargetLUN != 0) {
		/* Copy settings */
		struct DeviceCtlBlk *prevDCB = pACB->pLinkDCB;
		while (prevDCB->TargetID != pDCB->TargetID)
			prevDCB = prevDCB->pNextDCB;
		dprintkdbg(DBG_KG,
		       "Copy settings from %02i-%02i to %02i-%02i\n",
		       prevDCB->TargetID, prevDCB->TargetLUN,
		       pDCB->TargetID, pDCB->TargetLUN);
		pDCB->SyncMode = prevDCB->SyncMode;
		pDCB->SyncPeriod = prevDCB->SyncPeriod;
		pDCB->MinNegoPeriod = prevDCB->MinNegoPeriod;
		pDCB->SyncOffset = prevDCB->SyncOffset;
		pDCB->Inquiry7 = prevDCB->Inquiry7;
	};

	pACB->DCBmap[target] |= (1 << lun);
	pACB->children[target][lun] = pDCB;
}


#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
/*
 * Memory for trace buffers
 */
void DC395x_free_tracebufs(struct AdapterCtlBlk *pACB, int SRBIdx)
{
	int srbidx;
	const unsigned bufs_per_page = PAGE_SIZE / DEBUGTRACEBUFSZ;
	for (srbidx = 0; srbidx < SRBIdx; srbidx += bufs_per_page) {
		/*dprintkl(KERN_DEBUG, "Free tracebuf %p (for %i)\n", */
		/*      pACB->SRB_array[srbidx].debugtrace, srbidx); */
		dc395x_kfree(pACB->SRB_array[srbidx].debugtrace);
	}
}


int DC395x_alloc_tracebufs(struct AdapterCtlBlk *pACB)
{
	const unsigned mem_needed =
	    (DC395x_MAX_SRB_CNT + 1) * DEBUGTRACEBUFSZ;
	int pages = (mem_needed + (PAGE_SIZE - 1)) / PAGE_SIZE;
	const unsigned bufs_per_page = PAGE_SIZE / DEBUGTRACEBUFSZ;
	int SRBIdx = 0;
	unsigned i = 0;
	unsigned char *ptr;
	/*dprintkl(KERN_DEBUG, "Alloc %i pages for tracebufs\n", pages); */
	while (pages--) {
		ptr = dc395x_kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!ptr) {
			DC395x_free_tracebufs(pACB, SRBIdx);
			return 1;
		}
		/*dprintkl(KERN_DEBUG, "Alloc %li bytes at %p for tracebuf %i\n", */
		/*      PAGE_SIZE, ptr, SRBIdx); */
		i = 0;
		while (i < bufs_per_page && SRBIdx < DC395x_MAX_SRB_CNT)
			pACB->SRB_array[SRBIdx++].debugtrace =
			    ptr + (i++ * DEBUGTRACEBUFSZ);
	}
	if (i < bufs_per_page) {
		pACB->TmpSRB.debugtrace = ptr + (i * DEBUGTRACEBUFSZ);
		pACB->TmpSRB.debugtrace[0] = 0;
	} else
		dprintkl(KERN_DEBUG, "No space for tmpSRB tracebuf reserved?!\n");
	return 0;
}
#endif


/* Free SG tables */
static
void DC395x_free_SG_tables(struct AdapterCtlBlk *pACB, int SRBIdx)
{
	int srbidx;
	const unsigned SRBs_per_page =
	    PAGE_SIZE / (DC395x_MAX_SG_LISTENTRY * sizeof(struct SGentry));
	for (srbidx = 0; srbidx < SRBIdx; srbidx += SRBs_per_page) {
		/*dprintkl(KERN_DEBUG, "Free SG segs %p (for %i)\n", */
		/*      pACB->SRB_array[srbidx].SegmentX, srbidx); */
		dc395x_kfree(pACB->SRB_array[srbidx].SegmentX);
	}
}


/*
 * Allocate SG tables; as we have to pci_map them, an SG list (struct SGentry*)
 * should never cross a page boundary */
int DC395x_alloc_SG_tables(struct AdapterCtlBlk *pACB)
{
	const unsigned mem_needed =
	    (DC395x_MAX_SRB_CNT +
	     1) * DC395x_MAX_SG_LISTENTRY * sizeof(struct SGentry);
	int pages = (mem_needed + (PAGE_SIZE - 1)) / PAGE_SIZE;
	const unsigned SRBs_per_page =
	    PAGE_SIZE / (DC395x_MAX_SG_LISTENTRY * sizeof(struct SGentry));
	int SRBIdx = 0;
	unsigned i = 0;
	struct SGentry *ptr;
	/*dprintkl(KERN_DEBUG, "Alloc %i pages for SG tables\n", pages); */
	while (pages--) {
		ptr = (struct SGentry *) dc395x_kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!ptr) {
			DC395x_free_SG_tables(pACB, SRBIdx);
			return 1;
		}
		/*dprintkl(KERN_DEBUG, "Alloc %li bytes at %p for SG segments %i\n", */
		/*      PAGE_SIZE, ptr, SRBIdx); */
		i = 0;
		while (i < SRBs_per_page && SRBIdx < DC395x_MAX_SRB_CNT)
			pACB->SRB_array[SRBIdx++].SegmentX =
			    ptr + (i++ * DC395x_MAX_SG_LISTENTRY);
	}
	if (i < SRBs_per_page)
		pACB->TmpSRB.SegmentX =
		    ptr + (i * DC395x_MAX_SG_LISTENTRY);
	else
		dprintkl(KERN_DEBUG, "No space for tmpSRB SG table reserved?!\n");
	return 0;
}


/*
 ********************************************************************
 * scsiio
 *		DC395x_initACB
 ********************************************************************
 */
void __init DC395x_linkSRB(struct AdapterCtlBlk *pACB)
{
	int i;

	for (i = 0; i < pACB->SRBCount - 1; i++)
		pACB->SRB_array[i].pNextSRB = &pACB->SRB_array[i + 1];
	pACB->SRB_array[i].pNextSRB = NULL;
	/*DC395x_Free_integrity (pACB);     */
}


/*
 ***********************************************************************
 *		DC395x_init
 *
 * Function : static void DC395x_initACB
 *  Purpose :  initialize the internal structures for a given SCSI host
 *   Inputs : host - pointer to this host adapter's structure
 ***********************************************************************
 */
int __init
DC395x_initACB(struct Scsi_Host *host, u32 io_port, u8 irq, u16 index)
{
	struct NvRamType *eeprom;
	struct AdapterCtlBlk *pACB;
	u16 i;

	eeprom = &dc395x_trm_eepromBuf[index];
	host->max_cmd_len = 24;
	host->can_queue = DC395x_MAX_CMD_QUEUE;
	host->cmd_per_lun = DC395x_MAX_CMD_PER_LUN;
	host->this_id = (int) eeprom->NvramScsiId;
	host->io_port = io_port;
	host->n_io_port = 0x80;
	host->dma_channel = -1;
	host->unique_id = io_port;
	host->irq = irq;
	host->last_reset = jiffies;

	pACB = (struct AdapterCtlBlk *) host->hostdata;

	host->max_id = 16;
	if (host->max_id - 1 == eeprom->NvramScsiId)
		host->max_id--;
#ifdef	CONFIG_SCSI_MULTI_LUN
	if (eeprom->NvramChannelCfg & NAC_SCANLUN)
		host->max_lun = 8;
	else
		host->max_lun = 1;
#else
	host->max_lun = 1;
#endif
	/*
	 ********************************
	 */
	pACB->pScsiHost = host;
	pACB->IOPortBase = (u16) io_port;
	pACB->pLinkDCB = NULL;
	pACB->pDCBRunRobin = NULL;
	pACB->pActiveDCB = NULL;
	pACB->SRBCount = DC395x_MAX_SRB_CNT;
	pACB->AdapterIndex = index;
	pACB->status = 0;
	pACB->pScsiHost->this_id = eeprom->NvramScsiId;
	pACB->HostID_Bit = (1 << pACB->pScsiHost->this_id);
	/*pACB->pScsiHost->this_lun = 0; */
	pACB->DCBCnt = 0;
	pACB->DeviceCnt = 0;
	pACB->IRQLevel = irq;
	pACB->TagMaxNum = 1 << eeprom->NvramMaxTag;
	if (pACB->TagMaxNum > 30)
		pACB->TagMaxNum = 30;
	pACB->ACBFlag = 0;	/* RESET_DETECT, RESET_DONE, RESET_DEV */
	pACB->scan_devices = 1;
	pACB->MsgLen = 0;
	pACB->Gmode2 = eeprom->NvramChannelCfg;
	if (eeprom->NvramChannelCfg & NAC_SCANLUN)
		pACB->LUNchk = 1;
	/* 
	 * link all device's SRB Q of this adapter 
	 */
	if (DC395x_alloc_SG_tables(pACB)) {
		dprintkl(KERN_DEBUG, "SG table allocation failed!\n");
		return 1;
	}
#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
	if (DC395x_alloc_tracebufs(pACB)) {
		dprintkl(KERN_DEBUG, "SG trace buffer allocation failed!\n");
		DC395x_free_SG_tables(pACB, DC395x_MAX_SRB_CNT);
		return 1;
	}
#endif
	DC395x_linkSRB(pACB);
	pACB->pFreeSRB = pACB->SRB_array;
	/* 
	 * temp SRB for Q tag used or abort command used 
	 */
	pACB->pTmpSRB = &pACB->TmpSRB;
	pACB->TmpSRB.pSRBDCB = 0;
	pACB->TmpSRB.pNextSRB = 0;
	init_timer(&pACB->Waiting_Timer);

	for (i = 0; i < DC395x_MAX_SCSI_ID; i++)
		pACB->DCBmap[i] = 0;
	dprintkdbg(DBG_0, "pACB = %p, pDCBmap = %p, pSRB_array = %p\n", pACB,
	       pACB->DCBmap, pACB->SRB_array);
	dprintkdbg(DBG_0, "ACB size= %04x, DCB size= %04x, SRB size= %04x\n",
	       sizeof(struct AdapterCtlBlk), sizeof(struct DeviceCtlBlk),
	       sizeof(struct ScsiReqBlk));
	return 0;
}


/*===========================================================================
                                Init
  ===========================================================================*/
/*
 * Intialise the SCSI chip control registers
 *
 * @param host     This hosts adapter strcuture
 * @param io_port  The base I/O port
 * @param irq      IRQ
 * @param index    Card number?? (for multiple cards?)
 */
static int __init
DC395x_initAdapter(struct Scsi_Host *host, u32 io_port, u8 irq, u16 index)
{
	struct NvRamType *eeprom;
	struct AdapterCtlBlk *pACB;
	struct AdapterCtlBlk *pTempACB;
	u16 used_irq = 0;

	eeprom = &dc395x_trm_eepromBuf[index];
	pTempACB = DC395x_pACB_start;
	if (pTempACB != NULL) {
		while (pTempACB) {
			if (pTempACB->IRQLevel == irq) {
				used_irq = 1;
				break;
			} else
				pTempACB = pTempACB->pNextACB;
		}
	}

	if (!request_region(io_port, host->n_io_port, DC395X_NAME)) {
		dprintkl(KERN_ERR, "Failed to reserve IO region 0x%x\n", io_port);
		return -1;
	}
	if (!used_irq) {
		if (request_irq
		    (irq, DC395x_Interrupt, SA_SHIRQ, DC395X_NAME,
		     (void *) host->hostdata)) {
			dprintkl(KERN_INFO, "Failed to register IRQ!\n");
			return -1;
		}
	}

	pACB = (struct AdapterCtlBlk *) host->hostdata;
	pACB->IOPortBase = io_port;

	/* selection timeout = 250 ms */
	pACB->sel_timeout = DC395x_SEL_TIMEOUT;

	/* Mask all the interrupt */
	DC395x_write8(TRM_S1040_DMA_INTEN, 0x00);
	DC395x_write8(TRM_S1040_SCSI_INTEN, 0x00);

	/* Reset SCSI module */
	DC395x_write16(TRM_S1040_SCSI_CONTROL, DO_RSTMODULE);

	/* Reset PCI/DMA module */
	DC395x_write8(TRM_S1040_DMA_CONTROL, DMARESETMODULE);
	udelay(20);

	/* program configuration 0 */
	pACB->Config = HCC_AUTOTERM | HCC_PARITY;
	if (DC395x_read8(TRM_S1040_GEN_STATUS) & WIDESCSI)
		pACB->Config |= HCC_WIDE_CARD;

	if (eeprom->NvramChannelCfg & NAC_POWERON_SCSI_RESET)
		pACB->Config |= HCC_SCSI_RESET;

	if (pACB->Config & HCC_SCSI_RESET) {
		dprintkl(KERN_INFO, "Performing initial SCSI bus reset\n");
		DC395x_write8(TRM_S1040_SCSI_CONTROL, DO_RSTSCSI);

		/*while (!( DC395x_read8(TRM_S1040_SCSI_INTSTATUS) & INT_SCSIRESET )); */
		/*spin_unlock_irq (&io_request_lock); */
		udelay(500);

		pACB->pScsiHost->last_reset =
		    jiffies + HZ / 2 +
		    HZ *
		    dc395x_trm_eepromBuf[pACB->AdapterIndex].
		    NvramDelayTime;

		/*spin_lock_irq (&io_request_lock); */
	}
	DC395x_basic_config(pACB);
	return 0;
}


/*
 * eeprom - wait 30 us
 *
 * Waits for 30us (using the chip by the looks of it..)
 *
 * @param io_port  - base I/O address
 */
static void __init TRM_S1040_wait_30us(u16 io_port)
{
	/* ScsiPortStallExecution(30); wait 30 us */
	outb(5, io_port + TRM_S1040_GEN_TIMER);
	while (!(inb(io_port + TRM_S1040_GEN_STATUS) & GTIMEOUT))
		/* nothing */ ;
	return;
}


/*
 * eeprom - write command and address to chip
 *
 * Write the specified command and address 
 *
 * @param io_port - base I/O address
 * @param cmd     - SB + op code (command) to send
 * @param addr    - address to send
 */
static void __init TRM_S1040_write_cmd(u16 io_port, u8 cmd, u8 addr)
{
	int i;
	u8 send_data;

	/* program SB + OP code */
	for (i = 0; i < 3; i++, cmd <<= 1) {
		send_data = NVR_SELECT;
		if (cmd & 0x04)	/* Start from bit 2 */
			send_data |= NVR_BITOUT;

		outb(send_data, io_port + TRM_S1040_GEN_NVRAM);
		TRM_S1040_wait_30us(io_port);
		outb((send_data | NVR_CLOCK),
		     io_port + TRM_S1040_GEN_NVRAM);
		TRM_S1040_wait_30us(io_port);
	}

	/* send address */
	for (i = 0; i < 7; i++, addr <<= 1) {
		send_data = NVR_SELECT;
		if (addr & 0x40)	/* Start from bit 6 */
			send_data |= NVR_BITOUT;

		outb(send_data, io_port + TRM_S1040_GEN_NVRAM);
		TRM_S1040_wait_30us(io_port);
		outb((send_data | NVR_CLOCK),
		     io_port + TRM_S1040_GEN_NVRAM);
		TRM_S1040_wait_30us(io_port);
	}
	outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);
	TRM_S1040_wait_30us(io_port);
}


/*
 * eeprom - store a single byte in the SEEPROM
 *
 * Called from write all to write a single byte into the SSEEPROM
 * Which is done one bit at a time.
 *
 * @param io_port - base I/O address
 * @param addr    - offset into EEPROM
 * @param byte    - bytes to write
 */
static void __init TRM_S1040_set_data(u16 io_port, u8 addr, u8 byte)
{
	int i;
	u8 send_data;

	/* Send write command & address */
	TRM_S1040_write_cmd(io_port, 0x05, addr);

	/* Write data */
	for (i = 0; i < 8; i++, byte <<= 1) {
		send_data = NVR_SELECT;
		if (byte & 0x80)	/* Start from bit 7 */
			send_data |= NVR_BITOUT;

		outb(send_data, io_port + TRM_S1040_GEN_NVRAM);
		TRM_S1040_wait_30us(io_port);
		outb((send_data | NVR_CLOCK),
		     io_port + TRM_S1040_GEN_NVRAM);
		TRM_S1040_wait_30us(io_port);
	}
	outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);
	TRM_S1040_wait_30us(io_port);

	/* Disable chip select */
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
	TRM_S1040_wait_30us(io_port);

	outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);
	TRM_S1040_wait_30us(io_port);

	/* Wait for write ready */
	while (1) {
		outb((NVR_SELECT | NVR_CLOCK),
		     io_port + TRM_S1040_GEN_NVRAM);
		TRM_S1040_wait_30us(io_port);

		outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);
		TRM_S1040_wait_30us(io_port);

		if (inb(io_port + TRM_S1040_GEN_NVRAM) & NVR_BITIN)
			break;
	}

	/*  Disable chip select */
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
}


/*
 * eeprom - write 128 bytes to the SEEPROM
 *
 * Write the supplied 128 bytes to the chips SEEPROM
 *
 * @param eeprom  - the data to write
 * @param io_port - the base io port
 */
static void __init
TRM_S1040_write_all(struct NvRamType *eeprom, u16 io_port)
{
	u8 *b_eeprom = (u8 *) eeprom;
	u8 addr;

	/* Enable SEEPROM */
	outb((inb(io_port + TRM_S1040_GEN_CONTROL) | EN_EEPROM),
	     io_port + TRM_S1040_GEN_CONTROL);

	/* write enable */
	TRM_S1040_write_cmd(io_port, 0x04, 0xFF);
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
	TRM_S1040_wait_30us(io_port);

	/* write */
	for (addr = 0; addr < 128; addr++, b_eeprom++) {
		TRM_S1040_set_data(io_port, addr, *b_eeprom);
	}

	/* write disable */
	TRM_S1040_write_cmd(io_port, 0x04, 0x00);
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
	TRM_S1040_wait_30us(io_port);

	/* Disable SEEPROM */
	outb((inb(io_port + TRM_S1040_GEN_CONTROL) & ~EN_EEPROM),
	     io_port + TRM_S1040_GEN_CONTROL);
}


/*
 * eeprom - get a single byte from the SEEPROM
 *
 * Called from read all to read a single byte into the SSEEPROM
 * Which is done one bit at a time.
 *
 * @param io_port  - base I/O address
 * @param addr     - offset into SEEPROM
 * @return         - the byte read
 */
static u8 __init TRM_S1040_get_data(u16 io_port, u8 addr)
{
	int i;
	u8 read_byte;
	u8 result = 0;

	/* Send read command & address */
	TRM_S1040_write_cmd(io_port, 0x06, addr);

	/* read data */
	for (i = 0; i < 8; i++) {
		outb((NVR_SELECT | NVR_CLOCK),
		     io_port + TRM_S1040_GEN_NVRAM);
		TRM_S1040_wait_30us(io_port);
		outb(NVR_SELECT, io_port + TRM_S1040_GEN_NVRAM);

		/* Get data bit while falling edge */
		read_byte = inb(io_port + TRM_S1040_GEN_NVRAM);
		result <<= 1;
		if (read_byte & NVR_BITIN)
			result |= 1;

		TRM_S1040_wait_30us(io_port);
	}

	/* Disable chip select */
	outb(0, io_port + TRM_S1040_GEN_NVRAM);
	return result;
}


/*
 * eeprom - read_all
 *
 * Read the 128 bytes from the SEEPROM.
 *
 * @param eeprom - where to store the data
 * @param io_port - the base io port
 */
static void __init
TRM_S1040_read_all(struct NvRamType *eeprom, u16 io_port)
{
	u8 *b_eeprom = (u8 *) eeprom;
	u8 addr;

	/* Enable SEEPROM */
	outb((inb(io_port + TRM_S1040_GEN_CONTROL) | EN_EEPROM),
	     io_port + TRM_S1040_GEN_CONTROL);

	/* read details */
	for (addr = 0; addr < 128; addr++, b_eeprom++) {
		*b_eeprom = TRM_S1040_get_data(io_port, addr);
	}

	/* Disable SEEPROM */
	outb((inb(io_port + TRM_S1040_GEN_CONTROL) & ~EN_EEPROM),
	     io_port + TRM_S1040_GEN_CONTROL);
}



/*
 * eeprom - get and check contents
 *
 * Read seeprom 128 bytes into the memory provider in eeprom.
 * Checks the checksum and if it's not correct it uses a set of default
 * values.
 *
 * @param eeprom  - caller allocated strcuture to read the eeprom data into
 * @param io_port - io port to read from
 */
static void __init
DC395x_check_eeprom(struct NvRamType *eeprom, u16 io_port)
{
	u16 *w_eeprom = (u16 *) eeprom;
	u16 w_addr;
	u16 cksum;
	u32 d_addr;
	u32 *d_eeprom;

	TRM_S1040_read_all(eeprom, io_port);	/* read eeprom */

	cksum = 0;
	for (w_addr = 0, w_eeprom = (u16 *) eeprom; w_addr < 64;
	     w_addr++, w_eeprom++)
		cksum += *w_eeprom;
	if (cksum != 0x1234) {
		/*
		 * Checksum is wrong.
		 * Load a set of defaults into the eeprom buffer
		 */
		dprintkl(KERN_WARNING,
		       "EEProm checksum error: using default values and options.\n");
		eeprom->NvramSubVendorID[0] = (u8) PCI_VENDOR_ID_TEKRAM;
		eeprom->NvramSubVendorID[1] =
		    (u8) (PCI_VENDOR_ID_TEKRAM >> 8);
		eeprom->NvramSubSysID[0] =
		    (u8) PCI_DEVICE_ID_TEKRAM_TRMS1040;
		eeprom->NvramSubSysID[1] =
		    (u8) (PCI_DEVICE_ID_TEKRAM_TRMS1040 >> 8);
		eeprom->NvramSubClass = 0x00;
		eeprom->NvramVendorID[0] = (u8) PCI_VENDOR_ID_TEKRAM;
		eeprom->NvramVendorID[1] =
		    (u8) (PCI_VENDOR_ID_TEKRAM >> 8);
		eeprom->NvramDeviceID[0] =
		    (u8) PCI_DEVICE_ID_TEKRAM_TRMS1040;
		eeprom->NvramDeviceID[1] =
		    (u8) (PCI_DEVICE_ID_TEKRAM_TRMS1040 >> 8);
		eeprom->NvramReserved = 0x00;

		for (d_addr = 0, d_eeprom = (u32 *) eeprom->NvramTarget;
		     d_addr < 16; d_addr++, d_eeprom++)
			*d_eeprom = 0x00000077;	/* NvmTarCfg3,NvmTarCfg2,NvmTarPeriod,NvmTarCfg0 */

		*d_eeprom++ = 0x04000F07;	/* NvramMaxTag,NvramDelayTime,NvramChannelCfg,NvramScsiId */
		*d_eeprom++ = 0x00000015;	/* NvramReserved1,NvramBootLun,NvramBootTarget,NvramReserved0 */
		for (d_addr = 0; d_addr < 12; d_addr++, d_eeprom++)
			*d_eeprom = 0x00;

		/* Now load defaults (maybe set by boot/module params) */
		set_safe_settings();
		fix_settings();
		DC395x_EEprom_Override(eeprom);

		eeprom->NvramCheckSum = 0x00;
		for (w_addr = 0, cksum = 0, w_eeprom = (u16 *) eeprom;
		     w_addr < 63; w_addr++, w_eeprom++)
			cksum += *w_eeprom;

		*w_eeprom = 0x1234 - cksum;
		TRM_S1040_write_all(eeprom, io_port);
		eeprom->NvramDelayTime = cfg_data[CFG_RESET_DELAY].value;
	} else {
		set_safe_settings();
		eeprom_index_to_delay(eeprom);
		DC395x_EEprom_Override(eeprom);
	}
}


/*
 * adapter - print connection and terminiation config
 *
 * @param pACB - adapter control block
 */
static void __init DC395x_print_config(struct AdapterCtlBlk *pACB)
{
	u8 bval;

	bval = DC395x_read8(TRM_S1040_GEN_STATUS);
	dprintkl(KERN_INFO, "%c: Connectors: ",
	       ((bval & WIDESCSI) ? 'W' : ' '));
	if (!(bval & CON5068))
		printk("ext%s ", !(bval & EXT68HIGH) ? "68" : "50");
	if (!(bval & CON68))
		printk("int68%s ", !(bval & INT68HIGH) ? "" : "(50)");
	if (!(bval & CON50))
		printk("int50 ");
	if ((bval & (CON5068 | CON50 | CON68)) ==
	    0 /*(CON5068 | CON50 | CON68) */ )
		printk(" Oops! (All 3?) ");
	bval = DC395x_read8(TRM_S1040_GEN_CONTROL);
	printk(" Termination: ");
	if (bval & DIS_TERM)
		printk("Disabled\n");
	else {
		if (bval & AUTOTERM)
			printk("Auto ");
		if (bval & LOW8TERM)
			printk("Low ");
		if (bval & UP8TERM)
			printk("High ");
		printk("\n");
	}
}


/**
 * DC395x_print_eeprom_settings - output the eeprom settings
 * to the kernel log so people can see what they were.
 *
 * @index: Adapter number
 **/
static void __init
DC395x_print_eeprom_settings(u16 index)
{
	dprintkl(KERN_INFO, "Used settings: AdapterID=%02i, Speed=%i(%02i.%01iMHz), DevMode=0x%02x\n",
	       dc395x_trm_eepromBuf[index].NvramScsiId,
	       dc395x_trm_eepromBuf[index].NvramTarget[0].NvmTarPeriod,
	       dc395x_clock_speed[dc395x_trm_eepromBuf[index].NvramTarget[0].NvmTarPeriod] / 10,
	       dc395x_clock_speed[dc395x_trm_eepromBuf[index].NvramTarget[0].NvmTarPeriod] % 10,
	       dc395x_trm_eepromBuf[index].NvramTarget[0].NvmTarCfg0);
	dprintkl(KERN_INFO, "               AdaptMode=0x%02x, Tags=%i(%02i), DelayReset=%is\n",
	       dc395x_trm_eepromBuf[index].NvramChannelCfg,
	       dc395x_trm_eepromBuf[index].NvramMaxTag,
	       1 << dc395x_trm_eepromBuf[index].NvramMaxTag,
	       dc395x_trm_eepromBuf[index].NvramDelayTime);
}


/*
 *********************************************************************
 *			DC395x_detect
 *
 *      Function : static int DC395x_init (struct Scsi_Host *host)
 *       Purpose : initialize the internal structures for a given SCSI host
 *        Inputs : host - pointer to this host adapter's structure/
 * Preconditions : when this function is called, the chip_type
 *		   field of the pACB structure MUST have been set.
 *********************************************************************
 */
static struct Scsi_Host *__init
DC395x_init(Scsi_Host_Template * host_template, u32 io_port, u8 irq,
	    u16 index)
{
	struct Scsi_Host *host;
	struct AdapterCtlBlk *pACB;

	/*
	 * Read the eeprom contents info the buffer we supply. Use
	 * defaults is eeprom checksum is wrong.
	 */
	DC395x_check_eeprom(&dc395x_trm_eepromBuf[index], (u16) io_port);

	/*
	 *$$$$$$$$$$$  MEMORY ALLOCATE FOR ADAPTER CONTROL BLOCK $$$$$$$$$$$$
	 */
	host = scsi_register(host_template, sizeof(struct AdapterCtlBlk));
	if (!host) {
		dprintkl(KERN_INFO, "pSH scsi_register ERROR\n");
		return 0;
	}
	DC395x_print_eeprom_settings(index);

	pACB = (struct AdapterCtlBlk *) host->hostdata;

	if (DC395x_initACB(host, io_port, irq, index)) {
		scsi_unregister(host);
		return 0;
	}
	DC395x_print_config(pACB);

       /*
        *$$$$$$$$$$$$$$$$$ INITIAL ADAPTER $$$$$$$$$$$$$$$$$
        */
	if (!DC395x_initAdapter(host, io_port, irq, index)) {
		if (!DC395x_pACB_start) {
			DC395x_pACB_start = pACB;
		} else {
			DC395x_pACB_current->pNextACB = pACB;
		}
		DC395x_pACB_current = pACB;
		pACB->pNextACB = NULL;

	} else {
		dprintkl(KERN_INFO, "DC395x_initAdapter initial ERROR\n");
		scsi_unregister(host);
		host = NULL;
	}
	return host;
}

/*
 * Functions: DC395x_inquiry(), DC395x_inquiry_done()
 *
 * Purpose: When changing speed etc., we have to issue an INQUIRY
 *	    command to make sure, we agree upon the nego parameters
 *	    with the device
 */
static void DC395x_inquiry_done(Scsi_Cmnd * cmd)
{
	struct AdapterCtlBlk *pACB =
	    (struct AdapterCtlBlk *) cmd->device->host->hostdata;
	struct DeviceCtlBlk *pDCB =
	    DC395x_findDCB(pACB, cmd->device->id, cmd->device->lun);
#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
	struct ScsiReqBlk *pSRB = pACB->pFreeSRB;
#endif
	dprintkl(KERN_INFO,
	       "INQUIRY (%02i-%i) returned %08x: %02x %02x %02x %02x ...\n",
	       cmd->device->id, cmd->device->lun, cmd->result,
	       ((u8 *) cmd->request_buffer)[0],
	       ((u8 *) cmd->request_buffer)[1],
	       ((u8 *) cmd->request_buffer)[2],
	       ((u8 *) cmd->request_buffer)[3]);
	/*TRACEOUT ("%s\n", pSRB->debugtrace); */
	if (cmd->result) {
		dprintkl(KERN_INFO, "Unsetting Wide, Sync and TagQ!\n");
		if (pDCB) {
			TRACEOUT("%s\n", pSRB->debugtrace);
			pDCB->DevMode &=
			    ~(NTC_DO_SYNC_NEGO | NTC_DO_WIDE_NEGO |
			      NTC_DO_TAG_QUEUEING);
			DC395x_updateDCB(pACB, pDCB);
		}
	}
	if (pDCB) {
		if (!(pDCB->SyncMode & SYNC_NEGO_DONE)) {
			pDCB->SyncOffset = 0;	/*pDCB->SyncMode &= ~SYNC_NEGO_ENABLE; */
		}
		if (!(pDCB->SyncMode & WIDE_NEGO_DONE)) {
			pDCB->SyncPeriod &= ~WIDE_SYNC;
			pDCB->SyncMode &= ~WIDE_NEGO_ENABLE;
		}
	} else {
		dprintkl(KERN_ERR, 
		       "ERROR! No DCB existent for %02i-%i ?\n",
		       cmd->device->id, cmd->device->lun);
	}
	kfree(cmd->buffer);
	kfree(cmd);
}


/*
 * Perform INQUIRY
 */
void DC395x_inquiry(struct AdapterCtlBlk *pACB, struct DeviceCtlBlk *pDCB)
{
	char *buffer;
	Scsi_Cmnd *cmd;
	cmd = dc395x_kmalloc(sizeof(Scsi_Cmnd), GFP_ATOMIC);
	if (!cmd) {
		dprintkl(KERN_ERR, "kmalloc failed in inquiry!\n");
		return;
	}
	buffer = kmalloc(256, GFP_ATOMIC);
	if (!buffer) {
		kfree(cmd);
		dprintkl(KERN_ERR, "kmalloc failed in inquiry!\n");
		return;
	}

	memset(cmd, 0, sizeof(Scsi_Cmnd));
	cmd->cmnd[0] = INQUIRY;
	cmd->cmnd[1] = (pDCB->TargetLUN << 5) & 0xe0;
	cmd->cmnd[4] = 0xff;

	cmd->cmd_len = 6;
	cmd->old_cmd_len = 6;
	cmd->device->host = pACB->pScsiHost;
	cmd->device->id = pDCB->TargetID;
	cmd->device->lun = pDCB->TargetLUN;
	cmd->serial_number = 1;
	cmd->pid = 395;
	cmd->bufflen = 128;
	cmd->buffer = buffer;
	cmd->request_bufflen = 128;
	cmd->request_buffer = &buffer[128];
	cmd->done = DC395x_inquiry_done;
	cmd->scsi_done = DC395x_inquiry_done;
	cmd->timeout_per_command = HZ;

#if 0
	/* XXX */
	cmd->request.rq_status = RQ_SCSI_BUSY;
#endif

	pDCB->SyncMode &= ~SYNC_NEGO_DONE;
	pDCB->SyncMode |= SYNC_NEGO_ENABLE;
	pDCB->SyncMode &= ~WIDE_NEGO_DONE;
	pDCB->SyncMode |= WIDE_NEGO_ENABLE;
	dprintkl(KERN_INFO, 
	       "Queue INQUIRY command to dev %02i-%i\n", pDCB->TargetID,
	       pDCB->TargetLUN);
	DC395x_queue_command(cmd, DC395x_inquiry_done);
}

#undef SEARCH
#undef YESNO
#undef SCANF


/*
 ******************************************************************
 * Function: DC395x_proc_info(char* buffer, char **start,
 *			 off_t offset, int length, int hostno, int inout)
 *  Purpose: return SCSI Adapter/Device Info
 *    Input:
 *          buffer: Pointer to a buffer where to write info
 *		 start :
 *		 offset:
 *		 hostno: Host adapter index
 *		 inout : Read (=0) or set(!=0) info
 *   Output:
 *          buffer: contains info length 
 *		         
 *    return value: length of info in buffer
 *
 ******************************************************************
 */

/* KG: proc_info taken from driver aha152x.c */

#undef SPRINTF
#define SPRINTF(args...) pos += sprintf(pos, args)

#define YESNO(YN) \
 if (YN) SPRINTF(" Yes ");\
 else SPRINTF(" No  ")

static int
DC395x_proc_info(struct Scsi_Host *shpnt, char *buffer, char **start, off_t offset, int length,
		 int inout)
{
	int dev, spd, spd1;
	char *pos = buffer;
	struct AdapterCtlBlk *pACB;
	struct DeviceCtlBlk *pDCB;
	unsigned long flags;
	Scsi_Cmnd *pcmd;

	/*  Scsi_Cmnd *ptr; */

	pACB = DC395x_pACB_start;

	while (pACB) {
		if (pACB->pScsiHost == shpnt)
			break;
		pACB = pACB->pNextACB;
	}
	if (!pACB)
		return -ESRCH;

	if (inout)		/* Has data been written to the file ? */
		return -EPERM;

	SPRINTF(DC395X_BANNER " PCI SCSI Host Adapter\n");
	SPRINTF(" Driver Version " DC395X_VERSION "\n");

	DC395x_LOCK_IO(pACB->pScsiHost);

	SPRINTF("SCSI Host Nr %i, ", shpnt->host_no);
	SPRINTF("DC395U/UW/F DC315/U %s Adapter Nr %i\n",
		(pACB->Config & HCC_WIDE_CARD) ? "Wide" : "",
		pACB->AdapterIndex);
	SPRINTF("IOPortBase 0x%04x, ", pACB->IOPortBase);
	SPRINTF("IRQLevel 0x%02x, ", pACB->IRQLevel);
	SPRINTF(" SelTimeout %ims\n", (1638 * pACB->sel_timeout) / 1000);

	SPRINTF("MaxID %i, MaxLUN %i, ", shpnt->max_id, shpnt->max_lun);
	SPRINTF("AdapterID %i\n", shpnt->this_id);

	SPRINTF("TagMaxNum %i, Status %i", pACB->TagMaxNum, pACB->status);
	/*SPRINTF(", DMA_Status %i\n", DC395x_read8(TRM_S1040_DMA_STATUS)); */
	SPRINTF(", FilterCfg 0x%02x",
		DC395x_read8(TRM_S1040_SCSI_CONFIG1));
	SPRINTF(", DelayReset %is\n",
		dc395x_trm_eepromBuf[pACB->AdapterIndex].NvramDelayTime);
	/*SPRINTF("\n"); */

	SPRINTF("Nr of attached devices: %i, Nr of DCBs: %i\n",
		pACB->DeviceCnt, pACB->DCBCnt);
	SPRINTF
	    ("Map of attached LUNs: %02x %02x %02x %02x %02x %02x %02x %02x\n",
	     pACB->DCBmap[0], pACB->DCBmap[1], pACB->DCBmap[2],
	     pACB->DCBmap[3], pACB->DCBmap[4], pACB->DCBmap[5],
	     pACB->DCBmap[6], pACB->DCBmap[7]);
	SPRINTF
	    ("                      %02x %02x %02x %02x %02x %02x %02x %02x\n",
	     pACB->DCBmap[8], pACB->DCBmap[9], pACB->DCBmap[10],
	     pACB->DCBmap[11], pACB->DCBmap[12], pACB->DCBmap[13],
	     pACB->DCBmap[14], pACB->DCBmap[15]);

	SPRINTF
	    ("Un ID LUN Prty Sync Wide DsCn SndS TagQ NegoPeriod SyncFreq SyncOffs MaxCmd\n");

	pDCB = pACB->pLinkDCB;
	for (dev = 0; dev < pACB->DCBCnt; dev++) {
		int NegoPeriod;
		SPRINTF("%02i %02i  %02i ", dev, pDCB->TargetID,
			pDCB->TargetLUN);
		YESNO(pDCB->DevMode & NTC_DO_PARITY_CHK);
		YESNO(pDCB->SyncOffset);
		YESNO(pDCB->SyncPeriod & WIDE_SYNC);
		YESNO(pDCB->DevMode & NTC_DO_DISCONNECT);
		YESNO(pDCB->DevMode & NTC_DO_SEND_START);
		YESNO(pDCB->SyncMode & EN_TAG_QUEUEING);
		NegoPeriod =
		    dc395x_clock_period[pDCB->SyncPeriod & 0x07] << 2;
		if (pDCB->SyncOffset)
			SPRINTF("  %03i ns ", NegoPeriod);
		else
			SPRINTF(" (%03i ns)", (pDCB->MinNegoPeriod << 2));

		if (pDCB->SyncOffset & 0x0f) {
			spd = 1000 / (NegoPeriod);
			spd1 = 1000 % (NegoPeriod);
			spd1 = (spd1 * 10 + NegoPeriod / 2) / (NegoPeriod);
			SPRINTF("   %2i.%1i M     %02i ", spd, spd1,
				(pDCB->SyncOffset & 0x0f));
		} else
			SPRINTF("                 ");

		/* Add more info ... */
		SPRINTF("     %02i\n", pDCB->MaxCommand);
		pDCB = pDCB->pNextDCB;
	}

	SPRINTF("Commands in Queues: Query: %i:", pACB->QueryCnt);
	for (pcmd = pACB->pQueryHead; pcmd;
	     pcmd = (Scsi_Cmnd *) pcmd->host_scribble)
		SPRINTF(" %li", pcmd->pid);
	if (timer_pending(&pACB->Waiting_Timer))
		SPRINTF("Waiting queue timer running\n");
	else
		SPRINTF("\n");
	pDCB = pACB->pLinkDCB;

	for (dev = 0; dev < pACB->DCBCnt; dev++) {
		struct ScsiReqBlk *pSRB;
		if (pDCB->WaitSRBCnt)
			SPRINTF("DCB (%02i-%i): Waiting: %i:",
				pDCB->TargetID, pDCB->TargetLUN,
				pDCB->WaitSRBCnt);
		for (pSRB = pDCB->pWaitingSRB; pSRB; pSRB = pSRB->pNextSRB)
			SPRINTF(" %li", pSRB->pcmd->pid);
		if (pDCB->GoingSRBCnt)
			SPRINTF("\nDCB (%02i-%i): Going  : %i:",
				pDCB->TargetID, pDCB->TargetLUN,
				pDCB->GoingSRBCnt);
		for (pSRB = pDCB->pGoingSRB; pSRB; pSRB = pSRB->pNextSRB)
#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
			SPRINTF("\n  %s", pSRB->debugtrace);
#else
			SPRINTF(" %li", pSRB->pcmd->pid);
#endif
		if (pDCB->WaitSRBCnt || pDCB->GoingSRBCnt)
			SPRINTF("\n");
		pDCB = pDCB->pNextDCB;
	}

	if (debug_enabled(DBG_DCB)) {
		SPRINTF("DCB list for ACB %p:\n", pACB);
		pDCB = pACB->pLinkDCB;
		SPRINTF("%p", pDCB);
		for (dev = 0; dev < pACB->DCBCnt; dev++, pDCB = pDCB->pNextDCB)
			SPRINTF("->%p", pDCB->pNextDCB);
		SPRINTF("\n");
	}

	*start = buffer + offset;
	DC395x_UNLOCK_IO(pACB->pScsiHost);

	if (pos - buffer < offset)
		return 0;
	else if (pos - buffer - offset < length)
		return pos - buffer - offset;
	else
		return length;
}


/**
 * DC395x_chip_shutdown - cleanly shut down the scsi controller chip,
 * stopping all operations and disablig interrupt generation on the
 * card.
 *
 * @acb: The scsi adapter control block of the adapter to shut down.
 **/
static
void DC395x_chip_shutdown(struct AdapterCtlBlk *pACB)
{
	/* disable interrupt */
	DC395x_write8(TRM_S1040_DMA_INTEN, 0);
	DC395x_write8(TRM_S1040_SCSI_INTEN, 0);

	/* remove timers */
	if (timer_pending(&pACB->Waiting_Timer))
		del_timer(&pACB->Waiting_Timer);
	if (timer_pending(&pACB->SelTO_Timer))
		del_timer(&pACB->SelTO_Timer);

	/* reset the scsi bus */
	if (pACB->Config & HCC_SCSI_RESET)
		DC395x_ResetSCSIBus(pACB);

	/* clear any pending interupt state */
	DC395x_read8(TRM_S1040_SCSI_INTSTATUS);

	/* release chip resources */
#if debug_enabled(DBG_TRACE|DBG_TRACEALL)
	DC395x_free_tracebufs(pACB, DC395x_MAX_SRB_CNT);
#endif
	DC395x_free_SG_tables(pACB, DC395x_MAX_SRB_CNT);
}


/**
 * DC395x_free_DCBs - Free all of the DCBs.
 *
 * @pACB: Adapter to remove the DCBs for.
 **/
static
void DC395x_free_DCBs(struct AdapterCtlBlk* pACB)
{
	struct DeviceCtlBlk *dcb;
	struct DeviceCtlBlk *dcb_next;

	dprintkdbg(DBG_DCB, "Free %i DCBs\n", pACB->DCBCnt);

	for (dcb = pACB->pLinkDCB; dcb != NULL; dcb = dcb_next)
	{
		dcb_next = dcb->pNextDCB;
		dprintkdbg(DBG_DCB, "Free DCB (ID %i, LUN %i): %p\n",
				dcb->TargetID, dcb->TargetLUN, dcb);
		/*
		 * Free the DCB. This removes the entry from the
		 * pLinkDCB list and decrements the count in DCBCnt
		 */
		DC395x_remove_dev(pACB, dcb);

	}
}

/**
 * DC395x_release - shutdown device and release resources that were
 * allocate for it. Called once for each card as it is shutdown.
 *
 * @host: The adapter instance to shutdown.
 **/
static
void DC395x_release(struct Scsi_Host *host)
{
	struct AdapterCtlBlk *pACB = (struct AdapterCtlBlk *) (host->hostdata);
	unsigned long flags;

	dprintkl(KERN_DEBUG, "DC395x release\n");

	DC395x_LOCK_IO(pACB->pScsiHost);
	DC395x_chip_shutdown(pACB);
	DC395x_free_DCBs(pACB);

	if (host->irq != NO_IRQ) {
		free_irq(host->irq, DC395x_pACB_start);
	}
	release_region(host->io_port, host->n_io_port);

	DC395x_UNLOCK_IO(pACB->pScsiHost);
}


/*
 * SCSI host template
 */
static Scsi_Host_Template dc395x_driver_template = {
	.module                 = THIS_MODULE,
	.proc_name              = DC395X_NAME,
	.proc_info              = DC395x_proc_info,
	.name                   = DC395X_BANNER " " DC395X_VERSION,
	.queuecommand           = DC395x_queue_command,
	.bios_param             = DC395x_bios_param,
	.slave_alloc            = DC395x_slave_alloc,
	.slave_destroy          = DC395x_slave_destroy,
	.can_queue              = DC395x_MAX_CAN_QUEUE,
	.this_id                = 7,
	.sg_tablesize           = DC395x_MAX_SG_TABLESIZE,
	.cmd_per_lun            = DC395x_MAX_CMD_PER_LUN,
	.eh_abort_handler       = DC395x_eh_abort,
	.eh_bus_reset_handler   = DC395x_eh_bus_reset,
	.unchecked_isa_dma      = 0,
	.use_clustering         = DISABLE_CLUSTERING,
};

/*
 * Called to initialise a single instance of the adaptor
 */

static
int __devinit dc395x_init_one(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	unsigned int io_port;
	u8 irq;
	struct Scsi_Host *scsi_host;
	static int banner_done = 0;

	dprintkdbg(DBG_0, "Init one instance of the dc395x\n");
	if (!banner_done)
	{
		dprintkl(KERN_INFO, "%s %s\n", DC395X_BANNER, DC395X_VERSION);
		banner_done = 1;
	}

	if (pci_enable_device(pdev))
	{
		dprintkl(KERN_INFO, "PCI Enable device failed.\n");
		return -ENODEV;
	}

	dprintkdbg(DBG_0, "Get resources...\n");
	io_port = pci_resource_start(pdev, 0) & PCI_BASE_ADDRESS_IO_MASK;
	irq = pdev->irq;
	dprintkdbg(DBG_0, "IO_PORT=%04x,IRQ=%x\n", (unsigned int) io_port, irq);

	scsi_host = DC395x_init(&dc395x_driver_template, io_port, irq, DC395x_adapterCnt);
	if (!scsi_host)
	{
		dprintkdbg(DBG_0, "DC395x_init failed\n");
		return -ENOMEM;
	}

	pci_set_master(pdev);

	/* store pci devices in out host data object. */
	((struct AdapterCtlBlk *)(scsi_host->hostdata))->pdev = pdev;

	/* increment adaptor count */
	DC395x_adapterCnt++;

	/* store ptr to scsi host in the PCI device structure */
	pci_set_drvdata(pdev, scsi_host);

	/* get the scsi mid level to scan for new devices on the bus */
	scsi_add_host(scsi_host, &pdev->dev);

	return 0;
}


/*
 * Called to remove a single instance of the adaptor
 */
static void __devexit dc395x_remove_one(struct pci_dev *pdev)
{
	struct Scsi_Host *host = pci_get_drvdata(pdev);
	dprintkdbg(DBG_0, "Removing instance\n");
	scsi_remove_host(host);
	DC395x_release(host);
	pci_set_drvdata(pdev, NULL);
}

/*
 * Table which identifies the PCI devices which
 * are handled by this device driver.
 */
static struct pci_device_id dc395x_pci_table[] __devinitdata = {
	{
		.vendor		= PCI_VENDOR_ID_TEKRAM,
		.device		= PCI_DEVICE_ID_TEKRAM_TRMS1040,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	 },
	{}			/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, dc395x_pci_table);

/*
 * PCI driver operations.
 * Tells the PCI sub system what can be done with the card.
 */
static struct pci_driver dc395x_driver = {
	.name           = DC395X_NAME,
	.id_table       = dc395x_pci_table,
	.probe          = dc395x_init_one,
	.remove         = __devexit_p(dc395x_remove_one),
};

static int __init dc395x_module_init(void)
{
	return pci_module_init(&dc395x_driver);
}

static void __exit dc395x_module_exit(void)
{
	pci_unregister_driver(&dc395x_driver);
}

module_init(dc395x_module_init);
module_exit(dc395x_module_exit);

MODULE_AUTHOR("C.L. Huang / Erich Chen / Kurt Garloff");
MODULE_DESCRIPTION("SCSI host adapter driver for Tekram TRM-S1040 based adapters: Tekram DC395 and DC315 series");
MODULE_LICENSE("GPL");
