/*
 * Copyright (c)  2003-2005 QLogic Corporation
 * QLogic Linux iSCSI Driver
 *
 * This program includes a device driver for Linux 2.6 that may be
 * distributed with QLogic hardware specific firmware binary file.
 * You may modify and redistribute the device driver code under the
 * GNU General Public License as published by the Free Software
 * Foundation (version 2 or a later version) and/or under the
 * following terms, as applicable:
 *
 * 	1. Redistribution of source code must retain the above
 * 	   copyright notice, this list of conditions and the
 * 	   following disclaimer.
 *
 * 	2. Redistribution in binary form must reproduce the above
 * 	   copyright notice, this list of conditions and the
 * 	   following disclaimer in the documentation and/or other
 * 	   materials provided with the distribution.
 *
 * 	3. The name of QLogic Corporation may not be used to
 * 	   endorse or promote products derived from this software
 * 	   without specific prior written permission.
 *
 * You may redistribute the hardware specific firmware binary file
 * under the following terms:
 *
 * 	1. Redistribution of source code (only if applicable),
 * 	   must retain the above copyright notice, this list of
 * 	   conditions and the following disclaimer.
 *
 * 	2. Redistribution in binary form must reproduce the above
 * 	   copyright notice, this list of conditions and the
 * 	   following disclaimer in the documentation and/or other
 * 	   materials provided with the distribution.
 *
 * 	3. The name of QLogic Corporation may not be used to
 * 	   endorse or promote products derived from this software
 * 	   without specific prior written permission
 *
 * REGARDLESS OF WHAT LICENSING MECHANISM IS USED OR APPLICABLE,
 * THIS PROGRAM IS PROVIDED BY QLOGIC CORPORATION "AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * USER ACKNOWLEDGES AND AGREES THAT USE OF THIS PROGRAM WILL NOT
 * CREATE OR GIVE GROUNDS FOR A LICENSE BY IMPLICATION, ESTOPPEL, OR
 * OTHERWISE IN ANY INTELLECTUAL PROPERTY RIGHTS (PATENT, COPYRIGHT,
 * TRADE SECRET, MASK WORK, OR OTHER PROPRIETARY RIGHT) EMBODIED IN
 * ANY OTHER QLOGIC HARDWARE OR SOFTWARE EITHER SOLELY OR IN
 * COMBINATION WITH THIS PROGRAM.
 */
#ifndef _QLA4X_FW_H
#define _QLA4X_FW_H

#ifndef uint64_t
#define uint64_t __u64
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

// ISP Maximum number of DSD per command
#define DSD_MAX                                 1024

// FW check
#define FW_UP(reg,stat)                         (((stat = RD_REG_DWORD(reg->mailbox[0])) != 0) && (stat != 0x0007))

#define INVALID_REGISTER 			((uint32_t)-1)

#define ISP4010_NET_FUNCTION                            0
#define ISP4010_ISCSI_FUNCTION                          1

/*************************************************************************
 *
 * 		ISP 4010 I/O Register Set Structure and Definitions
 *
 *************************************************************************/

typedef struct _PORT_CTRL_STAT_REGS {
	uint32_t ext_hw_conf;	// 80 x50  R/W
	uint32_t intChipConfiguration;	// 84 x54   *
	uint32_t port_ctrl;	// 88 x58   *
	uint32_t port_status;	// 92 x5c   *
	uint32_t HostPrimMACHi;	// 96 x60   *
	uint32_t HostPrimMACLow;	//100 x64   *
	uint32_t HostSecMACHi;	//104 x68   *
	uint32_t HostSecMACLow;	//108 x6c   *
	uint32_t EPPrimMACHi;	//112 x70   *
	uint32_t EPPrimMACLow;	//116 x74   *
	uint32_t EPSecMACHi;	//120 x78   *
	uint32_t EPSecMACLow;	//124 x7c   *
	uint32_t HostPrimIPHi;	//128 x80   *
	uint32_t HostPrimIPMidHi;	//132 x84   *
	uint32_t HostPrimIPMidLow;	//136 x88   *
	uint32_t HostPrimIPLow;	//140 x8c   *
	uint32_t HostSecIPHi;	//144 x90   *
	uint32_t HostSecIPMidHi;	//148 x94   *
	uint32_t HostSecIPMidLow;	//152 x98   *
	uint32_t HostSecIPLow;	//156 x9c   *
	uint32_t EPPrimIPHi;	//160 xa0   *
	uint32_t EPPrimIPMidHi;	//164 xa4   *
	uint32_t EPPrimIPMidLow;	//168 xa8   *
	uint32_t EPPrimIPLow;	//172 xac   *
	uint32_t EPSecIPHi;	//176 xb0   *
	uint32_t EPSecIPMidHi;	//180 xb4   *
	uint32_t EPSecIPMidLow;	//184 xb8   *
	uint32_t EPSecIPLow;	//188 xbc   *
	uint32_t IPReassemblyTimeout;	//192 xc0   *
	uint32_t EthMaxFramePayload;	//196 xc4   *
	uint32_t TCPMaxWindowSize;	//200 xc8   *
	uint32_t TCPCurrentTimestampHi;	//204 xcc   *
	uint32_t TCPCurrentTimestampLow;	//208 xd0   *
	uint32_t LocalRAMAddress;	//212 xd4   *
	uint32_t LocalRAMData;	//216 xd8   *
	uint32_t PCSReserved1;	//220 xdc   *
	uint32_t gp_out;	//224 xe0   *
	uint32_t gp_in;		//228 xe4   *
	uint32_t ProbeMuxAddr;	//232 xe8   *
	uint32_t ProbeMuxData;	//236 xec   *
	uint32_t ERMQueueBaseAddr0;	//240 xf0   *
	uint32_t ERMQueueBaseAddr1;	//244 xf4   *
	uint32_t MACConfiguration;	//248 xf8   *
	uint32_t port_err_status;	//252 xfc  COR
} PORT_CTRL_STAT_REGS, *PPORT_CTRL_STAT_REGS;

typedef struct _HOST_MEM_CFG_REGS {
	uint32_t NetRequestQueueOut;	// 80 x50   *
	uint32_t NetRequestQueueOutAddrHi;	// 84 x54   *
	uint32_t NetRequestQueueOutAddrLow;	// 88 x58   *
	uint32_t NetRequestQueueBaseAddrHi;	// 92 x5c   *
	uint32_t NetRequestQueueBaseAddrLow;	// 96 x60   *
	uint32_t NetRequestQueueLength;	//100 x64   *
	uint32_t NetResponseQueueIn;	//104 x68   *
	uint32_t NetResponseQueueInAddrHi;	//108 x6c   *
	uint32_t NetResponseQueueInAddrLow;	//112 x70   *
	uint32_t NetResponseQueueBaseAddrHi;	//116 x74   *
	uint32_t NetResponseQueueBaseAddrLow;	//120 x78   *
	uint32_t NetResponseQueueLength;	//124 x7c   *
	uint32_t req_q_out;	//128 x80   *
	uint32_t RequestQueueOutAddrHi;	//132 x84   *
	uint32_t RequestQueueOutAddrLow;	//136 x88   *
	uint32_t RequestQueueBaseAddrHi;	//140 x8c   *
	uint32_t RequestQueueBaseAddrLow;	//144 x90   *
	uint32_t RequestQueueLength;	//148 x94   *
	uint32_t ResponseQueueIn;	//152 x98   *
	uint32_t ResponseQueueInAddrHi;	//156 x9c   *
	uint32_t ResponseQueueInAddrLow;	//160 xa0   *
	uint32_t ResponseQueueBaseAddrHi;	//164 xa4   *
	uint32_t ResponseQueueBaseAddrLow;	//168 xa8   *
	uint32_t ResponseQueueLength;	//172 xac   *
	uint32_t NetRxLargeBufferQueueOut;	//176 xb0   *
	uint32_t NetRxLargeBufferQueueBaseAddrHi;	//180 xb4   *
	uint32_t NetRxLargeBufferQueueBaseAddrLow;	//184 xb8   *
	uint32_t NetRxLargeBufferQueueLength;	//188 xbc   *
	uint32_t NetRxLargeBufferLength;	//192 xc0   *
	uint32_t NetRxSmallBufferQueueOut;	//196 xc4   *
	uint32_t NetRxSmallBufferQueueBaseAddrHi;	//200 xc8   *
	uint32_t NetRxSmallBufferQueueBaseAddrLow;	//204 xcc   *
	uint32_t NetRxSmallBufferQueueLength;	//208 xd0   *
	uint32_t NetRxSmallBufferLength;	//212 xd4   *
	uint32_t HMCReserved0[10];	//216 xd8   *
} HOST_MEM_CFG_REGS, *PHOST_MEM_CFG_REGS;

