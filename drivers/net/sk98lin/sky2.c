/******************************************************************************
 *
 * Name:	sky2.c
 * Project:	Yukon2 specific functions and implementations
 * Version:	$Revision: 1.25 $
 * Date:       	$Date: 2004/06/15 10:49:23 $
 * Purpose:	The main driver source module
 *
 *****************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect GmbH.
 *	(C)Copyright 2002-2003 Marvell.
 *
 *	Driver for Marvell Yukon/2 chipset and SysKonnect Gigabit Ethernet 
 *      Server Adapters.
 *
 *	Created 26-jan-2004
 *	Author: Ralph Roesler (rroesler@syskonnect.de)
 *	        Mirko Lindner (mlindner@syskonnect.de)
 *
 *	Address all question to: linux@syskonnect.de
 *
 *	The technical manual for the adapters is available from SysKonnect's
 *	web pages: www.syskonnect.com
 *	
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 *****************************************************************************/

#include	"h/skdrv1st.h"
#include	"h/skdrv2nd.h"

/******************************************************************************
 *
 * Defines
 *
 *****************************************************************************/

/* use the transmit hw checksum driver functionality */
#define USE_SK_TX_CHECKSUM

/* use the receive hw checksum driver functionality */
#define USE_SK_RX_CHECKSUM

/******************************************************************************
 *
 * Local Function Prototypes
 *
 *****************************************************************************/

static void InitPacketQueues(SK_AC *pAC,int Port);
static void GiveRxBufferToHw(SK_AC *pAC,SK_IOC IoC,int Port,SK_PACKET *pPacket);
static void FillReceiveTableYukon2(SK_AC *pAC,SK_IOC IoC,int Port);
static SK_BOOL HandleReceives(SK_AC *pAC,int Port,SK_U16 Len,SK_U32 FrameStatus,SK_U16 Tcp1,SK_U16 Tcp2,SK_U32 Tist,SK_U16 Vlan);
static void CheckForSendComplete(SK_AC *pAC,SK_IOC IoC,int Port,SK_PKT_QUEUE *pPQ,SK_LE_TABLE *pLETab,unsigned int Done);
static void UnmapAndFreeTxPktBuffer(SK_AC *pAC,SK_PACKET *pSkPacket,int TxPort);
static SK_BOOL AllocateAndInitLETables(SK_AC *pAC);
static SK_BOOL AllocatePacketBuffersYukon2(SK_AC *pAC);
static void FreeLETables(SK_AC *pAC);
static void FreePacketBuffers(SK_AC *pAC);
static SK_BOOL AllocAndMapRxBuffer(SK_AC *pAC,SK_PACKET *pSkPacket,int Port);
#ifdef CONFIG_SK98LIN_NAPI
static SK_BOOL HandleStatusLEs(SK_AC *pAC,int *WorkDone,int WorkToDo);
#else
static SK_BOOL HandleStatusLEs(SK_AC *pAC);
#endif

/******************************************************************************
 *
 * Local Variables
 *
 *****************************************************************************/

#define MAX_NBR_RX_BUFFERS_IN_HW	0x15
static SK_U8 NbrRxBuffersInHW;

#if defined(__i386__) || defined(__x86_64__)
// Investigate faster method, because using 'wbinvd' costs too much time...
// #define FLUSH_OPC(le)   asm volatile ("wbinvd":::"memory");
#define FLUSH_OPC(le)
#else
#define FLUSH_OPC(le) 
#endif

/******************************************************************************
 *
 * Extern Function Prototypes
 *
 *****************************************************************************/

/******************************************************************************
 *
 * Extern Variables
 *
 *****************************************************************************/

/******************************************************************************
 *
 * Global Functions
 *
 *****************************************************************************/

int SkY2Xmit( struct sk_buff *skb, struct SK_NET_DEVICE *dev); 

/*****************************************************************************
 *
 * 	SkY2RestartStatusUnit - restarts teh status unit
 *
 * Description:
 *	Reenables the status unit after any De-Init (e.g. when altering 
 *	the sie of the MTU via 'ifconfig a.b.c.d mtu xxx')
 *
 * Returns:	N/A
 */

void SkY2RestartStatusUnit(
SK_AC	*pAC)   /* pointer to adapter control context */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("==> SkY2RestartStatusUnit\n"));

	/*
	** It might be that the TX timer is not started. Therefore
	** it is initialized here -> to be more investigated!
	*/
	SK_OUT32(pAC->IoBase, STAT_TX_TIMER_INI, HW_MS_TO_TICKS(pAC,10));

	pAC->StatusLETable.Done  = 0;
	pAC->StatusLETable.Put   = 0;
	pAC->StatusLETable.HwPut = 0;
	SkGeY2InitStatBmu(pAC, pAC->IoBase, &pAC->StatusLETable);

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("<== SkY2RestartStatusUnit\n"));
}

/*****************************************************************************
 *
 * 	SkY2RlmtSend - sends out a single RLMT notification
 *
 * Description:
 *	This function sends out an RLMT frame
 *
 * Returns:	
 *	> 0 - on succes: the number of bytes in the message
 *	= 0 - on resource shortage: this frame sent or dropped, now
 *	      the ring is full ( -> set tbusy)
 *	< 0 - on failure: other problems ( -> return failure to upper layers)
 */

int SkY2RlmtSend (
SK_AC          *pAC,       /* pointer to adapter control context           */
int             PortNr,    /* index of port the packet(s) shall be send to */
struct sk_buff *pMessage)  /* pointer to send-message                      */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("=== SkY2RlmtSend\n"));
	return -1;   // temporarily do not send out RLMT frames
#if 0
	return(SkY2Xmit(pMessage, pAC->dev[PortNr])); SkY2Xmit needs device
#endif
}

/*****************************************************************************
 *
 * 	SkY2AllocateResources - Allocates all required resources for Yukon2
 *
 * Description:
 *	This function allocates all memory needed for the Yukon2. 
 *	It maps also RX buffers to the LETables and initializes the
 *	status list element table.
 *
 * Returns:	
 *	SK_TRUE, if all resources could be allocated and setup succeeded
 *	SK_FALSE, if an error 
 */
SK_BOOL SkY2AllocateResources (
SK_AC   *pAC)   /* pointer to adapter control context */
{
	int i;

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
		("==> SkY2AllocateResources\n"));

	/*
	** Initialize the packet queue variables first
	*/
	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		InitPacketQueues(pAC, i);
	}

	/* 
	** Get sufficient memory for the LETables
	*/
	if (!AllocateAndInitLETables(pAC)) {
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
			SK_DBGCAT_INIT | SK_DBGCAT_DRV_ERROR,
			("No memory for LETable.\n"));
		return(SK_FALSE);
	}

	/*
	** Allocate and intialize memory for both RX and TX 
	** packet and fragment buffers. On an error, free 
	** previously allocated LETable memory and quit.
	*/
	if (!AllocatePacketBuffersYukon2(pAC)) {
		FreeLETables(pAC);
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
			SK_DBGCAT_INIT | SK_DBGCAT_DRV_ERROR,
			("No memory for Packetbuffers.\n"));
		return(SK_FALSE);
	}

	/* 
	** Rx and Tx LE tables will be initialized in SkGeOpen() 
	**
	** It might be that the TX timer is not started. Therefore
	** it is initialized here -> to be more investigated!
	*/
	SK_OUT32(pAC->IoBase, STAT_TX_TIMER_INI, HW_MS_TO_TICKS(pAC,10));
	SkGeY2InitStatBmu(pAC, pAC->IoBase, &pAC->StatusLETable);

	pAC->MaxUnusedRxLeWorking = MAX_UNUSED_RX_LE_WORKING;

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
		("<== SkY2AllocateResources\n"));

	return (SK_TRUE);
}

/*****************************************************************************
 *
 * 	SkY2FreeResources - Frees previously allocated resources of Yukon2
 *
 * Description:
 *	This function frees all previously allocated memory of the Yukon2. 
 *
 * Returns: N/A
 */
void SkY2FreeResources (
SK_AC   *pAC)   /* pointer to adapter control context */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("==> SkY2FreeResources\n"));

	FreeLETables(pAC);
	FreePacketBuffers(pAC);

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("<== SkY2FreeResources\n"));
}

/*****************************************************************************
 *
 * 	SkY2AllocateRxBuffers - Allocates the receive buffers for a port
 *
 * Description:
 *	This function allocated all the RX buffers of the Yukon2. 
 *
 * Returns: N/A
 */
void SkY2AllocateRxBuffers (
SK_AC    *pAC,   /* pointer to adapter control context */
SK_IOC    IoC,	 /* I/O control context                */
int       Port)	 /* port index of RX                   */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
		("==> SkY2AllocateRxBuffers (Port %c)\n", Port));

	FillReceiveTableYukon2(pAC, IoC, Port);

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
		("<== SkY2AllocateRxBuffers\n"));
}

/*****************************************************************************
 *
 * 	SkY2FreeRxBuffers - Free's all allocates RX buffers of
 *
 * Description:
 *	This function frees all RX buffers of the Yukon2 for a single port
 *
 * Returns: N/A
 */
void SkY2FreeRxBuffers (
SK_AC    *pAC,   /* pointer to adapter control context */
SK_IOC    IoC,	 /* I/O control context                */
int       Port)	 /* port index of RX                   */
{
	SK_PACKET	*pSkPacket;
	unsigned long	 Flags;		/* for macro spinlocks */

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("==> SkY2FreeRxBuffers (Port %c)\n", Port));

	if ((pAC->RxPort[Port].ReceivePacketTable   != NULL) && 
	    (pAC->RxPort[Port].ReceiveFragmentTable != NULL)) {
		POP_FIRST_PKT_FROM_QUEUE(&pAC->RxPort[Port].RxQ_working, pSkPacket);
		while (pSkPacket != NULL) {
			if ((pSkPacket->pFrag) != NULL) {
				pci_unmap_page(pAC->PciDev,
				(dma_addr_t) pSkPacket->pFrag->pPhys,
				pSkPacket->pFrag->FragLen - 2,
				PCI_DMA_FROMDEVICE);

				DEV_KFREE_SKB_ANY(pSkPacket->pMBuf);
				pSkPacket->pMBuf        = NULL;
				pSkPacket->pFrag->pPhys = (SK_U64) 0;
				pSkPacket->pFrag->pVirt = NULL;
			}
			PUSH_PKT_AS_LAST_IN_QUEUE(&pAC->RxPort[Port].RxQ_waiting, pSkPacket);
			POP_FIRST_PKT_FROM_QUEUE(&pAC->RxPort[Port].RxQ_working, pSkPacket);
		}
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("<== SkY2FreeRxBuffers\n"));
}

/*****************************************************************************
 *
 * 	SkY2FreeTxBuffers - Free's any currently maintained Tx buffer
 *
 * Description:
 *	This function frees the TX buffers of the Yukon2 for a single port
 *	which might be in use by a transmit action
 *
 * Returns: N/A
 */
void SkY2FreeTxBuffers (
SK_AC    *pAC,   /* pointer to adapter control context */
SK_IOC    IoC,	 /* I/O control context                */
int       Port)	 /* port index of TX                   */
{
	SK_PACKET	*pSkPacket;
	SK_FRAG		*pSkFrag;
	unsigned long	 Flags;

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("==> SkY2FreeTxBuffers (Port %c)\n", Port));
 
	if (pAC->TxPort[Port][0].TransmitPacketTable != NULL) {
		POP_FIRST_PKT_FROM_QUEUE(&pAC->TxPort[Port][0].TxAQ_working, pSkPacket);
		while (pSkPacket != NULL) {
			if ((pSkFrag = pSkPacket->pFrag) != NULL) {
				UnmapAndFreeTxPktBuffer(pAC, pSkPacket, Port);
			}
			PUSH_PKT_AS_LAST_IN_QUEUE(&pAC->TxPort[Port][0].TxAQ_waiting, pSkPacket);
			POP_FIRST_PKT_FROM_QUEUE(&pAC->TxPort[Port][0].TxAQ_working, pSkPacket);
		}
#if USE_SYNC_TX_QUEUE
		POP_FIRST_PKT_FROM_QUEUE(&pAC->TxPort[Port][0].TxSQ_working, pSkPacket);
		while (pSkPacket != NULL) {
			if ((pSkFrag = pSkPacket->pFrag) != NULL) {
				UnmapAndFreeTxPktBuffer(pAC, pSkPacket, Port);
			}
			PUSH_PKT_AS_LAST_IN_QUEUE(&pAC->TxPort[Port][0].TxSQ_waiting, pSkPacket);
			POP_FIRST_PKT_FROM_QUEUE(&pAC->TxPort[Port][0].TxSQ_working, pSkPacket);
		}
#endif
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("<== SkY2FreeTxBuffers\n"));
}

/*****************************************************************************
 *
 * 	SkY2Isr - handle a receive IRQ for all yukon2 cards
 *
 * Description:
 *	This function is called when a receive IRQ is set. (only for yukon2)
 *	HandleReceives does the deferred processing of all outstanding
 *	interrupt operations.
 *
 * Returns:	N/A
 */
SkIsrRetVar SkY2Isr (
int              irq,     /* the irq we have received (might be shared!) */
void            *dev_id,  /* current device id                           */
struct  pt_regs *ptregs)  /* not used by our driver                      */
{
	struct SK_NET_DEVICE	*dev	= (struct SK_NET_DEVICE *)dev_id;
	DEV_NET			*pNet	= (DEV_NET*) dev->priv;
	SK_AC			*pAC	= pNet->pAC;
	SK_U32			IntSrc;
	unsigned long		Flags;
#ifndef CONFIG_SK98LIN_NAPI
	SK_BOOL			handledStatLE = SK_FALSE;
#endif

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
		("==> SkY2Isr\n"));

	SK_IN32(pAC->IoBase, B0_Y2_SP_ISRC2, &IntSrc);

	if (IntSrc == 0) {
		SK_OUT32(pAC->IoBase, B0_Y2_SP_ICR, 2);
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
			("No Interrupt\n ==> SkY2Isr\n"));
		return SkIsrRetNone;

	}

#ifdef CONFIG_SK98LIN_NAPI
	if (netif_rx_schedule_prep(dev)) {
		pAC->GIni.GIValIrqMask &= ~(Y2_IS_STAT_BMU);
		SK_OUT32(pAC->IoBase, B0_IMSK, pAC->GIni.GIValIrqMask);
		__netif_rx_schedule(dev);
	}
#else
	handledStatLE = HandleStatusLEs(pAC);
#endif

	/* 
	** Check for Special Interrupts 
	*/
	if (IntSrc & ~Y2_IS_STAT_BMU) {
		spin_lock_irqsave(&pAC->SlowPathLock, Flags);
		SkGeSirqIsr(pAC, pAC->IoBase, IntSrc);
		SkEventDispatcher(pAC, pAC->IoBase);
		spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	}

	/* Speed enhancement for a2 chipsets */
	if (HW_FEATURE(pAC, HWF_WA_DEV_42)) {
		spin_lock_irqsave(&pAC->SetPutIndexLock, Flags);
		SkGeY2SetPutIndex(pAC, pAC->IoBase, Y2_PREF_Q_ADDR(Q_XA1,0), &pAC->TxPort[0][0].TxALET);
		SkGeY2SetPutIndex(pAC, pAC->IoBase, Y2_PREF_Q_ADDR(Q_R1,0), &pAC->RxPort[0].RxLET);
		spin_unlock_irqrestore(&pAC->SetPutIndexLock, Flags);
	} 

	/* 
	** Reenable interrupts and signal end of ISR 
	*/
	SK_OUT32(pAC->IoBase, B0_Y2_SP_ICR, 2);
			
	/*
	** Stop and restart TX timer in case a Status LE was handled
	*/
#ifndef CONFIG_SK98LIN_NAPI
	if ((HW_FEATURE(pAC, HWF_WA_DEV_43_418)) && (handledStatLE)) {
		SK_OUT8(pAC->IoBase, STAT_TX_TIMER_CTRL, TIM_STOP);
		SK_OUT8(pAC->IoBase, STAT_TX_TIMER_CTRL, TIM_START);
	}
#endif

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
		("<== SkY2Isr\n"));

	return SkIsrRetHandled;
}	/* SkY2Isr */

/*****************************************************************************
 *
 *	SkY2Xmit - Linux frame transmit function for Yukon2
 *
 * Description:
 *	The system calls this function to send frames onto the wire.
 *	It puts the frame in the tx descriptor ring. If the ring is
 *	full then, the 'tbusy' flag is set.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 *
 * WARNING: 
 *	returning 1 in 'tbusy' case caused system crashes (double
 *	allocated skb's) !!!
 */
int SkY2Xmit(
struct sk_buff *skb,        /* socket buffer to be sent */
struct SK_NET_DEVICE *dev)  /* via which device?        */
{
	DEV_NET		*pNet	= (DEV_NET*) dev->priv;
	SK_AC		*pAC	= pNet->pAC;

	SK_HWLE		*pLE;
	SK_PACKET	*pSkPacket;
	SK_FRAG		*pFrag;
	SK_FRAG		*PrevFrag;
	SK_FRAG		*CurrFrag;
	SK_PKT_QUEUE	*pWorkQueue;	/* corresponding TX queue	*/
	SK_PKT_QUEUE	*pFreeQueue; 
	SK_LE_TABLE	*pLETab;	/* corresponding LETable	*/ 
	skb_frag_t	*sk_frag;

	SK_U64		 PhysAddr;
	SK_BOOL		 SetOpcodePacketFlag;
	SK_U32		 PrefetchReg;		/* register for Put idx	*/
	SK_U32		 StartAddrPrefetchUnit;	/* begin prefetch unit	*/
	SK_U32		 HighAddress;
	SK_U32		 LowAddress;
	SK_U16		 TcpSumStart; 
	SK_U16		 TcpSumWrite;
	SK_U8		 OpCode;
	SK_U8		 Ctrl;

	unsigned long	 Flags;
	unsigned int	 Port;
	int		 CurrFragCtr;
	int		 Protocol;

#if 0 // YK2_TX_POLLING
	SK_EVPARA	 NewPara;
#endif

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
		("==> SkY2Xmit\n"));

	if (pAC->RlmtNets == 2) {
		Port = pNet->PortNr;
	} else {
		Port = pAC->ActivePort;
	}

	/*
	** Put any new packet to be sent in the waiting queue and 
	** handle also any possible fragment of that packet.
	*/
	pWorkQueue = &(pAC->TxPort[Port][TX_PRIO_LOW].TxAQ_working);
	pFreeQueue = &(pAC->TxPort[Port][TX_PRIO_LOW].TxQ_free);
	pLETab     = &(pAC->TxPort[Port][TX_PRIO_LOW].TxALET);

	if (Port == 0) {
		PrefetchReg = Y2_PREF_Q_ADDR(Q_XA1, PREF_UNIT_PUT_IDX_REG);
		StartAddrPrefetchUnit = Y2_PREF_Q_ADDR(Q_XA1, 0);
	} else {
		PrefetchReg = Y2_PREF_Q_ADDR(Q_XA2, PREF_UNIT_PUT_IDX_REG);
		StartAddrPrefetchUnit = Y2_PREF_Q_ADDR(Q_XA1, 0);
	}

	spin_lock_irqsave(&pAC->TxPort[Port][TX_PRIO_LOW].TxDesRingLock, Flags);
	PLAIN_POP_FIRST_PKT_FROM_QUEUE(pFreeQueue, pSkPacket);
	if(pSkPacket == NULL) {
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
			SK_DBGCAT_DRV_TX_PROGRESS | SK_DBGCAT_DRV_ERROR,
			("Could not obtain free packet used for xmit\n"));
		spin_unlock_irqrestore(&pAC->TxPort[Port][TX_PRIO_LOW].TxDesRingLock, Flags);
		netif_stop_queue(dev);

		return 1; /* zero bytes sent! */
	} else {
		if(pFreeQueue->pHead == NULL) {
			PLAIN_PUSH_PKT_AS_FIRST_IN_QUEUE(pFreeQueue, pSkPacket);
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_TX_PROGRESS | SK_DBGCAT_DRV_ERROR,
				("avoid using all packets for send xmit\n"));
			spin_unlock_irqrestore(&pAC->TxPort[Port][TX_PRIO_LOW].TxDesRingLock, Flags);
			netif_stop_queue(dev);
			return 1; /* zero bytes sent! */
		}
	} 

	/*
	** Normal send operations require only one fragment,
	** because only one sk_buff data area is passed. 
	** In contradiction to this, scatter-gather (zerocopy)
	** send operations might pass one or more additional 
	** fragments where each fragment needs a separate
	** fragment info packet.
	*/
	if (((skb_shinfo(skb)->nr_frags + 1) * MAX_FRAG_OVERHEAD) > 
					NUM_FREE_LE_IN_TABLE(pLETab)) {
		PLAIN_PUSH_PKT_AS_FIRST_IN_QUEUE(pFreeQueue, pSkPacket);
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
			SK_DBGCAT_DRV_TX_PROGRESS | SK_DBGCAT_DRV_ERROR,
			("Not enough LE available for send\n"));
		spin_unlock_irqrestore(&pAC->TxPort[Port][TX_PRIO_LOW].TxDesRingLock, Flags);
		return 1; /* zero bytes sent! */
	}
	
	if ((skb_shinfo(skb)->nr_frags + 1) > NUM_FREE_FRAGS(pAC, Port)) {
		PLAIN_PUSH_PKT_AS_FIRST_IN_QUEUE(pFreeQueue, pSkPacket);
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
			SK_DBGCAT_DRV_TX_PROGRESS | SK_DBGCAT_DRV_ERROR,
			("Not even one fragment available for send\n"));
		spin_unlock_irqrestore(&pAC->TxPort[Port][TX_PRIO_LOW].TxDesRingLock, Flags);
		return 1; /* zero bytes sent! */
	}

	PLAIN_POP_FIRST_FRAG_FROM_QUEUE(Port, pSkPacket->pFrag);
		
	/* 
	** map the sk_buff to be available for the adapter 
	*/
	PhysAddr = (SK_U64) pci_map_page(pAC->PciDev,
			virt_to_page(skb->data),
			((unsigned long) skb->data & ~PAGE_MASK),
			skb->len, 
			PCI_DMA_TODEVICE);
	pSkPacket->pMBuf	= skb;
	pSkPacket->pFrag->pPhys = PhysAddr;
	// pSkPacket->pFrag->pVirt = skb->data;
	pSkPacket->pFrag->FragLen = skb_headlen(skb);
	pSkPacket->NumFrags	= skb_shinfo(skb)->nr_frags + 1;

	// BytesSend = skb_headlen(skb);
	PrevFrag = pSkPacket->pFrag;

	/*
	** Each scatter-gather fragment need to be mapped...
	*/
        for (	CurrFragCtr = 0; 
		CurrFragCtr < skb_shinfo(skb)->nr_frags;
		CurrFragCtr++) {
		sk_frag = &skb_shinfo(skb)->frags[CurrFragCtr];
		PLAIN_POP_FIRST_FRAG_FROM_QUEUE(Port, CurrFrag);

                /* 
		** map the sk_buff to be available for the adapter 
		*/
		PhysAddr = (SK_U64) pci_map_page(pAC->PciDev,
				sk_frag->page,
		 		sk_frag->page_offset,
		 		sk_frag->size,
		 		PCI_DMA_TODEVICE);

		CurrFrag->pPhys   = PhysAddr;
 		// CurrFrag->pVirt   = (void *) sk_frag->page;
 		CurrFrag->FragLen = sk_frag->size;
 		// BytesSend         = BytesSend + sk_frag->size;

		/*
		** Add the new fragment to the list of fragments
		*/
		PrevFrag->pNext = CurrFrag;
		PrevFrag = CurrFrag;
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
		("\tWe have a packet to send %p\n", pSkPacket));

	/* 
	** the first frag of a packet gets opcode OP_PACKET 
	*/
	SetOpcodePacketFlag	= SK_TRUE;
	pFrag			= pSkPacket->pFrag;

	/* 
	** fill list elements with data from fragments 
	*/
	while (pFrag != NULL) {
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
			("\tGet LE\n"));

		GET_TX_LE(pLE, pLETab);
		Ctrl	= 0;

		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
			("\tGot empty LE %p idx %d\n", pLE, GET_PUT_IDX(pLETab)));

		SK_DBG_DUMP_TX_LE(pLE);

		LowAddress  = (SK_U32) (pFrag->pPhys & 0xffffffff);
		HighAddress = (SK_U32) (pFrag->pPhys >> 32);
		if (HighAddress != pLETab->BufHighAddr) {
			/* set opcode high part of the address in one LE */
			OpCode = OP_ADDR64 | HW_OWNER;

			/* Set now the 32 high bits of the address */
			TXLE_SET_ADDR( pLE, HighAddress);

			/* Set the opcode into the LE */
			TXLE_SET_OPC(pLE, OpCode);

			/* Flush the LE to memory */
			FLUSH_OPC(pLE);

			/* remember the HighAddress we gave to the Hardware */
			pLETab->BufHighAddr = HighAddress;
			
			/* get a new LE because we filled one with high address */
			GET_TX_LE(pLE, pLETab);
		}

		/*
		** TCP checksum offload
		*/

		if ((pSkPacket->pMBuf->ip_summed == CHECKSUM_HW) && 
		    (SetOpcodePacketFlag         == SK_TRUE)) {
			Protocol = ((SK_U8)pSkPacket->pMBuf->data[C_OFFSET_IPPROTO] & 0xff);
			// if (Protocol & C_PROTO_ID_IP) {
			//	Ctrl = 0;
			// }
			if (Protocol & C_PROTO_ID_TCP) {
				Ctrl = CALSUM | WR_SUM | INIT_SUM | LOCK_SUM;
				/* TCP Checksum Calculation Start Position */
				TcpSumStart = C_LEN_ETHERMAC_HEADER + IP_HDR_LEN;
				/* TCP Checksum Write Position */
				TcpSumWrite = TcpSumStart + TCP_CSUM_OFFS;
			} else {
				Ctrl = UDPTCP | CALSUM | WR_SUM | INIT_SUM | LOCK_SUM;
				/* TCP Checksum Calculation Start Position */
				TcpSumStart = ETHER_MAC_HDR_LEN + IP_HDR_LEN;
				/* UDP Checksum Write Position */
				TcpSumWrite = TcpSumStart + UDP_CSUM_OFFS;
			}

			if ((Ctrl) && (pLETab->Bmu.RxTx.TcpWp != TcpSumWrite)) {
				/* Update the last value of the write position */
				pLETab->Bmu.RxTx.TcpWp = TcpSumWrite;

				/* Set the Lock field for this LE: */
				/* Checksum calculation for one packet only */
				TXLE_SET_LCKCS(pLE, 1);

				/* Set the start position for checksum. */
				TXLE_SET_STACS(pLE, TcpSumStart);

				/* Set the position where the checksum will be writen */
				TXLE_SET_WRICS(pLE, TcpSumWrite);

				/* Set the initial value for checksum */
				/* PseudoHeader CS passed from Linux -> 0! */
				TXLE_SET_INICS(pLE, 0);

				/* Set the opcode for tcp checksum */
				TXLE_SET_OPC(pLE, OP_TCPLISW | HW_OWNER);

				/* Flush the LE to memory */
				FLUSH_OPC(pLE);

				/* get a new LE because we filled one with data for checksum */
				GET_TX_LE(pLE, pLETab);
			}
		} /* end TCP offload handling */

		TXLE_SET_ADDR(pLE, LowAddress);
		TXLE_SET_LEN(pLE, pFrag->FragLen);

		if (SetOpcodePacketFlag){
			// New frame starts always with opcode OP_PACKET
			OpCode = OP_PACKET| HW_OWNER;
			SetOpcodePacketFlag = SK_FALSE;
		} else {
			// Follow packet in a sequence has always OP_BUFFER
			OpCode = OP_BUFFER | HW_OWNER;
		}

		pFrag = pFrag->pNext;
		if (pFrag == NULL) {
			/* mark last fragment */
			Ctrl |= EOP;
		}
		TXLE_SET_CTRL(pLE, Ctrl);
		TXLE_SET_OPC(pLE, OpCode);
		FLUSH_OPC(pLE);
		SK_DBG_DUMP_TX_LE(pLE);
	}

	/* 
	** Remember next LE for tx complete 
	*/
	pSkPacket->NextLE = GET_PUT_IDX(pLETab);
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
		("\tNext LE for pkt %p is %d\n", pSkPacket, pSkPacket->NextLE));

	/* 
	** Add packet to working packets queue 
	*/
	PLAIN_PUSH_PKT_AS_LAST_IN_QUEUE(pWorkQueue, pSkPacket);

	/* 
	** give transmit start command
	*/
	if (HW_FEATURE(pAC, HWF_WA_DEV_42)) {
		spin_lock(&pAC->SetPutIndexLock);
		SkGeY2SetPutIndex(pAC, pAC->IoBase, Y2_PREF_Q_ADDR(Q_XA1,0), &pAC->TxPort[0][0].TxALET);
		spin_unlock(&pAC->SetPutIndexLock);
	} else {
		/* write put index */
		if (pNet->PortNr == 0) { 
			SK_OUT32(pAC->IoBase, Y2_PREF_Q_ADDR(Q_XA1, PREF_UNIT_PUT_IDX_REG), GET_PUT_IDX(&pAC->TxPort[0][0].TxALET)); 
			UPDATE_HWPUT_IDX(&pAC->TxPort[0][0].TxALET);
		} else {
			SK_OUT32(pAC->IoBase, Y2_PREF_Q_ADDR(Q_XA2, PREF_UNIT_PUT_IDX_REG), GET_PUT_IDX(&pAC->TxPort[1][0].TxALET)); 
			UPDATE_HWPUT_IDX(&pAC->TxPort[1][0].TxALET);
		}
	}

	dev->trans_start = jiffies;
	spin_unlock_irqrestore(&pAC->TxPort[Port][TX_PRIO_LOW].TxDesRingLock, Flags);
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
		("<== SkY2Xmit(return 0)\n"));
	return (0);
}	/* SkY2Xmit */

#ifdef CONFIG_SK98LIN_NAPI
/*****************************************************************************
 *
 *	SkY2Poll - NAPI Rx polling callback for Yukon2 chipsets
 *
 * Description:
 *	Called by the Linux system in case NAPI polling is activated
 *
 * Returns
 *	The number of work data still to be handled
 */
int SkY2Poll(
struct net_device *dev,
int               *budget)
{
	SK_AC	*pAC          = ((DEV_NET*)(dev->priv))->pAC;
	int	WorkToDo      = min(*budget, dev->quota);
	int	WorkDone      = 0;
	SK_BOOL handledStatLE = SK_FALSE;

	handledStatLE = HandleStatusLEs(pAC, &WorkDone, WorkToDo);

	*budget -= WorkDone;
	dev->quota -= WorkDone;

	if(WorkDone < WorkToDo) {
		netif_rx_complete(dev);
		pAC->GIni.GIValIrqMask |= (Y2_IS_STAT_BMU);
		SK_OUT32(pAC->IoBase, B0_IMSK, pAC->GIni.GIValIrqMask);
		if ((HW_FEATURE(pAC, HWF_WA_DEV_43_418)) && (handledStatLE)) {
			SK_OUT8(pAC->IoBase, STAT_TX_TIMER_CTRL, TIM_STOP);
			SK_OUT8(pAC->IoBase, STAT_TX_TIMER_CTRL, TIM_START);
		}
	}
	return (WorkDone >= WorkToDo);
}	/* SkY2Poll */
#endif

/******************************************************************************
 *
 *	SkY2PortStop - stop a port on Yukon2
 *
 * Description:
 *	This function stops a port of the Yukon2 chip. This stop 
 *	stop needs to be performed in a specific order:
 * 
 *	a) Stop the Prefetch unit
 *	b) Stop the Port (MAC, PHY etc.)
 *
 * Returns: N/A
 */
void SkY2PortStop(
SK_AC   *pAC,      /* adapter control context                             */
SK_IOC   IoC,      /* I/O control context (address of adapter registers)  */
int      Port,     /* port to stop (MAC_1 + n)                            */
int      Dir,      /* StopDirection (SK_STOP_RX, SK_STOP_TX, SK_STOP_ALL) */
int      RstMode)  /* Reset Mode (SK_SOFT_RST, SK_HARD_RST)               */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("==> SkY2PortStop (Port %c)\n", 'A' + Port));

	/*
	** Stop the HW
	*/
	SkGeStopPort(pAC, IoC, Port, Dir, RstMode);

	/*
	** Move any TX packet from work queues into the free queue again
	*/
	SkY2FreeTxBuffers(pAC, pAC->IoBase, Port);

	/*
	** Move any RX packet from work queue into the waiting queue
	*/
	SkY2FreeRxBuffers(pAC, pAC->IoBase, Port);

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("<== SkY2PortStop()\n"));
}

/******************************************************************************
 *
 *	SkY2PortStart - start a port on Yukon2
 *
 * Description:
 *	This function starts a port of the Yukon2 chip. This start 
 *	action needs to be performed in a specific order:
 * 
 *	a) Initialize the LET indices (PUT/GET to 0)
 *	b) Initialize the LET in HW (enables also prefetch unit)
 *	c) Move all RX buffers from waiting queue to working queue
 *	   which involves also setting up of RX list elements
 *	d) Initialize the FIFO settings of Yukon2 (Watermark etc.)
 *	e) Initialize the Port (MAC, PHY etc.)
 *	f) Initialize the MC addresses
 *
 * Returns:	N/A
 */
void SkY2PortStart(
SK_AC   *pAC,    /* adapter control context                            */
SK_IOC   IoC,    /* I/O control context (address of adapter registers) */
int      Port)   /* port to start                                      */
{
	SK_U32	DWord;
	SK_HWLE	*pLE;
	SK_U32	PrefetchReg;	/* register for Put index  */

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("==> SkY2PortStart (Port %c)\n", 'A' + Port));

	/*
	** Initialize the LET indices
	*/
	pAC->RxPort[Port].RxLET.Done                = 0; 
	pAC->RxPort[Port].RxLET.Put                 = 0;
	pAC->RxPort[Port].RxLET.HwPut               = 0;
	pAC->TxPort[Port][TX_PRIO_LOW].TxALET.Done  = 0;    
	pAC->TxPort[Port][TX_PRIO_LOW].TxALET.Put   = 0;
	pAC->TxPort[Port][TX_PRIO_LOW].TxALET.HwPut = 0;
	if (HW_SYNC_TX_SUPPORTED(pAC)) {
		pAC->TxPort[Port][TX_PRIO_LOW].TxSLET.Done  = 0;    
		pAC->TxPort[Port][TX_PRIO_LOW].TxSLET.Put   = 0;
		pAC->TxPort[Port][TX_PRIO_LOW].TxSLET.HwPut = 0;
	}
	
	if (HW_FEATURE(pAC, HWF_WA_DEV_420)) {
		/*
		** It might be that we have to limit the RX buffers 
		** effectively passed to HW. Initialize the start
		** value in that case...
		*/
		NbrRxBuffersInHW = 0;
	}

	/*
	** TODO on dual net adapters we need to check if
	** StatusLETable need to be set...
	** 
	** pAC->StatusLETable.Done  = 0;
	** pAC->StatusLETable.Put   = 0;
	** pAC->StatusLETable.HwPut = 0;
	** SkGeY2InitPrefetchUnit(pAC, pAC->IoBase, Q_ST, &pAC->StatusLETable);
	*/

	/*
	** Initialize the LET in HW (enables also prefetch unit)
	*/
	SkGeY2InitPrefetchUnit(pAC, IoC,(Port == 0) ? Q_R1 : Q_R2,
			&pAC->RxPort[Port].RxLET);
	SkGeY2InitPrefetchUnit( pAC, IoC,(Port == 0) ? Q_XA1 : Q_XA2, 
			&pAC->TxPort[Port][TX_PRIO_LOW].TxALET);
	if (HW_SYNC_TX_SUPPORTED(pAC)) {
		SkGeY2InitPrefetchUnit( pAC, IoC, (Port == 0) ? Q_XS1 : Q_XS2,
				&pAC->TxPort[Port][TX_PRIO_HIGH].TxSLET);
	}

	/*
	** Using default values for the watermarks and the timer inis
	** degrades the performance a lot! Therefore use own values 
	** instead.
	*/

	/*
	** SK_OUT8(IoC, STAT_FIFO_WM, 1);
	** SK_OUT8(IoC, STAT_FIFO_ISR_WM, 1);
	** SK_OUT32(IoC, STAT_LEV_TIMER_INI, 50);
	** SK_OUT32(IoC, STAT_ISR_TIMER_INI, 10);
	*/

	/*
	** Initialize the Port (MAC, PHY etc.)
	*/
	if (SkGeInitPort(pAC, IoC, Port)) {
		if (Port == 0) {
			printk("%s: SkGeInitPort A failed.\n",pAC->dev[0]->name);
		} else {
			printk("%s: SkGeInitPort B failed.\n",pAC->dev[1]->name);
		}
	}
	
	if (IS_GMAC(pAC)) {
		/* disable Rx GMAC FIFO Flush Mode */
		SK_OUT8(IoC, MR_ADDR(Port, RX_GMF_CTRL_T), (SK_U8) GMF_RX_F_FL_OFF);
	}

	/*
	** Initialize the MC addresses
	*/
	SkAddrMcUpdate(pAC,IoC, Port);

	SkMacRxTxEnable(pAC, IoC,Port);
				
