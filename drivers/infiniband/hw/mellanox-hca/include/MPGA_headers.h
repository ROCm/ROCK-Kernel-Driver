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
#ifndef MPGA_headers_H
#define MPGA_headers_H


                
typedef u_int8_t nMPGA_bit_t;                


           
struct IB_LRH_p_t{               /* *****  LRH  *****   Local Route Header(8 bytes)*/
  nMPGA_bit_t  VL[0x04];         /*"Only 4 LS-bits"The virtual lane that the packet is using */
  nMPGA_bit_t  LVer[0x04];       /*"Only 4 LS-bits"Link level protocol of the packet*/
  nMPGA_bit_t  SL[0x04];         /*"Only 4 LS-bits"Service level requested within the subnet*/
  nMPGA_bit_t  reserved1[0x02];  /*"Only 2 LS-bits"Transmitted as 0,ignored on receive. **internally modified** */
  nMPGA_bit_t  LNH[0x02];        /*"Only 2 LS-bits"Identifies the headers that follow the LRH. **internally modified** */
  nMPGA_bit_t  DLID[0x10];       /*The destination port and path on the local subnet*/
  nMPGA_bit_t  reserved2[0x05];  /*"Only 5 LS-bits"Transmitted as 0,ignored on receive.**internally modified** */
  nMPGA_bit_t  PktLen[0x0b];     /*"Only 11 LS-bits"The size of tha packet in four-byte words. **internally modified** */
  nMPGA_bit_t  SLID[0x10];       /*The source port (injection point) on the local subnet*/
};

struct IB_GRH_p_t{              /* **** GRH ****   Global Route Header(40 bytes)*/
  nMPGA_bit_t  IPVer[0x04];        /*"Only 4 LS-bits"The version og the GRH*/
  nMPGA_bit_t  TClass[0x08];       /*Used by IBA to communicate global service level*/
  nMPGA_bit_t  FlowLabel[0x14];    /*"Only 20 LS-bits"Sequences of packets requiring special handl*/
  nMPGA_bit_t  PayLen[0x10];       /*The length of the packet in bytes **internally modified** */
  nMPGA_bit_t  NxtHdr[0x08];       /*Identifies the headers that follow the GRH*/
  nMPGA_bit_t  HopLmt[0x08];       /*Bound on the number of hops between subnets*/
  nMPGA_bit_t  SGID[0x80];         /*Global indentifier for the source port*/
  nMPGA_bit_t  DGID[0x80];         /*Global indentifier for the detination port*/
};

struct IB_BTH_p_t{             /* **** BTH ****   Base Transport Header (12 bytes)*/
  nMPGA_bit_t  OpCode[0x08];     /*IBA packet type and which extentions follows **internally modified** */
  nMPGA_bit_t  SE;               /*"Only 1 LS-bit"If an event sould be gen by responder or not*/
  nMPGA_bit_t  M;                /*"Only 1 LS-bit"Communication migration state*/
  nMPGA_bit_t  PadCnt[0x02];     /*"Only 2 LS-bits"Number of bytes that align to 4 byte boundary **internally modified** */
  nMPGA_bit_t  TVer[0x04];       /*"Only 4 LS-bits"IBA transport headers version. **internally modified** */
  nMPGA_bit_t  P_KEY[0x10];      /*Logical partition associated with this packet*/
  nMPGA_bit_t  reserved1[0x08];  /*Transmitted as 0,ignored on receive. Not included in the icrc. **internally modified** */
  nMPGA_bit_t  DestQP[0x18];     /*"Only 24 LS-bits"Destination work queu pair number*/
  nMPGA_bit_t  A;                /*"Only 1 LS-bit"If an ack should be returnd by the responder*/
  nMPGA_bit_t  reserved2[0x07];  /*"only 7 LS-bits"Transmitted as 0,ignored .included in icrc. **internally modified** */
  nMPGA_bit_t  PSN[0x18];        /*"Only 24 LS-bits"detect a missing or duplicate packet*/
};

struct IB_RDETH_p_t{            /* **** RDETH **** (4 bytes)*/
                                   /*Reliable Datagram Extended Transport Header*/
  nMPGA_bit_t  reserved1[0x08]; /*Transmitted as 0,ignored on receive.*/
  nMPGA_bit_t  EECnxt[0x18];    /*"Only 24 LS-bits"Which end to end context for this packet*/
};

struct IB_DETH_p_t{             /* **** DETH ****(8 bytes)*/
			                       /*Datagram Extended Transport Header */
  nMPGA_bit_t  Q_Key[0x20];	   /*For an authorize access to destination queue*/
  nMPGA_bit_t  reserved1[0x08]; /*ransmitted as 0,ignored on receive.*/
  nMPGA_bit_t  SrcQP[0x18];     /*"Only 24 LS-bits"Work queu nuber at the source*/
};

struct IB_RETH_p_t{             /* **** RETH ****(16 bytes)*/
                                   /*RDMA Extended Transport Header */
  nMPGA_bit_t  VA[0x40];        /*Virtual address of the RDMA operation*/
  nMPGA_bit_t  R_Key[0x20];     /*Remote key that authorize access for the RDMA operation*/
  nMPGA_bit_t  DMALen[0x20];    /*The length of the DMA operation*/
};

struct IB_AtomicETH_p_t{        /* **** AtomicETH ****(28 bytes)*/
			                       /*Atomic Extended Transport Header */
  nMPGA_bit_t  VA[0x40];        /*Remote virtual address */
  nMPGA_bit_t  R_Key[0x20];     /*Remote key that authorize access to the remote virtual address*/
  nMPGA_bit_t  SwapDt[0x40];    /*An operand in atomic operations*/
  nMPGA_bit_t  CmpDt[0x40];     /*An operand in cmpswap atomic operation*/
};

