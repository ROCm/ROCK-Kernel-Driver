/******************************************************************************
 *
 * Name:      	skge.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.29 $
 * Date:       	$Date: 2000/02/21 13:31:56 $
 * Purpose:	The main driver source module
 *
 ******************************************************************************/
 
/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	Driver for SysKonnect Gigabit Ethernet Server Adapters:
 *
 *	SK-9841 (single link 1000Base-LX)
 *	SK-9842 (dual link   1000Base-LX)
 *	SK-9843 (single link 1000Base-SX)
 *	SK-9844 (dual link   1000Base-SX)
 *	SK-9821 (single link 1000Base-T)
 *	SK-9822 (dual link   1000Base-T)
 *
 *	Created 10-Feb-1999, based on Linux' acenic.c, 3c59x.c and 
 *	SysKonnects GEnesis Solaris driver
 *	Author: Christoph Goos (cgoos@syskonnect.de)
 *
 *	Address all question to: linux@syskonnect.de
 *
 *	The technical manual for the adapters is available from SysKonnect's
 *	web pages: www.syskonnect.com
 *	Goto "Support" and search Knowledge Base for "manual".
 *	
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/******************************************************************************
 *
 * History:
 *
 *	$Log: skge.c,v $
 *	Kernel 2.4.x specific:
 *	Revision 1.xx  2000/09/12 13:31:56  cgoos
 *	Fixed missign "dev=NULL in skge_probe.
 *	Added counting for jumbo frames (corrects error statistic).
 *	Removed VLAN tag check (enables VLAN support).
 *	
 *	Kernel 2.2.x specific:
 *	Revision 1.29  2000/02/21 13:31:56  cgoos
 *	Fixed "unused" warning for UltraSPARC change.
 *	
 *	Partially kernel 2.2.x specific:
 *	Revision 1.28  2000/02/21 10:32:36  cgoos
 *	Added fixes for UltraSPARC.
 *	Now printing RlmtMode and PrefPort setting at startup.
 *	Changed XmitFrame return value.
 *	Fixed rx checksum calculation for BIG ENDIAN systems.
 *	Fixed rx jumbo frames counted as ierrors.
 *	
 *	
 *	Revision 1.27  1999/11/25 09:06:28  cgoos
 *	Changed base_addr to unsigned long.
 *	
 *	Revision 1.26  1999/11/22 13:29:16  cgoos
 *	Changed license header to GPL.
 *	Changes for inclusion in linux kernel (2.2.13).
 *	Removed 2.0.x defines.
 *	Changed SkGeProbe to skge_probe.
 *	Added checks in SkGeIoctl.
 *	
 *	Revision 1.25  1999/10/07 14:47:52  cgoos
 *	Changed 984x to 98xx.
 *	
 *	Revision 1.24  1999/09/30 07:21:01  cgoos
 *	Removed SK_RLMT_SLOW_LOOKAHEAD option.
 *	Giving spanning tree packets also to OS now.
 *	
 *	Revision 1.23  1999/09/29 07:36:50  cgoos
 *	Changed assignment for IsBc/IsMc.
 *	
 *	Revision 1.22  1999/09/28 12:57:09  cgoos
 *	Added CheckQueue also to Single-Port-ISR.
 *	
 *	Revision 1.21  1999/09/28 12:42:41  cgoos
 *	Changed parameter strings for RlmtMode.
 *	
 *	Revision 1.20  1999/09/28 12:37:57  cgoos
 *	Added CheckQueue for fast delivery of RLMT frames.
 *	
 *	Revision 1.19  1999/09/16 07:57:25  cgoos
 *	Copperfield changes.
 *	
 *	Revision 1.18  1999/09/03 13:06:30  cgoos
 *	Fixed RlmtMode=CheckSeg bug: wrong DEV_KFREE_SKB in RLMT_SEND caused
 *	double allocated skb's.
 *	FrameStat in ReceiveIrq was accessed via wrong Rxd.
 *	Queue size for async. standby Tx queue was zero.
 *	FillRxLimit of 0 could cause problems with ReQueue, changed to 1.
 *	Removed debug output of checksum statistic.
 *	
 *	Revision 1.17  1999/08/11 13:55:27  cgoos
 *	Transmit descriptor polling was not reenabled after SkGePortInit.
 *	
 *	Revision 1.16  1999/07/27 15:17:29  cgoos
 *	Added some "\n" in output strings (removed while debuging...).
 *	
 *	Revision 1.15  1999/07/23 12:09:30  cgoos
 *	Performance optimization, rx checksumming, large frame support.
 *	
 *	Revision 1.14  1999/07/14 11:26:27  cgoos
 *	Removed Link LED settings (now in RLMT).
 *	Added status output at NET UP.
 *	Fixed SMP problems with Tx and SWITCH running in parallel.
 *	Fixed return code problem at RLMT_SEND event.
 *	
 *	Revision 1.13  1999/04/07 10:11:42  cgoos
 *	Fixed Single Port problems.
 *	Fixed Multi-Adapter problems.
 *	Always display startup string.
 *	
 *	Revision 1.12  1999/03/29 12:26:37  cgoos
 *	Reversed locking to fine granularity.
 *	Fixed skb double alloc problem (caused by incorrect xmit return code).
 *	Enhanced function descriptions.
 *	
 *	Revision 1.11  1999/03/15 13:10:51  cgoos
 *	Changed device identifier in output string to ethX.
 *	
 *	Revision 1.10  1999/03/15 12:12:34  cgoos
 *	Changed copyright notice.
 *	
 *	Revision 1.9  1999/03/15 12:10:17  cgoos
 *	Changed locking to one driver lock.
 *	Added check of SK_AC-size (for consistency with library).
 *	
 *	Revision 1.8  1999/03/08 11:44:02  cgoos
 *	Fixed missing dev->tbusy in SkGeXmit.
 *	Changed large frame (jumbo) buffer number.
 *	Added copying of short frames.
 *	
 *	Revision 1.7  1999/03/04 13:26:57  cgoos
 *	Fixed spinlock calls for SMP.
 *	
 *	Revision 1.6  1999/03/02 09:53:51  cgoos
 *	Added descriptor revertion for big endian machines.
 *	
 *	Revision 1.5  1999/03/01 08:50:59  cgoos
 *	Fixed SkGeChangeMtu.
 *	Fixed pci config space accesses.
 *	
 *	Revision 1.4  1999/02/18 15:48:44  cgoos
 *	Corrected some printk's.
 *	
 *	Revision 1.3  1999/02/18 12:45:55  cgoos
 *	Changed SK_MAX_CARD_PARAM to default 16
 *	
 *	Revision 1.2  1999/02/18 10:55:32  cgoos
 *	Removed SkGeDrvTimeStamp function.
 *	Printing "ethX:" before adapter type at adapter init.
 *	
 *
 *	10-Feb-1999 cg	Created, based on Linux' acenic.c, 3c59x.c and 
 *			SysKonnects GEnesis Solaris driver
 *
 ******************************************************************************/

/******************************************************************************
 *
 * Possible compiler options (#define xxx / -Dxxx):
 *
 *	debugging can be enable by changing SK_DEBUG_CHKMOD and 
 *	SK_DEBUG_CHKCAT in makefile (described there).
 *
 ******************************************************************************/
 
/******************************************************************************
 *
 * Description:
 *
 *	This is the main module of the Linux GE driver.
 *	
 *	All source files except skge.c, skdrv1st.h, skdrv2nd.h and sktypes.h
 *	are part of SysKonnect's COMMON MODULES for the SK-98xx adapters.
 *	Those are used for drivers on multiple OS', so some thing may seem
 *	unnecessary complicated on Linux. Please do not try to 'clean up'
 *	them without VERY good reasons, because this will make it more
 *	difficult to keep the Linux driver in synchronisation with the
 *	other versions.
 *
 * Include file hierarchy:
 *
 *	<linux/module.h>
 *
 *	"h/skdrv1st.h"
 *		<linux/version.h>
 *		<linux/types.h>
 *		<linux/kernel.h>
 *		<linux/string.h>
 *		<linux/errno.h>
 *		<linux/ioport.h>
 *		<linux/malloc.h>
 *		<linux/interrupt.h>
 *		<linux/pci.h>
 *		<asm/byteorder.h>
 *		<asm/bitops.h>
 *		<asm/io.h>
 *		<linux/netdevice.h>
 *		<linux/etherdevice.h>
 *		<linux/skbuff.h>
 *	    those three depending on kernel version used:
 *		<linux/bios32.h>
 *		<linux/init.h>
 *		<asm/uaccess.h>
 *		<net/checksum.h>
 *
 *		"h/skerror.h"
 *		"h/skdebug.h"
 *		"h/sktypes.h"
 *		"h/lm80.h"
 *		"h/xmac_ii.h"
 *
 *      "h/skdrv2nd.h"
 *		"h/skqueue.h"
 *		"h/skgehwt.h"
 *		"h/sktimer.h"
 *		"h/ski2c.h"
 *		"h/skgepnmi.h"
 *		"h/skvpd.h"
 *		"h/skgehw.h"
 *		"h/skgeinit.h"
 *		"h/skaddr.h"
 *		"h/skgesirq.h"
 *		"h/skcsum.h"
 *		"h/skrlmt.h"
 *
 ******************************************************************************/

static const char SysKonnectFileId[] = "@(#)" __FILE__ " (C) SysKonnect.";
static const char SysKonnectBuildNumber[] =
	"@(#)SK-BUILD: 3.05 (20000907) PL: 01"; 

#include	<linux/module.h>
#include	<linux/init.h>

#include	"h/skdrv1st.h"
#include	"h/skdrv2nd.h"

/* defines ******************************************************************/

#define BOOT_STRING	"sk98lin: Network Device Driver v3.05\n" \
			"Copyright (C) 1999-2000 SysKonnect"

#define VER_STRING	"3.05"


/* for debuging on x86 only */
/* #define BREAKPOINT() asm(" int $3"); */

/* use of a transmit complete interrupt */
#define USE_TX_COMPLETE

/* use interrupt moderation (for tx complete only) */
// #define USE_INT_MOD
#define INTS_PER_SEC	1000

/*
 * threshold for copying small receive frames
 * set to 0 to avoid copying, set to 9001 to copy all frames
 */
#define SK_COPY_THRESHOLD	200

/* number of adapters that can be configured via command line params */
#define SK_MAX_CARD_PARAM	16

/*
 * use those defines for a compile-in version of the driver instead 
 * of command line parameters
 */
// #define AUTO_NEG_A	{"Sense", }
// #define AUTO_NEG_B	{"Sense", }
// #define DUP_CAP_A	{"Both", }
// #define DUP_CAP_B	{"Both", }
// #define FLOW_CTRL_A	{"SymOrRem", }
// #define FLOW_CTRL_B	{"SymOrRem", }
// #define ROLE_A	{"Auto", }
// #define ROLE_B	{"Auto", }
// #define PREF_PORT	{"A", }
// #define RLMT_MODE	{"CheckLink", }


#define DEV_KFREE_SKB(skb) dev_kfree_skb(skb)
#define DEV_KFREE_SKB_IRQ(skb) dev_kfree_skb_irq(skb)
#define DEV_KFREE_SKB_ANY(skb) dev_kfree_skb_any(skb)

/* function prototypes ******************************************************/
static void	FreeResources(struct net_device *dev);
int		init_module(void);
void		cleanup_module(void);
static int	SkGeBoardInit(struct net_device *dev, SK_AC *pAC);
static SK_BOOL	BoardAllocMem(SK_AC *pAC);
static void	BoardFreeMem(SK_AC *pAC);
static void	BoardInitMem(SK_AC *pAC);
static void	SetupRing(SK_AC*, void*, uintptr_t, RXD**, RXD**, RXD**,
			int*, SK_BOOL);

static void	SkGeIsr(int irq, void *dev_id, struct pt_regs *ptregs);
static void	SkGeIsrOnePort(int irq, void *dev_id, struct pt_regs *ptregs);
static int	SkGeOpen(struct net_device *dev);
static int	SkGeClose(struct net_device *dev);
static int	SkGeXmit(struct sk_buff *skb, struct net_device *dev);
static int	SkGeSetMacAddr(struct net_device *dev, void *p);
static void	SkGeSetRxMode(struct net_device *dev);
static struct net_device_stats *SkGeStats(struct net_device *dev);
static int	SkGeIoctl(struct net_device *dev, struct ifreq *rq, int cmd);
static void	GetConfiguration(SK_AC*);
static void	ProductStr(SK_AC*);
static int	XmitFrame(SK_AC*, TX_PORT*, struct sk_buff*);
static void	FreeTxDescriptors(SK_AC*pAC, TX_PORT*);
static void	FillRxRing(SK_AC*, RX_PORT*);
static SK_BOOL	FillRxDescriptor(SK_AC*, RX_PORT*);
static void	ReceiveIrq(SK_AC*, RX_PORT*);
static void	ClearAndStartRx(SK_AC*, int);
static void	ClearTxIrq(SK_AC*, int, int);
static void	ClearRxRing(SK_AC*, RX_PORT*);
static void	ClearTxRing(SK_AC*, TX_PORT*);
static void	SetQueueSizes(SK_AC	*pAC);
static int	SkGeChangeMtu(struct net_device *dev, int new_mtu);
static void	PortReInitBmu(SK_AC*, int);
static int	SkGeIocMib(SK_AC*, unsigned int, int);
#ifdef DEBUG
static void	DumpMsg(struct sk_buff*, char*);
static void	DumpData(char*, int);
static void	DumpLong(char*, int);
#endif


/* global variables *********************************************************/
static const char *BootString = BOOT_STRING;
static struct net_device *root_dev = NULL;
static int probed __initdata = 0;

/* local variables **********************************************************/
static uintptr_t TxQueueAddr[SK_MAX_MACS][2] = {{0x680, 0x600},{0x780, 0x700}};
static uintptr_t RxQueueAddr[SK_MAX_MACS] = {0x400, 0x480};

/*****************************************************************************
 *
 * 	skge_probe - find all SK-98xx adapters
 *
 * Description:
 *	This function scans the PCI bus for SK-98xx adapters. Resources for
 *	each adapter are allocated and the adapter is brought into Init 1
 *	state.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 */
static int __init skge_probe (void)
{
	int boards_found = 0;
	int		version_disp = 0;
	SK_AC		*pAC;
	struct pci_dev	*pdev = NULL;
	unsigned long	base_address;
	struct net_device *dev = NULL;

	if (probed)
		return -ENODEV;
	probed++;
	
	/* display driver info */
	if (!version_disp)
	{
		/* set display flag to TRUE so that */
		/* we only display this string ONCE */
		version_disp = 1;
		printk("%s\n", BootString);
	}

	if (!pci_present())		/* is PCI support present? */
		return -ENODEV;

	while((pdev = pci_find_device(PCI_VENDOR_ID_SYSKONNECT,
				      PCI_DEVICE_ID_SYSKONNECT_GE, pdev)) != NULL) {
		if (pci_enable_device(pdev))
			continue;

		dev = NULL;
		dev = init_etherdev(dev, sizeof(SK_AC));

		if (dev == NULL) {
			printk(KERN_ERR "Unable to allocate etherdev "
			       "structure!\n");
			break;
		}

		pAC = dev->priv;
		pAC->PciDev = *pdev;
		pAC->PciDevId = pdev->device;
		pAC->dev = dev;
		sprintf(pAC->Name, "SysKonnect SK-98xx");
		pAC->CheckQueue = SK_FALSE;

		dev->irq = pdev->irq;

		dev->open =		&SkGeOpen;
		dev->stop =		&SkGeClose;
		dev->hard_start_xmit =	&SkGeXmit;
		dev->get_stats =	&SkGeStats;
		dev->set_multicast_list = &SkGeSetRxMode;
		dev->set_mac_address =	&SkGeSetMacAddr;
		dev->do_ioctl =		&SkGeIoctl;
		dev->change_mtu =	&SkGeChangeMtu;
		
		/*
		 * Dummy value.
		 */
		dev->base_addr = 42;

		pci_set_master(pdev);

		base_address = pci_resource_start (pdev, 0);

#ifdef SK_BIG_ENDIAN
		/*
		 * On big endian machines, we use the adapter's aibility of
		 * reading the descriptors as big endian.
		 */
		{
		SK_U32		our2;
			SkPciReadCfgDWord(pAC, PCI_OUR_REG_2, &our2);
			our2 |= PCI_REV_DESC;
			SkPciWriteCfgDWord(pAC, PCI_OUR_REG_2, our2);
		}
#endif /* BIG ENDIAN */

		/*
		 * Remap the regs into kernel space.
		 */


		pAC->IoBase = (char*)ioremap(base_address, 0x4000);
		if (!pAC->IoBase){
			printk(KERN_ERR "%s:  Unable to map I/O register, "
			       "SK 98xx No. %i will be disabled.\n",
			       dev->name, boards_found);
			break;
		}
		pAC->Index = boards_found;

		if (SkGeBoardInit(dev, pAC)) {
			FreeResources(dev);
			continue;
		}

                memcpy((caddr_t) &dev->dev_addr,
			(caddr_t) &pAC->Addr.CurrentMacAddress, 6);
 
		boards_found++;

		/*
		 * This is bollocks, but we need to tell the net-init
		 * code that it shall go for the next device.
		 */
#ifndef MODULE
		dev->base_addr = 0;
#endif
	}

	/*
	 * If we're at this point we're going through skge_probe() for
	 * the first time.  Return success (0) if we've initialized 1
	 * or more boards. Otherwise, return failure (-ENODEV).
	 */

	return boards_found;
} /* skge_probe */