#ifdef USE_SK_RX_CHECKSUM
	/*
	**
	*/
	SkGeRxCsum(pAC, IoC, Port, SK_TRUE);
	
	GET_RX_LE(pLE, &pAC->RxPort[Port].RxLET);
	RXLE_SET_STACS1(pLE, pAC->CsOfs1);
	RXLE_SET_STACS2(pLE, pAC->CsOfs2);
	RXLE_SET_CTRL(pLE, 0);

	RXLE_SET_OPC(pLE, OP_TCPSTART | HW_OWNER);
	FLUSH_OPC(pLE);
	if (Port == 0) {
		PrefetchReg = Y2_PREF_Q_ADDR(Q_R1, PREF_UNIT_PUT_IDX_REG);
	} else {
		PrefetchReg = Y2_PREF_Q_ADDR(Q_R2, PREF_UNIT_PUT_IDX_REG);
	}
	DWord = GET_PUT_IDX(&pAC->RxPort[Port].RxLET);
	SK_OUT32(IoC, PrefetchReg, DWord);
	UPDATE_HWPUT_IDX(&pAC->RxPort[Port].RxLET);
#endif

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("<== SkY2PortStart()\n"));
}

/******************************************************************************
 *
 * Local Functions
 *
 *****************************************************************************/

/*****************************************************************************
 *
 *	InitPacketQueues - initialize SW settings of packet queues
 *
 * Description:
 *	This function will initialize the packet queues for a port.
 *
 * Returns: N/A
 */
static void InitPacketQueues(
SK_AC  *pAC,   /* pointer to adapter control context */
int     Port)  /* index of port to be initialized    */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
		("==> InitPacketQueues(Port %c)\n", 'A' + Port));
	
	pAC->RxPort[Port].RxQ_working.pHead = NULL;
	pAC->RxPort[Port].RxQ_working.pTail = NULL;
	spin_lock_init(&pAC->RxPort[Port].RxQ_working.QueueLock);
	
	pAC->RxPort[Port].RxQ_waiting.pHead = NULL;
	pAC->RxPort[Port].RxQ_waiting.pTail = NULL;
	spin_lock_init(&pAC->RxPort[Port].RxQ_waiting.QueueLock);
	
	pAC->TxPort[Port][TX_PRIO_LOW].TxQ_free.pHead = NULL;
	pAC->TxPort[Port][TX_PRIO_LOW].TxQ_free.pTail = NULL;
	spin_lock_init(&pAC->TxPort[Port][TX_PRIO_LOW].TxQ_free.QueueLock);

	pAC->TxPort[Port][TX_PRIO_LOW].TxAQ_working.pHead = NULL;
	pAC->TxPort[Port][TX_PRIO_LOW].TxAQ_working.pTail = NULL;
	spin_lock_init(&pAC->TxPort[Port][TX_PRIO_LOW].TxAQ_working.QueueLock);
	
	pAC->TxPort[Port][TX_PRIO_LOW].TxAQ_waiting.pHead = NULL;
	pAC->TxPort[Port][TX_PRIO_LOW].TxAQ_waiting.pTail = NULL;
	spin_lock_init(&pAC->TxPort[Port][TX_PRIO_LOW].TxAQ_waiting.QueueLock);
	
#if USE_SYNC_TX_QUEUE
	pAC->TxPort[Port][TX_PRIO_LOW].TxSQ_working.pHead = NULL;
	pAC->TxPort[Port][TX_PRIO_LOW].TxSQ_working.pTail = NULL;
	spin_lock_init(&pAC->TxPort[Port][TX_PRIO_LOW].TxSQ_working.QueueLock);

	pAC->TxPort[Port][TX_PRIO_LOW].TxSQ_waiting.pHead = NULL;
	pAC->TxPort[Port][TX_PRIO_LOW].TxSQ_waiting.pTail = NULL;
	spin_lock_init(&pAC->TxPort[Port][TX_PRIO_LOW].TxSQ_waiting.QueueLock);
#endif
	
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
		("<== InitPacketQueues(Port %c)\n", 'A' + Port));
}	/* InitPacketQueues */

/***********************************************************************
 *
 *	GiveRxBufferToHw - commits a previously allocated DMA area to HW
 *
 * Description:
 *	This functions gives receive buffers to HW. If no list elements
 *	are available the buffers will be queued. 
 *
 * Notes:
 *       This function can run only once in a system at one time.
 *
 * Returns: N/A
 */
static void GiveRxBufferToHw(
SK_AC      *pAC,       /* pointer to adapter control context         */
SK_IOC      IoC,       /* I/O control context (address of registers) */
int         Port,      /* port index for which the buffer is used    */
SK_PACKET  *pPacket)   /* receive buffer(s)                          */
{
	SK_HWLE		*pLE;
	SK_LE_TABLE	*pLETab;

	SK_BOOL		Done = SK_FALSE;	/* at least on LE changed? */
	SK_U32		LowAddress;
	SK_U32		HighAddress;
	SK_U32		PrefetchReg;		/* register for Put index  */
	SK_U8		OpCode;
	SK_U8		Ctrl;

	unsigned	NumFree;
	unsigned	Required;
	unsigned long	Flags;

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
		("==> GiveRxBufferToHw(Port %c, Packet %p)\n", 'A' + Port,
		pPacket));

	pLETab	= &pAC->RxPort[Port].RxLET;

	if (Port == 0) {
		PrefetchReg = Y2_PREF_Q_ADDR(Q_R1, PREF_UNIT_PUT_IDX_REG);
	} else {
		PrefetchReg = Y2_PREF_Q_ADDR(Q_R2, PREF_UNIT_PUT_IDX_REG);
	} 

	if (pPacket != NULL) {
		/*
		** For the time being, we have only one packet passed
		** to this function which might be changed in future!
		*/
		PUSH_PKT_AS_LAST_IN_QUEUE(&pAC->RxPort[Port].RxQ_waiting, pPacket);
	}

	/* 
	** now pPacket contains the very first waiting packet
	*/
	POP_FIRST_PKT_FROM_QUEUE(&pAC->RxPort[Port].RxQ_waiting, pPacket);
	while (pPacket != NULL) {
		if (HW_FEATURE(pAC, HWF_WA_DEV_420)) {
			if (NbrRxBuffersInHW >= MAX_NBR_RX_BUFFERS_IN_HW) {
				PUSH_PKT_AS_FIRST_IN_QUEUE(&pAC->RxPort[Port].RxQ_waiting, pPacket);
				SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
					("<== GiveRxBufferToHw()\n"));
				return;
			} 
			NbrRxBuffersInHW++;
		}

		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
			("Try to add packet %p\n", pPacket));

		/* 
		** Check whether we have enough listelements:
		**
		** we have to take into account that each fragment 
		** may need an additional list element for the high 
		** part of the address here I simplified it by 
		** using MAX_FRAG_OVERHEAD maybe it's worth to split 
		** this constant for Rx and Tx or to calculate the
		** real number of needed LE's
		*/
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
			("\tNum %d Put %d Done %d Free %d %d\n",
			pLETab->Num, pLETab->Put, pLETab->Done,
			NUM_FREE_LE_IN_TABLE(pLETab),
			(NUM_FREE_LE_IN_TABLE(pLETab))));

		Required = pPacket->NumFrags + MAX_FRAG_OVERHEAD;
		NumFree = NUM_FREE_LE_IN_TABLE(pLETab);
		if (NumFree) {
			NumFree--;
		}

		if (Required > NumFree ) {
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
				SK_DBGCAT_DRV_RX_PROGRESS | SK_DBGCAT_DRV_ERROR,
				("\tOut of LEs have %d need %d\n",
				NumFree, Required));

			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
				("\tWaitQueue starts with packet %p\n", pPacket));
			PUSH_PKT_AS_FIRST_IN_QUEUE(&pAC->RxPort[Port].RxQ_waiting, pPacket);
			if (Done) {
				/*
				** write Put index to BMU or Polling Unit and make the LE's
				** available for the hardware
				*/
				SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
					("\tWrite new Put Idx\n"));

				SK_OUT32(IoC, PrefetchReg, GET_PUT_IDX(pLETab));
				UPDATE_HWPUT_IDX(pLETab);
			}
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
				("<== GiveRxBufferToHw()\n"));
			return;
		} else {
			if (!AllocAndMapRxBuffer(pAC, pPacket, Port)) {
				/*
				** Failure while allocating sk_buff might
				** be due to temporary short of resources
				** Maybe next time buffers are available.
				** Until this, the packet remains in the 
				** RX waiting queue...
				*/
				SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
					SK_DBGCAT_DRV_RX_PROGRESS | SK_DBGCAT_DRV_ERROR,
					("Failed to allocate Rx buffer\n"));

				SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
					("WaitQueue starts with packet %p\n", pPacket));
				PUSH_PKT_AS_FIRST_IN_QUEUE(&pAC->RxPort[Port].RxQ_waiting, pPacket);
				if (Done) {
					/*
					** write Put index to BMU or Polling 
					** Unit and make the LE's
					** available for the hardware
					*/
					SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
						("\tWrite new Put Idx\n"));
	
					SK_OUT32(IoC, PrefetchReg, GET_PUT_IDX(pLETab));
					UPDATE_HWPUT_IDX(pLETab);
				}
				SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
					("<== GiveRxBufferToHw()\n"));
				return;
			}
		}
		Done = SK_TRUE;

		/*
		** :;:; TODO
		** here we have to check for the 64 bit part of the address first
		** and use a ADDR64-LE in case of change
		**
		** so this is simplified model without csum offload and with
		** 32 bit phys addresses
		*/
		OpCode = OP_PACKET | HW_OWNER;
		Ctrl = 0;

		/*
		** Begin fragment processing: On the receive side we always have 
		** one fragment belonging to one packet.
		*/
		GET_RX_LE(pLE, pLETab);

		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
			("=== LE empty\n"));

		SK_DBG_DUMP_RX_LE(pLE);

		/* 
		** Fill data into listelement 
		*/
		LowAddress = (SK_U32) (pPacket->pFrag->pPhys & 0xffffffff);
		HighAddress = (SK_U32) (pPacket->pFrag->pPhys >> 32);

		RXLE_SET_ADDR(pLE, LowAddress);
		RXLE_SET_LEN(pLE, pPacket->pFrag->FragLen);
		RXLE_SET_CTRL(pLE, Ctrl);
		RXLE_SET_OPC(pLE, OpCode);
		FLUSH_OPC(pLE);

		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
			("=== LE filled\n"));

		SK_DBG_DUMP_RX_LE(pLE);

		/* 
		** Change OpCode for the following fragments 
		** of a receive buffer. End of fragment processing.
		*/
		OpCode = OP_BUFFER | HW_OWNER;

		/* 
		** Remember next LE for rx complete 
		*/
		pPacket->NextLE = GET_PUT_IDX(pLETab);

		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
			("\tPackets Next LE is %d\n", pPacket->NextLE));

		/* 
		** Add packet to working receive buffer queue and get
		** any next packet out of the waiting queue
		*/
		PUSH_PKT_AS_LAST_IN_QUEUE(&pAC->RxPort[Port].RxQ_working, pPacket);
		POP_FIRST_PKT_FROM_QUEUE(&pAC->RxPort[Port].RxQ_waiting, pPacket);
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
		("\tWaitQueue is empty\n"));

	if (Done) {
		/*
		** write Put index to BMU or Polling Unit and make the LE's
		** available for the hardware
		*/
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
			("\tWrite new Put Idx\n"));

		spin_lock_irqsave(&pAC->SetPutIndexLock, Flags);
		/* Speed enhancement for a2 chipsets */
		if (HW_FEATURE(pAC, HWF_WA_DEV_42)) {
			SkGeY2SetPutIndex(pAC, pAC->IoBase, Y2_PREF_Q_ADDR(Q_R1,0), pLETab);
		} else {
			/* write put index */
			if (Port == 0) { 
				SK_OUT32(IoC, Y2_PREF_Q_ADDR(Q_R1, PREF_UNIT_PUT_IDX_REG), GET_PUT_IDX(pLETab)); 
			} else {
				SK_OUT32(IoC, Y2_PREF_Q_ADDR(Q_R2, PREF_UNIT_PUT_IDX_REG), GET_PUT_IDX(pLETab)); 
			}

			/* Update put index */
			UPDATE_HWPUT_IDX(pLETab);
		}
		spin_unlock_irqrestore(&pAC->SetPutIndexLock, Flags);
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
		("<== GiveRxBufferToHw()\n"));
}       /* GiveRxBufferToHw */