struct IB_AETH_p_t{             /* *** ACK ****(4 bytes)*/
                                   /*ACK Extended Transport Header */
  nMPGA_bit_t  Syndrome[0x08];    /*Indicates ACK or NAK plus additional info*/
  nMPGA_bit_t  MSN[0x18];         /*Sequence number of the last message completed*/
};

struct IB_AtomicAckETH_p_t{     /* **** AtomicAckETH ****(8 bytes)*/
                                   /* Atomic ACK Extended Transport Header */
  nMPGA_bit_t  OrigRemDt[0x40];   /*Return oprand in atomic operation and contains the data*/
   			                       /*in the remote memory location before the atomic operation*/
};

struct IB_ImmDt_p_t{               /* **** Immediate Data **** (4 bytes)*/
                                  /* Contains the additional data that is placed in the */
  nMPGA_bit_t ImmDt[0x20];       /* received Completion Queue Element (CQE).           */
			                      /* The ImmDt is Only in Send or RDMA-Write packets.   */
};



/*IBA LOCAL*/
struct MPGA_rc_send_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_rc_send_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_rc_send_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct  MPGA_rc_send_last_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_rc_send_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_rc_send_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};


/*RDMA Write types*/
struct MPGA_rc_write_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_rc_write_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_rc_write_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_rc_write_last_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_rc_write_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_rc_write_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

/*RDMA read types*/
struct MPGA_rc_read_req_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_rc_read_res_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

struct MPGA_rc_read_res_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_rc_read_res_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

struct MPGA_rc_read_res_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

/* Other Types*/
struct MPGA_rc_ack_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

struct MPGA_rc_atomic_ack_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AETH_p_t IB_AETH_P;
  struct IB_AtomicAckETH_p_t IB_AtomicAckETH;
};

struct MPGA_rc_CmpSwap_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AtomicETH_p_t IB_AtomicETH;
};

struct MPGA_rc_FetchAdd_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AtomicETH_p_t IB_AtomicETH;
};

/* Unreliable Connection */
/*Send Types*/
struct MPGA_uc_send_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_uc_send_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_uc_send_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_uc_send_last_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_uc_send_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_uc_send_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

/*RDMA Write types*/
struct MPGA_uc_write_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_uc_write_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_uc_write_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_uc_write_last_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_uc_write_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_uc_write_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

/* Reliable Datagram */

/*Send Types*/
struct MPGA_rd_send_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_rd_send_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_rd_send_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_rd_send_last_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_rd_send_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_rd_send_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};


/*RDMA Write types*/
struct MPGA_rd_write_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_rd_write_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_rd_write_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_rd_write_last_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_rd_write_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_rd_write_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_RETH_p_t IB_RETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_rd_read_req_p_t{    
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_rd_read_res_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

struct MPGA_rd_read_res_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
};

struct MPGA_rd_read_res_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

struct MPGA_rd_read_res_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_AETH_p_t IB_AETH_P;
};


struct MPGA_rd_ack_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

struct MPGA_rd_atomic_ack_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_AETH_p_t IB_AETH_P;
  struct IB_AtomicAckETH_p_t IB_AtomicAckETH_P;
};

struct MPGA_rd_CmpSwap_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_AtomicETH_p_t IB_AtomicETH_P;
};

struct MPGA_rd_FetchAdd_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_AtomicETH_p_t IB_AtomicETH_P;
};

struct MPGA_rd_resync_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

/* Unreliable Datagram */

struct MPGA_ud_send_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_ud_send_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};




/*IBA GLOBAL*/
struct MPGA_G_rc_send_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_G_rc_send_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_G_rc_send_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct  MPGA_G_rc_send_last_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_G_rc_send_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_G_rc_send_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};


/*RDMA Write types*/
struct MPGA_G_rc_write_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_G_rc_write_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_G_rc_write_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_G_rc_write_last_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_G_rc_write_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_G_rc_write_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

/*RDMA read types*/
struct MPGA_G_rc_read_req_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_G_rc_read_res_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

struct MPGA_G_rc_read_res_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_G_rc_read_res_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

struct MPGA_G_rc_read_res_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

/* Other Types*/
struct MPGA_G_rc_ack_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

struct MPGA_G_rc_atomic_ack_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AETH_p_t IB_AETH_P;
  struct IB_AtomicAckETH_p_t IB_AtomicAckETH;
};

struct MPGA_G_rc_CmpSwap_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AtomicETH_p_t IB_AtomicETH;
};

struct MPGA_G_rc_FetchAdd_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_AtomicETH_p_t IB_AtomicETH;
};

/* Unreliable Connection */
/*Send Types*/
struct MPGA_G_uc_send_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_G_uc_send_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_G_uc_send_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_G_uc_send_last_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_G_uc_send_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_G_uc_send_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

/*RDMA Write types*/
struct MPGA_G_uc_write_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_G_uc_write_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_G_uc_write_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
};

struct MPGA_G_uc_write_last_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_G_uc_write_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_G_uc_write_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RETH_p_t IB_RETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

/* Reliable Datagram */

/*Send Types*/
struct MPGA_G_rd_send_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_G_rd_send_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_G_rd_send_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_G_rd_send_last_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_G_rd_send_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_G_rd_send_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};


/*RDMA Write types*/
struct MPGA_G_rd_write_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_G_rd_write_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_G_rd_write_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_G_rd_write_last_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_G_rd_write_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_G_rd_write_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_RETH_p_t IB_RETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};

struct MPGA_G_rd_read_req_p_t{    
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_RETH_p_t IB_RETH_P;
};

struct MPGA_G_rd_read_res_first_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

struct MPGA_G_rd_read_res_middle_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
};

struct MPGA_G_rd_read_res_last_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

struct MPGA_G_rd_read_res_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_AETH_p_t IB_AETH_P;
};


struct MPGA_G_rd_ack_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_AETH_p_t IB_AETH_P;
};

struct MPGA_G_rd_atomic_ack_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_AETH_p_t IB_AETH_P;
  struct IB_AtomicAckETH_p_t IB_AtomicAckETH_P;
};

