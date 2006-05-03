/******************************************************************************
 *     Copyright (C)  2003 -2005 QLogic Corporation
 * QLogic ISP4xxx Device Driver
 *
 * This program includes a device driver for Linux 2.6.x that may be
 * distributed with QLogic hardware specific firmware binary file.
 * You may modify and redistribute the device driver code under the
 * GNU General Public License as published by the Free Software Foundation
 * (version 2 or a later version) and/or under the following terms,
 * as applicable:
 *
 * 	1. Redistribution of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in
 *         the documentation and/or other materials provided with the
 *         distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 * 	
 * You may redistribute the hardware specific firmware binary file under
 * the following terms:
 * 	1. Redistribution of source code (only if applicable), must
 *         retain the above copyright notice, this list of conditions and
 *         the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials provided
 *         with the distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 *
 * REGARDLESS OF WHAT LICENSING MECHANISM IS USED OR APPLICABLE,
 * THIS PROGRAM IS PROVIDED BY QLOGIC CORPORATION "AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * USER ACKNOWLEDGES AND AGREES THAT USE OF THIS PROGRAM WILL NOT CREATE
 * OR GIVE GROUNDS FOR A LICENSE BY IMPLICATION, ESTOPPEL, OR OTHERWISE
 * IN ANY INTELLECTUAL PROPERTY RIGHTS (PATENT, COPYRIGHT, TRADE SECRET,
 * MASK WORK, OR OTHER PROPRIETARY RIGHT) EMBODIED IN ANY OTHER QLOGIC
 * HARDWARE OR SOFTWARE EITHER SOLELY OR IN COMBINATION WITH THIS PROGRAM
 *
 ******************************************************************************
 *
 * This file defines mailbox structures and definitions for the QLA4xxx
 *  iSCSI HBA firmware.
 */

#ifndef _QLA4X_FW_H
#define _QLA4X_FW_H

#ifndef INT8
#define INT8 __s8
#endif
#ifndef INT16
#define INT16 __s16
#endif
#ifndef INT32
#define INT32 __s32
#endif
#ifndef UINT8
#define UINT8 __u8
#endif
#ifndef UINT16
#define UINT16 __u16
#endif
#ifndef UINT32
#define UINT32 __u32
#endif
#ifndef UINT64
#define UINT64 __u64
#endif


#define QLA4XXX_VENDOR_ID   	0x1077
#define QLA4000_DEVICE_ID  	0x4000
#define QLA4010_DEVICE_ID  	0x4010

#define QLA4040_SSDID_NIC  	0x011D	/* Uses QLA4010 PCI Device ID */
#define QLA4040_SSDID_ISCSI  	0x011E
#define QLA4040C_SSDID_NIC  	0x011F
#define QLA4040C_SSDID_ISCSI  	0x0120

#define MAX_PRST_DEV_DB_ENTRIES         64
#define MIN_DISC_DEV_DB_ENTRY           MAX_PRST_DEV_DB_ENTRIES
#define MAX_DEV_DB_ENTRIES              512
#define MAX_ISNS_DISCOVERED_TARGETS     MAX_DEV_DB_ENTRIES

/* ISP Maximum number of DSD per command */
#define DSD_MAX                                 1024

/* FW check */
#define FW_UP(reg,stat)                         (((stat = RD_REG_DWORD(reg->mailbox[0])) != 0) && (stat != 0x0007))

#define INVALID_REGISTER 			((UINT32)-1)

#define ISP4010_NET_FUNCTION                            0
#define ISP4010_ISCSI_FUNCTION                          1


/*************************************************************************
 *
 * 		ISP 4010 I/O Register Set Structure and Definitions
 *
 *************************************************************************/

typedef struct _PORT_CTRL_STAT_REGS {
	UINT32  ext_hw_conf;      		/*  80 x50  R/W	*/
	UINT32  intChipConfiguration;	        /*  84 x54   *  */
	UINT32  port_ctrl;		        /*  88 x58   *  */
	UINT32  port_status;		        /*  92 x5c   *  */
	UINT32  HostPrimMACHi;		        /*  96 x60   *  */
	UINT32  HostPrimMACLow;		        /* 100 x64   *  */
	UINT32  HostSecMACHi;		        /* 104 x68   *  */
	UINT32  HostSecMACLow;		        /* 108 x6c   *  */
	UINT32  EPPrimMACHi;		        /* 112 x70   *  */
	UINT32  EPPrimMACLow;		        /* 116 x74   *  */
	UINT32  EPSecMACHi;		        /* 120 x78   *  */
	UINT32  EPSecMACLow;		        /* 124 x7c   *  */
	UINT32  HostPrimIPHi;		        /* 128 x80   *  */
	UINT32  HostPrimIPMidHi;	        /* 132 x84   *  */
	UINT32  HostPrimIPMidLow;	        /* 136 x88   *  */
	UINT32  HostPrimIPLow;		        /* 140 x8c   *  */
	UINT32  HostSecIPHi;		        /* 144 x90   *  */
	UINT32  HostSecIPMidHi;		        /* 148 x94   *  */
	UINT32  HostSecIPMidLow;	        /* 152 x98   *  */
	UINT32  HostSecIPLow;		        /* 156 x9c   *  */
	UINT32  EPPrimIPHi;		        /* 160 xa0   *  */
	UINT32  EPPrimIPMidHi;		        /* 164 xa4   *  */
	UINT32  EPPrimIPMidLow;		        /* 168 xa8   *  */
	UINT32  EPPrimIPLow;		        /* 172 xac   *  */
	UINT32  EPSecIPHi;		        /* 176 xb0   *  */
	UINT32  EPSecIPMidHi;		        /* 180 xb4   *  */
	UINT32  EPSecIPMidLow;		        /* 184 xb8   *  */
	UINT32  EPSecIPLow;		        /* 188 xbc   *  */
	UINT32  IPReassemblyTimeout;	        /* 192 xc0   *  */
	UINT32  EthMaxFramePayload;	        /* 196 xc4   *  */
	UINT32  TCPMaxWindowSize;	        /* 200 xc8   *  */
	UINT32  TCPCurrentTimestampHi;	        /* 204 xcc   *  */
	UINT32  TCPCurrentTimestampLow;	        /* 208 xd0   *  */
	UINT32  LocalRAMAddress;	        /* 212 xd4   *  */
	UINT32  LocalRAMData;		        /* 216 xd8   *  */
	UINT32  PCSReserved1;		        /* 220 xdc   *  */
	UINT32  gp_out;	       			/* 224 xe0   *  */
	UINT32  gp_in;	       			/* 228 xe4   *  */
	UINT32  ProbeMuxAddr;		       	/* 232 xe8   *  */
	UINT32  ProbeMuxData;		       	/* 236 xec   *  */
	UINT32  ERMQueueBaseAddr0;	       	/* 240 xf0   *  */
	UINT32  ERMQueueBaseAddr1;	       	/* 244 xf4   *  */
	UINT32  MACConfiguration;	       	/* 248 xf8   *  */
	UINT32  port_err_status;	       	/* 252 xfc  COR */
} PORT_CTRL_STAT_REGS, *PPORT_CTRL_STAT_REGS;

typedef struct _HOST_MEM_CFG_REGS {
	UINT32  NetRequestQueueOut;	       	   /*  80 x50   *  */
	UINT32  NetRequestQueueOutAddrHi;      	   /*  84 x54   *  */
	UINT32  NetRequestQueueOutAddrLow;     	   /*  88 x58   *  */
	UINT32  NetRequestQueueBaseAddrHi;     	   /*  92 x5c   *  */
	UINT32  NetRequestQueueBaseAddrLow;    	   /*  96 x60   *  */
	UINT32  NetRequestQueueLength;	       	   /* 100 x64   *  */
	UINT32  NetResponseQueueIn;	       	   /* 104 x68   *  */
	UINT32  NetResponseQueueInAddrHi;      	   /* 108 x6c   *  */
	UINT32  NetResponseQueueInAddrLow;     	   /* 112 x70   *  */
	UINT32  NetResponseQueueBaseAddrHi;    	   /* 116 x74   *  */
	UINT32  NetResponseQueueBaseAddrLow;   	   /* 120 x78   *  */
	UINT32  NetResponseQueueLength;	       	   /* 124 x7c   *  */
	UINT32  req_q_out;	       	       	   /* 128 x80   *  */
	UINT32  RequestQueueOutAddrHi;	       	   /* 132 x84   *  */
	UINT32  RequestQueueOutAddrLow;	       	   /* 136 x88   *  */
	UINT32  RequestQueueBaseAddrHi;	       	   /* 140 x8c   *  */
	UINT32  RequestQueueBaseAddrLow;       	   /* 144 x90   *  */
	UINT32  RequestQueueLength;	       	   /* 148 x94   *  */
	UINT32  ResponseQueueIn;	       	   /* 152 x98   *  */
	UINT32  ResponseQueueInAddrHi;	       	   /* 156 x9c   *  */
	UINT32  ResponseQueueInAddrLow;	       	   /* 160 xa0   *  */
	UINT32  ResponseQueueBaseAddrHi;       	   /* 164 xa4   *  */
	UINT32  ResponseQueueBaseAddrLow;      	   /* 168 xa8   *  */
	UINT32  ResponseQueueLength;	       	   /* 172 xac   *  */
	UINT32  NetRxLargeBufferQueueOut;      	   /* 176 xb0   *  */
	UINT32  NetRxLargeBufferQueueBaseAddrHi;   /* 180 xb4   *  */
	UINT32  NetRxLargeBufferQueueBaseAddrLow;  /* 184 xb8   *  */
	UINT32  NetRxLargeBufferQueueLength;   	   /* 188 xbc   *  */
	UINT32  NetRxLargeBufferLength;	       	   /* 192 xc0   *  */
	UINT32  NetRxSmallBufferQueueOut;      	   /* 196 xc4   *  */
	UINT32  NetRxSmallBufferQueueBaseAddrHi;   /* 200 xc8   *  */
	UINT32  NetRxSmallBufferQueueBaseAddrLow;  /* 204 xcc   *  */
	UINT32  NetRxSmallBufferQueueLength;   	   /* 208 xd0   *  */
	UINT32  NetRxSmallBufferLength;	       	   /* 212 xd4   *  */
	UINT32  HMCReserved0[10];	       	   /* 216 xd8   *  */
} HOST_MEM_CFG_REGS, *PHOST_MEM_CFG_REGS;

typedef struct _LOCAL_RAM_CFG_REGS {
	UINT32  BufletSize;		       	/*  80 x50   *  */
	UINT32  BufletMaxCount;		       	/*  84 x54   *  */
	UINT32  BufletCurrCount;	       	/*  88 x58   *  */
	UINT32  BufletPauseThresholdCount;     	/*  92 x5c   *  */
	UINT32  BufletTCPWinThresholdHi;       	/*  96 x60   *  */
	UINT32  BufletTCPWinThresholdLow;      	/* 100 x64   *  */
	UINT32  IPHashTableBaseAddr;	       	/* 104 x68   *  */
	UINT32  IPHashTableSize;	       	/* 108 x6c   *  */
	UINT32  TCPHashTableBaseAddr;	       	/* 112 x70   *  */
	UINT32  TCPHashTableSize;	       	/* 116 x74   *  */
	UINT32  NCBAreaBaseAddr;	       	/* 120 x78   *  */
	UINT32  NCBMaxCount;		       	/* 124 x7c   *  */
	UINT32  NCBCurrCount;		       	/* 128 x80   *  */
	UINT32  DRBAreaBaseAddr;	       	/* 132 x84   *  */
	UINT32  DRBMaxCount;		       	/* 136 x88   *  */
	UINT32  DRBCurrCount;		       	/* 140 x8c   *  */
	UINT32  LRCReserved[28];	       	/* 144 x90   *  */
} LOCAL_RAM_CFG_REGS, *PLOCAL_RAM_CFG_REGS;

