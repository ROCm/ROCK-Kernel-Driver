/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#ifndef H_PACKET_APPEND_H
#define H_PACKET_APPEND_H

/* Layers Includes */ 
#include <mtl_types.h>

/*******************/

#ifdef __WIN__
#include <string.h>
#endif

#if !defined(__DARWIN__) && defined(__LINUX__) && !defined(__KERNEL__)
  #include <endian.h>
#endif

#if defined(VXWORKS_OS) && defined(MT_BIG_ENDIAN)
#define __cpu_to_le32(x) (x)
#define __cpu_to_le16(x) (x)
#endif 

#define IBA_TRANSPORT_HEADER_VERSION  0
/*Define of all fields length*/
#define IBWORD            4    /* 4 bytes */
#define LRH_LEN           8    /* (LRH_LEN = 8 byte) */
#define RWH_LEN           4
#define GRH_LEN           40
#define BTH_LEN           12
#define RDETH_LEN         4
#define DETH_LEN          8
#define RETH_LEN          16
#define AETH_LEN          4
#define ImmDt_LEN         4
#define AtomETH_LEN       28
#define AtomAETH_LEN      8
#define ICRC_LEN          4
#define VCRC_LEN          2

/*Define all transport layer packet length with out LRH or GRH*/

/********************* Reliable Connection (RC) *************************/

#define RC_SEND_FIRST_LEN        (BTH_LEN)
#define RC_SEND_MIDDLE_LEN       (BTH_LEN)
#define RC_SEND_LAST_LEN         (BTH_LEN)
#define RC_SEND_ONLY_LEN         (BTH_LEN) /*need to write all the relavent packets*/
#define RC_WRITE_ONLY_LEN        (BTH_LEN + RETH_LEN)
#define RC_WRITE_FIRST_LEN       (BTH_LEN + RETH_LEN)
#define RC_WRITE_MIDDLE_LEN      (BTH_LEN)
#define RC_WRITE_LAST_LEN        (BTH_LEN)
#define RC_READ_REQ_LEN          (BTH_LEN + RETH_LEN)
#define RC_READ_RESP_FIRST_LEN   (BTH_LEN + AETH_LEN)
#define RC_READ_RESP_MIDDLE_LEN  (BTH_LEN)
#define RC_READ_RESP_LAST_LEN    (BTH_LEN + AETH_LEN) 
#define RC_READ_RESP_ONLY_LEN    (BTH_LEN + AETH_LEN)
#define RC_ACKNOWLEDGE_LEN       (BTH_LEN + AETH_LEN)

/********************* Unreliable Connection (UC) ************************/


/********************* Reliable Datagram (RD) ****************************/


/********************* Unreliable Datagram (UD) ***************************/

#define UD_SEND_ONLY_LEN         (BTH_LEN + DETH_LEN)

/*Define Link Next Header Definition */
enum {
   RAW = 0x0,               /* |LRH|... (Etertype)*/
   IP_NON_IBA_TRANS = 0x1,  /* |LRH|GRH|...       */
   IBA_LOCAL = 0x2,         /* |LRH|BTH|...       */
   IBA_GLOBAL = 0x3         /* |LRH|GRH|BTH|...   */
}; 
typedef u_int32_t LNH_t;
	
typedef enum{
   NON_RAW_IBA = 0x1B      /* |LRH|GRH|BTH|...*/
   /* TBD IETF RFC 1700 et.seq*/
   /* All the rest is ipver6 headers*/
} NxtHdr_t;
	

typedef enum{
  FIRST_PACKET=0,
  MIDDLE_PACKET=1,
  LAST_PACKET=2
} IB_pkt_place;

/**************************************** fields structures define *******************************/	
typedef struct IB_LRH_st IB_LRH_st;

struct IB_LRH_st{             /* *****  LRH  *****   Local Route Header(8 bytes)*/
	u_int8_t  VL;         /*"Only 4 LS-bits"The virtual lane that the packet is using */
	u_int8_t  LVer;       /*"Only 4 LS-bits"Link level protocol of the packet*/
	u_int8_t  SL;         /*"Only 4 LS-bits"Service level requested within the subnet*/
        u_int8_t  reserved1;  /*"Only 2 LS-bits"Transmitted as 0,ignored on receive. **internally modified** */
        u_int8_t  LNH;        /*"Only 2 LS-bits"Identifies the headers that follow the LRH. **internally modified** */
        u_int16_t DLID;       /*The destination port and path on the local subnet*/
        u_int8_t  reserved2;  /*"Only 5 LS-bits"Transmitted as 0,ignored on receive.**internally modified** */
	u_int16_t PktLen;     /*"Only 11 LS-bits"The size of tha packet in four-byte words. **internally modified** */
	u_int16_t SLID;       /*The source port (injection point) on the local subnet*/
 };

typedef struct IB_GRH_st IB_GRH_st;

struct IB_GRH_st{            /* **** GRH ****   Global Route Header(40 bytes)*/
  u_int8_t  IPVer;           /*"Only 4 LS-bits"The version og the GRH*/
  u_int8_t  TClass;          /*Used by IBA to communicate global service level*/
  u_int32_t FlowLabel;       /*"Only 20 LS-bits"Sequences of packets requiring special handl*/
  u_int16_t PayLen;          /*The length of the packet in bytes **internally modified** */
  u_int8_t  NxtHdr;          /*Identifies the headers that follow the GRH*/
  u_int8_t  HopLmt;          /*Bound on the number of hops between subnets*/
  u_int8_t SGID[16];         /*Global indentifier for the source port*/
  u_int8_t DGID[16];         /*Global indentifier for the detination port*/
};

typedef struct IB_BTH_st IB_BTH_st;

struct IB_BTH_st{              /* **** BTH ****   Base Transport Header (12 bytes)*/
	u_int8_t  OpCode;      /*IBA packet type and which extentions follows **internally modified** */
	u_int8_t  SE;          /*"Only 1 LS-bit"If an event sould be gen by responder or not*/
	u_int8_t  M;           /*"Only 1 LS-bit"Communication migration state*/
	u_int8_t  PadCnt;      /*"Only 2 LS-bits"Number of bytes that align to 4 byte boundary **internally modified** */
	u_int8_t  TVer;        /*"Only 4 LS-bits"IBA transport headers version. **internally modified** */
	u_int16_t P_KEY;       /*Logical partition associated with this packet*/
	u_int8_t  reserved1;   /*Transmitted as 0,ignored on receive. Not included in the icrc. **internally modified** */
	u_int32_t DestQP;      /*"Only 24 LS-bits"Destination work queu pair number*/
	u_int8_t  A;           /*"Only 1 LS-bit"If an ack should be returnd by the responder*/
	u_int8_t  reserved2;   /*"only 7 LS-bits"Transmitted as 0,ignored .included in icrc. **internally modified** */
	u_int32_t PSN;         /*"Only 24 LS-bits"detect a missing or duplicate packet*/
};

typedef struct IB_RDETH_st IB_RDETH_st;

struct IB_RDETH_st{                /* **** RDETH **** (4 bytes)*/
                                   /*Reliable Datagram Extended Transport Header*/
	u_int8_t  reserved1;	   /*Transmitted as 0,ignored on receive.*/
	u_int32_t EECnxt;          /*"Only 24 LS-bits"Which end to end context for this packet*/
};

typedef struct IB_DETH_st IB_DETH_st;

struct IB_DETH_st{                 /* **** DETH ****(8 bytes)*/
			           /*Datagram Extended Transport Header */
	u_int32_t Q_Key;	   /*For an authorize access to destination queue*/
	u_int8_t  reserved1;       /*ransmitted as 0,ignored on receive.*/
	u_int32_t SrcQP;           /*"Only 24 LS-bits"Work queu nuber at the source*/
};
	
typedef struct IB_RETH_st IB_RETH_st;

struct IB_RETH_st{	         /* **** RETH ****(16 bytes)*/
                                 /*RDMA Extended Transport Header */
  u_int64_t VA;                  /*Virtual address of the RDMA operation*/
	u_int32_t R_Key;         /*Remote key that authorize access for the RDMA operation*/
	u_int32_t DMALen;        /*The length of the DMA operation*/
};

typedef struct IB_AtomicETH_st IB_AtomicETH_st;

struct IB_AtomicETH_st{          /* **** AtomicETH ****(28 bytes)*/
			         /*Atomic Extended Transport Header */
	u_int64_t VA;            /*Remote virtual address */
	u_int32_t R_Key;         /*Remote key that authorize access to the remote virtual address*/
	u_int64_t SwapDt;        /*An operand in atomic operations*/
	u_int64_t CmpDt;         /*An operand in cmpswap atomic operation*/
};

typedef struct IB_AETH_st IB_AETH_st;

struct IB_AETH_st{               /* **** AETH ****(4 bytes)*/
			         /*ACK	Extended Transport Header */
	u_int8_t Syndrome;       /*Indicates if NAK or ACK and additional info about ACK & NAK*/
	u_int32_t MSN;           /*"Only 24 Ls-bit"Sequence number of the last message comleted*/
};	

typedef struct IB_AtomicAckETH_st  IB_AtomicAckETH_st;

struct IB_AtomicAckETH_st{       /* **** AtomicAckETH ****(8 bytes)*/
                                 /* Atomic ACK Extended Transport Header */
  u_int64_t OrigRemDt;           /*Return oprand in atomic operation and contains the data*/
   			         /*in the remote memory location before the atomic operation*/
};

typedef struct IB_ImmDt_st IB_ImmDt_st;

struct IB_ImmDt_st{               /* **** Immediate Data **** (4 bytes)*/
                                  /* Contains the additional data that is placed in the */
  u_int32_t ImmDt;                /* received Completion Queue Element (CQE).           */
			          /* The ImmDt is Only in Send or RDMA-Write packets.   */
}; 

typedef struct IB_PKT_st IB_PKT_st;

struct IB_PKT_st{            /*IB packet structure Analayze structure*/
  IB_LRH_st           *lrh_st_p;
  IB_GRH_st           *grh_st_p;
  IB_BTH_st           *bth_st_p;
  IB_RDETH_st         *rdeth_st_p;
  IB_DETH_st          *deth_st_p;
  IB_RETH_st          *reth_st_p;
  IB_AtomicETH_st     *atomic_eth_st_p;
  IB_AETH_st          *aeth_st_p;
  IB_AtomicAckETH_st  *atomic_acketh_st_p;
  IB_ImmDt_st         *immdt_st_p;
  u_int16_t           packet_size;
  u_int16_t           *payload_buf;
  u_int16_t           payload_size;
};


/*Start of function declarations*/
/******************************************************************************
 *  Function: append_LRH
 *
 *  Description: This function is appending LRH to IB packets .
 *  To use this function you must have a LRH struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with free space for the LRH field
 *  The function Ignores the OpCode,PadCnt,reserved1,reserved2 fields it overwrite
 *  The given information.
 *
 *  Parameters:
 *    lrh_st_p(IN)  IB_LRH_st *
 *	Link next header .
 *    packet_size (out) u_int16_t.
 *  Full packet size include ICRC VCRC;
 *    packet_buf_p(out) u_int16_t **
 *	pointer to the pointer of the full packet .
 *      The function will return the pointer 8 bytes back).
 *  LNH ( out) LNH_t
 *    Link next header IB local , global , RAW ...
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if no packet was generated.
 *****************************************************************************/
call_result_t
append_LRH (IB_LRH_st *lrh_st_p, u_int16_t packet_size,
	    u_int16_t **packet_buf_p,LNH_t LNH);

/******************************************************************************
 *  Function: extract_LRH
 *
 *  Description: This function is extractinging the LRH from the  IB packets .
 *  The function will update all the members in the IB_LRH struct.
 *
 *  Parameters:
 *    lrh_st_p(out)  IB_LRH_st *
 *	Local route  header of the generated packet.
 *    packet_buf_p(out) u_int16_t **
 *	pointer to the pointer of the full packet .
 *      (The function will return the pointer 8 bytes forward).
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
extract_LRH(IB_LRH_st *lrh_st_p, u_int16_t **packet_buf_p);

/******************************************************************************
 *  Function: append_GRH
 *
 *  Description: This function is appending GRH to IB packets .
 *  To use this function you must have a GRH struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with free space for the GRH field
 *  Note : The function Ignores the PayLen and NxtHdr fields it overwrite
 *  The given information.
 *
 *
 *  Parameters:
 *    grh_st_p(IN)  IB_GRH_st *
 *	Global Route Header.
 *    packet_size (out) u_int16_t.
 *  Full packet size include ICRC VCRC;
 *    packet_buf_p(out) u_int16_t **
 *	pointer to the pointer of the full packet .
 *      The function will return the pointer 40 bytes back).
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if the field was not appended.
 *****************************************************************************/
call_result_t
append_GRH (IB_GRH_st *grh_st_p, u_int16_t packet_size,
	        u_int16_t **packet_buf_vp);

/******************************************************************************
 *  Function: extract_GRH
 *
 *  Description: This function is extractinging the GRH from the  IB packets .
 *  The function will update all the members in the IB_GRH struct.
 *
 *  Parameters:
 *    grh_st_p(out)  IB_GRH_st *
 *	Global route  header of the generated packet.
 *    packet_buf_p(out) u_int16_t **
 *	pointer to the pointer of the full packet .
 *      (The function will return the pointer 40 bytes forward).
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
extract_GRH(IB_GRH_st *grh_st_p, u_int16_t **packet_buf_p);

/******************************************************************************
 *  Function: append_BTH
 *
 *  Description: This function is appending BTH to IB packets .
 *  To use this function you must have a BTH struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with free space for the BTH field
 *  The function Ignores the OpCode,PadCnt,reserved1,reserved2 fields it overwrite
 *  The given information.
 *
 *  Parameters:
 *    bth_st_p(out)  IB_BTH_st *
 *	Base trasport header of the generated packet. (the func ignores the reserved1/2 fields).
 *    packet_buf_p(out) u_int16_t **
 *	pointer to the pointer of the full packet .
 *      The function will return the pointer 12 bytes back).
 *    payload_size(in) u_int16_t
 *	The payload_size in bytes for calc the PadCnt.
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if no packet was generated.
 *****************************************************************************/
call_result_t
append_BTH (IB_BTH_st *bth_st_p, u_int16_t **packet_buf_p, u_int16_t payload_size);

/******************************************************************************
 *  Function: extract_BTH
 *
 *  Description: This function is extractinging the BTH from the  IB packets .
 *  The function will update all the members in the IB_BTH struct.
 *
 *  Parameters:
 *    bth_st_p(out)  IB_BTH_st *
 *	Base trasport header of the generated packet.
 *    packet_buf_p(out) u_int16_t **
 *	pointer to the pointer of the full packet .
 *      (The function will return the pointer 12 bytes forward).
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
extract_BTH(IB_BTH_st *bth_st_p, u_int16_t **packet_buf_p);

/******************************************************************************
 *  Function: append_RETH
 *
 *  Description: This function is appending RETH to IB packets .
 *  To use this function you must have a RETH struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with a free space for the RETH field.
 *
 *  Parameters:
 *    reth_st_p(in)  IB_RETH_st *
 *	RDMA Extended Transport Header.
 *    packet_buf_p(out) u_int16_t **
 *	pointer to the pointer of the full packet .
 *      The function will move the pointer 16 bytes back).
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if no packet was generated.
 *****************************************************************************/
call_result_t
append_RETH (IB_RETH_st *reth_st_p, u_int16_t **packet_buf_p);

/******************************************************************************
 *  Function: extract_RETH
 *
 *  Description: This function is extractinging the RETH from the  IB packets .
 *  The function will update all the members in the IB_RETH struct.
 *
 *  Parameters:
 *    reth_st_p(out)  IB_RETH_st *
 *	RDMA Extended Trasport header of the generated packet.
 *    packet_buf_p(out) u_int16_t **
 *	pointer to the pointer of the full packet .
 *      (The function will return the pointer 16 bytes forward).
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
extract_RETH(IB_RETH_st *reth_st_p, u_int16_t **packet_buf_p);

/******************************************************************************
 *  Function: append_AETH
 *
 *  Description: This function is appending AETH to IB packets .
 *  To use this function you must have a AETH struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with a free space for the AETH field.
 *
 *  Parameters:
 *    aeth_st_p(in)  IB_AETH_st *
 *	ACK Extended Trasport Header.
 *    packet_buf_p(out) u_int16_t **
 *	pointer to the pointer of the full packet .
 *      (The function will move the pointer 4 bytes back).
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR if no packet was generated.
 *****************************************************************************/
call_result_t
append_AETH (IB_AETH_st *aeth_st_p, u_int16_t **packet_buf_p);

/******************************************************************************
 *  Function: extract_AETH
 *
 *  Description: This function is extractinging the AETH from the  IB packets .
 *  The function will update all the members in the IB_AETH struct.
 *
 *  Parameters:
 *    aeth_st_p(out)  IB_AETH_st *
 *	ACK Extended Trasport header of the generated packet.
 *    packet_buf_p(out) u_int16_t **
 *	pointer to the pointer of the full packet .
 *      (The function will return the pointer 4 bytes forward).
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
extract_AETH (IB_AETH_st *aeth_st_p, u_int16_t **packet_buf_p);

/*****************************************************************************/
/*                   From this point the functiom is Datagram                */
/*****************************************************************************/