struct MPGA_G_rd_CmpSwap_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_AtomicETH_p_t IB_AtomicETH_P;
};

struct MPGA_G_rd_FetchAdd_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_AtomicETH_p_t IB_AtomicETH_P;
};

struct MPGA_G_rd_resync_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_RDETH_p_t IB_RDETH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

/* Unreliable Datagram */

struct MPGA_G_ud_send_only_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_DETH_p_t IB_DETH_P;
};

struct MPGA_G_ud_send_only_ImmDt_p_t{
  struct IB_LRH_p_t  IB_LRH_P;
  struct IB_GRH_p_t  IB_GRH_P;
  struct IB_BTH_p_t  IB_BTH_P;
  struct IB_DETH_p_t IB_DETH_P;
  struct IB_ImmDt_p_t IB_ImmDt_P;
};


union MPGA_headers_p_t{
  /* IBA LOCAL*/
  /* RC - Reliable Connection - opcode prefix 000*/               /* OpCode */      
  struct MPGA_rc_send_first_p_t        MPGA_rc_send_first_p;      /* 00000  */
  struct MPGA_rc_send_middle_p_t       MPGA_rc_send_middle_p;     /* 00001  */
  struct MPGA_rc_send_last_p_t         MPGA_rc_send_last_p;       /* 00010  */
  struct MPGA_rc_send_last_ImmDt_p_t   MPGA_rc_send_last_ImmDt_p; /* 00011  */
  struct MPGA_rc_send_only_p_t         MPGA_rc_send_only_p;       /* 00100  */
  struct MPGA_rc_send_only_ImmDt_p_t   MPGA_rc_send_only_ImmDt_p; /* 00101  */

  struct MPGA_rc_write_first_p_t       MPGA_rc_write_first_p;     /* 00110  */
  struct MPGA_rc_write_middle_p_t      MPGA_rc_write_middle_p;    /* 00111  */
  struct MPGA_rc_write_last_p_t        MPGA_rc_write_last_p;      /* 01000  */
  struct MPGA_rc_write_last_ImmDt_p_t  MPGA_rc_write_last_ImmDt_p;/* 01001  */
  struct MPGA_rc_write_only_p_t        MPGA_rc_write_only_p;      /* 01010  */
  struct MPGA_rc_write_only_ImmDt_p_t  MPGA_rc_write_only_ImmDt_p;/* 01011  */

  struct MPGA_rc_read_req_p_t          MPGA_rc_read_req_p;        /* 01100  */
  struct MPGA_rc_read_res_first_p_t    MPGA_rc_read_res_first_p;  /* 01101  */
  struct MPGA_rc_read_res_middle_p_t   MPGA_rc_read_res_middle_p; /* 01110  */
  struct MPGA_rc_read_res_last_p_t     MPGA_rc_read_res_last_p;   /* 01111  */
  struct MPGA_rc_read_res_only_p_t     MPGA_rc_read_res_only_p;   /* 10000  */
                                                                              
  struct MPGA_rc_ack_p_t               MPGA_rc_ack_p;             /* 10001  */
  struct MPGA_rc_atomic_ack_p_t        MPGA_rc_atomic_ack_p;      /* 10010  */
  struct MPGA_rc_CmpSwap_p_t           MPGA_rc_CmpSwap_p;         /* 10011  */
  struct MPGA_rc_FetchAdd_p_t          MPGA_rc_FetchAdd_p;        /* 10100  */
                                                                              
  /* UC - Unreliable Connection - opcode prefix 001*/                         
  struct MPGA_uc_send_first_p_t        MPGA_uc_send_first_p;      /* 00000  */
  struct MPGA_uc_send_middle_p_t       MPGA_uc_send_middle_p;     /* 00001  */
  struct MPGA_uc_send_last_p_t         MPGA_uc_send_last_p;       /* 00010  */
  struct MPGA_uc_send_last_ImmDt_p_t   MPGA_uc_send_last_ImmDt_p; /* 00011  */
  struct MPGA_uc_send_only_p_t         MPGA_uc_send_only_p;       /* 00100  */
  struct MPGA_uc_send_only_ImmDt_p_t   MPGA_uc_send_only_ImmDt_p; /* 00101  */
                                                                              
  struct MPGA_uc_write_first_p_t       MPGA_uc_write_first_p;     /* 00110  */
  struct MPGA_uc_write_middle_p_t      MPGA_uc_write_middle_p;    /* 00111  */
  struct MPGA_uc_write_last_p_t        MPGA_uc_write_last_p;      /* 01000  */
  struct MPGA_uc_write_last_ImmDt_p_t  MPGA_uc_write_last_ImmDt_p;/* 01001  */
  struct MPGA_uc_write_only_p_t        MPGA_uc_write_only_p;      /* 01010  */
  struct MPGA_uc_write_only_ImmDt_p_t  MPGA_uc_write_only_ImmDt_p;/* 01011  */
                                                                              
  /* RD - Reliable Datagram - opcode prefix 010*/                             
  struct MPGA_rd_send_first_p_t        MPGA_rd_send_first_p;      /* 00000  */
  struct MPGA_rd_send_middle_p_t       MPGA_rd_send_middle_p;     /* 00001  */
  struct MPGA_rd_send_last_p_t         MPGA_rd_send_last_p;       /* 00010  */
  struct MPGA_rd_send_last_ImmDt_p_t   MPGA_rd_send_last_ImmDt_p; /* 00011  */
  struct MPGA_rd_send_only_p_t         MPGA_rd_send_only_p;       /* 00100  */
  struct MPGA_rd_send_only_ImmDt_p_t   MPGA_rd_send_only_ImmDt_p; /* 00101  */
                                                                              