typedef struct _LOCAL_RAM_CFG_REGS {
	uint32_t BufletSize;	// 80 x50   *
	uint32_t BufletMaxCount;	// 84 x54   *
	uint32_t BufletCurrCount;	// 88 x58   *
	uint32_t BufletPauseThresholdCount;	// 92 x5c   *
	uint32_t BufletTCPWinThresholdHi;	// 96 x60   *
	uint32_t BufletTCPWinThresholdLow;	//100 x64   *
	uint32_t IPHashTableBaseAddr;	//104 x68   *
	uint32_t IPHashTableSize;	//108 x6c   *
	uint32_t TCPHashTableBaseAddr;	//112 x70   *
	uint32_t TCPHashTableSize;	//116 x74   *
	uint32_t NCBAreaBaseAddr;	//120 x78   *
	uint32_t NCBMaxCount;	//124 x7c   *
	uint32_t NCBCurrCount;	//128 x80   *
	uint32_t DRBAreaBaseAddr;	//132 x84   *
	uint32_t DRBMaxCount;	//136 x88   *
	uint32_t DRBCurrCount;	//140 x8c   *
	uint32_t LRCReserved[28];	//144 x90   *
} LOCAL_RAM_CFG_REGS, *PLOCAL_RAM_CFG_REGS;

typedef struct _PROT_STAT_REGS {
	uint32_t MACTxFrameCount;	// 80 x50   R
	uint32_t MACTxByteCount;	// 84 x54   R
	uint32_t MACRxFrameCount;	// 88 x58   R
	uint32_t MACRxByteCount;	// 92 x5c   R
	uint32_t MACCRCErrCount;	// 96 x60   R
	uint32_t MACEncErrCount;	//100 x64   R
	uint32_t MACRxLengthErrCount;	//104 x68   R
	uint32_t IPTxPacketCount;	//108 x6c   R
	uint32_t IPTxByteCount;	//112 x70   R
	uint32_t IPTxFragmentCount;	//116 x74   R
	uint32_t IPRxPacketCount;	//120 x78   R
	uint32_t IPRxByteCount;	//124 x7c   R
	uint32_t IPRxFragmentCount;	//128 x80   R
	uint32_t IPDatagramReassemblyCount;	//132 x84   R
	uint32_t IPV6RxPacketCount;	//136 x88   R
	uint32_t IPErrPacketCount;	//140 x8c   R
	uint32_t IPReassemblyErrCount;	//144 x90   R
	uint32_t TCPTxSegmentCount;	//148 x94   R
	uint32_t TCPTxByteCount;	//152 x98   R
	uint32_t TCPRxSegmentCount;	//156 x9c   R
	uint32_t TCPRxByteCount;	//160 xa0   R
	uint32_t TCPTimerExpCount;	//164 xa4   R
	uint32_t TCPRxAckCount;	//168 xa8   R
	uint32_t TCPTxAckCount;	//172 xac   R
	uint32_t TCPRxErrOOOCount;	//176 xb0   R
	uint32_t PSReserved0;	//180 xb4   *
	uint32_t TCPRxWindowProbeUpdateCount;	//184 xb8   R
	uint32_t ECCErrCorrectionCount;	//188 xbc   R
	uint32_t PSReserved1[16];	//192 xc0   *
} PROT_STAT_REGS, *PPROT_STAT_REGS;

#define MBOX_REG_COUNT                          8

// remote register set (access via PCI memory read/write)
typedef struct isp_reg_t {
	uint32_t mailbox[MBOX_REG_COUNT];

	uint32_t flash_address;	/* 0x20 */
	uint32_t flash_data;
	uint32_t ctrl_status;

	union {
		struct {
			uint32_t nvram;
			uint32_t reserved1[2];	/* 0x30 */
		} __attribute__ ((packed)) isp4010;
		struct {
			uint32_t intr_mask;
			uint32_t nvram;	/* 0x30 */
			uint32_t semaphore;
		} __attribute__ ((packed)) isp4022;
	} u1;

	uint32_t req_q_in;	/* SCSI Request Queue Producer Index */
	uint32_t rsp_q_out;	/* SCSI Completion Queue Consumer Index */

	uint32_t reserved2[4];	/* 0x40 */

	union {
		struct {
			uint32_t ext_hw_conf;	/* 0x50 */
			uint32_t flow_ctrl;
			uint32_t port_ctrl;
			uint32_t port_status;

			uint32_t reserved3[8];	/* 0x60 */

			uint32_t req_q_out;	/* 0x80 */

			uint32_t reserved4[23];	/* 0x84 */

			uint32_t gp_out;	/* 0xe0 */
			uint32_t gp_in;

			uint32_t reserved5[5];

			uint32_t port_err_status;	/* 0xfc */
		} __attribute__ ((packed)) isp4010;
		struct {
			union {
				PORT_CTRL_STAT_REGS p0;
				HOST_MEM_CFG_REGS p1;
				LOCAL_RAM_CFG_REGS p2;
				PROT_STAT_REGS p3;
				uint32_t r_union[44];
			};

		} __attribute__ ((packed)) isp4022;
	} u2;
} isp_reg_t;			//256 x100

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

#define QL4XXX_LOCK_DRVR_WAIT(a) \
{ \
	int i = 0; \
	while (1) { \
		if (QL4XXX_LOCK_DRVR(a) == 0) { \
			set_current_state(TASK_UNINTERRUPTIBLE); \
			schedule_timeout(10); \
			if (!i) { \
				DEBUG2(printk("scsi%ld: %s: Waiting for " \
				    "Global Init Semaphore...n", a->host_no, \
				    __func__)); \
				i++; \
			} \
		} else { \
			DEBUG2(printk("scsi%ld: %s: Global Init Semaphore " \
			    "acquired.n", a->host_no, __func__)); \
			break; \
		} \
	} \
}

/* Page # defines for 4022 */
#define PORT_CTRL_STAT_PAGE                     0	/* 4022 */
#define HOST_MEM_CFG_PAGE                       1	/* 4022 */
#define LOCAL_RAM_CFG_PAGE                      2	/* 4022 */
#define PROT_STAT_PAGE                          3	/* 4022 */

/* Register Mask - sets corresponding mask bits in the upper word */
#define SET_RMASK(val)	((val & 0xffff) | (val << 16))
#define CLR_RMASK(val)	(0 | (val << 16))

// ctrl_status definitions
#define CSR_SCSI_PAGE_SELECT                    0x00000003
#define CSR_SCSI_INTR_ENABLE                    0x00000004	/* 4010 */
#define CSR_SCSI_RESET_INTR                     0x00000008
#define CSR_SCSI_COMPLETION_INTR                0x00000010
#define CSR_SCSI_PROCESSOR_INTR                 0x00000020
#define CSR_INTR_RISC                           0x00000040
#define CSR_BOOT_ENABLE                         0x00000080
#define CSR_NET_PAGE_SELECT                     0x00000300	/* 4010 */
#define CSR_NET_INTR_ENABLE                     0x00000400	/* 4010 */
#define CSR_FUNC_NUM                            0x00000700	/* 4022 */
#define CSR_PCI_FUNC_NUM_MASK                   0x00000300	/* 4022 */
#define CSR_NET_RESET_INTR                      0x00000800	/* 4010 */
#define CSR_NET_COMPLETION_INTR                 0x00001000	/* 4010 */
#define CSR_FORCE_SOFT_RESET                    0x00002000	/* 4022 */
#define CSR_FATAL_ERROR                         0x00004000
#define CSR_SOFT_RESET                          0x00008000
#define ISP_CONTROL_FN_MASK     		CSR_FUNC_NUM
#define ISP_CONTROL_FN0_NET     		0x0400
#define ISP_CONTROL_FN0_SCSI    		0x0500
#define ISP_CONTROL_FN1_NET     		0x0600
#define ISP_CONTROL_FN1_SCSI    		0x0700

#define INTR_PENDING                            (CSR_SCSI_COMPLETION_INTR | CSR_SCSI_PROCESSOR_INTR | CSR_SCSI_RESET_INTR)

/* ISP InterruptMask definitions */
#define IMR_SCSI_INTR_ENABLE                    0x00000004	/* 4022 */

/* ISP 4022 nvram definitions */
#define NVR_WRITE_ENABLE			0x00000010	/* 4022 */

// ISP port_ctrl definitions
#define PCR_CONFIG_COMPLETE			0x00008000	/* 4022 */
#define PCR_BIOS_BOOTED_FIRMWARE		0x00008000	/* 4010 */
#define PCR_ENABLE_SERIAL_DATA			0x00001000	/* 4010 */
#define PCR_SERIAL_DATA_OUT			0x00000800	/* 4010 */
#define PCR_ENABLE_SERIAL_CLOCK			0x00000400	/* 4010 */
#define PCR_SERIAL_CLOCK			0x00000200	/* 4010 */

// ISP port_status definitions
#define PSR_CONFIG_COMPLETE			0x00000001	/* 4010 */
#define PSR_INIT_COMPLETE			0x00000200

// ISP Semaphore definitions
#define SR_FIRWMARE_BOOTED			0x00000001

// ISP General Purpose Output definitions
#define GPOR_TOPCAT_RESET                  	0x00000004

// shadow registers (DMA'd from HA to system memory.  read only)
typedef struct {
	/* SCSI Request Queue Consumer Index */
	uint32_t req_q_out;	// 0 x0   R

	/* SCSI Completion Queue Producer Index */
	uint32_t rsp_q_in;	// 4 x4   R
} shadow_regs_t;		// 8 x8

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

// External hardware configuration register
typedef union _EXTERNAL_HW_CONFIG_REG {
	struct {
		uint32_t bReserved0:1;
		uint32_t bSDRAMProtectionMethod:2;
		uint32_t bSDRAMBanks:1;
		uint32_t bSDRAMChipWidth:1;
		uint32_t bSDRAMChipSize:2;
		uint32_t bParityDisable:1;
		uint32_t bExternalMemoryType:1;
		uint32_t bFlashBIOSWriteEnable:1;
		uint32_t bFlashUpperBankSelect:1;
		uint32_t bWriteBurst:2;
		uint32_t bReserved1:3;
		uint32_t bMask:16;
	};
	uint32_t Asuint32_t;
} EXTERNAL_HW_CONFIG_REG, *PEXTERNAL_HW_CONFIG_REG;

/*************************************************************************
 *
 *		Mailbox Commands Structures and Definitions
 *
 *************************************************************************/

// Mailbox command definitions
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

// Mailbox status definitions
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
	uint8_t Version;	/* 00 */
	uint8_t Control;	/* 01 */

	uint16_t FwOptions;	/* 02-03 */
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

	uint16_t ExecThrottle;	/* 04-05 */
	uint8_t RetryCount;	/* 06 */
	uint8_t RetryDelay;	/* 07 */
	uint16_t MaxEthFrPayloadSize;	/* 08-09 */
	uint16_t AddFwOptions;	/* 0A-0B */
#define  ADDFWOPT_AUTOCONNECT_DISABLE     0x0002
#define  ADDFWOPT_SUSPEND_ON_FW_ERROR     0x0001

	uint8_t HeartbeatInterval;	/* 0C */
	uint8_t InstanceNumber;	/* 0D */
	uint16_t RES2;		/* 0E-0F */
	uint16_t ReqQConsumerIndex;	/* 10-11 */
	uint16_t ComplQProducerIndex;	/* 12-13 */
	uint16_t ReqQLen;	/* 14-15 */
	uint16_t ComplQLen;	/* 16-17 */
	uint32_t ReqQAddrLo;	/* 18-1B */
	uint32_t ReqQAddrHi;	/* 1C-1F */
	uint32_t ComplQAddrLo;	/* 20-23 */
	uint32_t ComplQAddrHi;	/* 24-27 */
	uint32_t ShadowRegBufAddrLo;	/* 28-2B */
	uint32_t ShadowRegBufAddrHi;	/* 2C-2F */

	uint16_t iSCSIOptions;	/* 30-31 */
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

	uint16_t TCPOptions;	/* 32-33 */
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

	uint16_t IPOptions;	/* 34-35 */
#define  IPOPT_FRAG_DISABLE               0x0010
#define  IPOPT_PAUSE_FRAME_ENABLE         0x0002
#define  IPOPT_IP_ADDRESS_VALID           0x0001

	uint16_t MaxPDUSize;	/* 36-37 */
	uint16_t RcvMarkerInt;	/* 38-39 */
	uint16_t SndMarkerInt;	/* 3A-3B */
	uint16_t InitMarkerlessInt;	/* 3C-3D *///FIXME: Reserved in spec, but IOCTL struct uses it
	uint16_t FirstBurstSize;	/* 3E-3F */
	uint16_t DefaultTime2Wait;	/* 40-41 */
	uint16_t DefaultTime2Retain;	/* 42-43 */
	uint16_t MaxOutStndngR2T;	/* 44-45 */
	uint16_t KeepAliveTimeout;	/* 46-47 */
	uint16_t PortNumber;	/* 48-49 */
	uint16_t MaxBurstSize;	/* 4A-4B */
	uint32_t RES4;		/* 4C-4F */
	uint8_t IPAddr[4];	/* 50-53 */
	uint8_t RES5[12];	/* 54-5F */
	uint8_t SubnetMask[4];	/* 60-63 */
	uint8_t RES6[12];	/* 64-6F */
	uint8_t GatewayIPAddr[4];	/* 70-73 */
	uint8_t RES7[12];	/* 74-7F */
	uint8_t PriDNSIPAddr[4];	/* 80-83 */
	uint8_t SecDNSIPAddr[4];	/* 84-87 */
	uint8_t RES8[8];	/* 88-8F */
	uint8_t Alias[32];	/* 90-AF */
	uint8_t TargAddr[8];	/* B0-B7 *///FIXME: Remove??
	uint8_t CHAPNameSecretsTable[8];	/* B8-BF */
	uint8_t EthernetMACAddr[6];	/* C0-C5 */
	uint16_t TargetPortalGroup;	/* C6-C7 */
	uint8_t SendScale;	/* C8    */
	uint8_t RecvScale;	/* C9    */
	uint8_t TypeOfService;	/* CA    */
	uint8_t Time2Live;	/* CB    */
	uint16_t VLANPriority;	/* CC-CD */
	uint16_t Reserved8;	/* CE-CF */
	uint8_t SecIPAddr[4];	/* D0-D3 */
	uint8_t Reserved9[12];	/* D4-DF */
	uint8_t iSNSIPAddr[4];	/* E0-E3 */
	uint16_t iSNSServerPortNumber;	/* E4-E5 */
	uint8_t Reserved10[10];	/* E6-EF */
	uint8_t SLPDAIPAddr[4];	/* F0-F3 */
	uint8_t Reserved11[12];	/* F4-FF */
	uint8_t iSCSINameString[256];	/* 100-1FF */
} INIT_FW_CTRL_BLK;

typedef struct {
	INIT_FW_CTRL_BLK init_fw_cb;
	uint32_t Cookie;
#define INIT_FW_CTRL_BLK_COOKIE 	0x11BEAD5A
} FLASH_INIT_FW_CTRL_BLK;

/*************************************************************************/

typedef struct _DEV_DB_ENTRY {
	uint8_t options;	/* 00 */
#define  DDB_OPT_DISABLE                  0x08	/* do not connect to device */
#define  DDB_OPT_ACCESSGRANTED            0x04
#define  DDB_OPT_TARGET                   0x02	/* device is a target */
#define  DDB_OPT_INITIATOR                0x01	/* device is an initiator */

	uint8_t control;	/* 01 */
#define  DDB_CTRL_DATABASE_ENTRY_STATE    0xC0
#define  DDB_CTRL_SESSION_RECOVERY        0x10
#define  DDB_CTRL_SENDING                 0x08
#define  DDB_CTRL_XFR_PENDING             0x04
#define  DDB_CTRL_QUEUE_ABORTED           0x02
#define  DDB_CTRL_LOGGED_IN               0x01

	uint16_t exeThrottle;	/* 02-03 */
	uint16_t exeCount;	/* 04-05 */
	uint8_t retryCount;	/* 06    */
	uint8_t retryDelay;	/* 07    */
	uint16_t iSCSIOptions;	/* 08-09 */
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

	uint16_t TCPOptions;	/* 0A-0B */
#define DDB_TOPT_NAGLE_DISABLE            0x0020
#define DDB_TOPT_TIMER_SCALE_MASK         0x000E
#define DDB_TOPT_TIME_STAMP_ENABLE        0x0001

	uint16_t IPOptions;	/* 0C-0D */
#define DDB_IPOPT_FRAG_DISABLE     	     0x0002
#define DDB_IPOPT_IP_ADDRESS_VALID        0x0001

	uint16_t maxPDUSize;	/* 0E-0F */
	uint16_t rcvMarkerInt;	/* 10-11 */
	uint16_t sndMarkerInt;	/* 12-13 */
	uint16_t iSCSIMaxSndDataSegLen;	/* 14-15 */
	uint16_t firstBurstSize;	/* 16-17 */
	uint16_t minTime2Wait;	/* 18-19 : RA :default_time2wait */
	uint16_t maxTime2Retain;	/* 1A-1B */
	uint16_t maxOutstndngR2T;	/* 1C-1D */
	uint16_t keepAliveTimeout;	/* 1E-1F */
	uint8_t ISID[6];	/* 20-25 big-endian, must be converted to little-endian */
	uint16_t TSID;		/* 26-27 */
	uint16_t portNumber;	/* 28-29 */
	uint16_t maxBurstSize;	/* 2A-2B */
	uint16_t taskMngmntTimeout;	/* 2C-2D */
	uint16_t reserved1;	/* 2E-2F */
	uint8_t ipAddr[0x10];	/* 30-3F */
	uint8_t iSCSIAlias[0x20];	/* 40-5F */
	uint8_t targetAddr[0x20];	/* 60-7F */
	uint8_t userID[0x20];	/* 80-9F */
	uint8_t password[0x20];	/* A0-BF */
	uint8_t iscsiName[0x100];	/* C0-1BF : xxzzy Make this a pointer to a string so we don't
					   have to reserve soooo much RAM */
	uint16_t ddbLink;	/* 1C0-1C1 */
	uint16_t CHAPTableIndex;	/* 1C2-1C3 */
	uint16_t TargetPortalGroup;	/* 1C4-1C5 */
	uint16_t reserved2[2];	/* 1C6-1C7 */
	uint32_t statSN;	/* 1C8-1CB */
	uint32_t expStatSN;	/* 1CC-1CF */
	uint16_t reserved3[0x2C];	/* 1D0-1FB */
	uint16_t ddbValidCookie;	/* 1FC-1FD */
	uint16_t ddbValidSize;	/* 1FE-1FF */
} DEV_DB_ENTRY;

/*************************************************************************/

// Flash definitions
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

#define MAX_FLASH_SZ                  0x400000	/* 4M flash */
#define FLASH_DEFAULTBLOCKSIZE        0x20000
#define FLASH_EOF_OFFSET              FLASH_DEFAULTBLOCKSIZE - 8	/* 4 bytes for EOF signature */
#define FLASH_FILESIZE_OFFSET         FLASH_EOF_OFFSET - 4	/* 4 bytes for file size */
#define FLASH_CKSUM_OFFSET            FLASH_FILESIZE_OFFSET - 4	/* 4 bytes for chksum protection */

typedef struct _SYS_INFO_PHYS_ADDR {
	uint8_t address[6];	/* 00-05 */
	uint8_t filler[2];	/* 06-07 */
} SYS_INFO_PHYS_ADDR;

typedef struct _FLASH_SYS_INFO {
	uint32_t cookie;	/* 00-03 */
	uint32_t physAddrCount;	/* 04-07 */
	SYS_INFO_PHYS_ADDR physAddr[4];	/* 08-27 */
	uint8_t vendorId[128];	/* 28-A7 */
	uint8_t productId[128];	/* A8-127 */
	uint32_t serialNumber;	/* 128-12B */

	// PCI Configuration values
	uint32_t pciDeviceVendor;	/* 12C-12F */
	uint32_t pciDeviceId;	/* 130-133 */
	uint32_t pciSubsysVendor;	/* 134-137 */
	uint32_t pciSubsysId;	/* 138-13B */

	// This validates version 1.
	uint32_t crumbs;	/* 13C-13F */

	uint32_t enterpriseNumber;	/* 140-143 */

	uint32_t mtu;		/* 144-147 */
	uint32_t reserved0;	/* 148-14b */
	uint32_t crumbs2;	/* 14c-14f */
	uint8_t acSerialNumber[16];	/* 150-15f */
	uint32_t crumbs3;	/* 160-16f */

	// Leave this last in the struct so it is declared invalid if
	// any new items are added.
	uint32_t reserved1[39];	/* 170-1ff */
} FLASH_SYS_INFO, *PFLASH_SYS_INFO;	/* 200 */

typedef struct _FLASH_DRIVER_INFO {
	uint32_t LinuxDriverCookie;
#define FLASH_LINUX_DRIVER_COOKIE		0x0A1B2C3D
	uint8_t Pad[4];

} FLASH_DRIVER_INFO, *PFLASH_DRIVER_INFO;