typedef struct _PROT_STAT_REGS {
	UINT32  MACTxFrameCount;	       /*  80 x50   R   */
	UINT32  MACTxByteCount;		       /*  84 x54   R   */
	UINT32  MACRxFrameCount;	       /*  88 x58   R   */
	UINT32  MACRxByteCount;		       /*  92 x5c   R   */
	UINT32  MACCRCErrCount;		       /*  96 x60   R   */
	UINT32  MACEncErrCount;		       /* 100 x64   R   */
	UINT32  MACRxLengthErrCount;	       /* 104 x68   R   */
	UINT32  IPTxPacketCount;	       /* 108 x6c   R   */
	UINT32  IPTxByteCount;		       /* 112 x70   R   */
	UINT32  IPTxFragmentCount;	       /* 116 x74   R   */
	UINT32  IPRxPacketCount;	       /* 120 x78   R   */
	UINT32  IPRxByteCount;		       /* 124 x7c   R   */
	UINT32  IPRxFragmentCount;	       /* 128 x80   R   */
	UINT32  IPDatagramReassemblyCount;     /* 132 x84   R   */
	UINT32  IPV6RxPacketCount;	       /* 136 x88   R   */
	UINT32  IPErrPacketCount;	       /* 140 x8c   R   */
	UINT32  IPReassemblyErrCount;	       /* 144 x90   R   */
	UINT32  TCPTxSegmentCount;	       /* 148 x94   R   */
	UINT32  TCPTxByteCount;		       /* 152 x98   R   */
	UINT32  TCPRxSegmentCount;	       /* 156 x9c   R   */
	UINT32  TCPRxByteCount;		       /* 160 xa0   R   */
	UINT32  TCPTimerExpCount;	       /* 164 xa4   R   */
	UINT32  TCPRxAckCount;		       /* 168 xa8   R   */
	UINT32  TCPTxAckCount;		       /* 172 xac   R   */
	UINT32  TCPRxErrOOOCount;	       /* 176 xb0   R   */
	UINT32  PSReserved0;		       /* 180 xb4   *   */
	UINT32  TCPRxWindowProbeUpdateCount;   /* 184 xb8   R   */
	UINT32  ECCErrCorrectionCount;	       /* 188 xbc   R   */
	UINT32  PSReserved1[16];	       /* 192 xc0   *   */
} PROT_STAT_REGS, *PPROT_STAT_REGS;

#define MBOX_REG_COUNT                          8

/* remote register set (access via PCI memory read/write) */
typedef struct isp_reg_t {
	uint32_t mailbox[MBOX_REG_COUNT];

	uint32_t flash_address;				/* 0x20 */
	uint32_t flash_data;
	uint32_t ctrl_status;

	union {
		struct {
			uint32_t nvram;
			uint32_t reserved1[2];		/* 0x30 */
		} __attribute__((packed)) isp4010;
		struct {
			uint32_t intr_mask;
			uint32_t nvram;			/* 0x30 */
			uint32_t semaphore;
		} __attribute__((packed)) isp4022;
	} u1;

	
	uint32_t req_q_in;  /* SCSI Request Queue Producer Index */
	uint32_t rsp_q_out; /* SCSI Completion Queue Consumer Index */

	uint32_t reserved2[4];				/* 0x40 */

	union {
		struct {
			uint32_t ext_hw_conf;		/* 0x50 */
			uint32_t flow_ctrl;
			uint32_t port_ctrl;
			uint32_t port_status;

			uint32_t reserved3[8];		/* 0x60 */

			uint32_t req_q_out;		/* 0x80 */

			uint32_t reserved4[23];		/* 0x84 */

			uint32_t gp_out;		/* 0xe0 */
			uint32_t gp_in;

			uint32_t reserved5[5];

			uint32_t port_err_status;	/* 0xfc */
		} __attribute__((packed)) isp4010;
		struct {
			union {
				PORT_CTRL_STAT_REGS p0;
				HOST_MEM_CFG_REGS   p1;
				LOCAL_RAM_CFG_REGS  p2;
				PROT_STAT_REGS	    p3;
				uint32_t  r_union[44];
			};

		} __attribute__((packed)) isp4022;
	} u2;
} isp_reg_t;	/* 256 x100 */

#define ISP_SEMAPHORE(ha) \
	(IS_QLA4022(ha) ? \
	 &ha->reg->u1.isp4022.semaphore : \
	 &ha->reg->u1.isp4010.nvram)

#define ISP_NVRAM(ha) \
	(IS_QLA4022(ha) ? \
	 &ha->reg->u1.isp4022.nvram : \
	 &ha->reg->u1.isp4010.nvram)

#define ISP_EXT_HW_CONF(ha) \
	(IS_QLA4022(ha) ? \
	 &ha->reg->u2.isp4022.p0.ext_hw_conf : \
	 &ha->reg->u2.isp4010.ext_hw_conf)

#define ISP_PORT_STATUS(ha) \
	(IS_QLA4022(ha) ? \
	 &ha->reg->u2.isp4022.p0.port_status : \
	 &ha->reg->u2.isp4010.port_status)

#define ISP_PORT_CTRL(ha) \
	(IS_QLA4022(ha) ? \
	 &ha->reg->u2.isp4022.p0.port_ctrl : \
	 &ha->reg->u2.isp4010.port_ctrl)

#define ISP_REQ_Q_OUT(ha) \
	(IS_QLA4022(ha) ? \
	 &ha->reg->u2.isp4022.p1.req_q_out : \
	 &ha->reg->u2.isp4010.req_q_out)

#define ISP_PORT_ERROR_STATUS(ha) \
	(IS_QLA4022(ha) ? \
	 &ha->reg->u2.isp4022.p0.port_err_status : \
	 &ha->reg->u2.isp4010.port_err_status)

#define ISP_GP_OUT(ha) \
	(IS_QLA4022(ha) ? \
	 &ha->reg->u2.isp4022.p0.gp_out : \
	 &ha->reg->u2.isp4010.gp_out)

#define ISP_GP_IN(ha) \
	(IS_QLA4022(ha) ? \
	 &ha->reg->u2.isp4022.p0.gp_in : \
	 &ha->reg->u2.isp4010.gp_in)

/* Semaphore Defines for 4010 */
#define QL4010_DRVR_SEM_BITS    0x00000030
#define QL4010_GPIO_SEM_BITS    0x000000c0
#define QL4010_SDRAM_SEM_BITS   0x00000300
#define QL4010_PHY_SEM_BITS     0x00000c00
#define QL4010_NVRAM_SEM_BITS   0x00003000
#define QL4010_FLASH_SEM_BITS   0x0000c000

#define QL4010_DRVR_SEM_MASK    0x00300000
#define QL4010_GPIO_SEM_MASK    0x00c00000
#define QL4010_SDRAM_SEM_MASK   0x03000000
#define QL4010_PHY_SEM_MASK     0x0c000000
#define	QL4010_NVRAM_SEM_MASK	0x30000000
#define QL4010_FLASH_SEM_MASK   0xc0000000


/* Semaphore Defines for 4022 */
#define QL4022_RESOURCE_MASK_BASE_CODE 0x7
#define QL4022_RESOURCE_BITS_BASE_CODE 0x4

#define QL4022_DRVR_SEM_BITS    (QL4022_RESOURCE_BITS_BASE_CODE << 1)
#define QL4022_DDR_RAM_SEM_BITS (QL4022_RESOURCE_BITS_BASE_CODE << 4)
#define QL4022_PHY_GIO_SEM_BITS (QL4022_RESOURCE_BITS_BASE_CODE << 7)
#define QL4022_NVRAM_SEM_BITS   (QL4022_RESOURCE_BITS_BASE_CODE << 10)
#define QL4022_FLASH_SEM_BITS   (QL4022_RESOURCE_BITS_BASE_CODE << 13)

#define QL4022_DRVR_SEM_MASK    (QL4022_RESOURCE_MASK_BASE_CODE << (1+16))
#define QL4022_DDR_RAM_SEM_MASK (QL4022_RESOURCE_MASK_BASE_CODE << (4+16))
#define QL4022_PHY_GIO_SEM_MASK (QL4022_RESOURCE_MASK_BASE_CODE << (7+16))
#define QL4022_NVRAM_SEM_MASK   (QL4022_RESOURCE_MASK_BASE_CODE << (10+16))
#define QL4022_FLASH_SEM_MASK   (QL4022_RESOURCE_MASK_BASE_CODE << (13+16))


#define QL4XXX_LOCK_FLASH(a)    \
	(IS_QLA4022(a) ? \
    ql4xxx_sem_spinlock(a, QL4022_FLASH_SEM_MASK, (QL4022_RESOURCE_BITS_BASE_CODE | (a->mac_index)) << 13) : \
    ql4xxx_sem_spinlock(a, QL4010_FLASH_SEM_MASK, QL4010_FLASH_SEM_BITS) )

#define QL4XXX_LOCK_NVRAM(a)    \
	(IS_QLA4022(a) ? \
	ql4xxx_sem_spinlock(a, QL4022_NVRAM_SEM_MASK, (QL4022_RESOURCE_BITS_BASE_CODE | (a->mac_index)) << 10) : \
	ql4xxx_sem_spinlock(a, QL4010_NVRAM_SEM_MASK, QL4010_NVRAM_SEM_BITS) )

#define QL4XXX_LOCK_GIO(a) \
	(IS_QLA4022(a) ? \
	ql4xxx_sem_spinlock(a, QL4022_PHY_GIO_SEM_MASK, (QL4022_RESOURCE_BITS_BASE_CODE | (a->mac_index)) << 7) : \
	ql4xxx_sem_spinlock(a, QL4010_GPIO_SEM_MASK, QL4010_GPIO_SEM_BITS) )

#define QL4XXX_LOCK_PHY(a) \
	(IS_QLA4022(a) ? \
	ql4xxx_sem_spinlock(a, QL4022_PHY_GIO_SEM_MASK, (QL4022_RESOURCE_BITS_BASE_CODE | (a->mac_index)) << 7) : \
	ql4xxx_sem_spinlock(a, QL4010_PHY_SEM_MASK, QL4010_PHY_SEM_BITS) )

#define QL4XXX_LOCK_DDR_RAM(a)  \
	(IS_QLA4022(a) ? \
	ql4xxx_sem_spinlock(a, QL4022_DDR_RAM_SEM_MASK, (QL4022_RESOURCE_BITS_BASE_CODE | (a->mac_index)) << 4) : \
	ql4xxx_sem_spinlock(a, QL4010_SDRAM_SEM_MASK, QL4010_SDRAM_SEM_BITS) )

#define QL4XXX_LOCK_DRVR(a)  \
	(IS_QLA4022(a) ? \
	ql4xxx_sem_lock(a, QL4022_DRVR_SEM_MASK, (QL4022_RESOURCE_BITS_BASE_CODE | (a->mac_index)) << 1) : \
	ql4xxx_sem_lock(a, QL4010_DRVR_SEM_MASK, QL4010_DRVR_SEM_BITS) )