  struct MPGA_rd_write_first_p_t       MPGA_rd_write_first_p;     /* 00110  */
  struct MPGA_rd_write_middle_p_t      MPGA_rd_write_middle_p;    /* 00111  */
  struct MPGA_rd_write_last_p_t        MPGA_rd_write_last_p;      /* 01000  */
  struct MPGA_rd_write_last_ImmDt_p_t  MPGA_rd_write_last_ImmDt_p;/* 01001  */
  struct MPGA_rd_write_only_p_t        MPGA_rd_write_only_p;      /* 01010  */
  struct MPGA_rd_write_only_ImmDt_p_t  MPGA_rd_write_only_ImmDt_p;/* 01011  */
                                                                              
  struct MPGA_rd_read_req_p_t          MPGA_rd_read_req_p;        /* 01100  */
  struct MPGA_rd_read_res_first_p_t    MPGA_rd_read_res_first_p;  /* 01101  */
  struct MPGA_rd_read_res_middle_p_t   MPGA_rd_read_res_middle_p; /* 01110  */
  struct MPGA_rd_read_res_last_p_t     MPGA_rd_read_res_last_p;   /* 01111  */
  struct MPGA_rd_read_res_only_p_t     MPGA_rd_read_res_only_p;   /* 10000  */
                                                                              
  struct MPGA_rd_ack_p_t               MPGA_rd_ack_p;             /* 10001  */
  struct MPGA_rd_atomic_ack_p_t        MPGA_rd_atomic_ack_p;      /* 10010  */
  struct MPGA_rd_CmpSwap_p_t           MPGA_rd_CmpSwap_p;         /* 10011  */
  struct MPGA_rd_FetchAdd_p_t          MPGA_rd_FetchAdd_p;        /* 10100  */
  struct MPGA_rd_resync_p_t            MPGA_rd_resync_p;          /* 10101  */
                                                                              
  /* UD - UnReliable Datagram - opcode prefix 011*/                           
  struct MPGA_ud_send_only_p_t         MPGA_ud_send_only_p;       /* 00100  */
  struct MPGA_ud_send_only_ImmDt_p_t   MPGA_ud_send_only_ImmDt_p; /* 00101  */


  /*IBA GLOBAL*/
  /* RC - Reliable Connection - opcode prefix 000*/               /* OpCode */      
  struct MPGA_G_rc_send_first_p_t        MPGA_G_rc_send_first_p;      /* 00000  */
  struct MPGA_G_rc_send_middle_p_t       MPGA_G_rc_send_middle_p;     /* 00001  */
  struct MPGA_G_rc_send_last_p_t         MPGA_G_rc_send_last_p;       /* 00010  */
  struct MPGA_G_rc_send_last_ImmDt_p_t   MPGA_G_rc_send_last_ImmDt_p; /* 00011  */
  struct MPGA_G_rc_send_only_p_t         MPGA_G_rc_send_only_p;       /* 00100  */
  struct MPGA_G_rc_send_only_ImmDt_p_t   MPGA_G_rc_send_only_ImmDt_p; /* 00101  */

  struct MPGA_G_rc_write_first_p_t       MPGA_G_rc_write_first_p;     /* 00110  */
  struct MPGA_G_rc_write_middle_p_t      MPGA_G_rc_write_middle_p;    /* 00111  */
  struct MPGA_G_rc_write_last_p_t        MPGA_G_rc_write_last_p;      /* 01000  */
  struct MPGA_G_rc_write_last_ImmDt_p_t  MPGA_G_rc_write_last_ImmDt_p;/* 01001  */
  struct MPGA_G_rc_write_only_p_t        MPGA_G_rc_write_only_p;      /* 01010  */
  struct MPGA_G_rc_write_only_ImmDt_p_t  MPGA_G_rc_write_only_ImmDt_p;/* 01011  */

  struct MPGA_G_rc_read_req_p_t          MPGA_G_rc_read_req_p;        /* 01100  */
  struct MPGA_G_rc_read_res_first_p_t    MPGA_G_rc_read_res_first_p;  /* 01101  */
  struct MPGA_G_rc_read_res_middle_p_t   MPGA_G_rc_read_res_middle_p; /* 01110  */
  struct MPGA_G_rc_read_res_last_p_t     MPGA_G_rc_read_res_last_p;   /* 01111  */
  struct MPGA_G_rc_read_res_only_p_t     MPGA_G_rc_read_res_only_p;   /* 10000  */
                                                                              
  struct MPGA_G_rc_ack_p_t               MPGA_G_rc_ack_p;             /* 10001  */
  struct MPGA_G_rc_atomic_ack_p_t        MPGA_G_rc_atomic_ack_p;      /* 10010  */
  struct MPGA_G_rc_CmpSwap_p_t           MPGA_G_rc_CmpSwap_p;         /* 10011  */
  struct MPGA_G_rc_FetchAdd_p_t          MPGA_G_rc_FetchAdd_p;        /* 10100  */
                                                                              
  /* UC - Unreliable Connection - opcode prefix 001*/                         
  struct MPGA_G_uc_send_first_p_t        MPGA_G_uc_send_first_p;      /* 00000  */
  struct MPGA_G_uc_send_middle_p_t       MPGA_G_uc_send_middle_p;     /* 00001  */
  struct MPGA_G_uc_send_last_p_t         MPGA_G_uc_send_last_p;       /* 00010  */
  struct MPGA_G_uc_send_last_ImmDt_p_t   MPGA_G_uc_send_last_ImmDt_p; /* 00011  */
  struct MPGA_G_uc_send_only_p_t         MPGA_G_uc_send_only_p;       /* 00100  */
  struct MPGA_G_uc_send_only_ImmDt_p_t   MPGA_G_uc_send_only_ImmDt_p; /* 00101  */
                                                                              