typedef struct _CHAP_ENTRY {
	uint16_t link;		//  0 x0
#define CHAP_FLAG_PEER_NAME		0x40
#define CHAP_FLAG_LOCAL_NAME    	0x80

	uint8_t flags;		//  2 x2
#define MIN_CHAP_SECRET_LENGTH  	12
#define MAX_CHAP_SECRET_LENGTH  	100

	uint8_t secretLength;	//  3 x3
	uint8_t secret[MAX_CHAP_SECRET_LENGTH];	//  4 x4
#define MAX_CHAP_CHALLENGE_LENGTH       256

	uint8_t user_name[MAX_CHAP_CHALLENGE_LENGTH];	//104 x68
	uint16_t reserved;	//360 x168
#define CHAP_COOKIE                     0x4092

	uint16_t cookie;	//362 x16a
} CHAP_ENTRY, *PCHAP_ENTRY;	//364 x16c

/*************************************************************************/

typedef struct _CRASH_RECORD {
	uint16_t fw_major_version;	/* 00 - 01 */
	uint16_t fw_minor_version;	/* 02 - 03 */
	uint16_t fw_patch_version;	/* 04 - 05 */
	uint16_t fw_build_version;	/* 06 - 07 */

	uint8_t build_date[16];	/* 08 - 17 */
	uint8_t build_time[16];	/* 18 - 27 */
	uint8_t build_user[16];	/* 28 - 37 */
	uint8_t card_serial_num[16];	/* 38 - 47 */

	uint32_t time_of_crash_in_secs;	/* 48 - 4B */
	uint32_t time_of_crash_in_ms;	/* 4C - 4F */

	uint16_t out_RISC_sd_num_frames;	/* 50 - 51 */
	uint16_t OAP_sd_num_words;	/* 52 - 53 */
	uint16_t IAP_sd_num_frames;	/* 54 - 55 */
	uint16_t in_RISC_sd_num_words;	/* 56 - 57 */

	uint8_t reserved1[28];	/* 58 - 7F */

	uint8_t out_RISC_reg_dump[256];	/* 80 -17F */
	uint8_t in_RISC_reg_dump[256];	/*180 -27F */
	uint8_t in_out_RISC_stack_dump[0];	/*280 - ??? */
} CRASH_RECORD, *PCRASH_RECORD;

/*************************************************************************/

#define MAX_CONN_EVENT_LOG_ENTRIES	100

typedef struct _CONN_EVENT_LOG_ENTRY {
	uint32_t timestamp_sec;	/* 00 - 03 seconds since boot */
	uint32_t timestamp_ms;	/* 04 - 07 milliseconds since boot */
	uint16_t device_index;	/* 08 - 09  */
	uint16_t fw_conn_state;	/* 0A - 0B  */
	uint8_t event_type;	/* 0C - 0C  */
	uint8_t error_code;	/* 0D - 0D  */
	uint16_t error_code_detail;	/* 0E - 0F  */
	uint8_t num_consecutive_events;	/* 10 - 10  */
	uint8_t rsvd[3];	/* 11 - 13  */
} CONN_EVENT_LOG_ENTRY, *PCONN_EVENT_LOG_ENTRY;

/*************************************************************************
 *
 *				IOCB Commands Structures and Definitions
 *
 *************************************************************************/
#define IOCB_MAX_CDB_LEN            16	/* Bytes in a CBD */
#define IOCB_MAX_SENSEDATA_LEN      32	/* Bytes of sense data */
#define IOCB_MAX_EXT_SENSEDATA_LEN  60	/* Bytes of extended sense data */
#define IOCB_MAX_DSD_CNT             1	/* DSDs per noncontinuation type IOCB */
#define IOCB_CONT_MAX_DSD_CNT        5	/* DSDs per Continuation */
#define CTIO_MAX_SENSEDATA_LEN      24	/* Bytes of sense data in a CTIO */

#define RESERVED_BYTES_MARKER       40	/* Reserved Bytes at end of Marker */
#define RESERVED_BYTES_INOT         28	/* Reserved Bytes at end of Immediate Notify */
#define RESERVED_BYTES_NOTACK       28	/* Reserved Bytes at end of Notify Acknowledge */
#define RESERVED_BYTES_CTIO          2	/* Reserved Bytes in middle of CTIO */

#define MAX_MBX_COUNT               14	/* Maximum number of mailboxes in MBX IOCB */

#define ISCSI_MAX_NAME_BYTECNT      256	/* Bytes in a target name */

#define IOCB_ENTRY_SIZE       	    0x40

/* IOCB header structure */
typedef struct _HEADER {
	uint8_t entryType;
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

	uint8_t entryStatus;
#define ES_MASK                 0x3E
#define ES_SUPPRESS_COMPL_INT   0x01
#define ES_BUSY                 0x02
#define ES_INVALID_ENTRY_TYPE   0x04
#define ES_INVALID_ENTRY_PARAM  0x08
#define ES_INVALID_ENTRY_COUNT  0x10
#define ES_INVALID_ENTRY_ORDER  0x20
	uint8_t systemDefined;
	uint8_t entryCount;

	/* SyetemDefined definition */
#define SD_PASSTHRU_IOCB        0x01
} HEADER;

/* Genric queue entry structure*/
typedef struct QUEUE_ENTRY {
	uint8_t data[60];
	uint32_t signature;

} QUEUE_ENTRY;

/* 64 bit addressing segment counts*/

#define COMMAND_SEG_A64             1
#define CONTINUE_SEG_A64            5
#define CONTINUE_SEG_A64_MINUS1     4

/* 64 bit addressing segment definition*/

typedef struct DATA_SEG_A64 {
	struct {
		uint32_t addrLow;
		uint32_t addrHigh;

	} base;

	uint32_t count;

} DATA_SEG_A64;

/* Command Type 3 entry structure*/

typedef struct _COMMAND_T3_ENTRY {
	HEADER hdr;		/* 00-03 */

	uint32_t handle;	/* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t connection_id;	/* 0A-0B */

	uint8_t control_flags;	/* 0C */
#define CF_IMMEDIATE		   0x80

	/* data direction  (bits 5-6) */
#define CF_WRITE                0x20
#define CF_READ                 0x40
#define CF_NO_DATA              0x00
#define CF_DIRECTION_MASK       0x60

	/* misc  (bits 4-3) */
#define CF_DSD_PTR_ENABLE	   0x10	/* 4010 only */
#define CF_CMD_PTR_ENABLE	   0x08	/* 4010 only */

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
	uint8_t state_flags;	/* 0D */
	uint8_t cmdRefNum;	/* 0E */
	uint8_t reserved1;	/* 0F */
	uint8_t cdb[IOCB_MAX_CDB_LEN];	/* 10-1F */
	uint8_t lun[8];		/* 20-27 */
	uint32_t cmdSeqNum;	/* 28-2B */
	uint16_t timeout;	/* 2C-2D */
	uint16_t dataSegCnt;	/* 2E-2F */
	uint32_t ttlByteCnt;	/* 30-33 */
	DATA_SEG_A64 dataseg[COMMAND_SEG_A64];	/* 34-3F */

} COMMAND_T3_ENTRY;

