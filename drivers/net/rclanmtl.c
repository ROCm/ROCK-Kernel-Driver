/*
** *************************************************************************
**
**
**     R C L A N M T L . C             $Revision: 6 $
**
**
**  RedCreek I2O LAN Message Transport Layer program module.
**
**  ---------------------------------------------------------------------
**  ---     Copyright (c) 1997-1999, RedCreek Communications Inc.     ---
**  ---                   All rights reserved.                        ---
**  ---------------------------------------------------------------------
**
**  File Description:
**
**  Host side I2O (Intelligent I/O) LAN message transport layer.
**
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.

**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.

**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** 1998-1999, LAN API was modified and enhanced by Alice Hennessy.
**
** Sometime in 1997, LAN API was written from scratch by Wendell Nichols.
** *************************************************************************
*/

#undef DEBUG

#define RC_LINUX_MODULE
#include "rclanmtl.h"

#define dprintf kprintf

extern int printk(const char * fmt, ...);

 /* RedCreek LAN device Target ID */
#define RC_LAN_TARGET_ID  0x10 
 /* RedCreek's OSM default LAN receive Initiator */
#define DEFAULT_RECV_INIT_CONTEXT  0xA17  


/*
** I2O message structures
*/

#define    I2O_TID_SZ                                  12
#define    I2O_FUNCTION_SZ                             8

/* Transaction Reply Lists (TRL) Control Word structure */

#define    I2O_TRL_FLAGS_SINGLE_FIXED_LENGTH           0x00
#define    I2O_TRL_FLAGS_SINGLE_VARIABLE_LENGTH        0x40
#define    I2O_TRL_FLAGS_MULTIPLE_FIXED_LENGTH         0x80

/* LAN Class specific functions */

#define    I2O_LAN_PACKET_SEND                         0x3B
#define    I2O_LAN_SDU_SEND                            0x3D
#define    I2O_LAN_RECEIVE_POST                        0x3E
#define    I2O_LAN_RESET                               0x35
#define    I2O_LAN_SHUTDOWN                            0x37

/* Private Class specfic function */
#define    I2O_PRIVATE                                 0xFF

/*  I2O Executive Function Codes.  */

#define    I2O_EXEC_ADAPTER_ASSIGN                     0xB3
#define    I2O_EXEC_ADAPTER_READ                       0xB2
#define    I2O_EXEC_ADAPTER_RELEASE                    0xB5
#define    I2O_EXEC_BIOS_INFO_SET                      0xA5
#define    I2O_EXEC_BOOT_DEVICE_SET                    0xA7
#define    I2O_EXEC_CONFIG_VALIDATE                    0xBB
#define    I2O_EXEC_CONN_SETUP                         0xCA
#define    I2O_EXEC_DEVICE_ASSIGN                      0xB7
#define    I2O_EXEC_DEVICE_RELEASE                     0xB9
#define    I2O_EXEC_HRT_GET                            0xA8
#define    I2O_EXEC_IOP_CLEAR                          0xBE
#define    I2O_EXEC_IOP_CONNECT                        0xC9
#define    I2O_EXEC_IOP_RESET                          0xBD
#define    I2O_EXEC_LCT_NOTIFY                         0xA2
#define    I2O_EXEC_OUTBOUND_INIT                      0xA1
#define    I2O_EXEC_PATH_ENABLE                        0xD3
#define    I2O_EXEC_PATH_QUIESCE                       0xC5
#define    I2O_EXEC_PATH_RESET                         0xD7
#define    I2O_EXEC_STATIC_MF_CREATE                   0xDD
#define    I2O_EXEC_STATIC_MF_RELEASE                  0xDF
#define    I2O_EXEC_STATUS_GET                         0xA0
#define    I2O_EXEC_SW_DOWNLOAD                        0xA9
#define    I2O_EXEC_SW_UPLOAD                          0xAB
#define    I2O_EXEC_SW_REMOVE                          0xAD
#define    I2O_EXEC_SYS_ENABLE                         0xD1
#define    I2O_EXEC_SYS_MODIFY                         0xC1
#define    I2O_EXEC_SYS_QUIESCE                        0xC3
#define    I2O_EXEC_SYS_TAB_SET                        0xA3


 /* Init Outbound Q status */
#define    I2O_EXEC_OUTBOUND_INIT_IN_PROGRESS          0x01
#define    I2O_EXEC_OUTBOUND_INIT_REJECTED             0x02
#define    I2O_EXEC_OUTBOUND_INIT_FAILED               0x03
#define    I2O_EXEC_OUTBOUND_INIT_COMPLETE             0x04


#define    I2O_UTIL_NOP                                0x00


/* I2O Get Status State values */

#define    I2O_IOP_STATE_INITIALIZING                  0x01
#define    I2O_IOP_STATE_RESET                         0x02
#define    I2O_IOP_STATE_HOLD                          0x04
#define    I2O_IOP_STATE_READY                         0x05
#define    I2O_IOP_STATE_OPERATIONAL                   0x08
#define    I2O_IOP_STATE_FAILED                        0x10
#define    I2O_IOP_STATE_FAULTED                       0x11


/* Defines for Request Status Codes:  Table 3-1 Reply Status Codes.  */

#define    I2O_REPLY_STATUS_SUCCESS                    0x00
#define    I2O_REPLY_STATUS_ABORT_DIRTY                0x01
#define    I2O_REPLY_STATUS_ABORT_NO_DATA_TRANSFER     0x02
#define    I2O_REPLY_STATUS_ABORT_PARTIAL_TRANSFER     0x03
#define    I2O_REPLY_STATUS_ERROR_DIRTY                0x04
#define    I2O_REPLY_STATUS_ERROR_NO_DATA_TRANSFER     0x05
#define    I2O_REPLY_STATUS_ERROR_PARTIAL_TRANSFER     0x06
#define    I2O_REPLY_STATUS_PROCESS_ABORT_DIRTY        0x07
#define    I2O_REPLY_STATUS_PROCESS_ABORT_NO_DATA_TRANSFER   0x08
#define    I2O_REPLY_STATUS_PROCESS_ABORT_PARTIAL_TRANSFER   0x09
#define    I2O_REPLY_STATUS_TRANSACTION_ERROR          0x0A
#define    I2O_REPLY_STATUS_PROGRESS_REPORT            0x80


/* DetailedStatusCode defines for ALL messages: Table 3-2 Detailed Status Codes.*/

#define    I2O_DETAIL_STATUS_SUCCESS                        0x0000
#define    I2O_DETAIL_STATUS_BAD_KEY                        0x0001
#define    I2O_DETAIL_STATUS_CHAIN_BUFFER_TOO_LARGE         0x0002
#define    I2O_DETAIL_STATUS_DEVICE_BUSY                    0x0003
#define    I2O_DETAIL_STATUS_DEVICE_LOCKED                  0x0004
#define    I2O_DETAIL_STATUS_DEVICE_NOT_AVAILABLE           0x0005
#define    I2O_DETAIL_STATUS_DEVICE_RESET                   0x0006
#define    I2O_DETAIL_STATUS_INAPPROPRIATE_FUNCTION         0x0007
#define    I2O_DETAIL_STATUS_INSUFFICIENT_RESOURCE_HARD     0x0008
#define    I2O_DETAIL_STATUS_INSUFFICIENT_RESOURCE_SOFT     0x0009
#define    I2O_DETAIL_STATUS_INVALID_INITIATOR_ADDRESS      0x000A
#define    I2O_DETAIL_STATUS_INVALID_MESSAGE_FLAGS          0x000B
#define    I2O_DETAIL_STATUS_INVALID_OFFSET                 0x000C
#define    I2O_DETAIL_STATUS_INVALID_PARAMETER              0x000D
#define    I2O_DETAIL_STATUS_INVALID_REQUEST                0x000E
#define    I2O_DETAIL_STATUS_INVALID_TARGET_ADDRESS         0x000F
#define    I2O_DETAIL_STATUS_MESSAGE_TOO_LARGE              0x0010
#define    I2O_DETAIL_STATUS_MESSAGE_TOO_SMALL              0x0011
#define    I2O_DETAIL_STATUS_MISSING_PARAMETER              0x0012
#define    I2O_DETAIL_STATUS_NO_SUCH_PAGE                   0x0013
#define    I2O_DETAIL_STATUS_REPLY_BUFFER_FULL              0x0014
#define    I2O_DETAIL_STATUS_TCL_ERROR                      0x0015
#define    I2O_DETAIL_STATUS_TIMEOUT                        0x0016
#define    I2O_DETAIL_STATUS_UNKNOWN_ERROR                  0x0017
#define    I2O_DETAIL_STATUS_UNKNOWN_FUNCTION               0x0018
#define    I2O_DETAIL_STATUS_UNSUPPORTED_FUNCTION           0x0019
#define    I2O_DETAIL_STATUS_UNSUPPORTED_VERSION            0x001A

 /* I2O msg header defines for VersionOffset */
#define I2OMSGVER_1_5   0x0001
#define SGL_OFFSET_0    I2OMSGVER_1_5
#define SGL_OFFSET_4    (0x0040 | I2OMSGVER_1_5)
#define TRL_OFFSET_5    (0x0050 | I2OMSGVER_1_5)
#define TRL_OFFSET_6    (0x0060 | I2OMSGVER_1_5)

 /* I2O msg header defines for MsgFlags */
#define MSG_STATIC      0x0100
#define MSG_64BIT_CNTXT 0x0200
#define MSG_MULTI_TRANS 0x1000
#define MSG_FAIL        0x2000
#define MSG_LAST        0x4000
#define MSG_REPLY       0x8000

  /* normal LAN request message MsgFlags and VersionOffset (0x1041) */
#define LAN_MSG_REQST  (MSG_MULTI_TRANS | SGL_OFFSET_4)

 /* minimum size msg */