/******************************************************************************
 *  Function: append_DETH
 *
 *  Description: This function is appending DETH to IB packets .
 *  To use this function you must have a DETH struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with a free space for the DETH field.
 *
 *  Parameters:
 *    deth_st_p(in)  IB_DETH_st *
 *	Datagram Extended Trasport Header. (the func ignores the reserved1 field).
 *    packet_buf_p(out) u_int16_t **
 *	pointer to the pointer of the full packet .
 *      (The function will move the pointer 8 bytes back).
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR .
 *****************************************************************************/
call_result_t
append_DETH (IB_DETH_st *deth_st_p, u_int16_t **packet_buf_p);

/******************************************************************************
 *  Function: extract_DETH
 *
 *  Description: This function is extractinging the DETH from the  IB packets .
 *  The function will update all the members in the IB_DETH struct.
 *
 *  Parameters:
 *    deth_st_p(out)  IB_DETH_st *
 *	Datagram Extended Trasport header of the generated packet.
 *    packet_buf_p(out) u_int16_t **
 *	pointer to the pointer of the full packet .
 *      (The function will return the pointer 8 bytes forward).
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
extract_DETH(IB_DETH_st *deth_st_p, u_int16_t **packet_buf_p);

/******************************************************************************
 *  Function: append_ImmDt
 *
 *  Description: This function is appending ImmDt to IB packets .
 *  To use this function you must have a ImmDt struct,
 *  with all the detailes to create the wanted packet.
 *  and an allocated area with a free space for the ImmDt field.
 *
 *  Parameters:
 *    ImmDt_st_p(in)  IB_ImmDt_st *
 *       Contains the additional data of the generated packet.
 *    packet_buf_p(out) u_int16_t **
 *      pointer to the pointer of the full packet .
 *      (The function will move the pointer 8 bytes back).
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR .
 *****************************************************************************/   
call_result_t
append_ImmDt(IB_ImmDt_st *ImmDt_st_p, u_int16_t **packet_buf_p);            

/******************************************************************************
 *  Function: extract_ImmDt
 *
 *  Description: This function is extractinging the ImmDt from the  IB packets .
 *  The function will update all the members in the IB_ImmDt struct.
 *
 *  Parameters:
 *    ImmDt_st_p(out)  IB_ImmDt_st *
 *      Contains the additional data of the generated packet.
 *    packet_buf_p(out) u_int16_t **
 *      pointer to the pointer of the full packet .
 *      (The function will return the pointer 4 bytes forward).
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
extract_ImmDt(IB_ImmDt_st *ImmDt_st_p, u_int16_t **packet_buf_p);   

 /******************************************************************************
 *  Function: append_ICRC
 *
 *  Description: This function is appending the ICRC  to the  IB packets .
 *
 *  Parameters:
 *   start_ICRC(in) u_int16_t *
 *    pointer to the start of the ICRC field
 *   ICRC(in) u_int32_t
 *    The ICRC to insert
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
append_ICRC(u_int16_t *start_ICRC, u_int32_t ICRC);

 /******************************************************************************
 *  Function: V
 *
 *  Description: This function is extractinging the ICRC  from the  IB packets .
 *
 *  Parameters:
 *   start_ICRC(in) u_int16_t *
 *    pointer to the start of the ICRC field
 *   ICRC(out) u_int32_t *
 *    The ICRC to extract
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
extract_ICRC(u_int16_t *start_ICRC, u_int32_t *ICRC);

  /******************************************************************************
 *  Function: append_VCRC
 *
 *  Description: This function is appending the VCRC  to the  IB packets .
 *
 *  Parameters:
 *   start_VCRC(in) u_int16_t *
 *    pointer to the start of the VCRC field
 *   VCRC(in) u_int32_t
 *    The VCRC to insert
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
append_VCRC(u_int16_t *start_VCRC, u_int16_t VCRC);

/******************************************************************************
 *  Function: extract_ICRC
 *
 *  Description: This function is extractinging the ICRC  from the  IB packets .
 *
 *  Parameters:
 *   start_ICRC(in) u_int16_t *
 *    pointer to the start of the ICRC field
 *   ICRC(out) u_int32_t *
 *    The ICRC to extract
 *
 *  Returns:
 *    call_result_t
 *        MT_OK,
 *        MT_ERROR
 *****************************************************************************/
call_result_t
extract_VCRC(u_int16_t *start_VCRC, u_int16_t *VCRC);


#endif /* H_PACKET_APPEND_H */