typedef struct _COMMAND_T4_ENTRY {
	HEADER hdr;		/* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t connection_id;	/* 0A-0B */
	uint8_t control_flags;	/* 0C */

	/* STATE FLAGS FIELD IS A PLACE HOLDER. THE FW WILL SET BITS IN THIS FIELD
	   AS THE COMMAND IS PROCESSED. WHEN THE IOCB IS CHANGED TO AN IOSB THIS
	   FIELD WILL HAVE THE STATE FLAGS SET PROPERLY.
	 */
	uint8_t state_flags;	/* 0D */
	uint8_t cmdRefNum;	/* 0E */
	uint8_t reserved1;	/* 0F */
	uint8_t cdb[IOCB_MAX_CDB_LEN];	/* 10-1F */
	uint8_t lun[8];		/* 20-27 */
	uint32_t cmdSeqNum;	/* 28-2B */
	uint16_t timeout;	/* 2C-2D */
	uint16_t dataSegCnt;	/* 2E-2F */
	uint32_t ttlByteCnt;	/* 30-33 */

	/* WE ONLY USE THE ADDRESS FIELD OF THE FOLLOWING STRUCT.
	   THE COUNT FIELD IS RESERVED */
	DATA_SEG_A64 dataseg[COMMAND_SEG_A64];	/* 34-3F */
} COMMAND_T4_ENTRY;

/* Continuation Type 1 entry structure*/
typedef struct _CONTINUATION_T1_ENTRY {
	HEADER hdr;

	DATA_SEG_A64 dataseg[CONTINUE_SEG_A64];

} CONTINUATION_T1_ENTRY;

/* Status Continuation Type entry structure*/
typedef struct _STATUS_CONTINUATION_ENTRY {
	HEADER hdr;

	uint8_t extSenseData[IOCB_MAX_EXT_SENSEDATA_LEN];

} STATUS_CONTINUATION_ENTRY;

/* Parameterize for 64 or 32 bits */
#define COMMAND_SEG     COMMAND_SEG_A64
#define CONTINUE_SEG    CONTINUE_SEG_A64

#define COMMAND_ENTRY   COMMAND_T3_ENTRY
#define CONTINUE_ENTRY  CONTINUATION_T1_ENTRY

#define ET_COMMAND      ET_CMND_T3
#define ET_CONTINUE     ET_CONT_T1

/* Marker entry structure*/
typedef struct _MARKER_ENTRY {
	HEADER hdr;		/* 00-03 */

	uint32_t system_defined;	/* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t modifier;	/* 0A-0B */
#define MM_LUN_RESET         0
#define MM_TARGET_WARM_RESET 1
#define MM_TARGET_COLD_RESET 2
#define MM_CLEAR_ACA    	3
#define MM_CLEAR_TASK_SET    4
#define MM_ABORT_TASK_SET    5

	uint16_t flags;		/* 0C-0D */
	uint16_t reserved1;	/* 0E-0F */
	uint8_t lun[8];		/* 10-17 */
	uint64_t reserved2;	/* 18-1F */
	uint64_t reserved3;	/* 20-27 */
	uint64_t reserved4;	/* 28-2F */
	uint64_t reserved5;	/* 30-37 */
	uint64_t reserved6;	/* 38-3F */
} MARKER_ENTRY;

/* Status entry structure*/
typedef struct _STATUS_ENTRY {
	HEADER hdr;		/* 00-03 */

	uint32_t handle;	/* 04-07 */

	uint8_t scsiStatus;	/* 08 */
#define SCSI_STATUS_MASK                  0xFF
#define SCSI_STATUS                       0xFF
#define SCSI_GOOD                         0x00
#define SCSI_CHECK_CONDITION              0x02

	uint8_t iscsiFlags;	/* 09 */
#define ISCSI_FLAG_RESIDUAL_UNDER         0x02
#define ISCSI_FLAG_RESIDUAL_OVER          0x04
#define ISCSI_FLAG_RESIDUAL_UNDER_BIREAD  0x08
#define ISCSI_FLAG_RESIDUAL_OVER_BIREAD   0x10

	uint8_t iscsiResponse;	/* 0A */
#define ISCSI_RSP_COMPLETE                    0x00
#define ISCSI_RSP_TARGET_FAILURE              0x01
#define ISCSI_RSP_DELIVERY_SUBSYS_FAILURE     0x02
#define ISCSI_RSP_UNSOLISITED_DATA_REJECT     0x03
#define ISCSI_RSP_NOT_ENOUGH_UNSOLISITED_DATA 0x04
#define ISCSI_RSP_CMD_IN_PROGRESS             0x05

	uint8_t completionStatus;	/* 0B */
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

	uint8_t reserved1;	/* 0C */

	/* state_flags MUST be at the same location as state_flags in the
	   Command_T3/4_Entry */
	uint8_t state_flags;	/* 0D */
#define STATE_FLAG_SENT_COMMAND           0x01
#define STATE_FLAG_TRANSFERRED_DATA       0x02
#define STATE_FLAG_GOT_STATUS             0x04
#define STATE_FLAG_LOGOUT_SENT            0x10

	uint16_t senseDataByteCnt;	/* 0E-0F */
	uint32_t residualByteCnt;	/* 10-13 */
	uint32_t bidiResidualByteCnt;	/* 14-17 */
	uint32_t expSeqNum;	/* 18-1B */
	uint32_t maxCmdSeqNum;	/* 1C-1F */
	uint8_t senseData[IOCB_MAX_SENSEDATA_LEN];	/* 20-3F */

} STATUS_ENTRY;

/*
 * Performance Status Entry where up to 30 handles can be posted in a
 * single IOSB. Handles are of 16 bit value.
 */
typedef struct _PERFORMANCE_STATUS_ENTRY {
	uint8_t entryType;
	uint8_t entryCount;
	uint16_t handleCount;

#define MAX_STATUS_HANDLE  30
	uint16_t handleArray[MAX_STATUS_HANDLE];

} PERFORMANCE_STATUS_ENTRY;

typedef struct _IMMEDIATE_NOTIFY_ENTRY {
	HEADER hdr;
	uint32_t handle;
	uint16_t initiator;
	uint16_t InitSessionID;
	uint16_t ConnectionID;
	uint16_t TargSessionID;
	uint16_t inotStatus;
#define INOT_STATUS_ABORT_TASK      0x0020
#define INOT_STATUS_LOGIN_RECVD     0x0021
#define INOT_STATUS_LOGOUT_RECVD    0x0022
#define INOT_STATUS_LOGGED_OUT      0x0029
#define INOT_STATUS_RESTART_RECVD   0x0030
#define INOT_STATUS_MSG_RECVD       0x0036
#define INOT_STATUS_TSK_REASSIGN    0x0037

	uint16_t taskFlags;
#define TASK_FLAG_CLEAR_ACA         0x4000
#define TASK_FLAG_COLD_RESET        0x2000
#define TASK_FLAG_WARM_RESET        0x0800
#define TASK_FLAG_LUN_RESET         0x1000
#define TASK_FLAG_CLEAR_TASK_SET    0x0400
#define TASK_FLAG_ABORT_TASK_SET    0x0200

	uint32_t refTaskTag;
	uint8_t lun[8];
	uint32_t inotTaskTag;
	uint8_t res3[RESERVED_BYTES_INOT];
} IMMEDIATE_NOTIFY_ENTRY;

typedef struct _NOTIFY_ACK_ENTRY {
	HEADER hdr;
	uint32_t handle;
	uint16_t initiator;
	uint16_t res1;
	uint16_t flags;
	uint8_t responseCode;
	uint8_t qualifier;
	uint16_t notAckStatus;
	uint16_t taskFlags;
#define NACK_FLAG_RESPONSE_CODE_VALID 0x0010

	uint32_t refTaskTag;
	uint8_t lun[8];
	uint32_t inotTaskTag;
	uint8_t res3[RESERVED_BYTES_NOTACK];
} NOTIFY_ACK_ENTRY;

