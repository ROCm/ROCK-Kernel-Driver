/******************************************************************************
 *
 * Name:	skdrv2nd.h
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.21 $
 * Date:	$Date: 2004/06/19 13:57:24 $
 * Purpose:	Second header file for driver and all other modules
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect GmbH.
 *	(C)Copyright 2002-2003 Marvell.
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
 * Description:
 *
 * This is the second include file of the driver, which includes all other
 * neccessary files and defines all structures and constants used by the
 * driver and the common modules.
 *
 * Include File Hierarchy:
 *
 *	see skge.c
 *
 ******************************************************************************/

#ifndef __INC_SKDRV2ND_H
#define __INC_SKDRV2ND_H

#include "h/skqueue.h"
#include "h/skgehwt.h"
#include "h/sktimer.h"
#include "h/sktwsi.h"
#include "h/skgepnmi.h"
#include "h/skvpd.h"
#include "h/skgehw.h"
#include "h/sky2le.h"
#include "h/skgeinit.h"
#include "h/skaddr.h"
#include "h/skgesirq.h"
#include "h/skcsum.h"
#include "h/skrlmt.h"
#include "h/skgedrv.h"

/* Isr return values */
#define SkIsrRetVar     irqreturn_t
#define SkIsrRetNone    IRQ_NONE
#define SkIsrRetHandled IRQ_HANDLED

#define DEV_KFREE_SKB(skb) dev_kfree_skb(skb)
#define DEV_KFREE_SKB_IRQ(skb) dev_kfree_skb_irq(skb)
#define DEV_KFREE_SKB_ANY(skb) dev_kfree_skb_any(skb)

/* global function prototypes ******************************************/
extern SK_MBUF		*SkDrvAllocRlmtMbuf(SK_AC*, SK_IOC, unsigned);
extern void		SkDrvFreeRlmtMbuf(SK_AC*, SK_IOC, SK_MBUF*);
extern SK_U64		SkOsGetTime(SK_AC*);
extern int		SkPciReadCfgDWord(SK_AC*, int, SK_U32*);
extern int		SkPciReadCfgWord(SK_AC*, int, SK_U16*);
extern int		SkPciReadCfgByte(SK_AC*, int, SK_U8*);
extern int		SkPciWriteCfgDWord(SK_AC*, int, SK_U32);
extern int		SkPciWriteCfgWord(SK_AC*, int, SK_U16);
extern int		SkPciWriteCfgByte(SK_AC*, int, SK_U8);
extern int		SkDrvEvent(SK_AC*, SK_IOC IoC, SK_U32, SK_EVPARA);
extern int		SkDrvEnterDiagMode(SK_AC *pAc);
extern int		SkDrvLeaveDiagMode(SK_AC *pAc);

struct s_DrvRlmtMbuf {
	SK_MBUF		*pNext;		/* Pointer to next RLMT Mbuf. */
	SK_U8		*pData;		/* Data buffer (virtually contig.). */
	unsigned	Size;		/* Data buffer size. */
	unsigned	Length;		/* Length of packet (<= Size). */
	SK_U32		PortIdx;	/* Receiving/transmitting port. */
#ifdef SK_RLMT_MBUF_PRIVATE
	SK_RLMT_MBUF	Rlmt;		/* Private part for RLMT. */
#endif
	struct sk_buff	*pOs;		/* Pointer to message block */
};


/*
 * Time macros
 */
#if SK_TICKS_PER_SEC == 100
#define SK_PNMI_HUNDREDS_SEC(t)	(t)
#else
#define SK_PNMI_HUNDREDS_SEC(t)	((((unsigned long)t) * 100) / \
										(SK_TICKS_PER_SEC))
#endif

/*
 * New SkOsGetTime
 */
#define SkOsGetTimeCurrent(pAC, pUsec) {\
	struct timeval t;\
	do_gettimeofday(&t);\
	*pUsec = ((((t.tv_sec) * 1000000L)+t.tv_usec)/10000);\
}


/*
 * ioctl definitions
 */
#define		SK_IOCTL_BASE		(SIOCDEVPRIVATE)
#define		SK_IOCTL_GETMIB		(SK_IOCTL_BASE + 0)
#define		SK_IOCTL_SETMIB		(SK_IOCTL_BASE + 1)
#define		SK_IOCTL_PRESETMIB	(SK_IOCTL_BASE + 2)
#define		SK_IOCTL_GEN		(SK_IOCTL_BASE + 3)
#define		SK_IOCTL_DIAG		(SK_IOCTL_BASE + 4)

typedef struct s_IOCTL	SK_GE_IOCTL;

struct s_IOCTL {
	char*		pData;
	unsigned int	Len;
};


/*
 * define sizes of descriptor rings in bytes
 */
#define		TX_RING_SIZE		(24*1024)
#define		RX_RING_SIZE		(24*1024)

/*
 * define sizes of buffers (Yukon2)
 */
#define		RX_MAX_NBR_BUFFERS	128 /* max number receive buffers */
#define		TX_MAX_NBR_BUFFERS	128 /* max number receive buffers */

/*
 * Buffer size for ethernet packets
 */
#define	ETH_BUF_SIZE	1560	/* multiples of 8 bytes */
#define	ETH_MAX_MTU	1514
#define ETH_MIN_MTU	60
#define ETH_MULTICAST_BIT	0x01
#define SK_JUMBO_MTU	9000

/*
 * transmit priority selects the queue: LOW=asynchron, HIGH=synchron
 */
#define TX_PRIO_LOW	0
#define TX_PRIO_HIGH	1

/*
 * alignment of rx/tx descriptors
 */
#define DESCR_ALIGN	64

/*
 * definitions for pnmi. TODO
 */
#define SK_DRIVER_RESET(pAC, IoC)	0
#define SK_DRIVER_SENDEVENT(pAC, IoC)	0
#define SK_DRIVER_SELFTEST(pAC, IoC)	0
/* For get mtu you must add an own function */
#define SK_DRIVER_GET_MTU(pAc,IoC,i)	0
#define SK_DRIVER_SET_MTU(pAc,IoC,i,v)	0
#define SK_DRIVER_PRESET_MTU(pAc,IoC,i,v)	0

/*
** Interim definition of SK_DRV_TIMER placed in this file until 
** common modules have boon finallized
*/
#define SK_DRV_TIMER			11 
#define	SK_DRV_MODERATION_TIMER		1
#define SK_DRV_MODERATION_TIMER_LENGTH  1000000  /* 1 second */
#define SK_DRV_RX_CLEANUP_TIMER		2
#define SK_DRV_RX_CLEANUP_TIMER_LENGTH	1000000	 /* 100 millisecs */
#define SK_DRV_TX_POLL_TIMER		3
#define SK_DRV_TX_POLL_TIMER_LENGTH	10

/*
** Definitions regarding transmitting frames 
** any calculating any checksum.
*/
#define C_LEN_ETHERMAC_HEADER_DEST_ADDR 6
#define C_LEN_ETHERMAC_HEADER_SRC_ADDR  6
#define C_LEN_ETHERMAC_HEADER_LENTYPE   2
#define C_LEN_ETHERMAC_HEADER           ( (C_LEN_ETHERMAC_HEADER_DEST_ADDR) + \
                                          (C_LEN_ETHERMAC_HEADER_SRC_ADDR)  + \
                                          (C_LEN_ETHERMAC_HEADER_LENTYPE) )

#define C_LEN_ETHERMTU_MINSIZE          46
#define C_LEN_ETHERMTU_MAXSIZE_STD      1500
#define C_LEN_ETHERMTU_MAXSIZE_JUMBO    9000

#define C_LEN_ETHERNET_MINSIZE          ( (C_LEN_ETHERMAC_HEADER) + \
                                          (C_LEN_ETHERMTU_MINSIZE) )

#define C_OFFSET_IPHEADER               C_LEN_ETHERMAC_HEADER
#define C_OFFSET_IPHEADER_IPPROTO       9
#define C_OFFSET_TCPHEADER_TCPCS        16
#define C_OFFSET_UDPHEADER_UDPCS        6

#define C_OFFSET_IPPROTO                ( (C_LEN_ETHERMAC_HEADER) + \
                                          (C_OFFSET_IPHEADER_IPPROTO) )

#define C_PROTO_ID_UDP                  17       /* refer to RFC 790 or Stevens'   */
#define C_PROTO_ID_TCP                  6        /* TCP/IP illustrated for details */

/* TX and RX descriptors *****************************************************/

typedef struct s_RxD RXD; /* the receive descriptor */

struct s_RxD {
	volatile SK_U32	RBControl;	/* Receive Buffer Control */
	SK_U32		VNextRxd;	/* Next receive descriptor,low dword */
	SK_U32		VDataLow;	/* Receive buffer Addr, low dword */
	SK_U32		VDataHigh;	/* Receive buffer Addr, high dword */
	SK_U32		FrameStat;	/* Receive Frame Status word */
	SK_U32		TimeStamp;	/* Time stamp from XMAC */
	SK_U32		TcpSums;	/* TCP Sum 2 / TCP Sum 1 */
	SK_U32		TcpSumStarts;	/* TCP Sum Start 2 / TCP Sum Start 1 */
	RXD		*pNextRxd;	/* Pointer to next Rxd */
	struct sk_buff	*pMBuf;		/* Pointer to Linux' socket buffer */
};

typedef struct s_TxD TXD; /* the transmit descriptor */