#define QL4XXX_UNLOCK_DRVR(a) \
	(IS_QLA4022(a) ? \
	ql4xxx_sem_unlock(a, QL4022_DRVR_SEM_MASK) : \
	ql4xxx_sem_unlock(a, QL4010_DRVR_SEM_MASK) )

#define QL4XXX_UNLOCK_GIO(a) \
	(IS_QLA4022(a) ? \
	ql4xxx_sem_unlock(a, QL4022_PHY_GIO_SEM_MASK) : \
	ql4xxx_sem_unlock(a, QL4010_GPIO_SEM_MASK) )

#define QL4XXX_UNLOCK_DDR_RAM(a)  \
	(IS_QLA4022(a) ? \
	ql4xxx_sem_unlock(a, QL4022_DDR_RAM_SEM_MASK) : \
	ql4xxx_sem_unlock(a, QL4010_SDRAM_SEM_MASK) )

#define QL4XXX_UNLOCK_PHY(a) \
	(IS_QLA4022(a) ? \
	ql4xxx_sem_unlock(a, QL4022_PHY_GIO_SEM_MASK) : \
	ql4xxx_sem_unlock(a, QL4010_PHY_SEM_MASK) )

#define QL4XXX_UNLOCK_NVRAM(a) \
	(IS_QLA4022(a) ? \
	ql4xxx_sem_unlock(a, QL4022_NVRAM_SEM_MASK) : \
	ql4xxx_sem_unlock(a, QL4010_NVRAM_SEM_MASK) )

#define QL4XXX_UNLOCK_FLASH(a)  \
	(IS_QLA4022(a) ? \
	ql4xxx_sem_unlock(a, QL4022_FLASH_SEM_MASK) : \
	ql4xxx_sem_unlock(a, QL4010_FLASH_SEM_MASK) )


#define QL4XXX_LOCK_DRVR_WAIT(a)                                                           \
{																					   \
    int i = 0;																			   \
    while(1) {																		   \
        if(QL4XXX_LOCK_DRVR(a) == 0) {										               \
	    set_current_state(TASK_UNINTERRUPTIBLE); \
	    schedule_timeout(10); \
            if(!i) {																   \
                DEBUG2(printk("scsi%d: %s: Waiting for Global Init Semaphore...\n",a->host_no,__func__)); \
                i++;																   \
            }																		   \
        }																			   \
        else {																		   \
            DEBUG2(printk("scsi%d: %s: Global Init Semaphore acquired.\n",a->host_no,__func__));		   \
            break;																	   \
        }																			   \
    }																				   \
}
/* Page # defines for 4022 */
#define PORT_CTRL_STAT_PAGE                     0 /* 4022 */
#define HOST_MEM_CFG_PAGE                       1 /* 4022 */
#define LOCAL_RAM_CFG_PAGE                      2 /* 4022 */
#define PROT_STAT_PAGE                          3 /* 4022 */

/* Register Mask - sets corresponding mask bits in the upper word */
#define SET_RMASK(val)	((val & 0xffff) | (val << 16))
#define CLR_RMASK(val)	(0 | (val << 16))

/* ctrl_status definitions */
#define CSR_SCSI_PAGE_SELECT                    0x00000003
#define CSR_SCSI_INTR_ENABLE                    0x00000004 /* 4010 */
#define CSR_SCSI_RESET_INTR                     0x00000008
#define CSR_SCSI_COMPLETION_INTR                0x00000010
#define CSR_SCSI_PROCESSOR_INTR                 0x00000020
#define CSR_INTR_RISC                           0x00000040
#define CSR_BOOT_ENABLE                         0x00000080
#define CSR_NET_PAGE_SELECT                     0x00000300 /* 4010 */
#define CSR_NET_INTR_ENABLE                     0x00000400 /* 4010 */
#define CSR_FUNC_NUM                            0x00000700 /* 4022 */
#define CSR_PCI_FUNC_NUM_MASK                   0x00000300 /* 4022 */
#define CSR_NET_RESET_INTR                      0x00000800 /* 4010 */
#define CSR_NET_COMPLETION_INTR                 0x00001000 /* 4010 */
#define CSR_FORCE_SOFT_RESET                    0x00002000 /* 4022 */
#define CSR_FATAL_ERROR                         0x00004000
#define CSR_SOFT_RESET                          0x00008000
#define ISP_CONTROL_FN_MASK     		CSR_FUNC_NUM
#define ISP_CONTROL_FN0_NET     		0x0400
#define ISP_CONTROL_FN0_SCSI    		0x0500
#define ISP_CONTROL_FN1_NET     		0x0600
#define ISP_CONTROL_FN1_SCSI    		0x0700

#define INTR_PENDING                            (CSR_SCSI_COMPLETION_INTR | CSR_SCSI_PROCESSOR_INTR | CSR_SCSI_RESET_INTR)

/* ISP InterruptMask definitions */
#define IMR_SCSI_INTR_ENABLE                    0x00000004  /* 4022 */

/* ISP 4022 nvram definitions */
#define NVR_WRITE_ENABLE			0x00000010  /* 4022 */

/* ISP port_ctrl definitions */
#define PCR_CONFIG_COMPLETE			0x00008000  /* 4022 */
#define PCR_BIOS_BOOTED_FIRMWARE		0x00008000  /* 4010 */
#define PCR_ENABLE_SERIAL_DATA			0x00001000  /* 4010 */
#define PCR_SERIAL_DATA_OUT			0x00000800  /* 4010 */
#define PCR_ENABLE_SERIAL_CLOCK			0x00000400  /* 4010 */
#define PCR_SERIAL_CLOCK			0x00000200  /* 4010 */

/* ISP port_status definitions */
#define PSR_CONFIG_COMPLETE			0x00000001  /* 4010 */
#define PSR_INIT_COMPLETE			0x00000200

/* ISP Semaphore definitions */
#define SR_FIRWMARE_BOOTED			0x00000001

/* ISP General Purpose Output definitions */
#define GPOR_TOPCAT_RESET                  	0x00000004

/* shadow registers (DMA'd from HA to system memory.  read only) */
typedef struct {
	/* SCSI Request Queue Consumer Index */
	UINT32   req_q_out;	/* 0 x0   R  */

	/* SCSI Completion Queue Producer Index */
	UINT32   rsp_q_in;	/* 4 x4   R  */
} shadow_regs_t;		/* 8 x8	     */

#define EHWC_PROT_METHOD_NONE                         0
#define EHWC_PROT_METHOD_BYTE_PARITY                  1
#define EHWC_PROT_METHOD_ECC                          2
#define EHWC_SDRAM_BANKS_1                            0
#define EHWC_SDRAM_BANKS_2                            1
#define EHWC_SDRAM_WIDTH_8_BIT                        0
#define EHWC_SDRAM_WIDTH_16_BIT                       1
#define EHWC_SDRAM_CHIP_SIZE_64MB                     0
#define EHWC_SDRAM_CHIP_SIZE_128MB                    1
#define EHWC_SDRAM_CHIP_SIZE_256MB                    2
#define EHWC_MEM_TYPE_SYNC_FLOWTHROUGH                0
#define EHWC_MEM_TYPE_SYNC_PIPELINE                   1
#define EHWC_WRITE_BURST_512                          0
#define EHWC_WRITE_BURST_1024                         1
#define EHWC_WRITE_BURST_2048                         2
#define EHWC_WRITE_BURST_4096                         3

/* External hardware configuration register */
typedef union _EXTERNAL_HW_CONFIG_REG {
	struct {
		UINT32  bReserved0                :1;
		UINT32  bSDRAMProtectionMethod    :2;
		UINT32  bSDRAMBanks               :1;
		UINT32  bSDRAMChipWidth           :1;
		UINT32  bSDRAMChipSize            :2;
		UINT32  bParityDisable            :1;
		UINT32  bExternalMemoryType       :1;
		UINT32  bFlashBIOSWriteEnable     :1;
		UINT32  bFlashUpperBankSelect     :1;
		UINT32  bWriteBurst               :2;
		UINT32  bReserved1                :3;
		UINT32  bMask                     :16;
	};
	UINT32   AsUINT32;
} EXTERNAL_HW_CONFIG_REG, *PEXTERNAL_HW_CONFIG_REG;

/*************************************************************************
 *
 *		Mailbox Commands Structures and Definitions
 *
 *************************************************************************/

/* Mailbox command definitions */
#define MBOX_CMD_LOAD_RISC_RAM_EXT              0x0001
#define MBOX_CMD_EXECUTE_FW                     0x0002
#define MBOX_CMD_DUMP_RISC_RAM_EXT              0x0003
#define MBOX_CMD_WRITE_RISC_RAM_EXT             0x0004
#define MBOX_CMD_READ_RISC_RAM_EXT              0x0005
#define MBOX_CMD_REGISTER_TEST                  0x0006
#define MBOX_CMD_VERIFY_CHECKSUM                0x0007
#define MBOX_CMD_ABOUT_FW                       0x0009
#define MBOX_CMD_LOOPBACK_DIAG                  0x000A
#define MBOX_CMD_PING                           0x000B
#define MBOX_CMD_CHECKSUM_FW                    0x000E
#define MBOX_CMD_RESET_FW                       0x0014
#define MBOX_CMD_ABORT_TASK                     0x0015
#define MBOX_CMD_LUN_RESET                      0x0016
#define MBOX_CMD_TARGET_WARM_RESET              0x0017
#define MBOX_CMD_TARGET_COLD_RESET              0x0018
#define MBOX_CMD_ABORT_QUEUE                    0x001C
#define MBOX_CMD_GET_QUEUE_STATUS               0x001D
#define MBOX_CMD_GET_MANAGEMENT_DATA            0x001E
#define MBOX_CMD_GET_FW_STATUS                  0x001F
#define MBOX_CMD_SET_ISNS_SERVICE               0x0021
		#define ISNS_DISABLE                            0
		#define ISNS_ENABLE                             1
		#define ISNS_STATUS                             2
#define MBOX_CMD_COPY_FLASH                     0x0024
		#define COPY_FLASH_OPTION_PRIM_TO_SEC           0
		#define COPY_FLASH_OPTION_SEC_TO_PRIM           1
#define MBOX_CMD_WRITE_FLASH                    0x0025
		#define WRITE_FLASH_OPTION_HOLD_DATA            0
		#define WRITE_FLASH_OPTION_COMMIT_DATA          2
		#define WRITE_FLASH_OPTION_FLASH_DATA    	3
#define MBOX_CMD_READ_FLASH                     0x0026
#define MBOX_CMD_GET_QUEUE_PARAMS               0x0029
#define MBOX_CMD_CLEAR_DATABASE_ENTRY           0x0031
#define MBOX_CMD_SET_QUEUE_PARAMS               0x0039
#define MBOX_CMD_CONN_CLOSE_SESS_LOGOUT         0x0056
		#define LOGOUT_OPTION_CLOSE_SESSION             0x01
		#define LOGOUT_OPTION_RELOGIN                   0x02