typedef struct _ATIO_ENTRY {
	HEADER hdr;		/* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t initiator;	/* 08-09 */
	uint16_t connectionID;	/* 0A-0B */
	uint32_t taskTag;	/* 0C-0f */
	uint8_t scsiCDB[IOCB_MAX_CDB_LEN];	/* 10-1F */
	uint8_t LUN[8];		/* 20-27 */
	uint8_t cmdRefNum;	/* 28 */

	uint8_t pduType;	/* 29 */
#define PDU_TYPE_NOPOUT                0x00
#define PDU_TYPE_SCSI_CMD              0x01
#define PDU_TYPE_SCSI_TASK_MNGMT_CMD   0x02
#define PDU_TYPE_LOGIN_CMD             0x03
#define PDU_TYPE_TEXT_CMD              0x04
#define PDU_TYPE_SCSI_DATA             0x05
#define PDU_TYPE_LOGOUT_CMD            0x06
#define PDU_TYPE_SNACK                 0x10

	uint16_t atioStatus;	/* 2A-2B */
#define ATIO_CDB_RECVD                 0x003d

	uint16_t reserved1;	/* 2C-2D */

	uint8_t taskCode;	/* 2E */
#define ATIO_TASK_CODE_UNTAGGED        0x00
#define ATIO_TASK_CODE_SIMPLE_QUEUE    0x01
#define ATIO_TASK_CODE_ORDERED_QUEUE   0x02
#define ATIO_TASK_CODE_HEAD_OF_QUEUE   0x03
#define ATIO_TASK_CODE_ACA_QUEUE       0x04

	uint8_t reserved2;	/* 2F */
	uint32_t totalByteCnt;	/* 30-33 */
	uint32_t cmdSeqNum;	/* 34-37 */
	uint64_t immDataBufDesc;	/* 38-3F */
} ATIO_ENTRY;

typedef struct _CTIO3_ENTRY {
	HEADER hdr;		/* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t initiator;	/* 08-09 */
	uint16_t connectionID;	/* 0A-0B */
	uint32_t taskTag;	/* 0C-0F */

	uint8_t flags;		/* 10 */
#define CTIO_FLAG_SEND_SCSI_STATUS     0x01
#define CTIO_FLAG_TERMINATE_COMMAND    0x10
#define CTIO_FLAG_FAST_POST            0x08
#define CTIO_FLAG_FINAL_CTIO           0x80

	/*  NOTE:  Our firmware assumes that the CTIO_FLAG_SEND_DATA and
	   CTIO_FLAG_GET_DATA flags are in the same bit positions
	   as the R and W bits in SCSI Command PDUs, so their values
	   should not be changed!
	 */
#define CTIO_FLAG_SEND_DATA            0x0040	/* (see note) Read Data Flag, send data to initiator       */
#define CTIO_FLAG_GET_DATA             0x0020	/* (see note) Write Data Flag, get data from the initiator */

	uint8_t scsiStatus;	/* 11 */
	uint16_t timeout;	/* 12-13 */
	uint32_t offset;	/* 14-17 */
	uint32_t r2tSN;		/* 18-1B */
	uint32_t expCmdSN;	/* 1C-1F */
	uint32_t maxCmdSN;	/* 20-23 */
	uint32_t dataSN;	/* 24-27 */
	uint32_t residualCount;	/* 28-2B */
	uint16_t reserved;	/* 2C-2D */
	uint16_t segmentCnt;	/* 2E-2F */
	uint32_t totalByteCnt;	/* 30-33 */
	DATA_SEG_A64 dataseg[COMMAND_SEG_A64];	/* 34-3F */
} CTIO3_ENTRY;

typedef struct _CTIO4_ENTRY {
	HEADER hdr;		/* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t initiator;	/* 08-09 */
	uint16_t connectionID;	/* 0A-0B */
	uint32_t taskTag;	/* 0C-0F */
	uint8_t flags;		/* 10 */
	uint8_t scsiStatus;	/* 11 */
	uint16_t timeout;	/* 12-13 */
	uint32_t offset;	/* 14-17 */
	uint32_t r2tSN;		/* 18-1B */
	uint32_t expCmdSN;	/* 1C-1F */
	uint32_t maxCmdSN;	/* 20-23 */
	uint32_t dataSN;	/* 24-27 */
	uint32_t residualCount;	/* 28-2B */
	uint16_t reserved;	/* 2C-2D */
	uint16_t segmentCnt;	/* 2E-2F */
	uint32_t totalByteCnt;	/* 30-33 */
	/* WE ONLY USE THE ADDRESS FROM THE FOLLOWING STRUCTURE THE COUNT FIELD IS
	   RESERVED */
	DATA_SEG_A64 dataseg[COMMAND_SEG_A64];	/* 34-3F */
} CTIO4_ENTRY;

typedef struct _CTIO5_ENTRY {
	HEADER hdr;		/* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t initiator;	/* 08-09 */
	uint16_t connectionID;	/* 0A-0B */
	uint32_t taskTag;	/* 0C-0F */
	uint8_t response;	/* 10 */
	uint8_t scsiStatus;	/* 11 */
	uint16_t timeout;	/* 12-13 */
	uint32_t reserved1;	/* 14-17 */
	uint32_t expR2TSn;	/* 18-1B */
	uint32_t expCmdSn;	/* 1C-1F */
	uint32_t MaxCmdSn;	/* 20-23 */
	uint32_t expDataSn;	/* 24-27 */
	uint32_t residualCnt;	/* 28-2B */
	uint32_t bidiResidualCnt;	/* 2C-2F */
	uint32_t reserved2;	/* 30-33 */
	DATA_SEG_A64 dataseg[1];	/* 34-3F */
} CTIO5_ENTRY;

typedef struct _CTIO6_ENTRY {
	HEADER hdr;		/* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t initiator;	/* 08-09 */
	uint16_t connection;	/* 0A-0B */
	uint32_t taskTag;	/* 0C-0F */
	uint16_t flags;		/* 10-11 */
	uint16_t timeout;	/* 12-13 */
	uint32_t reserved1;	/* 14-17 */
	uint64_t reserved2;	/* 18-1F */
	uint64_t reserved3;	/* 20-27 */
	uint64_t reserved4;	/* 28-2F */
	uint32_t reserved5;	/* 30-33 */
	DATA_SEG_A64 dataseg[1];	/* 34-3F */
} CTIO6_ENTRY;

typedef struct _CTIO_STATUS_ENTRY {
	HEADER hdr;		/* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t initiator;	/* 08-09 */
	uint16_t connectionID;	/* 0A-0B */
	uint32_t taskTag;	/* 0C-0F */
	uint16_t status;	/* 10-11 */
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

	uint16_t timeout;	/* 12-13 */
	uint32_t reserved1;	/* 14-17 */
	uint32_t expR2TSN;	/* 18-1B */
	uint32_t reserved2;	/* 1C-1F */
	uint32_t reserved3;	/* 20-23 */
	uint64_t expDataSN;	/* 24-27 */
	uint32_t residualCount;	/* 28-2B */
	uint32_t reserved4;	/* 2C-2F */
	uint64_t reserved5;	/* 30-37 */
	uint64_t reserved6;	/* 38-3F */
} CTIO_STATUS_ENTRY;

typedef struct _MAILBOX_ENTRY {
	HEADER hdr;
	uint32_t handle;
	uint32_t mbx[MAX_MBX_COUNT];
} MAILBOX_ENTRY;

typedef struct MAILBOX_STATUS_ENTRY {
	HEADER hdr;
	uint32_t handle;
	uint32_t mbx[MAX_MBX_COUNT];
} MAILBOX_STATUS_ENTRY;

typedef struct _PDU_ENTRY {
	uint8_t *Buff;
	uint32_t BuffLen;
	uint32_t SendBuffLen;
	uint32_t RecvBuffLen;
	struct _PDU_ENTRY *Next;
	dma_addr_t DmaBuff;
} PDU_ENTRY, *PPDU_ENTRY;