/***********************************************************************
 *
 *	FillReceiveTableYukon2 - map any waiting RX buffers to HW
 *
 * Description:
 *	If the list element table contains more empty elements than 
 *	specified this function tries to refill them.
 *
 * Notes:
 *       This function can run only once per port in a system at one time.
 *
 * Returns: N/A
 */
static void FillReceiveTableYukon2(
SK_AC    *pAC,	 /* pointer to adapter control context */
SK_IOC    IoC,	 /* I/O control context                */
int       Port)	 /* port index of RX                   */
{
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
		("==> FillReceiveTableYukon2 (Port %c)\n", 'A' + Port));

	if (NUM_FREE_LE_IN_TABLE(&pAC->RxPort[Port].RxLET) >
		pAC->MaxUnusedRxLeWorking) {

		/* 
		** Give alle waiting receive buffers down 
		** The queue holds all RX packets that
		** need a fresh allocation of the sk_buff.
		*/
		if (pAC->RxPort[Port].RxQ_waiting.pHead != NULL) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
			("Waiting queue is not empty -> give it to HW"));
			GiveRxBufferToHw(pAC, IoC, Port, NULL);
		}
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
		("<== FillReceiveTableYukon2 ()\n"));
}	/* FillReceiveTableYukon2 */

/******************************************************************************
 *
 *
 *	HandleReceives - will pass any ready RX packet to kernel
 *
 * Description:
 *	This functions handles a received packet. It checks wether it is
 *	valid, updates the receive list element table and gives the receive
 *	buffer to Linux
 *
 * Notes:
 *	This function can run only once per port at one time in the system.
 *
 * Returns: N/A
 */

static SK_BOOL HandleReceives(
SK_AC  *pAC,          /* adapter control context                     */
int     Port,         /* port on which a packet has been received    */
SK_U16  Len,          /* number of bytes which was actually received */
SK_U32  FrameStatus,  /* MAC frame status word                       */
SK_U16  Tcp1,         /* first hw checksum                           */
SK_U16  Tcp2,         /* second hw checksum                          */
SK_U32  Tist,         /* timestamp                                   */
SK_U16  Vlan)         /* Vlan Id                                     */
{

	SK_PACKET	*pSkPacket;
	SK_LE_TABLE	*pLETab;
	SK_MBUF		*pRlmtMbuf;	/* buffer for giving RLMT frame */
	struct sk_buff	*pMsg;		/* ptr to message holding frame	*/

	SK_BOOL		SlowPathLock = SK_TRUE;
	SK_BOOL		IsGoodPkt;
	SK_BOOL		IsBc;
	SK_BOOL		IsMc;
	SK_EVPARA	EvPara;		/* an event parameter union	*/
	SK_I16		LenToFree;	/* must be signed integer	*/

	unsigned long	Flags;		/* for spin lock		*/
	unsigned int	ForRlmt;
	unsigned short	Csum1;
	unsigned short	Csum2;
	unsigned short	Type;
	int		IpFrameLength;
	int		FrameLength;	/* total length of recvd frame	*/
	int		NumBytes; 
	int		Result;
	int		Offset = 0; 

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
		("==> HandleReceives (Port %c)\n", 'A' + Port));

	/* 
	** initialize vars for selected port 
	*/
	pLETab = &pAC->RxPort[Port].RxLET;

	/* 
	** check whether we want to receive this packet 
	*/
	SK_Y2_RXSTAT_CHECK_PKT(Len, FrameStatus, IsGoodPkt);

	/*
	** Remember length to free (in case of RxBuffer overruns;
	** unlikely, but might happen once in a while)
	*/
	LenToFree = (SK_I16) Len;

	/* 
	** maybe we put these two checks into the SK_RXDESC_CHECK_PKT macro too 
	*/
	if (Len > pAC->RxBufSize) {
		IsGoodPkt = SK_FALSE;
	}

	/*
	** take first receive buffer out of working queue 
	*/
	POP_FIRST_PKT_FROM_QUEUE(&pAC->RxPort[Port].RxQ_working, pSkPacket);
	if (HW_FEATURE(pAC, HWF_WA_DEV_420)) {
		NbrRxBuffersInHW--;
	}

	/* 
	** Verify the received length of the frame! Note that having 
	** multiple RxBuffers being aware of one single receive packet
	** (one packet spread over multiple RxBuffers) is not supported 
	** by this driver!
	*/
	if ((Len > pAC->RxBufSize) || (Len > (SK_U16) pSkPacket->PacketLen)) {
		IsGoodPkt = SK_FALSE;
	}

	/* 
	** Reset own bit in LE's between old and new Done index
	** This is not really necessary but makes debugging easier 
	*/
	CLEAR_LE_OWN_FROM_DONE_TO(pLETab, pSkPacket->NextLE);

	/* 
	** Free the list elements for new Rx buffers 
	*/
	SET_DONE_INDEX(pLETab, pSkPacket->NextLE);
	pMsg = pSkPacket->pMBuf;
	FrameLength = Len;

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
		("Received frame of length %d on port %d\n",
		FrameLength, Port));

	if (!IsGoodPkt) {
		/* 
		** release the DMA mapping 
		*/
		pci_dma_sync_single(pAC->PciDev,
				(dma_addr_t) pSkPacket->pFrag->pPhys,
				pSkPacket->pFrag->FragLen,
				PCI_DMA_FROMDEVICE); 

		DEV_KFREE_SKB_ANY(pSkPacket->pMBuf);
		PUSH_PKT_AS_LAST_IN_QUEUE(&pAC->RxPort[Port].RxQ_waiting, pSkPacket);
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
			("<== HandleReceives (Port %c)\n", 'A' + Port));

		/*
		** Sanity check for RxBuffer overruns...
		*/
		LenToFree = LenToFree - (pSkPacket->pFrag->FragLen);
		while (LenToFree > 0) {
			POP_FIRST_PKT_FROM_QUEUE(&pAC->RxPort[Port].RxQ_working, pSkPacket);
			if (HW_FEATURE(pAC, HWF_WA_DEV_420)) {
				NbrRxBuffersInHW--;
			}
			CLEAR_LE_OWN_FROM_DONE_TO(pLETab, pSkPacket->NextLE);
			SET_DONE_INDEX(pLETab, pSkPacket->NextLE);
			pci_dma_sync_single(pAC->PciDev,
					(dma_addr_t) pSkPacket->pFrag->pPhys,
					pSkPacket->pFrag->FragLen,
					PCI_DMA_FROMDEVICE); 

			DEV_KFREE_SKB_ANY(pSkPacket->pMBuf);
			PUSH_PKT_AS_LAST_IN_QUEUE(&pAC->RxPort[Port].RxQ_waiting, pSkPacket);
			LenToFree = LenToFree - ((SK_I16)(pSkPacket->pFrag->FragLen));
			
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_RX_PROGRESS | SK_DBGCAT_DRV_ERROR,
				("<== HandleReceives (Port %c) drop faulty len pkt (2)\n", 'A' + Port));
		}
		return(SK_TRUE);
	} else {
		/* 
		** if short frame then copy data to reduce memory waste ++++ 
		*/

		/*
		** if large frame, or SKB allocation failed, pass
		** the SKB directly to the networking
		*/

		/* 
		** Release the DMA mapping 
		*/
		pci_unmap_single(pAC->PciDev,
				 pSkPacket->pFrag->pPhys,
				 pAC->RxBufSize - 2,
				 PCI_DMA_FROMDEVICE);

		/* set length in message */
		skb_put(pMsg, FrameLength);
		/* hardware checksum */
		Type = ntohs(*((short*)&pMsg->data[12]));

#ifdef USE_SK_RX_CHECKSUM
		if (Type == 0x800) {
			Csum1=Tcp1;  /* le16_to_cpu(pRxd->TcpSums & 0xffff); */
			Csum2=Tcp2;  /* le16_to_cpu((pRxd->TcpSums >> 16) & 0xffff); */
			*((char *)&(IpFrameLength)) = pMsg->data[16];
			*(((char *)&(IpFrameLength))+1) = pMsg->data[17];
			IpFrameLength = ntohs(IpFrameLength);
			/*
			 * Test: If frame is padded, a check is not possible!
			 * Frame not padded? Length difference must be 14 (0xe)!
			 */
			if ((FrameLength - IpFrameLength) != 0xe) {
					/* Frame padded => TCP offload not possible! */
					pMsg->ip_summed = CHECKSUM_NONE;
			} else {
			/* Frame not padded => TCP offload! */
				if ((((Csum1 & 0xfffe) && (Csum2 & 0xfffe)) &&
					(pAC->GIni.GIChipId == CHIP_ID_GENESIS)) ||
				(pAC->ChipsetType)) {
					Result = SkCsGetReceiveInfo(pAC,
						&pMsg->data[14],
						Csum1, Csum2, Port);

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
					} else if (Result ==
						SKCS_STATUS_TCP_CSUM_ERROR ||
						Result ==
						SKCS_STATUS_UDP_CSUM_ERROR ||
						Result ==
						SKCS_STATUS_IP_CSUM_ERROR_UDP ||
						Result ==
						SKCS_STATUS_IP_CSUM_ERROR_TCP ||
						Result ==
						SKCS_STATUS_IP_CSUM_ERROR ) {
						/* HW Checksum error */
						SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
							SK_DBGCAT_DRV_RX_PROGRESS | SK_DBGCAT_DRV_ERROR,
							("skge: CRC error. Frame dropped!\n"));
						DEV_KFREE_SKB_ANY(pMsg);
						PUSH_PKT_AS_LAST_IN_QUEUE(&pAC->RxPort[Port].RxQ_waiting, pSkPacket);
						SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
								SK_DBGCAT_DRV_RX_PROGRESS,
							("<== HandleReceives (Port %c)\n", 'A' + Port));
						return(SK_TRUE);
					} else {
						pMsg->ip_summed = CHECKSUM_NONE;
					}
				}/* checksumControl calculation valid */
			} /* Frame length check */
		} /* IP frame */
#else
		pMsg->ip_summed = CHECKSUM_NONE;	
#endif
		/* } * frame > SK_COPY_TRESHOLD */
		
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV,	1,("V"));
		ForRlmt = SK_RLMT_RX_PROTOCOL;

		IsBc = (FrameStatus & XMR_FS_BC)==XMR_FS_BC;
		SK_RLMT_PRE_LOOKAHEAD(pAC, Port, FrameLength,
			IsBc, &Offset, &NumBytes);
		if (NumBytes != 0) {
			IsMc = (FrameStatus & XMR_FS_MC)==XMR_FS_MC;
			SK_RLMT_LOOKAHEAD(pAC, Port,
				&pMsg->data[Offset],
				IsBc, IsMc, &ForRlmt);
		}

		if (ForRlmt == SK_RLMT_RX_PROTOCOL) {
					SK_DBG_MSG(NULL, SK_DBGMOD_DRV,	1,("W"));
			/* send up only frames from active port */
			if ((Port == pAC->ActivePort) ||
				(pAC->RlmtNets == 2)) {
				/* frame for upper layer */
				SK_DBG_MSG(NULL, SK_DBGMOD_DRV, 1,("U"));
#ifdef xDEBUG
				DumpMsg(pMsg, "Rx");
#endif
				SK_PNMI_CNT_RX_OCTETS_DELIVERED(pAC,
					FrameLength, Port);

				pMsg->dev = pAC->dev[Port];
				pMsg->protocol = eth_type_trans(pMsg,
					pAC->dev[Port]);
				netif_rx(pMsg);
				pAC->dev[Port]->last_rx = jiffies;
			} else {
				/* drop frame */
				SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
					SK_DBGCAT_DRV_RX_PROGRESS,
					("D"));
				DEV_KFREE_SKB_ANY(pMsg);
			}
			PUSH_PKT_AS_LAST_IN_QUEUE(&pAC->RxPort[Port].RxQ_waiting, pSkPacket);
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
				("<== HandleReceives (Port %c)\n", 'A' + Port));
			return(SK_TRUE);
			
		} else { /* if not for rlmt */
			/* packet for rlmt */
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_RX_PROGRESS, ("R"));
			pRlmtMbuf = SkDrvAllocRlmtMbuf(pAC,
				pAC->IoBase, FrameLength);
			if (pRlmtMbuf != NULL) {
				pRlmtMbuf->pNext = NULL;
				pRlmtMbuf->Length = FrameLength;
				pRlmtMbuf->PortIdx = Port;
				EvPara.pParaPtr = pRlmtMbuf;
				memcpy((char*)(pRlmtMbuf->pData),
					   (char*)(pMsg->data),
					   FrameLength);

				/* SlowPathLock needed? */
				if (SlowPathLock == SK_TRUE) {
					spin_lock_irqsave(&pAC->SlowPathLock, Flags);
					SkEventQueue(pAC, SKGE_RLMT,
						SK_RLMT_PACKET_RECEIVED,
						EvPara);
					pAC->CheckQueue = SK_TRUE;
					spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
				} else {
					SkEventQueue(pAC, SKGE_RLMT,
						SK_RLMT_PACKET_RECEIVED,
						EvPara);
					pAC->CheckQueue = SK_TRUE;
				}

				SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
					SK_DBGCAT_DRV_RX_PROGRESS,
					("Q"));
			}
			if ((pAC->dev[Port]->flags &
				(IFF_PROMISC | IFF_ALLMULTI)) != 0 ||
				(ForRlmt & SK_RLMT_RX_PROTOCOL) ==
				SK_RLMT_RX_PROTOCOL) {
				pMsg->dev = pAC->dev[Port];
				pMsg->protocol = eth_type_trans(pMsg,
					pAC->dev[Port]);
				netif_rx(pMsg);
				pAC->dev[Port]->last_rx = jiffies;
			} else {
				DEV_KFREE_SKB_ANY(pMsg);
			}
			PUSH_PKT_AS_LAST_IN_QUEUE(&pAC->RxPort[Port].RxQ_waiting, pSkPacket);
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
				("<== HandleReceives (Port %c)\n", 'A' + Port));
			return(SK_TRUE);

		} /* if packet for rlmt */
	} /* end if-else (IsGoodPkt) */

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
		("<== HandleReceives (Port %c)\n", 'A' + Port));
	return(SK_TRUE);

}	/* HandleReceives */

/***********************************************************************
 *
 * CheckForSendComplete
 *
 * Description:
 *	This function checks the queues of a port for completed send
 *	packets and returns these packets back to the OS.
 *
 * Notes:
 *	This function can run simultaneously for both ports if
 *	the OS function OSReturnPacket() can handle this,
 *
 *	Such a send complete does not mean, that the packet is really
 *	out on the wire. We just know that the adapter has copied it
 *	into its internal memory and the buffer in the systems memory
 *	is no longer needed.
 *
 * Returns: N/A
 */
static void CheckForSendComplete(
SK_AC         *pAC,     /* pointer to adapter control context  */
SK_IOC         IoC,     /* I/O control context                 */
int            Port,    /* port index                          */
SK_PKT_QUEUE  *pPQ,     /* tx working packet queue to check    */
SK_LE_TABLE   *pLETab,  /* corresponding list element table    */
unsigned int   Done)    /* done index reported for this LET    */
{
	SK_PACKET	*pSkPacket;
	SK_FRAG		*pNextFrag;
	SK_PKT_QUEUE	 SendCmplPktQ = { NULL, NULL, SPIN_LOCK_UNLOCKED };
	SK_BOOL          DoWakeQueue  = SK_FALSE;
	unsigned long	 Flags;
	unsigned	 Put;
	
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
		("==> CheckForSendComplete(Port %c)\n", 'A' + Port));

	/* 
	** Reset own bit in LE's between old and new Done index
	** This is not really necessairy but makes debugging easier 
	*/
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
		("Clear Own Bits in TxTable from %d to %d\n",
		pLETab->Done, (Done == 0) ?
		NUM_LE_IN_TABLE(pLETab) :
		(Done - 1)));

	spin_lock_irqsave(&pAC->TxPort[Port][0].TxDesRingLock, Flags);

	CLEAR_LE_OWN_FROM_DONE_TO(pLETab, Done);

	Put = GET_PUT_IDX(pLETab);

	/* 
	** Check whether some packets have been completed 
	*/
	PLAIN_POP_FIRST_PKT_FROM_QUEUE(pPQ, pSkPacket);
	while (pSkPacket != NULL) {
		
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
			("Check Completion of Tx packet %p\n", pSkPacket));
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
			("Put %d NewDone %d NextLe of Packet %d\n", Put, Done,
			pSkPacket->NextLE));

		if ((Put > Done) &&
			((pSkPacket->NextLE > Put) || (pSkPacket->NextLE <= Done))) {
			PLAIN_PUSH_PKT_AS_LAST_IN_QUEUE(&SendCmplPktQ, pSkPacket);
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
				("Packet finished (a)\n"));
		} else if ((Done > Put) &&
			(pSkPacket->NextLE > Put) && (pSkPacket->NextLE <= Done)) {
			PLAIN_PUSH_PKT_AS_LAST_IN_QUEUE(&SendCmplPktQ, pSkPacket);
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
				("Packet finished (b)\n"));
		} else if ((Done == TXA_MAX_LE-1) && (Put == 0) && (pSkPacket->NextLE == 0)) {
			PLAIN_PUSH_PKT_AS_LAST_IN_QUEUE(&SendCmplPktQ, pSkPacket);
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
				("Packet finished (b)\n"));
			DoWakeQueue = SK_TRUE;
		} else if (Done == Put) {
			/* all packets have been sent */
			PLAIN_PUSH_PKT_AS_LAST_IN_QUEUE(&SendCmplPktQ, pSkPacket);
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
				("Packet finished (c)\n"));
		} else {
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
				("Packet not yet finished\n"));
			PLAIN_PUSH_PKT_AS_FIRST_IN_QUEUE(pPQ, pSkPacket);
			break;
		}
		PLAIN_POP_FIRST_PKT_FROM_QUEUE(pPQ, pSkPacket);
	}

	/* 
	** Set new done index in list element table
	*/
	SET_DONE_INDEX(pLETab, Done);
	 
	/*
	** All TX packets that are send complete should be added to
	** the free queue again for new sents to come
	*/
	pSkPacket = SendCmplPktQ.pHead;
	while (pSkPacket != NULL) {
		while (pSkPacket->pFrag != NULL) {
			pci_unmap_page(pAC->PciDev,
					(dma_addr_t) pSkPacket->pFrag->pPhys,
					pSkPacket->pFrag->FragLen,
					PCI_DMA_FROMDEVICE);
			PLAIN_PUSH_FRAG_AS_LAST_IN_QUEUE(Port, pSkPacket->pFrag, pNextFrag);
			pSkPacket->pFrag = pNextFrag;
		}

		DEV_KFREE_SKB_ANY(pSkPacket->pMBuf);
		pSkPacket->pMBuf	= NULL;
		pSkPacket = pSkPacket->pNext; /* get next packet */
	}

	/*
	** Append the available TX packets back to free queue
	**
	** May be replaced with macro:
	**
	** PUSH_MULTIPLE_PKT_AS_LAST_IN_QUEUE(&pAC->TxPort[Port][0].TxQ_free, 
	**		SendCmplPktQ.pHead, SendCmplPktQ.pTail);
	*/
	if (SendCmplPktQ.pHead != NULL) { 
		spin_lock_irqsave(&(pAC->TxPort[Port][0].TxQ_free.QueueLock), Flags);
		if (pAC->TxPort[Port][0].TxQ_free.pTail != NULL) {
			pAC->TxPort[Port][0].TxQ_free.pTail->pNext = SendCmplPktQ.pHead;
			pAC->TxPort[Port][0].TxQ_free.pTail        = SendCmplPktQ.pTail;
			if (pAC->TxPort[Port][0].TxQ_free.pHead->pNext == NULL) {
                                netif_wake_queue(pAC->dev[Port]);
                        }
		} else {
			pAC->TxPort[Port][0].TxQ_free.pHead = SendCmplPktQ.pHead;
			pAC->TxPort[Port][0].TxQ_free.pTail = SendCmplPktQ.pTail; 
			netif_wake_queue(pAC->dev[Port]);
		}
		if (Done == Put) {
                        netif_wake_queue(pAC->dev[Port]);
                }
                if (DoWakeQueue) {
                        netif_wake_queue(pAC->dev[Port]);
                        DoWakeQueue = SK_FALSE;
                }
		spin_unlock_irqrestore(&pAC->TxPort[Port][0].TxQ_free.QueueLock, Flags);
	}
	spin_unlock_irqrestore(&pAC->TxPort[Port][0].TxDesRingLock, Flags);

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
		("<== CheckForSendComplete()\n"));

	return;
}	/* CheckForSendComplete */

/*****************************************************************************
 *
 *	UnmapAndFreeTxPktBuffer
 *
 * Description:
 *      This function free any allocated space of receive buffers
 *
 * Arguments:
 *      pAC - A pointer to the adapter context struct.
 *
 */
static void UnmapAndFreeTxPktBuffer(
SK_AC       *pAC,       /* pointer to adapter context             */
SK_PACKET   *pSkPacket,	/* pointer to port struct of ring to fill */
int          TxPort)    /* TX port index                          */
{
	SK_FRAG		*pFrag = pSkPacket->pFrag;
	SK_FRAG		*pNextFrag;
	unsigned long	Flags;

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
		("--> UnmapAndFreeTxPktBuffer\n"));

#define TxLock pAC->TxPort[TxPort][TX_PRIO_LOW].TxDesRingLock
	spin_lock_irqsave(&TxLock,Flags);

	while (pFrag != NULL) {
		pci_unmap_page(pAC->PciDev,
				(dma_addr_t) pFrag->pPhys,
				pFrag->FragLen,
				PCI_DMA_FROMDEVICE);
		PUSH_FRAG_AS_LAST_IN_QUEUE(TxPort, pFrag, pNextFrag);
		pFrag = pNextFrag;
	}

	DEV_KFREE_SKB_ANY(pSkPacket->pMBuf);
	pSkPacket->pMBuf	= NULL;

	spin_unlock_irqrestore(&TxLock,Flags);

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
		("<-- UnmapAndFreeTxPktBuffer\n"));
}

/*****************************************************************************
 *
 * 	HandleStatusLEs
 *
 * Description:
 *	This function checks for any new status LEs that may have been 
  *	received. Those status LEs may either be Rx or Tx ones.
 *
 * Returns:	N/A
 */