#define MBOX_CMD_EXECUTE_IOCB_A64		0x005A
#define MBOX_CMD_INITIALIZE_FIRMWARE            0x0060
#define MBOX_CMD_GET_INIT_FW_CTRL_BLOCK         0x0061
#define MBOX_CMD_REQUEST_DATABASE_ENTRY         0x0062
#define MBOX_CMD_SET_DATABASE_ENTRY             0x0063					
#define MBOX_CMD_GET_DATABASE_ENTRY             0x0064
		#define DDB_DS_UNASSIGNED                       0x00
		#define DDB_DS_NO_CONNECTION_ACTIVE             0x01
		#define DDB_DS_DISCOVERY                        0x02
		#define DDB_DS_NO_SESSION_ACTIVE                0x03
		#define DDB_DS_SESSION_ACTIVE                   0x04
		#define DDB_DS_LOGGING_OUT                      0x05
		#define DDB_DS_SESSION_FAILED                   0x06
		#define DDB_DS_LOGIN_IN_PROCESS                 0x07
		#define DELETEABLE_DDB_DS(ds) ((ds == DDB_DS_UNASSIGNED) || \
		                               (ds == DDB_DS_NO_CONNECTION_ACTIVE) || \
					       (ds == DDB_DS_SESSION_FAILED))
#define MBOX_CMD_CLEAR_ACA                      0x0065
#define MBOX_CMD_CLEAR_TASK_SET                 0x0067
#define MBOX_CMD_ABORT_TASK_SET                 0x0068
#define MBOX_CMD_GET_FW_STATE                   0x0069

/* Mailbox 1 */
		#define FW_STATE_READY                          0x0000
		#define FW_STATE_CONFIG_WAIT                    0x0001
		#define FW_STATE_WAIT_LOGIN                     0x0002
		#define FW_STATE_ERROR                          0x0004
		#define FW_STATE_DHCP_IN_PROGRESS		0x0008
		#define FW_STATE_ISNS_IN_PROGRESS               0x0010
		#define FW_STATE_TOPCAT_INIT_IN_PROGRESS       	0x0040

/* Mailbox 3 */
		#define FW_ADDSTATE_COPPER_MEDIA                0x0000
		#define FW_ADDSTATE_OPTICAL_MEDIA               0x0001
		#define	FW_ADDSTATE_DHCP_ENABLED		0x0002
		#define	FW_ADDSTATE_DHCP_LEASE_ACQUIRED		0x0004
		#define	FW_ADDSTATE_DHCP_LEASE_EXPIRED		0x0008
		#define FW_ADDSTATE_LINK_UP                     0x0010
		#define FW_ADDSTATE_ISNS_SVC_ENABLED            0x0020
		#define FW_ADDSTATE_TOPCAT_NOT_INITIALIZED     	0x0040
#define MBOX_CMD_GET_INIT_FW_CTRL_BLOCK_DEFAULTS 0x006A
#define MBOX_CMD_GET_DATABASE_ENTRY_DEFAULTS    0x006B
#define MBOX_CMD_CONN_OPEN_SESS_LOGIN           0x0074
#define MBOX_CMD_DIAGNOSTICS_TEST_RESULTS       0x0075	/* 4010 only */
		#define DIAG_TEST_LOCAL_RAM_SIZE		0x0002
		#define DIAG_TEST_LOCAL_RAM_READ_WRITE		0x0003
		#define DIAG_TEST_RISC_RAM			0x0004
		#define DIAG_TEST_NVRAM				0x0005
		#define DIAG_TEST_FLASH_ROM			0x0006
		#define DIAG_TEST_NW_INT_LOOPBACK		0x0007
		#define DIAG_TEST_NW_EXT_LOOPBACK		0x0008
#define MBOX_CMD_GET_CRASH_RECORD       	0x0076	/* 4010 only */
#define MBOX_CMD_GET_CONN_EVENT_LOG       	0x0077
#define MBOX_CMD_RESTORE_FACTORY_DEFAULTS      	0x0087
#define MBOX_CMD_NOP                            0x00FF

/* Mailbox status definitions */
#define MBOX_COMPLETION_STATUS			4
#define MBOX_STS_BUSY                           0x0007
#define MBOX_STS_INTERMEDIATE_COMPLETION    	0x1000
#define MBOX_STS_COMMAND_COMPLETE               0x4000
#define MBOX_STS_INVALID_COMMAND                0x4001
#define MBOX_STS_HOST_INTERFACE_ERROR           0x4002
#define MBOX_STS_TEST_FAILED                    0x4003
#define MBOX_STS_COMMAND_ERROR                  0x4005
#define MBOX_STS_COMMAND_PARAMETER_ERROR        0x4006
#define MBOX_STS_TARGET_MODE_INIT_FAIL          0x4007
#define MBOX_STS_INITIATOR_MODE_INIT_FAIL       0x4008

#define MBOX_ASYNC_EVENT_STATUS			8
#define MBOX_ASTS_SYSTEM_ERROR                  0x8002
#define MBOX_ASTS_REQUEST_TRANSFER_ERROR        0x8003
#define MBOX_ASTS_RESPONSE_TRANSFER_ERROR       0x8004
#define MBOX_ASTS_PROTOCOL_STATISTIC_ALARM      0x8005
#define MBOX_ASTS_SCSI_COMMAND_PDU_REJECTED     0x8006
#define MBOX_ASTS_LINK_UP  			0x8010
#define MBOX_ASTS_LINK_DOWN			0x8011
#define MBOX_ASTS_DATABASE_CHANGED              0x8014
#define MBOX_ASTS_UNSOLICITED_PDU_RECEIVED      0x8015
#define MBOX_ASTS_SELF_TEST_FAILED      	0x8016
#define MBOX_ASTS_LOGIN_FAILED      		0x8017
#define MBOX_ASTS_DNS      			0x8018
#define MBOX_ASTS_HEARTBEAT      		0x8019
#define MBOX_ASTS_NVRAM_INVALID      		0x801A
#define MBOX_ASTS_MAC_ADDRESS_CHANGED      	0x801B
#define MBOX_ASTS_IP_ADDRESS_CHANGED      	0x801C
#define MBOX_ASTS_DHCP_LEASE_EXPIRED      	0x801D
#define MBOX_ASTS_DHCP_LEASE_ACQUIRED           0x801F
#define MBOX_ASTS_ISNS_UNSOLICITED_PDU_RECEIVED 0x8021
		#define ISNS_EVENT_DATA_RECEIVED		0x0000
		#define ISNS_EVENT_CONNECTION_OPENED		0x0001
		#define ISNS_EVENT_CONNECTION_FAILED		0x0002
#define MBOX_ASTS_IPSEC_SYSTEM_FATAL_ERROR	0x8022
#define MBOX_ASTS_SUBNET_STATE_CHANGE		0x8027


/*************************************************************************/

/* Host Adapter Initialization Control Block (from host) */
typedef struct _INIT_FW_CTRL_BLK {
	UINT8   Version;			/* 00 */
	UINT8   Control;			/* 01 */

	UINT16  FwOptions;			/* 02-03 */
   #define  FWOPT_HEARTBEAT_ENABLE           0x1000
   #define  FWOPT_MARKER_DISABLE             0x0400
   #define  FWOPT_PROTOCOL_STAT_ALARM_ENABLE 0x0200
   #define  FWOPT_TARGET_ACCEPT_AEN_ENABLE   0x0100
   #define  FWOPT_ACCESS_CONTROL_ENABLE      0x0080
   #define  FWOPT_SESSION_MODE               0x0040
   #define  FWOPT_INITIATOR_MODE             0x0020
   #define  FWOPT_TARGET_MODE                0x0010
   #define  FWOPT_FAST_POSTING               0x0008
   #define  FWOPT_AUTO_TARGET_INFO_DISABLE   0x0004
   #define  FWOPT_SENSE_BUFFER_DATA_ENABLE   0x0002

	UINT16  ExecThrottle;			/* 04-05 */
	UINT8   RetryCount;			/* 06 */
	UINT8   RetryDelay;			/* 07 */
	UINT16  MaxEthFrPayloadSize;		/* 08-09 */
	UINT16  AddFwOptions;			/* 0A-0B */
   #define  ADDFWOPT_AUTOCONNECT_DISABLE     0x0002
   #define  ADDFWOPT_SUSPEND_ON_FW_ERROR     0x0001

	UINT8   HeartbeatInterval;		/* 0C */
	UINT8   InstanceNumber;			/* 0D */
	UINT16  RES2;				/* 0E-0F */
	UINT16  ReqQConsumerIndex;		/* 10-11 */
	UINT16  ComplQProducerIndex;		/* 12-13 */
	UINT16  ReqQLen;			/* 14-15 */
	UINT16  ComplQLen;			/* 16-17 */
	UINT32  ReqQAddrLo;			/* 18-1B */
	UINT32  ReqQAddrHi;			/* 1C-1F */
	UINT32  ComplQAddrLo;			/* 20-23 */
	UINT32  ComplQAddrHi;			/* 24-27 */
	UINT32  ShadowRegBufAddrLo;		/* 28-2B */
	UINT32  ShadowRegBufAddrHi;		/* 2C-2F */

	UINT16  iSCSIOptions;			/* 30-31 */
   #define  IOPT_RCV_ISCSI_MARKER_ENABLE     0x8000
   #define  IOPT_SEND_ISCSI_MARKER_ENABLE    0x4000
   #define  IOPT_HEADER_DIGEST_ENABLE        0x2000
   #define  IOPT_DATA_DIGEST_ENABLE          0x1000
   #define  IOPT_IMMEDIATE_DATA_ENABLE       0x0800
   #define  IOPT_INITIAL_R2T_ENABLE          0x0400
   #define  IOPT_DATA_SEQ_IN_ORDER           0x0200
   #define  IOPT_DATA_PDU_IN_ORDER           0x0100
   #define  IOPT_CHAP_AUTH_ENABLE            0x0080
   #define  IOPT_SNACK_REQ_ENABLE            0x0040
   #define  IOPT_DISCOVERY_LOGOUT_ENABLE     0x0020
   #define  IOPT_BIDIR_CHAP_ENABLE     	     0x0010

	UINT16  TCPOptions;			/* 32-33 */
   #define  TOPT_ISNS_ENABLE		     0x4000
   #define  TOPT_SLP_USE_DA_ENABLE	     0x2000
   #define  TOPT_AUTO_DISCOVERY_ENABLE       0x1000
   #define  TOPT_SLP_UA_ENABLE               0x0800
   #define  TOPT_SLP_SA_ENABLE               0x0400
   #define  TOPT_DHCP_ENABLE                 0x0200
   #define  TOPT_GET_DNS_VIA_DHCP_ENABLE     0x0100
   #define  TOPT_GET_SLP_VIA_DHCP_ENABLE     0x0080
   #define  TOPT_LEARN_ISNS_IP_ADDR_ENABLE   0x0040
   #define  TOPT_NAGLE_DISABLE               0x0020
   #define  TOPT_TIMER_SCALE_MASK            0x000E
   #define  TOPT_TIME_STAMP_ENABLE           0x0001

	UINT16  IPOptions;			/* 34-35 */
   #define  IPOPT_FRAG_DISABLE               0x0010
   #define  IPOPT_PAUSE_FRAME_ENABLE         0x0002
   #define  IPOPT_IP_ADDRESS_VALID           0x0001

	UINT16  MaxPDUSize;			/* 36-37 */
	UINT16  RcvMarkerInt;			/* 38-39 */
	UINT16  SndMarkerInt;			/* 3A-3B */
	UINT16  InitMarkerlessInt;		/* 3C-3D */
	UINT16  FirstBurstSize;			/* 3E-3F */
	UINT16  DefaultTime2Wait;		/* 40-41 */
	UINT16  DefaultTime2Retain;		/* 42-43 */
	UINT16  MaxOutStndngR2T;		/* 44-45 */
	UINT16  KeepAliveTimeout;		/* 46-47 */
	UINT16  PortNumber;			/* 48-49 */
	UINT16  MaxBurstSize;			/* 4A-4B */
	UINT32  RES4;				/* 4C-4F */
	UINT8   IPAddr[4];			/* 50-53 */
	UINT8   RES5[12];			/* 54-5F */
	UINT8   SubnetMask[4];			/* 60-63 */
	UINT8   RES6[12];			/* 64-6F */
	UINT8   GatewayIPAddr[4];		/* 70-73 */
	UINT8   RES7[12];			/* 74-7F */
	UINT8   PriDNSIPAddr[4];		/* 80-83 */
	UINT8   SecDNSIPAddr[4];		/* 84-87 */
	UINT8   RES8[8];			/* 88-8F */
	UINT8   Alias[32];			/* 90-AF */
	UINT8   TargAddr[8];			/* B0-B7 */
	UINT8   CHAPNameSecretsTable[8];	/* B8-BF */
	UINT8   EthernetMACAddr[6];		/* C0-C5 */
	UINT16  TargetPortalGroup;		/* C6-C7 */
	UINT8   SendScale;			/* C8    */
	UINT8   RecvScale;			/* C9    */
	UINT8   TypeOfService;			/* CA    */
	UINT8   Time2Live;			/* CB    */
	UINT16  VLANPriority;			/* CC-CD */
	UINT16  Reserved8;			/* CE-CF */
	UINT8   SecIPAddr[4];			/* D0-D3 */
	UINT8   Reserved9[12];			/* D4-DF */
	UINT8   iSNSIPAddr[4];			/* E0-E3 */
	UINT16  iSNSServerPortNumber;		/* E4-E5 */
	UINT8   Reserved10[10];			/* E6-EF */
	UINT8   SLPDAIPAddr[4];			/* F0-F3 */
	UINT8   Reserved11[12];			/* F4-FF */
	UINT8   iSCSINameString[256];		/* 100-1FF */
} INIT_FW_CTRL_BLK;