#define THREE_WORD_MSG_SIZE 0x00030000
#define FOUR_WORD_MSG_SIZE  0x00040000
#define FIVE_WORD_MSG_SIZE  0x00050000
#define SIX_WORD_MSG_SIZE   0x00060000
#define SEVEN_WORD_MSG_SIZE 0x00070000
#define EIGHT_WORD_MSG_SIZE 0x00080000
#define NINE_WORD_MSG_SIZE  0x00090000

/* Special TID Assignments */

#define I2O_IOP_TID   0
#define I2O_HOST_TID  0xB91

 /* RedCreek I2O private message codes */
#define RC_PRIVATE_GET_MAC_ADDR     0x0001/**/ /* OBSOLETE */
#define RC_PRIVATE_SET_MAC_ADDR     0x0002
#define RC_PRIVATE_GET_NIC_STATS    0x0003
#define RC_PRIVATE_GET_LINK_STATUS  0x0004
#define RC_PRIVATE_SET_LINK_SPEED   0x0005
#define RC_PRIVATE_SET_IP_AND_MASK  0x0006
/* #define RC_PRIVATE_GET_IP_AND_MASK  0x0007 */ /* OBSOLETE */
#define RC_PRIVATE_GET_LINK_SPEED   0x0008
#define RC_PRIVATE_GET_FIRMWARE_REV 0x0009
/* #define RC_PRIVATE_GET_MAC_ADDR     0x000A *//**/
#define RC_PRIVATE_GET_IP_AND_MASK  0x000B /**/
#define RC_PRIVATE_DEBUG_MSG        0x000C
#define RC_PRIVATE_REPORT_DRIVER_CAPABILITY  0x000D
#define RC_PRIVATE_SET_PROMISCUOUS_MODE  0x000e
#define RC_PRIVATE_GET_PROMISCUOUS_MODE  0x000f
#define RC_PRIVATE_SET_BROADCAST_MODE    0x0010
#define RC_PRIVATE_GET_BROADCAST_MODE    0x0011

#define RC_PRIVATE_REBOOT           0x00FF


/* I2O message header */
typedef struct _I2O_MESSAGE_FRAME 
{
    U8                          VersionOffset;
    U8                          MsgFlags;
    U16                         MessageSize;
    BF                          TargetAddress:I2O_TID_SZ;
    BF                          InitiatorAddress:I2O_TID_SZ;
    BF                          Function:I2O_FUNCTION_SZ;
    U32                         InitiatorContext;
    /* SGL[] */ 
}
I2O_MESSAGE_FRAME, *PI2O_MESSAGE_FRAME;


 /* assumed a 16K minus 256 byte space for outbound queue message frames */
#define MSG_FRAME_SIZE  512
#define NMBR_MSG_FRAMES 30

/*
**  Message Unit CSR definitions for RedCreek PCI45 board
*/
typedef struct tag_rcatu 
{
    volatile unsigned long APICRegSel;  /* APIC Register Select */
    volatile unsigned long reserved0;
    volatile unsigned long APICWinReg;  /* APIC Window Register */
    volatile unsigned long reserved1;
    volatile unsigned long InMsgReg0;   /* inbound message register 0 */
    volatile unsigned long InMsgReg1;   /* inbound message register 1 */
    volatile unsigned long OutMsgReg0;  /* outbound message register 0 */
    volatile unsigned long OutMsgReg1;  /* outbound message register 1 */
    volatile unsigned long InDoorReg;   /* inbound doorbell register */
    volatile unsigned long InIntStat;   /* inbound interrupt status register */
    volatile unsigned long InIntMask;   /* inbound interrupt mask register */
    volatile unsigned long OutDoorReg;  /* outbound doorbell register */
    volatile unsigned long OutIntStat;  /* outbound interrupt status register */
    volatile unsigned long OutIntMask;  /* outbound interrupt mask register */
    volatile unsigned long reserved2;
    volatile unsigned long reserved3;
    volatile unsigned long InQueue;     /* inbound queue port */
    volatile unsigned long OutQueue;    /* outbound queue port */
    volatile unsigned long reserved4;
    volatile unsigned long reserver5;
    /* RedCreek extension */
    volatile unsigned long EtherMacLow;
    volatile unsigned long EtherMacHi;
    volatile unsigned long IPaddr;
    volatile unsigned long IPmask;
}
ATU, *PATU;

 /* 
 ** typedef PAB
 **
 ** PCI Adapter Block - holds instance specific information and is located
 ** in a reserved space at the start of the message buffer allocated by user.
 */
typedef struct
{
    PATU             p_atu;                /* ptr to  ATU register block */
    PU8              pPci45LinBaseAddr;
    PU8              pLinOutMsgBlock;
    U32              outMsgBlockPhyAddr; 
    PFNTXCALLBACK    pTransCallbackFunc;
    PFNRXCALLBACK    pRecvCallbackFunc;
    PFNCALLBACK      pRebootCallbackFunc;
    PFNCALLBACK      pCallbackFunc;
    U16              IOPState;
    U16              InboundMFrameSize;
}
PAB, *PPAB;

 /* 
 ** in reserved space right after PAB in host memory is area for returning
 ** values from card 
 */

 /* 
 ** Array of pointers to PCI Adapter Blocks.
 ** Indexed by a zero based (0-31) interface number.
 */ 
#define MAX_ADAPTERS 32
static PPAB  PCIAdapterBlock[MAX_ADAPTERS];

/*
** typedef NICSTAT
**
** Data structure for NIC statistics retruned from PCI card.  Data copied from
** here to user allocated RCLINKSTATS (see rclanmtl.h) structure.
*/
typedef struct tag_NicStat 
{
    unsigned long   TX_good; 
    unsigned long   TX_maxcol;
    unsigned long   TX_latecol;
    unsigned long   TX_urun;
    unsigned long   TX_crs;         /* lost carrier sense */
    unsigned long   TX_def;         /* transmit deferred */
    unsigned long   TX_singlecol;   /* single collisions */
    unsigned long   TX_multcol;
    unsigned long   TX_totcol;
    unsigned long   Rcv_good;
    unsigned long   Rcv_CRCerr;
    unsigned long   Rcv_alignerr;
    unsigned long   Rcv_reserr;     /* rnr'd pkts */
    unsigned long   Rcv_orun;
    unsigned long   Rcv_cdt;
    unsigned long   Rcv_runt;
    unsigned long   dump_status;    /* last field directly from the chip */
} 
NICSTAT, *P_NICSTAT;
 

#define DUMP_DONE   0x0000A005      /* completed statistical dump */
#define DUMP_CLEAR  0x0000A007      /* completed stat dump and clear counters */


static volatile int msgFlag;


/* local function prototypes */
static void ProcessOutboundI2OMsg(PPAB pPab, U32 phyMsgAddr);
static int FillI2OMsgSGLFromTCB(PU32 pMsg, PRCTCB pXmitCntrlBlock);
static int GetI2OStatus(PPAB pPab);
static int SendI2OOutboundQInitMsg(PPAB pPab);
static int SendEnableSysMsg(PPAB pPab);


/* 1st 100h bytes of message block is reserved for messenger instance */
#define ADAPTER_BLOCK_RESERVED_SPACE 0x100

/*
** =========================================================================
** RCInitI2OMsgLayer()
**
** Initialize the RedCreek I2O Module and adapter.
**
** Inputs:  AdapterID - interface number from 0 to 15
**          pciBaseAddr - virual base address of PCI (set by BIOS)
**          p_msgbuf - virual address to private message block (min. 16K)
**          p_phymsgbuf - physical address of private message block
**          TransmitCallbackFunction - address of transmit callback function
**          ReceiveCallbackFunction  - address of receive  callback function
**
** private message block is allocated by user.  It must be in locked pages.
** p_msgbuf and p_phymsgbuf point to the same location.  Must be contigous
** memory block of a minimum of 16K byte and long word aligned.
** =========================================================================
*/
RC_RETURN
RCInitI2OMsgLayer(U16 AdapterID, U32 pciBaseAddr, 
                  PU8 p_msgbuf,  PU8 p_phymsgbuf,
                  PFNTXCALLBACK  TransmitCallbackFunction,
                  PFNRXCALLBACK  ReceiveCallbackFunction,
                  PFNCALLBACK    RebootCallbackFunction)
{
    int result;
    PPAB pPab; 
    
#ifdef DEBUG
    kprintf("InitI2O: Adapter:0x%04.4ux ATU:0x%08.8ulx msgbuf:0x%08.8ulx phymsgbuf:0x%08.8ulx\n"
            "TransmitCallbackFunction:0x%08.8ulx  ReceiveCallbackFunction:0x%08.8ulx\n",
            AdapterID, pciBaseAddr, p_msgbuf, p_phymsgbuf, TransmitCallbackFunction, ReceiveCallbackFunction);
#endif /* DEBUG */

    
    /* Check if this interface already initialized - if so, shut it down */
    if (PCIAdapterBlock[AdapterID] != NULL)
    {
        printk("PCIAdapterBlock[%d]!=NULL\n", AdapterID);
//        RCResetLANCard(AdapterID, 0, (PU32)NULL, (PFNCALLBACK)NULL);
        PCIAdapterBlock[AdapterID] = NULL;
    }

    /* 
    ** store adapter instance values in adapter block.
    ** Adapter block is at beginning of message buffer
    */  
    pPab = (PPAB)p_msgbuf;
    
    pPab->p_atu = (PATU)pciBaseAddr;
    pPab->pPci45LinBaseAddr =  (PU8)pciBaseAddr;
    
    /* Set outbound message frame addr - skip over Adapter Block */
    pPab->outMsgBlockPhyAddr = (U32)(p_phymsgbuf + ADAPTER_BLOCK_RESERVED_SPACE);
    pPab->pLinOutMsgBlock    = (PU8)(p_msgbuf + ADAPTER_BLOCK_RESERVED_SPACE);

    /* store callback function addresses */
    pPab->pTransCallbackFunc = TransmitCallbackFunction;
    pPab->pRecvCallbackFunc  = ReceiveCallbackFunction;
    pPab->pRebootCallbackFunc  = RebootCallbackFunction;
    pPab->pCallbackFunc  = (PFNCALLBACK)NULL;

    /*
    ** Initialize I2O IOP
    */
    result = GetI2OStatus(pPab);
    
    if (result != RC_RTN_NO_ERROR)
        return result;

    if (pPab->IOPState == I2O_IOP_STATE_OPERATIONAL)
    {
        printk("pPab->IOPState == op: resetting adapter\n");
        RCResetLANCard(AdapterID, 0, (PU32)NULL, (PFNCALLBACK)NULL);
    }
        
    result = SendI2OOutboundQInitMsg(pPab);
    
    if (result != RC_RTN_NO_ERROR)
        return result;

    result = SendEnableSysMsg(pPab);
   
    if (result != RC_RTN_NO_ERROR)
        return result;
   
    PCIAdapterBlock[AdapterID] = pPab;
    return RC_RTN_NO_ERROR;
}