/*****************************************************************************
 *
 * 	FreeResources - release resources allocated for adapter
 *
 * Description:
 *	This function releases the IRQ, unmaps the IO and
 *	frees the desriptor ring.
 *
 * Returns: N/A
 *	
 */
static void FreeResources(struct net_device *dev)
{
SK_U32 AllocFlag;
SK_AC	*pAC;

	if (dev->priv) {
		pAC = (SK_AC*) dev->priv;
		AllocFlag = pAC->AllocFlag;
		if (AllocFlag & SK_ALLOC_IRQ) {
			free_irq(dev->irq, dev);
		}
		if (pAC->IoBase) {
			iounmap(pAC->IoBase);
		}
		if (pAC->pDescrMem) {
			BoardFreeMem(pAC);
		}
	}
	
} /* FreeResources */


MODULE_AUTHOR("Christoph Goos <cgoos@syskonnect.de>");
MODULE_DESCRIPTION("SysKonnect SK-NET Gigabit Ethernet SK-98xx driver");
MODULE_PARM(AutoNeg_A,  "1-" __MODULE_STRING(SK_MAX_CARD_PARAM) "s");
MODULE_PARM(AutoNeg_B,  "1-" __MODULE_STRING(SK_MAX_CARD_PARAM) "s");
MODULE_PARM(DupCap_A,   "1-" __MODULE_STRING(SK_MAX_CARD_PARAM) "s");
MODULE_PARM(DupCap_B,   "1-" __MODULE_STRING(SK_MAX_CARD_PARAM) "s");
MODULE_PARM(FlowCtrl_A, "1-" __MODULE_STRING(SK_MAX_CARD_PARAM) "s");
MODULE_PARM(FlowCtrl_B, "1-" __MODULE_STRING(SK_MAX_CARD_PARAM) "s");
MODULE_PARM(Role_A,	"1-" __MODULE_STRING(SK_MAX_CARD_PARAM) "s");
MODULE_PARM(Role_B,	"1-" __MODULE_STRING(SK_MAX_CARD_PARAM) "s");
MODULE_PARM(PrefPort,   "1-" __MODULE_STRING(SK_MAX_CARD_PARAM) "s");
MODULE_PARM(RlmtMode,   "1-" __MODULE_STRING(SK_MAX_CARD_PARAM) "s");
/* not used, just there because every driver should have them: */
MODULE_PARM(options,    "1-" __MODULE_STRING(SK_MAX_CARD_PARAM) "i");
MODULE_PARM(debug,      "i");


#ifdef AUTO_NEG_A
static char *AutoNeg_A[SK_MAX_CARD_PARAM] = AUTO_NEG_A;
#else
static char *AutoNeg_A[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef DUP_CAP_A
static char *DupCap_A[SK_MAX_CARD_PARAM] = DUP_CAP_A;
#else
static char *DupCap_A[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef FLOW_CTRL_A
static char *FlowCtrl_A[SK_MAX_CARD_PARAM] = FLOW_CTRL_A;
#else
static char *FlowCtrl_A[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef ROLE_A
static char *Role_A[SK_MAX_CARD_PARAM] = ROLE_A;
#else
static char *Role_A[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef AUTO_NEG_B
static char *AutoNeg_B[SK_MAX_CARD_PARAM] = AUTO_NEG_B;
#else
static char *AutoNeg_B[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef DUP_CAP_B
static char *DupCap_B[SK_MAX_CARD_PARAM] = DUP_CAP_B;
#else
static char *DupCap_B[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef FLOW_CTRL_B
static char *FlowCtrl_B[SK_MAX_CARD_PARAM] = FLOW_CTRL_B;
#else
static char *FlowCtrl_B[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef ROLE_B
static char *Role_B[SK_MAX_CARD_PARAM] = ROLE_B;
#else
static char *Role_B[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef PREF_PORT
static char *PrefPort[SK_MAX_CARD_PARAM] = PREF_PORT;
#else
static char *PrefPort[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef RLMT_MODE
static char *RlmtMode[SK_MAX_CARD_PARAM] = RLMT_MODE;
#else
static char *RlmtMode[SK_MAX_CARD_PARAM] = {"", };
#endif


static int debug = 0; /* not used */
static int options[SK_MAX_CARD_PARAM] = {0, }; /* not used */


/*****************************************************************************
 *
 * 	skge_init_module - module initialization function
 *
 * Description:
 *	Very simple, only call skge_probe and return approriate result.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 */
static int __init skge_init_module(void)
{
	int cards;

	root_dev = NULL;
	
	/* just to avoid warnings ... */
	debug = 0;
	options[0] = 0;

	cards = skge_probe();
	if (cards == 0) {
		printk("No adapter found\n");
	}
	return cards ? 0 : -ENODEV;
} /* skge_init_module */


/*****************************************************************************
 *
 * 	skge_cleanup_module - module unload function
 *
 * Description:
 *	Disable adapter if it is still running, free resources,
 *	free device struct.
 *
 * Returns: N/A
 */
static void __exit skge_cleanup_module(void)
{
SK_AC	*pAC;
struct net_device *next;
unsigned long Flags;
SK_EVPARA EvPara;

	while (root_dev) {
		pAC = (SK_AC*)root_dev->priv;
		next = pAC->Next;

		netif_stop_queue(root_dev);
		SkGeYellowLED(pAC, pAC->IoBase, 0);
		
		if(pAC->BoardLevel == 2) {
			/* board is still alive */
			spin_lock_irqsave(&pAC->SlowPathLock, Flags);
			SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_STOP, EvPara);
			SkEventDispatcher(pAC, pAC->IoBase);
			/* disable interrupts */
			SK_OUT32(pAC->IoBase, B0_IMSK, 0);
			SkGeDeInit(pAC, pAC->IoBase); 
			spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
			pAC->BoardLevel = 0;
			/* We do NOT check here, if IRQ was pending, of course*/
		}
	
		if(pAC->BoardLevel == 1) {
			/* board is still alive */
			SkGeDeInit(pAC, pAC->IoBase); 
			pAC->BoardLevel = 0;
		}
		
		FreeResources(root_dev);
		
		root_dev->get_stats = NULL;
		/* 
		 * otherwise unregister_netdev calls get_stats with
		 * invalid IO ...  :-(
		 */
		unregister_netdev(root_dev);
		kfree(root_dev);

		root_dev = next;
	}
} /* skge_cleanup_module */

module_init(skge_init_module);
module_exit(skge_cleanup_module);

/*****************************************************************************
 *
 * 	SkGeBoardInit - do level 0 and 1 initialization
 *
 * Description:
 *	This function prepares the board hardware for running. The desriptor
 *	ring is set up, the IRQ is allocated and the configuration settings
 *	are examined.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 */
static int __init SkGeBoardInit(struct net_device *dev, SK_AC *pAC)
{
short	i;
unsigned long Flags;
char	*DescrString = "sk98lin: Driver for Linux"; /* this is given to PNMI */
char	*VerStr	= VER_STRING;
int	Ret;			/* return code of request_irq */

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("IoBase: %08lX\n", (unsigned long)pAC->IoBase));
	for (i=0; i<SK_MAX_MACS; i++) {
		pAC->TxPort[i][0].HwAddr = pAC->IoBase + TxQueueAddr[i][0];
		pAC->TxPort[i][0].PortIndex = i;
		pAC->RxPort[i].HwAddr = pAC->IoBase + RxQueueAddr[i];
		pAC->RxPort[i].PortIndex = i;
	}

	/* Initialize the mutexes */

	for (i=0; i<SK_MAX_MACS; i++) {
		spin_lock_init(&pAC->TxPort[i][0].TxDesRingLock);
		spin_lock_init(&pAC->RxPort[i].RxDesRingLock);
	}
	spin_lock_init(&pAC->SlowPathLock);

	/* level 0 init common modules here */
	
	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	/* Does a RESET on board ...*/
	if (SkGeInit(pAC, pAC->IoBase, 0) != 0) {
		printk("HWInit (0) failed.\n");
		spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
		return(-EAGAIN);
	}
	SkI2cInit(  pAC, pAC->IoBase, 0);
	SkEventInit(pAC, pAC->IoBase, 0);
	SkPnmiInit( pAC, pAC->IoBase, 0);
	SkAddrInit( pAC, pAC->IoBase, 0);
	SkRlmtInit( pAC, pAC->IoBase, 0);
	SkTimerInit(pAC, pAC->IoBase, 0);
	
	pAC->BoardLevel = 0;
	pAC->RxBufSize = ETH_BUF_SIZE;

	SK_PNMI_SET_DRIVER_DESCR(pAC, DescrString);
	SK_PNMI_SET_DRIVER_VER(pAC, VerStr);

	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);

	GetConfiguration(pAC);

	/* level 1 init common modules here (HW init) */
	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	if (SkGeInit(pAC, pAC->IoBase, 1) != 0) {
		printk("HWInit (1) failed.\n");
		spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
		return(-EAGAIN);
	}
	SkI2cInit(  pAC, pAC->IoBase, 1);
	SkEventInit(pAC, pAC->IoBase, 1);
	SkPnmiInit( pAC, pAC->IoBase, 1);
	SkAddrInit( pAC, pAC->IoBase, 1);
	SkRlmtInit( pAC, pAC->IoBase, 1);
	SkTimerInit(pAC, pAC->IoBase, 1);

	pAC->BoardLevel = 1;
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);

	if (pAC->GIni.GIMacsFound == 2) {
		 Ret = request_irq(dev->irq, SkGeIsr, SA_SHIRQ, pAC->Name, dev);
	} else if (pAC->GIni.GIMacsFound == 1) {
		Ret = request_irq(dev->irq, SkGeIsrOnePort, SA_SHIRQ,
			pAC->Name, dev);
	} else {
		printk(KERN_WARNING "%s: illegal number of ports: %d\n",
		       dev->name, pAC->GIni.GIMacsFound);
		return -EAGAIN;
	}
	if (Ret) {
		printk(KERN_WARNING "%s: Requested IRQ %d is busy\n",
		       dev->name, dev->irq);
		return -EAGAIN;
	}
	pAC->AllocFlag |= SK_ALLOC_IRQ;

	/* Alloc memory for this board (Mem for RxD/TxD) : */
	if(!BoardAllocMem(pAC)) {
		printk("No memory for descriptor rings\n");
       		return(-EAGAIN);
	}

	SkCsSetReceiveFlags(pAC,
		SKCS_PROTO_IP | SKCS_PROTO_TCP | SKCS_PROTO_UDP,
		&pAC->CsOfs1, &pAC->CsOfs2);
	pAC->CsOfs = (pAC->CsOfs2 << 16) | pAC->CsOfs1;

	BoardInitMem(pAC);

	SetQueueSizes(pAC);

	/* Print adapter specific string from vpd */
	ProductStr(pAC);
	printk("%s: %s\n", dev->name, pAC->DeviceStr);

	/* Print configuration settings */
	printk("      PrefPort:%c  RlmtMode:%s\n",
		'A' + pAC->Rlmt.PrefPort,
		(pAC->RlmtMode==0)  ? "ChkLink" :
		((pAC->RlmtMode==1) ? "ChkLink" :
		((pAC->RlmtMode==3) ? "ChkOth" :
		((pAC->RlmtMode==7) ? "ChkSeg" : "Error"))));

	SkGeYellowLED(pAC, pAC->IoBase, 1);

	/*
	 * Register the device here
	 */
	pAC->Next = root_dev;
	root_dev = dev;

	return (0);
} /* SkGeBoardInit */


/*****************************************************************************
 *
 * 	BoardAllocMem - allocate the memory for the descriptor rings
 *
 * Description:
 *	This function allocates the memory for all descriptor rings.
 *	Each ring is aligned for the desriptor alignment and no ring
 *	has a 4 GByte boundary in it (because the upper 32 bit must
 *	be constant for all descriptiors in one rings).
 *
 * Returns:
 *	SK_TRUE, if all memory could be allocated
 *	SK_FALSE, if not
 */
static SK_BOOL BoardAllocMem(
SK_AC	*pAC)
{
caddr_t		pDescrMem;	/* pointer to descriptor memory area */
size_t		AllocLength;	/* length of complete descriptor area */
int		i;		/* loop counter */
unsigned long	BusAddr;

	
	/* rings plus one for alignment (do not cross 4 GB boundary) */
	/* RX_RING_SIZE is assumed bigger than TX_RING_SIZE */
#if (BITS_PER_LONG == 32)
	AllocLength = (RX_RING_SIZE + TX_RING_SIZE) * pAC->GIni.GIMacsFound + 8;
#else
	AllocLength = (RX_RING_SIZE + TX_RING_SIZE) * pAC->GIni.GIMacsFound
		+ RX_RING_SIZE + 8;
#endif
	pDescrMem = pci_alloc_consistent(&pAC->PciDev, AllocLength,
					 &pAC->pDescrMemDMA);
	if (pDescrMem == NULL) {
		return (SK_FALSE);
	}
	pAC->pDescrMem = pDescrMem;

	/* Descriptors need 8 byte alignment, and this is ensured
	 * by pci_alloc_consistent.
	 */
	BusAddr = (unsigned long) pAC->pDescrMemDMA;
	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
			("TX%d/A: pDescrMem: %lX,   PhysDescrMem: %lX\n",
			i, (unsigned long) pDescrMem,
			BusAddr));
		pAC->TxPort[i][0].pTxDescrRing = pDescrMem;
		pAC->TxPort[i][0].VTxDescrRing = BusAddr;
		pDescrMem += TX_RING_SIZE;
		BusAddr += TX_RING_SIZE;
	
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
			("RX%d: pDescrMem: %lX,   PhysDescrMem: %lX\n",
			i, (unsigned long) pDescrMem,
			(unsigned long)BusAddr));
		pAC->RxPort[i].pRxDescrRing = pDescrMem;
		pAC->RxPort[i].VRxDescrRing = BusAddr;
		pDescrMem += RX_RING_SIZE;
		BusAddr += RX_RING_SIZE;
	} /* for */
	
	return (SK_TRUE);
} /* BoardAllocMem */


/****************************************************************************
 *
 *	BoardFreeMem - reverse of BoardAllocMem
 *
 * Description:
 *	Free all memory allocated in BoardAllocMem: adapter context,
 *	descriptor rings, locks.
 *
 * Returns:	N/A
 */
static void BoardFreeMem(
SK_AC		*pAC)
{
size_t		AllocLength;	/* length of complete descriptor area */

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("BoardFreeMem\n"));
#if (BITS_PER_LONG == 32)
	AllocLength = (RX_RING_SIZE + TX_RING_SIZE) * pAC->GIni.GIMacsFound + 8;
#else
	AllocLength = (RX_RING_SIZE + TX_RING_SIZE) * pAC->GIni.GIMacsFound
		+ RX_RING_SIZE + 8;
#endif
	pci_free_consistent(&pAC->PciDev, AllocLength,
			    pAC->pDescrMem, pAC->pDescrMemDMA);
	pAC->pDescrMem = NULL;
} /* BoardFreeMem */


/*****************************************************************************
 *
 * 	BoardInitMem - initiate the descriptor rings
 *
 * Description:
 *	This function sets the descriptor rings up in memory.
 *	The adapter is initialized with the descriptor start addresses.
 *
 * Returns:	N/A
 */
static void BoardInitMem(
SK_AC	*pAC)	/* pointer to adapter context */
{
int	i;		/* loop counter */
int	RxDescrSize;	/* the size of a rx descriptor rounded up to alignment*/
int	TxDescrSize;	/* the size of a tx descriptor rounded up to alignment*/

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("BoardInitMem\n"));

	RxDescrSize = (((sizeof(RXD) - 1) / DESCR_ALIGN) + 1) * DESCR_ALIGN;
	pAC->RxDescrPerRing = RX_RING_SIZE / RxDescrSize;
	TxDescrSize = (((sizeof(TXD) - 1) / DESCR_ALIGN) + 1) * DESCR_ALIGN;
	pAC->TxDescrPerRing = TX_RING_SIZE / RxDescrSize;
	
	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		SetupRing(
			pAC,
			pAC->TxPort[i][0].pTxDescrRing,
			pAC->TxPort[i][0].VTxDescrRing,
			(RXD**)&pAC->TxPort[i][0].pTxdRingHead,
			(RXD**)&pAC->TxPort[i][0].pTxdRingTail,
			(RXD**)&pAC->TxPort[i][0].pTxdRingPrev,
			&pAC->TxPort[i][0].TxdRingFree,
			SK_TRUE);
		SetupRing(
			pAC,
			pAC->RxPort[i].pRxDescrRing,
			pAC->RxPort[i].VRxDescrRing,
			&pAC->RxPort[i].pRxdRingHead,
			&pAC->RxPort[i].pRxdRingTail,
			&pAC->RxPort[i].pRxdRingPrev,
			&pAC->RxPort[i].RxdRingFree,
			SK_FALSE);
	}
} /* BoardInitMem */