typedef struct {
	INIT_FW_CTRL_BLK init_fw_cb;
	UINT32       Cookie;
	#define INIT_FW_CTRL_BLK_COOKIE 	0x11BEAD5A
} FLASH_INIT_FW_CTRL_BLK;

/*************************************************************************/

typedef struct _DEV_DB_ENTRY {
	UINT8  options;	      /* 00 */
   #define  DDB_OPT_DISABLE                  0x08  /* do not connect to device */
   #define  DDB_OPT_ACCESSGRANTED            0x04
   #define  DDB_OPT_TARGET                   0x02  /* device is a target */
   #define  DDB_OPT_INITIATOR                0x01  /* device is an initiator */

	UINT8  control;	      /* 01 */
   #define  DDB_CTRL_DATABASE_ENTRY_STATE    0xC0
   #define  DDB_CTRL_SESSION_RECOVERY        0x10
   #define  DDB_CTRL_SENDING                 0x08
   #define  DDB_CTRL_XFR_PENDING             0x04
   #define  DDB_CTRL_QUEUE_ABORTED           0x02
   #define  DDB_CTRL_LOGGED_IN               0x01

	UINT16 exeThrottle;   /* 02-03 */
	UINT16 exeCount;      /* 04-05 */
	UINT8  retryCount;    /* 06    */
	UINT8  retryDelay;    /* 07    */
	UINT16 iSCSIOptions;  /* 08-09 */
   #define DDB_IOPT_RECV_ISCSI_MARKER_ENABLE 0x8000
   #define DDB_IOPT_SEND_ISCSI_MARKER_ENABLE 0x4000
   #define DDB_IOPT_HEADER_DIGEST_ENABLE     0x2000
   #define DDB_IOPT_DATA_DIGEST_ENABLE       0x1000
   #define DDB_IOPT_IMMEDIATE_DATA_ENABLE    0x0800
   #define DDB_IOPT_INITIAL_R2T_ENABLE       0x0400
   #define DDB_IOPT_DATA_SEQUENCE_IN_ORDER   0x0200
   #define DDB_IOPT_DATA_PDU_IN_ORDER        0x0100
   #define DDB_IOPT_CHAP_AUTH_ENABLE         0x0080
   #define DDB_IOPT_BIDIR_CHAP_CHAL_ENABLE   0x0010
   #define DDB_IOPT_RESERVED2                0x007F

	UINT16 TCPOptions;    /* 0A-0B */
   #define DDB_TOPT_NAGLE_DISABLE            0x0020
   #define DDB_TOPT_TIMER_SCALE_MASK         0x000E
   #define DDB_TOPT_TIME_STAMP_ENABLE        0x0001

	UINT16 IPOptions;     /* 0C-0D */
   #define DDB_IPOPT_FRAG_DISABLE     	     0x0002
   #define DDB_IPOPT_IP_ADDRESS_VALID        0x0001

	UINT16 maxPDUSize;    /* 0E-0F */
	UINT16 rcvMarkerInt;  /* 10-11 */
	UINT16 sndMarkerInt;  /* 12-13 */
	UINT16 iSCSIMaxSndDataSegLen;  /* 14-15 */
	UINT16 firstBurstSize;	   /* 16-17 */
	UINT16 minTime2Wait; /* 18-19 : RA :default_time2wait */
	UINT16 maxTime2Retain; /* 1A-1B */
	UINT16 maxOutstndngR2T;	   /* 1C-1D */
	UINT16 keepAliveTimeout;   /* 1E-1F */
	UINT8 ISID[6];	      /* 20-25 big-endian, must be converted to little-endian */
	UINT16 TSID;	      /* 26-27 */
	UINT16 portNumber;    /* 28-29 */
	UINT16 maxBurstSize;  /* 2A-2B */
	UINT16 taskMngmntTimeout;  /* 2C-2D */
	UINT16 reserved1;     /* 2E-2F */
	UINT8  ipAddr[0x10];  /* 30-3F */
	UINT8  iSCSIAlias[0x20];   /* 40-5F */
	UINT8  targetAddr[0x20];   /* 60-7F */
	UINT8  userID[0x20];  /* 80-9F */
	UINT8  password[0x20];	   /* A0-BF */
	UINT8  iscsiName[0x100];   /* C0-1BF : xxzzy Make this a pointer to a string so we don't
						     have to reserve soooo much RAM */
	UINT16 ddbLink;	      /* 1C0-1C1 */
	UINT16 CHAPTableIndex;	   /* 1C2-1C3 */
	UINT16 TargetPortalGroup;  /* 1C4-1C5 */
	UINT16 reserved2[2];		/* 1C6-1C7 */
	UINT32 statSN;			/* 1C8-1CB */
	UINT32 expStatSN;		/* 1CC-1CF */
	UINT16 reserved3[0x2C];		/* 1D0-1FB */
	UINT16 ddbValidCookie;		/* 1FC-1FD */
	UINT16 ddbValidSize;		/* 1FE-1FF */
} DEV_DB_ENTRY;


/*************************************************************************/

/* Flash definitions */
#define FLASH_FW_IMG_PAGE_SIZE        0x20000
#define FLASH_FW_IMG_PAGE(addr)       (0xfffe0000 & (addr))
#define FLASH_STRUCTURE_TYPE_MASK     0x0f000000

#define FLASH_OFFSET_FW_LOADER_IMG    0x00000000
#define FLASH_OFFSET_SECONDARY_FW_IMG 0x01000000
#define FLASH_OFFSET_SYS_INFO         0x02000000
#define FLASH_OFFSET_DRIVER_BLK       0x03000000
#define FLASH_OFFSET_INIT_FW_CTRL_BLK 0x04000000
#define FLASH_OFFSET_DEV_DB_AREA      0x05000000
#define FLASH_OFFSET_CHAP_AREA        0x06000000
#define FLASH_OFFSET_PRIMARY_FW_IMG   0x07000000
#define FLASH_READ_RAM_FLAG           0x10000000

#define MAX_FLASH_SZ                  0x400000    /* 4M flash */
#define FLASH_DEFAULTBLOCKSIZE        0x20000
#define FLASH_EOF_OFFSET              FLASH_DEFAULTBLOCKSIZE - 8 /* 4 bytes for EOF signature */
#define FLASH_FILESIZE_OFFSET         FLASH_EOF_OFFSET - 4       /* 4 bytes for file size */
#define FLASH_CKSUM_OFFSET            FLASH_FILESIZE_OFFSET - 4  /* 4 bytes for chksum protection */

typedef struct _SYS_INFO_PHYS_ADDR {
	UINT8            address[6];		/* 00-05 */
	UINT8            filler[2];		/* 06-07 */
} SYS_INFO_PHYS_ADDR;

typedef struct _FLASH_SYS_INFO {
	UINT32           cookie;		/* 00-03 */
	UINT32           physAddrCount;		/* 04-07 */
	SYS_INFO_PHYS_ADDR physAddr[4];		/* 08-27 */
	UINT8            vendorId[128];		/* 28-A7 */
	UINT8            productId[128];	/* A8-127 */
	UINT32           serialNumber;		/* 128-12B */

	/* PCI Configuration values */
	UINT32           pciDeviceVendor;	/* 12C-12F */
	UINT32           pciDeviceId;		/* 130-133 */
	UINT32           pciSubsysVendor;	/* 134-137 */
	UINT32           pciSubsysId;		/* 138-13B */

	/* This validates version 1. */
	UINT32           crumbs;		/* 13C-13F */

	UINT32           enterpriseNumber;	/* 140-143 */

	UINT32           mtu;			/* 144-147 */
	UINT32           reserved0;		/* 148-14b */
	UINT32           crumbs2;		/* 14c-14f */
	UINT8            acSerialNumber[16];	/* 150-15f */
	UINT32           crumbs3;		/* 160-16f */

	/* Leave this last in the struct so it is declared invalid if
	 * any new items are added. */
	UINT32           reserved1[39];		/* 170-1ff */
} FLASH_SYS_INFO, *PFLASH_SYS_INFO;		/* 200 */

typedef struct _FLASH_DRIVER_INFO {
	UINT32          LinuxDriverCookie;
	#define FLASH_LINUX_DRIVER_COOKIE		0x0A1B2C3D
	UINT8       Pad[4];

} FLASH_DRIVER_INFO, *PFLASH_DRIVER_INFO;

typedef struct _CHAP_ENTRY {
	UINT16 link;				  /*  0 x0   */
   #define CHAP_FLAG_PEER_NAME		0x40
   #define CHAP_FLAG_LOCAL_NAME    	0x80

	UINT8 flags;				 /*  2 x2    */
   #define MIN_CHAP_SECRET_LENGTH  	12
   #define MAX_CHAP_SECRET_LENGTH  	100

	UINT8 secretLength;			 /*  3 x3    */
	UINT8 secret[MAX_CHAP_SECRET_LENGTH];	 /*  4 x4    */
   #define MAX_CHAP_CHALLENGE_LENGTH       256

	UINT8 user_name[MAX_CHAP_CHALLENGE_LENGTH]; /* 104 x68  */
	UINT16 reserved;			    /* 360 x168 */
   #define CHAP_COOKIE                     0x4092

	UINT16 cookie;				    /* 362 x16a */
} CHAP_ENTRY, *PCHAP_ENTRY;			    /* 364 x16c */


/*************************************************************************/