  struct MPGA_G_uc_write_first_p_t       MPGA_G_uc_write_first_p;     /* 00110  */
  struct MPGA_G_uc_write_middle_p_t      MPGA_G_uc_write_middle_p;    /* 00111  */
  struct MPGA_G_uc_write_last_p_t        MPGA_G_uc_write_last_p;      /* 01000  */
  struct MPGA_G_uc_write_last_ImmDt_p_t  MPGA_G_uc_write_last_ImmDt_p;/* 01001  */
  struct MPGA_G_uc_write_only_p_t        MPGA_G_uc_write_only_p;      /* 01010  */
  struct MPGA_G_uc_write_only_ImmDt_p_t  MPGA_G_uc_write_only_ImmDt_p;/* 01011  */
                                                                              
  /* RD - Reliable Datagram - opcode prefix 010*/                             
  struct MPGA_G_rd_send_first_p_t        MPGA_G_rd_send_first_p;      /* 00000  */
  struct MPGA_G_rd_send_middle_p_t       MPGA_G_rd_send_middle_p;     /* 00001  */
  struct MPGA_G_rd_send_last_p_t         MPGA_G_rd_send_last_p;       /* 00010  */
  struct MPGA_G_rd_send_last_ImmDt_p_t   MPGA_G_rd_send_last_ImmDt_p; /* 00011  */
  struct MPGA_G_rd_send_only_p_t         MPGA_G_rd_send_only_p;       /* 00100  */
  struct MPGA_G_rd_send_only_ImmDt_p_t   MPGA_G_rd_send_only_ImmDt_p; /* 00101  */
                                                                              
  struct MPGA_G_rd_write_first_p_t       MPGA_G_rd_write_first_p;     /* 00110  */
  struct MPGA_G_rd_write_middle_p_t      MPGA_G_rd_write_middle_p;    /* 00111  */
  struct MPGA_G_rd_write_last_p_t        MPGA_G_rd_write_last_p;      /* 01000  */
  struct MPGA_G_rd_write_last_ImmDt_p_t  MPGA_G_rd_write_last_ImmDt_p;/* 01001  */
  struct MPGA_G_rd_write_only_p_t        MPGA_G_rd_write_only_p;      /* 01010  */
  struct MPGA_G_rd_write_only_ImmDt_p_t  MPGA_G_rd_write_only_ImmDt_p;/* 01011  */
                                                                              
  struct MPGA_G_rd_read_req_p_t          MPGA_G_rd_read_req_p;        /* 01100  */
  struct MPGA_G_rd_read_res_first_p_t    MPGA_G_rd_read_res_first_p;  /* 01101  */
  struct MPGA_G_rd_read_res_middle_p_t   MPGA_G_rd_read_res_middle_p; /* 01110  */
  struct MPGA_G_rd_read_res_last_p_t     MPGA_G_rd_read_res_last_p;   /* 01111  */
  struct MPGA_G_rd_read_res_only_p_t     MPGA_G_rd_read_res_only_p;   /* 10000  */
                                                                              
  struct MPGA_G_rd_ack_p_t               MPGA_G_rd_ack_p;             /* 10001  */
  struct MPGA_G_rd_atomic_ack_p_t        MPGA_G_rd_atomic_ack_p;      /* 10010  */
  struct MPGA_G_rd_CmpSwap_p_t           MPGA_G_rd_CmpSwap_p;         /* 10011  */
  struct MPGA_G_rd_FetchAdd_p_t          MPGA_G_rd_FetchAdd_p;        /* 10100  */
  struct MPGA_G_rd_resync_p_t            MPGA_G_rd_resync_p;          /* 10101  */
                                                                              
  /* UD - UnReliable Datagram - opcode prefix 011*/                           
  struct MPGA_G_ud_send_only_p_t         MPGA_G_ud_send_only_p;       /* 00100  */
  struct MPGA_G_ud_send_only_ImmDt_p_t   MPGA_G_ud_send_only_ImmDt_p; /* 00101  */


};



/* IBA LOCAL */
/* RC - Reliable Connected types */
/*-------------------------------*/
/*Send Types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_rc_send_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_rc_send_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_rc_send_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_rc_send_last_ImmDt_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_rc_send_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_rc_send_only_ImmDt_t;


/*RDMA Write types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
} MPGA_rc_write_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_rc_write_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_rc_write_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_rc_write_last_ImmDt_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
} MPGA_rc_write_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_rc_write_only_ImmDt_t;
/*RDMA read types*/
typedef struct {    
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
} MPGA_rc_read_req_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_AETH_st IB_AETH;
} MPGA_rc_read_res_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_rc_read_res_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_AETH_st IB_AETH;
} MPGA_rc_read_res_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_AETH_st IB_AETH;
} MPGA_rc_read_res_only_t;
/* Other Types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_AETH_st IB_AETH;
} MPGA_rc_ack_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_AETH_st IB_AETH;
  IB_AtomicAckETH_st IB_AtomicAckETH;
} MPGA_rc_atomic_ack_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_AtomicETH_st IB_AtomicETH;
} MPGA_rc_CmpSwap_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_AtomicETH_st IB_AtomicETH;
} MPGA_rc_FetchAdd_t;

/* Unreliable Connection */
/*Send Types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_uc_send_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_uc_send_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_uc_send_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_uc_send_last_ImmDt_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_uc_send_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_uc_send_only_ImmDt_t;

/*RDMA Write types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
} MPGA_uc_write_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_uc_write_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
} MPGA_uc_write_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_uc_write_last_ImmDt_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
} MPGA_uc_write_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_uc_write_only_ImmDt_t;

/* Reliable Datagram */