/*
** =========================================================================
** Disable and Enable I2O interrupts.  I2O interrupts are enabled at Init time
** but can be disabled and re-enabled through these two function calls.
** Packets will still be put into any posted received buffers and packets will
** be sent through RCI2OSendPacket() functions.  Disabling I2O interrupts
** will prevent hardware interrupt to host even though the outbound I2O msg
** queue is not emtpy.
** =========================================================================
*/
#define i960_OUT_POST_Q_INT_BIT        0x0008 /* bit set masks interrupts */

RC_RETURN RCDisableI2OInterrupts(U16 AdapterID)
{
    PPAB pPab;


    pPab = PCIAdapterBlock[AdapterID];
    
    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
        
    pPab->p_atu->OutIntMask |= i960_OUT_POST_Q_INT_BIT;

    return RC_RTN_NO_ERROR;
}

RC_RETURN RCEnableI2OInterrupts(U16 AdapterID)
{
    PPAB pPab;

    pPab = PCIAdapterBlock[AdapterID];
    
    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
    
    pPab->p_atu->OutIntMask &= ~i960_OUT_POST_Q_INT_BIT;
    
    return RC_RTN_NO_ERROR;

}


/*
** =========================================================================
** RCI2OSendPacket()
** =========================================================================
*/
RC_RETURN
RCI2OSendPacket(U16 AdapterID, U32 InitiatorContext, PRCTCB pTransCtrlBlock)
{
    U32 msgOffset;
    PU32 pMsg;
    int size;
    PPAB pPab;

#ifdef DEBUG
    kprintf("RCI2OSendPacket()...\n");
#endif /* DEBUG */
    
    pPab = PCIAdapterBlock[AdapterID];

    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
    
    /* get Inbound free Q entry - reading from In Q gets free Q entry */
    /* offset to Msg Frame in PCI msg block */

    msgOffset = pPab->p_atu->InQueue; 

    if (msgOffset == 0xFFFFFFFF)
    {
#ifdef DEBUG
        kprintf("RCI2OSendPacket(): Inbound Free Q empty!\n");
#endif /* DEBUG */
        return RC_RTN_FREE_Q_EMPTY;
    }
        
    /* calc virual address of msg - virual already mapped to physical */    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + msgOffset);

    size = FillI2OMsgSGLFromTCB(pMsg + 4, pTransCtrlBlock);

    if (size == -1) /* error processing TCB - send NOP msg */
    {
#ifdef DEBUG
        kprintf("RCI2OSendPacket(): Error Rrocess TCB!\n");
#endif /* DEBUG */
        pMsg[0] = THREE_WORD_MSG_SIZE | SGL_OFFSET_0;
        pMsg[1] = I2O_UTIL_NOP << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
        return RC_RTN_TCB_ERROR;
    }
    else /* send over msg header */
    {    
        pMsg[0] = (size + 4) << 16 | LAN_MSG_REQST; /* send over message size and flags */
        pMsg[1] = I2O_LAN_PACKET_SEND << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
        pMsg[2] = InitiatorContext;
        pMsg[3] = 0;  /* batch reply */
        /* post to Inbound Post Q */   
        pPab->p_atu->InQueue = msgOffset;
        return RC_RTN_NO_ERROR;
    }
}
 

/*
** =========================================================================
** RCI2OPostRecvBuffer()
**
** inputs:  pBufrCntrlBlock - pointer to buffer control block
**
** returns TRUE if successful in sending message, else FALSE.
** =========================================================================
*/
RC_RETURN
RCPostRecvBuffers(U16 AdapterID, PRCTCB pTransCtrlBlock)
{
    U32 msgOffset;
    PU32 pMsg;
    int size;
    PPAB pPab;

#ifdef DEBUG
    kprintf("RCPostRecvBuffers()...\n");
#endif /* DEBUG */
    
    /* search for DeviceHandle */
    pPab = PCIAdapterBlock[AdapterID];

    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
    

    /* get Inbound free Q entry - reading from In Q gets free Q entry */
    /* offset to Msg Frame in PCI msg block */
    msgOffset = pPab->p_atu->InQueue; 

    if (msgOffset == 0xFFFFFFFF)
    {
#ifdef DEBUG
        kprintf("RCPostRecvBuffers(): Inbound Free Q empty!\n");
#endif /* DEBUG */
        return RC_RTN_FREE_Q_EMPTY;
   
    }
    /* calc virual address of msg - virual already mapped to physical */    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + msgOffset);

    size = FillI2OMsgSGLFromTCB(pMsg + 4, pTransCtrlBlock);

    if (size == -1) /* error prcessing TCB - send 3 DWORD private msg == NOP */
    {
#ifdef DEBUG
        kprintf("RCPostRecvBuffers(): Error Processing TCB! size = %d\n", size);
#endif /* DEBUG */
        pMsg[0] = THREE_WORD_MSG_SIZE | SGL_OFFSET_0;
        pMsg[1] = I2O_UTIL_NOP << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
        /* post to Post Q */   
        pPab->p_atu->InQueue = msgOffset;
        return RC_RTN_TCB_ERROR;
    }
    else /* send over size msg header */
    {    
        pMsg[0] = (size + 4) << 16 | LAN_MSG_REQST; /* send over message size and flags */
        pMsg[1] = I2O_LAN_RECEIVE_POST << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
        pMsg[2] = DEFAULT_RECV_INIT_CONTEXT;
        pMsg[3] = *(PU32)pTransCtrlBlock; /* number of packet buffers */
        /* post to Post Q */   
        pPab->p_atu->InQueue = msgOffset;
        return RC_RTN_NO_ERROR;
    }
}


/*
** =========================================================================
** RCProcI2OMsgQ()
**
** Process I2O outbound message queue until empty.
** =========================================================================
*/
void 
RCProcI2OMsgQ(U16 AdapterID)
{
    U32 phyAddrMsg;
    PU8 p8Msg;
    PU32 p32;
    U16 count;
    PPAB pPab;
    unsigned char debug_msg[20];
    
    pPab = PCIAdapterBlock[AdapterID];
    
    if (pPab == NULL)
        return;
    
    phyAddrMsg = pPab->p_atu->OutQueue;

    while (phyAddrMsg != 0xFFFFFFFF)
    {
        p8Msg = pPab->pLinOutMsgBlock + (phyAddrMsg - pPab->outMsgBlockPhyAddr);
        p32 = (PU32)p8Msg;
        
        //printk(" msg: 0x%x  0x%x \n", p8Msg[7], p32[5]);

        /* 
        ** Send Packet Reply Msg
        */
        if (I2O_LAN_PACKET_SEND == p8Msg[7])  /* function code byte */
        {
            count = *(PU16)(p8Msg+2);
            count -= p8Msg[0] >> 4;
            /* status, count, context[], adapter */
            (*pPab->pTransCallbackFunc)(p8Msg[19], count, p32+5, AdapterID);
        }             
        /* 
        ** Receive Packet Reply Msg */
        else if (I2O_LAN_RECEIVE_POST == p8Msg[7])
        {
#ifdef DEBUG    
            kprintf("I2O_RECV_REPLY pPab:0x%08.8ulx p8Msg:0x%08.8ulx p32:0x%08.8ulx\n", pPab, p8Msg, p32);
            kprintf("msg: 0x%08.8ulx:0x%08.8ulx:0x%08.8ulx:0x%08.8ulx\n",
                    p32[0], p32[1], p32[2], p32[3]);
            kprintf("     0x%08.8ulx:0x%08.8ulx:0x%08.8ulx:0x%08.8ulx\n",
                    p32[4], p32[5], p32[6], p32[7]);
            kprintf("     0x%08.8ulx:0X%08.8ulx:0x%08.8ulx:0x%08.8ulx\n",
                    p32[8], p32[9], p32[10], p32[11]);
#endif
            /*  status, count, buckets remaining, packetParmBlock, adapter */
            (*pPab->pRecvCallbackFunc)(p8Msg[19], p8Msg[12], p32[5], p32+6, AdapterID);
      
      
        }
        else if (I2O_LAN_RESET == p8Msg[7] || I2O_LAN_SHUTDOWN == p8Msg[7])
        {
            if (pPab->pCallbackFunc)
            {
                (*pPab->pCallbackFunc)(p8Msg[19],0,0,AdapterID);
            }
            else
            {
                pPab->pCallbackFunc = (PFNCALLBACK) 1;
            }
            //PCIAdapterBlock[AdapterID] = 0;
        }
        else if (I2O_PRIVATE == p8Msg[7])
        {
            //printk("i2o private 0x%x, 0x%x \n", p8Msg[7], p32[5]);
            switch (p32[5])
            {
            case RC_PRIVATE_DEBUG_MSG:
                msgFlag = 1;
                /*printk("Received I2O_PRIVATE msg\n");*/
                debug_msg[15]  = (p32[6]&0xff000000) >> 24;
                debug_msg[14]  = (p32[6]&0x00ff0000) >> 16;
                debug_msg[13]  = (p32[6]&0x0000ff00) >> 8;
                debug_msg[12]  = (p32[6]&0x000000ff);

                debug_msg[11]  = (p32[7]&0xff000000) >> 24;
                debug_msg[10]  = (p32[7]&0x00ff0000) >> 16;
                debug_msg[ 9]  = (p32[7]&0x0000ff00) >> 8;
                debug_msg[ 8]  = (p32[7]&0x000000ff);

                debug_msg[ 7]  = (p32[8]&0xff000000) >> 24;
                debug_msg[ 6]  = (p32[8]&0x00ff0000) >> 16;
                debug_msg[ 5] = (p32[8]&0x0000ff00) >> 8;
                debug_msg[ 4] = (p32[8]&0x000000ff);

                debug_msg[ 3] = (p32[9]&0xff000000) >> 24;
                debug_msg[ 2] = (p32[9]&0x00ff0000) >> 16;
                debug_msg[ 1] = (p32[9]&0x0000ff00) >> 8;
                debug_msg[ 0] = (p32[9]&0x000000ff);

                debug_msg[16] = '\0';
                printk (debug_msg);
                break;
            case RC_PRIVATE_REBOOT:
                printk("Adapter reboot initiated...\n");
                if (pPab->pRebootCallbackFunc)
                {
                    (*pPab->pRebootCallbackFunc)(0,0,0,AdapterID);
                }
                break;
            default:
                printk("Unknown private I2O msg received: 0x%lx\n",
                       p32[5]);
                break;
            }
        }

        /* 
        ** Process other Msg's
        */
        else
        {
            ProcessOutboundI2OMsg(pPab, phyAddrMsg);
        }
        
        /* return MFA to outbound free Q*/
        pPab->p_atu->OutQueue = phyAddrMsg;
    
        /* any more msgs? */
        phyAddrMsg = pPab->p_atu->OutQueue;
    }
}


/*
** =========================================================================
**  Returns LAN interface statistical counters to space provided by caller at
**  StatsReturnAddr.  Returns 0 if success, else RC_RETURN code.
**  This function will call the WaitCallback function provided by
**  user while waiting for card to respond.
** =========================================================================
*/
RC_RETURN
RCGetLinkStatistics(U16 AdapterID, 
                    P_RCLINKSTATS StatsReturnAddr,
                    PFNWAITCALLBACK WaitCallback)
{
    U32 msgOffset;
    volatile U32 timeout;
    volatile PU32 pMsg;
    volatile PU32 p32, pReturnAddr;
    P_NICSTAT pStats;
    int i;
    PPAB pPab;

/*kprintf("Get82558Stats() StatsReturnAddr:0x%08.8ulx\n", StatsReturnAddr);*/

    pPab = PCIAdapterBlock[AdapterID];

    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
    
    msgOffset = pPab->p_atu->InQueue;

    if (msgOffset == 0xFFFFFFFF)
    {
#ifdef DEBUG
        kprintf("Get8255XStats(): Inbound Free Q empty!\n");
#endif
        return RC_RTN_FREE_Q_EMPTY;
    }

    /* calc virual address of msg - virual already mapped to physical */
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + msgOffset);

/*dprintf("Get82558Stats - pMsg = 0x%08ulx, InQ msgOffset = 0x%08ulx\n", pMsg, msgOffset);*/
/*dprintf("Get82558Stats - pMsg = 0x%08X, InQ msgOffset = 0x%08X\n", pMsg, msgOffset);*/

    pMsg[0] = SIX_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = DEFAULT_RECV_INIT_CONTEXT;
    pMsg[3] = 0x112; /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_GET_NIC_STATS;
    pMsg[5] = pPab->outMsgBlockPhyAddr - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB); 

    p32 = (PU32)(pPab->pLinOutMsgBlock - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB));

    pStats = (P_NICSTAT)p32;
    pStats->dump_status = 0xFFFFFFFF;

    /* post to Inbound Post Q */
    pPab->p_atu->InQueue = msgOffset;

    timeout = 100000;
    while (1)
    {
        if (WaitCallback)
            (*WaitCallback)();
        
        for (i = 0; i < 1000; i++)
            ;

        if (pStats->dump_status != 0xFFFFFFFF)
            break;

        if (!timeout--)
        {
#ifdef DEBUG
            kprintf("RCGet82558Stats() Timeout waiting for NIC statistics\n");
#endif
            return RC_RTN_MSG_REPLY_TIMEOUT;
        }
    }
    
    pReturnAddr = (PU32)StatsReturnAddr;
    
    /* copy Nic stats to user's structure */
    for (i = 0; i < (int) sizeof(RCLINKSTATS) / 4; i++)
        pReturnAddr[i] = p32[i];
   
    return RC_RTN_NO_ERROR;     
}


/*
** =========================================================================
** Get82558LinkStatus()
** =========================================================================
*/
RC_RETURN
RCGetLinkStatus(U16 AdapterID, PU32 ReturnAddr, PFNWAITCALLBACK WaitCallback)
{
    U32 msgOffset;
    volatile U32 timeout;
    volatile PU32 pMsg;
    volatile PU32 p32;
    PPAB pPab;

/*kprintf("Get82558LinkStatus() ReturnPhysAddr:0x%08.8ulx\n", ReturnAddr);*/

    pPab = PCIAdapterBlock[AdapterID];

    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
    
    msgOffset = pPab->p_atu->InQueue;

    if (msgOffset == 0xFFFFFFFF)
    {
#ifdef DEBUG
        dprintf("Get82558LinkStatus(): Inbound Free Q empty!\n");
#endif
        return RC_RTN_FREE_Q_EMPTY;
    }

    /* calc virual address of msg - virual already mapped to physical */
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + msgOffset);
/*dprintf("Get82558LinkStatus - pMsg = 0x%08ulx, InQ msgOffset = 0x%08ulx\n", pMsg, msgOffset);*/
/*dprintf("Get82558LinkStatus - pMsg = 0x%08X, InQ msgOffset = 0x%08X\n", pMsg, msgOffset);*/

    pMsg[0] = SIX_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = DEFAULT_RECV_INIT_CONTEXT;
    pMsg[3] = 0x112; /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_GET_LINK_STATUS;
    pMsg[5] = pPab->outMsgBlockPhyAddr - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB); 

    p32 = (PU32)(pPab->pLinOutMsgBlock - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB));
    *p32 = 0xFFFFFFFF;
    
    /* post to Inbound Post Q */
    pPab->p_atu->InQueue = msgOffset;

    timeout = 100000;
    while (1)
    {
        U32 i;

        if (WaitCallback)
            (*WaitCallback)();
        
        for (i = 0; i < 1000; i++)
            ;

        if (*p32 != 0xFFFFFFFF)
            break;

        if (!timeout--)
        {
#ifdef DEBUG
            kprintf("Timeout waiting for link status\n");
#endif    
            return RC_RTN_MSG_REPLY_TIMEOUT;
        }
    }
    
    *ReturnAddr = *p32; /* 1 = up 0 = down */
    
    return RC_RTN_NO_ERROR;
    
}

/*
** =========================================================================
** RCGetMAC()
**
** get the MAC address the adapter is listening for in non-promiscous mode.
** MAC address is in media format.
** =========================================================================
*/
RC_RETURN
RCGetMAC(U16 AdapterID, PU8 mac, PFNWAITCALLBACK WaitCallback)
{
    unsigned i, timeout;
    U32      off;
    PU32     p;
    U32      temp[2];
    PPAB     pPab;
    PATU     p_atu;
    
    pPab = PCIAdapterBlock[AdapterID];
    
    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
    
    p_atu = pPab->p_atu;

    p_atu->EtherMacLow = 0;     /* first zero return data */
    p_atu->EtherMacHi = 0;
    
    off = p_atu->InQueue;   /* get addresss of message */
 
    if (0xFFFFFFFF == off)
        return RC_RTN_FREE_Q_EMPTY;

    p = (PU32)(pPab->pPci45LinBaseAddr + off);

#ifdef RCDEBUG
    printk("RCGetMAC: p_atu 0x%08x, off 0x%08x, p 0x%08x\n", 
           (uint)p_atu, (uint)off, (uint)p);
#endif /* RCDEBUG */
    /* setup private message */
    p[0] = FIVE_WORD_MSG_SIZE | SGL_OFFSET_0;
    p[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    p[2] = 0;               /* initiator context */
    p[3] = 0x218;           /* transaction context */
    p[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_GET_MAC_ADDR;


    p_atu->InQueue = off;   /* send it to the I2O device */
#ifdef RCDEBUG
    printk("RCGetMAC: p_atu 0x%08x, off 0x%08x, p 0x%08x\n", 
           (uint)p_atu, (uint)off, (uint)p);
#endif /* RCDEBUG */

    /* wait for the rcpci45 board to update the info */
    timeout = 1000000;
    while (0 == p_atu->EtherMacLow) 
    {
        if (WaitCallback)
            (*WaitCallback)();
    
        for (i = 0; i < 1000; i++)
            ;

        if (!timeout--)
        {
            printk("rc_getmac: Timeout\n");
            return RC_RTN_MSG_REPLY_TIMEOUT;
        }
    }
    
    /* read the mac address  */
    temp[0] = p_atu->EtherMacLow;
    temp[1] = p_atu->EtherMacHi;
    memcpy((char *)mac, (char *)temp, 6);


#ifdef RCDEBUG
//    printk("rc_getmac: 0x%X\n", ptr);
#endif /* RCDEBUG */

    return RC_RTN_NO_ERROR;
}

 
/*
** =========================================================================
** RCSetMAC()
**
** set MAC address the adapter is listening for in non-promiscous mode.
** MAC address is in media format.
** =========================================================================
*/
RC_RETURN
RCSetMAC(U16 AdapterID, PU8 mac)
{
    U32  off;
    PU32 pMsg;
    PPAB pPab;


    pPab = PCIAdapterBlock[AdapterID];
    
    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
    
    off = pPab->p_atu->InQueue; /* get addresss of message */
    
    if (0xFFFFFFFF == off)
        return RC_RTN_FREE_Q_EMPTY;
    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + off);

    /* setup private message */
    pMsg[0] = SEVEN_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = 0;                 /* initiator context */
    pMsg[3] = 0x219;             /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_SET_MAC_ADDR;
    pMsg[5] = *(unsigned *)mac;  /* first four bytes */
    pMsg[6] = *(unsigned *)(mac + 4); /* last two bytes */
    
    pPab->p_atu->InQueue = off;   /* send it to the I2O device */

    return RC_RTN_NO_ERROR ;
}


/*
** =========================================================================
** RCSetLinkSpeed()
**
** set ethernet link speed. 
** input: speedControl - determines action to take as follows
**          0 = reset and auto-negotiate (NWay)
**          1 = Full Duplex 100BaseT
**          2 = Half duplex 100BaseT
**          3 = Full Duplex  10BaseT
**          4 = Half duplex  10BaseT
**          all other values are ignore (do nothing)
** =========================================================================
*/
RC_RETURN
RCSetLinkSpeed(U16 AdapterID, U16 LinkSpeedCode)
{
    U32  off;
    PU32 pMsg;
    PPAB pPab;

    
    pPab =PCIAdapterBlock[AdapterID];
     
    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
    
    off = pPab->p_atu->InQueue; /* get addresss of message */
    
    if (0xFFFFFFFF == off)
        return RC_RTN_FREE_Q_EMPTY;
    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + off);

    /* setup private message */
    pMsg[0] = SIX_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = 0;                 /* initiator context */
    pMsg[3] = 0x219;             /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_SET_LINK_SPEED;
    pMsg[5] = LinkSpeedCode;     /* link speed code */

    pPab->p_atu->InQueue = off;   /* send it to the I2O device */

    return RC_RTN_NO_ERROR ;
}
/*
** =========================================================================
** RCSetPromiscuousMode()
**
** Defined values for Mode:
**  0 - turn off promiscuous mode
**  1 - turn on  promiscuous mode
**
** =========================================================================
*/
RC_RETURN
RCSetPromiscuousMode(U16 AdapterID, U16 Mode)
{
    U32  off;
    PU32 pMsg;
    PPAB pPab;
    
    pPab =PCIAdapterBlock[AdapterID];
     
    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
    
    off = pPab->p_atu->InQueue; /* get addresss of message */
    
    if (0xFFFFFFFF == off)
        return RC_RTN_FREE_Q_EMPTY;
    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + off);

    /* setup private message */
    pMsg[0] = SIX_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = 0;                 /* initiator context */
    pMsg[3] = 0x219;             /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_SET_PROMISCUOUS_MODE;
    pMsg[5] = Mode;     /* promiscuous mode setting */

    pPab->p_atu->InQueue = off;   /* send it to the device */

    return RC_RTN_NO_ERROR ;
}
/*
** =========================================================================
** RCGetPromiscuousMode()
**
** get promiscuous mode setting
**
** Possible return values placed in pMode:
**  0 = promisuous mode not set
**  1 = promisuous mode is set
**
** =========================================================================
*/
RC_RETURN
RCGetPromiscuousMode(U16 AdapterID, PU32 pMode, PFNWAITCALLBACK WaitCallback)
{
    U32 msgOffset, timeout;
    PU32 pMsg;
    volatile PU32 p32;
    PPAB pPab;

    pPab =PCIAdapterBlock[AdapterID];


    msgOffset = pPab->p_atu->InQueue;


    if (msgOffset == 0xFFFFFFFF)
    {
        kprintf("RCGetLinkSpeed(): Inbound Free Q empty!\n");
        return RC_RTN_FREE_Q_EMPTY;
    }

    /* calc virtual address of msg - virtual already mapped to physical */    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + msgOffset);

    /* virtual pointer to return buffer - clear first two dwords */
    p32 = (volatile PU32)(pPab->pLinOutMsgBlock - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB));
    p32[0] = 0xff;

    /* setup private message */
    pMsg[0] = SIX_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = 0;                 /* initiator context */
    pMsg[3] = 0x219;             /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_GET_PROMISCUOUS_MODE;
    /* phys address to return status - area right after PAB */
    pMsg[5] = pPab->outMsgBlockPhyAddr - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB); 
   
    /* post to Inbound Post Q */   

    pPab->p_atu->InQueue = msgOffset;

    /* wait for response */
    timeout = 1000000;
    while(1)
    {
        int i;
        
        if (WaitCallback)
            (*WaitCallback)();

        for (i = 0; i < 1000; i++)      /* please don't hog the bus!!! */
            ;
            
        if (p32[0] != 0xff)
            break;
            
        if (!timeout--)
        {
            kprintf("Timeout waiting for promiscuous mode from adapter\n");
            kprintf("0x%8.8lx\n", p32[0]);
            return RC_RTN_NO_LINK_SPEED;
        }
    }

    /* get mode */
    *pMode = (U8)((volatile PU8)p32)[0] & 0x0f;

    return RC_RTN_NO_ERROR;
}
/*
** =========================================================================
** RCSetBroadcastMode()
**
** Defined values for Mode:
**  0 - turn off promiscuous mode
**  1 - turn on  promiscuous mode
**
** =========================================================================
*/
RC_RETURN
RCSetBroadcastMode(U16 AdapterID, U16 Mode)
{
    U32  off;
    PU32 pMsg;
    PPAB pPab;
    
    pPab =PCIAdapterBlock[AdapterID];
     
    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
    
    off = pPab->p_atu->InQueue; /* get addresss of message */
    
    if (0xFFFFFFFF == off)
        return RC_RTN_FREE_Q_EMPTY;
    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + off);

    /* setup private message */
    pMsg[0] = SIX_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = 0;                 /* initiator context */
    pMsg[3] = 0x219;             /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_SET_BROADCAST_MODE;
    pMsg[5] = Mode;     /* promiscuous mode setting */

    pPab->p_atu->InQueue = off;   /* send it to the device */

    return RC_RTN_NO_ERROR ;
}
/*
** =========================================================================
** RCGetBroadcastMode()
**
** get promiscuous mode setting
**
** Possible return values placed in pMode:
**  0 = promisuous mode not set
**  1 = promisuous mode is set
**
** =========================================================================
*/
RC_RETURN
RCGetBroadcastMode(U16 AdapterID, PU32 pMode, PFNWAITCALLBACK WaitCallback)
{
    U32 msgOffset, timeout;
    PU32 pMsg;
    volatile PU32 p32;
    PPAB pPab;

    pPab =PCIAdapterBlock[AdapterID];


    msgOffset = pPab->p_atu->InQueue;


    if (msgOffset == 0xFFFFFFFF)
    {
        kprintf("RCGetLinkSpeed(): Inbound Free Q empty!\n");
        return RC_RTN_FREE_Q_EMPTY;
    }

    /* calc virtual address of msg - virtual already mapped to physical */    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + msgOffset);

    /* virtual pointer to return buffer - clear first two dwords */
    p32 = (volatile PU32)(pPab->pLinOutMsgBlock - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB));
    p32[0] = 0xff;

    /* setup private message */
    pMsg[0] = SIX_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = 0;                 /* initiator context */
    pMsg[3] = 0x219;             /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_GET_BROADCAST_MODE;
    /* phys address to return status - area right after PAB */
    pMsg[5] = pPab->outMsgBlockPhyAddr - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB); 
   
    /* post to Inbound Post Q */   

    pPab->p_atu->InQueue = msgOffset;

    /* wait for response */
    timeout = 1000000;
    while(1)
    {
        int i;
        
        if (WaitCallback)
            (*WaitCallback)();

        for (i = 0; i < 1000; i++)      /* please don't hog the bus!!! */
            ;
            
        if (p32[0] != 0xff)
            break;
            
        if (!timeout--)
        {
            kprintf("Timeout waiting for promiscuous mode from adapter\n");
            kprintf("0x%8.8lx\n", p32[0]);
            return RC_RTN_NO_LINK_SPEED;
        }
    }

    /* get mode */
    *pMode = (U8)((volatile PU8)p32)[0] & 0x0f;

    return RC_RTN_NO_ERROR;
}

/*
** =========================================================================
** RCGetLinkSpeed()
**
** get ethernet link speed. 
**
** 0 = Unknown
** 1 = Full Duplex 100BaseT
** 2 = Half duplex 100BaseT
** 3 = Full Duplex  10BaseT
** 4 = Half duplex  10BaseT
**
** =========================================================================
*/
RC_RETURN
RCGetLinkSpeed(U16 AdapterID, PU32 pLinkSpeedCode, PFNWAITCALLBACK WaitCallback)
{
    U32 msgOffset, timeout;
    PU32 pMsg;
    volatile PU32 p32;
    U8 IOPLinkSpeed;
    PPAB pPab;

    pPab =PCIAdapterBlock[AdapterID];


    msgOffset = pPab->p_atu->InQueue;


    if (msgOffset == 0xFFFFFFFF)
    {
        kprintf("RCGetLinkSpeed(): Inbound Free Q empty!\n");
        return RC_RTN_FREE_Q_EMPTY;
    }

    /* calc virtual address of msg - virtual already mapped to physical */    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + msgOffset);

    /* virtual pointer to return buffer - clear first two dwords */
    p32 = (volatile PU32)(pPab->pLinOutMsgBlock - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB));
    p32[0] = 0xff;

    /* setup private message */
    pMsg[0] = SIX_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = 0;                 /* initiator context */
    pMsg[3] = 0x219;             /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_GET_LINK_SPEED;
    /* phys address to return status - area right after PAB */
    pMsg[5] = pPab->outMsgBlockPhyAddr - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB); 
   
    /* post to Inbound Post Q */   

    pPab->p_atu->InQueue = msgOffset;

    /* wait for response */
    timeout = 1000000;
    while(1)
    {
        int i;
        
        if (WaitCallback)
            (*WaitCallback)();

        for (i = 0; i < 1000; i++)      /* please don't hog the bus!!! */
            ;
            
        if (p32[0] != 0xff)
            break;
            
        if (!timeout--)
        {
            kprintf("Timeout waiting for link speed from IOP\n");
            kprintf("0x%8.8lx\n", p32[0]);
            return RC_RTN_NO_LINK_SPEED;
        }
    }

    /* get Link speed */
    IOPLinkSpeed = (U8)((volatile PU8)p32)[0] & 0x0f;

    *pLinkSpeedCode= IOPLinkSpeed;

    return RC_RTN_NO_ERROR;
}

/*
** =========================================================================
** RCReportDriverCapability(U16 AdapterID, U32 capability)
**
** Currently defined bits:
** WARM_REBOOT_CAPABLE   0x01
**
** =========================================================================
*/
RC_RETURN
RCReportDriverCapability(U16 AdapterID, U32 capability)
{
    U32  off;
    PU32 pMsg;
    PPAB pPab;

    pPab =PCIAdapterBlock[AdapterID];
     
    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
    
    off = pPab->p_atu->InQueue; /* get addresss of message */
    
    if (0xFFFFFFFF == off)
        return RC_RTN_FREE_Q_EMPTY;
    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + off);

    /* setup private message */
    pMsg[0] = SIX_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = 0;                 /* initiator context */
    pMsg[3] = 0x219;             /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_REPORT_DRIVER_CAPABILITY;
    pMsg[5] = capability;

    pPab->p_atu->InQueue = off;   /* send it to the I2O device */

    return RC_RTN_NO_ERROR ;
}

/*
** =========================================================================
** RCGetFirmwareVer()
**
** Return firmware version in the form "SoftwareVersion : Bt BootVersion"
**
** =========================================================================
*/
RC_RETURN
RCGetFirmwareVer(U16 AdapterID, PU8 pFirmString, PFNWAITCALLBACK WaitCallback)
{
    U32 msgOffset, timeout;
    PU32 pMsg;
    volatile PU32 p32;
    PPAB pPab;

    pPab =PCIAdapterBlock[AdapterID];

    msgOffset = pPab->p_atu->InQueue;


    if (msgOffset == 0xFFFFFFFF)
    {
        kprintf("RCGetFirmwareVer(): Inbound Free Q empty!\n");
        return RC_RTN_FREE_Q_EMPTY;
    }

    /* calc virtual address of msg - virtual already mapped to physical */    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + msgOffset);

    /* virtual pointer to return buffer - clear first two dwords */
    p32 = (volatile PU32)(pPab->pLinOutMsgBlock - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB));
    p32[0] = 0xff;

    /* setup private message */
    pMsg[0] = SIX_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = 0;                 /* initiator context */
    pMsg[3] = 0x219;             /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_GET_FIRMWARE_REV;
    /* phys address to return status - area right after PAB */
    pMsg[5] = pPab->outMsgBlockPhyAddr - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB); 


   
    /* post to Inbound Post Q */   

    pPab->p_atu->InQueue = msgOffset;

    
    /* wait for response */
    timeout = 1000000;
    while(1)
    {
        int i;
        
        if (WaitCallback)
            (*WaitCallback)();
            
        for (i = 0; i < 1000; i++)      /* please don't hog the bus!!! */
            ;

        if (p32[0] != 0xff)
            break;
            
        if (!timeout--)
        {
            kprintf("Timeout waiting for link speed from IOP\n");
            return RC_RTN_NO_FIRM_VER;
        }
    }

    strcpy(pFirmString, (PU8)p32);
    return RC_RTN_NO_ERROR;
}

/*
** =========================================================================
** RCResetLANCard()
**
** ResourceFlags indicates whether to return buffer resource explicitly
** to host or keep and reuse.
** CallbackFunction (if not NULL) is the function to be called when 
** reset is complete.
** If CallbackFunction is NULL, ReturnAddr will have a 1 placed in it when
** reset is done (if not NULL).
**
** =========================================================================
*/
RC_RETURN 
RCResetLANCard(U16 AdapterID, U16 ResourceFlags, PU32 ReturnAddr, PFNCALLBACK CallbackFunction)
{
    unsigned long off;
    PU32 pMsg;
    PPAB pPab;
    int i;
    long timeout = 0;

    
    pPab =PCIAdapterBlock[AdapterID];
     
    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;

    off = pPab->p_atu->InQueue; /* get addresss of message */
    
    if (0xFFFFFFFF == off)
        return RC_RTN_FREE_Q_EMPTY;
    
    pPab->pCallbackFunc = CallbackFunction;

    pMsg = (PU32)(pPab->pPci45LinBaseAddr + off);

    /* setup message */
    pMsg[0] = FOUR_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_LAN_RESET << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = DEFAULT_RECV_INIT_CONTEXT;
    pMsg[3] = ResourceFlags << 16;   /* resource flags */

    pPab->p_atu->InQueue = off;   /* send it to the I2O device */

    if (CallbackFunction == (PFNCALLBACK)NULL)
    {
        /* call RCProcI2OMsgQ() until something in pPab->pCallbackFunc
           or until timer goes off */
        while (pPab->pCallbackFunc == (PFNCALLBACK)NULL)
        {
            RCProcI2OMsgQ(AdapterID);
            for (i = 0; i < 100000; i++)     /* please don't hog the bus!!! */
                ;
            timeout++;
            if (timeout > 10000)
            {
                break;
            }
        }
        if (ReturnAddr != (PU32)NULL)
            *ReturnAddr = (U32)pPab->pCallbackFunc;
    }

    return RC_RTN_NO_ERROR ;
}
/*
** =========================================================================
** RCResetIOP()
**
** Send StatusGet Msg, wait for results return directly to buffer.
**
** =========================================================================
*/
RC_RETURN 
RCResetIOP(U16 AdapterID)
{
    U32 msgOffset, timeout;
    PU32 pMsg;
    PPAB pPab;
    volatile PU32 p32;
    
    pPab = PCIAdapterBlock[AdapterID];
    msgOffset = pPab->p_atu->InQueue;

    if (msgOffset == 0xFFFFFFFF)
    {
        return RC_RTN_FREE_Q_EMPTY;
    }

    /* calc virtual address of msg - virtual already mapped to physical */    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + msgOffset);

    pMsg[0] = NINE_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_EXEC_IOP_RESET << 24 | I2O_HOST_TID << 12 | I2O_IOP_TID;
    pMsg[2] = 0; /* universal context */
    pMsg[3] = 0; /* universal context */
    pMsg[4] = 0; /* universal context */
    pMsg[5] = 0; /* universal context */
    /* phys address to return status - area right after PAB */
    pMsg[6] = pPab->outMsgBlockPhyAddr - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB); 
    pMsg[7] = 0;
    pMsg[8] = 1;  /*  return 1 byte */

    /* virual pointer to return buffer - clear first two dwords */
    p32 = (volatile PU32)(pPab->pLinOutMsgBlock - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB));
    p32[0] = 0;
    p32[1] = 0;

    /* post to Inbound Post Q */   

    pPab->p_atu->InQueue = msgOffset;

    /* wait for response */
    timeout = 1000000;
    while(1)
    {
        int i;
        
        for (i = 0; i < 1000; i++)      /* please don't hog the bus!!! */
            ;
            
        if (p32[0] || p32[1])
            break;
            
        if (!timeout--)
        {
            printk("RCResetIOP timeout\n");
            return RC_RTN_MSG_REPLY_TIMEOUT;
        }
    }
    return RC_RTN_NO_ERROR;
}

/*
** =========================================================================
** RCShutdownLANCard()
**
** ResourceFlags indicates whether to return buffer resource explicitly
** to host or keep and reuse.
** CallbackFunction (if not NULL) is the function to be called when 
** shutdown is complete.
** If CallbackFunction is NULL, ReturnAddr will have a 1 placed in it when
** shutdown is done (if not NULL).
**
** =========================================================================
*/
RC_RETURN 
RCShutdownLANCard(U16 AdapterID, U16 ResourceFlags, PU32 ReturnAddr, PFNCALLBACK CallbackFunction)
{
    volatile PU32 pMsg;
    U32 off;
    PPAB pPab;
    int i;
    long timeout = 0;

    pPab = PCIAdapterBlock[AdapterID];

    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;

    off = pPab->p_atu->InQueue; /* get addresss of message */
    
    if (0xFFFFFFFF == off)
        return RC_RTN_FREE_Q_EMPTY;

    pPab->pCallbackFunc = CallbackFunction;
    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + off);

    /* setup message */
    pMsg[0] = FOUR_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_LAN_SHUTDOWN << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = DEFAULT_RECV_INIT_CONTEXT;
    pMsg[3] = ResourceFlags << 16;   /* resource flags */

    pPab->p_atu->InQueue = off;   /* send it to the I2O device */

    if (CallbackFunction == (PFNCALLBACK)NULL)
    {
        /* call RCProcI2OMsgQ() until something in pPab->pCallbackFunc
           or until timer goes off */
        while (pPab->pCallbackFunc == (PFNCALLBACK)NULL)
        {
            RCProcI2OMsgQ(AdapterID);
            for (i = 0; i < 100000; i++)     /* please don't hog the bus!!! */
                ;
            timeout++;
            if (timeout > 10000)
            {
				printk("RCShutdownLANCard(): timeout\n");
                break;
            }
        }
        if (ReturnAddr != (PU32)NULL)
            *ReturnAddr = (U32)pPab->pCallbackFunc;
    }
    return RC_RTN_NO_ERROR ;
}


/*
** =========================================================================
** RCSetRavlinIPandMask()
**
** Set the Ravlin 45/PCI cards IP address and network mask.
**
** IP address and mask must be in network byte order.
** For example, IP address 1.2.3.4 and mask 255.255.255.0 would be
** 0x04030201 and 0x00FFFFFF on a little endian machine.
**
** =========================================================================
*/
RC_RETURN
RCSetRavlinIPandMask(U16 AdapterID, U32 ipAddr, U32 netMask)
{
    volatile PU32 pMsg;
    U32 off;
    PPAB pPab;

    pPab = PCIAdapterBlock[AdapterID];

    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;

    off = pPab->p_atu->InQueue; /* get addresss of message */
    
    if (0xFFFFFFFF == off)
        return RC_RTN_FREE_Q_EMPTY;
    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + off);

    /* setup private message */
    pMsg[0] = SEVEN_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = 0;                 /* initiator context */
    pMsg[3] = 0x219;             /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_SET_IP_AND_MASK;
    pMsg[5] = ipAddr; 
    pMsg[6] = netMask;


    pPab->p_atu->InQueue = off;   /* send it to the I2O device */
    return RC_RTN_NO_ERROR ;

}

/*
** =========================================================================
** RCGetRavlinIPandMask()
**
** get the IP address and MASK from the card
** 
** =========================================================================
*/
RC_RETURN
RCGetRavlinIPandMask(U16 AdapterID, PU32 pIpAddr, PU32 pNetMask, 
                     PFNWAITCALLBACK WaitCallback)
{
    unsigned i, timeout;
    U32      off;
    PU32     pMsg, p32;
    PPAB     pPab;
    PATU     p_atu;
    
#ifdef DEBUG
    kprintf("RCGetRavlinIPandMask: pIpAddr is 0x%08.8ulx, *IpAddr is 0x%08.8ulx\n", pIpAddr, *pIpAddr);
#endif /* DEBUG */

    pPab = PCIAdapterBlock[AdapterID];
    
    if (pPab == NULL)
        return RC_RTN_ADPTR_NOT_REGISTERED;
    
    p_atu = pPab->p_atu;
    off = p_atu->InQueue;   /* get addresss of message */
 
    if (0xFFFFFFFF == off)
        return RC_RTN_FREE_Q_EMPTY;

    p32 = (volatile PU32)(pPab->pLinOutMsgBlock - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB));
    *p32 = 0xFFFFFFFF;

    pMsg = (PU32)(pPab->pPci45LinBaseAddr + off);

#ifdef DEBUG
    kprintf("RCGetRavlinIPandMask: p_atu 0x%08.8ulx, off 0x%08.8ulx, p32 0x%08.8ulx\n", p_atu, off, p32);
#endif /* DEBUG */
    /* setup private message */
    pMsg[0] = FIVE_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_PRIVATE << 24 | I2O_HOST_TID << 12 | RC_LAN_TARGET_ID;
    pMsg[2] = 0;               /* initiator context */
    pMsg[3] = 0x218;           /* transaction context */
    pMsg[4] = RC_PCI45_VENDOR_ID << 16 | RC_PRIVATE_GET_IP_AND_MASK;
    pMsg[5] = pPab->outMsgBlockPhyAddr - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB); 

    p_atu->InQueue = off;   /* send it to the I2O device */
#ifdef DEBUG
    kprintf("RCGetRavlinIPandMask: p_atu 0x%08.8ulx, off 0x%08.8ulx, p32 0x%08.8ulx\n", p_atu, off, p32);
#endif /* DEBUG */

    /* wait for the rcpci45 board to update the info */
    timeout = 100000;
    while (0xffffffff == *p32)
    {
        if (WaitCallback)
            (*WaitCallback)();
    
        for (i = 0; i < 1000; i++)
            ;

        if (!timeout--)
        {
#ifdef DEBUG
            kprintf("RCGetRavlinIPandMask: Timeout\n");
#endif /* DEBUG */
            return RC_RTN_MSG_REPLY_TIMEOUT;
        }
    }

#ifdef DEBUG
    kprintf("RCGetRavlinIPandMask: after time out\n", \
            "p32[0] (IpAddr) 0x%08.8ulx, p32[1] (IPmask) 0x%08.8ulx\n", p32[0], p32[1]);
#endif /* DEBUG */
    
    /* send IP and mask to user's space  */
    *pIpAddr  = p32[0];
    *pNetMask = p32[1];


#ifdef DEBUG
    kprintf("RCGetRavlinIPandMask: pIpAddr is 0x%08.8ulx, *IpAddr is 0x%08.8ulx\n", pIpAddr, *pIpAddr);
#endif /* DEBUG */

    return RC_RTN_NO_ERROR;
}

/* 
** /////////////////////////////////////////////////////////////////////////
** /////////////////////////////////////////////////////////////////////////
**
**                        local functions
**
** /////////////////////////////////////////////////////////////////////////
** /////////////////////////////////////////////////////////////////////////
*/

/*
** =========================================================================
** SendI2OOutboundQInitMsg()
**
** =========================================================================
*/
static int 
SendI2OOutboundQInitMsg(PPAB pPab)
{
    U32 msgOffset, timeout, phyOutQFrames, i;
    volatile PU32 pMsg;
    volatile PU32 p32;
    
    
    
    msgOffset = pPab->p_atu->InQueue;

    
    if (msgOffset == 0xFFFFFFFF)
    {
#ifdef DEBUG
        kprintf("SendI2OOutboundQInitMsg(): Inbound Free Q empty!\n");
#endif /* DEBUG */
        return RC_RTN_FREE_Q_EMPTY;
    }
    
    
    /* calc virual address of msg - virual already mapped to physical */    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + msgOffset);

#ifdef DEBUG
    kprintf("SendI2OOutboundQInitMsg - pMsg = 0x%08.8ulx, InQ msgOffset = 0x%08.8ulx\n", pMsg, msgOffset);
#endif /* DEBUG */

    pMsg[0] = EIGHT_WORD_MSG_SIZE | TRL_OFFSET_6;
    pMsg[1] = I2O_EXEC_OUTBOUND_INIT << 24 | I2O_HOST_TID << 12 | I2O_IOP_TID;
    pMsg[2] = DEFAULT_RECV_INIT_CONTEXT;
    pMsg[3] = 0x106; /* transaction context */
    pMsg[4] = 4096; /* Host page frame size */
    pMsg[5] = MSG_FRAME_SIZE  << 16 | 0x80; /* outbound msg frame size and Initcode */
    pMsg[6] = 0xD0000004;       /* simple sgl element LE, EOB */
    /* phys address to return status - area right after PAB */
    pMsg[7] = pPab->outMsgBlockPhyAddr - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB); 

    /* virual pointer to return buffer - clear first two dwords */
    p32 = (PU32)(pPab->pLinOutMsgBlock - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB));
    p32[0] = 0;
    
    /* post to Inbound Post Q */   
    pPab->p_atu->InQueue = msgOffset;
    
    /* wait for response */
    timeout = 100000;
    while(1)
    {
        for (i = 0; i < 1000; i++)      /* please don't hog the bus!!! */
            ;
            
        if (p32[0])
            break;
            
        if (!timeout--)
        {
#ifdef DEBUG
            kprintf("Timeout wait for InitOutQ InPrgress status from IOP\n");
#endif /* DEBUG */
            return RC_RTN_NO_I2O_STATUS;
        }
    }

    timeout = 100000;
    while(1)
    {
        for (i = 0; i < 1000; i++)      /* please don't hog the bus!!! */
            ;
            
        if (p32[0] == I2O_EXEC_OUTBOUND_INIT_COMPLETE)
            break;

        if (!timeout--)
        {
#ifdef DEBUG
            kprintf("Timeout wait for InitOutQ Complete status from IOP\n");
#endif /* DEBUG */
            return RC_RTN_NO_I2O_STATUS;
        }
    }

    /* load PCI outbound free Q with MF physical addresses */
    phyOutQFrames = pPab->outMsgBlockPhyAddr;

    for (i = 0; i < NMBR_MSG_FRAMES; i++)
    {
        pPab->p_atu->OutQueue = phyOutQFrames;
        phyOutQFrames += MSG_FRAME_SIZE;
    }
    return RC_RTN_NO_ERROR;
}


/*
** =========================================================================
** GetI2OStatus()
**
** Send StatusGet Msg, wait for results return directly to buffer.
**
** =========================================================================
*/
static int 
GetI2OStatus(PPAB pPab)
{
    U32 msgOffset, timeout;
    PU32 pMsg;
    volatile PU32 p32;
    
    
    msgOffset = pPab->p_atu->InQueue; 
#ifdef DEBUG
    printk("GetI2OStatus: msg offset = 0x%x\n", msgOffset);
#endif /* DEBUG */
    if (msgOffset == 0xFFFFFFFF)
    {
#ifdef DEBUG
        kprintf("GetI2OStatus(): Inbound Free Q empty!\n");
#endif /* DEBUG */
        return RC_RTN_FREE_Q_EMPTY;
    }

    /* calc virual address of msg - virual already mapped to physical */    
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + msgOffset);

    pMsg[0] = NINE_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_EXEC_STATUS_GET << 24 | I2O_HOST_TID << 12 | I2O_IOP_TID;
    pMsg[2] = 0; /* universal context */
    pMsg[3] = 0; /* universal context */
    pMsg[4] = 0; /* universal context */
    pMsg[5] = 0; /* universal context */
    /* phys address to return status - area right after PAB */
    pMsg[6] = pPab->outMsgBlockPhyAddr - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB); 
    pMsg[7] = 0;
    pMsg[8] = 88;  /*  return 88 bytes */

    /* virual pointer to return buffer - clear first two dwords */
    p32 = (volatile PU32)(pPab->pLinOutMsgBlock - ADAPTER_BLOCK_RESERVED_SPACE + sizeof(PAB));
    p32[0] = 0;
    p32[1] = 0;