static SK_BOOL HandleStatusLEs(
#ifdef CONFIG_SK98LIN_NAPI
SK_AC *pAC,       /* pointer to adapter context   */
int   *WorkDone,  /* Done counter needed for NAPI */
int    WorkToDo)  /* ToDo counter for NAPI        */
#else
SK_AC *pAC)       /* pointer to adapter context   */
#endif
{
	int	 DoneTxA[SK_MAX_MACS];
	int	 DoneTxS[SK_MAX_MACS];
	int	 Port;
	SK_BOOL	 handledStatLE	= SK_FALSE;
	SK_BOOL	 NewDone	= SK_FALSE;
	SK_HWLE	*pLE;
	SK_U16	 HighVal;
	SK_U32	 LowVal;

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
		("==> HandleStatusLEs\n"));

	do {
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
			("Check next Own Bit of ST-LE[%d]: 0x%li \n",
			(pAC->StatusLETable.Done + 1) % NUM_LE_IN_TABLE(&pAC->StatusLETable),
			 OWN_OF_FIRST_LE(&pAC->StatusLETable)));

		while (OWN_OF_FIRST_LE(&pAC->StatusLETable) == HW_OWNER) {
			GET_ST_LE(pLE, &pAC->StatusLETable);
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
				("Working on finished status LE[%d]:\n",
				GET_DONE_INDEX(&pAC->StatusLETable)));
			SK_DBG_DUMP_ST_LE(pLE);
			handledStatLE = SK_TRUE;
			switch (STLE_GET_OPC(pLE) & ~HW_OWNER) {
			case OP_RXSTAT:
				if (pAC->StatusLETable.private) {
					/* 
					** This is always the last Status LE belonging
					** to a received packet -> handle it...
					*/
					HandleReceives(
						pAC,
						STLE_GET_LINK(pLE),
						STLE_GET_LEN(pLE),
						STLE_GET_FRSTATUS(pLE),
						pAC->StatusLETable.Bmu.Stat.TcpSum1,
						pAC->StatusLETable.Bmu.Stat.TcpSum2,
						pAC->StatusLETable.Bmu.Stat.RxTimeStamp,
						pAC->StatusLETable.Bmu.Stat.VlanId);
					pAC->StatusLETable.private = SK_FALSE;
#ifdef CONFIG_SK98LIN_NAPI
					if (*WorkDone >= WorkToDo) {
						break;
					}
					(*WorkDone)++;
#endif
				} else {
					pAC->StatusLETable.Bmu.Stat.TcpSum1 = STLE_GET_TCP1(pLE);
					pAC->StatusLETable.Bmu.Stat.TcpSum2 = STLE_GET_TCP2(pLE);
					pAC->StatusLETable.private = SK_TRUE;
				}
				break;
			case OP_RXVLAN:
				/* this value will be used for next RXSTAT */
				pAC->StatusLETable.Bmu.Stat.VlanId = STLE_GET_VLAN(pLE);
				break;
			case OP_RXTIMEVLAN:
				/* this value will be used for next RXSTAT */
				pAC->StatusLETable.Bmu.Stat.VlanId = STLE_GET_VLAN(pLE);
				/* fall through */
			case OP_RXTIMESTAMP:
				/* this value will be used for next RXSTAT */
				pAC->StatusLETable.Bmu.Stat.RxTimeStamp = STLE_GET_TIST(pLE);
				break;
			case OP_RXCHKSVLAN:
				/* this value will be used for next RXSTAT */
				pAC->StatusLETable.Bmu.Stat.VlanId = STLE_GET_VLAN(pLE);
				/* fall through */
			case OP_RXCHKS:
				/* this value will be used for next RXSTAT */
				pAC->StatusLETable.Bmu.Stat.TcpSum1 = STLE_GET_TCP1(pLE);
				pAC->StatusLETable.Bmu.Stat.TcpSum2 = STLE_GET_TCP2(pLE);
				pAC->StatusLETable.private = SK_TRUE;
				break;
			case OP_RSS_HASH:
				/* this value will be used for next RXSTAT */
#if 0
				pAC->StatusLETable.Bmu.Stat.RssHashValue = STLE_GET_RSS(pLE);
#endif
				break;
			case OP_TXINDEXLE:
				/*
				** :;:; TODO
				** it would be possible to check for which queues
				** the index has been changed and call 
				** CheckForSendComplete() only for such queues
				*/
				STLE_GET_DONE_IDX(pLE,LowVal,HighVal);
				SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
					("LowVal: 0x%x HighVal: 0x%x\n", LowVal, HighVal));

				/*
				** It would be possible to check whether we really
				** need the values for second port or sync queue, 
				** but I think checking whether we need them is 
				** more expensive than the calculation
				*/
				DoneTxA[0] = STLE_GET_DONE_IDX_TXA1(LowVal,HighVal);
				DoneTxS[0] = STLE_GET_DONE_IDX_TXS1(LowVal,HighVal);
				DoneTxA[1] = STLE_GET_DONE_IDX_TXA2(LowVal,HighVal);
				DoneTxS[1] = STLE_GET_DONE_IDX_TXS2(LowVal,HighVal);

				SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
					("DoneTxa1 0x%x DoneTxS1: 0x%x DoneTxa2 0x%x DoneTxS2: 0x%x\n",
					DoneTxA[0], DoneTxS[0], DoneTxA[1], DoneTxS[1]));

				NewDone = SK_TRUE;
				break;
			default:
				/* 
				** We have to handle the illegal Opcode in 
				** Status LE 
				*/
				SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
					("Unexpected OpCode\n"));
				break;
			}

			/* 
			** Reset own bit we have to do this in order to detect a overflow 
			*/
			STLE_SET_OPC(pLE, SW_OWNER);
		}

		/* 
		** Now handle any new transmit complete 
		*/
		if (NewDone) {
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
				("Done Index for Tx BMU has been changed\n"));
			for (Port = 0; Port < pAC->GIni.GIMacsFound; Port++) {
				/* 
				** Do we have a new Done idx ? 
				*/
				if (DoneTxA[Port] != GET_DONE_INDEX(&pAC->TxPort[Port][0].TxALET)) {
					SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
						("Check TxA%d\n", Port + 1));
					CheckForSendComplete(pAC, pAC->IoBase, Port,
						&(pAC->TxPort[Port][0].TxAQ_working),
						&pAC->TxPort[Port][0].TxALET,
						DoneTxA[Port]);
				} else {
					SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
						("No changes for TxA%d\n", Port + 1));
				}
#if USE_SYNC_TX_QUEUE
				if (HW_SYNC_TX_SUPPORTED(pAC)) {
					/* 
					** Do we have a new Done idx ? 
					*/
					if (DoneTxS[Port] !=
						GET_DONE_INDEX(&pAC->TxPort[Port][0].TxSLET)) {
						SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
							SK_DBGCAT_DRV_INT_SRC,
							("Check TxS%d\n", Port));
						CheckForSendComplete(pAC, pAC->IoBase, Port,
							&(pAC->TxPort[Port][0].TxSQ_working),
							&pAC->TxPort[Port][0].TxSLET,
							DoneTxS[Port]);
					} else {
						SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
							SK_DBGCAT_DRV_INT_SRC,
							("No changes for TxS%d\n", Port));
					}
				}
#endif
			}
		}
		NewDone = SK_FALSE;

		/* 
		** Check whether we have to refill our RX table  
		*/
		if (HW_FEATURE(pAC, HWF_WA_DEV_420)) {
			if (NbrRxBuffersInHW < MAX_NBR_RX_BUFFERS_IN_HW) {
				for (Port = 0; Port < pAC->GIni.GIMacsFound; Port++) {
					SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
						("Check for refill of RxBuffers on Port %c\n", 'A' + Port));
					FillReceiveTableYukon2(pAC, pAC->IoBase, Port);
				}
			}
		} else {
			for (Port = 0; Port < pAC->GIni.GIMacsFound; Port++) {
				SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
					("Check for refill of RxBuffers on Port %c\n", 'A' + Port));
				if (NUM_FREE_LE_IN_TABLE(&pAC->RxPort[Port].RxLET) >= 64) {
					FillReceiveTableYukon2(pAC, pAC->IoBase, Port);
				}
			}
		}
#ifdef CONFIG_SK98LIN_NAPI
		if (*WorkDone >= WorkToDo) {
			break;
		}
#endif
	} while (OWN_OF_FIRST_LE(&pAC->StatusLETable) == HW_OWNER);

	/* 
	** Clear status BMU 
	*/
	SK_OUT32(pAC->IoBase, STAT_CTRL, SC_STAT_CLR_IRQ);

	return(handledStatLE);
}	/* HandleStatusLEs */

/*****************************************************************************
 *
 *	AllocateAndInitLETables - allocate memory for the LETable and init
 *
 * Description:
 *	This function will allocate space for the LETable and will also  
 *	initialize them. The size of the tables must have been specified 
 *	before.
 *
 * Arguments:
 *	pAC - A pointer to the adapter context struct.
 *
 * Returns:
 *	SK_TRUE  - all LETables initialized
 *	SK_FALSE - failed
 */
static SK_BOOL AllocateAndInitLETables(
SK_AC  *pAC) /* pointer to adapter context */
{
	unsigned	i;	/* for loops */
	char 		*pVirtMemAddr;
	dma_addr_t	pPhysMemAddr;
	unsigned	Size;
	unsigned	Aligned;
	unsigned	Alignment;

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
		("==> AllocateAndInitLETables()\n"));
	/*
	 * :;:; TODO
	 * the alignment stuff is not ellegant nor optimized
	 * its just a short hack to make it working
	 */
	Alignment = MAX_LEN_OF_LE_TAB;
	/* find out how much memory we need */
	Size = 0;
	for (i = 0; i < pAC->GIni.GIMacsFound; i++) {
		SK_ALIGN_SIZE(LE_TAB_SIZE(RX_MAX_LE), Alignment, Aligned);
		Size += Aligned;
		SK_ALIGN_SIZE(LE_TAB_SIZE(TXA_MAX_LE), Alignment, Aligned);
		Size += Aligned;
		SK_ALIGN_SIZE(LE_TAB_SIZE(TXS_MAX_LE), Alignment, Aligned);
		Size += Aligned;
	}
	SK_ALIGN_SIZE(LE_TAB_SIZE(ST_MAX_LE), Alignment, Aligned);
	Size += Aligned;
	/* if we dont start aligned */
	Size += Alignment;
	pAC->SizeOfAlignedLETables = Size;
	
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
		("We need %08x Bytes\n", Size));
	
	pVirtMemAddr = pci_alloc_consistent(pAC->PciDev, Size, &pPhysMemAddr);
	if (pVirtMemAddr == NULL) {
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
			SK_DBGCAT_INIT | SK_DBGCAT_DRV_ERROR,
			("AllocateAndInitLETables: kernel malloc failed!\n"));
		return (SK_FALSE); 
	}
	/* pAC->pVirtMemAddr = pVirtMemAddr; */

	/* initialize memory */
	SK_MEMSET(pVirtMemAddr, 0, Size);
	ALIGN_ADDR(pVirtMemAddr, Alignment); /* Macro defined in skgew.h */
	
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
		("Virtual address of LETab is %8p!\n", pVirtMemAddr));

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
		("Phys address of LETab is %8p!\n", (void *) pPhysMemAddr));

	for (i = 0; i < pAC->GIni.GIMacsFound; i++) {
		/* Rx list element table */
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
			("RxLeTable for Port %c", 'A' + i));
		SkGeY2InitSingleLETable(
			pAC,
			&pAC->RxPort[i].RxLET,
			RX_MAX_LE,
			pVirtMemAddr,
			(SK_U32) (pPhysMemAddr & 0xffffffff),
			(SK_U32) (((SK_U64) pPhysMemAddr) >> 32));

		SK_ALIGN_SIZE(LE_TAB_SIZE(RX_MAX_LE), Alignment, Aligned);
		pVirtMemAddr += Aligned;
		pPhysMemAddr += Aligned;

		/* tx async list element table */
		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
			("TxALeTable for Port %c", 'A' + i));
		SkGeY2InitSingleLETable(
			pAC,
			&pAC->TxPort[i][0].TxALET,
			TXA_MAX_LE,
			pVirtMemAddr,
			(SK_U32) (pPhysMemAddr & 0xffffffff),
			(SK_U32) (((SK_U64) pPhysMemAddr) >> 32));

		SK_ALIGN_SIZE(LE_TAB_SIZE(TXA_MAX_LE), Alignment, Aligned);
		pVirtMemAddr += Aligned;
		pPhysMemAddr += Aligned;
		/* they are now all initialized with 0 which is sufficient */

		SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
			("TxSLeTable for Port %c", 'A' + i));
		SkGeY2InitSingleLETable(
			pAC,
			&pAC->TxPort[i][0].TxSLET,
			TXS_MAX_LE,
			pVirtMemAddr,
			(SK_U32) (pPhysMemAddr & 0xffffffff),
			(SK_U32) (((SK_U64) pPhysMemAddr) >> 32));

		SK_ALIGN_SIZE(LE_TAB_SIZE(TXS_MAX_LE), Alignment, Aligned);
		pVirtMemAddr += Aligned;
		pPhysMemAddr += Aligned;
		/* they are now all initialized with 0 which is sufficient */
	}
	/* Status list element table */
	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("StLeTable"));

	SkGeY2InitSingleLETable(
		pAC,
		&pAC->StatusLETable,
		ST_MAX_LE,
		pVirtMemAddr,
		(SK_U32) (pPhysMemAddr & 0xffffffff),
		(SK_U32) (((SK_U64) pPhysMemAddr) >> 32));

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT, 
		("<== AllocateAndInitLETables(OK)\n"));
	return(SK_TRUE);
}	/* AllocateAndInitLETables */