typedef struct _CRASH_RECORD {
	UINT16  fw_major_version;	/* 00 - 01 */
	UINT16  fw_minor_version;	/* 02 - 03 */
	UINT16  fw_patch_version;	/* 04 - 05 */
	UINT16  fw_build_version;	/* 06 - 07 */

	UINT8   build_date[16];		/* 08 - 17 */
	UINT8   build_time[16];		/* 18 - 27 */
	UINT8   build_user[16];		/* 28 - 37 */
	UINT8   card_serial_num[16];	/* 38 - 47 */

	UINT32  time_of_crash_in_secs;	/* 48 - 4B */
	UINT32  time_of_crash_in_ms;	/* 4C - 4F */

	UINT16  out_RISC_sd_num_frames;	/* 50 - 51 */
	UINT16  OAP_sd_num_words;	/* 52 - 53 */
	UINT16  IAP_sd_num_frames;	/* 54 - 55 */
	UINT16  in_RISC_sd_num_words;	/* 56 - 57 */

	UINT8   reserved1[28];		/* 58 - 7F */

	UINT8   out_RISC_reg_dump[256];	/* 80 -17F */
	UINT8   in_RISC_reg_dump[256];	/*180 -27F */
	UINT8   in_out_RISC_stack_dump[0]; /*280 - ??? */
} CRASH_RECORD, *PCRASH_RECORD;


/*************************************************************************/

#define MAX_CONN_EVENT_LOG_ENTRIES	100

typedef struct _CONN_EVENT_LOG_ENTRY {
	UINT32  timestamp_sec;		/* 00 - 03 seconds since boot */
	UINT32  timestamp_ms;		/* 04 - 07 milliseconds since boot */
	UINT16  device_index;		/* 08 - 09  */
	UINT16  fw_conn_state;		/* 0A - 0B  */
	UINT8   event_type;		/* 0C - 0C  */
	UINT8   error_code;		/* 0D - 0D  */
	UINT16  error_code_detail;	/* 0E - 0F  */
	UINT8   num_consecutive_events;	/* 10 - 10  */
	UINT8   rsvd[3];		/* 11 - 13  */
} CONN_EVENT_LOG_ENTRY, *PCONN_EVENT_LOG_ENTRY;


/*************************************************************************
 *
 *				IOCB Commands Structures and Definitions
 *
 *************************************************************************/
#define IOCB_MAX_CDB_LEN            16  /* Bytes in a CBD */
#define IOCB_MAX_SENSEDATA_LEN      32  /* Bytes of sense data */
#define IOCB_MAX_EXT_SENSEDATA_LEN  60  /* Bytes of extended sense data */
#define IOCB_MAX_DSD_CNT             1  /* DSDs per noncontinuation type IOCB */
#define IOCB_CONT_MAX_DSD_CNT        5  /* DSDs per Continuation */
#define CTIO_MAX_SENSEDATA_LEN      24  /* Bytes of sense data in a CTIO*/

#define RESERVED_BYTES_MARKER       40  /* Reserved Bytes at end of Marker */
#define RESERVED_BYTES_INOT         28  /* Reserved Bytes at end of Immediate Notify */
#define RESERVED_BYTES_NOTACK       28  /* Reserved Bytes at end of Notify Acknowledge */
#define RESERVED_BYTES_CTIO          2  /* Reserved Bytes in middle of CTIO */

#define MAX_MBX_COUNT               14  /* Maximum number of mailboxes in MBX IOCB */

#define ISCSI_MAX_NAME_BYTECNT      256  /* Bytes in a target name */

#define IOCB_ENTRY_SIZE       	    0x40


/* IOCB header structure */
typedef struct _HEADER {
	UINT8 entryType;
   #define ET_STATUS                0x03
   #define ET_MARKER                0x04
   #define ET_CONT_T1               0x0A
   #define ET_INOT                  0x0D
   #define ET_NACK                  0x0E
   #define ET_STATUS_CONTINUATION   0x10
   #define ET_CMND_T4               0x15
   #define ET_ATIO                  0x16
   #define ET_CMND_T3               0x19
   #define ET_CTIO4                 0x1E
   #define ET_CTIO3                 0x1F
   #define ET_PERFORMANCE_STATUS    0x20
   #define ET_MAILBOX_CMD           0x38
   #define ET_MAILBOX_STATUS        0x39
   #define ET_PASSTHRU0             0x3A
   #define ET_PASSTHRU1             0x3B
   #define ET_PASSTHRU_STATUS       0x3C
   #define ET_ASYNCH_MSG            0x3D
   #define ET_CTIO5                 0x3E
   #define ET_CTIO6                 0x3F

	UINT8 entryStatus;
    #define ES_MASK                 0x3E
    #define ES_SUPPRESS_COMPL_INT   0x01
    #define ES_BUSY                 0x02
    #define ES_INVALID_ENTRY_TYPE   0x04
    #define ES_INVALID_ENTRY_PARAM  0x08
    #define ES_INVALID_ENTRY_COUNT  0x10
    #define ES_INVALID_ENTRY_ORDER  0x20
	UINT8 systemDefined;
	UINT8 entryCount;

	/* SyetemDefined definition */
    #define SD_PASSTHRU_IOCB        0x01
} HEADER ;

/* Genric queue entry structure*/
typedef struct QUEUE_ENTRY {
	UINT8  data[60];
	UINT32 signature;

} QUEUE_ENTRY;


/* 64 bit addressing segment counts*/

#define COMMAND_SEG_A64             1
#define CONTINUE_SEG_A64            5
#define CONTINUE_SEG_A64_MINUS1     4

/* 64 bit addressing segment definition*/

typedef struct DATA_SEG_A64 {
	struct {
		UINT32 addrLow;
		UINT32 addrHigh;

	} base;

	UINT32 count;

} DATA_SEG_A64;

/* Command Type 3 entry structure*/

typedef struct _COMMAND_T3_ENTRY {
	HEADER  hdr;		   /* 00-03 */

	UINT32  handle;		   /* 04-07 */
	UINT16  target;		   /* 08-09 */
	UINT16  connection_id;	   /* 0A-0B */

	UINT8   control_flags;	   /* 0C */
   #define CF_IMMEDIATE		   0x80

	/* data direction  (bits 5-6)*/
   #define CF_WRITE                0x20
   #define CF_READ                 0x40
   #define CF_NO_DATA              0x00
   #define CF_DIRECTION_MASK       0x60

	/* misc  (bits 4-3)*/
   #define CF_DSD_PTR_ENABLE	   0x10	   /* 4010 only */
   #define CF_CMD_PTR_ENABLE	   0x08    /* 4010 only */

	/* task attributes (bits 2-0) */
   #define CF_ACA_QUEUE            0x04
   #define CF_HEAD_TAG             0x03
   #define CF_ORDERED_TAG          0x02
   #define CF_SIMPLE_TAG           0x01
   #define CF_TAG_TYPE_MASK        0x07
   #define CF_ATTRIBUTES_MASK      0x67

	/* STATE FLAGS FIELD IS A PLACE HOLDER. THE FW WILL SET BITS IN THIS FIELD
	   AS THE COMMAND IS PROCESSED. WHEN THE IOCB IS CHANGED TO AN IOSB THIS
	   FIELD WILL HAVE THE STATE FLAGS SET PROPERLY.
	*/
	UINT8   state_flags;	   /* 0D */
	UINT8   cmdRefNum;	   /* 0E */
	UINT8   reserved1;	   /* 0F */
	UINT8   cdb[IOCB_MAX_CDB_LEN];	/* 10-1F */
	UINT8   lun[8];		   /* 20-27 */
	UINT32  cmdSeqNum;	   /* 28-2B */
	UINT16  timeout;	   /* 2C-2D */
	UINT16  dataSegCnt;	   /* 2E-2F */
	UINT32  ttlByteCnt;	   /* 30-33 */
	DATA_SEG_A64 dataseg[COMMAND_SEG_A64];	/* 34-3F */

} COMMAND_T3_ENTRY;

typedef struct _COMMAND_T4_ENTRY {
	HEADER  hdr;		  /* 00-03 */
	UINT32  handle;		  /* 04-07 */
	UINT16  target;		  /* 08-09 */
	UINT16  connection_id;	  /* 0A-0B */
	UINT8   control_flags;	  /* 0C */

	/* STATE FLAGS FIELD IS A PLACE HOLDER. THE FW WILL SET BITS IN THIS FIELD
	   AS THE COMMAND IS PROCESSED. WHEN THE IOCB IS CHANGED TO AN IOSB THIS
	   FIELD WILL HAVE THE STATE FLAGS SET PROPERLY.
	*/
	UINT8   state_flags;	  /* 0D */
	UINT8   cmdRefNum;	  /* 0E */
	UINT8   reserved1;	  /* 0F */
	UINT8   cdb[IOCB_MAX_CDB_LEN]; /* 10-1F */
	UINT8   lun[8];		  /* 20-27 */
	UINT32  cmdSeqNum;	  /* 28-2B */
	UINT16  timeout;	  /* 2C-2D */
	UINT16  dataSegCnt;	  /* 2E-2F */
	UINT32  ttlByteCnt;	  /* 30-33 */

	/* WE ONLY USE THE ADDRESS FIELD OF THE FOLLOWING STRUCT.
	   THE COUNT FIELD IS RESERVED */
	DATA_SEG_A64 dataseg[COMMAND_SEG_A64];	/* 34-3F */
} COMMAND_T4_ENTRY;

/* Continuation Type 1 entry structure*/
typedef struct _CONTINUATION_T1_ENTRY {
	HEADER  hdr;

	DATA_SEG_A64 dataseg[CONTINUE_SEG_A64];

}CONTINUATION_T1_ENTRY;

/* Status Continuation Type entry structure*/
typedef struct _STATUS_CONTINUATION_ENTRY {
	HEADER  hdr;

	UINT8 extSenseData[IOCB_MAX_EXT_SENSEDATA_LEN];

}STATUS_CONTINUATION_ENTRY;

/* Parameterize for 64 or 32 bits */
    #define COMMAND_SEG     COMMAND_SEG_A64
    #define CONTINUE_SEG    CONTINUE_SEG_A64

    #define COMMAND_ENTRY   COMMAND_T3_ENTRY
    #define CONTINUE_ENTRY  CONTINUATION_T1_ENTRY

    #define ET_COMMAND      ET_CMND_T3
    #define ET_CONTINUE     ET_CONT_T1



/* Marker entry structure*/
typedef struct _MARKER_ENTRY {
	HEADER  hdr;		/* 00-03 */

	UINT32  system_defined;	/* 04-07 */
	UINT16  target;		/* 08-09 */
	UINT16  modifier;	/* 0A-0B */
   #define MM_LUN_RESET         0
   #define MM_TARGET_WARM_RESET 1
   #define MM_TARGET_COLD_RESET 2
   #define MM_CLEAR_ACA    	3
   #define MM_CLEAR_TASK_SET    4
   #define MM_ABORT_TASK_SET    5

	UINT16  flags;		/* 0C-0D */
	UINT16  reserved1;	/* 0E-0F */
	UINT8   lun[8];		/* 10-17 */
	UINT64  reserved2;	/* 18-1F */
	UINT64  reserved3;	/* 20-27 */
	UINT64  reserved4;	/* 28-2F */
	UINT64  reserved5;	/* 30-37 */
	UINT64  reserved6;	/* 38-3F */
}MARKER_ENTRY;