#ifdef DEBUG
    kprintf("GetI2OStatus - pMsg:0x%08.8ulx, msgOffset:0x%08.8ulx, [1]:0x%08.8ulx, [6]:0x%08.8ulx\n",
            pMsg, msgOffset, pMsg[1], pMsg[6]);
#endif /* DEBUG */
   
    /* post to Inbound Post Q */   
    pPab->p_atu->InQueue = msgOffset;
    
#ifdef DEBUG
    kprintf("Return status to p32 = 0x%08.8ulx\n", p32);
#endif /* DEBUG */
    
    /* wait for response */
    timeout = 1000000;
    while(1)
    {
        int i;
        
        for (i = 0; i < 1000; i++)      /* please don't hog the bus!!! */
            ;
            
        if (p32[0] && p32[1])
            break;
            
        if (!timeout--)
        {
#ifdef DEBUG
            kprintf("Timeout waiting for status from IOP\n");
            kprintf("0x%08.8ulx:0x%08.8ulx:0x%08.8ulx:0x%08.8ulx\n", p32[0], p32[1], p32[2], p32[3]);
            kprintf("0x%08.8ulx:0x%08.8ulx:0x%08.8ulx:0x%08.8ulx\n", p32[4], p32[5], p32[6], p32[7]);
            kprintf("0x%08.8ulx:0x%08.8ulx:0x%08.8ulx:0x%08.8ulx\n", p32[8], p32[9], p32[10], p32[11]);
#endif /* DEBUG */
            return RC_RTN_NO_I2O_STATUS;
        }
    }
            
#ifdef DEBUG
    kprintf("0x%08.8ulx:0x%08.8ulx:0x%08.8ulx:0x%08.8ulx\n", p32[0], p32[1], p32[2], p32[3]);
    kprintf("0x%08.8ulx:0x%08.8ulx:0x%08.8ulx:0x%08.8ulx\n", p32[4], p32[5], p32[6], p32[7]);
    kprintf("0x%08.8ulx:0x%08.8ulx:0x%08.8ulx:0x%08.8ulx\n", p32[8], p32[9], p32[10], p32[11]);
#endif /* DEBUG */
    /* get IOP state */
    pPab->IOPState = ((volatile PU8)p32)[10];
    pPab->InboundMFrameSize  = ((volatile PU16)p32)[6];
    
#ifdef DEBUG
    kprintf("IOP state 0x%02.2x InFrameSize = 0x%04.4x\n", 
            pPab->IOPState, pPab->InboundMFrameSize);
#endif /* DEBUG */
    return RC_RTN_NO_ERROR;
}


/*
** =========================================================================
** SendEnableSysMsg()
**
**
** =========================================================================
*/
static int 
SendEnableSysMsg(PPAB pPab)
{
    U32 msgOffset; // timeout;
    volatile PU32 pMsg;

    msgOffset = pPab->p_atu->InQueue;

    if (msgOffset == 0xFFFFFFFF)
    {
#ifdef DEBUG
        kprintf("SendEnableSysMsg(): Inbound Free Q empty!\n");
#endif /* DEBUG */
        return RC_RTN_FREE_Q_EMPTY;
    }

    /* calc virual address of msg - virual already mapped to physical */
    pMsg = (PU32)(pPab->pPci45LinBaseAddr + msgOffset);

#ifdef DEBUG
    kprintf("SendEnableSysMsg - pMsg = 0x%08.8ulx, InQ msgOffset = 0x%08.8ulx\n", pMsg, msgOffset);
#endif /* DEBUG */

    pMsg[0] = FOUR_WORD_MSG_SIZE | SGL_OFFSET_0;
    pMsg[1] = I2O_EXEC_SYS_ENABLE << 24 | I2O_HOST_TID << 12 | I2O_IOP_TID;
    pMsg[2] = DEFAULT_RECV_INIT_CONTEXT;
    pMsg[3] = 0x110; /* transaction context */
    pMsg[4] = 0x50657465; /*  RedCreek Private */

    /* post to Inbound Post Q */
    pPab->p_atu->InQueue = msgOffset;

    return RC_RTN_NO_ERROR;
}


/*
** =========================================================================
** FillI2OMsgFromTCB()
**
** inputs   pMsgU32 - virual pointer (mapped to physical) of message frame
**          pXmitCntrlBlock - pointer to caller buffer control block.
**
** fills in LAN SGL after Transaction Control Word or Bucket Count.
** =========================================================================
*/
static int 
FillI2OMsgSGLFromTCB(PU32 pMsgFrame, PRCTCB pTransCtrlBlock)
{
    unsigned int nmbrBuffers, nmbrSeg, nmbrDwords, context, flags;
    PU32 pTCB, pMsg;

    /* SGL element flags */   
#define EOB        0x40000000
#define LE         0x80000000
#define SIMPLE_SGL 0x10000000
#define BC_PRESENT 0x01000000

    pTCB = (PU32)pTransCtrlBlock;
    pMsg = pMsgFrame;
    nmbrDwords = 0;

#ifdef DEBUG
    kprintf("FillI2OMsgSGLFromTCBX\n");
    kprintf("TCB  0x%08.8ulx:0x%08.8ulx:0x%08.8ulx:0x%08.8ulx:0x%08.8ulx\n",
            pTCB[0], pTCB[1], pTCB[2], pTCB[3], pTCB[4]);
    kprintf("pTCB 0x%08.8ulx, pMsg 0x%08.8ulx\n", pTCB, pMsg);
#endif /* DEBUG */
    
    nmbrBuffers = *pTCB++;
    
    if (!nmbrBuffers)
    {
        return -1;
    }

    do
    {
        context = *pTCB++; /* buffer tag (context) */
        nmbrSeg = *pTCB++; /* number of segments */

        if (!nmbrSeg)
        {
            return -1;
        }
        
        flags = SIMPLE_SGL | BC_PRESENT;

        if (1 == nmbrSeg)
        {
            flags |= EOB;
        
            if (1 == nmbrBuffers)
                flags |= LE;
        }    

        /* 1st SGL buffer element has context */
        pMsg[0] = pTCB[0] | flags ; /* send over count (segment size) */
        pMsg[1] = context;
        pMsg[2] = pTCB[1]; /* send buffer segment physical address */
        nmbrDwords += 3;
        pMsg += 3;
        pTCB += 2;

        
        if (--nmbrSeg)
        {
            do
            {
                flags = SIMPLE_SGL;
                
                if (1 == nmbrSeg)
                {
                    flags |= EOB;
                
                    if (1 == nmbrBuffers)
                        flags |= LE;
                }    
                
                pMsg[0] = pTCB[0] | flags;  /* send over count */
                pMsg[1] = pTCB[1];   /* send buffer segment physical address */
                nmbrDwords += 2;
                pTCB += 2;
                pMsg += 2;
        
            } while (--nmbrSeg);
        }
        
    } while (--nmbrBuffers);
    
    return nmbrDwords;
}


/*
** =========================================================================
** ProcessOutboundI2OMsg()
**
** process I2O reply message
** * change to msg structure *
** =========================================================================
*/
static void 
ProcessOutboundI2OMsg(PPAB pPab, U32 phyAddrMsg)
{
    PU8 p8Msg;
    PU32 p32;
    //  U16 count;
    
    
    p8Msg = pPab->pLinOutMsgBlock + (phyAddrMsg - pPab->outMsgBlockPhyAddr);
    p32 = (PU32)p8Msg;
    
#ifdef DEBUG
    kprintf("VXD: ProcessOutboundI2OMsg - pPab 0x%08.8ulx, phyAdr 0x%08.8ulx, linAdr 0x%08.8ulx\n", pPab, phyAddrMsg, p8Msg);
    kprintf("msg :0x%08.8ulx:0x%08.8ulx:0x%08.8ulx:0x%08.8ulx\n", p32[0], p32[1], p32[2], p32[3]);
    kprintf("msg :0x%08.8ulx:0x%08.8ulx:0x%08.8ulx:0x%08.8ulx\n", p32[4], p32[5], p32[6], p32[7]);
#endif /* DEBUG */

    if (p32[4] >> 24 != I2O_REPLY_STATUS_SUCCESS)
    {
#ifdef DEBUG
        kprintf("Message reply status not success\n");
#endif /* DEBUG */
        return;
    }
    
    switch (p8Msg[7] )  /* function code byte */
    {
    case I2O_EXEC_SYS_TAB_SET:
        msgFlag = 1;
#ifdef DEBUG
        kprintf("Received I2O_EXEC_SYS_TAB_SET reply\n");
#endif /* DEBUG */
        break;

    case I2O_EXEC_HRT_GET:
        msgFlag = 1;
#ifdef DEBUG
        kprintf("Received I2O_EXEC_HRT_GET reply\n");
#endif /* DEBUG */
        break;
        
    case I2O_EXEC_LCT_NOTIFY:
        msgFlag = 1;
#ifdef DEBUG
        kprintf("Received I2O_EXEC_LCT_NOTIFY reply\n");
#endif /* DEBUG */
        break;
        
    case I2O_EXEC_SYS_ENABLE:
        msgFlag = 1;
#ifdef DEBUG
        kprintf("Received I2O_EXEC_SYS_ENABLE reply\n");
#endif /* DEBUG */
        break;

    default:    
#ifdef DEBUG
        kprintf("Received UNKNOWN reply\n");
#endif /* DEBUG */
        break;
    }
}