/*****************************************************************************
 *
 *	AllocatePacketBuffersYukon2 - allocate packet and fragment buffers
 *
 * Description:
 *      This function will allocate space for the packets and fragments
 *
 * Arguments:
 *      pAC - A pointer to the adapter context struct.
 *
 * Returns:
 *      SK_TRUE  - Memory was allocated correctly
 *      SK_FALSE - An error occured
 */

static SK_BOOL AllocatePacketBuffersYukon2(
SK_AC   *pAC)   /* pointer to adapter context */
{
	SK_PACKET	*pRxPacket;
	SK_PACKET	*pTxPacket;
	SK_FRAG		*pRxFrag;
	SK_FRAG		*pTxFrag;
	SK_FRAG		*pTxJunkFrag;

	unsigned long	 Flags;
	unsigned int	 NumberOfBuffers;
	unsigned int	 i;

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
		("==> AllocatePacketBuffersYukon2()"));

	for (i = 0; i < pAC->GIni.GIMacsFound; i++) {
		/* 
		** Allocate RX packet space, initilize the packets and
		** add them to the RX waiting queue. Waiting queue means 
		** that packet and fragment are initialized, but no sk_buff
		** has been assigned to it yet.
		*/
		pAC->RxPort[i].ReceivePacketTable = 
			kmalloc((RX_MAX_NBR_BUFFERS * sizeof(SK_PACKET)), GFP_KERNEL);

		if (pAC->RxPort[i].ReceivePacketTable == NULL) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
					SK_DBGCAT_INIT | SK_DBGCAT_DRV_ERROR,
				("no space for ReceivePacketTable (port %i)", i));
			break;
		} else {
			SK_MEMSET(pAC->RxPort[i].ReceivePacketTable, 0, 
				(RX_MAX_NBR_BUFFERS * sizeof(SK_PACKET)));
		}

		/*
		** Allocate space for the receive fragment table
		*/
		pAC->RxPort[i].ReceiveFragmentTable = 
			kmalloc((RX_MAX_NBR_BUFFERS * sizeof(SK_FRAG)), GFP_KERNEL);

		if (pAC->RxPort[i].ReceiveFragmentTable == NULL) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
					SK_DBGCAT_INIT | SK_DBGCAT_DRV_ERROR,
				("no space for ReceiveFragmentTable (port %i)", i));
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
					SK_DBGCAT_INIT | SK_DBGCAT_DRV_ERROR,
				("<== AllocatePacketBuffersYukon2 (FAILURE)\n"));
			kfree(pAC->RxPort[i].ReceivePacketTable);
			return(SK_FALSE);
		}

		pRxFrag   = pAC->RxPort[i].ReceiveFragmentTable;
		pRxPacket = pAC->RxPort[i].ReceivePacketTable;

		for (   NumberOfBuffers = 0;
			NumberOfBuffers < RX_MAX_NBR_BUFFERS;
			NumberOfBuffers++) {
			PUSH_PKT_AS_LAST_IN_QUEUE(&pAC->RxPort[i].RxQ_waiting, pRxPacket);
			pRxPacket->pFrag = pRxFrag;
			pRxFrag++;
			pRxPacket++;
		}

		/*
		** Allocate TX packet space, initialize the packets and
		** add them to the TX free queue. Free queue means that
		** packet is available and initialized, but no fragment
		** has been assigned to it. (Must be done at TX side)
		*/
		pAC->TxPort[i][0].TransmitPacketTable = 
			kmalloc((TX_MAX_NBR_BUFFERS * sizeof(SK_PACKET)), GFP_KERNEL);

		if (pAC->TxPort[i][0].TransmitPacketTable == NULL) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
					SK_DBGCAT_INIT | SK_DBGCAT_DRV_ERROR,
				("no space for TransmitPacketTable (port %i)", i));
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
					SK_DBGCAT_INIT | SK_DBGCAT_DRV_ERROR,
				("<== AllocatePacketBuffersYukon2 (FAILURE)\n"));
			kfree(pAC->RxPort[i].ReceiveFragmentTable);
			kfree(pAC->RxPort[i].ReceivePacketTable);
			return(SK_FALSE);
		} else {
			SK_MEMSET(pAC->TxPort[i][0].TransmitPacketTable, 0, 
				(TX_MAX_NBR_BUFFERS * sizeof(SK_PACKET)));
		}
		
		pTxPacket = pAC->TxPort[i][0].TransmitPacketTable;

		for (	NumberOfBuffers = 0; 
			NumberOfBuffers < TX_MAX_NBR_BUFFERS; 
			NumberOfBuffers++) {
			PUSH_PKT_AS_LAST_IN_QUEUE(&pAC->TxPort[i][0].TxQ_free, pTxPacket);
			pTxPacket++;
		}

		/*
		** Allocate space for the transmit fragments and add
		** each fragment in the free fragment queue, so that they
		** can be used in any transmit action.
		*/
		pAC->TxPort[i][0].TransmitFragmentTable = 
			kmalloc(2* (TX_MAX_NBR_BUFFERS * sizeof(SK_FRAG)), GFP_KERNEL);

		if (pAC->TxPort[i][0].TransmitFragmentTable == NULL) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV, 
					SK_DBGCAT_INIT | SK_DBGCAT_DRV_ERROR,
				("no space for TransmitFragmentTable (port %i)", i));
			SK_DBG_MSG(pAC, SK_DBGMOD_DRV, 
					SK_DBGCAT_INIT | SK_DBGCAT_DRV_ERROR,
				("<== AllocatePacketBuffersYukon2 (FAILURE)\n"));
			kfree(pAC->RxPort[i].ReceivePacketTable);
			kfree(pAC->RxPort[i].ReceiveFragmentTable);
			kfree(pAC->TxPort[i][0].TransmitPacketTable);
			return(SK_FALSE);
		} else {
			SK_MEMSET(pAC->TxPort[i][0].TransmitFragmentTable, 0, 
				(2 * (TX_MAX_NBR_BUFFERS * sizeof(SK_PACKET))));
		}

		pTxFrag = pAC->TxPort[i][0].TransmitFragmentTable;
		spin_lock_init(&pAC->TxPort[i][0].TxFreeFragQueue.QueueLock);

		for (	NumberOfBuffers = 0; 
			NumberOfBuffers < (2 * TX_MAX_NBR_BUFFERS); 
			NumberOfBuffers++) {
			/*
			** pTxJunkFrag provided as return value,
			** but is not used at allocation time...
			*/
			PUSH_FRAG_AS_LAST_IN_QUEUE(i, pTxFrag, pTxJunkFrag);
			pTxFrag++;
		}
	} /* end for (i = 0; i < pAC->GIni.GIMacsFound; i++) */

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_INIT,
		("<== AllocatePacketBuffersYukon2 (OK)\n"));
	return(SK_TRUE);

}	/* AllocatePacketBuffersYukon2 */

/*****************************************************************************
 *
 *	FreeLETables - release allocated memory of LETables
 *
 * Description:
 *      This function will free all resources of the LETables
 *
 * Arguments:
 *      pAC - A pointer to the adapter context struct.
 *
 * Returns: N/A
 */

static void FreeLETables(
SK_AC   *pAC)   /* pointer to adapter control context */
{
	dma_addr_t		pPhysMemAddr;
	char *			pVirtMemAddr;
	/* int			Rtv  = 0; */

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("==> FreeLETables()\n"));
	
	/*
	** The RxLETable is the first of all LET. 
	** Therefore we can use its address for the input 
	** of the free function.
	*/
	pVirtMemAddr = (char *) pAC->RxPort[0].RxLET.pLETab;
	pPhysMemAddr = (((SK_U64) pAC->RxPort[0].RxLET.pPhyLETABHigh << (SK_U64) 32) | 
			((SK_U64) pAC->RxPort[0].RxLET.pPhyLETABLow));

	/* free continuous memory */
	pci_free_consistent(pAC->PciDev, pAC->SizeOfAlignedLETables,
			    pVirtMemAddr, pPhysMemAddr);

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("<== FreeLETables()\n"));
}	/* FreeLETables */

/*****************************************************************************
 *
 *	FreePacketBuffers - free's all packet buffers of an adapter
 *
 * Description:
 *      This function will free all previously allocated memory of the 
 *	packet buffers.
 *
 * Arguments:
 *      pAC - A pointer to the adapter context struct.
 *
 * Returns: N/A
 */
static void FreePacketBuffers(
SK_AC   *pAC)   /* pointer to adapter control context */
{
	int Port;

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("==> FreePacketBuffers()\n"));
	
	for (Port = 0; Port < pAC->GIni.GIMacsFound; Port++) {
		kfree(pAC->RxPort[Port].ReceiveFragmentTable);
		kfree(pAC->RxPort[Port].ReceivePacketTable);
		kfree(pAC->TxPort[Port][0].TransmitPacketTable);
		kfree(pAC->TxPort[Port][0].TransmitFragmentTable);
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MSG,
		("<== FreePacketBuffers()\n"));
}	/* FreePacketBuffers */

/*****************************************************************************
 *
 * 	AllocAndMapRxBuffer - fill one buffer into the receive packet/fragment
 *
 * Description:
 *	The function allocates a new receive buffer and assigns it to the
 *	the passsed receive packet/fragment
 *
 * Returns:
 *	SK_TRUE - a buffer was allocated and assigned
 *	SK_FALSE - a buffer could not be added
 */
static SK_BOOL AllocAndMapRxBuffer(
SK_AC      *pAC,        /* pointer to the adapter control context */
SK_PACKET  *pSkPacket,  /* pointer to packet that is to fill      */
int         Port)       /* port the packet belongs to             */
{
	struct sk_buff	*pMsgBlock;	/* pointer to a new message block */
	SK_U64		PhysAddr;	/* physical address of a rx buffer */

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
		("--> AllocAndMapRxBuffer (Port: %i)\n", Port));

	pMsgBlock = alloc_skb(pAC->RxBufSize, GFP_ATOMIC);
	if (pMsgBlock == NULL) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
			SK_DBGCAT_DRV_RX_PROGRESS | SK_DBGCAT_DRV_ERROR,
			("%s: Allocation of rx buffer failed !\n",
			pAC->dev[Port]->name));
		SK_PNMI_CNT_NO_RX_BUF(pAC, pAC->RxPort[Port].PortIndex);
		return(SK_FALSE);
	} 
	/* Alignment for IP frames has been removed, because latest 
	   common module requires 8 byte alignment of data buffer...
	else {
		SK_MEMSET(pMsgBlock->data, 0x00, pAC->RxBufSize);
	}
	skb_reserve(pMsgBlock, 2); * to align IP frames *
	*/

	PhysAddr = (SK_U64) pci_map_page(pAC->PciDev,
		virt_to_page(pMsgBlock->data),
		((unsigned long) pMsgBlock->data &
		~PAGE_MASK),
		pAC->RxBufSize - 2,
		PCI_DMA_FROMDEVICE);

	pSkPacket->pFrag->pVirt		= pMsgBlock->data;
	pSkPacket->pFrag->pPhys		= PhysAddr;
	pSkPacket->pFrag->FragLen	= pAC->RxBufSize - 2; /* for correct unmap later */
	pSkPacket->pMBuf		= pMsgBlock;	
	pSkPacket->NumFrags		= 1;
	pSkPacket->PacketLen		= pAC->RxBufSize - 2;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_RX_PROGRESS,
		("<-- AllocAndMapRxBuffer\n"));

	return (SK_TRUE);
}	/* AllocAndMapRxBuffer */

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