struct s_TxD {
	volatile SK_U32	TBControl;	/* Transmit Buffer Control */
	SK_U32		VNextTxd;	/* Next transmit descriptor,low dword */
	SK_U32		VDataLow;	/* Transmit Buffer Addr, low dword */
	SK_U32		VDataHigh;	/* Transmit Buffer Addr, high dword */
	SK_U32		FrameStat;	/* Transmit Frame Status Word */
	SK_U32		TcpSumOfs;	/* Reserved / TCP Sum Offset */
	SK_U16		TcpSumSt;	/* TCP Sum Start */
	SK_U16		TcpSumWr;	/* TCP Sum Write */
	SK_U32		TcpReserved;	/* not used */
	TXD		*pNextTxd;	/* Pointer to next Txd */
	struct sk_buff	*pMBuf;		/* Pointer to Linux' socket buffer */
};

/* 
** The following definitions need to be replaced with the ones from
** the header file sky2le.h!
*/

#define LE_SIZE sizeof(SK_HWLE)

#define MIN_LEN_OF_LE_TAB		128
#define MAX_LEN_OF_LE_TAB		4096
#define MAX_UNUSED_RX_LE_WORKING	8
#ifdef MAX_FRAG_OVERHEAD
#undef MAX_FRAG_OVERHEAD
#define MAX_FRAG_OVERHEAD		4
#endif

// as we have a maximum of 16 physical fragments,
// maximum 1 ADDR64 per physical fragment
// maximum 4 LEs for VLAN, Csum, LargeSend, Packet
#define MIN_LE_FREE_REQUIRED		((16*2) + 4)

/*
** The following defines will most likely stay in this
** file like they are...
*/
#define IS_GMAC(pAc)			(!pAc->GIni.GIGenesis)
#define NUM_FREE_FRAGS(pAC, Port) \
	(pAC->TxPort[Port][TX_PRIO_LOW].TxFreeFragQueue.NumFragsInQueue)

#if USE_SYNC_TX_QUEUE
#define TXS_MAX_LE			256
#else /* !USE_SYNC_TX_QUEUE */
#define TXS_MAX_LE			0
#endif

#define ETHER_MAC_HDR_LEN		(6+6+2) // MAC SRC ADDR, MAC DST ADDR, TYPE
#define IP_HDR_LEN			20
#define TCP_CSUM_OFFS			0x10
#define UDP_CSUM_OFFS			0x06
#define TXA_MAX_LE			256
#define RX_MAX_LE			256
#define ST_MAX_LE			(SK_MAX_MACS) * ((3*RX_MAX_LE) + \
						(TXA_MAX_LE) + (TXS_MAX_LE))

/******************************************************************************
 *
 * Structures specific for Yukon-II
 *
 ******************************************************************************/

typedef	struct s_frag SK_FRAG;
struct s_frag {
 	SK_FRAG		*pNext;
 	char		*pVirt;
  	SK_U64		 pPhys;
 	unsigned int	 FragLen;
};

typedef	struct s_packet SK_PACKET;
struct s_packet {
	/* Common infos */
	SK_PACKET 	*pNext;       	/* pointer for packet queues          */
	unsigned int	 PacketLen;	/* length of packet                   */
	unsigned int	 NumFrags;	/* nbr of fragments (for Rx always 1) */
	SK_FRAG		*pFrag;		/* fragment list                      */
	unsigned int	 NextLE;	/* next LE to use for the next packet */

	/* Private infos */
	struct sk_buff	*pMBuf;		/* Pointer to Linux' socket buffer    */
};

typedef	struct s_queue SK_PKT_QUEUE;
struct s_queue {
 	SK_PACKET	*pHead;
 	SK_PACKET	*pTail;
	spinlock_t	 QueueLock;	/* serialize packet accesses          */
};

typedef struct s_frag_queue SK_FRAG_QUEUE;
struct s_frag_queue {
	SK_FRAG		*pHead;
        SK_FRAG		*pTail;
	spinlock_t	 QueueLock;	/* serialize fragment accesses        */
	unsigned int	 NumFragsInQueue;
};

/*******************************************************************************
 *
 * Macros specific for Yukon-II queues
 *
 ******************************************************************************/

#define PLAIN_POP_FIRST_PKT_FROM_QUEUE(pQueue, pPacket)	{	\
        if ((pQueue)->pHead != NULL) {				\
		(pPacket)       = (pQueue)->pHead;		\
		(pQueue)->pHead = (pPacket)->pNext;		\
		if ((pQueue)->pHead == NULL) {			\
			(pQueue)->pTail = NULL;			\
		}						\
		(pPacket)->pNext = NULL;			\
	} else {						\
		(pPacket) = NULL;				\
	}							\
}

#define PLAIN_PUSH_PKT_AS_FIRST_IN_QUEUE(pQueue, pPacket) {	\
	if ((pQueue)->pHead != NULL) {				\
		(pPacket)->pNext = (pQueue)->pHead;		\
	} else {						\
		(pPacket)->pNext = NULL;			\
		(pQueue)->pTail  = (pPacket);			\
	}							\
      	(pQueue)->pHead  = (pPacket);				\
}

#define PLAIN_PUSH_PKT_AS_LAST_IN_QUEUE(pQueue, pPacket) {	\
	(pPacket)->pNext = NULL;				\
	if ((pQueue)->pTail != NULL) {				\
		(pQueue)->pTail->pNext = (pPacket);		\
	} else {						\
		(pQueue)->pHead        = (pPacket);		\
	}							\
	(pQueue)->pTail = (pPacket);				\
}

#define PLAIN_PUSH_MULTIPLE_PKT_AS_LAST_IN_QUEUE(pQueue,pPktGrpStart,pPktGrpEnd) { \
	if ((pPktGrpStart) != NULL) {					\
		if ((pQueue)->pTail != NULL) {				\
			(pQueue)->pTail->pNext = (pPktGrpStart);	\
		} else {						\
			(pQueue)->pHead = (pPktGrpStart);		\
		}							\
		(pQueue)->pTail = (pPktGrpEnd);				\
	}								\
}

#define PLAIN_POP_FIRST_FRAG_FROM_QUEUE(Port, pFrag) {			\
	(pFrag) = pAC->TxPort[(Port)][0].TxFreeFragQueue.pHead;		\
	pAC->TxPort[(Port)][0].TxFreeFragQueue.pHead = (pFrag)->pNext;	\
	if ((pFrag) != NULL) {						\
		(pFrag)->pNext = NULL;					\
	}								\
	if (pAC->TxPort[(Port)][0].TxFreeFragQueue.pHead == NULL) {	\
		pAC->TxPort[(Port)][0].TxFreeFragQueue.pTail = NULL;	\
	}								\
	pAC->TxPort[(Port)][0].TxFreeFragQueue.NumFragsInQueue--;	\
}

#define PLAIN_PUSH_FRAG_AS_LAST_IN_QUEUE(Port, pFrag, pNextFrag) {	\
	(pNextFrag)      = (pFrag)->pNext;				\
	(pFrag)->pNext   = NULL;					\
	(pFrag)->pVirt   = NULL;					\
	(pFrag)->pPhys   = (SK_U64) 0;					\
	(pFrag)->FragLen = 0;						\
	if (pAC->TxPort[(Port)][0].TxFreeFragQueue.pTail != NULL) {	\
		pAC->TxPort[(Port)][0].TxFreeFragQueue.pTail->pNext = (pFrag);	\
	} else {							\
		pAC->TxPort[(Port)][0].TxFreeFragQueue.pHead = (pFrag);	\
	}								\
	pAC->TxPort[(Port)][0].TxFreeFragQueue.pTail = (pFrag);		\
	pAC->TxPort[(Port)][0].TxFreeFragQueue.NumFragsInQueue++;	\
}

/* Required: 'Flags' */ 
#define POP_FIRST_PKT_FROM_QUEUE(pQueue, pPacket)	{	\
	spin_lock_irqsave(&((pQueue)->QueueLock), Flags);	\
	if ((pQueue)->pHead != NULL) {				\
		(pPacket)       = (pQueue)->pHead;		\
		(pQueue)->pHead = (pPacket)->pNext;		\
		if ((pQueue)->pHead == NULL) {			\
			(pQueue)->pTail = NULL;			\
		}						\
		(pPacket)->pNext = NULL;			\
	} else {						\
		(pPacket) = NULL;				\
	}							\
	spin_unlock_irqrestore(&((pQueue)->QueueLock), Flags);	\
}

/* Required: 'Flags' */
#define PUSH_PKT_AS_FIRST_IN_QUEUE(pQueue, pPacket)	{	\
	spin_lock_irqsave(&(pQueue)->QueueLock, Flags);		\
	if ((pQueue)->pHead != NULL) {				\
		(pPacket)->pNext = (pQueue)->pHead;		\
	} else {						\
		(pPacket)->pNext = NULL;			\
		(pQueue)->pTail  = (pPacket);			\
	}							\
	(pQueue)->pHead = (pPacket);				\
	spin_unlock_irqrestore(&(pQueue)->QueueLock, Flags);	\
}

/* Required: 'Flags' */
#define PUSH_PKT_AS_LAST_IN_QUEUE(pQueue, pPacket)	{	\
	(pPacket)->pNext = NULL;				\
	spin_lock_irqsave(&(pQueue)->QueueLock, Flags);		\
	if ((pQueue)->pTail != NULL) {				\
		(pQueue)->pTail->pNext = (pPacket);		\
	} else {						\
		(pQueue)->pHead = (pPacket);			\
	}							\
	(pQueue)->pTail = (pPacket);				\
	spin_unlock_irqrestore(&(pQueue)->QueueLock, Flags);	\
}

/* Required: 'Flags' */
#define PUSH_MULTIPLE_PKT_AS_LAST_IN_QUEUE(pQueue,pPktGrpStart,pPktGrpEnd) {	\
	if ((pPktGrpStart) != NULL) {					\
		spin_lock_irqsave(&(pQueue)->QueueLock, Flags);		\
		if ((pQueue)->pTail != NULL) {				\
			(pQueue)->pTail->pNext = (pPktGrpStart);	\
		} else {						\
			(pQueue)->pHead = (pPktGrpStart);		\
		}							\
		(pQueue)->pTail = (pPktGrpEnd);				\
		spin_unlock_irqrestore(&(pQueue)->QueueLock, Flags);	\
	}								\
}

/* Required: 'Flags' */ 
#define POP_FIRST_FRAG_FROM_QUEUE(Port, pFrag) {						\
	spin_lock_irqsave(&pAC->TxPort[(Port)][0].TxFreeFragQueue.QueueLock, Flags);		\
	(pFrag) = pAC->TxPort[(Port)][0].TxFreeFragQueue.pHead;					\
	pAC->TxPort[(Port)][0].TxFreeFragQueue.pHead = (pFrag)->pNext;				\
	if ((pFrag) != NULL) {									\
		(pFrag)->pNext = NULL;								\
        }											\
	if (pAC->TxPort[(Port)][0].TxFreeFragQueue.pHead == NULL) {				\
		pAC->TxPort[(Port)][0].TxFreeFragQueue.pTail = NULL;				\
	}											\
	pAC->TxPort[(Port)][0].TxFreeFragQueue.NumFragsInQueue--;				\
	spin_unlock_irqrestore(&pAC->TxPort[(Port)][0].TxFreeFragQueue.QueueLock, Flags);	\
}

/* Required: 'Flags' */ 
#define PUSH_FRAG_AS_LAST_IN_QUEUE(Port, pFrag, pNextFrag) {			\
	spin_lock_irqsave(&pAC->TxPort[(Port)][0].TxFreeFragQueue.QueueLock, Flags); \
	(pNextFrag)      = (pFrag)->pNext;						\
	(pFrag)->pNext   = NULL;						\
	(pFrag)->pVirt   = NULL;						\
	(pFrag)->pPhys   = (SK_U64) 0;						\
	(pFrag)->FragLen = 0;							\
	if (pAC->TxPort[(Port)][0].TxFreeFragQueue.pTail != NULL) {		\
		pAC->TxPort[(Port)][0].TxFreeFragQueue.pTail->pNext = (pFrag);	\
	} else {								\
		pAC->TxPort[(Port)][0].TxFreeFragQueue.pHead = (pFrag);		\
	}									\
	pAC->TxPort[(Port)][0].TxFreeFragQueue.pTail = (pFrag);			\
	pAC->TxPort[(Port)][0].TxFreeFragQueue.NumFragsInQueue++;		\
	spin_unlock_irqrestore(&pAC->TxPort[(Port)][0].TxFreeFragQueue.QueueLock, Flags); \
}

/*******************************************************************************
 *
 * Used interrupt bits in the interrupts source register
 *
 ******************************************************************************/

#define DRIVER_IRQS	((IS_IRQ_SW) | \
			 (IS_R1_F)   | (IS_R2_F)  | \
			 (IS_XS1_F)  | (IS_XA1_F) | \
			 (IS_XS2_F)  | (IS_XA2_F))

#define TX_COMPL_IRQS	((IS_XS1_B)  | (IS_XS1_F) | \
			 (IS_XA1_B)  | (IS_XA1_F) | \
			 (IS_XS2_B)  | (IS_XS2_F) | \
			 (IS_XA2_B)  | (IS_XA2_F))

#define NAPI_DRV_IRQS	((IS_R1_F)   | (IS_R2_F) | \
			 (IS_XS1_F)  | (IS_XA1_F)| \
			 (IS_XS2_F)  | (IS_XA2_F))

#define Y2_DRIVER_IRQS	((Y2_IS_STAT_BMU) | (Y2_IS_IRQ_SW) | (Y2_IS_POLL_CHK))

#define SPECIAL_IRQS	((IS_HW_ERR)    |(IS_I2C_READY)  | \
			 (IS_EXT_REG)   |(IS_TIMINT)     | \
			 (IS_PA_TO_RX1) |(IS_PA_TO_RX2)  | \
			 (IS_PA_TO_TX1) |(IS_PA_TO_TX2)  | \
			 (IS_MAC1)      |(IS_LNK_SYNC_M1)| \
			 (IS_MAC2)      |(IS_LNK_SYNC_M2)| \
			 (IS_R1_C)      |(IS_R2_C)       | \
			 (IS_XS1_C)     |(IS_XA1_C)      | \
			 (IS_XS2_C)     |(IS_XA2_C))

#define Y2_SPECIAL_IRQS	((Y2_IS_HW_ERR)   |(Y2_IS_ASF)      | \
			 (Y2_IS_TWSI_RDY) |(Y2_IS_TIMINT)   | \
			 (Y2_IS_IRQ_PHY2) |(Y2_IS_IRQ_MAC2) | \
			 (Y2_IS_CHK_RX2)  |(Y2_IS_CHK_TXS2) | \
			 (Y2_IS_CHK_TXA2) |(Y2_IS_IRQ_PHY1) | \
			 (Y2_IS_IRQ_MAC1) |(Y2_IS_CHK_RX1)  | \
			 (Y2_IS_CHK_TXS1) |(Y2_IS_CHK_TXA1))

#define IRQ_MASK	((IS_IRQ_SW)    | \
			 (IS_R1_F)      |(IS_R2_F)     | \
			 (IS_XS1_F)     |(IS_XA1_F)    | \
			 (IS_XS2_F)     |(IS_XA2_F)    | \
			 (IS_HW_ERR)    |(IS_I2C_READY)| \
			 (IS_EXT_REG)   |(IS_TIMINT)   | \
			 (IS_PA_TO_RX1) |(IS_PA_TO_RX2)| \
			 (IS_PA_TO_TX1) |(IS_PA_TO_TX2)| \
			 (IS_MAC1)      |(IS_MAC2)     | \
			 (IS_R1_C)      |(IS_R2_C)     | \
			 (IS_XS1_C)     |(IS_XA1_C)    | \
			 (IS_XS2_C)     |(IS_XA2_C))

#define Y2_IRQ_MASK	((Y2_DRIVER_IRQS) | (Y2_SPECIAL_IRQS))

#define IRQ_HWE_MASK	(IS_ERR_MSK)		/* enable all HW irqs */
#define Y2_IRQ_HWE_MASK	(Y2_HWE_ALL_MSK)	/* enable all HW irqs */

typedef struct s_DevNet DEV_NET;

struct s_DevNet {
	struct		proc_dir_entry *proc;
	int		PortNr;
	int		NetNr;
	int		Mtu;
	int		Up;
	SK_AC		*pAC;
};  

typedef struct s_TxPort		TX_PORT;

struct s_TxPort {
	/* the transmit descriptor rings */
	caddr_t		pTxDescrRing;	/* descriptor area memory */
	SK_U64		VTxDescrRing;	/* descr. area bus virt. addr. */
	TXD		*pTxdRingHead;	/* Head of Tx rings */
	TXD		*pTxdRingTail;	/* Tail of Tx rings */
	TXD		*pTxdRingPrev;	/* descriptor sent previously */
	int		TxdRingPrevFree;/* previously # of free entrys */
	int		TxdRingFree;	/* # of free entrys */
	spinlock_t	TxDesRingLock;	/* serialize descriptor accesses */
	caddr_t		HwAddr;		/* bmu registers address */
	int		PortIndex;	/* index number of port (0 or 1) */
	SK_PACKET	*TransmitPacketTable;
	SK_FRAG         *TransmitFragmentTable;
	SK_LE_TABLE	TxALET; 	/* tx (async) list element table */
	SK_LE_TABLE	TxSLET; 	/* tx (sync) list element table */
	SK_PKT_QUEUE	TxQ_free;
	SK_PKT_QUEUE	TxAQ_waiting;
	SK_PKT_QUEUE	TxSQ_waiting;
	SK_PKT_QUEUE	TxAQ_working;
	SK_PKT_QUEUE	TxSQ_working;
	SK_FRAG_QUEUE   TxFreeFragQueue;
};

typedef struct s_RxPort		RX_PORT;

struct s_RxPort {
	/* the receive descriptor rings */
	caddr_t		pRxDescrRing;	/* descriptor area memory */
	SK_U64		VRxDescrRing;   /* descr. area bus virt. addr. */
	RXD		*pRxdRingHead;	/* Head of Rx rings */
	RXD		*pRxdRingTail;	/* Tail of Rx rings */
	RXD		*pRxdRingPrev;	/* descriptor given to BMU previously */
	int		RxdRingFree;	/* # of free entrys */
	spinlock_t	RxDesRingLock;	/* serialize descriptor accesses */
	int		RxFillLimit;	/* limit for buffers in ring */
	caddr_t		HwAddr;		/* bmu registers address */
	int		PortIndex;	/* index number of port (0 or 1) */
	SK_FRAG 	*ReceiveFragmentTable;
	SK_PACKET	*ReceivePacketTable;
	SK_LE_TABLE	RxLET; 		/* rx list element table */
	SK_PKT_QUEUE	RxQ_working;
	SK_PKT_QUEUE	RxQ_waiting;
};

/* Definitions needed for interrupt moderation *******************************/

#define IRQ_EOF_AS_TX     ((IS_XA1_F)     | (IS_XA2_F))
#define IRQ_EOF_SY_TX     ((IS_XS1_F)     | (IS_XS2_F))
#define IRQ_MASK_TX_ONLY  ((IRQ_EOF_AS_TX)| (IRQ_EOF_SY_TX))
#define IRQ_MASK_RX_ONLY  ((IS_R1_F)      | (IS_R2_F))
#define IRQ_MASK_SP_ONLY  (SPECIAL_IRQS)
#define IRQ_MASK_TX_RX    ((IRQ_MASK_TX_ONLY)| (IRQ_MASK_RX_ONLY))
#define IRQ_MASK_SP_RX    ((SPECIAL_IRQS)    | (IRQ_MASK_RX_ONLY))
#define IRQ_MASK_SP_TX    ((SPECIAL_IRQS)    | (IRQ_MASK_TX_ONLY))
#define IRQ_MASK_RX_TX_SP ((SPECIAL_IRQS)    | (IRQ_MASK_TX_RX))

#define IRQ_MASK_Y2_TX_ONLY  (Y2_IS_STAT_BMU)
#define IRQ_MASK_Y2_RX_ONLY  (Y2_IS_STAT_BMU)
#define IRQ_MASK_Y2_SP_ONLY  (SPECIAL_IRQS)
#define IRQ_MASK_Y2_TX_RX    ((IRQ_MASK_TX_ONLY)| (IRQ_MASK_RX_ONLY))
#define IRQ_MASK_Y2_SP_RX    ((SPECIAL_IRQS)    | (IRQ_MASK_RX_ONLY))
#define IRQ_MASK_Y2_SP_TX    ((SPECIAL_IRQS)    | (IRQ_MASK_TX_ONLY))
#define IRQ_MASK_Y2_RX_TX_SP ((SPECIAL_IRQS)    | (IRQ_MASK_TX_RX))

#define C_INT_MOD_NONE			1
#define C_INT_MOD_STATIC		2
#define C_INT_MOD_DYNAMIC		4

#define C_CLK_FREQ_GENESIS		 53215000	/* or:  53.125 MHz */
#define C_CLK_FREQ_YUKON		 78215000	/* or:  78.125 MHz */
#define C_CLK_FREQ_YUKON_EC		125000000	/* or: 125.000 MHz */

#define C_Y2_INTS_PER_SEC_DEFAULT	5000 
#define C_INTS_PER_SEC_DEFAULT		2000 
#define C_INT_MOD_ENABLE_PERCENTAGE	50 /* if higher 50% enable */
#define C_INT_MOD_DISABLE_PERCENTAGE	50 /* if lower 50% disable */
#define C_INT_MOD_IPS_LOWER_RANGE	30
#define C_INT_MOD_IPS_UPPER_RANGE	40000


typedef struct s_DynIrqModInfo  DIM_INFO;
struct s_DynIrqModInfo {
	unsigned long	PrevTimeVal;
	unsigned int	PrevSysLoad;
	unsigned int	PrevUsedTime;
	unsigned int	PrevTotalTime;
	int		PrevUsedDescrRatio;
	int		NbrProcessedDescr;
        SK_U64		PrevPort0RxIntrCts;
        SK_U64		PrevPort1RxIntrCts;
        SK_U64		PrevPort0TxIntrCts;
        SK_U64		PrevPort1TxIntrCts;
	SK_BOOL		ModJustEnabled;     /* Moderation just enabled yes/no */

	int		MaxModIntsPerSec;            /* Moderation Threshold */
	int		MaxModIntsPerSecUpperLimit;  /* Upper limit for DIM  */
	int		MaxModIntsPerSecLowerLimit;  /* Lower limit for DIM  */

	long		MaskIrqModeration;   /* ModIrqType (eg. 'TxRx')      */
	SK_BOOL		DisplayStats;        /* Stats yes/no                 */
	SK_BOOL		AutoSizing;          /* Resize DIM-timer on/off      */
	int		IntModTypeSelect;    /* EnableIntMod (eg. 'dynamic') */

	SK_TIMER	ModTimer; /* just some timer */
};

typedef struct s_PerStrm	PER_STRM;

#define SK_ALLOC_IRQ	0x00000001

#define	DIAG_ACTIVE		1
#define	DIAG_NOTACTIVE		0

/****************************************************************************
 * Per board structure / Adapter Context structure:
 *	Allocated within attach(9e) and freed within detach(9e).
 *	Contains all 'per device' necessary handles, flags, locks etc.:
 */
struct s_AC  {
	SK_GEINIT	GIni;		/* GE init struct */
	SK_PNMI		Pnmi;		/* PNMI data struct */
	SK_VPD		vpd;		/* vpd data struct */
	SK_QUEUE	Event;		/* Event queue */
	SK_HWT		Hwt;		/* Hardware Timer control struct */
	SK_TIMCTRL	Tim;		/* Software Timer control struct */
	SK_I2C		I2c;		/* I2C relevant data structure */
	SK_ADDR		Addr;		/* for Address module */
	SK_CSUM		Csum;		/* for checksum module */
	SK_RLMT		Rlmt;		/* for rlmt module */
	spinlock_t	SlowPathLock;	/* Normal IRQ lock */
	SK_PNMI_STRUCT_DATA PnmiStruct;	/* structure to get all Pnmi-Data */
	int			RlmtMode;	/* link check mode to set */
	int			RlmtNets;	/* Number of nets */
	
	SK_IOC		IoBase;		/* register set of adapter */
	int		BoardLevel;	/* level of active hw init (0-2) */
	char		DeviceStr[80];	/* adapter string from vpd */
	SK_U32		AllocFlag;	/* flag allocation of resources */
	struct pci_dev	*PciDev;	/* for access to pci config space */
	SK_U32		PciDevId;	/* pci device id */
	struct SK_NET_DEVICE	*dev[2];	/* pointer to device struct */
	char		Name[30];	/* driver name */
	struct SK_NET_DEVICE	*Next;		/* link all devices (for clearing) */
	int		RxBufSize;	/* length of receive buffers */
	struct net_device_stats stats;	/* linux 'netstat -i' statistics */
	int		Index;		/* internal board index number */

	/* adapter RAM sizes for queues of active port */
	int		RxQueueSize;	/* memory used for receive queue */
	int		TxSQueueSize;	/* memory used for sync. tx queue */
	int		TxAQueueSize;	/* memory used for async. tx queue */

	int		PromiscCount;	/* promiscuous mode counter  */
	int		AllMultiCount;  /* allmulticast mode counter */
	int		MulticCount;	/* number of different MC    */
					/*  addresses for this board */
					/*  (may be more than HW can)*/

	int		HWRevision;	/* Hardware revision */
	int		ActivePort;	/* the active XMAC port */
	int		MaxPorts;		/* number of activated ports */
	int		TxDescrPerRing;	/* # of descriptors per tx ring */
	int		RxDescrPerRing;	/* # of descriptors per rx ring */

	caddr_t		pDescrMem;	/* Pointer to the descriptor area */
	dma_addr_t	pDescrMemDMA;	/* PCI DMA address of area */

	/* the port structures with descriptor rings */
	TX_PORT		TxPort[SK_MAX_MACS][2];
	RX_PORT		RxPort[SK_MAX_MACS];

	/* Yukon2 specific structures and variables start */
	SK_LE_TABLE 	TxListElement[SK_MAX_MACS][2];
	SK_LE_TABLE 	RxListElement[SK_MAX_MACS];
	SK_LE_TABLE 	StatusLETable;		/* Status LETable */
	unsigned	SizeOfAlignedLETables;	/* evaluated size */
	spinlock_t	SetPutIndexLock;	/* put index lock */
	SK_TIMER	TxPollTimer;		/* for TX polling */
	int		MaxUnusedRxLeWorking;
	/* Yukon2 specific config variables stop */

	unsigned int	CsOfs1;		/* for checksum calculation */
	unsigned int	CsOfs2;		/* for checksum calculation */
	SK_U32		CsOfs;		/* for checksum calculation */

	SK_BOOL		CheckQueue;	/* check event queue soon */
	SK_TIMER	DrvCleanupTimer;/* to check for pending descriptors */
	DIM_INFO	DynIrqModInfo;  /* all data related to DIM */

	/* Only for tests */
	int		PortUp;
	int		PortDown;
	int		ChipsetType;	/*  Chipset family type 
					 *  0 == Genesis family support
					 *  1 == Yukon family support
					 */
	SK_U32		DiagModeActive;		/* is diag active?	*/
	SK_BOOL		DiagFlowCtrl;		/* for control purposes	*/
	SK_PNMI_STRUCT_DATA PnmiBackup;		/* backup structure for all Pnmi-Data */
	SK_BOOL		WasIfUp[SK_MAX_MACS];   /* for OpenClose while 
						 * DIAG is busy with NIC 
						 */
};

#endif

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