/*Send Types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_rd_send_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_rd_send_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_rd_send_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_rd_send_last_ImmDt_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_rd_send_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_rd_send_only_ImmDt_t;


/*RDMA Write types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_RETH_st IB_RETH;
} MPGA_rd_write_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_rd_write_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_rd_write_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_rd_write_last_ImmDt_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_RETH_st IB_RETH;
} MPGA_rd_write_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_RETH_st IB_RETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_rd_write_only_ImmDt_t;

typedef struct {    
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_RETH_st IB_RETH;
} MPGA_rd_read_req_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_AETH_st IB_AETH;
} MPGA_rd_read_res_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
} MPGA_rd_read_res_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_AETH_st IB_AETH;
} MPGA_rd_read_res_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_AETH_st IB_AETH;
} MPGA_rd_read_res_only_t;


typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_AETH_st IB_AETH;
} MPGA_rd_ack_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_AETH_st IB_AETH;
  IB_AtomicAckETH_st IB_AtomicAckETH;
} MPGA_rd_atomic_ack_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_AtomicETH_st IB_AtomicETH;
} MPGA_rd_CmpSwap_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_AtomicETH_st IB_AtomicETH;
} MPGA_rd_FetchAdd_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_rd_resync_t;

/* Unreliable Datagram */

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_DETH_st IB_DETH;
} MPGA_ud_send_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_BTH_st  IB_BTH;
  IB_DETH_st IB_DETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_ud_send_only_ImmDt_t;


/* IBA GLOBAL */
/* RC - Reliable Connected types */
/*-------------------------------*/
/*Send Types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_rc_send_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_rc_send_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_rc_send_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_rc_send_last_ImmDt_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_rc_send_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_rc_send_only_ImmDt_t;


/*RDMA Write types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
} MPGA_G_rc_write_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_rc_write_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_rc_write_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_rc_write_last_ImmDt_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
} MPGA_G_rc_write_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_rc_write_only_ImmDt_t;
/*RDMA read types*/
typedef struct {    
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
} MPGA_G_rc_read_req_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_AETH_st IB_AETH;
} MPGA_G_rc_read_res_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_rc_read_res_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_AETH_st IB_AETH;
} MPGA_G_rc_read_res_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_AETH_st IB_AETH;
} MPGA_G_rc_read_res_only_t;
/* Other Types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_AETH_st IB_AETH;
} MPGA_G_rc_ack_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_AETH_st IB_AETH;
  IB_AtomicAckETH_st IB_AtomicAckETH;
} MPGA_G_rc_atomic_ack_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_AtomicETH_st IB_AtomicETH;
} MPGA_G_rc_CmpSwap_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_AtomicETH_st IB_AtomicETH;
} MPGA_G_rc_FetchAdd_t;

/* Unreliable Connection */
/*Send Types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_uc_send_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_uc_send_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_uc_send_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_uc_send_last_ImmDt_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_uc_send_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_uc_send_only_ImmDt_t;

/*RDMA Write types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
} MPGA_G_uc_write_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_uc_write_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
} MPGA_G_uc_write_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_uc_write_last_ImmDt_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
} MPGA_G_uc_write_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RETH_st IB_RETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_uc_write_only_ImmDt_t;

/* Reliable Datagram */

/*Send Types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_G_rd_send_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_G_rd_send_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_G_rd_send_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_rd_send_last_ImmDt_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_G_rd_send_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_rd_send_only_ImmDt_t;


/*RDMA Write types*/
typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_RETH_st IB_RETH;
} MPGA_G_rd_write_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_G_rd_write_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_G_rd_write_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_rd_write_last_ImmDt_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_RETH_st IB_RETH;
} MPGA_G_rd_write_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_RETH_st IB_RETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_rd_write_only_ImmDt_t;

typedef struct {    
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_RETH_st IB_RETH;
} MPGA_G_rd_read_req_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_AETH_st IB_AETH;
} MPGA_G_rd_read_res_first_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
} MPGA_G_rd_read_res_middle_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_AETH_st IB_AETH;
} MPGA_G_rd_read_res_last_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_AETH_st IB_AETH;
} MPGA_G_rd_read_res_only_t;


typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_AETH_st IB_AETH;
} MPGA_G_rd_ack_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_AETH_st IB_AETH;
  IB_AtomicAckETH_st IB_AtomicAckETH;
} MPGA_G_rd_atomic_ack_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_AtomicETH_st IB_AtomicETH;
} MPGA_G_rd_CmpSwap_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
  IB_AtomicETH_st IB_AtomicETH;
} MPGA_G_rd_FetchAdd_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_RDETH_st IB_RDETH;
  IB_DETH_st IB_DETH;
} MPGA_G_rd_resync_t;

/* Unreliable Datagram */

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_DETH_st IB_DETH;
} MPGA_G_ud_send_only_t;

typedef struct {
  IB_LRH_st  IB_LRH;
  IB_GRH_st  IB_GRH;
  IB_BTH_st  IB_BTH;
  IB_DETH_st IB_DETH;
  IB_ImmDt_st IB_ImmDt;
} MPGA_G_ud_send_only_ImmDt_t;


typedef union {

  /*IBA LOCAL*/
  /* RC - Reliable Connection - opcode prefix 000*/   /* OpCode */      
  MPGA_rc_send_first_t       MPGA_rc_send_first;      /* 00000  */      
  MPGA_rc_send_middle_t      MPGA_rc_send_middle;     /* 00001  */      
  MPGA_rc_send_last_t        MPGA_rc_send_last;       /* 00010  */      
  MPGA_rc_send_last_ImmDt_t  MPGA_rc_send_last_ImmDt; /* 00011  */            
  MPGA_rc_send_only_t        MPGA_rc_send_only;       /* 00100  */        
  MPGA_rc_send_only_ImmDt_t  MPGA_rc_send_only_ImmDt; /* 00101  */            
                                                                         
  MPGA_rc_write_first_t      MPGA_rc_write_first;     /* 00110  */      
  MPGA_rc_write_middle_t     MPGA_rc_write_middle;    /* 00111  */      
  MPGA_rc_write_last_t       MPGA_rc_write_last;      /* 01000  */        
  MPGA_rc_write_last_ImmDt_t MPGA_rc_write_last_ImmDt;/* 01001  */            
  MPGA_rc_write_only_t       MPGA_rc_write_only;      /* 01010  */      
  MPGA_rc_write_only_ImmDt_t MPGA_rc_write_only_ImmDt;/* 01011  */            

  MPGA_rc_read_req_t         MPGA_rc_read_req;        /* 01100  */
  MPGA_rc_read_res_first_t   MPGA_rc_read_res_first;  /* 01101  */
  MPGA_rc_read_res_middle_t  MPGA_rc_read_res_middle; /* 01110  */
  MPGA_rc_read_res_last_t    MPGA_rc_read_res_last;   /* 01111  */
  MPGA_rc_read_res_only_t    MPGA_rc_read_res_only;   /* 10000  */
                                                    
  MPGA_rc_ack_t              MPGA_rc_ack;             /* 10001  */         
  MPGA_rc_atomic_ack_t       MPGA_rc_atomic_ack;      /* 10010  */ 
  MPGA_rc_CmpSwap_t          MPGA_rc_CmpSwap;         /* 10011  */         
  MPGA_rc_FetchAdd_t         MPGA_rc_FetchAdd;        /* 10100  */ 

  /* UC - Unreliable Connection - opcode prefix 001*/
  MPGA_uc_send_first_t       MPGA_uc_send_first;      /* 00000  */      
  MPGA_uc_send_middle_t      MPGA_uc_send_middle;     /* 00001  */      
  MPGA_uc_send_last_t        MPGA_uc_send_last;       /* 00010  */      
  MPGA_uc_send_last_ImmDt_t  MPGA_uc_send_last_ImmDt; /* 00011  */            
  MPGA_uc_send_only_t        MPGA_uc_send_only;       /* 00100  */        
  MPGA_uc_send_only_ImmDt_t  MPGA_uc_send_only_ImmDt; /* 00101  */            
                                                                         
  MPGA_uc_write_first_t      MPGA_uc_write_first;     /* 00110  */      
  MPGA_uc_write_middle_t     MPGA_uc_write_middle;    /* 00111  */      
  MPGA_uc_write_last_t       MPGA_uc_write_last;      /* 01000  */        
  MPGA_uc_write_last_ImmDt_t MPGA_uc_write_last_ImmDt;/* 01001  */            
  MPGA_uc_write_only_t       MPGA_uc_write_only;      /* 01010  */      
  MPGA_uc_write_only_ImmDt_t MPGA_uc_write_only_ImmDt;/* 01011  */

  /* RD - Reliable Datagram - opcode prefix 010*/      
  MPGA_rd_send_first_t       MPGA_rd_send_first;      /* 00000  */      
  MPGA_rd_send_middle_t      MPGA_rd_send_middle;     /* 00001  */      
  MPGA_rd_send_last_t        MPGA_rd_send_last;       /* 00010  */      
  MPGA_rd_send_last_ImmDt_t  MPGA_rd_send_last_ImmDt; /* 00011  */            
  MPGA_rd_send_only_t        MPGA_rd_send_only;       /* 00100  */        
  MPGA_rd_send_only_ImmDt_t  MPGA_rd_send_only_ImmDt; /* 00101  */            
                                                                         
  MPGA_rd_write_first_t      MPGA_rd_write_first;     /* 00110  */      
  MPGA_rd_write_middle_t     MPGA_rd_write_middle;    /* 00111  */      
  MPGA_rd_write_last_t       MPGA_rd_write_last;      /* 01000  */        
  MPGA_rd_write_last_ImmDt_t MPGA_rd_write_last_ImmDt;/* 01001  */            
  MPGA_rd_write_only_t       MPGA_rd_write_only;      /* 01010  */      
  MPGA_rd_write_only_ImmDt_t MPGA_rd_write_only_ImmDt;/* 01011  */            

  MPGA_rd_read_req_t         MPGA_rd_read_req;        /* 01100  */
  MPGA_rd_read_res_first_t   MPGA_rd_read_res_first;  /* 01101  */
  MPGA_rd_read_res_middle_t  MPGA_rd_read_res_middle; /* 01110  */
  MPGA_rd_read_res_last_t    MPGA_rd_read_res_last;   /* 01111  */
  MPGA_rd_read_res_only_t    MPGA_rd_read_res_only;   /* 10000  */
                                                    
  MPGA_rd_ack_t              MPGA_rd_ack;             /* 10001  */         
  MPGA_rd_atomic_ack_t       MPGA_rd_atomic_ack;      /* 10010  */ 
  MPGA_rd_CmpSwap_t          MPGA_rd_CmpSwap;         /* 10011  */         
  MPGA_rd_FetchAdd_t         MPGA_rd_FetchAdd;        /* 10100  */ 
  MPGA_rd_resync_t           MPGA_rd_resync;          /* 10101  */

  /* UD - UnReliable Datagram - opcode prefix 011*/      
  MPGA_ud_send_only_t       MPGA_ud_send_only;        /* 01010  */      
  MPGA_ud_send_only_ImmDt_t MPGA_ud_send_only_ImmDt;  /* 01011  */



  /* IBA GLOBAL */
  /* RC - Reliable Connection - opcode prefix 000*/   /* OpCode */      
  MPGA_G_rc_send_first_t       MPGA_G_rc_send_first;      /* 00000  */      
  MPGA_G_rc_send_middle_t      MPGA_G_rc_send_middle;     /* 00001  */      
  MPGA_G_rc_send_last_t        MPGA_G_rc_send_last;       /* 00010  */      
  MPGA_G_rc_send_last_ImmDt_t  MPGA_G_rc_send_last_ImmDt; /* 00011  */            
  MPGA_G_rc_send_only_t        MPGA_G_rc_send_only;       /* 00100  */        
  MPGA_G_rc_send_only_ImmDt_t  MPGA_G_rc_send_only_ImmDt; /* 00101  */            
                                                                         
  MPGA_G_rc_write_first_t      MPGA_G_rc_write_first;     /* 00110  */      
  MPGA_G_rc_write_middle_t     MPGA_G_rc_write_middle;    /* 00111  */      
  MPGA_G_rc_write_last_t       MPGA_G_rc_write_last;      /* 01000  */        
  MPGA_G_rc_write_last_ImmDt_t MPGA_G_rc_write_last_ImmDt;/* 01001  */            
  MPGA_G_rc_write_only_t       MPGA_G_rc_write_only;      /* 01010  */      
  MPGA_G_rc_write_only_ImmDt_t MPGA_G_rc_write_only_ImmDt;/* 01011  */            

  MPGA_G_rc_read_req_t         MPGA_G_rc_read_req;        /* 01100  */
  MPGA_G_rc_read_res_first_t   MPGA_G_rc_read_res_first;  /* 01101  */
  MPGA_G_rc_read_res_middle_t  MPGA_G_rc_read_res_middle; /* 01110  */
  MPGA_G_rc_read_res_last_t    MPGA_G_rc_read_res_last;   /* 01111  */
  MPGA_G_rc_read_res_only_t    MPGA_G_rc_read_res_only;   /* 10000  */
                                                    
  MPGA_G_rc_ack_t              MPGA_G_rc_ack;             /* 10001  */         
  MPGA_G_rc_atomic_ack_t       MPGA_G_rc_atomic_ack;      /* 10010  */ 
  MPGA_G_rc_CmpSwap_t          MPGA_G_rc_CmpSwap;         /* 10011  */         
  MPGA_G_rc_FetchAdd_t         MPGA_G_rc_FetchAdd;        /* 10100  */ 

  /* UC - Unreliable Connection - opcode prefix 001*/
  MPGA_G_uc_send_first_t       MPGA_G_uc_send_first;      /* 00000  */      
  MPGA_G_uc_send_middle_t      MPGA_G_uc_send_middle;     /* 00001  */      
  MPGA_G_uc_send_last_t        MPGA_G_uc_send_last;       /* 00010  */      
  MPGA_G_uc_send_last_ImmDt_t  MPGA_G_uc_send_last_ImmDt; /* 00011  */            
  MPGA_G_uc_send_only_t        MPGA_G_uc_send_only;       /* 00100  */        
  MPGA_G_uc_send_only_ImmDt_t  MPGA_G_uc_send_only_ImmDt; /* 00101  */            
                                                                         
  MPGA_G_uc_write_first_t      MPGA_G_uc_write_first;     /* 00110  */      
  MPGA_G_uc_write_middle_t     MPGA_G_uc_write_middle;    /* 00111  */      
  MPGA_G_uc_write_last_t       MPGA_G_uc_write_last;      /* 01000  */        
  MPGA_G_uc_write_last_ImmDt_t MPGA_G_uc_write_last_ImmDt;/* 01001  */            
  MPGA_G_uc_write_only_t       MPGA_G_uc_write_only;      /* 01010  */      
  MPGA_G_uc_write_only_ImmDt_t MPGA_G_uc_write_only_ImmDt;/* 01011  */

  /* RD - Reliable Datagram - opcode prefix 010*/      
  MPGA_G_rd_send_first_t       MPGA_G_rd_send_first;      /* 00000  */      
  MPGA_G_rd_send_middle_t      MPGA_G_rd_send_middle;     /* 00001  */      
  MPGA_G_rd_send_last_t        MPGA_G_rd_send_last;       /* 00010  */      
  MPGA_G_rd_send_last_ImmDt_t  MPGA_G_rd_send_last_ImmDt; /* 00011  */            
  MPGA_G_rd_send_only_t        MPGA_G_rd_send_only;       /* 00100  */        
  MPGA_G_rd_send_only_ImmDt_t  MPGA_G_rd_send_only_ImmDt; /* 00101  */            
                                                                         
  MPGA_G_rd_write_first_t      MPGA_G_rd_write_first;     /* 00110  */      
  MPGA_G_rd_write_middle_t     MPGA_G_rd_write_middle;    /* 00111  */      
  MPGA_G_rd_write_last_t       MPGA_G_rd_write_last;      /* 01000  */        
  MPGA_G_rd_write_last_ImmDt_t MPGA_G_rd_write_last_ImmDt;/* 01001  */            
  MPGA_G_rd_write_only_t       MPGA_G_rd_write_only;      /* 01010  */      
  MPGA_G_rd_write_only_ImmDt_t MPGA_G_rd_write_only_ImmDt;/* 01011  */            

  MPGA_G_rd_read_req_t         MPGA_G_rd_read_req;        /* 01100  */
  MPGA_G_rd_read_res_first_t   MPGA_G_rd_read_res_first;  /* 01101  */
  MPGA_G_rd_read_res_middle_t  MPGA_G_rd_read_res_middle; /* 01110  */
  MPGA_G_rd_read_res_last_t    MPGA_G_rd_read_res_last;   /* 01111  */
  MPGA_G_rd_read_res_only_t    MPGA_G_rd_read_res_only;   /* 10000  */
                                                    
  MPGA_G_rd_ack_t              MPGA_G_rd_ack;             /* 10001  */         
  MPGA_G_rd_atomic_ack_t       MPGA_G_rd_atomic_ack;      /* 10010  */ 
  MPGA_G_rd_CmpSwap_t          MPGA_G_rd_CmpSwap;         /* 10011  */         
  MPGA_G_rd_FetchAdd_t         MPGA_G_rd_FetchAdd;        /* 10100  */ 
  MPGA_G_rd_resync_t           MPGA_G_rd_resync;          /* 10101  */

  /* UD - UnReliable Datagram - opcode prefix 011*/      
  MPGA_G_ud_send_only_t       MPGA_G_ud_send_only;        /* 01010  */      
  MPGA_G_ud_send_only_ImmDt_t MPGA_G_ud_send_only_ImmDt;  /* 01011  */

}MPGA_headers_t;

#endif /* MPGA_headers_H */