/* Status entry structure*/
typedef struct _STATUS_ENTRY {
	HEADER  hdr;			     /* 00-03 */

	UINT32  handle;			     /* 04-07 */

	UINT8   scsiStatus;		     /* 08 */
   #define SCSI_STATUS_MASK                  0xFF
   #define SCSI_STATUS                       0xFF
   #define SCSI_GOOD                         0x00

	UINT8   iscsiFlags;		     /* 09 */
   #define ISCSI_FLAG_RESIDUAL_UNDER         0x02
   #define ISCSI_FLAG_RESIDUAL_OVER          0x04
   #define ISCSI_FLAG_RESIDUAL_UNDER_BIREAD  0x08
   #define ISCSI_FLAG_RESIDUAL_OVER_BIREAD   0x10

	UINT8   iscsiResponse;		     /* 0A */
   #define ISCSI_RSP_COMPLETE                    0x00
   #define ISCSI_RSP_TARGET_FAILURE              0x01
   #define ISCSI_RSP_DELIVERY_SUBSYS_FAILURE     0x02
   #define ISCSI_RSP_UNSOLISITED_DATA_REJECT     0x03
   #define ISCSI_RSP_NOT_ENOUGH_UNSOLISITED_DATA 0x04
   #define ISCSI_RSP_CMD_IN_PROGRESS             0x05

	UINT8   completionStatus;	     /* 0B */
   #define SCS_COMPLETE                      0x00
   #define SCS_INCOMPLETE                    0x01
   #define SCS_DMA_ERROR                     0x02
   #define SCS_TRANSPORT_ERROR               0x03
   #define SCS_RESET_OCCURRED                0x04
   #define SCS_ABORTED                       0x05
   #define SCS_TIMEOUT                       0x06
   #define SCS_DATA_OVERRUN                  0x07
   #define SCS_DATA_DIRECTION_ERROR          0x08
   #define SCS_DATA_UNDERRUN                 0x15
   #define SCS_QUEUE_FULL                    0x1C
   #define SCS_DEVICE_UNAVAILABLE            0x28
   #define SCS_DEVICE_LOGGED_OUT             0x29
   #define SCS_DEVICE_CONFIG_CHANGED         0x2A

	UINT8   reserved1;		     /* 0C */

	/* state_flags MUST be at the same location as state_flags in the
	   Command_T3/4_Entry */
	UINT8   state_flags;		     /* 0D */
   #define STATE_FLAG_SENT_COMMAND           0x01
   #define STATE_FLAG_TRANSFERRED_DATA       0x02
   #define STATE_FLAG_GOT_STATUS             0x04
   #define STATE_FLAG_LOGOUT_SENT            0x10

	UINT16  senseDataByteCnt;	     /* 0E-0F */
	UINT32  residualByteCnt;	     /* 10-13 */
	UINT32  bidiResidualByteCnt;	     /* 14-17 */
	UINT32  expSeqNum;		     /* 18-1B */
	UINT32  maxCmdSeqNum;		     /* 1C-1F */
	UINT8   senseData[IOCB_MAX_SENSEDATA_LEN]; /* 20-3F */

}STATUS_ENTRY;

/*
 * Performance Status Entry where up to 30 handles can be posted in a
 * single IOSB. Handles are of 16 bit value.
 */
typedef struct  _PERFORMANCE_STATUS_ENTRY {
	UINT8  entryType;
	UINT8  entryCount;
	UINT16 handleCount;

   #define MAX_STATUS_HANDLE  30
	UINT16 handleArray[ MAX_STATUS_HANDLE ];

} PERFORMANCE_STATUS_ENTRY;


typedef struct _IMMEDIATE_NOTIFY_ENTRY {
	HEADER  hdr;
	UINT32  handle;
	UINT16  initiator;
	UINT16  InitSessionID;
	UINT16  ConnectionID;
	UINT16  TargSessionID;
	UINT16  inotStatus;
   #define INOT_STATUS_ABORT_TASK      0x0020
   #define INOT_STATUS_LOGIN_RECVD     0x0021
   #define INOT_STATUS_LOGOUT_RECVD    0x0022
   #define INOT_STATUS_LOGGED_OUT      0x0029
   #define INOT_STATUS_RESTART_RECVD   0x0030
   #define INOT_STATUS_MSG_RECVD       0x0036
   #define INOT_STATUS_TSK_REASSIGN    0x0037

	UINT16  taskFlags;
   #define TASK_FLAG_CLEAR_ACA         0x4000
   #define TASK_FLAG_COLD_RESET        0x2000
   #define TASK_FLAG_WARM_RESET        0x0800
   #define TASK_FLAG_LUN_RESET         0x1000
   #define TASK_FLAG_CLEAR_TASK_SET    0x0400
   #define TASK_FLAG_ABORT_TASK_SET    0x0200


	UINT32  refTaskTag;
	UINT8   lun[8];
	UINT32  inotTaskTag;
	UINT8   res3[RESERVED_BYTES_INOT];
} IMMEDIATE_NOTIFY_ENTRY ;

typedef struct _NOTIFY_ACK_ENTRY {
	HEADER  hdr;
	UINT32  handle;
	UINT16  initiator;
	UINT16  res1;
	UINT16  flags;
	UINT8        responseCode;
	UINT8        qualifier;
	UINT16  notAckStatus;
	UINT16  taskFlags;
   #define NACK_FLAG_RESPONSE_CODE_VALID 0x0010

	UINT32  refTaskTag;
	UINT8   lun[8];
	UINT32  inotTaskTag;
	UINT8   res3[RESERVED_BYTES_NOTACK];
} NOTIFY_ACK_ENTRY ;

typedef struct _ATIO_ENTRY {
	HEADER  hdr;			  /* 00-03 */
	UINT32  handle;			  /* 04-07 */
	UINT16  initiator;		  /* 08-09 */
	UINT16  connectionID;		  /* 0A-0B */
	UINT32  taskTag;		  /* 0C-0f */
	UINT8   scsiCDB[IOCB_MAX_CDB_LEN];     /* 10-1F */
	UINT8   LUN[8];			  /* 20-27 */
	UINT8   cmdRefNum;		  /* 28 */

	UINT8   pduType;		  /* 29 */
   #define PDU_TYPE_NOPOUT                0x00
   #define PDU_TYPE_SCSI_CMD              0x01
   #define PDU_TYPE_SCSI_TASK_MNGMT_CMD   0x02
   #define PDU_TYPE_LOGIN_CMD             0x03
   #define PDU_TYPE_TEXT_CMD              0x04
   #define PDU_TYPE_SCSI_DATA             0x05
   #define PDU_TYPE_LOGOUT_CMD            0x06
   #define PDU_TYPE_SNACK                 0x10

	UINT16  atioStatus;		  /* 2A-2B */
   #define ATIO_CDB_RECVD                 0x003d

	UINT16  reserved1;		  /* 2C-2D */

	UINT8   taskCode;		  /* 2E */
   #define ATIO_TASK_CODE_UNTAGGED        0x00
   #define ATIO_TASK_CODE_SIMPLE_QUEUE    0x01
   #define ATIO_TASK_CODE_ORDERED_QUEUE   0x02
   #define ATIO_TASK_CODE_HEAD_OF_QUEUE   0x03
   #define ATIO_TASK_CODE_ACA_QUEUE       0x04

	UINT8   reserved2;		  /* 2F */
	UINT32  totalByteCnt;		  /* 30-33 */
	UINT32  cmdSeqNum;		  /* 34-37 */
	UINT64  immDataBufDesc;		  /* 38-3F */
} ATIO_ENTRY ;

typedef struct _CTIO3_ENTRY {
	HEADER  hdr;			  /* 00-03 */
	UINT32  handle;			  /* 04-07 */
	UINT16  initiator;		  /* 08-09 */
	UINT16  connectionID;		  /* 0A-0B */
	UINT32  taskTag;		  /* 0C-0F */

	UINT8   flags;			  /* 10 */
   #define CTIO_FLAG_SEND_SCSI_STATUS     0x01
   #define CTIO_FLAG_TERMINATE_COMMAND    0x10
   #define CTIO_FLAG_FAST_POST            0x08
   #define CTIO_FLAG_FINAL_CTIO           0x80

	/*  NOTE:  Our firmware assumes that the CTIO_FLAG_SEND_DATA and
		   CTIO_FLAG_GET_DATA flags are in the same bit positions
		   as the R and W bits in SCSI Command PDUs, so their values
		   should not be changed!
	 */
   #define CTIO_FLAG_SEND_DATA            0x0040   /* (see note) Read Data Flag, send data to initiator       */
   #define CTIO_FLAG_GET_DATA             0x0020   /* (see note) Write Data Flag, get data from the initiator */

	UINT8   scsiStatus;		  /* 11 */
	UINT16  timeout;		  /* 12-13 */
	UINT32  offset;			  /* 14-17 */
	UINT32  r2tSN;			  /* 18-1B */
	UINT32  expCmdSN;		  /* 1C-1F */
	UINT32  maxCmdSN;		  /* 20-23 */
	UINT32  dataSN;			  /* 24-27 */
	UINT32  residualCount;		  /* 28-2B */
	UINT16  reserved;		  /* 2C-2D */
	UINT16  segmentCnt;		  /* 2E-2F */
	UINT32  totalByteCnt;		  /* 30-33 */
	DATA_SEG_A64 dataseg[COMMAND_SEG_A64]; /* 34-3F */
} CTIO3_ENTRY ;

typedef struct _CTIO4_ENTRY {
	HEADER  hdr;			  /* 00-03 */
	UINT32  handle;			  /* 04-07 */
	UINT16  initiator;		  /* 08-09 */
	UINT16  connectionID;		  /* 0A-0B */
	UINT32  taskTag;		  /* 0C-0F */
	UINT8   flags;			  /* 10 */
	UINT8   scsiStatus;		  /* 11 */
	UINT16  timeout;		  /* 12-13 */
	UINT32  offset;			  /* 14-17 */
	UINT32  r2tSN;			  /* 18-1B */
	UINT32  expCmdSN;		  /* 1C-1F */
	UINT32  maxCmdSN;		  /* 20-23 */
	UINT32  dataSN;			  /* 24-27 */
	UINT32  residualCount;		  /* 28-2B */
	UINT16  reserved;		  /* 2C-2D */
	UINT16  segmentCnt;		  /* 2E-2F */
	UINT32  totalByteCnt;		  /* 30-33 */
	/* WE ONLY USE THE ADDRESS FROM THE FOLLOWING STRUCTURE THE COUNT FIELD IS
	   RESERVED */
	DATA_SEG_A64 dataseg[COMMAND_SEG_A64]; /* 34-3F */
} CTIO4_ENTRY ;

typedef struct _CTIO5_ENTRY {
	HEADER  hdr;			  /* 00-03 */
	UINT32  handle;			  /* 04-07 */
	UINT16  initiator;		  /* 08-09 */
	UINT16  connectionID;		  /* 0A-0B */
	UINT32  taskTag;		  /* 0C-0F */
	UINT8   response;		  /* 10 */
	UINT8   scsiStatus;		  /* 11 */
	UINT16  timeout;		  /* 12-13 */
	UINT32  reserved1;		  /* 14-17 */
	UINT32  expR2TSn;		  /* 18-1B */
	UINT32  expCmdSn;		  /* 1C-1F */
	UINT32  MaxCmdSn;		  /* 20-23 */
	UINT32  expDataSn;		  /* 24-27 */
	UINT32  residualCnt;		  /* 28-2B */
	UINT32  bidiResidualCnt;	  /* 2C-2F */
	UINT32  reserved2;		  /* 30-33 */
	DATA_SEG_A64 dataseg[1];	  /* 34-3F */
} CTIO5_ENTRY ;