/*****************************************************************************
 *
 * 	SetupRing - create one descriptor ring
 *
 * Description:
 *	This function creates one descriptor ring in the given memory area.
 *	The head, tail and number of free descriptors in the ring are set.
 *
 * Returns:
 *	none
 */
static void SetupRing(
SK_AC		*pAC,
void		*pMemArea,	/* a pointer to the memory area for the ring */
uintptr_t	VMemArea,	/* the virtual bus address of the memory area */
RXD		**ppRingHead,	/* address where the head should be written */
RXD		**ppRingTail,	/* address where the tail should be written */
RXD		**ppRingPrev,	/* address where the tail should be written */
int		*pRingFree,	/* address where the # of free descr. goes */
SK_BOOL		IsTx)		/* flag: is this a tx ring */
{
int	i;		/* loop counter */
int	DescrSize;	/* the size of a descriptor rounded up to alignment*/
int	DescrNum;	/* number of descriptors per ring */
RXD	*pDescr;	/* pointer to a descriptor (receive or transmit) */
RXD	*pNextDescr;	/* pointer to the next descriptor */
RXD	*pPrevDescr;	/* pointer to the previous descriptor */
uintptr_t VNextDescr;	/* the virtual bus address of the next descriptor */

	if (IsTx == SK_TRUE) {
		DescrSize = (((sizeof(TXD) - 1) / DESCR_ALIGN) + 1) *
			DESCR_ALIGN;
		DescrNum = TX_RING_SIZE / DescrSize;
	}
	else {
		DescrSize = (((sizeof(RXD) - 1) / DESCR_ALIGN) + 1) *
			DESCR_ALIGN;
		DescrNum = RX_RING_SIZE / DescrSize;
	}
	
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
		("Descriptor size: %d   Descriptor Number: %d\n",
		DescrSize,DescrNum));
	
	pDescr = (RXD*) pMemArea;
	pPrevDescr = NULL;
	pNextDescr = (RXD*) (((char*)pDescr) + DescrSize);
	VNextDescr = VMemArea + DescrSize;
	for(i=0; i<DescrNum; i++) {
		/* set the pointers right */
		pDescr->VNextRxd = VNextDescr & 0xffffffffULL;
		pDescr->pNextRxd = pNextDescr;
		pDescr->TcpSumStarts = pAC->CsOfs;
		/* advance on step */
		pPrevDescr = pDescr;
		pDescr = pNextDescr;
		pNextDescr = (RXD*) (((char*)pDescr) + DescrSize);
		VNextDescr += DescrSize;
	}
	pPrevDescr->pNextRxd = (RXD*) pMemArea;
	pPrevDescr->VNextRxd = VMemArea;
	pDescr = (RXD*) pMemArea;
	*ppRingHead = (RXD*) pMemArea;
	*ppRingTail = *ppRingHead;
	*ppRingPrev = pPrevDescr;
	*pRingFree = DescrNum;
} /* SetupRing */


/*****************************************************************************
 *
 * 	PortReInitBmu - re-initiate the descriptor rings for one port
 *
 * Description:
 *	This function reinitializes the descriptor rings of one port
 *	in memory. The port must be stopped before.
 *	The HW is initialized with the descriptor start addresses.
 *
 * Returns:
 *	none
 */
static void PortReInitBmu(
SK_AC	*pAC,		/* pointer to adapter context */
int	PortIndex)	/* index of the port for which to re-init */
{
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("PortReInitBmu "));

	/* set address of first descriptor of ring in BMU */
	SK_OUT32(pAC->IoBase, TxQueueAddr[PortIndex][TX_PRIO_LOW]+
		TX_Q_CUR_DESCR_LOW,
		(uint32_t)(((caddr_t)
		(pAC->TxPort[PortIndex][TX_PRIO_LOW].pTxdRingHead) -
		pAC->TxPort[PortIndex][TX_PRIO_LOW].pTxDescrRing +
		pAC->TxPort[PortIndex][TX_PRIO_LOW].VTxDescrRing) &
		0xFFFFFFFF));
	SK_OUT32(pAC->IoBase, TxQueueAddr[PortIndex][TX_PRIO_LOW]+
		TX_Q_DESCR_HIGH,
		(uint32_t)(((caddr_t)
		(pAC->TxPort[PortIndex][TX_PRIO_LOW].pTxdRingHead) -
		pAC->TxPort[PortIndex][TX_PRIO_LOW].pTxDescrRing +
		pAC->TxPort[PortIndex][TX_PRIO_LOW].VTxDescrRing) >> 32));
	SK_OUT32(pAC->IoBase, RxQueueAddr[PortIndex]+RX_Q_CUR_DESCR_LOW,
		(uint32_t)(((caddr_t)(pAC->RxPort[PortIndex].pRxdRingHead) -
		pAC->RxPort[PortIndex].pRxDescrRing +
		pAC->RxPort[PortIndex].VRxDescrRing) & 0xFFFFFFFF));
	SK_OUT32(pAC->IoBase, RxQueueAddr[PortIndex]+RX_Q_DESCR_HIGH,
		(uint32_t)(((caddr_t)(pAC->RxPort[PortIndex].pRxdRingHead) -
		pAC->RxPort[PortIndex].pRxDescrRing +
		pAC->RxPort[PortIndex].VRxDescrRing) >> 32));
} /* PortReInitBmu */


/****************************************************************************
 *
 *	SkGeIsr - handle adapter interrupts
 *
 * Description:
 *	The interrupt routine is called when the network adapter
 *	generates an interrupt. It may also be called if another device
 *	shares this interrupt vector with the driver.
 *
 * Returns: N/A
 *
 */
static void SkGeIsr(int irq, void *dev_id, struct pt_regs *ptregs)
{
struct net_device *dev = (struct net_device *)dev_id;
SK_AC		*pAC;
SK_U32		IntSrc;		/* interrupts source register contents */	

	pAC = (SK_AC*) dev->priv;
	
	/*
	 * Check and process if its our interrupt
	 */
	SK_IN32(pAC->IoBase, B0_SP_ISRC, &IntSrc);
	if (IntSrc == 0) {
		return;
	}

	while (((IntSrc & IRQ_MASK) & ~SPECIAL_IRQS) != 0) {
#if 0 /* software irq currently not used */
		if (IntSrc & IRQ_SW) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("Software IRQ\n"));
		}
#endif
		if (IntSrc & IRQ_EOF_RX1) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF RX1 IRQ\n"));
			ReceiveIrq(pAC, &pAC->RxPort[0]);
			SK_PNMI_CNT_RX_INTR(pAC);
		}
		if (IntSrc & IRQ_EOF_RX2) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF RX2 IRQ\n"));
			ReceiveIrq(pAC, &pAC->RxPort[1]);
			SK_PNMI_CNT_RX_INTR(pAC);
		}
#ifdef USE_TX_COMPLETE /* only if tx complete interrupt used */
		if (IntSrc & IRQ_EOF_AS_TX1) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF AS TX1 IRQ\n"));
			SK_PNMI_CNT_TX_INTR(pAC);
			spin_lock(&pAC->TxPort[0][TX_PRIO_LOW].TxDesRingLock);
			FreeTxDescriptors(pAC, &pAC->TxPort[0][TX_PRIO_LOW]);
			spin_unlock(&pAC->TxPort[0][TX_PRIO_LOW].TxDesRingLock);
		}
		if (IntSrc & IRQ_EOF_AS_TX2) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF AS TX2 IRQ\n"));
			SK_PNMI_CNT_TX_INTR(pAC);
			spin_lock(&pAC->TxPort[1][TX_PRIO_LOW].TxDesRingLock);
			FreeTxDescriptors(pAC, &pAC->TxPort[1][TX_PRIO_LOW]);
			spin_unlock(&pAC->TxPort[1][TX_PRIO_LOW].TxDesRingLock);
		}
#if 0 /* only if sync. queues used */
		if (IntSrc & IRQ_EOF_SY_TX1) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF SY TX1 IRQ\n"));
			SK_PNMI_CNT_TX_INTR(pAC);
			spin_lock(&pAC->TxPort[0][TX_PRIO_HIGH].TxDesRingLock);
			FreeTxDescriptors(pAC, 0, TX_PRIO_HIGH);
			spin_unlock(&pAC->TxPort[0][TX_PRIO_HIGH].TxDesRingLock);
			ClearTxIrq(pAC, 0, TX_PRIO_HIGH);
		}
		if (IntSrc & IRQ_EOF_SY_TX2) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF SY TX2 IRQ\n"));
			SK_PNMI_CNT_TX_INTR(pAC);
			spin_lock(&pAC->TxPort[1][TX_PRIO_HIGH].TxDesRingLock);
			FreeTxDescriptors(pAC, 1, TX_PRIO_HIGH);
			spin_unlock(&pAC->TxPort[1][TX_PRIO_HIGH].TxDesRingLock);
			ClearTxIrq(pAC, 1, TX_PRIO_HIGH);
		}
#endif /* 0 */
#endif /* USE_TX_COMPLETE */

		/* do all IO at once */
		if (IntSrc & IRQ_EOF_RX1)
			ClearAndStartRx(pAC, 0);
		if (IntSrc & IRQ_EOF_RX2)
			ClearAndStartRx(pAC, 1);
#ifdef USE_TX_COMPLETE /* only if tx complete interrupt used */
		if (IntSrc & IRQ_EOF_AS_TX1)
			ClearTxIrq(pAC, 0, TX_PRIO_LOW);
		if (IntSrc & IRQ_EOF_AS_TX2)
			ClearTxIrq(pAC, 1, TX_PRIO_LOW);
#endif
		SK_IN32(pAC->IoBase, B0_ISRC, &IntSrc);
	} /* while (IntSrc & IRQ_MASK != 0) */

	if ((IntSrc & SPECIAL_IRQS) || pAC->CheckQueue) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
			("SPECIAL IRQ\n"));
		pAC->CheckQueue = SK_FALSE;
		spin_lock(&pAC->SlowPathLock);
		if (IntSrc & SPECIAL_IRQS)
			SkGeSirqIsr(pAC, pAC->IoBase, IntSrc);
		SkEventDispatcher(pAC, pAC->IoBase);
		spin_unlock(&pAC->SlowPathLock);
	}
	/*
	 * do it all again is case we cleared an interrupt that 
	 * came in after handling the ring (OUTs may be delayed
	 * in hardware buffers, but are through after IN)
	 */
	ReceiveIrq(pAC, &pAC->RxPort[pAC->ActivePort]);
//	ReceiveIrq(pAC, &pAC->RxPort[1]);

#if 0
// #ifdef USE_TX_COMPLETE /* only if tx complete interrupt used */
	spin_lock(&pAC->TxPort[0][TX_PRIO_LOW].TxDesRingLock);
	FreeTxDescriptors(pAC, &pAC->TxPort[0][TX_PRIO_LOW]);
	spin_unlock(&pAC->TxPort[0][TX_PRIO_LOW].TxDesRingLock);

	spin_lock(&pAC->TxPort[1][TX_PRIO_LOW].TxDesRingLock);
	FreeTxDescriptors(pAC, &pAC->TxPort[1][TX_PRIO_LOW]);
	spin_unlock(&pAC->TxPort[1][TX_PRIO_LOW].TxDesRingLock);

#if 0	/* only if sync. queues used */
	spin_lock(&pAC->TxPort[0][TX_PRIO_HIGH].TxDesRingLock);
	FreeTxDescriptors(pAC, 0, TX_PRIO_HIGH);
	spin_unlock(&pAC->TxPort[0][TX_PRIO_HIGH].TxDesRingLock);
	
	spin_lock(&pAC->TxPort[1][TX_PRIO_HIGH].TxDesRingLock);
	FreeTxDescriptors(pAC, 1, TX_PRIO_HIGH);
	spin_unlock(&pAC->TxPort[1][TX_PRIO_HIGH].TxDesRingLock);
#endif /* 0 */
#endif /* USE_TX_COMPLETE */

	/* IRQ is processed - Enable IRQs again*/
	SK_OUT32(pAC->IoBase, B0_IMSK, IRQ_MASK);

	return;
} /* SkGeIsr */


/****************************************************************************
 *
 *	SkGeIsrOnePort - handle adapter interrupts for single port adapter
 *
 * Description:
 *	The interrupt routine is called when the network adapter
 *	generates an interrupt. It may also be called if another device
 *	shares this interrupt vector with the driver.
 *	This is the same as above, but handles only one port.
 *
 * Returns: N/A
 *
 */
static void SkGeIsrOnePort(int irq, void *dev_id, struct pt_regs *ptregs)
{
struct net_device *dev = (struct net_device *)dev_id;
SK_AC		*pAC;
SK_U32		IntSrc;		/* interrupts source register contents */	

	pAC = (SK_AC*) dev->priv;
	
	/*
	 * Check and process if its our interrupt
	 */
	SK_IN32(pAC->IoBase, B0_SP_ISRC, &IntSrc);
	if (IntSrc == 0) {
		return;
	}

	while (((IntSrc & IRQ_MASK) & ~SPECIAL_IRQS) != 0) {
#if 0 /* software irq currently not used */
		if (IntSrc & IRQ_SW) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("Software IRQ\n"));
		}
#endif
		if (IntSrc & IRQ_EOF_RX1) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF RX1 IRQ\n"));
			ReceiveIrq(pAC, &pAC->RxPort[0]);
			SK_PNMI_CNT_RX_INTR(pAC);
		}
#ifdef USE_TX_COMPLETE /* only if tx complete interrupt used */
		if (IntSrc & IRQ_EOF_AS_TX1) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF AS TX1 IRQ\n"));
			SK_PNMI_CNT_TX_INTR(pAC);
			spin_lock(&pAC->TxPort[0][TX_PRIO_LOW].TxDesRingLock);
			FreeTxDescriptors(pAC, &pAC->TxPort[0][TX_PRIO_LOW]);
			spin_unlock(&pAC->TxPort[0][TX_PRIO_LOW].TxDesRingLock);
		}
#if 0 /* only if sync. queues used */
		if (IntSrc & IRQ_EOF_SY_TX1) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF SY TX1 IRQ\n"));
			SK_PNMI_CNT_TX_INTR(pAC);
			spin_lock(&pAC->TxPort[0][TX_PRIO_HIGH].TxDesRingLock);
			FreeTxDescriptors(pAC, 0, TX_PRIO_HIGH);
			spin_unlock(&pAC->TxPort[0][TX_PRIO_HIGH].TxDesRingLock);
			ClearTxIrq(pAC, 0, TX_PRIO_HIGH);
		}
#endif /* 0 */
#endif /* USE_TX_COMPLETE */

		/* do all IO at once */
		if (IntSrc & IRQ_EOF_RX1)
			ClearAndStartRx(pAC, 0);
#ifdef USE_TX_COMPLETE /* only if tx complete interrupt used */
		if (IntSrc & IRQ_EOF_AS_TX1)
			ClearTxIrq(pAC, 0, TX_PRIO_LOW);
#endif
		SK_IN32(pAC->IoBase, B0_ISRC, &IntSrc);
	} /* while (IntSrc & IRQ_MASK != 0) */
	
	if ((IntSrc & SPECIAL_IRQS) || pAC->CheckQueue) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
			("SPECIAL IRQ\n"));
		pAC->CheckQueue = SK_FALSE;
		spin_lock(&pAC->SlowPathLock);
		if (IntSrc & SPECIAL_IRQS)
			SkGeSirqIsr(pAC, pAC->IoBase, IntSrc);
		SkEventDispatcher(pAC, pAC->IoBase);
		spin_unlock(&pAC->SlowPathLock);
	}
	/*
	 * do it all again is case we cleared an interrupt that 
	 * came in after handling the ring (OUTs may be delayed
	 * in hardware buffers, but are through after IN)
	 */
	ReceiveIrq(pAC, &pAC->RxPort[0]);

#if 0
// #ifdef USE_TX_COMPLETE /* only if tx complete interrupt used */
	spin_lock(&pAC->TxPort[0][TX_PRIO_LOW].TxDesRingLock);
	FreeTxDescriptors(pAC, &pAC->TxPort[0][TX_PRIO_LOW]);
	spin_unlock(&pAC->TxPort[0][TX_PRIO_LOW].TxDesRingLock);

#if 0	/* only if sync. queues used */
	spin_lock(&pAC->TxPort[0][TX_PRIO_HIGH].TxDesRingLock);
	FreeTxDescriptors(pAC, 0, TX_PRIO_HIGH);
	spin_unlock(&pAC->TxPort[0][TX_PRIO_HIGH].TxDesRingLock);
	
#endif /* 0 */
#endif /* USE_TX_COMPLETE */

	/* IRQ is processed - Enable IRQs again*/
	SK_OUT32(pAC->IoBase, B0_IMSK, IRQ_MASK);

	return;
} /* SkGeIsrOnePort */


/****************************************************************************
 *
 *	SkGeOpen - handle start of initialized adapter
 *
 * Description:
 *	This function starts the initialized adapter.
 *	The board level variable is set and the adapter is
 *	brought to full functionality.
 *	The device flags are set for operation.
 *	Do all necessary level 2 initialization, enable interrupts and
 *	give start command to RLMT.
 *
 * Returns:
 *	0 on success
 *	!= 0 on error
 */
static int SkGeOpen(
struct net_device	*dev)
{
SK_AC		*pAC;		/* pointer to adapter context struct */
unsigned int	Flags;		/* for spin lock */
int		i;
SK_EVPARA	EvPara;		/* an event parameter union */

	pAC = (SK_AC*) dev->priv;
	
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeOpen: pAC=0x%lX:\n", (unsigned long)pAC));

	if (pAC->BoardLevel == 0) {
		/* level 1 init common modules here */
		if (SkGeInit(pAC, pAC->IoBase, 1) != 0) {
			printk("%s: HWInit(1) failed\n", pAC->dev->name);
			return (-1);
		}
		SkI2cInit	(pAC, pAC->IoBase, 1);
		SkEventInit	(pAC, pAC->IoBase, 1);
		SkPnmiInit	(pAC, pAC->IoBase, 1);
		SkAddrInit	(pAC, pAC->IoBase, 1);
		SkRlmtInit	(pAC, pAC->IoBase, 1);
		SkTimerInit	(pAC, pAC->IoBase, 1);
		pAC->BoardLevel = 1;
	}
		
	/* level 2 init modules here */
	SkGeInit	(pAC, pAC->IoBase, 2);
	SkI2cInit	(pAC, pAC->IoBase, 2);
	SkEventInit	(pAC, pAC->IoBase, 2);
	SkPnmiInit	(pAC, pAC->IoBase, 2);
	SkAddrInit	(pAC, pAC->IoBase, 2);
	SkRlmtInit	(pAC, pAC->IoBase, 2);
	SkTimerInit	(pAC, pAC->IoBase, 2);
	pAC->BoardLevel = 2;
	
	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		// Enable transmit descriptor polling.
		SkGePollTxD(pAC, pAC->IoBase, i, SK_TRUE);
		FillRxRing(pAC, &pAC->RxPort[i]);
	}
	SkGeYellowLED(pAC, pAC->IoBase, 1);

#ifdef USE_INT_MOD
// moderate only TX complete interrupts (these are not time critical)
#define IRQ_MOD_MASK (IRQ_EOF_AS_TX1 | IRQ_EOF_AS_TX2)
	{
		unsigned long ModBase;
		ModBase = 53125000 / INTS_PER_SEC;
		SK_OUT32(pAC->IoBase, B2_IRQM_INI, ModBase);
		SK_OUT32(pAC->IoBase, B2_IRQM_MSK, IRQ_MOD_MASK);
		SK_OUT32(pAC->IoBase, B2_IRQM_CTRL, TIM_START);
	}
#endif

	/* enable Interrupts */
	SK_OUT32(pAC->IoBase, B0_IMSK, IRQ_MASK);
	SK_OUT32(pAC->IoBase, B0_HWE_IMSK, IRQ_HWE_MASK);

	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_START, EvPara);
	if (pAC->RlmtMode != 0) {
		EvPara.Para32[0] = pAC->RlmtMode;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_MODE_CHANGE,
			EvPara);
	}
	SkEventDispatcher(pAC, pAC->IoBase);
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	
	MOD_INC_USE_COUNT;
	
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeOpen suceeded\n"));

	return (0);
} /* SkGeOpen */


/****************************************************************************
 *
 *	SkGeClose - Stop initialized adapter
 *
 * Description:
 *	Close initialized adapter.
 *
 * Returns:
 *	0 - on success
 *	error code - on error
 */
static int SkGeClose(
struct net_device	*dev)
{
SK_AC		*pAC;
unsigned int	Flags;		/* for spin lock */
int		i;
SK_EVPARA	EvPara;

	netif_stop_queue(dev);

	pAC = (SK_AC*) dev->priv;
	
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeClose: pAC=0x%lX ", (unsigned long)pAC));

	/* 
	 * Clear multicast table, promiscuous mode ....
	 */
	SkAddrMcClear(pAC, pAC->IoBase, pAC->ActivePort, 0);
	SkAddrPromiscuousChange(pAC, pAC->IoBase, pAC->ActivePort,
		SK_PROM_MODE_NONE);


	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	/* disable interrupts */
	SK_OUT32(pAC->IoBase, B0_IMSK, 0);
	SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_STOP, EvPara);
	SkEventDispatcher(pAC, pAC->IoBase);
	SK_OUT32(pAC->IoBase, B0_IMSK, 0);
	/* stop the hardware */
	SkGeDeInit(pAC, pAC->IoBase);
	pAC->BoardLevel = 0;
	
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);

	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		/* clear all descriptor rings */
		ReceiveIrq(pAC, &pAC->RxPort[i]);
		ClearRxRing(pAC, &pAC->RxPort[i]);
		ClearTxRing(pAC, &pAC->TxPort[i][TX_PRIO_LOW]);
	}

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeClose: done "));

	MOD_DEC_USE_COUNT;
	
	return (0);
} /* SkGeClose */

/*****************************************************************************
 *
 * 	SkGeXmit - Linux frame transmit function
 *
 * Description:
 *	The system calls this function to send frames onto the wire.
 *	It puts the frame in the tx descriptor ring. If the ring is
 *	full then, the 'tbusy' flag is set.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 * WARNING: returning 1 in 'tbusy' case caused system crashes (double
 *	allocated skb's) !!!
 */
static int SkGeXmit(struct sk_buff *skb, struct net_device *dev)
{
SK_AC		*pAC;
int		Rc;	/* return code of XmitFrame */
	
	pAC = (SK_AC*) dev->priv;

	Rc = XmitFrame(pAC, &pAC->TxPort[pAC->ActivePort][TX_PRIO_LOW], skb);

	/* Transmitter out of resources? */
	if (Rc <= 0)
		netif_stop_queue(dev);

	/* If not taken, give buffer ownership back to the
	 * queueing layer.
	 */
	if (Rc < 0)
		return (1);

	dev->trans_start = jiffies;
	return (0);
} /* SkGeXmit */
	

/*****************************************************************************
 *
 * 	XmitFrame - fill one socket buffer into the transmit ring
 *
 * Description:
 *	This function puts a message into the transmit descriptor ring
 *	if there is a descriptors left.
 *	Linux skb's consist of only one continuous buffer.
 *	The first step locks the ring. It is held locked
 *	all time to avoid problems with SWITCH_../PORT_RESET.
 *	Then the descriptoris allocated.
 *	The second part is linking the buffer to the descriptor.
 *	At the very last, the Control field of the descriptor
 *	is made valid for the BMU and a start TX command is given
 *	if necessary.
 *
 * Returns:
 *	> 0 - on succes: the number of bytes in the message
 *	= 0 - on resource shortage: this frame sent or dropped, now
 *        the ring is full ( -> set tbusy)
 *	< 0 - on failure: other problems ( -> return failure to upper layers)
 */
static int XmitFrame(
SK_AC 		*pAC,		/* pointer to adapter context */
TX_PORT		*pTxPort,	/* pointer to struct of port to send to */
struct sk_buff	*pMessage)	/* pointer to send-message */
{
TXD		*pTxd;		/* the rxd to fill */
unsigned int	Flags;
SK_U64		PhysAddr;
int		BytesSend;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
		("X"));

	spin_lock_irqsave(&pTxPort->TxDesRingLock, Flags);

	if (pTxPort->TxdRingFree == 0) {
		/* no enough free descriptors in ring at the moment */
		FreeTxDescriptors(pAC, pTxPort);
		if (pTxPort->TxdRingFree == 0) {
			spin_unlock_irqrestore(&pTxPort->TxDesRingLock, Flags);
			SK_PNMI_CNT_NO_TX_BUF(pAC);
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_TX_PROGRESS,
				("XmitFrame failed\n"));
			/* this message can not be sent now */
			return (-1);
		}
	}
	/* advance head counter behind descriptor needed for this frame */
	pTxd = pTxPort->pTxdRingHead;
	pTxPort->pTxdRingHead = pTxd->pNextTxd;
	pTxPort->TxdRingFree--;
	/* the needed descriptor is reserved now */
	
	/* 
	 * everything allocated ok, so add buffer to descriptor
	 */

#ifdef SK_DUMP_TX
	DumpMsg(pMessage, "XmitFrame");
#endif

	/* set up descriptor and CONTROL dword */
	PhysAddr = (SK_U64) pci_map_single(&pAC->PciDev,
					   pMessage->data,
					   pMessage->len,
					   PCI_DMA_TODEVICE);
	pTxd->VDataLow = (SK_U32)  (PhysAddr & 0xffffffff);
	pTxd->VDataHigh = (SK_U32) (PhysAddr >> 32);
	pTxd->pMBuf = pMessage;
	pTxd->TBControl = TX_CTRL_OWN_BMU | TX_CTRL_STF |
		TX_CTRL_CHECK_DEFAULT | TX_CTRL_SOFTWARE |
#ifdef USE_TX_COMPLETE
		TX_CTRL_EOF | TX_CTRL_EOF_IRQ | pMessage->len;
#else
		TX_CTRL_EOF | pMessage->len;
#endif
	
	if ((pTxPort->pTxdRingPrev->TBControl & TX_CTRL_OWN_BMU) == 0) {
		/* previous descriptor already done, so give tx start cmd */
		/* StartTx(pAC, pTxPort->HwAddr); */
		SK_OUT8(pTxPort->HwAddr, TX_Q_CTRL, TX_Q_CTRL_START);
	}
	pTxPort->pTxdRingPrev = pTxd;
	
	
	BytesSend = pMessage->len;
	/* after releasing the lock, the skb may be immidiately freed */
	if (pTxPort->TxdRingFree != 0) {
		spin_unlock_irqrestore(&pTxPort->TxDesRingLock, Flags);
		return (BytesSend);
	}
	else {
		/* ring full: set tbusy on return */
		spin_unlock_irqrestore(&pTxPort->TxDesRingLock, Flags);
		return (0);
	}
} /* XmitFrame */


/*****************************************************************************
 *
 * 	FreeTxDescriptors - release descriptors from the descriptor ring
 *
 * Description:
 *	This function releases descriptors from a transmit ring if they
 *	have been sent by the BMU.
 *	If a descriptors is sent, it can be freed and the message can
 *	be freed, too.
 *	The SOFTWARE controllable bit is used to prevent running around a
 *	completely free ring for ever. If this bit is no set in the
 *	frame (by XmitFrame), this frame has never been sent or is
 *	already freed.
 *	The Tx descriptor ring lock must be held while calling this function !!!
 *
 * Returns:
 *	none
 */
static void FreeTxDescriptors(
SK_AC	*pAC,		/* pointer to the adapter context */
TX_PORT	*pTxPort)	/* pointer to destination port structure */
{
TXD	*pTxd;		/* pointer to the checked descriptor */
TXD	*pNewTail;	/* pointer to 'end' of the ring */
SK_U32	Control;	/* TBControl field of descriptor */
SK_U64	PhysAddr;	/* address of DMA mapping */

	pNewTail = pTxPort->pTxdRingTail;
	pTxd = pNewTail;
	
	/* 
	 * loop forever; exits if TX_CTRL_SOFTWARE bit not set in start frame
	 * or TX_CTRL_OWN_BMU bit set in any frame
	 */
	while (1) {
		Control = pTxd->TBControl;
		if ((Control & TX_CTRL_SOFTWARE) == 0) {
			/* 
			 * software controllable bit is set in first
			 * fragment when given to BMU. Not set means that
			 * this fragment was never sent or is already 
			 * freed ( -> ring completely free now).
			 */
			pTxPort->pTxdRingTail = pTxd;
			netif_start_queue(pAC->dev);
			return;
		}
		if (Control & TX_CTRL_OWN_BMU) {
			pTxPort->pTxdRingTail = pTxd;
			if (pTxPort->TxdRingFree > 0) {
				netif_start_queue(pAC->dev);
			}
			return;
		}
		
		/* release the DMA mapping */
		PhysAddr = ((SK_U64) pTxd->VDataHigh) << (SK_U64) 32;
		PhysAddr |= (SK_U64) pTxd->VDataLow;
		pci_unmap_single(&pAC->PciDev, PhysAddr,
				 pTxd->pMBuf->len,
				 PCI_DMA_TODEVICE);

		/* free message */
		DEV_KFREE_SKB_ANY(pTxd->pMBuf);
		pTxPort->TxdRingFree++;
		pTxd->TBControl &= ~TX_CTRL_SOFTWARE;
		pTxd = pTxd->pNextTxd; /* point behind fragment with EOF */
	} /* while(forever) */
} /* FreeTxDescriptors */


/*****************************************************************************
 *
 * 	FillRxRing - fill the receive ring with valid descriptors
 *
 * Description:
 *	This function fills the receive ring descriptors with data
 *	segments and makes them valid for the BMU.
 *	The active ring is filled completely, if possible.
 *	The non-active ring is filled only partial to save memory.
 *
 * Description of rx ring structure:
 *	head - points to the descriptor which will be used next by the BMU
 *	tail - points to the next descriptor to give to the BMU
 *	
 * Returns:	N/A
 */
static void FillRxRing(
SK_AC		*pAC,		/* pointer to the adapter context */
RX_PORT		*pRxPort)	/* ptr to port struct for which the ring
				   should be filled */
{
unsigned int	Flags;

	spin_lock_irqsave(&pRxPort->RxDesRingLock, Flags);
	while (pRxPort->RxdRingFree > pRxPort->RxFillLimit) {
		if(!FillRxDescriptor(pAC, pRxPort))
			break;
	}
	spin_unlock_irqrestore(&pRxPort->RxDesRingLock, Flags);
} /* FillRxRing */


/*****************************************************************************
 *
 * 	FillRxDescriptor - fill one buffer into the receive ring
 *
 * Description:
 *	The function allocates a new receive buffer and
 *	puts it into the next descriptor.
 *
 * Returns:
 *	SK_TRUE - a buffer was added to the ring
 *	SK_FALSE - a buffer could not be added
 */
static SK_BOOL FillRxDescriptor(
SK_AC		*pAC,		/* pointer to the adapter context struct */
RX_PORT		*pRxPort)	/* ptr to port struct of ring to fill */
{
struct sk_buff	*pMsgBlock;	/* pointer to a new message block */
RXD		*pRxd;		/* the rxd to fill */
SK_U16		Length;		/* data fragment length */
SK_U64		PhysAddr;	/* physical address of a rx buffer */

	pMsgBlock = alloc_skb(pAC->RxBufSize, GFP_ATOMIC);
	if (pMsgBlock == NULL) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
			SK_DBGCAT_DRV_ENTRY,
			("%s: Allocation of rx buffer failed !\n",
			pAC->dev->name));
		SK_PNMI_CNT_NO_RX_BUF(pAC);
		return(SK_FALSE);
	}
	skb_reserve(pMsgBlock, 2); /* to align IP frames */
	/* skb allocated ok, so add buffer */
	pRxd = pRxPort->pRxdRingTail;
	pRxPort->pRxdRingTail = pRxd->pNextRxd;
	pRxPort->RxdRingFree--;
	Length = pAC->RxBufSize;
	PhysAddr = (SK_U64) pci_map_single(&pAC->PciDev,
					   pMsgBlock->data,
					   pAC->RxBufSize - 2,
					   PCI_DMA_FROMDEVICE);
	pRxd->VDataLow = (SK_U32) (PhysAddr & 0xffffffff);
	pRxd->VDataHigh = (SK_U32) (PhysAddr >> 32);
	pRxd->pMBuf = pMsgBlock;
	pRxd->RBControl = RX_CTRL_OWN_BMU | RX_CTRL_STF |
		RX_CTRL_EOF_IRQ | RX_CTRL_CHECK_CSUM | Length;
	return (SK_TRUE);

} /* FillRxDescriptor */


/*****************************************************************************
 *
 * 	ReQueueRxBuffer - fill one buffer back into the receive ring
 *
 * Description:
 *	Fill a given buffer back into the rx ring. The buffer
 *	has been previously allocated and aligned, and its phys.
 *	address calculated, so this is no more necessary.
 *
 * Returns: N/A
 */
static void ReQueueRxBuffer(
SK_AC		*pAC,		/* pointer to the adapter context struct */
RX_PORT		*pRxPort,	/* ptr to port struct of ring to fill */
struct sk_buff	*pMsg,		/* pointer to the buffer */
SK_U32		PhysHigh,	/* phys address high dword */
SK_U32		PhysLow)	/* phys address low dword */
{
RXD		*pRxd;		/* the rxd to fill */
SK_U16		Length;		/* data fragment length */

	pRxd = pRxPort->pRxdRingTail;
	pRxPort->pRxdRingTail = pRxd->pNextRxd;
	pRxPort->RxdRingFree--;
	Length = pAC->RxBufSize;
	pRxd->VDataLow = PhysLow;
	pRxd->VDataHigh = PhysHigh;
	pRxd->pMBuf = pMsg;
	pRxd->RBControl = RX_CTRL_OWN_BMU | RX_CTRL_STF |
		RX_CTRL_EOF_IRQ | RX_CTRL_CHECK_CSUM | Length;
	return;
} /* ReQueueRxBuffer */


/*****************************************************************************
 *
 * 	ReceiveIrq - handle a receive IRQ
 *
 * Description:
 *	This function is called when a receive IRQ is set.
 *	It walks the receive descriptor ring and sends up all
 *	frames that are complete.
 *
 * Returns:	N/A
 */
static void ReceiveIrq(
SK_AC		*pAC,		/* pointer to adapter context */
RX_PORT		*pRxPort)	/* pointer to receive port struct */
{
RXD		*pRxd;		/* pointer to receive descriptors */
SK_U32		Control;	/* control field of descriptor */
struct sk_buff	*pMsg;		/* pointer to message holding frame */
struct sk_buff	*pNewMsg;	/* pointer to a new message for copying frame */
int		FrameLength;	/* total length of received frame */
SK_MBUF		*pRlmtMbuf;	/* ptr to a buffer for giving a frame to rlmt */
SK_EVPARA	EvPara;		/* an event parameter union */	
int		PortIndex = pRxPort->PortIndex;
unsigned int	Offset;
unsigned int	NumBytes;
unsigned int	ForRlmt;
SK_BOOL		IsBc;
SK_BOOL		IsMc;
SK_U32		FrameStat;
unsigned short	Csum1;
unsigned short	Csum2;
unsigned short	Type;
int		Result;
SK_U64		PhysAddr;


rx_start:	
	/* do forever; exit if RX_CTRL_OWN_BMU found */
	while (pRxPort->RxdRingFree < pAC->RxDescrPerRing) {
		pRxd = pRxPort->pRxdRingHead;
		
		Control = pRxd->RBControl;
		
		/* check if this descriptor is ready */
		if ((Control & RX_CTRL_OWN_BMU) != 0) {
			/* this descriptor is not yet ready */
			FillRxRing(pAC, pRxPort);
			return;
		}
		
		/* get length of frame and check it */
		FrameLength = Control & RX_CTRL_LEN_MASK;
		if (FrameLength > pAC->RxBufSize)
			goto rx_failed;

		/* check for STF and EOF */
		if ((Control & (RX_CTRL_STF | RX_CTRL_EOF)) !=
			(RX_CTRL_STF | RX_CTRL_EOF))
			goto rx_failed;
		
		/* here we have a complete frame in the ring */
		pMsg = pRxd->pMBuf;
		
		/*
		 * if short frame then copy data to reduce memory waste
		 */
		pNewMsg = NULL;
		if (FrameLength < SK_COPY_THRESHOLD) {
			pNewMsg = alloc_skb(FrameLength+2, GFP_ATOMIC);
			if (pNewMsg != NULL) {
				PhysAddr = ((SK_U64) pRxd->VDataHigh) << (SK_U64)32;
				PhysAddr |= (SK_U64) pRxd->VDataLow;

				/* use new skb and copy data */
				skb_reserve(pNewMsg, 2);
				skb_put(pNewMsg, FrameLength);
				pci_dma_sync_single(&pAC->PciDev,
						    (dma_addr_t) PhysAddr,
						    FrameLength,
						    PCI_DMA_FROMDEVICE);
				eth_copy_and_sum(pNewMsg, pMsg->data,
					FrameLength, 0);
				ReQueueRxBuffer(pAC, pRxPort, pMsg,
					pRxd->VDataHigh, pRxd->VDataLow);
				pMsg = pNewMsg;
			}
		}

		/*
		 * if large frame, or SKB allocation failed, pass
		 * the SKB directly to the networking
		 */
		if (pNewMsg == NULL) {
			PhysAddr = ((SK_U64) pRxd->VDataHigh) << (SK_U64)32;
			PhysAddr |= (SK_U64) pRxd->VDataLow;

			/* release the DMA mapping */
			pci_unmap_single(&pAC->PciDev,
					 PhysAddr,
					 pAC->RxBufSize - 2,
					 PCI_DMA_FROMDEVICE);

			/* set length in message */
			skb_put(pMsg, FrameLength);
			/* hardware checksum */
			Type = ntohs(*((short*)&pMsg->data[12]));
			if (Type == 0x800) {
				Csum1=le16_to_cpu(pRxd->TcpSums & 0xffff);
				Csum2=le16_to_cpu((pRxd->TcpSums >> 16) & 0xffff);
				if ((Csum1 & 0xfffe) && (Csum2 & 0xfffe)) {
					Result = SkCsGetReceiveInfo(pAC,
						&pMsg->data[14], 
						Csum1, Csum2);
					if (Result == 
						SKCS_STATUS_IP_FRAGMENT ||
						Result ==
						SKCS_STATUS_IP_CSUM_OK ||
						Result ==
						SKCS_STATUS_TCP_CSUM_OK ||
						Result ==
						SKCS_STATUS_UDP_CSUM_OK) {
						pMsg->ip_summed =
						CHECKSUM_UNNECESSARY;
					}
				} /* checksum calculation valid */
			} /* IP frame */
		} /* frame > SK_COPY_TRESHOLD */
		
		FrameStat = pRxd->FrameStat;
                if ((FrameStat & XMR_FS_LNG_ERR) != 0) {
                        /* jumbo frame, count to correct statistic */
                        SK_PNMI_CNT_RX_LONGFRAMES(pAC, pRxPort->PortIndex);
                }
		pRxd = pRxd->pNextRxd;
		pRxPort->pRxdRingHead = pRxd;
		pRxPort->RxdRingFree ++;
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
			("Received frame of length %d on port %d\n",
			FrameLength, PortIndex));
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
			("Number of free rx descriptors: %d\n",
			pRxPort->RxdRingFree));
		
		if ((Control & RX_CTRL_STAT_VALID) == RX_CTRL_STAT_VALID &&
			(FrameStat & XMR_FS_ANY_ERR) == 0) {
			// was the following, changed to allow VLAN support
			// (XMR_FS_ANY_ERR | XMR_FS_1L_VLAN | XMR_FS_2L_VLAN)
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_RX_PROGRESS,("V"));
			ForRlmt = SK_RLMT_RX_PROTOCOL;
			IsBc = (FrameStat & XMR_FS_BC)==XMR_FS_BC;
			SK_RLMT_PRE_LOOKAHEAD(pAC, PortIndex, FrameLength,
				IsBc, &Offset, &NumBytes);
			if (NumBytes != 0) {
				IsMc = (FrameStat & XMR_FS_MC)==XMR_FS_MC;
				SK_RLMT_LOOKAHEAD(pAC, PortIndex, 
					&pMsg->data[Offset],
					IsBc, IsMc, &ForRlmt);
			}
			if (ForRlmt == SK_RLMT_RX_PROTOCOL) {
				/* send up only frames from active port */
			    	if (PortIndex == pAC->ActivePort) {
					/* frame for upper layer */
					SK_DBG_MSG(NULL, SK_DBGMOD_DRV, 
						SK_DBGCAT_DRV_RX_PROGRESS,
						("U"));
#ifdef DUMP_RX
					DumpMsg(pMsg, "Rx");
#endif
					pMsg->dev = pAC->dev;
					pMsg->protocol = eth_type_trans(pMsg,
						pAC->dev);
					SK_PNMI_CNT_RX_OCTETS_DELIVERED(pAC,
						FrameLength);
					netif_rx(pMsg);
					pAC->dev->last_rx = jiffies;
				}
				else {
					/* drop frame */
					SK_DBG_MSG(NULL, SK_DBGMOD_DRV, 
						SK_DBGCAT_DRV_RX_PROGRESS,
						("D"));
					DEV_KFREE_SKB_IRQ(pMsg);
				}
			} /* if not for rlmt */
			else {
				/* packet for rlmt */
				SK_DBG_MSG(NULL, SK_DBGMOD_DRV, 
					SK_DBGCAT_DRV_RX_PROGRESS, ("R"));
				pRlmtMbuf = SkDrvAllocRlmtMbuf(pAC,
					pAC->IoBase, FrameLength);
				if (pRlmtMbuf != NULL) {
					pRlmtMbuf->pNext = NULL;
					pRlmtMbuf->Length = FrameLength;
					pRlmtMbuf->PortIdx = PortIndex;
					EvPara.pParaPtr = pRlmtMbuf;
					memcpy((char*)(pRlmtMbuf->pData),
					       (char*)(pMsg->data),
					       FrameLength);
					SkEventQueue(pAC, SKGE_RLMT,
						SK_RLMT_PACKET_RECEIVED,
						EvPara);
					pAC->CheckQueue = SK_TRUE;
					SK_DBG_MSG(NULL, SK_DBGMOD_DRV, 
						SK_DBGCAT_DRV_RX_PROGRESS,
						("Q"));
				}
				if ((pAC->dev->flags & 
					(IFF_PROMISC | IFF_ALLMULTI)) != 0 ||
					(ForRlmt & SK_RLMT_RX_PROTOCOL) == 
					SK_RLMT_RX_PROTOCOL) { 
					pMsg->dev = pAC->dev;
					pMsg->protocol = eth_type_trans(pMsg,
						pAC->dev);
					netif_rx(pMsg);
					pAC->dev->last_rx = jiffies;
				}
				else {
					DEV_KFREE_SKB_IRQ(pMsg);
				}

			} /* if packet for rlmt */
		} /* if valid frame */
		else {
			/* there is a receive error in this frame */
			if ((FrameStat & XMR_FS_1L_VLAN) != 0) {
				printk("%s: Received frame"
					" with VLAN Level 1 header, check"
					" switch configuration\n",
					pAC->dev->name);
			}
			if ((FrameStat & XMR_FS_2L_VLAN) != 0) {
				printk("%s: Received frame"
					" with VLAN Level 2 header, check"
					" switch configuration\n",
					pAC->dev->name);
			}
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_RX_PROGRESS,
				("skge: Error in received frame, dropped!\n"
				"Control: %x\nRxStat: %x\n",
				Control, FrameStat));
			DEV_KFREE_SKB_IRQ(pMsg);
		}
	} /* while */
	FillRxRing(pAC, pRxPort);
	/* do not start if called from Close */
	if (pAC->BoardLevel > 0) {
		ClearAndStartRx(pAC, PortIndex);
	}
	return;

rx_failed:
	/* remove error frame */
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ERROR,
		("Schrottdescriptor, length: 0x%x\n", FrameLength));

	/* release the DMA mapping */
	PhysAddr = ((SK_U64) pRxd->VDataHigh) << (SK_U64)32;
	PhysAddr |= (SK_U64) pRxd->VDataLow;
	pci_unmap_single(&pAC->PciDev,
			 PhysAddr,
			 pAC->RxBufSize - 2,
			 PCI_DMA_FROMDEVICE);
	DEV_KFREE_SKB_IRQ(pRxd->pMBuf);
	pRxd->pMBuf = NULL;
	pRxPort->RxdRingFree++;
	pRxPort->pRxdRingHead = pRxd->pNextRxd;
	goto rx_start;

} /* ReceiveIrq */


/*****************************************************************************
 *
 * 	ClearAndStartRx - give a start receive command to BMU, clear IRQ
 *
 * Description:
 *	This function sends a start command and a clear interrupt
 *	command for one receive queue to the BMU.
 *
 * Returns: N/A
 *	none
 */
static void ClearAndStartRx(
SK_AC	*pAC,		/* pointer to the adapter context */
int	PortIndex)	/* index of the receive port (XMAC) */
{
	SK_OUT8(pAC->IoBase, RxQueueAddr[PortIndex]+RX_Q_CTRL,
		RX_Q_CTRL_START | RX_Q_CTRL_CLR_I_EOF);
} /* ClearAndStartRx */


/*****************************************************************************
 *
 * 	ClearTxIrq - give a clear transmit IRQ command to BMU
 *
 * Description:
 *	This function sends a clear tx IRQ command for one
 *	transmit queue to the BMU.
 *
 * Returns: N/A
 */
static void ClearTxIrq(
SK_AC	*pAC,		/* pointer to the adapter context */
int	PortIndex,	/* index of the transmit port (XMAC) */
int	Prio)		/* priority or normal queue */
{
	SK_OUT8(pAC->IoBase, TxQueueAddr[PortIndex][Prio]+TX_Q_CTRL,
		TX_Q_CTRL_CLR_I_EOF);
} /* ClearTxIrq */


/*****************************************************************************
 *
 * 	ClearRxRing - remove all buffers from the receive ring
 *
 * Description:
 *	This function removes all receive buffers from the ring.
 *	The receive BMU must be stopped before calling this function.
 *
 * Returns: N/A
 */
static void ClearRxRing(
SK_AC	*pAC,		/* pointer to adapter context */
RX_PORT	*pRxPort)	/* pointer to rx port struct */
{
RXD		*pRxd;	/* pointer to the current descriptor */
unsigned int	Flags;
 SK_U64		PhysAddr;

	if (pRxPort->RxdRingFree == pAC->RxDescrPerRing) {
		return;
	}
	spin_lock_irqsave(&pRxPort->RxDesRingLock, Flags);
	pRxd = pRxPort->pRxdRingHead;
	do {
		if (pRxd->pMBuf != NULL) {
			PhysAddr = ((SK_U64) pRxd->VDataHigh) << (SK_U64)32;
			PhysAddr |= (SK_U64) pRxd->VDataLow;
			pci_unmap_single(&pAC->PciDev,
					 PhysAddr,
					 pAC->RxBufSize - 2,
					 PCI_DMA_FROMDEVICE);
			DEV_KFREE_SKB(pRxd->pMBuf);
			pRxd->pMBuf = NULL;
		}
		pRxd->RBControl &= RX_CTRL_OWN_BMU;
		pRxd = pRxd->pNextRxd;
		pRxPort->RxdRingFree++;
	} while (pRxd != pRxPort->pRxdRingTail);
	pRxPort->pRxdRingTail = pRxPort->pRxdRingHead;
	spin_unlock_irqrestore(&pRxPort->RxDesRingLock, Flags);
} /* ClearRxRing */


/*****************************************************************************
 *
 *	ClearTxRing - remove all buffers from the transmit ring
 *
 * Description:
 *	This function removes all transmit buffers from the ring.
 *	The transmit BMU must be stopped before calling this function
 *	and transmitting at the upper level must be disabled.
 *	The BMU own bit of all descriptors is cleared, the rest is
 *	done by calling FreeTxDescriptors.
 *
 * Returns: N/A
 */
static void ClearTxRing(
SK_AC	*pAC,		/* pointer to adapter context */
TX_PORT	*pTxPort)	/* pointer to tx prt struct */
{
TXD		*pTxd;		/* pointer to the current descriptor */
int		i;
unsigned int	Flags;

	spin_lock_irqsave(&pTxPort->TxDesRingLock, Flags);
	pTxd = pTxPort->pTxdRingHead;
	for (i=0; i<pAC->TxDescrPerRing; i++) {
		pTxd->TBControl &= ~TX_CTRL_OWN_BMU;
		pTxd = pTxd->pNextTxd;
	}
	FreeTxDescriptors(pAC, pTxPort);
	spin_unlock_irqrestore(&pTxPort->TxDesRingLock, Flags);
} /* ClearTxRing */


/*****************************************************************************
 *
 * 	SetQueueSizes - configure the sizes of rx and tx queues
 *
 * Description:
 *	This function assigns the sizes for active and passive port
 *	to the appropriate HWinit structure variables.
 *	The passive port(s) get standard values, all remaining RAM
 *	is given to the active port.
 *	The queue sizes are in kbyte and must be multiple of 8.
 *	The limits for the number of buffers filled into the rx rings
 *	is also set in this routine.
 *
 * Returns:
 *	none
 */
static void SetQueueSizes(
SK_AC	*pAC)	/* pointer to the adapter context */
{
int	StandbyRam;	/* adapter RAM used for a standby port */
int	RemainingRam;	/* adapter RAM available for the active port */
int	RxRam;		/* RAM used for the active port receive queue */
int	i;		/* loop counter */

	StandbyRam = SK_RLMT_STANDBY_QRXSIZE + SK_RLMT_STANDBY_QXASIZE +
		SK_RLMT_STANDBY_QXSSIZE;
	RemainingRam = pAC->GIni.GIRamSize - 
		(pAC->GIni.GIMacsFound-1) * StandbyRam;
	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		pAC->GIni.GP[i].PRxQSize = SK_RLMT_STANDBY_QRXSIZE;
		pAC->GIni.GP[i].PXSQSize = SK_RLMT_STANDBY_QXSSIZE;
		pAC->GIni.GP[i].PXAQSize = SK_RLMT_STANDBY_QXASIZE;
	}
	RxRam = (RemainingRam * 8 / 10) & ~7;
	pAC->GIni.GP[pAC->ActivePort].PRxQSize = RxRam;
	pAC->GIni.GP[pAC->ActivePort].PXSQSize = 0;
	pAC->GIni.GP[pAC->ActivePort].PXAQSize =
		(RemainingRam - RxRam) & ~7;
	pAC->RxQueueSize = RxRam;
	pAC->TxSQueueSize = 0;
	pAC->TxAQueueSize = (RemainingRam - RxRam) & ~7;
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("queue sizes settings - rx:%d  txA:%d txS:%d\n",
		pAC->RxQueueSize,pAC->TxAQueueSize, pAC->TxSQueueSize));
	
	for (i=0; i<SK_MAX_MACS; i++) {
		pAC->RxPort[i].RxFillLimit = pAC->RxDescrPerRing;
	}
	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		pAC->RxPort[i].RxFillLimit = pAC->RxDescrPerRing - 100;
	}
	/*
	 * Do not set the Limit to 0, because this could cause
	 * wrap around with ReQueue'ed buffers (a buffer could
	 * be requeued in the same position, made accessable to
	 * the hardware, and the hardware could change its
	 * contents!
	 */
	pAC->RxPort[pAC->ActivePort].RxFillLimit = 1;

#ifdef DEBUG
	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
			("i: %d,  RxQSize: %d,  PXSQsize: %d, PXAQSize: %d\n",
			i,
			pAC->GIni.GP[i].PRxQSize,
			pAC->GIni.GP[i].PXSQSize,
			pAC->GIni.GP[i].PXAQSize));
	}
#endif
} /* SetQueueSizes */


/*****************************************************************************
 *
 * 	SkGeSetMacAddr - Set the hardware MAC address
 *
 * Description:
 *	This function sets the MAC address used by the adapter.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 */
static int SkGeSetMacAddr(struct net_device *dev, void *p)
{
SK_AC		*pAC = (SK_AC*) dev->priv;
struct sockaddr	*addr = p;
unsigned int	Flags;
	
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeSetMacAddr starts now...\n"));
	if(netif_running(dev)) {
		return -EBUSY;
	}
	memcpy(dev->dev_addr, addr->sa_data,dev->addr_len);
	
	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	SkAddrOverride(pAC, pAC->IoBase, pAC->ActivePort,
		(SK_MAC_ADDR*)dev->dev_addr, SK_ADDR_VIRTUAL_ADDRESS);
	
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	return 0;
} /* SkGeSetMacAddr */


/*****************************************************************************
 *
 * 	SkGeSetRxMode - set receive mode
 *
 * Description:
 *	This function sets the receive mode of an adapter. The adapter
 *	supports promiscuous mode, allmulticast mode and a number of
 *	multicast addresses. If more multicast addresses the available
 *	are selected, a hash function in the hardware is used.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 */
static void SkGeSetRxMode(struct net_device *dev)
{
SK_AC			*pAC;
struct dev_mc_list	*pMcList;
int			i;
unsigned int		Flags;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeSetRxMode starts now... "));
	pAC = (SK_AC*) dev->priv;
	
	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	if (dev->flags & IFF_PROMISC) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
			("PROMISCUOUS mode\n"));
		SkAddrPromiscuousChange(pAC, pAC->IoBase, pAC->ActivePort,
			SK_PROM_MODE_LLC);
	} else if (dev->flags & IFF_ALLMULTI) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
			("ALLMULTI mode\n"));
		SkAddrPromiscuousChange(pAC, pAC->IoBase, pAC->ActivePort,
			SK_PROM_MODE_ALL_MC);
	} else {
		SkAddrPromiscuousChange(pAC, pAC->IoBase, pAC->ActivePort,
			SK_PROM_MODE_NONE);
		SkAddrMcClear(pAC, pAC->IoBase, pAC->ActivePort, 0);

		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
			("Number of MC entries: %d ", dev->mc_count));
		
		pMcList = dev->mc_list;
		for (i=0; i<dev->mc_count; i++, pMcList = pMcList->next) {
			SkAddrMcAdd(pAC, pAC->IoBase, pAC->ActivePort,
				(SK_MAC_ADDR*)pMcList->dmi_addr, 0);
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MCA,
				("%02x:%02x:%02x:%02x:%02x:%02x\n",
				pMcList->dmi_addr[0],
				pMcList->dmi_addr[1],
				pMcList->dmi_addr[2],
				pMcList->dmi_addr[3],
				pMcList->dmi_addr[4],
				pMcList->dmi_addr[5]));
		}
		SkAddrMcUpdate(pAC, pAC->IoBase, pAC->ActivePort);					
	
	}
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	
	return;
} /* SkGeSetRxMode */


/*****************************************************************************
 *
 * 	SkGeChangeMtu - set the MTU to another value
 *
 * Description:
 *	This function sets is called whenever the MTU size is changed
 *	(ifconfig mtu xxx dev ethX). If the MTU is bigger than standard
 *	ethernet MTU size, long frame support is activated.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 */
static int SkGeChangeMtu(struct net_device *dev, int NewMtu)
{
SK_AC		*pAC;
unsigned int	Flags;
int		i;
SK_EVPARA 	EvPara;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeChangeMtu starts now...\n"));
 
	pAC = (SK_AC*) dev->priv;
	if ((NewMtu < 68) || (NewMtu > SK_JUMBO_MTU)) {
		return -EINVAL;
	}

	pAC->RxBufSize = NewMtu + 32;
	dev->mtu = NewMtu;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("New MTU: %d\n", NewMtu));

	/* prevent reconfiguration while changing the MTU */

	/* disable interrupts */
	SK_OUT32(pAC->IoBase, B0_IMSK, 0);
	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_STOP, EvPara);
	SkEventDispatcher(pAC, pAC->IoBase);

	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		spin_lock(&pAC->TxPort[i][TX_PRIO_LOW].TxDesRingLock);
	}
	netif_stop_queue(pAC->dev);

	/* 
	 * adjust number of rx buffers allocated
	 */
	if (NewMtu > 1500) {
		/* use less rx buffers */
		for (i=0; i<pAC->GIni.GIMacsFound; i++) {
			if (i == pAC->ActivePort)
				pAC->RxPort[i].RxFillLimit =
					pAC->RxDescrPerRing - 100;
			else
				pAC->RxPort[i].RxFillLimit =
					pAC->RxDescrPerRing - 10;

		}
	}
	else {
		/* use normal anoumt of rx buffers */
		for (i=0; i<pAC->GIni.GIMacsFound; i++) {
			if (i == pAC->ActivePort)
				pAC->RxPort[i].RxFillLimit = 1;
			else
				pAC->RxPort[i].RxFillLimit =
					pAC->RxDescrPerRing - 100;
		}
	}
	 
	SkGeDeInit(pAC, pAC->IoBase); 

	/* 
	 * enable/disable hardware support for long frames
	 */
	if (NewMtu > 1500) {
		pAC->GIni.GIPortUsage = SK_JUMBO_LINK;
		for (i=0; i<pAC->GIni.GIMacsFound; i++) {
			pAC->GIni.GP[i].PRxCmd = 
				XM_RX_STRIP_FCS | XM_RX_LENERR_OK;
		}
	}
	else {
		pAC->GIni.GIPortUsage = SK_RED_LINK;
		for (i=0; i<pAC->GIni.GIMacsFound; i++) {
			pAC->GIni.GP[i].PRxCmd =
				XM_RX_STRIP_FCS | XM_RX_LENERR_OK;
		}
	}

	SkGeInit(   pAC, pAC->IoBase, 1);
	SkI2cInit(  pAC, pAC->IoBase, 1);
	SkEventInit(pAC, pAC->IoBase, 1);
	SkPnmiInit( pAC, pAC->IoBase, 1);
	SkAddrInit( pAC, pAC->IoBase, 1);
	SkRlmtInit( pAC, pAC->IoBase, 1);
	SkTimerInit(pAC, pAC->IoBase, 1);
	
	SkGeInit(   pAC, pAC->IoBase, 2);
	SkI2cInit(  pAC, pAC->IoBase, 2);
	SkEventInit(pAC, pAC->IoBase, 2);
	SkPnmiInit( pAC, pAC->IoBase, 2);
	SkAddrInit( pAC, pAC->IoBase, 2);
	SkRlmtInit( pAC, pAC->IoBase, 2);
	SkTimerInit(pAC, pAC->IoBase, 2);

	/* 
	 * clear and reinit the rx rings here
	 */
	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		ReceiveIrq(pAC, &pAC->RxPort[i]);
		ClearRxRing(pAC, &pAC->RxPort[i]);
		FillRxRing(pAC, &pAC->RxPort[i]);

		// Enable transmit descriptor polling.
		SkGePollTxD(pAC, pAC->IoBase, i, SK_TRUE);
		FillRxRing(pAC, &pAC->RxPort[i]);
	};

	SkGeYellowLED(pAC, pAC->IoBase, 1);

#ifdef USE_INT_MOD
	{
		unsigned long ModBase;
		ModBase = 53125000 / INTS_PER_SEC;
		SK_OUT32(pAC->IoBase, B2_IRQM_INI, ModBase);
		SK_OUT32(pAC->IoBase, B2_IRQM_MSK, IRQ_MOD_MASK);
		SK_OUT32(pAC->IoBase, B2_IRQM_CTRL, TIM_START);
	}
#endif

	netif_start_queue(pAC->dev);
	for (i=pAC->GIni.GIMacsFound-1; i>=0; i--) {
		spin_unlock(&pAC->TxPort[i][TX_PRIO_LOW].TxDesRingLock);
	}

	/* enable Interrupts */
	SK_OUT32(pAC->IoBase, B0_IMSK, IRQ_MASK);
	SK_OUT32(pAC->IoBase, B0_HWE_IMSK, IRQ_HWE_MASK);

	SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_START, EvPara);
	SkEventDispatcher(pAC, pAC->IoBase);


	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	
	return 0;
} /* SkGeChangeMtu */


/*****************************************************************************
 *
 * 	SkGeStats - return ethernet device statistics
 *
 * Description:
 *	This function return statistic data about the ethernet device
 *	to the operating system.
 *
 * Returns:
 *	pointer to the statistic structure.
 */
static struct net_device_stats *SkGeStats(struct net_device *dev)
{
SK_AC	*pAC = (SK_AC*) dev->priv;
SK_PNMI_STRUCT_DATA *pPnmiStruct;       /* structure for all Pnmi-Data */
SK_PNMI_STAT    *pPnmiStat;             /* pointer to virtual XMAC stat. data */SK_PNMI_CONF    *pPnmiConf;             /* pointer to virtual link config. */
unsigned int    Size;                   /* size of pnmi struct */
unsigned int	Flags;			/* for spin lock */

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeStats starts now...\n"));
	pPnmiStruct = &pAC->PnmiStruct;
        memset(pPnmiStruct, 0, sizeof(SK_PNMI_STRUCT_DATA));
        spin_lock_irqsave(&pAC->SlowPathLock, Flags);
        Size = SK_PNMI_STRUCT_SIZE;
        SkPnmiGetStruct(pAC, pAC->IoBase, pPnmiStruct, &Size);
        spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
        pPnmiStat = &pPnmiStruct->Stat[0];
        pPnmiConf = &pPnmiStruct->Conf[0];

	pAC->stats.rx_packets = (SK_U32) pPnmiStruct->RxDeliveredCts & 0xFFFFFFFF;
	pAC->stats.tx_packets = (SK_U32) pPnmiStat->StatTxOkCts & 0xFFFFFFFF;
	pAC->stats.rx_bytes = (SK_U32) pPnmiStruct->RxOctetsDeliveredCts;
	pAC->stats.tx_bytes = (SK_U32) pPnmiStat->StatTxOctetsOkCts;
	pAC->stats.rx_errors = (SK_U32) pPnmiStruct->InErrorsCts & 0xFFFFFFFF;
	pAC->stats.tx_errors = (SK_U32) pPnmiStat->StatTxSingleCollisionCts & 0xFFFFFFFF;
	pAC->stats.rx_dropped = (SK_U32) pPnmiStruct->RxNoBufCts & 0xFFFFFFFF;
	pAC->stats.tx_dropped = (SK_U32) pPnmiStruct->TxNoBufCts & 0xFFFFFFFF;
	pAC->stats.multicast = (SK_U32) pPnmiStat->StatRxMulticastOkCts & 0xFFFFFFFF;
	pAC->stats.collisions = (SK_U32) pPnmiStat->StatTxSingleCollisionCts & 0xFFFFFFFF;

	/* detailed rx_errors: */
	pAC->stats.rx_length_errors = (SK_U32) pPnmiStat->StatRxRuntCts & 0xFFFFFFFF;
	pAC->stats.rx_over_errors = (SK_U32) pPnmiStat->StatRxFifoOverflowCts & 0xFFFFFFFF;
	pAC->stats.rx_crc_errors = (SK_U32) pPnmiStat->StatRxFcsCts & 0xFFFFFFFF;
	pAC->stats.rx_frame_errors = (SK_U32) pPnmiStat->StatRxFramingCts & 0xFFFFFFFF;
	pAC->stats.rx_fifo_errors = (SK_U32) pPnmiStat->StatRxFifoOverflowCts & 0xFFFFFFFF;
	pAC->stats.rx_missed_errors = (SK_U32) pPnmiStat->StatRxMissedCts & 0xFFFFFFFF;

	/* detailed tx_errors */
	pAC->stats.tx_aborted_errors = (SK_U32) 0;
	pAC->stats.tx_carrier_errors = (SK_U32) pPnmiStat->StatTxCarrierCts & 0xFFFFFFFF;
	pAC->stats.tx_fifo_errors = (SK_U32) pPnmiStat->StatTxFifoUnderrunCts & 0xFFFFFFFF;
	pAC->stats.tx_heartbeat_errors = (SK_U32) pPnmiStat->StatTxCarrierCts & 0xFFFFFFFF;
	pAC->stats.tx_window_errors = (SK_U32) 0;

	return(&pAC->stats);
} /* SkGeStats */


/*****************************************************************************
 *
 * 	SkGeIoctl - IO-control function
 *
 * Description:
 *	This function is called if an ioctl is issued on the device.
 *	There are three subfunction for reading, writing and test-writing
 *	the private MIB data structure (usefull for SysKonnect-internal tools).
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 */
static int SkGeIoctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
SK_AC		*pAC;
SK_GE_IOCTL	Ioctl;
unsigned int	Err = 0;
int		Size;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeIoctl starts now...\n"));
	pAC = (SK_AC*) dev->priv;
	
	if(copy_from_user(&Ioctl, rq->ifr_data, sizeof(SK_GE_IOCTL))) {
		return -EFAULT;
	}

	switch(cmd) {
	case SK_IOCTL_SETMIB:
	case SK_IOCTL_PRESETMIB:
		if (!capable(CAP_NET_ADMIN)) return -EPERM;
 	case SK_IOCTL_GETMIB:
		if(copy_from_user(&pAC->PnmiStruct, Ioctl.pData, 
			Ioctl.Len<sizeof(pAC->PnmiStruct)?
			Ioctl.Len : sizeof(pAC->PnmiStruct))) {
			return -EFAULT;
		}
		Size = SkGeIocMib(pAC, Ioctl.Len, cmd);
		if(copy_to_user(Ioctl.pData, &pAC->PnmiStruct,
			Ioctl.Len<Size? Ioctl.Len : Size)) {
			return -EFAULT;
		}
		Ioctl.Len = Size;
		if(copy_to_user(rq->ifr_data, &Ioctl, sizeof(SK_GE_IOCTL))) {
			return -EFAULT;
		}
		break;
	default:
		Err = -EOPNOTSUPP;
	}
	return(Err);
} /* SkGeIoctl */


/*****************************************************************************
 *
 * 	SkGeIocMib - handle a GetMib, SetMib- or PresetMib-ioctl message
 *
 * Description:
 *	This function reads/writes the MIB data using PNMI (Private Network
 *	Management Interface).
 *	The destination for the data must be provided with the
 *	ioctl call and is given to the driver in the form of
 *	a user space address.
 *	Copying from the user-provided data area into kernel messages
 *	and back is done by copy_from_user and copy_to_user calls in
 *	SkGeIoctl.
 *
 * Returns:
 *	returned size from PNMI call
 */
static int SkGeIocMib(
SK_AC		*pAC,	/* pointer to the adapter context */
unsigned int	Size,	/* length of ioctl data */
int		mode)	/* flag for set/preset */
{
unsigned int	Flags;	/* for spin lock */
	
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeIocMib starts now...\n"));
	/* access MIB */
	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	switch(mode) {
	case SK_IOCTL_GETMIB:
		SkPnmiGetStruct(pAC, pAC->IoBase, &pAC->PnmiStruct, &Size);
		break;
	case SK_IOCTL_PRESETMIB:
		SkPnmiPreSetStruct(pAC, pAC->IoBase, &pAC->PnmiStruct, &Size);
		break;
	case SK_IOCTL_SETMIB:
		SkPnmiSetStruct(pAC, pAC->IoBase, &pAC->PnmiStruct, &Size);
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("MIB data access succeeded\n"));
	return (Size);
} /* SkGeIocMib */


/*****************************************************************************
 *
 * 	GetConfiguration - read configuration information
 *
 * Description:
 *	This function reads per-adapter configuration information from
 *	the options provided on the command line.
 *
 * Returns:
 *	none
 */
static void GetConfiguration(
SK_AC	*pAC)	/* pointer to the adapter context structure */
{
SK_I32	Port;		/* preferred port */
int	AutoNeg;	/* auto negotiation off (0) or on (1) */
int	DuplexCap;	/* duplex capabilities (0=both, 1=full, 2=half */
int	MSMode;		/* master / slave mode selection */
SK_BOOL	AutoSet;
SK_BOOL DupSet;
/*
 *	The two parameters AutoNeg. and DuplexCap. map to one configuration
 *	parameter. The mapping is described by this table:
 *	DuplexCap ->	|	both	|	full	|	half	|
 *	AutoNeg		|		|		|		|
 *	-----------------------------------------------------------------
 *	Off		|    illegal	|	Full	|	Half	|
 *	-----------------------------------------------------------------
 *	On		|   AutoBoth	|   AutoFull	|   AutoHalf	|
 *	-----------------------------------------------------------------
 *	Sense		|   AutoSense	|   AutoSense	|   AutoSense	|
 */
int	Capabilities[3][3] = 
		{ {		  -1, SK_LMODE_FULL,     SK_LMODE_HALF}, 
		  {SK_LMODE_AUTOBOTH, SK_LMODE_AUTOFULL, SK_LMODE_AUTOHALF},
		  {SK_LMODE_AUTOSENSE, SK_LMODE_AUTOSENSE, SK_LMODE_AUTOSENSE} };
#define DC_BOTH	0
#define DC_FULL 1
#define DC_HALF 2
#define AN_OFF	0
#define AN_ON	1
#define AN_SENS	2

	/* settings for port A */
	AutoNeg = AN_SENS; /* default: do auto Sense */
	AutoSet = SK_FALSE;
	if (AutoNeg_A != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		AutoNeg_A[pAC->Index] != NULL) {
		AutoSet = SK_TRUE;
		if (strcmp(AutoNeg_A[pAC->Index],"")==0) {
			AutoSet = SK_FALSE;
		}
		else if (strcmp(AutoNeg_A[pAC->Index],"On")==0) {
			AutoNeg = AN_ON;
		}
		else if (strcmp(AutoNeg_A[pAC->Index],"Off")==0) {
			AutoNeg = AN_OFF;
		}
		else if (strcmp(AutoNeg_A[pAC->Index],"Sense")==0) {
			AutoNeg = AN_SENS;
		}
		else printk("%s: Illegal value for AutoNeg_A\n",
			pAC->dev->name);
	}

	DuplexCap = DC_BOTH;
	DupSet = SK_FALSE;
	if (DupCap_A != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		DupCap_A[pAC->Index] != NULL) {
		DupSet = SK_TRUE;
		if (strcmp(DupCap_A[pAC->Index],"")==0) {
			DupSet = SK_FALSE;
		}
		else if (strcmp(DupCap_A[pAC->Index],"Both")==0) {
			DuplexCap = DC_BOTH;
		}
		else if (strcmp(DupCap_A[pAC->Index],"Full")==0) {
			DuplexCap = DC_FULL;
		}
		else if (strcmp(DupCap_A[pAC->Index],"Half")==0) {
			DuplexCap = DC_HALF;
		}
		else printk("%s: Illegal value for DupCap_A\n",
			pAC->dev->name);
	}
	
	/* check for illegal combinations */
	if (AutoSet && AutoNeg==AN_SENS && DupSet) {
		printk("%s, Port A: DuplexCapabilities"
			" ignored using Sense mode\n", pAC->dev->name);
	}
	if (AutoSet && AutoNeg==AN_OFF && DupSet && DuplexCap==DC_BOTH){
		printk("%s, Port A: Illegal combination"
			" of values AutoNeg. and DuplexCap.\n    Using "
			"Full Duplex\n", pAC->dev->name);

		DuplexCap = DC_FULL;
	}
	if (AutoSet && AutoNeg==AN_OFF && !DupSet) {
		DuplexCap = DC_FULL;
	}
	
	if (!AutoSet && DupSet) {
		printk("%s, Port A: Duplex setting not"
			" possible in\n    default AutoNegotiation mode"
			" (Sense).\n    Using AutoNegotiation On\n",
			pAC->dev->name);
		AutoNeg = AN_ON;
	}
	
	/* set the desired mode */
	pAC->GIni.GP[0].PLinkModeConf =
		Capabilities[AutoNeg][DuplexCap];
	
	pAC->GIni.GP[0].PFlowCtrlMode = SK_FLOW_MODE_SYM_OR_REM;
	if (FlowCtrl_A != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		FlowCtrl_A[pAC->Index] != NULL) {
		if (strcmp(FlowCtrl_A[pAC->Index],"") == 0) {
		}
		else if (strcmp(FlowCtrl_A[pAC->Index],"SymOrRem") == 0) {
			pAC->GIni.GP[0].PFlowCtrlMode =
				SK_FLOW_MODE_SYM_OR_REM;
		}
		else if (strcmp(FlowCtrl_A[pAC->Index],"Sym")==0) {
			pAC->GIni.GP[0].PFlowCtrlMode =
				SK_FLOW_MODE_SYMMETRIC;
		}
		else if (strcmp(FlowCtrl_A[pAC->Index],"LocSend")==0) {
			pAC->GIni.GP[0].PFlowCtrlMode =
				SK_FLOW_MODE_LOC_SEND;
		}
		else if (strcmp(FlowCtrl_A[pAC->Index],"None")==0) {
			pAC->GIni.GP[0].PFlowCtrlMode =
				SK_FLOW_MODE_NONE;
		}
		else printk("Illegal value for FlowCtrl_A\n");
	}
	if (AutoNeg==AN_OFF && pAC->GIni.GP[0].PFlowCtrlMode!=
		SK_FLOW_MODE_NONE) {
		printk("%s, Port A: FlowControl"
			" impossible without AutoNegotiation,"
			" disabled\n", pAC->dev->name);
		pAC->GIni.GP[0].PFlowCtrlMode = SK_FLOW_MODE_NONE;
	}

	MSMode = SK_MS_MODE_AUTO; /* default: do auto select */
	if (Role_A != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		Role_A[pAC->Index] != NULL) {
		if (strcmp(Role_A[pAC->Index],"")==0) {
		}
		else if (strcmp(Role_A[pAC->Index],"Auto")==0) {
			MSMode = SK_MS_MODE_AUTO;
		}
		else if (strcmp(Role_A[pAC->Index],"Master")==0) {
			MSMode = SK_MS_MODE_MASTER;
		}
		else if (strcmp(Role_A[pAC->Index],"Slave")==0) {
			MSMode = SK_MS_MODE_SLAVE;
		}
		else printk("%s: Illegal value for Role_A\n",
			pAC->dev->name);
	}
	pAC->GIni.GP[0].PMSMode = MSMode;
	
	
	/* settings for port B */
	AutoNeg = AN_SENS; /* default: do auto Sense */
	AutoSet = SK_FALSE;
	if (AutoNeg_B != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		AutoNeg_B[pAC->Index] != NULL) {
		AutoSet = SK_TRUE;
		if (strcmp(AutoNeg_B[pAC->Index],"")==0) {
			AutoSet = SK_FALSE;
		}
		else if (strcmp(AutoNeg_B[pAC->Index],"On")==0) {
			AutoNeg = AN_ON;
		}
		else if (strcmp(AutoNeg_B[pAC->Index],"Off")==0) {
			AutoNeg = AN_OFF;
		}
		else if (strcmp(AutoNeg_B[pAC->Index],"Sense")==0) {
			AutoNeg = AN_SENS;
		}
		else printk("Illegal value for AutoNeg_B\n");
	}

	DuplexCap = DC_BOTH;
	DupSet = SK_FALSE;
	if (DupCap_B != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		DupCap_B[pAC->Index] != NULL) {
		DupSet = SK_TRUE;
		if (strcmp(DupCap_B[pAC->Index],"")==0) {
			DupSet = SK_FALSE;
		}
		else if (strcmp(DupCap_B[pAC->Index],"Both")==0) {
			DuplexCap = DC_BOTH;
		}
		else if (strcmp(DupCap_B[pAC->Index],"Full")==0) {
			DuplexCap = DC_FULL;
		}
		else if (strcmp(DupCap_B[pAC->Index],"Half")==0) {
			DuplexCap = DC_HALF;
		}
		else printk("Illegal value for DupCap_B\n");
	}
	
	/* check for illegal combinations */
	if (AutoSet && AutoNeg==AN_SENS && DupSet) {
		printk("%s, Port B: DuplexCapabilities"
			" ignored using Sense mode\n", pAC->dev->name);
	}
	if (AutoSet && AutoNeg==AN_OFF && DupSet && DuplexCap==DC_BOTH){
		printk("%s, Port B: Illegal combination"
			" of values AutoNeg. and DuplexCap.\n    Using "
			"Full Duplex\n", pAC->dev->name);

		DuplexCap = DC_FULL;
	}
	if (AutoSet && AutoNeg==AN_OFF && !DupSet) {
		DuplexCap = DC_FULL;
	}
	
	if (!AutoSet && DupSet) {
		printk("%s, Port B: Duplex setting not"
			" possible in\n    default AutoNegotiation mode"
			" (Sense).\n    Using AutoNegotiation On\n",
			pAC->dev->name);
		AutoNeg = AN_ON;
	}
	
	/* set the desired mode */
	pAC->GIni.GP[1].PLinkModeConf =
		Capabilities[AutoNeg][DuplexCap];
	
	pAC->GIni.GP[1].PFlowCtrlMode = SK_FLOW_MODE_SYM_OR_REM;
	if (FlowCtrl_B != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		FlowCtrl_B[pAC->Index] != NULL) {
		if (strcmp(FlowCtrl_B[pAC->Index],"") == 0) {
		}
		else if (strcmp(FlowCtrl_B[pAC->Index],"SymOrRem") == 0) {
			pAC->GIni.GP[1].PFlowCtrlMode =
				SK_FLOW_MODE_SYM_OR_REM;
		}
		else if (strcmp(FlowCtrl_B[pAC->Index],"Sym")==0) {
			pAC->GIni.GP[1].PFlowCtrlMode =
				SK_FLOW_MODE_SYMMETRIC;
		}
		else if (strcmp(FlowCtrl_B[pAC->Index],"LocSend")==0) {
			pAC->GIni.GP[1].PFlowCtrlMode =
				SK_FLOW_MODE_LOC_SEND;
		}
		else if (strcmp(FlowCtrl_B[pAC->Index],"None")==0) {
			pAC->GIni.GP[1].PFlowCtrlMode =
				SK_FLOW_MODE_NONE;
		}
		else printk("Illegal value for FlowCtrl_B\n");
	}
	if (AutoNeg==AN_OFF && pAC->GIni.GP[1].PFlowCtrlMode!=
		SK_FLOW_MODE_NONE) {
		printk("%s, Port B: FlowControl"
			" impossible without AutoNegotiation,"
			" disabled\n", pAC->dev->name);
		pAC->GIni.GP[1].PFlowCtrlMode = SK_FLOW_MODE_NONE;
	}

	MSMode = SK_MS_MODE_AUTO; /* default: do auto select */
	if (Role_B != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		Role_B[pAC->Index] != NULL) {
		if (strcmp(Role_B[pAC->Index],"")==0) {
		}
		else if (strcmp(Role_B[pAC->Index],"Auto")==0) {
			MSMode = SK_MS_MODE_AUTO;
		}
		else if (strcmp(Role_B[pAC->Index],"Master")==0) {
			MSMode = SK_MS_MODE_MASTER;
		}
		else if (strcmp(Role_B[pAC->Index],"Slave")==0) {
			MSMode = SK_MS_MODE_SLAVE;
		}
		else printk("%s: Illegal value for Role_B\n",
			pAC->dev->name);
	}
	pAC->GIni.GP[1].PMSMode = MSMode;
	
	
	/* settings for both ports */
	pAC->ActivePort = 0;
	if (PrefPort != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		PrefPort[pAC->Index] != NULL) {
		if (strcmp(PrefPort[pAC->Index],"") == 0) { /* Auto */
			pAC->ActivePort = 0;
			pAC->Rlmt.MacPreferred = -1; /* auto */
			pAC->Rlmt.PrefPort = 0;
		}
		else if (strcmp(PrefPort[pAC->Index],"A") == 0) {
			/*
			 * do not set ActivePort here, thus a port
			 * switch is issued after net up.
			 */
			Port = 0;
			pAC->Rlmt.MacPreferred = Port;
			pAC->Rlmt.PrefPort = Port;
		}
		else if (strcmp(PrefPort[pAC->Index],"B") == 0) {
			/*
			 * do not set ActivePort here, thus a port
			 * switch is issued after net up.
			 */
			Port = 1;
			pAC->Rlmt.MacPreferred = Port;
			pAC->Rlmt.PrefPort = Port;
		}
		else printk("%s: Illegal value for PrefPort\n",
			pAC->dev->name);
	}
	
	if (RlmtMode != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		RlmtMode[pAC->Index] != NULL) {
		if (strcmp(RlmtMode[pAC->Index], "") == 0) {
			pAC->RlmtMode = 0;
		}
		else if (strcmp(RlmtMode[pAC->Index], "CheckLinkState") == 0) {
			pAC->RlmtMode = SK_RLMT_CHECK_LINK;
		}
		else if (strcmp(RlmtMode[pAC->Index], "CheckLocalPort") == 0) {
			pAC->RlmtMode = SK_RLMT_CHECK_LINK |
				SK_RLMT_CHECK_LOC_LINK;
		}
		else if (strcmp(RlmtMode[pAC->Index], "CheckSeg") == 0) {
			pAC->RlmtMode = SK_RLMT_CHECK_LINK |
				SK_RLMT_CHECK_LOC_LINK | 
				SK_RLMT_CHECK_SEG;
		}
		else {
			printk("%s: Illegal value for"
				" RlmtMode, using default\n", pAC->dev->name);
			pAC->RlmtMode = 0;
		}
	}
	else {
		pAC->RlmtMode = 0;
	}
} /* GetConfiguration */


/*****************************************************************************
 *
 * 	ProductStr - return a adapter identification string from vpd
 *
 * Description:
 *	This function reads the product name string from the vpd area
 *	and puts it the field pAC->DeviceString.
 *
 * Returns: N/A
 */
static void ProductStr(
SK_AC	*pAC		/* pointer to adapter context */
)
{
int	StrLen = 80;		/* length of the string, defined in SK_AC */
char	Keyword[] = VPD_NAME;	/* vpd productname identifier */
int	ReturnCode;		/* return code from vpd_read */
unsigned int Flags;

	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	ReturnCode = VpdRead(pAC, pAC->IoBase, Keyword, pAC->DeviceStr,
		&StrLen);
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	if (ReturnCode != 0) {
		/* there was an error reading the vpd data */
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ERROR,
			("Error reading VPD data: %d\n", ReturnCode));
		pAC->DeviceStr[0] = '\0';
	}
} /* ProductStr */




/****************************************************************************/
/* functions for common modules *********************************************/
/****************************************************************************/


/*****************************************************************************
 *
 *	SkDrvAllocRlmtMbuf - allocate an RLMT mbuf
 *
 * Description:
 *	This routine returns an RLMT mbuf or NULL. The RLMT Mbuf structure
 *	is embedded into a socket buff data area.
 *
 * Context:
 *	runtime
 *
 * Returns:
 *	NULL or pointer to Mbuf.
 */
SK_MBUF *SkDrvAllocRlmtMbuf(
SK_AC		*pAC,		/* pointer to adapter context */
SK_IOC		IoC,		/* the IO-context */
unsigned	BufferSize)	/* size of the requested buffer */
{
SK_MBUF		*pRlmtMbuf;	/* pointer to a new rlmt-mbuf structure */
struct sk_buff	*pMsgBlock;	/* pointer to a new message block */

	pMsgBlock = alloc_skb(BufferSize + sizeof(SK_MBUF), GFP_ATOMIC);
	if (pMsgBlock == NULL) {
		return (NULL);
	}
	pRlmtMbuf = (SK_MBUF*) pMsgBlock->data;
	skb_reserve(pMsgBlock, sizeof(SK_MBUF));
	pRlmtMbuf->pNext = NULL;
	pRlmtMbuf->pOs = pMsgBlock;
	pRlmtMbuf->pData = pMsgBlock->data;	/* Data buffer. */
	pRlmtMbuf->Size = BufferSize;		/* Data buffer size. */
	pRlmtMbuf->Length = 0;		/* Length of packet (<= Size). */
	return (pRlmtMbuf);

} /* SkDrvAllocRlmtMbuf */


/*****************************************************************************
 *
 *	SkDrvFreeRlmtMbuf - free an RLMT mbuf
 *
 * Description:
 *	This routine frees one or more RLMT mbuf(s).
 *
 * Context:
 *	runtime
 *
 * Returns:
 *	Nothing
 */
void  SkDrvFreeRlmtMbuf(
SK_AC		*pAC,		/* pointer to adapter context */  
SK_IOC		IoC,		/* the IO-context */              
SK_MBUF		*pMbuf)		/* size of the requested buffer */
{
SK_MBUF		*pFreeMbuf;
SK_MBUF		*pNextMbuf;

	pFreeMbuf = pMbuf;
	do {
		pNextMbuf = pFreeMbuf->pNext;
		DEV_KFREE_SKB_ANY(pFreeMbuf->pOs);
		pFreeMbuf = pNextMbuf;
	} while ( pFreeMbuf != NULL );
} /* SkDrvFreeRlmtMbuf */


/*****************************************************************************
 *
 *	SkOsGetTime - provide a time value
 *
 * Description:
 *	This routine provides a time value. The unit is 1/HZ (defined by Linux).
 *	It is not used for absolute time, but only for time differences.
 *
 *
 * Returns:
 *	Time value
 */
SK_U64 SkOsGetTime(SK_AC *pAC)
{
	return jiffies;
} /* SkOsGetTime */


/*****************************************************************************
 *
 *	SkPciReadCfgDWord - read a 32 bit value from pci config space
 *
 * Description:
 *	This routine reads a 32 bit value from the pci configuration
 *	space.
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
int SkPciReadCfgDWord(
SK_AC *pAC,		/* Adapter Control structure pointer */
int PciAddr,		/* PCI register address */
SK_U32 *pVal)		/* pointer to store the read value */
{
	pci_read_config_dword(&pAC->PciDev, PciAddr, pVal);
	return(0);
} /* SkPciReadCfgDWord */


/*****************************************************************************
 *
 *	SkPciReadCfgWord - read a 16 bit value from pci config space
 *
 * Description:
 *	This routine reads a 16 bit value from the pci configuration
 *	space.
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
int SkPciReadCfgWord(
SK_AC *pAC,	/* Adapter Control structure pointer */
int PciAddr,		/* PCI register address */
SK_U16 *pVal)		/* pointer to store the read value */
{
	pci_read_config_word(&pAC->PciDev, PciAddr, pVal);
	return(0);
} /* SkPciReadCfgWord */


/*****************************************************************************
 *
 *	SkPciReadCfgByte - read a 8 bit value from pci config space
 *
 * Description:
 *	This routine reads a 8 bit value from the pci configuration
 *	space.
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
int SkPciReadCfgByte(
SK_AC *pAC,	/* Adapter Control structure pointer */
int PciAddr,		/* PCI register address */
SK_U8 *pVal)		/* pointer to store the read value */
{
	pci_read_config_byte(&pAC->PciDev, PciAddr, pVal);
	return(0);
} /* SkPciReadCfgByte */


/*****************************************************************************
 *
 *	SkPciWriteCfgDWord - write a 32 bit value to pci config space
 *
 * Description:
 *	This routine writes a 32 bit value to the pci configuration
 *	space.
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
int SkPciWriteCfgDWord(
SK_AC *pAC,	/* Adapter Control structure pointer */
int PciAddr,		/* PCI register address */
SK_U32 Val)		/* pointer to store the read value */
{
	pci_write_config_dword(&pAC->PciDev, PciAddr, Val);
	return(0);
} /* SkPciWriteCfgDWord */


/*****************************************************************************
 *
 *	SkPciWriteCfgWord - write a 16 bit value to pci config space
 *
 * Description:
 *	This routine writes a 16 bit value to the pci configuration
 *	space. The flag PciConfigUp indicates whether the config space
 *	is accesible or must be set up first.
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
int SkPciWriteCfgWord(
SK_AC *pAC,	/* Adapter Control structure pointer */
int PciAddr,		/* PCI register address */
SK_U16 Val)		/* pointer to store the read value */
{
	pci_write_config_word(&pAC->PciDev, PciAddr, Val);
	return(0);
} /* SkPciWriteCfgWord */


/*****************************************************************************
 *
 *	SkPciWriteCfgWord - write a 8 bit value to pci config space
 *
 * Description:
 *	This routine writes a 8 bit value to the pci configuration
 *	space. The flag PciConfigUp indicates whether the config space
 *	is accesible or must be set up first.
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
int SkPciWriteCfgByte(
SK_AC *pAC,	/* Adapter Control structure pointer */
int PciAddr,		/* PCI register address */
SK_U8 Val)		/* pointer to store the read value */
{
	pci_write_config_byte(&pAC->PciDev, PciAddr, Val);
	return(0);
} /* SkPciWriteCfgByte */


/*****************************************************************************
 *
 *	SkDrvEvent - handle driver events
 *
 * Description:
 *	This function handles events from all modules directed to the driver
 *
 * Context:
 *	Is called under protection of slow path lock.
 *
 * Returns:
 *	0 if everything ok
 *	< 0  on error
 *	
 */
int SkDrvEvent(
SK_AC *pAC,		/* pointer to adapter context */
SK_IOC IoC,		/* io-context */
SK_U32 Event,		/* event-id */
SK_EVPARA Param)	/* event-parameter */
{
SK_MBUF		*pRlmtMbuf;	/* pointer to a rlmt-mbuf structure */
struct sk_buff	*pMsg;		/* pointer to a message block */
int		FromPort;	/* the port from which we switch away */
int		ToPort;		/* the port we switch to */
SK_EVPARA	NewPara;	/* parameter for further events */
int		Stat;
unsigned int	Flags;

	switch (Event) {
	case SK_DRV_ADAP_FAIL:
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("ADAPTER FAIL EVENT\n"));
		printk("%s: Adapter failed.\n", pAC->dev->name);
		/* disable interrupts */
		SK_OUT32(pAC->IoBase, B0_IMSK, 0);
		/* cgoos */
		break;
	case SK_DRV_PORT_FAIL:
		FromPort = Param.Para32[0];
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("PORT FAIL EVENT, Port: %d\n", FromPort));
		if (FromPort == 0) {
			printk("%s: Port A failed.\n", pAC->dev->name);
		} else {
			printk("%s: Port B failed.\n", pAC->dev->name);
		}
		/* cgoos */
		break;
	case SK_DRV_PORT_RESET:	 /* SK_U32 PortIdx */
		/* action list 4 */
		FromPort = Param.Para32[0];
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("PORT RESET EVENT, Port: %d ", FromPort));
		NewPara.Para64 = FromPort;
		SkPnmiEvent(pAC, IoC, SK_PNMI_EVT_XMAC_RESET, NewPara);
		spin_lock_irqsave(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);
		SkGeStopPort(pAC, IoC, FromPort, SK_STOP_ALL, SK_HARD_RST);
		spin_unlock_irqrestore(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);
		
		/* clear rx ring from received frames */
		ReceiveIrq(pAC, &pAC->RxPort[FromPort]);
		
		ClearTxRing(pAC, &pAC->TxPort[FromPort][TX_PRIO_LOW]);
		spin_lock_irqsave(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);
		SkGeInitPort(pAC, IoC, FromPort);
		SkAddrMcUpdate(pAC,IoC, FromPort);
		PortReInitBmu(pAC, FromPort);
		SkGePollTxD(pAC, IoC, FromPort, SK_TRUE);
		ClearAndStartRx(pAC, FromPort);
		spin_unlock_irqrestore(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);
		break;
	case SK_DRV_NET_UP:	 /* SK_U32 PortIdx */
		/* action list 5 */
		FromPort = Param.Para32[0];
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("NET UP EVENT, Port: %d ", Param.Para32[0]));
		printk("%s: network connection up using"
			" port %c\n", pAC->dev->name, 'A'+Param.Para32[0]);
		printk("    speed:           1000\n");
		Stat = pAC->GIni.GP[FromPort].PLinkModeStatus;
		if (Stat == SK_LMODE_STAT_AUTOHALF ||
			Stat == SK_LMODE_STAT_AUTOFULL) {
			printk("    autonegotiation: yes\n");
		}
		else {
			printk("    autonegotiation: no\n");
		}
		if (Stat == SK_LMODE_STAT_AUTOHALF ||
			Stat == SK_LMODE_STAT_HALF) {
			printk("    duplex mode:     half\n");
		}
		else {
			printk("    duplex mode:     full\n");
		}
		Stat = pAC->GIni.GP[FromPort].PFlowCtrlStatus;
		if (Stat == SK_FLOW_STAT_REM_SEND ) {
			printk("    flowctrl:        remote send\n");
		}
		else if (Stat == SK_FLOW_STAT_LOC_SEND ){
			printk("    flowctrl:        local send\n");
		}
		else if (Stat == SK_FLOW_STAT_SYMMETRIC ){
			printk("    flowctrl:        symmetric\n");
		}
		else {
			printk("    flowctrl:        none\n");
		}
		if (pAC->GIni.GP[FromPort].PhyType != SK_PHY_XMAC) {
		Stat = pAC->GIni.GP[FromPort].PMSStatus;
			if (Stat == SK_MS_STAT_MASTER ) {
				printk("    role:            master\n");
			}
			else if (Stat == SK_MS_STAT_SLAVE ) {
				printk("    role:            slave\n");
			}
			else {
				printk("    role:            ???\n");
			}
		}
		
		if (Param.Para32[0] != pAC->ActivePort) {
			NewPara.Para32[0] = pAC->ActivePort;
			NewPara.Para32[1] = Param.Para32[0];
			SkEventQueue(pAC, SKGE_DRV, SK_DRV_SWITCH_INTERN,
				NewPara);
		}
		break;
	case SK_DRV_NET_DOWN:	 /* SK_U32 Reason */
		/* action list 7 */
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("NET DOWN EVENT "));
		printk("%s: network connection down\n", pAC->dev->name);
		break;
	case SK_DRV_SWITCH_HARD: /* SK_U32 FromPortIdx SK_U32 ToPortIdx */
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("PORT SWITCH HARD "));
	case SK_DRV_SWITCH_SOFT: /* SK_U32 FromPortIdx SK_U32 ToPortIdx */
	/* action list 6 */
		printk("%s: switching to port %c\n", pAC->dev->name,
			'A'+Param.Para32[1]);
	case SK_DRV_SWITCH_INTERN: /* SK_U32 FromPortIdx SK_U32 ToPortIdx */
		FromPort = Param.Para32[0];
		ToPort = Param.Para32[1];
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("PORT SWITCH EVENT, From: %d  To: %d (Pref %d) ",
			FromPort, ToPort, pAC->Rlmt.PrefPort));
		NewPara.Para64 = FromPort;
		SkPnmiEvent(pAC, IoC, SK_PNMI_EVT_XMAC_RESET, NewPara);
		NewPara.Para64 = ToPort;
		SkPnmiEvent(pAC, IoC, SK_PNMI_EVT_XMAC_RESET, NewPara);
		spin_lock_irqsave(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);
		spin_lock_irqsave(
			&pAC->TxPort[ToPort][TX_PRIO_LOW].TxDesRingLock, Flags);
		SkGeStopPort(pAC, IoC, FromPort, SK_STOP_ALL, SK_SOFT_RST);
		SkGeStopPort(pAC, IoC, ToPort, SK_STOP_ALL, SK_SOFT_RST);
		spin_unlock_irqrestore(
			&pAC->TxPort[ToPort][TX_PRIO_LOW].TxDesRingLock, Flags);
		spin_unlock_irqrestore(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);

		ReceiveIrq(pAC, &pAC->RxPort[FromPort]); /* clears rx ring */
		ReceiveIrq(pAC, &pAC->RxPort[ToPort]); /* clears rx ring */
		
		ClearTxRing(pAC, &pAC->TxPort[FromPort][TX_PRIO_LOW]);
		ClearTxRing(pAC, &pAC->TxPort[ToPort][TX_PRIO_LOW]);
		spin_lock_irqsave(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock, 
			Flags);
		spin_lock_irqsave(
			&pAC->TxPort[ToPort][TX_PRIO_LOW].TxDesRingLock, Flags);
		pAC->ActivePort = ToPort;
		SetQueueSizes(pAC);
		SkGeInitPort(pAC, IoC, FromPort);
		SkGeInitPort(pAC, IoC, ToPort);
		if (Event == SK_DRV_SWITCH_SOFT) {
			SkXmRxTxEnable(pAC, IoC, FromPort);
		}
		SkXmRxTxEnable(pAC, IoC, ToPort);
		SkAddrSwap(pAC, IoC, FromPort, ToPort);
		SkAddrMcUpdate(pAC, IoC, FromPort);
		SkAddrMcUpdate(pAC, IoC, ToPort);
		PortReInitBmu(pAC, FromPort);
		PortReInitBmu(pAC, ToPort);
		SkGePollTxD(pAC, IoC, FromPort, SK_TRUE);
		SkGePollTxD(pAC, IoC, ToPort, SK_TRUE);
		ClearAndStartRx(pAC, FromPort);
		ClearAndStartRx(pAC, ToPort);
		spin_unlock_irqrestore(
			&pAC->TxPort[ToPort][TX_PRIO_LOW].TxDesRingLock, Flags);
		spin_unlock_irqrestore(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);
		break;
	case SK_DRV_RLMT_SEND:	 /* SK_MBUF *pMb */
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("RLS "));
		pRlmtMbuf = (SK_MBUF*) Param.pParaPtr;
		pMsg = (struct sk_buff*) pRlmtMbuf->pOs;
		skb_put(pMsg, pRlmtMbuf->Length);
		if (XmitFrame(pAC, &pAC->TxPort[pRlmtMbuf->PortIdx][TX_PRIO_LOW],
			      pMsg) < 0)
			DEV_KFREE_SKB_ANY(pMsg);
		break;
	default:
		break;
	}
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
		("END EVENT "));
	
	return (0);
} /* SkDrvEvent */


/*****************************************************************************
 *
 *	SkErrorLog - log errors
 *
 * Description:
 *	This function logs errors to the system buffer and to the console
 *
 * Returns:
 *	0 if everything ok
 *	< 0  on error
 *	
 */
void SkErrorLog(
SK_AC	*pAC,
int	ErrClass,
int	ErrNum,
char	*pErrorMsg)
{
char	ClassStr[80];

	switch (ErrClass) {
	case SK_ERRCL_OTHER:
		strcpy(ClassStr, "Other error");
		break;
	case SK_ERRCL_CONFIG:
		strcpy(ClassStr, "Configuration error");
		break;
	case SK_ERRCL_INIT:
		strcpy(ClassStr, "Initialization error");
		break;
	case SK_ERRCL_NORES:
		strcpy(ClassStr, "Out of resources error");
		break;
	case SK_ERRCL_SW:
		strcpy(ClassStr, "internal Software error");
		break;
	case SK_ERRCL_HW:
		strcpy(ClassStr, "Hardware failure");
		break;
	case SK_ERRCL_COMM:
		strcpy(ClassStr, "Communication error");
		break;
	}
	printk(KERN_INFO "%s: -- ERROR --\n        Class:  %s\n"
		"        Nr:  0x%x\n        Msg:  %s\n", pAC->dev->name,
		ClassStr, ErrNum, pErrorMsg);

} /* SkErrorLog */

#ifdef DEBUG /***************************************************************/
/* "debug only" section *****************************************************/
/****************************************************************************/


/*****************************************************************************
 *
 *	DumpMsg - print a frame
 *
 * Description:
 *	This function prints frames to the system logfile/to the console.
 *
 * Returns: N/A
 *	
 */
static void DumpMsg(struct sk_buff *skb, char *str)
{
	int	msglen;

	if (skb == NULL) {
		printk("DumpMsg(): NULL-Message\n");
		return;
	}

	if (skb->data == NULL) {
		printk("DumpMsg(): Message empty\n");
		return;
	}

	msglen = skb->len;
	if (msglen > 64)
		msglen = 64;

	printk("--- Begin of message from %s , len %d (from %d) ----\n", str, msglen, skb->len);

	DumpData((char *)skb->data, msglen);

	printk("------- End of message ---------\n");
} /* DumpMsg */



/*****************************************************************************
 *
 *	DumpData - print a data area
 *
 * Description:
 *	This function prints a area of data to the system logfile/to the 
 *	console.
 *
 * Returns: N/A
 *	
 */
static void DumpData(char *p, int size)
{
register int    i;
int	haddr, addr;
char	hex_buffer[180];
char	asc_buffer[180];
char	HEXCHAR[] = "0123456789ABCDEF";

	addr = 0;
	haddr = 0;
	hex_buffer[0] = 0;
	asc_buffer[0] = 0;
	for (i=0; i < size; ) {
		if (*p >= '0' && *p <='z')
			asc_buffer[addr] = *p;
		else
			asc_buffer[addr] = '.';
		addr++;
		asc_buffer[addr] = 0;
		hex_buffer[haddr] = HEXCHAR[(*p & 0xf0) >> 4];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[*p & 0x0f];
		haddr++;
		hex_buffer[haddr] = ' ';
		haddr++;
		hex_buffer[haddr] = 0;
		p++;
		i++;
		if (i%16 == 0) {
			printk("%s  %s\n", hex_buffer, asc_buffer);
			addr = 0;
			haddr = 0;
		}
	}
} /* DumpData */


/*****************************************************************************
 *
 *	DumpLong - print a data area as long values
 *
 * Description:
 *	This function prints a area of data to the system logfile/to the 
 *	console.
 *
 * Returns: N/A
 *	
 */
static void DumpLong(char *pc, int size)
{
register int    i;
int	haddr, addr;
char	hex_buffer[180];
char	asc_buffer[180];
char	HEXCHAR[] = "0123456789ABCDEF";
long	*p;
int	l;

	addr = 0;
	haddr = 0;
	hex_buffer[0] = 0;
	asc_buffer[0] = 0;
	p = (long*) pc;
	for (i=0; i < size; ) {
		l = (long) *p;
		hex_buffer[haddr] = HEXCHAR[(l >> 28) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[(l >> 24) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[(l >> 20) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[(l >> 16) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[(l >> 12) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[(l >> 8) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[(l >> 4) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[l & 0x0f];
		haddr++;
		hex_buffer[haddr] = ' ';
		haddr++;
		hex_buffer[haddr] = 0;
		p++;
		i++;
		if (i%8 == 0) {
			printk("%4x %s\n", (i-8)*4, hex_buffer);
			haddr = 0;
		}
	}
	printk("------------------------\n");
} /* DumpLong */

#endif /* DEBUG */

/*
 * Local variables:
 * compile-command: "make"
 * End:
 */