typedef struct _ISNS_DISCOVERED_TARGET_PORTAL {
	uint8_t IPAddr[4];
	uint16_t PortNumber;
	uint16_t Reserved;
} ISNS_DISCOVERED_TARGET_PORTAL, *PISNS_DISCOVERED_TARGET_PORTAL;

typedef struct _ISNS_DISCOVERED_TARGET {
	uint32_t NumPortals;	/*  00-03 */
#define ISNS_MAX_PORTALS 4
	ISNS_DISCOVERED_TARGET_PORTAL Portal[ISNS_MAX_PORTALS];	/* 04-23 */
	uint32_t DDID;		/*  24-27 */
	uint8_t NameString[256];	/*  28-127 */
	uint8_t Alias[32];	/* 128-147 */
//    uint32_t SecurityBitmap
} ISNS_DISCOVERED_TARGET, *PISNS_DISCOVERED_TARGET;

typedef struct _PASSTHRU0_ENTRY {
	HEADER hdr;		/* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t connectionID;	/* 0A-0B */
#define ISNS_DEFAULT_SERVER_CONN_ID     ((uint16_t)0x8000)

	uint16_t controlFlags;	/* 0C-0D */
#define PT_FLAG_ETHERNET_FRAME   	0x8000
#define PT_FLAG_ISNS_PDU                0x8000
#define PT_FLAG_IP_DATAGRAM             0x4000
#define PT_FLAG_TCP_PACKET              0x2000
#define PT_FLAG_NETWORK_PDU             (PT_FLAG_ETHERNET_FRAME | PT_FLAG_IP_DATAGRAM | PT_FLAG_TCP_PACKET)
#define PT_FLAG_iSCSI_PDU               0x1000
#define PT_FLAG_SEND_BUFFER             0x0200
#define PT_FLAG_WAIT_4_RESPONSE         0x0100
#define PT_FLAG_NO_FAST_POST            0x0080

	uint16_t timeout;	/* 0E-0F */
#define PT_DEFAULT_TIMEOUT              30	// seconds

	DATA_SEG_A64 outDataSeg64;	/* 10-1B */
	uint32_t res1;		/* 1C-1F */
	DATA_SEG_A64 inDataSeg64;	/* 20-2B */
	uint8_t res2[20];	/* 2C-3F */
} PASSTHRU0_ENTRY;

typedef struct _PASSTHRU1_ENTRY {
	HEADER hdr;		/* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t connectionID;	/* 0A-0B */

	uint16_t controlFlags;	/* 0C-0D */
#define PT_FLAG_ETHERNET_FRAME         	0x8000
#define PT_FLAG_IP_DATAGRAM            	0x4000
#define PT_FLAG_TCP_PACKET             	0x2000
#define PT_FLAG_iSCSI_PDU              	0x1000
#define PT_FLAG_SEND_BUFFER            	0x0200
#define PT_FLAG_WAIT_4_REPONSE         	0x0100
#define PT_FLAG_NO_FAST_POST           	0x0080

	uint16_t timeout;	/* 0E-0F */
	DATA_SEG_A64 outDSDList;	/* 10-1B */
	uint32_t outDSDCnt;	/* 1C-1F */
	DATA_SEG_A64 inDSDList;	/* 20-2B */
	uint32_t inDSDCnt;	/* 2C-2F */
	uint8_t res1;		/* 30-3F */

} PASSTHRU1_ENTRY;

typedef struct _PASSTHRU_STATUS_ENTRY {
	HEADER hdr;		/* 00-03 */
	uint32_t handle;	/* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t connectionID;	/* 0A-0B */

	uint8_t completionStatus;	/* 0C */
#define PASSTHRU_STATUS_COMPLETE       		0x01
#define PASSTHRU_STATUS_ERROR          		0x04
#define PASSTHRU_STATUS_INVALID_DATA_XFER            0x06
#define PASSTHRU_STATUS_CMD_TIMEOUT    		0x0B
#define PASSTHRU_STATUS_PCI_ERROR      		0x10
#define PASSTHRU_STATUS_NO_CONNECTION  		0x28

	uint8_t residualFlags;	/* 0D */
#define PASSTHRU_STATUS_DATAOUT_OVERRUN              0x01
#define PASSTHRU_STATUS_DATAOUT_UNDERRUN             0x02
#define PASSTHRU_STATUS_DATAIN_OVERRUN               0x04
#define PASSTHRU_STATUS_DATAIN_UNDERRUN              0x08

	uint16_t timeout;	/* 0E-0F */
	uint16_t portNumber;	/* 10-11 */
	uint8_t res1[10];	/* 12-1B */
	uint32_t outResidual;	/* 1C-1F */
	uint8_t res2[12];	/* 20-2B */
	uint32_t inResidual;	/* 2C-2F */
	uint8_t res4[16];	/* 30-3F */
} PASSTHRU_STATUS_ENTRY;

typedef struct _ASYNCHMSG_ENTRY {
	HEADER hdr;
	uint32_t handle;
	uint16_t target;
	uint16_t connectionID;
	uint8_t lun[8];
	uint16_t iSCSIEvent;
#define AMSG_iSCSI_EVENT_NO_EVENT                  0x0000
#define AMSG_iSCSI_EVENT_TARG_RESET                0x0001
#define AMSG_iSCSI_EVENT_TARGT_LOGOUT              0x0002
#define AMSG_iSCSI_EVENT_CONNECTION_DROPPED        0x0003
#define AMSG_ISCSI_EVENT_ALL_CONNECTIONS_DROPPED   0x0004

	uint16_t SCSIEvent;
#define AMSG_NO_SCSI_EVENT                         0x0000
#define AMSG_SCSI_EVENT                            0x0001

	uint16_t parameter1;
	uint16_t parameter2;
	uint16_t parameter3;
	uint32_t expCmdSn;
	uint32_t maxCmdSn;
	uint16_t senseDataCnt;
	uint16_t reserved;
	uint32_t senseData[IOCB_MAX_SENSEDATA_LEN];
} ASYNCHMSG_ENTRY;

/* Timer entry structure, this is an internal generated structure
   which causes the QLA4000 initiator to send a NOP-OUT or the
   QLA4000 target to send a NOP-IN */

typedef struct _TIMER_ENTRY {
	HEADER hdr;		/* 00-03 */

	uint32_t handle;	/* 04-07 */
	uint16_t target;	/* 08-09 */
	uint16_t connection_id;	/* 0A-0B */

	uint8_t control_flags;	/* 0C */

	/* STATE FLAGS FIELD IS A PLACE HOLDER. THE FW WILL SET BITS IN THIS FIELD
	   AS THE COMMAND IS PROCESSED. WHEN THE IOCB IS CHANGED TO AN IOSB THIS
	   FIELD WILL HAVE THE STATE FLAGS SET PROPERLY.
	 */
	uint8_t state_flags;	/* 0D */
	uint8_t cmdRefNum;	/* 0E */
	uint8_t reserved1;	/* 0F */
	uint8_t cdb[IOCB_MAX_CDB_LEN];	/* 10-1F */
	uint8_t lun[8];		/* 20-27 */
	uint32_t cmdSeqNum;	/* 28-2B */
	uint16_t timeout;	/* 2C-2D */
	uint16_t dataSegCnt;	/* 2E-2F */
	uint32_t ttlByteCnt;	/* 30-33 */
	DATA_SEG_A64 dataseg[COMMAND_SEG_A64];	/* 34-3F */

} TIMER_ENTRY;

#endif				/* _QLA4X_FW_H */