typedef struct _CTIO6_ENTRY {
	HEADER  hdr;			  /* 00-03 */
	UINT32  handle;			  /* 04-07 */
	UINT16  initiator;		  /* 08-09 */
	UINT16  connection;		  /* 0A-0B */
	UINT32  taskTag;		  /* 0C-0F */
	UINT16  flags;			  /* 10-11 */
	UINT16  timeout;		  /* 12-13 */
	UINT32  reserved1;		  /* 14-17 */
	UINT64  reserved2;		  /* 18-1F */
	UINT64  reserved3;		  /* 20-27 */
	UINT64  reserved4;		  /* 28-2F */
	UINT32  reserved5;		  /* 30-33 */
	DATA_SEG_A64 dataseg[1];	  /* 34-3F */
} CTIO6_ENTRY ;

typedef struct _CTIO_STATUS_ENTRY {
	HEADER  hdr;			  /* 00-03 */
	UINT32  handle;			  /* 04-07 */
	UINT16  initiator;		  /* 08-09 */
	UINT16  connectionID;		  /* 0A-0B */
	UINT32  taskTag;		  /* 0C-0F */
	UINT16  status;			  /* 10-11 */
   #define CTIO_STATUS_COMPLETE           0x0001
   #define CTIO_STATUS_ABORTED            0x0002
   #define CTIO_STATUS_DMA_ERROR          0x0003
   #define CTIO_STATUS_ERROR              0x0004
   #define CTIO_STATUS_INVALID_TAG        0x0008
   #define CTIO_STATUS_DATA_OVERRUN       0x0009
   #define CTIO_STATUS_CMD_TIMEOUT        0x000B
   #define CTIO_STATUS_PCI_ERROR          0x0010
   #define CTIO_STATUS_DATA_UNDERRUN      0x0015
   #define CTIO_STATUS_TARGET_RESET       0x0017
   #define CTIO_STATUS_NO_CONNECTION      0x0028
   #define CTIO_STATUS_LOGGED_OUT         0x0029
   #define CTIO_STATUS_CONFIG_CHANGED     0x002A
   #define CTIO_STATUS_UNACK_EVENT        0x0035
   #define CTIO_STATUS_INVALID_DATA_XFER  0x0036

	UINT16  timeout;		  /* 12-13 */
	UINT32  reserved1;		  /* 14-17 */
	UINT32  expR2TSN;		  /* 18-1B */
	UINT32  reserved2;		  /* 1C-1F */
	UINT32  reserved3;		  /* 20-23 */
	UINT64  expDataSN;		  /* 24-27 */
	UINT32  residualCount;		  /* 28-2B */
	UINT32  reserved4;		  /* 2C-2F */
	UINT64  reserved5;		  /* 30-37 */
	UINT64  reserved6;		  /* 38-3F */
} CTIO_STATUS_ENTRY ;

typedef struct _MAILBOX_ENTRY {
	HEADER  hdr;
	UINT32  handle;
	UINT32  mbx[MAX_MBX_COUNT];
} MAILBOX_ENTRY ;

typedef struct MAILBOX_STATUS_ENTRY {
	HEADER  hdr;
	UINT32  handle;
	UINT32  mbx[MAX_MBX_COUNT];
} MAILBOX_STATUS_ENTRY ;


typedef struct _PDU_ENTRY {
	UINT8       *Buff;
	UINT32       BuffLen;
	UINT32       SendBuffLen;
	UINT32       RecvBuffLen;
	struct _PDU_ENTRY *Next;
	dma_addr_t DmaBuff;
} PDU_ENTRY, *PPDU_ENTRY;

typedef struct _ISNS_DISCOVERED_TARGET_PORTAL {
	UINT8       IPAddr[4];
	UINT16      PortNumber;
	UINT16      Reserved;
} ISNS_DISCOVERED_TARGET_PORTAL, *PISNS_DISCOVERED_TARGET_PORTAL;

typedef struct _ISNS_DISCOVERED_TARGET {
	UINT32      NumPortals;		  /*  00-03 */
#define ISNS_MAX_PORTALS 4
	ISNS_DISCOVERED_TARGET_PORTAL Portal[ISNS_MAX_PORTALS];	/* 04-23 */
	UINT32      DDID;		  /*  24-27 */
	UINT8       NameString[256];	  /*  28-127 */
	UINT8       Alias[32];		  /* 128-147 */
} ISNS_DISCOVERED_TARGET, *PISNS_DISCOVERED_TARGET;


typedef struct _PASSTHRU0_ENTRY {
	HEADER  hdr;			  /* 00-03 */
	UINT32  handle;			  /* 04-07 */
	UINT16  target;			  /* 08-09 */
	UINT16  connectionID;		  /* 0A-0B */
	#define ISNS_DEFAULT_SERVER_CONN_ID     ((uint16_t)0x8000)

	UINT16  controlFlags;		  /* 0C-0D */
	#define PT_FLAG_ETHERNET_FRAME   	0x8000
	#define PT_FLAG_ISNS_PDU                0x8000
	#define PT_FLAG_IP_DATAGRAM             0x4000
	#define PT_FLAG_TCP_PACKET              0x2000
	#define PT_FLAG_NETWORK_PDU             (PT_FLAG_ETHERNET_FRAME | PT_FLAG_IP_DATAGRAM | PT_FLAG_TCP_PACKET)
	#define PT_FLAG_iSCSI_PDU               0x1000
	#define PT_FLAG_SEND_BUFFER             0x0200
	#define PT_FLAG_WAIT_4_RESPONSE         0x0100
	#define PT_FLAG_NO_FAST_POST            0x0080

	UINT16  timeout;		  /* 0E-0F */
	#define PT_DEFAULT_TIMEOUT              30   /* seconds */

	DATA_SEG_A64 outDataSeg64;	  /* 10-1B */
	UINT32  res1;			  /* 1C-1F */
	DATA_SEG_A64 inDataSeg64;	  /* 20-2B */
	UINT8   res2[20];		  /* 2C-3F */
} PASSTHRU0_ENTRY ;

typedef struct _PASSTHRU1_ENTRY {
	HEADER  hdr;			  /* 00-03 */
	UINT32  handle;			  /* 04-07 */
	UINT16  target;			  /* 08-09 */
	UINT16  connectionID;		  /* 0A-0B */

	UINT16  controlFlags;		  /* 0C-0D */
   #define PT_FLAG_ETHERNET_FRAME         	0x8000
   #define PT_FLAG_IP_DATAGRAM            	0x4000
   #define PT_FLAG_TCP_PACKET             	0x2000
   #define PT_FLAG_iSCSI_PDU              	0x1000
   #define PT_FLAG_SEND_BUFFER            	0x0200
   #define PT_FLAG_WAIT_4_REPONSE         	0x0100
   #define PT_FLAG_NO_FAST_POST           	0x0080

	UINT16  timeout;		  /* 0E-0F */
	DATA_SEG_A64 outDSDList;	  /* 10-1B */
	UINT32  outDSDCnt;		  /* 1C-1F */
	DATA_SEG_A64 inDSDList;		  /* 20-2B */
	UINT32  inDSDCnt;		  /* 2C-2F */
	UINT8  res1;			  /* 30-3F */

} PASSTHRU1_ENTRY ;

typedef struct _PASSTHRU_STATUS_ENTRY {
	HEADER  hdr;			  /* 00-03 */
	UINT32  handle;			  /* 04-07 */
	UINT16  target;			  /* 08-09 */
	UINT16  connectionID;		  /* 0A-0B */

	UINT8   completionStatus;	  /* 0C */
   #define PASSTHRU_STATUS_COMPLETE       		0x01
   #define PASSTHRU_STATUS_ERROR          		0x04
   #define PASSTHRU_STATUS_INVALID_DATA_XFER            0x06
   #define PASSTHRU_STATUS_CMD_TIMEOUT    		0x0B
   #define PASSTHRU_STATUS_PCI_ERROR      		0x10
   #define PASSTHRU_STATUS_NO_CONNECTION  		0x28

	UINT8   residualFlags;		  /* 0D */
   #define PASSTHRU_STATUS_DATAOUT_OVERRUN              0x01
   #define PASSTHRU_STATUS_DATAOUT_UNDERRUN             0x02
   #define PASSTHRU_STATUS_DATAIN_OVERRUN               0x04
   #define PASSTHRU_STATUS_DATAIN_UNDERRUN              0x08

	UINT16  timeout;		  /* 0E-0F */
	UINT16  portNumber;		  /* 10-11 */
	UINT8   res1[10];		  /* 12-1B */
	UINT32  outResidual;		  /* 1C-1F */
	UINT8   res2[12];		  /* 20-2B */
	UINT32  inResidual;		  /* 2C-2F */
	UINT8   res4[16];		  /* 30-3F */
} PASSTHRU_STATUS_ENTRY ;

typedef struct _ASYNCHMSG_ENTRY {
	HEADER  hdr;
	UINT32  handle;
	UINT16  target;
	UINT16  connectionID;
	UINT8   lun[8];
	UINT16  iSCSIEvent;
   #define AMSG_iSCSI_EVENT_NO_EVENT                  0x0000
   #define AMSG_iSCSI_EVENT_TARG_RESET                0x0001
   #define AMSG_iSCSI_EVENT_TARGT_LOGOUT              0x0002
   #define AMSG_iSCSI_EVENT_CONNECTION_DROPPED        0x0003
   #define AMSG_ISCSI_EVENT_ALL_CONNECTIONS_DROPPED   0x0004

	UINT16  SCSIEvent;
   #define AMSG_NO_SCSI_EVENT                         0x0000
   #define AMSG_SCSI_EVENT                            0x0001

	UINT16  parameter1;
	UINT16  parameter2;
	UINT16  parameter3;
	UINT32  expCmdSn;
	UINT32  maxCmdSn;
	UINT16  senseDataCnt;
	UINT16  reserved;
	UINT32  senseData[IOCB_MAX_SENSEDATA_LEN];
} ASYNCHMSG_ENTRY ;

/* Timer entry structure, this is an internal generated structure
   which causes the QLA4000 initiator to send a NOP-OUT or the
   QLA4000 target to send a NOP-IN */

typedef struct _TIMER_ENTRY {
	HEADER  hdr;		   /* 00-03 */

	UINT32  handle;		   /* 04-07 */
	UINT16  target;		   /* 08-09 */
	UINT16  connection_id;	   /* 0A-0B */

	UINT8   control_flags;	   /* 0C */

	/* STATE FLAGS FIELD IS A PLACE HOLDER. THE FW WILL SET BITS IN THIS FIELD
	   AS THE COMMAND IS PROCESSED. WHEN THE IOCB IS CHANGED TO AN IOSB THIS
	   FIELD WILL HAVE THE STATE FLAGS SET PROPERLY.
	*/
	UINT8   state_flags;	   /* 0D */
	UINT8   cmdRefNum;	   /* 0E */
	UINT8   reserved1;	   /* 0F */
	UINT8   cdb[IOCB_MAX_CDB_LEN];	   /* 10-1F */
	UINT8   lun[8];		   /* 20-27 */
	UINT32  cmdSeqNum;	   /* 28-2B */
	UINT16  timeout;	   /* 2C-2D */
	UINT16  dataSegCnt;	   /* 2E-2F */
	UINT32  ttlByteCnt;	   /* 30-33 */
	DATA_SEG_A64 dataseg[COMMAND_SEG_A64];	/* 34-3F */

} TIMER_ENTRY;


#endif /* _QLA4X_FW_H */

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */

