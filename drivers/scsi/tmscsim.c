/************************************************************************
 *	FILE NAME : TMSCSIM.C						*
 *	     BY   : C.L. Huang,  ching@tekram.com.tw			*
 *	Description: Device Driver for Tekram DC-390(T) PCI SCSI	*
 *		     Bus Master Host Adapter				*
 * (C)Copyright 1995-1996 Tekram Technology Co., Ltd.			*
 ************************************************************************
 * (C) Copyright: put under GNU GPL in 10/96				*
 *				(see Documentation/scsi/tmscsim.txt)	*
 ************************************************************************
 * $Id: tmscsim.c,v 2.60.2.30 2000/12/20 01:07:12 garloff Exp $		*
 *	Enhancements and bugfixes by					*
 *	Kurt Garloff <kurt@garloff.de>	<garloff@suse.de>		*
 ************************************************************************
 *	HISTORY:							*
 *									*
 *	REV#	DATE	NAME	DESCRIPTION				*
 *	1.00  96/04/24	CLH	First release				*
 *	1.01  96/06/12	CLH	Fixed bug of Media Change for Removable *
 *				Device, scan all LUN. Support Pre2.0.10 *
 *	1.02  96/06/18	CLH	Fixed bug of Command timeout ...	*
 *	1.03  96/09/25	KG	Added tmscsim_proc_info()		*
 *	1.04  96/10/11	CLH	Updating for support KV 2.0.x		*
 *	1.05  96/10/18	KG	Fixed bug in DC390_abort(null ptr deref)*
 *	1.06  96/10/25	KG	Fixed module support			*
 *	1.07  96/11/09	KG	Fixed tmscsim_proc_info()		*
 *	1.08  96/11/18	KG	Fixed null ptr in DC390_Disconnect()	*
 *	1.09  96/11/30	KG	Added register the allocated IO space	*
 *	1.10  96/12/05	CLH	Modified tmscsim_proc_info(), and reset *
 *				pending interrupt in DC390_detect()	*
 *	1.11  97/02/05	KG/CLH	Fixeds problem with partitions greater	*
 *				than 1GB				*
 *	1.12  98/02/15  MJ      Rewritten PCI probing			*
 *	1.13  98/04/08	KG	Support for non DC390, __initfunc decls,*
 *				changed max devs from 10 to 16		*
 *	1.14a 98/05/05	KG	Dynamic DCB allocation, add-single-dev	*
 *				for LUNs if LUN_SCAN (BIOS) not set	*
 *				runtime config using /proc interface	*
 *	1.14b 98/05/06	KG	eliminated cli (); sti (); spinlocks	*
 *	1.14c 98/05/07	KG	2.0.x compatibility			*
 *	1.20a 98/05/07	KG	changed names of funcs to be consistent *
 *				DC390_ (entry points), dc390_ (internal)*
 *				reworked locking			*
 *	1.20b 98/05/12	KG	bugs: version, kfree, _ctmp		*
 *				debug output				*
 *	1.20c 98/05/12	KG	bugs: kfree, parsing, EEpromDefaults	*
 *	1.20d 98/05/14	KG	bugs: list linkage, clear flag after  	*
 *				reset on startup, code cleanup		*
 *	1.20e 98/05/15	KG	spinlock comments, name space cleanup	*
 *				pLastDCB now part of ACB structure	*
 *				added stats, timeout for 2.1, TagQ bug	*
 *				RESET and INQUIRY interface commands	*
 *	1.20f 98/05/18	KG	spinlocks fixes, max_lun fix, free DCBs	*
 *				for missing LUNs, pending int		*
 *	1.20g 98/05/19	KG	Clean up: Avoid short			*
 *	1.20h 98/05/21	KG	Remove AdaptSCSIID, max_lun ...		*
 *	1.20i 98/05/21	KG	Aiiie: Bug with TagQMask       		*
 *	1.20j 98/05/24	KG	Handle STAT_BUSY, handle pACB->pLinkDCB	*
 *				== 0 in remove_dev and DoingSRB_Done	*
 *	1.20k 98/05/25	KG	DMA_INT	(experimental)	       		*
 *	1.20l 98/05/27	KG	remove DMA_INT; DMA_IDLE cmds added;	*
 *	1.20m 98/06/10	KG	glitch configurable; made some global	*
 *				vars part of ACB; use DC390_readX	*
 *	1.20n 98/06/11	KG	startup params				*
 *	1.20o 98/06/15	KG	added TagMaxNum to boot/module params	*
 *				Device Nr -> Idx, TagMaxNum power of 2  *
 *	1.20p 98/06/17	KG	Docu updates. Reset depends on settings *
 *				pci_set_master added; 2.0.xx: pcibios_*	*
 *				used instead of MechNum things ...	*
 *	1.20q 98/06/23	KG	Changed defaults. Added debug code for	*
 *				removable media and fixed it. TagMaxNum	*
 *				fixed for DC390. Locking: ACB, DRV for	*
 *				better IRQ sharing. Spelling: Queueing	*
 *				Parsing and glitch_cfg changes. Display	*
 *				real SyncSpeed value. Made DisConn	*
 *				functional (!)				*
 *	1.20r 98/06/30	KG	Debug macros, allow disabling DsCn, set	*
 *				BIT4 in CtrlR4, EN_PAGE_INT, 2.0 module	*
 *				param -1 fixed.				*
 *	1.20s 98/08/20	KG	Debug info on abort(), try to check PCI,*
 *				phys_to_bus instead of phys_to_virt,	*
 *				fixed sel. process, fixed locking,	*
 *				added MODULE_XXX infos, changed IRQ	*
 *				request flags, disable DMA_INT		*
 *	1.20t 98/09/07	KG	TagQ report fixed; Write Erase DMA Stat;*
 *				initfunc -> __init; better abort;	*
 *				Timeout for XFER_DONE & BLAST_COMPLETE;	*
 *				Allow up to 33 commands being processed *
 *	2.0a  98/10/14	KG	Max Cmnds back to 17. DMA_Stat clearing *
 *				all flags. Clear within while() loops	*
 *				in DataIn_0/Out_0. Null ptr in dumpinfo	*
 *				for pSRB==0. Better locking during init.*
 *				bios_param() now respects part. table.	*
 *	2.0b  98/10/24	KG	Docu fixes. Timeout Msg in DMA Blast.	*
 *				Disallow illegal idx in INQUIRY/REMOVE	*
 *	2.0c  98/11/19	KG	Cleaned up detect/init for SMP boxes, 	*
 *				Write Erase DMA (1.20t) caused problems	*
 *	2.0d  98/12/25	KG	Christmas release ;-) Message handling  *
 *				completely reworked. Handle target ini-	*
 *				tiated SDTR correctly.			*
 *	2.0d1 99/01/25	KG	Try to handle RESTORE_PTR		*
 *	2.0d2 99/02/08	KG	Check for failure of kmalloc, correct 	*
 *				inclusion of scsicam.h, DelayReset	*
 *	2.0d3 99/05/31	KG	DRIVER_OK -> DID_OK, DID_NO_CONNECT,	*
 *				detect Target mode and warn.		*
 *				pcmd->result handling cleaned up.	*
 *	2.0d4 99/06/01	KG	Cleaned selection process. Found bug	*
 *				which prevented more than 16 tags. Now:	*
 *				24. SDTR cleanup. Cleaner multi-LUN	*
 *				handling. Don't modify ControlRegs/FIFO	*
 *				when connected.				*
 *	2.0d5 99/06/01	KG	Clear DevID, Fix INQUIRY after cfg chg.	*
 *	2.0d6 99/06/02	KG	Added ADD special command to allow cfg.	*
 *				before detection. Reset SYNC_NEGO_DONE	*
 *				after a bus reset.			*
 *	2.0d7 99/06/03	KG	Fixed bugs wrt add,remove commands	*
 *	2.0d8 99/06/04	KG	Removed copying of cmnd into CmdBlock.	*
 *				Fixed Oops in _release().		*
 *	2.0d9 99/06/06	KG	Also tag queue INQUIRY, T_U_R, ...	*
 *				Allow arb. no. of Tagged Cmnds. Max 32	*
 *	2.0d1099/06/20	KG	TagMaxNo changes now honoured! Queueing *
 *				clearified (renamed ..) TagMask handling*
 *				cleaned.				*
 *	2.0d1199/06/28	KG	cmd->result now identical to 2.0d2	*
 *	2.0d1299/07/04	KG	Changed order of processing in IRQ	*
 *	2.0d1399/07/05	KG	Don't update DCB fields if removed	*
 *	2.0d1499/07/05	KG	remove_dev: Move kfree() to the end	*
 *	2.0d1599/07/12	KG	use_new_eh_code: 0, ULONG -> UINT where	*
 *				appropriate				*
 *	2.0d1699/07/13	KG	Reenable StartSCSI interrupt, Retry msg	*
 *	2.0d1799/07/15	KG	Remove debug msg. Disable recfg. when	*
 *				there are queued cmnds			*
 *	2.0d1899/07/18	KG	Selection timeout: Don't requeue	*
 *	2.0d1999/07/18	KG	Abort: Only call scsi_done if dequeued	*
 *	2.0d2099/07/19	KG	Rst_Detect: DoingSRB_Done		*
 *	2.0d2199/08/15	KG	dev_id for request/free_irq, cmnd[0] for*
 *				RETRY, SRBdone does DID_ABORT for the 	*
 *				cmd passed by DC390_reset()		*
 *	2.0d2299/08/25	KG	dev_id fixed. can_queue: 42		*
 *	2.0d2399/08/25	KG	Removed some debugging code. dev_id 	*
 *				now is set to pACB. Use u8,u16,u32. 	*
 *	2.0d2499/11/14	KG	Unreg. I/O if failed IRQ alloc. Call	*
 * 				done () w/ DID_BAD_TARGET in case of	*
 *				missing DCB. We	are old EH!!		*
 *	2.0d2500/01/15	KG	2.3.3x compat from Andreas Schultz	*
 *				set unique_id. Disable RETRY message.	*
 *	2.0d2600/01/29	KG	Go to new EH.				*
 *	2.0d2700/01/31	KG	... but maintain 2.0 compat.		*
 *				and fix DCB freeing			*
 *	2.0d2800/02/14	KG	Queue statistics fixed, dump special cmd*
 *				Waiting_Timer for failed StartSCSI	*
 *				New EH: Don't return cmnds to ML on RST *
 *				Use old EH (don't have new EH fns yet)	*
 * 				Reset: Unlock, but refuse to queue	*
 * 				2.3 __setup function			*
 *	2.0e  00/05/22	KG	Return residual for 2.3			*
 *	2.0e1 00/05/25	KG	Compile fixes for 2.3.99		*
 *	2.0e2 00/05/27	KG	Jeff Garzik's pci_enable_device()	*
 *	2.0e3 00/09/29	KG	Some 2.4 changes. Don't try Sync Nego	*
 *				before INQUIRY has reported ability. 	*
 *				Recognise INQUIRY as scanning command.	*
 *	2.0e4 00/10/13	KG	Allow compilation into 2.4 kernel	*
 *	2.0e5 00/11/17	KG	Store Inq.flags in DCB			*
 *	2.0e6 00/11/22  KG	2.4 init function (Thx to O.Schumann)	*
 * 				2.4 PCI device table (Thx to A.Richter)	*
 *	2.0e7 00/11/28	KG	Allow overriding of BIOS settings	*
 *	2.0f  00/12/20	KG	Handle failed INQUIRYs during scan	*
 *	2.1a  03/11/29  GL, KG	Initial fixing for 2.6. Convert to	*
 *				use the current PCI-mapping API, update	*
 *				command-queuing.			*
 *	2.1b  04/04/13  GL	Fix for 64-bit platforms		*
 *	2.1b1 04/01/31	GL	(applied 05.04) Remove internal		*
 *				command-queuing.			*
 *	2.1b2 04/02/01	CH	(applied 05.04) Fix error-handling	*
 ***********************************************************************/

/* Uncomment SA_INTERRUPT, if the driver refuses to share its IRQ with other devices */
#define DC390_IRQ SA_SHIRQ /* | SA_INTERRUPT */

/* DEBUG options */
//#define DC390_DEBUG0
//#define DC390_DEBUG1
//#define DC390_DCBDEBUG
//#define DC390_PARSEDEBUG
//#define DC390_REMOVABLEDEBUG
//#define DC390_LOCKDEBUG

//#define NOP do{}while(0)
#define C_NOP

/* Debug definitions */
#ifdef DC390_DEBUG0
# define DEBUG0(x) x
#else
# define DEBUG0(x) C_NOP
#endif
#ifdef DC390_DEBUG1
# define DEBUG1(x) x
#else
# define DEBUG1(x) C_NOP
#endif
#ifdef DC390_DCBDEBUG
# define DCBDEBUG(x) x
#else
# define DCBDEBUG(x) C_NOP
#endif
#ifdef DC390_PARSEDEBUG
# define PARSEDEBUG(x) x
#else
# define PARSEDEBUG(x) C_NOP
#endif
#ifdef DC390_REMOVABLEDEBUG
# define REMOVABLEDEBUG(x) x
#else
# define REMOVABLEDEBUG(x) C_NOP
#endif
#define DCBDEBUG1(x) C_NOP

/* Includes */
#include <linux/module.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/system.h>
#include <linux/delay.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/blkdev.h>
#include <linux/timer.h>
#include <linux/interrupt.h>

#include "scsi.h"
#include "hosts.h"
#include <linux/stat.h>
#include <scsi/scsicam.h>

#include "dc390.h"

#define PCI_DEVICE_ID_AMD53C974 	PCI_DEVICE_ID_AMD_SCSI

/* Locking */

/* Note: Starting from 2.1.9x, the mid-level scsi code issues a 
 * spinlock_irqsave (&io_request_lock) before calling the driver's 
 * routines, so we don't need to lock, except in the IRQ handler.
 * The policy 3, let the midlevel scsi code do the io_request_locks
 * and us locking on a driver specific lock, shouldn't hurt anybody; it
 * just causes a minor performance degradation for setting the locks.
 */

/* spinlock things
 * level 3: lock on both adapter specific locks and (global) io_request_lock
 * level 2: lock on adapter specific locks only
 * level 1: rely on the locking of the mid level code (io_request_lock)
 * undef  : traditional save_flags; cli; restore_flags;
 */

#include <linux/init.h>
#include <linux/spinlock.h>

static struct pci_device_id tmscsim_pci_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_AMD,
		.device		= PCI_DEVICE_ID_AMD53C974,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
	},
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, tmscsim_pci_tbl);

#define USE_SPINLOCKS 1

#define DC390_IFLAGS unsigned long iflags
#define DC390_LOCK_IO(dev) spin_lock_irqsave (((struct Scsi_Host *)dev)->host_lock, iflags)
#define DC390_UNLOCK_IO(dev) spin_unlock_irqrestore (((struct Scsi_Host *)dev)->host_lock, iflags)

/* These macros are used for uniform access to 2.0.x and 2.1.x PCI config space*/

#define PDEV pdev
#define PDEVDECL struct pci_dev *pdev
#define PDEVDECL0 struct pci_dev *pdev = NULL
#define PDEVDECL1 struct pci_dev *pdev
#define PDEVSET pACB->pdev=pdev
#define PDEVSET1 pdev=pACB->pdev
#define PCI_WRITE_CONFIG_BYTE(pd, rv, bv) pci_write_config_byte (pd, rv, bv)
#define PCI_READ_CONFIG_BYTE(pd, rv, bv) pci_read_config_byte (pd, rv, bv)
#define PCI_WRITE_CONFIG_WORD(pd, rv, bv) pci_write_config_word (pd, rv, bv)
#define PCI_READ_CONFIG_WORD(pd, rv, bv) pci_read_config_word (pd, rv, bv)
#define PCI_PRESENT (1)
#define PCI_GET_IO_AND_IRQ do{io_port = pci_resource_start (pdev, 0); irq = pdev->irq;} while(0)

#include "tmscsim.h"

#ifndef __init
# define __init
#endif

static UCHAR dc390_StartSCSI( PACB pACB, PDCB pDCB, PSRB pSRB );
static void dc390_DataOut_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_DataIn_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_Command_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_Status_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_MsgOut_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_MsgIn_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_DataOutPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_DataInPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_CommandPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_StatusPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_MsgOutPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_MsgInPhase( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_Nop_0( PACB pACB, PSRB pSRB, PUCHAR psstatus);
static void dc390_Nop_1( PACB pACB, PSRB pSRB, PUCHAR psstatus);

static void dc390_SetXferRate( PACB pACB, PDCB pDCB );
static void dc390_Disconnect( PACB pACB );
static void dc390_Reselect( PACB pACB );
static void dc390_SRBdone( PACB pACB, PDCB pDCB, PSRB pSRB );
static void dc390_DoingSRB_Done( PACB pACB, PSCSICMD cmd );
static void dc390_ScsiRstDetect( PACB pACB );
static void dc390_ResetSCSIBus( PACB pACB );
static void __inline__ dc390_RequestSense( PACB pACB, PDCB pDCB, PSRB pSRB );
static void __inline__ dc390_InvalidCmd( PACB pACB );
static void __inline__ dc390_EnableMsgOut_Abort (PACB, PSRB);
static void dc390_remove_dev (PACB pACB, PDCB pDCB);
static irqreturn_t do_DC390_Interrupt( int, void *, struct pt_regs *);

static int    dc390_initAdapter( PSH psh, ULONG io_port, UCHAR Irq, UCHAR index );
static void   dc390_initDCB( PACB pACB, PDCB *ppDCB, UCHAR id, UCHAR lun);
static void   dc390_updateDCB (PACB pACB, PDCB pDCB);

static int DC390_release(struct Scsi_Host *host);
static int dc390_shutdown (struct Scsi_Host *host);
static int DC390_proc_info (struct Scsi_Host *shpnt, char *buffer, char **start,
			    off_t offset, int length, int inout);

static PACB	dc390_pACB_start= NULL;
static PACB	dc390_pACB_current = NULL;
static ULONG	dc390_lastabortedpid = 0;
static UINT	dc390_laststatus = 0;
static UCHAR	dc390_adapterCnt = 0;

/* Startup values, to be overriden on the commandline */
static int tmscsim[] = {-2, -2, -2, -2, -2, -2};
static int tmscsim_paramnum = ARRAY_SIZE(tmscsim);

module_param_array(tmscsim, int, tmscsim_paramnum, 0);
MODULE_PARM_DESC(tmscsim, "Host SCSI ID, Speed (0=10MHz), Device Flags, Adapter Flags, Max Tags (log2(tags)-1), DelayReset (s)");
MODULE_AUTHOR("C.L. Huang / Kurt Garloff");
MODULE_DESCRIPTION("SCSI host adapter driver for Tekram DC390 and other AMD53C974A based PCI SCSI adapters");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("sd,sr,sg,st");

static PVOID dc390_phase0[]={
       dc390_DataOut_0,
       dc390_DataIn_0,
       dc390_Command_0,
       dc390_Status_0,
       dc390_Nop_0,
       dc390_Nop_0,
       dc390_MsgOut_0,
       dc390_MsgIn_0,
       dc390_Nop_1
       };

static PVOID dc390_phase1[]={
       dc390_DataOutPhase,
       dc390_DataInPhase,
       dc390_CommandPhase,
       dc390_StatusPhase,
       dc390_Nop_0,
       dc390_Nop_0,
       dc390_MsgOutPhase,
       dc390_MsgInPhase,
       dc390_Nop_1
       };

#ifdef DC390_DEBUG1
static char* dc390_p0_str[] = {
       "dc390_DataOut_0",
       "dc390_DataIn_0",
       "dc390_Command_0",
       "dc390_Status_0",
       "dc390_Nop_0",
       "dc390_Nop_0",
       "dc390_MsgOut_0",
       "dc390_MsgIn_0",
       "dc390_Nop_1"
       };
     
static char* dc390_p1_str[] = {
       "dc390_DataOutPhase",
       "dc390_DataInPhase",
       "dc390_CommandPhase",
       "dc390_StatusPhase",
       "dc390_Nop_0",
       "dc390_Nop_0",
       "dc390_MsgOutPhase",
       "dc390_MsgInPhase",
       "dc390_Nop_1"
       };
#endif   

/* Devices erroneously pretending to be able to do TagQ */
static UCHAR  dc390_baddevname1[2][28] ={
       "SEAGATE ST3390N         9546",
       "HP      C3323-300       4269"};
#define BADDEVCNT	2

static char*  dc390_adapname = "DC390";
static UCHAR  dc390_eepromBuf[MAX_ADAPTER_NUM][EE_LEN];
static UCHAR  dc390_clock_period1[] = {4, 5, 6, 7, 8, 10, 13, 20};
static UCHAR  dc390_clock_speed[] = {100,80,67,57,50, 40, 31, 20};

/***********************************************************************
 * Functions for access to DC390 EEPROM
 * and some to emulate it
 *
 **********************************************************************/


static void __init dc390_EnDisableCE( UCHAR mode, PDEVDECL, PUCHAR regval )
{
    UCHAR bval;

    bval = 0;
    if(mode == ENABLE_CE)
	*regval = 0xc0;
    else
	*regval = 0x80;
    PCI_WRITE_CONFIG_BYTE(PDEV, *regval, bval);
    if(mode == DISABLE_CE)
        PCI_WRITE_CONFIG_BYTE(PDEV, *regval, bval);
    udelay(160);
}


/* Override EEprom values with explicitly set values */
static void __init dc390_EEprom_Override (UCHAR index)
{
    PUCHAR ptr;
    UCHAR  id;
    ptr = (PUCHAR) dc390_eepromBuf[index];
    
    /* Adapter Settings */
    if (tmscsim[0] != -2)
	ptr[EE_ADAPT_SCSI_ID] = (UCHAR)tmscsim[0];	/* Adapter ID */
    if (tmscsim[3] != -2)
	ptr[EE_MODE2] = (UCHAR)tmscsim[3];
    if (tmscsim[5] != -2)
	ptr[EE_DELAY] = tmscsim[5];			/* Reset delay */
    if (tmscsim[4] != -2)
	ptr[EE_TAG_CMD_NUM] = (UCHAR)tmscsim[4];	/* Tagged Cmds */
    
    /* Device Settings */
    for (id = 0; id < MAX_SCSI_ID; id++)
    {
	if (tmscsim[2] != -2)
		ptr[id<<2] = (UCHAR)tmscsim[2];		/* EE_MODE1 */
	if (tmscsim[1] != -2)
		ptr[(id<<2) + 1] = (UCHAR)tmscsim[1];	/* EE_Speed */
    }
}

/* Handle "-1" case */
static void __init dc390_check_for_safe_settings (void)
{
	if (tmscsim[0] == -1 || tmscsim[0] > 15) /* modules-2.0.0 passes -1 as string */
	{
		tmscsim[0] = 7; tmscsim[1] = 4;
		tmscsim[2] = 0x09; tmscsim[3] = 0x0f;
		tmscsim[4] = 2; tmscsim[5] = 10;
		printk (KERN_INFO "DC390: Using safe settings.\n");
	}
}


static int __initdata tmscsim_def[] = {7, 0 /* 10MHz */,
		PARITY_CHK_ | SEND_START_ | EN_DISCONNECT_
		| SYNC_NEGO_ | TAG_QUEUEING_,
		MORE2_DRV | GREATER_1G | RST_SCSI_BUS | ACTIVE_NEGATION
		/* | NO_SEEK */
# ifdef CONFIG_SCSI_MULTI_LUN
		| LUN_CHECK
# endif
		, 3 /* 16 Tags per LUN */, 1 /* s delay after Reset */ };

/* Copy defaults over set values where missing */
static void __init dc390_fill_with_defaults (void)
{
	int i;
	PARSEDEBUG(printk(KERN_INFO "DC390: setup %08x %08x %08x %08x %08x %08x\n", tmscsim[0],\
			  tmscsim[1], tmscsim[2], tmscsim[3], tmscsim[4], tmscsim[5]));
	for (i = 0; i < 6; i++)
	{
		if (tmscsim[i] < 0 || tmscsim[i] > 255)
			tmscsim[i] = tmscsim_def[i];
	}
	/* Sanity checks */
	if (tmscsim[0] >   7) tmscsim[0] =   7;
	if (tmscsim[1] >   7) tmscsim[1] =   4;
	if (tmscsim[4] >   5) tmscsim[4] =   4;
	if (tmscsim[5] > 180) tmscsim[5] = 180;
}

/* Override defaults on cmdline:
 * tmscsim: AdaptID, MaxSpeed (Index), DevMode (Bitmapped), AdaptMode (Bitmapped)
 */
static int __init dc390_setup (char *str)
{	
	int ints[8];
	int i, im;
	(void)get_options (str, ARRAY_SIZE(ints), ints);
	im = ints[0];
	if (im > 6)
	{
		printk (KERN_NOTICE "DC390: ignore extra params!\n");
		im = 6;
	}
	for (i = 0; i < im; i++)
		tmscsim[i] = ints[i+1];
	/* dc390_checkparams (); */
	return 1;
}
#ifndef MODULE
__setup("tmscsim=", dc390_setup);
#endif

static void __init dc390_EEpromOutDI( PDEVDECL, PUCHAR regval, UCHAR Carry )
{
    UCHAR bval;

    bval = 0;
    if(Carry)
    {
	bval = 0x40;
	*regval = 0x80;
	PCI_WRITE_CONFIG_BYTE(PDEV, *regval, bval);
    }
    udelay(160);
    bval |= 0x80;
    PCI_WRITE_CONFIG_BYTE(PDEV, *regval, bval);
    udelay(160);
    bval = 0;
    PCI_WRITE_CONFIG_BYTE(PDEV, *regval, bval);
    udelay(160);
}


static UCHAR __init dc390_EEpromInDO( PDEVDECL )
{
    UCHAR bval;

    PCI_WRITE_CONFIG_BYTE(PDEV, 0x80, 0x80);
    udelay(160);
    PCI_WRITE_CONFIG_BYTE(PDEV, 0x80, 0x40);
    udelay(160);
    PCI_READ_CONFIG_BYTE(PDEV, 0x00, &bval);
    if(bval == 0x22)
	return(1);
    else
	return(0);
}


static USHORT __init dc390_EEpromGetData1( PDEVDECL )
{
    UCHAR i;
    UCHAR carryFlag;
    USHORT wval;

    wval = 0;
    for(i=0; i<16; i++)
    {
	wval <<= 1;
	carryFlag = dc390_EEpromInDO(PDEV);
	wval |= carryFlag;
    }
    return(wval);
}


static void __init dc390_Prepare( PDEVDECL, PUCHAR regval, UCHAR EEpromCmd )
{
    UCHAR i,j;
    UCHAR carryFlag;

    carryFlag = 1;
    j = 0x80;
    for(i=0; i<9; i++)
    {
	dc390_EEpromOutDI(PDEV,regval,carryFlag);
	carryFlag = (EEpromCmd & j) ? 1 : 0;
	j >>= 1;
    }
}


static void __init dc390_ReadEEprom( PDEVDECL, PUSHORT ptr)
{
    UCHAR   regval,cmd;
    UCHAR   i;

    cmd = EEPROM_READ;
    for(i=0; i<0x40; i++)
    {
	dc390_EnDisableCE(ENABLE_CE, PDEV, &regval);
	dc390_Prepare(PDEV, &regval, cmd++);
	*ptr++ = dc390_EEpromGetData1(PDEV);
	dc390_EnDisableCE(DISABLE_CE, PDEV, &regval);
    }
}


static void __init dc390_interpret_delay (UCHAR index)
{
    char interpd [] = {1,3,5,10,16,30,60,120};
    dc390_eepromBuf[index][EE_DELAY] = interpd [dc390_eepromBuf[index][EE_DELAY]];
}

static UCHAR __init dc390_CheckEEpromCheckSum( PDEVDECL, UCHAR index )
{
    UCHAR  i;
    char  EEbuf[128];
    USHORT wval, *ptr = (PUSHORT)EEbuf;

    dc390_ReadEEprom( PDEV, ptr );
    memcpy (dc390_eepromBuf[index], EEbuf, EE_ADAPT_SCSI_ID);
    memcpy (&dc390_eepromBuf[index][EE_ADAPT_SCSI_ID], 
	    &EEbuf[REAL_EE_ADAPT_SCSI_ID], EE_LEN - EE_ADAPT_SCSI_ID);
    dc390_interpret_delay (index);
    
    wval = 0;
    for(i=0; i<0x40; i++, ptr++)
	wval += *ptr;
    return (wval == 0x1234 ? 0 : 1);
}


/***********************************************************************
 * Functions for the management of the internal structures 
 * (DCBs, SRBs, Queueing)
 *
 **********************************************************************/
static PDCB __inline__ dc390_findDCB ( PACB pACB, UCHAR id, UCHAR lun)
{
   PDCB pDCB = pACB->pLinkDCB; if (!pDCB) return 0;
   while (pDCB->TargetID != id || pDCB->TargetLUN != lun)
     {
	pDCB = pDCB->pNextDCB;
	if (pDCB == pACB->pLinkDCB)
	  {
	     DCBDEBUG(printk (KERN_WARNING "DC390: DCB not found (DCB=%p, DCBmap[%2x]=%2x)\n",
			      pDCB, id, pACB->DCBmap[id]));
	     return 0;
	  }
     }
   DCBDEBUG1( printk (KERN_DEBUG "DCB %p (%02x,%02x) found.\n",	\
		      pDCB, pDCB->TargetID, pDCB->TargetLUN));
   return pDCB;
}

/* Queueing philosphy:
 * There are a couple of lists:
 * - Query: Contains the Scsi Commands not yet turned into SRBs (per ACB)
 *   (Note: For new EH, it is unnecessary!)
 * - Waiting: Contains a list of SRBs not yet sent (per DCB)
 * - Free: List of free SRB slots
 * 
 * If there are no waiting commands for the DCB, the new one is sent to the bus
 * otherwise the oldest one is taken from the Waiting list and the new one is 
 * queued to the Waiting List
 * 
 * Lists are managed using two pointers and eventually a counter
 */


#if 0
/* Look for a SCSI cmd in a SRB queue */
static PSRB dc390_find_cmd_in_SRBq (PSCSICMD cmd, PSRB queue)
{
    PSRB q = queue;
    while (q)
    {
	if (q->pcmd == cmd) return q;
	q = q->pNextSRB;
	if (q == queue) return 0;
    }
    return q;
}
#endif

/* Return next free SRB */
static __inline__ PSRB dc390_Free_get ( PACB pACB )
{
    PSRB   pSRB;

    pSRB = pACB->pFreeSRB;
    DEBUG0(printk ("DC390: Get Free SRB %p\n", pSRB));
    if( pSRB )
    {
	pACB->pFreeSRB = pSRB->pNextSRB;
	pSRB->pNextSRB = NULL;
    }

    return( pSRB );
}

/* Insert SRB oin top of free list */
static __inline__ void dc390_Free_insert (PACB pACB, PSRB pSRB)
{
    DEBUG0(printk ("DC390: Free SRB %p\n", pSRB));
    pSRB->pNextSRB = pACB->pFreeSRB;
    pACB->pFreeSRB = pSRB;
}


/* Inserts a SRB to the top of the Waiting list */
static __inline__ void dc390_Waiting_insert ( PDCB pDCB, PSRB pSRB )
{
    DEBUG0(printk ("DC390: Insert pSRB %p cmd %li to Waiting\n", pSRB, pSRB->pcmd->pid));
    pSRB->pNextSRB = pDCB->pWaitingSRB;
    if (!pDCB->pWaitingSRB)
	pDCB->pWaitLast = pSRB;
    pDCB->pWaitingSRB = pSRB;
    pDCB->WaitSRBCnt++;
}


/* Queue SRB to waiting list */
static __inline__ void dc390_Waiting_append ( PDCB pDCB, PSRB pSRB)
{
	DEBUG0(printk ("DC390: Append pSRB %p cmd %li to Waiting\n", pSRB, pSRB->pcmd->pid));
    if( pDCB->pWaitingSRB )
	pDCB->pWaitLast->pNextSRB = pSRB;
    else
	pDCB->pWaitingSRB = pSRB;

    pDCB->pWaitLast = pSRB;
    pSRB->pNextSRB = NULL;
    pDCB->WaitSRBCnt++;
    pDCB->pDCBACB->CmdInQ++;
}

static __inline__ void dc390_Going_append (PDCB pDCB, PSRB pSRB)
{
    pDCB->GoingSRBCnt++;
    DEBUG0(printk("DC390: Append SRB %p to Going\n", pSRB));
    /* Append to the list of Going commands */
    if( pDCB->pGoingSRB )
	pDCB->pGoingLast->pNextSRB = pSRB;
    else
	pDCB->pGoingSRB = pSRB;

    pDCB->pGoingLast = pSRB;
    /* No next one in sent list */
    pSRB->pNextSRB = NULL;
}

static __inline__ void dc390_Going_remove (PDCB pDCB, PSRB pSRB)
{
	DEBUG0(printk("DC390: Remove SRB %p from Going\n", pSRB));
   if (pSRB == pDCB->pGoingSRB)
	pDCB->pGoingSRB = pSRB->pNextSRB;
   else
     {
	PSRB psrb = pDCB->pGoingSRB;
	while (psrb && psrb->pNextSRB != pSRB)
	  psrb = psrb->pNextSRB;
	if (!psrb) 
	  { printk (KERN_ERR "DC390: Remove non-ex. SRB %p from Going!\n", pSRB); return; }
	psrb->pNextSRB = pSRB->pNextSRB;
	if (pSRB == pDCB->pGoingLast)
	  pDCB->pGoingLast = psrb;
     }
   pDCB->GoingSRBCnt--;
}

/* Moves SRB from Going list to the top of Waiting list */
static void dc390_Going_to_Waiting ( PDCB pDCB, PSRB pSRB )
{
    DEBUG0(printk(KERN_INFO "DC390: Going_to_Waiting (SRB %p) pid = %li\n", pSRB, pSRB->pcmd->pid));
    /* Remove SRB from Going */
    dc390_Going_remove (pDCB, pSRB);
    /* Insert on top of Waiting */
    dc390_Waiting_insert (pDCB, pSRB);
    /* Tag Mask must be freed elsewhere ! (KG, 99/06/18) */
}

/* Moves first SRB from Waiting list to Going list */
static __inline__ void dc390_Waiting_to_Going ( PDCB pDCB, PSRB pSRB )
{	
	/* Remove from waiting list */
	DEBUG0(printk("DC390: Remove SRB %p from head of Waiting\n", pSRB));
	pDCB->pWaitingSRB = pSRB->pNextSRB;
	if( !pDCB->pWaitingSRB ) pDCB->pWaitLast = NULL;
	pDCB->WaitSRBCnt--;
	dc390_Going_append (pDCB, pSRB);
}

static void DC390_waiting_timed_out (unsigned long ptr);
/* Sets the timer to wake us up */
static void dc390_waiting_timer (PACB pACB, unsigned long to)
{
	if (timer_pending (&pACB->Waiting_Timer)) return;
	init_timer (&pACB->Waiting_Timer);
	pACB->Waiting_Timer.function = DC390_waiting_timed_out;
	pACB->Waiting_Timer.data = (unsigned long)pACB;
	if (time_before (jiffies + to, pACB->pScsiHost->last_reset))
		pACB->Waiting_Timer.expires = pACB->pScsiHost->last_reset + 1;
	else
		pACB->Waiting_Timer.expires = jiffies + to + 1;
	add_timer (&pACB->Waiting_Timer);
}


/* Send the next command from the waiting list to the bus */
static void dc390_Waiting_process ( PACB pACB )
{
    PDCB   ptr, ptr1;
    PSRB   pSRB;

    if( (pACB->pActiveDCB) || (pACB->ACBFlag & (RESET_DETECT+RESET_DONE+RESET_DEV) ) )
	return;
    if (timer_pending (&pACB->Waiting_Timer)) del_timer (&pACB->Waiting_Timer);
    ptr = pACB->pDCBRunRobin;
    if( !ptr )
      {
	ptr = pACB->pLinkDCB;
	pACB->pDCBRunRobin = ptr;
      }
    ptr1 = ptr;
    if (!ptr1) return;
    do 
      {
	pACB->pDCBRunRobin = ptr1->pNextDCB;
	if( !( pSRB = ptr1->pWaitingSRB ) ||
	    ( ptr1->MaxCommand <= ptr1->GoingSRBCnt ))
	  ptr1 = ptr1->pNextDCB;
	else
	  {
	    /* Try to send to the bus */
	    if( !dc390_StartSCSI(pACB, ptr1, pSRB) )
	      dc390_Waiting_to_Going (ptr1, pSRB);
	    else
	      dc390_waiting_timer (pACB, HZ/5);
	    break;
	  }
      } while (ptr1 != ptr);
    return;
}

/* Wake up waiting queue */
static void DC390_waiting_timed_out (unsigned long ptr)
{
	PACB pACB = (PACB)ptr;
	DC390_IFLAGS;
	DEBUG0(printk ("DC390: Debug: Waiting queue woken up by timer!\n"));
	DC390_LOCK_IO(pACB->pScsiHost);
	dc390_Waiting_process (pACB);
	DC390_UNLOCK_IO(pACB->pScsiHost);
}

/***********************************************************************
 * Function: static void dc390_SendSRB (PACB pACB, PSRB pSRB)
 *
 * Purpose: Send SCSI Request Block (pSRB) to adapter (pACB)
 *
 ***********************************************************************/

static void dc390_SendSRB( PACB pACB, PSRB pSRB )
{
    PDCB   pDCB;

    pDCB = pSRB->pSRBDCB;
    if( (pDCB->MaxCommand <= pDCB->GoingSRBCnt) || (pACB->pActiveDCB) ||
	(pACB->ACBFlag & (RESET_DETECT+RESET_DONE+RESET_DEV)) )
    {
	dc390_Waiting_append (pDCB, pSRB);
	dc390_Waiting_process (pACB);
	return;
    }

#if 0
    if( pDCB->pWaitingSRB )
    {
	dc390_Waiting_append (pDCB, pSRB);
/*	pSRB = GetWaitingSRB(pDCB); */	/* non-existent */
	pSRB = pDCB->pWaitingSRB;
	/* Remove from waiting list */
	pDCB->pWaitingSRB = pSRB->pNextSRB;
	pSRB->pNextSRB = NULL;
	if (!pDCB->pWaitingSRB) pDCB->pWaitLast = NULL;
    }
#endif
	
    if (!dc390_StartSCSI(pACB, pDCB, pSRB))
	dc390_Going_append (pDCB, pSRB);
    else {
	dc390_Waiting_insert (pDCB, pSRB);
	dc390_waiting_timer (pACB, HZ/5);
    }
}


/* Create pci mapping */
static int dc390_pci_map (PSRB pSRB)
{
	int error = 0;
	Scsi_Cmnd *pcmd = pSRB->pcmd;
	struct pci_dev *pdev = pSRB->pSRBDCB->pDCBACB->pdev;
	dc390_cmd_scp_t* cmdp = ((dc390_cmd_scp_t*)(&pcmd->SCp));
	/* Map sense buffer */
	if (pSRB->SRBFlag & AUTO_REQSENSE) {
		sg_dma_address(&pSRB->Segmentx) = cmdp->saved_dma_handle = 
			pci_map_page(pdev, virt_to_page(pcmd->sense_buffer),
				     (unsigned long)pcmd->sense_buffer & ~PAGE_MASK, sizeof(pcmd->sense_buffer),
				     DMA_FROM_DEVICE);
		sg_dma_len(&pSRB->Segmentx) = sizeof(pcmd->sense_buffer);
		pSRB->SGcount = 1;
		pSRB->pSegmentList = (PSGL) &pSRB->Segmentx;
		DEBUG1(printk("%s(): Mapped sense buffer %p at %x\n", __FUNCTION__, pcmd->sense_buffer, cmdp->saved_dma_handle));
	/* Make SG list */	
	} else if (pcmd->use_sg) {
		pSRB->pSegmentList = (PSGL) pcmd->request_buffer;
		pSRB->SGcount = pci_map_sg(pdev, pSRB->pSegmentList,
					   pcmd->use_sg,
					   scsi_to_pci_dma_dir(pcmd->sc_data_direction));
		/* TODO: error handling */
		if (!pSRB->SGcount)
			error = 1;
		DEBUG1(printk("%s(): Mapped SG %p with %d (%d) elements\n", __FUNCTION__, pcmd->request_buffer, pSRB->SGcount, pcmd->use_sg));
	/* Map single segment */
	} else if (pcmd->request_buffer && pcmd->request_bufflen) {
		sg_dma_address(&pSRB->Segmentx) = cmdp->saved_dma_handle =
			pci_map_page(pdev, virt_to_page(pcmd->request_buffer),
				     (unsigned long)pcmd->request_buffer & ~PAGE_MASK,
				     pcmd->request_bufflen, scsi_to_pci_dma_dir(pcmd->sc_data_direction));
		/* TODO: error handling */
		sg_dma_len(&pSRB->Segmentx) = pcmd->request_bufflen;
		pSRB->SGcount = 1;
		pSRB->pSegmentList = (PSGL) &pSRB->Segmentx;
		DEBUG1(printk("%s(): Mapped request buffer %p at %x\n", __FUNCTION__, pcmd->request_buffer, cmdp->saved_dma_handle));
	/* No mapping !? */	
    	} else
		pSRB->SGcount = 0;
	return error;
}

/* Remove pci mapping */
static void dc390_pci_unmap (PSRB pSRB)
{
	Scsi_Cmnd* pcmd = pSRB->pcmd;
	struct pci_dev *pdev = pSRB->pSRBDCB->pDCBACB->pdev;
	dc390_cmd_scp_t* cmdp = ((dc390_cmd_scp_t*)(&pcmd->SCp));

	if (pSRB->SRBFlag) {
		pci_unmap_page(pdev, cmdp->saved_dma_handle,
			       sizeof(pcmd->sense_buffer), DMA_FROM_DEVICE);
		DEBUG1(printk("%s(): Unmapped sense buffer at %x\n", __FUNCTION__, cmdp->saved_dma_handle));
	} else if (pcmd->use_sg) {
		pci_unmap_sg(pdev, pcmd->request_buffer, pcmd->use_sg,
			     scsi_to_pci_dma_dir(pcmd->sc_data_direction));
		DEBUG1(printk("%s(): Unmapped SG at %p with %d elements\n", __FUNCTION__, pcmd->request_buffer, pcmd->use_sg));
	} else if (pcmd->request_buffer && pcmd->request_bufflen) {
		pci_unmap_page(pdev,
			       cmdp->saved_dma_handle,
			       pcmd->request_bufflen,
			       scsi_to_pci_dma_dir(pcmd->sc_data_direction));
		DEBUG1(printk("%s(): Unmapped request buffer at %x\n", __FUNCTION__, cmdp->saved_dma_handle));
	}
}


/***********************************************************************
 * Function: static void dc390_BuildSRB (Scsi_Cmd *pcmd, PDCB pDCB, 
 * 					 PSRB pSRB)
 *
 * Purpose: Prepare SRB for being sent to Device DCB w/ command *pcmd
 *
 ***********************************************************************/

static void dc390_BuildSRB (Scsi_Cmnd* pcmd, PDCB pDCB, PSRB pSRB)
{
    pSRB->pSRBDCB = pDCB;
    pSRB->pcmd = pcmd;
    //pSRB->ScsiCmdLen = pcmd->cmd_len;
    //memcpy (pSRB->CmdBlock, pcmd->cmnd, pcmd->cmd_len);
    
    pSRB->SGIndex = 0;
    pSRB->AdaptStatus = 0;
    pSRB->TargetStatus = 0;
    pSRB->MsgCnt = 0;
    if( pDCB->DevType != TYPE_TAPE )
	pSRB->RetryCnt = 1;
    else
	pSRB->RetryCnt = 0;
    pSRB->SRBStatus = 0;
    pSRB->SRBFlag = 0;
    pSRB->SRBState = 0;
    pSRB->TotalXferredLen = 0;
    pSRB->SGBusAddr = 0;
    pSRB->SGToBeXferLen = 0;
    pSRB->ScsiPhase = 0;
    pSRB->EndMessage = 0;
    pSRB->TagNumber = 255;
    /* KG: deferred PCI mapping to dc390_StartSCSI */
}

/***********************************************************************
 * Function : static int DC390_queue_command (Scsi_Cmnd *cmd,
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
 *	  TO BE DONE:
 *	  new model: return 0 if successful
 *	  	     return 1 if command cannot be queued (queue full)
 *		     command will be inserted in midlevel queue then ...
 *
 ***********************************************************************/

static int DC390_queue_command (Scsi_Cmnd *cmd, void (* done)(Scsi_Cmnd *))
{
    PDCB   pDCB;
    PSRB   pSRB;
    PACB   pACB = (PACB) cmd->device->host->hostdata;

    DEBUG0(/*  if(pACB->scan_devices) */	\
	printk(KERN_INFO "DC390: Queue Cmd=%02x,Tgt=%d,LUN=%d (pid=%li), buffer=%p\n",\
	       cmd->cmnd[0],cmd->device->id,cmd->device->lun,cmd->pid, cmd->buffer));

    /* TODO: Change the policy: Always accept TEST_UNIT_READY or INQUIRY 
     * commands and alloc a DCB for the device if not yet there. DCB will
     * be removed in dc390_SRBdone if SEL_TIMEOUT */

    if (((pACB->scan_devices == END_SCAN) && (cmd->cmnd[0] != INQUIRY)) ||
	((pACB->scan_devices) && (cmd->cmnd[0] == READ_6)))
	pACB->scan_devices = 0;

    if ((pACB->scan_devices || cmd->cmnd[0] == TEST_UNIT_READY || cmd->cmnd[0] == INQUIRY) && 
	!(pACB->DCBmap[cmd->device->id] & (1 << cmd->device->lun))) {
	pACB->scan_devices = 1;
	DCBDEBUG(printk("Scanning target %02x lun %02x\n", cmd->device->id, cmd->device->lun));
    } else if (!(pACB->scan_devices) && !(pACB->DCBmap[cmd->device->id] & (1 << cmd->device->lun))) {
	printk(KERN_INFO "DC390: Ignore target %02x lun %02x\n",
		cmd->device->id, cmd->device->lun); 
	goto fail;
    }

    pDCB = dc390_findDCB (pACB, cmd->device->id, cmd->device->lun);

    /* Should it be: BUG_ON(!pDCB); ? */

    if (!pDCB)
    {  /* should never happen */
	printk (KERN_ERR "DC390: no DCB found, target %02x lun %02x\n", 
		cmd->device->id, cmd->device->lun);
	goto fail;
    }

    pACB->Cmds++;
    cmd->scsi_done = done;
    cmd->result = 0;

    pSRB = dc390_Free_get(pACB);
    if (!pSRB)
	    goto requeue;

    dc390_BuildSRB(cmd, pDCB, pSRB);
    if (pDCB->pWaitingSRB) {
	    dc390_Waiting_append(pDCB, pSRB);
	    dc390_Waiting_process(pACB);
    } else
	    dc390_SendSRB(pACB, pSRB);

    DEBUG1(printk (KERN_DEBUG " ... command (pid %li) queued successfully.\n", cmd->pid));
    return(0);

 requeue:
    return 1;
 fail:
    cmd->result = DID_BAD_TARGET << 16;
    done(cmd);
    return 0;
}

/* We ignore mapping problems, as we expect everybody to respect 
 * valid partition tables. Waiting for complaints ;-) */

#ifdef CONFIG_SCSI_DC390T_TRADMAP
/* 
 * The next function, partsize(), is copied from scsicam.c.
 *
 * This is ugly code duplication, but I didn't find another way to solve it:
 * We want to respect the partition table and if it fails, we apply the 
 * DC390 BIOS heuristic. Too bad, just calling scsicam_bios_param() doesn't do
 * the job, because we don't know, whether the values returned are from
 * the part. table or determined by setsize(). Unfortunately the setsize() 
 * values differ from the ones chosen by the DC390 BIOS.
 *
 * Looking forward to seeing suggestions for a better solution! KG, 98/10/14
 */
#include <asm/unaligned.h>

/*
 * Function : static int partsize(unsigned char *buf, unsigned long 
 *     capacity,unsigned int *cyls, unsigned int *hds, unsigned int *secs);
 *
 * Purpose : to determine the BIOS mapping used to create the partition
 *	table, storing the results in *cyls, *hds, and *secs 
 *
 * Returns : -1 on failure, 0 on success.
 *
 */

static int partsize(unsigned char *buf, unsigned long capacity,
    unsigned int  *cyls, unsigned int *hds, unsigned int *secs) {
    struct partition *p, *largest = NULL;
    int i, largest_cyl;
    int cyl, ext_cyl, end_head, end_cyl, end_sector;
    unsigned int logical_end, physical_end, ext_physical_end;
    

    if (*(unsigned short *) (buf+64) == 0xAA55) {
	for (largest_cyl = -1, p = (struct partition *) buf, 
    	    i = 0; i < 4; ++i, ++p) {
    	    if (!p->sys_ind)
    	    	continue;
    	    cyl = p->cyl + ((p->sector & 0xc0) << 2);
    	    if (cyl > largest_cyl) {
    	    	largest_cyl = cyl;
    	    	largest = p;
    	    }
    	}
    }

    if (largest) {
    	end_cyl = largest->end_cyl + ((largest->end_sector & 0xc0) << 2);
    	end_head = largest->end_head;
    	end_sector = largest->end_sector & 0x3f;

    	physical_end =  end_cyl * (end_head + 1) * end_sector +
    	    end_head * end_sector + end_sector;

	/* This is the actual _sector_ number at the end */
	logical_end = get_unaligned(&largest->start_sect)
			+ get_unaligned(&largest->nr_sects);

	/* This is for >1023 cylinders */
        ext_cyl= (logical_end-(end_head * end_sector + end_sector))
                                        /(end_head + 1) / end_sector;
	ext_physical_end = ext_cyl * (end_head + 1) * end_sector +
            end_head * end_sector + end_sector;

    	if ((logical_end == physical_end) ||
	    (end_cyl==1023 && ext_physical_end==logical_end)) {
    	    *secs = end_sector;
    	    *hds = end_head + 1;
    	    *cyls = capacity / ((end_head + 1) * end_sector);
    	    return 0;
    	}
    }
    return -1;
}

/***********************************************************************
 * Function:
 *   DC390_bios_param
 *
 * Description:
 *   Return the disk geometry for the given SCSI device.
 *   Respect the partition table, otherwise try own heuristic
 *
 * Note:
 *   In contrary to other externally callable funcs (DC390_), we don't lock
 ***********************************************************************/
static int DC390_bios_param (struct scsi_device *sdev, struct block_device *bdev,
			     sector_t capacity, int geom[])
{
    int heads, sectors, cylinders;
    PACB pACB = (PACB) sdev->host->hostdata;
    int ret_code = -1;
    int size = capacity;
    unsigned char *buf;

    if ((buf = scsi_bios_ptable(bdev)))
    {
	/* try to infer mapping from partition table */
	ret_code = partsize (buf, (unsigned long) size, (unsigned int *) geom + 2,
			     (unsigned int *) geom + 0, (unsigned int *) geom + 1);
	kfree (buf);
    }
    if (ret_code == -1)
    {
	heads = 64;
	sectors = 32;
	cylinders = size / (heads * sectors);

	if ( (pACB->Gmode2 & GREATER_1G) && (cylinders > 1024) )
	{
		heads = 255;
		sectors = 63;
		cylinders = size / (heads * sectors);
	}

	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;
    }

    return (0);
}
#else
static int DC390_bios_param (struct scsi_device *sdev, struct block_device *bdev,
			     sector_t capacity, int geom[])
{
    return scsicam_bios_param (bdev, capacity, geom);
}
#endif

static void dc390_dumpinfo (PACB pACB, PDCB pDCB, PSRB pSRB)
{
    USHORT pstat; PDEVDECL1;
    if (!pDCB) pDCB = pACB->pActiveDCB;
    if (!pSRB && pDCB) pSRB = pDCB->pActiveSRB;

    if (pSRB) 
    {
	printk ("DC390: SRB: Xferred %08lx, Remain %08lx, State %08x, Phase %02x\n",
		pSRB->TotalXferredLen, pSRB->SGToBeXferLen, pSRB->SRBState,
		pSRB->ScsiPhase);
	printk ("DC390: AdpaterStatus: %02x, SRB Status %02x\n", pSRB->AdaptStatus, pSRB->SRBStatus);
    }
    printk ("DC390: Status of last IRQ (DMA/SC/Int/IRQ): %08x\n", dc390_laststatus);
    printk ("DC390: Register dump: SCSI block:\n");
    printk ("DC390: XferCnt  Cmd Stat IntS IRQS FFIS Ctl1 Ctl2 Ctl3 Ctl4\n");
    printk ("DC390:  %06x   %02x   %02x   %02x",
	    DC390_read8(CtcReg_Low) + (DC390_read8(CtcReg_Mid) << 8) + (DC390_read8(CtcReg_High) << 16),
	    DC390_read8(ScsiCmd), DC390_read8(Scsi_Status), DC390_read8(Intern_State));
    printk ("   %02x   %02x   %02x   %02x   %02x   %02x\n",
	    DC390_read8(INT_Status), DC390_read8(Current_Fifo), DC390_read8(CtrlReg1),
	    DC390_read8(CtrlReg2), DC390_read8(CtrlReg3), DC390_read8(CtrlReg4));
    DC390_write32 (DMA_ScsiBusCtrl, WRT_ERASE_DMA_STAT | EN_INT_ON_PCI_ABORT);
    if (DC390_read8(Current_Fifo) & 0x1f)
      {
	printk ("DC390: FIFO:");
	while (DC390_read8(Current_Fifo) & 0x1f) printk (" %02x", DC390_read8(ScsiFifo));
	printk ("\n");
      }
    printk ("DC390: Register dump: DMA engine:\n");
    printk ("DC390: Cmd   STrCnt    SBusA    WrkBC    WrkAC Stat SBusCtrl\n");
    printk ("DC390:  %02x %08x %08x %08x %08x   %02x %08x\n",
	    DC390_read8(DMA_Cmd), DC390_read32(DMA_XferCnt), DC390_read32(DMA_XferAddr),
	    DC390_read32(DMA_Wk_ByteCntr), DC390_read32(DMA_Wk_AddrCntr),
	    DC390_read8(DMA_Status), DC390_read32(DMA_ScsiBusCtrl));
    DC390_write32 (DMA_ScsiBusCtrl, EN_INT_ON_PCI_ABORT);
    PDEVSET1; PCI_READ_CONFIG_WORD(PDEV, PCI_STATUS, &pstat);
    printk ("DC390: Register dump: PCI Status: %04x\n", pstat);
    printk ("DC390: In case of driver trouble read linux/Documentation/scsi/tmscsim.txt\n");
}


/***********************************************************************
 * Function : int DC390_abort (Scsi_Cmnd *cmd)
 *
 * Purpose : Abort an errant SCSI command
 *
 * Inputs : cmd - command to abort
 *
 * Returns : 0 on success, -1 on failure.
 *
 * Status: Buggy !
 ***********************************************************************/

static int DC390_abort (Scsi_Cmnd *cmd)
{
    PDCB  pDCB;
    PSRB  pSRB, psrb;
    UINT  count, i;
    int   status;
    //ULONG sbac;
    PACB  pACB = (PACB) cmd->device->host->hostdata;

    printk ("DC390: Abort command (pid %li, Device %02i-%02i)\n",
	    cmd->pid, cmd->device->id, cmd->device->lun);

    pDCB = dc390_findDCB (pACB, cmd->device->id, cmd->device->lun);
    if( !pDCB ) goto  NOT_RUN;

    /* Added 98/07/02 KG */
    /*
    pSRB = pDCB->pActiveSRB;
    if (pSRB && pSRB->pcmd == cmd )
	goto ON_GOING;
     */
    
    pSRB = pDCB->pWaitingSRB;
    if( !pSRB )
	goto  ON_GOING;

    /* Now scan Waiting queue */
    if( pSRB->pcmd == cmd )
    {
	pDCB->pWaitingSRB = pSRB->pNextSRB;
	goto  IN_WAIT;
    }
    else
    {
	psrb = pSRB;
	if( !(psrb->pNextSRB) )
	    goto ON_GOING;
	while( psrb->pNextSRB->pcmd != cmd )
	{
	    psrb = psrb->pNextSRB;
	    if( !(psrb->pNextSRB) || psrb == pSRB)
		goto ON_GOING;
	}
	pSRB = psrb->pNextSRB;
	psrb->pNextSRB = pSRB->pNextSRB;
	if( pSRB == pDCB->pWaitLast )
	    pDCB->pWaitLast = psrb;
IN_WAIT:
	dc390_Free_insert (pACB, pSRB);
	pDCB->WaitSRBCnt--;
	INIT_LIST_HEAD((struct list_head*)&cmd->SCp);
	status = SCSI_ABORT_SUCCESS;
	goto  ABO_X;
    }

    /* SRB has already been sent ! */
ON_GOING:
    /* abort() is too stupid for already sent commands at the moment. 
     * If it's called we are in trouble anyway, so let's dump some info 
     * into the syslog at least. (KG, 98/08/20,99/06/20) */
    dc390_dumpinfo (pACB, pDCB, pSRB);
    pSRB = pDCB->pGoingSRB;
    pDCB->DCBFlag |= ABORT_DEV_;
    /* Now for the hard part: The command is currently processed */
    for( count = pDCB->GoingSRBCnt, i=0; i<count; i++)
    {
	if( pSRB->pcmd != cmd )
	    pSRB = pSRB->pNextSRB;
	else
	{
	    if( (pACB->pActiveDCB == pDCB) && (pDCB->pActiveSRB == pSRB) )
	    {
		status = SCSI_ABORT_BUSY;
		printk ("DC390: Abort current command (pid %li, SRB %p)\n",
			cmd->pid, pSRB);
		goto  ABO_X;
	    }
	    else
	    {
		status = SCSI_ABORT_SNOOZE;
		goto  ABO_X;
	    }
	}
    }

NOT_RUN:
    status = SCSI_ABORT_NOT_RUNNING;

ABO_X:
    cmd->result = DID_ABORT << 16;
    printk(KERN_INFO "DC390: Aborted pid %li with status %i\n", cmd->pid, status);
#if 0
    if (cmd->pid == dc390_lastabortedpid) /* repeated failure ? */
	{
		/* Let's do something to help the bus getting clean again */
		DC390_write8 (DMA_Cmd, DMA_IDLE_CMD);
		DC390_write8 (ScsiCmd, DMA_COMMAND);
		//DC390_write8 (ScsiCmd, CLEAR_FIFO_CMD);
		//DC390_write8 (ScsiCmd, RESET_ATN_CMD);
		DC390_write8 (ScsiCmd, NOP_CMD);
		//udelay (10000);
		//DC390_read8 (INT_Status);
		//DC390_write8 (ScsiCmd, EN_SEL_RESEL);
	}
    sbac = DC390_read32 (DMA_ScsiBusCtrl);
    if (sbac & SCSI_BUSY)
    {	/* clear BSY, SEL and ATN */
	printk (KERN_WARNING "DC390: Reset SCSI device: ");
	//DC390_write32 (DMA_ScsiBusCtrl, (sbac | SCAM) & ~SCSI_LINES);
	//udelay (250);
	//sbac = DC390_read32 (DMA_ScsiBusCtrl);
	//printk ("%08lx ", sbac);
	//DC390_write32 (DMA_ScsiBusCtrl, sbac & ~(SCSI_LINES | SCAM));
	//udelay (100);
	//sbac = DC390_read32 (DMA_ScsiBusCtrl);
	//printk ("%08lx ", sbac);
	DC390_write8 (ScsiCmd, RST_DEVICE_CMD);
	udelay (250);
	DC390_write8 (ScsiCmd, NOP_CMD);
	sbac = DC390_read32 (DMA_ScsiBusCtrl);
	printk ("%08lx\n", sbac);
    }
#endif
    dc390_lastabortedpid = cmd->pid;
    //do_DC390_Interrupt (pACB->IRQLevel, 0, 0);
#ifndef USE_NEW_EH	
    if (status == SCSI_ABORT_SUCCESS) cmd->scsi_done(cmd);
#endif	
    return( status );
}


static void dc390_ResetDevParam( PACB pACB )
{
    PDCB   pDCB, pdcb;

    pDCB = pACB->pLinkDCB;
    if (! pDCB) return;
    pdcb = pDCB;
    do
    {
	pDCB->SyncMode &= ~SYNC_NEGO_DONE;
	pDCB->SyncPeriod = 0;
	pDCB->SyncOffset = 0;
	pDCB->TagMask = 0;
	pDCB->CtrlR3 = FAST_CLK;
	pDCB->CtrlR4 &= NEGATE_REQACKDATA | CTRL4_RESERVED | NEGATE_REQACK;
	pDCB->CtrlR4 |= pACB->glitch_cfg;
	pDCB = pDCB->pNextDCB;
    }
    while( pdcb != pDCB );
    pACB->ACBFlag &= ~(RESET_DEV | RESET_DONE | RESET_DETECT);

}

#if 0
/* Moves all SRBs from Going to Waiting for all DCBs */
static void dc390_RecoverSRB( PACB pACB )
{
    PDCB   pDCB, pdcb;
    PSRB   psrb, psrb2;
    UINT   cnt, i;

    pDCB = pACB->pLinkDCB;
    if( !pDCB ) return;
    pdcb = pDCB;
    do
    {
	cnt = pdcb->GoingSRBCnt;
	psrb = pdcb->pGoingSRB;
	for (i=0; i<cnt; i++)
	{
	    psrb2 = psrb;
	    psrb = psrb->pNextSRB;
/*	    dc390_RewaitSRB( pDCB, psrb ); */
	    if( pdcb->pWaitingSRB )
	    {
		psrb2->pNextSRB = pdcb->pWaitingSRB;
		pdcb->pWaitingSRB = psrb2;
	    }
	    else
	    {
		pdcb->pWaitingSRB = psrb2;
		pdcb->pWaitLast = psrb2;
		psrb2->pNextSRB = NULL;
	    }
	}
	pdcb->GoingSRBCnt = 0;
	pdcb->pGoingSRB = NULL;
	pdcb->TagMask = 0;
	pdcb = pdcb->pNextDCB;
    } while( pdcb != pDCB );
}
#endif

/***********************************************************************
 * Function : int DC390_reset (Scsi_Cmnd *cmd, ...)
 *
 * Purpose : perform a hard reset on the SCSI bus
 *
 * Inputs : cmd - command which caused the SCSI RESET
 *	    resetFlags - how hard to try
 *
 * Returns : 0 on success.
 ***********************************************************************/

static int DC390_reset (Scsi_Cmnd *cmd)
{
    UCHAR   bval;
    PACB    pACB = (PACB) cmd->device->host->hostdata;

    printk(KERN_INFO "DC390: RESET ... ");

    if (timer_pending (&pACB->Waiting_Timer)) del_timer (&pACB->Waiting_Timer);
    bval = DC390_read8 (CtrlReg1);
    bval |= DIS_INT_ON_SCSI_RST;
    DC390_write8 (CtrlReg1, bval);	/* disable IRQ on bus reset */

    pACB->ACBFlag |= RESET_DEV;
    dc390_ResetSCSIBus( pACB );

    dc390_ResetDevParam( pACB );
    udelay (1000);
    pACB->pScsiHost->last_reset = jiffies + 3*HZ/2 
		+ HZ * dc390_eepromBuf[pACB->AdapterIndex][EE_DELAY];
    
    DC390_write8 (ScsiCmd, CLEAR_FIFO_CMD);
    DC390_read8 (INT_Status);		/* Reset Pending INT */

    dc390_DoingSRB_Done( pACB, cmd );
    /* dc390_RecoverSRB (pACB); */
    pACB->pActiveDCB = NULL;

    pACB->ACBFlag = 0;
    bval = DC390_read8 (CtrlReg1);
    bval &= ~DIS_INT_ON_SCSI_RST;
    DC390_write8 (CtrlReg1, bval);	/* re-enable interrupt */

    dc390_Waiting_process( pACB );

    printk("done\n");
    return( SCSI_RESET_SUCCESS );
}

#include "scsiiom.c"


/***********************************************************************
 * Function : static void dc390_initDCB()
 *
 * Purpose :  initialize the internal structures for a DCB (to be malloced)
 *
 * Inputs : SCSI id and lun
 ***********************************************************************/

static void dc390_initDCB( PACB pACB, PDCB *ppDCB, UCHAR id, UCHAR lun )
{
    PEEprom	prom;
    UCHAR	index;
    PDCB pDCB, pDCB2;

    pDCB = kmalloc (sizeof(DC390_DCB), GFP_ATOMIC);
    DCBDEBUG(printk (KERN_INFO "DC390: alloc mem for DCB (ID %i, LUN %i): %p\n",	\
		     id, lun, pDCB));
 
    *ppDCB = pDCB;
    if (!pDCB) return;
    pDCB2 = 0;
    if( pACB->DCBCnt == 0 )
    {
	pACB->pLinkDCB = pDCB;
	pACB->pDCBRunRobin = pDCB;
    }
    else
    {
	pACB->pLastDCB->pNextDCB = pDCB;
    }
   
    pACB->DCBCnt++;
   
    pDCB->pNextDCB = pACB->pLinkDCB;
    pACB->pLastDCB = pDCB;

    pDCB->pDCBACB = pACB;
    pDCB->TargetID = id;
    pDCB->TargetLUN = lun;
    pDCB->pWaitingSRB = NULL;
    pDCB->pGoingSRB = NULL;
    pDCB->GoingSRBCnt = 0;
    pDCB->WaitSRBCnt = 0;
    pDCB->pActiveSRB = NULL;
    pDCB->TagMask = 0;
    pDCB->MaxCommand = 1;
    index = pACB->AdapterIndex;
    pDCB->DCBFlag = 0;

    /* Is there a corresp. LUN==0 device ? */
    if (lun != 0)
	pDCB2 = dc390_findDCB (pACB, id, 0);
    prom = (PEEprom) &dc390_eepromBuf[index][id << 2];
    /* Some values are for all LUNs: Copy them */
    /* In a clean way: We would have an own structure for a SCSI-ID */
    if (pDCB2)
    {
      pDCB->DevMode = pDCB2->DevMode;
      pDCB->SyncMode = pDCB2->SyncMode;
      pDCB->SyncPeriod = pDCB2->SyncPeriod;
      pDCB->SyncOffset = pDCB2->SyncOffset;
      pDCB->NegoPeriod = pDCB2->NegoPeriod;
      
      pDCB->CtrlR3 = pDCB2->CtrlR3;
      pDCB->CtrlR4 = pDCB2->CtrlR4;
      pDCB->Inquiry7 = pDCB2->Inquiry7;
    }
    else
    {		
      pDCB->DevMode = prom->EE_MODE1;
      pDCB->SyncMode = 0;
      pDCB->SyncPeriod = 0;
      pDCB->SyncOffset = 0;
      pDCB->NegoPeriod = (dc390_clock_period1[prom->EE_SPEED] * 25) >> 2;
            
      pDCB->CtrlR3 = FAST_CLK;
      
      pDCB->CtrlR4 = pACB->glitch_cfg | CTRL4_RESERVED;
      if( dc390_eepromBuf[index][EE_MODE2] & ACTIVE_NEGATION)
	pDCB->CtrlR4 |= NEGATE_REQACKDATA | NEGATE_REQACK;
      pDCB->Inquiry7 = 0;
    }

    pACB->DCBmap[id] |= (1 << lun);
    dc390_updateDCB(pACB, pDCB);
}

/***********************************************************************
 * Function : static void dc390_updateDCB()
 *
 * Purpose :  Set the configuration dependent DCB parameters
 ***********************************************************************/

static void dc390_updateDCB (PACB pACB, PDCB pDCB)
{
  pDCB->SyncMode &= EN_TAG_QUEUEING | SYNC_NEGO_DONE /*| EN_ATN_STOP*/;
  if (pDCB->DevMode & TAG_QUEUEING_) {
	//if (pDCB->SyncMode & EN_TAG_QUEUEING) pDCB->MaxCommand = pACB->TagMaxNum;
  } else {
	pDCB->SyncMode &= ~EN_TAG_QUEUEING;
	pDCB->MaxCommand = 1;
  }

  if( pDCB->DevMode & SYNC_NEGO_ )
	pDCB->SyncMode |= SYNC_ENABLE;
  else {
	pDCB->SyncMode &= ~(SYNC_NEGO_DONE | SYNC_ENABLE);
	pDCB->SyncOffset &= ~0x0f;
  }

  //if (! (pDCB->DevMode & EN_DISCONNECT_)) pDCB->SyncMode &= ~EN_ATN_STOP; 

  pDCB->CtrlR1 = pACB->pScsiHost->this_id;
  if( pDCB->DevMode & PARITY_CHK_ )
	pDCB->CtrlR1 |= PARITY_ERR_REPO;
}  

/***********************************************************************
 * Function : static void dc390_initSRB()
 *
 * Purpose :  initialize the internal structures for a given SRB
 *
 * Inputs : psrb - pointer to this scsi request block structure
 ***********************************************************************/

static void __inline__ dc390_initSRB( PSRB psrb )
{
  /* psrb->PhysSRB = virt_to_phys( psrb ); */
}


static void dc390_linkSRB( PACB pACB )
{
    UINT   count, i;

    count = pACB->SRBCount;
    for( i=0; i<count; i++)
    {
	if( i != count-1 )
	    pACB->SRB_array[i].pNextSRB = &pACB->SRB_array[i+1];
	else
	    pACB->SRB_array[i].pNextSRB = NULL;
	dc390_initSRB( &pACB->SRB_array[i] );
    }
}


/***********************************************************************
 * Function : static void dc390_initACB ()
 *
 * Purpose :  initialize the internal structures for a given SCSI host
 *
 * Inputs : psh - pointer to this host adapter's structure
 *	    io_port, Irq, index: Resources and adapter index
 ***********************************************************************/

static void __init dc390_initACB (PSH psh, ULONG io_port, UCHAR Irq, UCHAR index)
{
    PACB    pACB;
    UCHAR   i;

    psh->can_queue = MAX_CMD_QUEUE;
    psh->cmd_per_lun = MAX_CMD_PER_LUN;
    psh->this_id = (int) dc390_eepromBuf[index][EE_ADAPT_SCSI_ID];
    psh->io_port = io_port;
    psh->n_io_port = 0x80;
    psh->irq = Irq;
    psh->base = io_port;
    psh->unique_id = io_port;
    psh->dma_channel = -1;
    psh->last_reset = jiffies;
	
    pACB = (PACB) psh->hostdata;

    pACB->pScsiHost = psh;
    pACB->IOPortBase = (USHORT) io_port;
    pACB->IRQLevel = Irq;

    DEBUG0(printk (KERN_INFO "DC390: Adapter index %i, ID %i, IO 0x%08x, IRQ 0x%02x\n",	\
		   index, psh->this_id, (int)io_port, Irq));
   
    psh->max_id = 8;

    if( psh->max_id - 1 == dc390_eepromBuf[index][EE_ADAPT_SCSI_ID] )
	psh->max_id--;
    psh->max_lun = 1;
    if( dc390_eepromBuf[index][EE_MODE2] & LUN_CHECK )
	psh->max_lun = 8;

    pACB->pLinkDCB = NULL;
    pACB->pDCBRunRobin = NULL;
    pACB->pActiveDCB = NULL;
    pACB->pFreeSRB = pACB->SRB_array;
    pACB->SRBCount = MAX_SRB_CNT;
    pACB->AdapterIndex = index;
    pACB->status = 0;
    psh->this_id = dc390_eepromBuf[index][EE_ADAPT_SCSI_ID];
    pACB->DeviceCnt = 0;
    pACB->DCBCnt = 0;
    pACB->TagMaxNum = 2 << dc390_eepromBuf[index][EE_TAG_CMD_NUM];
    pACB->ACBFlag = 0;
    pACB->scan_devices = 1;
    pACB->MsgLen = 0;
    pACB->Ignore_IRQ = 0;
    pACB->Gmode2 = dc390_eepromBuf[index][EE_MODE2];
    dc390_linkSRB( pACB );
    pACB->pTmpSRB = &pACB->TmpSRB;
    dc390_initSRB( pACB->pTmpSRB );
    for(i=0; i<MAX_SCSI_ID; i++)
	pACB->DCBmap[i] = 0;
    pACB->sel_timeout = SEL_TIMEOUT;
    pACB->glitch_cfg = EATER_25NS;
    pACB->Cmds = pACB->CmdInQ = pACB->CmdOutOfSRB = 0;
    pACB->SelLost = pACB->SelConn = 0;
    init_timer (&pACB->Waiting_Timer);
}


/***********************************************************************
 * Function : static int dc390_initAdapter ()
 *
 * Purpose :  initialize the SCSI chip ctrl registers
 *
 * Inputs : psh - pointer to this host adapter's structure
 *	    io_port, Irq, index: Resources
 *
 * Outputs: 0 on success, -1 on error
 ***********************************************************************/

static int __init dc390_initAdapter (PSH psh, ULONG io_port, UCHAR Irq, UCHAR index)
{
    PACB   pACB, pACB2;
    UCHAR  dstate;
    int    i;
    
    pACB = (PACB) psh->hostdata;

    if (request_region (io_port, psh->n_io_port, "tmscsim") == NULL) {
	printk(KERN_ERR "DC390: register IO ports error!\n");
	return( -1 );
    }

    DC390_read8_ (INT_Status, io_port);		/* Reset Pending INT */

    if( (i = request_irq(Irq, do_DC390_Interrupt, DC390_IRQ, "tmscsim", pACB) ))
      {
	printk(KERN_ERR "DC390: register IRQ error!\n");
	release_region (io_port, psh->n_io_port);
	return( -1 );
      }

    if( !dc390_pACB_start )
      {
	pACB2 = NULL;
	dc390_pACB_start = pACB;
	dc390_pACB_current = pACB;
	pACB->pNextACB = NULL;
      }
    else
      {
	pACB2 = dc390_pACB_current;
	dc390_pACB_current->pNextACB = pACB;
	dc390_pACB_current = pACB;
	pACB->pNextACB = NULL;
      }

    DC390_write8 (CtrlReg1, DIS_INT_ON_SCSI_RST | psh->this_id);	/* Disable SCSI bus reset interrupt */

    if (pACB->Gmode2 & RST_SCSI_BUS)
    {
	dc390_ResetSCSIBus( pACB );
	udelay (1000);
	pACB->pScsiHost->last_reset = jiffies + HZ/2
		    + HZ * dc390_eepromBuf[pACB->AdapterIndex][EE_DELAY];
	/*
	for( i=0; i<(500 + 1000*dc390_eepromBuf[pACB->AdapterIndex][EE_DELAY]); i++ )
		udelay(1000);
	 */
    }
    pACB->ACBFlag = 0;
    DC390_read8 (INT_Status);				/* Reset Pending INT */
    
    DC390_write8 (Scsi_TimeOut, SEL_TIMEOUT);		/* 250ms selection timeout */
    DC390_write8 (Clk_Factor, CLK_FREQ_40MHZ);		/* Conversion factor = 0 , 40MHz clock */
    DC390_write8 (ScsiCmd, NOP_CMD);			/* NOP cmd - clear command register */
    DC390_write8 (CtrlReg2, EN_FEATURE+EN_SCSI2_CMD);	/* Enable Feature and SCSI-2 */
    DC390_write8 (CtrlReg3, FAST_CLK);			/* fast clock */
    DC390_write8 (CtrlReg4, pACB->glitch_cfg |			/* glitch eater */
		(dc390_eepromBuf[index][EE_MODE2] & ACTIVE_NEGATION) ? NEGATE_REQACKDATA : 0);	/* Negation */
    DC390_write8 (CtcReg_High, 0);			/* Clear Transfer Count High: ID */
    DC390_write8 (DMA_Cmd, DMA_IDLE_CMD);
    DC390_write8 (ScsiCmd, CLEAR_FIFO_CMD);
    DC390_write32 (DMA_ScsiBusCtrl, EN_INT_ON_PCI_ABORT);
    dstate = DC390_read8 (DMA_Status);
    DC390_write8 (DMA_Status, dstate);	/* clear */

    return(0);
}


/***********************************************************************
 * Function : static int DC390_init (struct Scsi_Host *host, ...)
 *
 * Purpose :  initialize the internal structures for a given SCSI host
 *
 * Inputs : host - pointer to this host adapter's structure
 *	    io_port - IO ports mapped to this adapter
 *	    irq - IRQ assigned to this adpater
 *	    struct pci_dev - PCI access handle
 *	    index - Adapter index
 *
 * Outputs: 0 on success, -1 on error
 *
 * Note: written in capitals, because the locking is only done here,
 *	not in DC390_detect, called from outside 
 ***********************************************************************/
static int __init dc390_init (PSH psh, unsigned long io_port, u8 irq, struct pci_dev *pdev, UCHAR index)
{
    PACB  pACB;

    if (dc390_CheckEEpromCheckSum (PDEV, index))
    {
	int speed;
	dc390_adapname = "AM53C974";
	printk (KERN_INFO "DC390_init: No EEPROM found! Trying default settings ...\n");
	dc390_check_for_safe_settings ();
	dc390_fill_with_defaults ();
	dc390_EEprom_Override (index);
	speed = dc390_clock_speed[tmscsim[1]];
	printk (KERN_INFO "DC390: Used defaults: AdaptID=%i, SpeedIdx=%i (%i.%i MHz),"
		" DevMode=0x%02x, AdaptMode=0x%02x, TaggedCmnds=%i (%i), DelayReset=%is\n", 
		tmscsim[0], tmscsim[1], speed/10, speed%10,
		(UCHAR)tmscsim[2], (UCHAR)tmscsim[3], tmscsim[4], 2 << (tmscsim[4]), tmscsim[5]);
    }
    else
    {
	dc390_check_for_safe_settings ();
	dc390_EEprom_Override (index);
    }
    pACB = (PACB) psh->hostdata;

    DEBUG0(printk(KERN_INFO "DC390: pSH = %8x, Index %02i\n", (UINT) psh, index));

    dc390_initACB( psh, io_port, irq, index );
        
    PDEVSET;

    if( !dc390_initAdapter( psh, io_port, irq, index ) )
    {
        return (0);
    }
    else
    {
	scsi_unregister( psh );
	return( -1 );
    }
}

static void __init dc390_set_pci_cfg (PDEVDECL)
{
	USHORT cmd;
	PCI_READ_CONFIG_WORD (PDEV, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_SERR | PCI_COMMAND_PARITY | PCI_COMMAND_IO;
	PCI_WRITE_CONFIG_WORD (PDEV, PCI_COMMAND, cmd);
	PCI_WRITE_CONFIG_WORD (PDEV, PCI_STATUS, (PCI_STATUS_SIG_SYSTEM_ERROR | PCI_STATUS_DETECTED_PARITY));
}

/**
 * dc390_slave_alloc - Called by the scsi mid layer to tell us about a new
 * scsi device that we need to deal with.
 *
 * @scsi_device: The new scsi device that we need to handle.
 */
static int dc390_slave_alloc(struct scsi_device *scsi_device)
{
	PDCB pDCB;
	PACB pACB = (PACB) scsi_device->host->hostdata;
	dc390_initDCB(pACB, &pDCB, scsi_device->id, scsi_device->lun);
	if (pDCB != NULL)
		return 0;
	return -ENOMEM;
}

/**
 * dc390_slave_destroy - Called by the scsi mid layer to tell us about a
 * device that is going away.
 *
 * @scsi_device: The scsi device that we need to remove.
 */
static void dc390_slave_destroy(struct scsi_device *scsi_device)
{
	PACB pACB = (PACB) scsi_device->host->hostdata;
	PDCB pDCB = dc390_findDCB (pACB, scsi_device->id, scsi_device->lun);;
	if (pDCB != NULL)
		dc390_remove_dev(pACB, pDCB);
	else
		printk(KERN_ERR"%s() called for non-existing device!\n", __FUNCTION__);
}
	
static Scsi_Host_Template driver_template = {
	.module			= THIS_MODULE,
	.proc_name		= "tmscsim", 
	.proc_info		= DC390_proc_info,
	.name			= DC390_BANNER " V" DC390_VERSION,
	.slave_alloc		= dc390_slave_alloc,
	.slave_destroy		= dc390_slave_destroy,
	.queuecommand		= DC390_queue_command,
	.eh_abort_handler	= DC390_abort,
	.eh_bus_reset_handler	= DC390_reset,
	.bios_param		= DC390_bios_param,
	.can_queue		= 42,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 16,
	.use_clustering		= DISABLE_CLUSTERING,
};

static int __devinit dc390_init_one(struct pci_dev *dev,
				    const struct pci_device_id *id)
{
	struct Scsi_Host *scsi_host;
	unsigned long io_port;
	u8 irq;
	PACB  pACB;
	int ret = -ENOMEM;

	if (pci_enable_device(dev))
		return -ENODEV;

	io_port = pci_resource_start(dev, 0);
	irq = dev->irq;

	/* allocate scsi host information (includes out adapter) */
	scsi_host = scsi_host_alloc(&driver_template, sizeof(struct _ACB));
	if (!scsi_host)
		goto nomem;

	pACB = (PACB)scsi_host->hostdata;

	if (dc390_init(scsi_host, io_port, irq, dev, dc390_adapterCnt)) {
		ret = -EBUSY;
		goto busy;
	}

	pci_set_master(dev);
	dc390_set_pci_cfg(dev);
	dc390_adapterCnt++;

	/* get the scsi mid level to scan for new devices on the bus */
	if (scsi_add_host(scsi_host, &dev->dev)) {
		ret = -ENODEV;
		goto nodev;
	}
	pci_set_drvdata(dev, scsi_host);
	scsi_scan_host(scsi_host);

	return 0;

nodev:
busy:
	scsi_host_put(scsi_host);
nomem:
	pci_disable_device(dev);
	return ret;
}

/**
 * dc390_remove_one - Called to remove a single instance of the adapter.
 *
 * @dev: The PCI device to remove.
 */
static void __devexit dc390_remove_one(struct pci_dev *dev)
{
	struct Scsi_Host *scsi_host = pci_get_drvdata(dev);

	scsi_remove_host(scsi_host);
	DC390_release(scsi_host);
	pci_disable_device(dev);
	scsi_host_put(scsi_host);
	pci_set_drvdata(dev, NULL);
}

/********************************************************************
 * Function: DC390_proc_info(char* buffer, char **start,
 *			     off_t offset, int length, int hostno, int inout)
 *
 * Purpose: return SCSI Adapter/Device Info
 *
 * Input: buffer: Pointer to a buffer where to write info
 *	  start :
 *	  offset:
 *	  hostno: Host adapter index
 *	  inout : Read (=0) or set(!=0) info
 *
 * Output: buffer: contains info
 *	   length; length of info in buffer
 *
 * return value: length
 *
 ********************************************************************/

#undef SPRINTF
#define SPRINTF(args...) pos += sprintf(pos, ## args)

#define YESNO(YN)		\
 if (YN) SPRINTF(" Yes ");	\
 else SPRINTF(" No  ")


static int DC390_proc_info (struct Scsi_Host *shpnt, char *buffer, char **start,
			    off_t offset, int length, int inout)
{
  int dev, spd, spd1;
  char *pos = buffer;
  PACB pACB;
  PDCB pDCB;

  pACB = dc390_pACB_start;

  while(pACB != (PACB)-1)
     {
	if (shpnt == pACB->pScsiHost)
		break;
	pACB = pACB->pNextACB;
     }

  if (pACB == (PACB)-1) return(-ESRCH);

  if(inout) /* Has data been written to the file ? */
      return -ENOSYS;
   
  SPRINTF("Tekram DC390/AM53C974 PCI SCSI Host Adapter, ");
  SPRINTF("Driver Version %s\n", DC390_VERSION);

  SPRINTF("SCSI Host Nr %i, ", shpnt->host_no);
  SPRINTF("%s Adapter Nr %i\n", dc390_adapname, pACB->AdapterIndex);
  SPRINTF("IOPortBase 0x%04x, ", pACB->IOPortBase);
  SPRINTF("IRQ %02i\n", pACB->IRQLevel);

  SPRINTF("MaxID %i, MaxLUN %i, ", shpnt->max_id, shpnt->max_lun);
  SPRINTF("AdapterID %i, SelTimeout %i ms, DelayReset %i s\n", 
	  shpnt->this_id, (pACB->sel_timeout*164)/100,
	  dc390_eepromBuf[pACB->AdapterIndex][EE_DELAY]);

  SPRINTF("TagMaxNum %i, Status 0x%02x, ACBFlag 0x%02x, GlitchEater %i ns\n",
	  pACB->TagMaxNum, pACB->status, pACB->ACBFlag, GLITCH_TO_NS(pACB->glitch_cfg)*12);

  SPRINTF("Statistics: Cmnds %li, Cmnds not sent directly %i, Out of SRB conds %i\n",
	  pACB->Cmds, pACB->CmdInQ, pACB->CmdOutOfSRB);
  SPRINTF("            Lost arbitrations %i, Sel. connected %i, Connected: %s\n", 
	  pACB->SelLost, pACB->SelConn, pACB->Connected? "Yes": "No");
   
  SPRINTF("Nr of attached devices: %i, Nr of DCBs: %i\n", pACB->DeviceCnt, pACB->DCBCnt);
  SPRINTF("Map of attached LUNs: %02x %02x %02x %02x %02x %02x %02x %02x\n",
	  pACB->DCBmap[0], pACB->DCBmap[1], pACB->DCBmap[2], pACB->DCBmap[3], 
	  pACB->DCBmap[4], pACB->DCBmap[5], pACB->DCBmap[6], pACB->DCBmap[7]);

  SPRINTF("Idx ID LUN Prty Sync DsCn SndS TagQ NegoPeriod SyncSpeed SyncOffs MaxCmd\n");

  pDCB = pACB->pLinkDCB;
  for (dev = 0; dev < pACB->DCBCnt; dev++)
     {
      SPRINTF("%02i  %02i  %02i ", dev, pDCB->TargetID, pDCB->TargetLUN);
      YESNO(pDCB->DevMode & PARITY_CHK_);
      YESNO(pDCB->SyncMode & SYNC_NEGO_DONE);
      YESNO(pDCB->DevMode & EN_DISCONNECT_);
      YESNO(pDCB->DevMode & SEND_START_);
      YESNO(pDCB->SyncMode & EN_TAG_QUEUEING);
      if (pDCB->SyncOffset & 0x0f)
      {
	 int sp = pDCB->SyncPeriod; if (! (pDCB->CtrlR3 & FAST_SCSI)) sp++;
	 SPRINTF("  %03i ns ", (pDCB->NegoPeriod) << 2);
	 spd = 40/(sp); spd1 = 40%(sp);
	 spd1 = (spd1 * 10 + sp/2) / (sp);
	 SPRINTF("   %2i.%1i M      %02i", spd, spd1, (pDCB->SyncOffset & 0x0f));
      }
      else SPRINTF(" (%03i ns)                 ", (pDCB->NegoPeriod) << 2);
      /* Add more info ...*/
      SPRINTF ("      %02i\n", pDCB->MaxCommand);
      pDCB = pDCB->pNextDCB;
     }
    if (timer_pending(&pACB->Waiting_Timer)) SPRINTF ("Waiting queue timer running\n");
    else SPRINTF ("\n");
    pDCB = pACB->pLinkDCB;
	
    for (dev = 0; dev < pACB->DCBCnt; dev++)
    {
	PSRB pSRB;
	if (pDCB->WaitSRBCnt) 
		    SPRINTF ("DCB (%02i-%i): Waiting: %i:", pDCB->TargetID, pDCB->TargetLUN,
			     pDCB->WaitSRBCnt);
	for (pSRB = pDCB->pWaitingSRB; pSRB; pSRB = pSRB->pNextSRB)
		SPRINTF(" %li", pSRB->pcmd->pid);
	if (pDCB->GoingSRBCnt) 
		    SPRINTF ("\nDCB (%02i-%i): Going  : %i:", pDCB->TargetID, pDCB->TargetLUN,
			     pDCB->GoingSRBCnt);
	for (pSRB = pDCB->pGoingSRB; pSRB; pSRB = pSRB->pNextSRB)
#if 0 //def DC390_DEBUGTRACE
		SPRINTF(" %s\n  ", pSRB->debugtrace);
#else
		SPRINTF(" %li", pSRB->pcmd->pid);
#endif
	if (pDCB->WaitSRBCnt || pDCB->GoingSRBCnt) SPRINTF ("\n");
	pDCB = pDCB->pNextDCB;
    }
	
#ifdef DC390_DEBUGDCB
    SPRINTF ("DCB list for ACB %p:\n", pACB);
    pDCB = pACB->pLinkDCB;
    SPRINTF ("%p", pDCB);
    for (dev = 0; dev < pACB->DCBCnt; dev++, pDCB=pDCB->pNextDCB)
	SPRINTF ("->%p", pDCB->pNextDCB);
    SPRINTF("\n");
#endif
  
  *start = buffer + offset;

  if (pos - buffer < offset)
    return 0;
  else if (pos - buffer - offset < length)
    return pos - buffer - offset;
  else
    return length;
}

#undef YESNO
#undef SPRINTF

/***********************************************************************
 * Function : static int dc390_shutdown (struct Scsi_Host *host)
 *
 * Purpose : does a clean (we hope) shutdown of the SCSI chip.
 *	     Use prior to dumping core, unloading the driver, etc.
 *
 * Returns : 0 on success
 ***********************************************************************/
static int dc390_shutdown (struct Scsi_Host *host)
{
    UCHAR    bval;
    PACB pACB = (PACB)(host->hostdata);
   
/*  pACB->soft_reset(host); */

    printk(KERN_INFO "DC390: shutdown\n");

    pACB->ACBFlag = RESET_DEV;
    bval = DC390_read8 (CtrlReg1);
    bval |= DIS_INT_ON_SCSI_RST;
    DC390_write8 (CtrlReg1, bval);	/* disable interrupt */
    if (pACB->Gmode2 & RST_SCSI_BUS)
		dc390_ResetSCSIBus (pACB);

    if (timer_pending (&pACB->Waiting_Timer)) del_timer (&pACB->Waiting_Timer);
    return( 0 );
}

static void dc390_freeDCBs (struct Scsi_Host *host)
{
    PDCB pDCB, nDCB;
    PACB pACB = (PACB)(host->hostdata);
    
    pDCB = pACB->pLinkDCB;
    if (!pDCB) return;
    do
    {
	nDCB = pDCB->pNextDCB;
	DCBDEBUG(printk (KERN_INFO "DC390: Free DCB (ID %i, LUN %i): %p\n",\
			 pDCB->TargetID, pDCB->TargetLUN, pDCB));
	//kfree (pDCB);
	dc390_remove_dev (pACB, pDCB);
	pDCB = nDCB;
    } while (pDCB && pACB->pLinkDCB);

}

static int DC390_release (struct Scsi_Host *host)
{
    DC390_IFLAGS;
    PACB pACB = (PACB)(host->hostdata);

    DC390_LOCK_IO(host);

    /* TO DO: We should check for outstanding commands first. */
    dc390_shutdown (host);

    if (host->irq != SCSI_IRQ_NONE)
    {
	DEBUG0(printk(KERN_INFO "DC390: Free IRQ %i\n",host->irq));
	free_irq (host->irq, pACB);
    }

    release_region(host->io_port,host->n_io_port);
    dc390_freeDCBs (host);
    DC390_UNLOCK_IO(host);
    return( 1 );
}

static struct pci_driver dc390_driver = {
	.name           = "tmscsim",
	.id_table       = tmscsim_pci_tbl,
	.probe          = dc390_init_one,
	.remove         = __devexit_p(dc390_remove_one),
};

static int __init dc390_module_init(void)
{
	return pci_module_init(&dc390_driver);
}

static void __exit dc390_module_exit(void)
{
	pci_unregister_driver(&dc390_driver);
}

module_init(dc390_module_init);
module_exit(dc390_module_exit);
