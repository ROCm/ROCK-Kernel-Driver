/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

#ifndef _H_ELX_HW
#define _H_ELX_HW

/*
 *  Begin Adapter configuration parameters.  Many of these will be replaced
 *  with parameters read from the registry or a configuration file in the 
 *  future.
 */

#define FC_MAX_TRANSFER    0x40000	/* Maximum transfer size per operation */

#define MAX_CONFIGURED_RINGS     3	/* # rings currently used for COMBO */
#define MAX_RINGS                4

#define FF_REG_AREA_SIZE       256	/* size, in bytes, of i/o register area */
#define FF_SLIM_SIZE          4096	/* size, in bytes, of SLIM */

#define OWN_CHIP        1	/* IOCB / Mailbox is owned by FireFly */
#define OWN_HOST        0	/* IOCB / Mailbox is owned by Host */
#define IOCB_WORD_SZ    8

/* defines for type field in fc header */
#define FC_ELS_DATA     0x1
#define FC_LLC_SNAP     0x5
#define FC_FCP_DATA     0x8
#define FC_COMMON_TRANSPORT_ULP 0x20

/* defines for rctl field in fc header */
#define FC_DEV_DATA     0x0
#define FC_UNSOL_CTL    0x2
#define FC_SOL_CTL      0x3
#define FC_UNSOL_DATA   0x4
#define FC_FCP_CMND     0x6
#define FC_ELS_REQ      0x22
#define FC_ELS_RSP      0x23
#define FC_NET_HDR      0x20	/* network headers for Dfctl field */

/*
 *  Start FireFly Register definitions
 */

/* PCI register offsets */
#define MEM_ADDR_OFFSET 0x10	/* SLIM base memory address */
#define MEMH_OFFSET     0x14	/* SLIM base memory high address */
#define REG_ADDR_OFFSET 0x18	/* REGISTER base memory address */
#define REGH_OFFSET     0x1c	/* REGISTER base memory high address */
#define IO_ADDR_OFFSET  0x20	/* BIU I/O registers */
#define REGIOH_OFFSET   0x24	/* REGISTER base io high address */

#define CMD_REG_OFFSET  0x4	/* PCI command configuration */

/* General PCI Register Definitions */
/* Refer To The PCI Specification For Detailed Explanations */

/* Register Offsets in little endian format */
#define PCI_VENDOR_ID_REGISTER      0x00	/* PCI Vendor ID Register */
#define PCI_DEVICE_ID_REGISTER      0x02	/* PCI Device ID Register */
#define PCI_CONFIG_ID_REGISTER      0x00	/* PCI Configuration ID Register */
#define PCI_COMMAND_REGISTER        0x04	/* PCI Command Register */
#define PCI_STATUS_REGISTER         0x06	/* PCI Status Register */
#define PCI_REV_ID_REGISTER         0x08	/* PCI Revision ID Register */
#define PCI_CLASS_CODE_REGISTER     0x09	/* PCI Class Code Register */
#define PCI_CACHE_LINE_REGISTER     0x0C	/* PCI Cache Line Register */
#define PCI_LATENCY_TMR_REGISTER    0x0D	/* PCI Latency Timer Register */
#define PCI_HEADER_TYPE_REGISTER    0x0E	/* PCI Header Type Register */
#define PCI_BIST_REGISTER           0x0F	/* PCI Built-In SelfTest Register */
#define PCI_BAR_0_REGISTER          0x10	/* PCI Base Address Register 0 */
#define PCI_BAR_1_REGISTER          0x14	/* PCI Base Address Register 1 */
#define PCI_BAR_2_REGISTER          0x18	/* PCI Base Address Register 2 */
#define PCI_BAR_3_REGISTER          0x1C	/* PCI Base Address Register 3 */
#define PCI_BAR_4_REGISTER          0x20	/* PCI Base Address Register 4 */
#define PCI_BAR_5_REGISTER          0x24	/* PCI Base Address Register 5 */
#define PCI_EXPANSION_ROM           0x30	/* PCI Expansion ROM Base Register */
#define PCI_INTR_LINE_REGISTER      0x3C	/* PCI Interrupt Line Register */
#define PCI_INTR_PIN_REGISTER       0x3D	/* PCI Interrupt Pin Register */
#define PCI_MIN_GNT_REGISTER        0x3E	/* PCI Min-Gnt Register */
#define PCI_MAX_LAT_REGISTER        0x3F	/* PCI Max_Lat Register */
#define PCI_NODE_ADDR_REGISTER      0x40	/* PCI Node Address Register */

/* number of PCI config bytes to access */
#define PCI_BYTE        1
#define PCI_WORD        2
#define PCI_DWORD       4

/* PCI related constants */
#define CMD_IO_ENBL     0x0001
#define CMD_MEM_ENBL    0x0002
#define CMD_BUS_MASTER  0x0004
#define CMD_MWI         0x0010
#define CMD_PARITY_CHK  0x0040
#define CMD_SERR_ENBL   0x0100

#define CMD_CFG_VALUE   0x156	/* mem enable, master, MWI, SERR, PERR */

#define PCI_VENDOR_ID_EMULEX        0x10df

#define PCI_DEVICE_ID_SUPERFLY      0xf700
#define PCI_DEVICE_ID_DRAGONFLY     0xf800
#define PCI_DEVICE_ID_RFLY          0xf095
#define PCI_DEVICE_ID_PFLY          0xf098
#define PCI_DEVICE_ID_TFLY          0xf0a5
#define PCI_DEVICE_ID_CENTAUR       0xf900
#define PCI_DEVICE_ID_PEGASUS       0xf980
#define PCI_DEVICE_ID_THOR          0xfa00
#define PCI_DEVICE_ID_VIPER         0xfb00
#define PCI_DEVICE_ID_LP101	    0xf0a1

#define JEDEC_ID_ADDRESS            0x0080001c
#define FIREFLY_JEDEC_ID            0x1ACC
#define SUPERFLY_JEDEC_ID           0x0020
#define DRAGONFLY_JEDEC_ID          0x0021
#define DRAGONFLY_V2_JEDEC_ID       0x0025
#define CENTAUR_2G_JEDEC_ID         0x0026
#define CENTAUR_1G_JEDEC_ID         0x0028
#define PEGASUS_ORION_JEDEC_ID      0x0036
#define PEGASUS_JEDEC_ID            0x0038
#define THOR_JEDEC_ID               0x0012
#define VIPER_JEDEC_ID              0x4838

#define JEDEC_ID_MASK               0x0FFFF000
#define JEDEC_ID_SHIFT              12
#define FC_JEDEC_ID(id)             ((id & JEDEC_ID_MASK) >> JEDEC_ID_SHIFT)

#define DEFAULT_PCI_LATENCY_CLOCKS  0xf8	/* 0xF8 is a special value for
						 * FF11.1N6 firmware.  Use
						 * 0x80 for pre-FF11.1N6 &N7, etc
						 */
#define PCI_LATENCY_VALUE           0xf8

typedef struct {		/* FireFly BIU registers */
	uint32_t hostAtt;	/* See definitions for Host Attention register */
	uint32_t chipAtt;	/* See definitions for Chip Attention register */
	uint32_t hostStatus;	/* See definitions for Host Status register */
	uint32_t hostControl;	/* See definitions for Host Control register */
	uint32_t buiConfig;	/* See definitions for BIU configuration register */
} FF_REGS, *PFF_REGS;

/* Host Attention Register */

#define HA_REG_OFFSET  0	/* Word offset from register base address */

#define HA_R0RE_REQ    0x00000001	/* Bit  0 */
#define HA_R0CE_RSP    0x00000002	/* Bit  1 */
#define HA_R0ATT       0x00000008	/* Bit  3 */
#define HA_R1RE_REQ    0x00000010	/* Bit  4 */
#define HA_R1CE_RSP    0x00000020	/* Bit  5 */
#define HA_R1ATT       0x00000080	/* Bit  7 */
#define HA_R2RE_REQ    0x00000100	/* Bit  8 */
#define HA_R2CE_RSP    0x00000200	/* Bit  9 */
#define HA_R2ATT       0x00000800	/* Bit 11 */
#define HA_R3RE_REQ    0x00001000	/* Bit 12 */
#define HA_R3CE_RSP    0x00002000	/* Bit 13 */
#define HA_R3ATT       0x00008000	/* Bit 15 */
#define HA_LATT        0x20000000	/* Bit 29 */
#define HA_MBATT       0x40000000	/* Bit 30 */
#define HA_ERATT       0x80000000	/* Bit 31 */

#define HA_RXRE_REQ    0x00000001	/* Bit  0 */
#define HA_RXCE_RSP    0x00000002	/* Bit  1 */
#define HA_RXATT       0x00000008	/* Bit  3 */
#define HA_RXMASK      0x0000000f

/* Chip Attention Register */

#define CA_REG_OFFSET  1	/* Word offset from register base address */

#define CA_R0CE_REQ    0x00000001	/* Bit  0 */
#define CA_R0RE_RSP    0x00000002	/* Bit  1 */
#define CA_R0ATT       0x00000008	/* Bit  3 */
#define CA_R1CE_REQ    0x00000010	/* Bit  4 */
#define CA_R1RE_RSP    0x00000020	/* Bit  5 */
#define CA_R1ATT       0x00000080	/* Bit  7 */
#define CA_R2CE_REQ    0x00000100	/* Bit  8 */
#define CA_R2RE_RSP    0x00000200	/* Bit  9 */
#define CA_R2ATT       0x00000800	/* Bit 11 */
#define CA_R3CE_REQ    0x00001000	/* Bit 12 */
#define CA_R3RE_RSP    0x00002000	/* Bit 13 */
#define CA_R3ATT       0x00008000	/* Bit 15 */
#define CA_MBATT       0x40000000	/* Bit 30 */

/* Host Status Register */

#define HS_REG_OFFSET  2	/* Word offset from register base address */

#define HS_MBRDY       0x00400000	/* Bit 22 */
#define HS_FFRDY       0x00800000	/* Bit 23 */
#define HS_FFER8       0x01000000	/* Bit 24 */
#define HS_FFER7       0x02000000	/* Bit 25 */
#define HS_FFER6       0x04000000	/* Bit 26 */
#define HS_FFER5       0x08000000	/* Bit 27 */
#define HS_FFER4       0x10000000	/* Bit 28 */
#define HS_FFER3       0x20000000	/* Bit 29 */
#define HS_FFER2       0x40000000	/* Bit 30 */
#define HS_FFER1       0x80000000	/* Bit 31 */
#define HS_FFERM       0xFF000000	/* Mask for error bits 31:24 */

/* Host Control Register */

#define HC_REG_OFFSET  3	/* Word offset from register base address */

#define HC_MBINT_ENA   0x00000001	/* Bit  0 */
#define HC_R0INT_ENA   0x00000002	/* Bit  1 */
#define HC_R1INT_ENA   0x00000004	/* Bit  2 */
#define HC_R2INT_ENA   0x00000008	/* Bit  3 */
#define HC_R3INT_ENA   0x00000010	/* Bit  4 */
#define HC_INITHBI     0x02000000	/* Bit 25 */
#define HC_INITMB      0x04000000	/* Bit 26 */
#define HC_INITFF      0x08000000	/* Bit 27 */
#define HC_LAINT_ENA   0x20000000	/* Bit 29 */
#define HC_ERINT_ENA   0x80000000	/* Bit 31 */

/* Mailbox Commands */
#define MBX_SHUTDOWN        0x00	/* terminate testing */
#define MBX_LOAD_SM         0x01
#define MBX_READ_NV         0x02
#define MBX_WRITE_NV        0x03
#define MBX_RUN_BIU_DIAG    0x04
#define MBX_INIT_LINK       0x05
#define MBX_DOWN_LINK       0x06
#define MBX_CONFIG_LINK     0x07
#define MBX_CONFIG_RING     0x09
#define MBX_RESET_RING      0x0A
#define MBX_READ_CONFIG     0x0B
#define MBX_READ_RCONFIG    0x0C
#define MBX_READ_SPARM      0x0D
#define MBX_READ_STATUS     0x0E
#define MBX_READ_RPI        0x0F
#define MBX_READ_XRI        0x10
#define MBX_READ_REV        0x11
#define MBX_READ_LNK_STAT   0x12
#define MBX_REG_LOGIN       0x13
#define MBX_UNREG_LOGIN     0x14
#define MBX_READ_LA         0x15
#define MBX_CLEAR_LA        0x16
#define MBX_DUMP_MEMORY     0x17
#define MBX_DUMP_CONTEXT    0x18
#define MBX_RUN_DIAGS       0x19
#define MBX_RESTART         0x1A
#define MBX_UPDATE_CFG      0x1B
#define MBX_DOWN_LOAD       0x1C
#define MBX_DEL_LD_ENTRY    0x1D
#define MBX_RUN_PROGRAM     0x1E
#define MBX_SET_MASK        0x20
#define MBX_SET_SLIM        0x21
#define MBX_UNREG_D_ID      0x23
#define MBX_CONFIG_FARP     0x25

#define MBX_LOAD_AREA       0x81
#define MBX_RUN_BIU_DIAG64  0x84
#define MBX_CONFIG_PORT     0x88
#define MBX_READ_SPARM64    0x8D
#define MBX_READ_RPI64      0x8F
#define MBX_REG_LOGIN64     0x93
#define MBX_READ_LA64       0x95

#define MBX_FLASH_WR_ULA    0x98
#define MBX_SET_DEBUG       0x99
#define MBX_LOAD_EXP_ROM    0x9C

#define MBX_MAX_CMDS        0x9D
#define MBX_SLI2_CMD_MASK   0x80

/* IOCB Commands */

#define CMD_RCV_SEQUENCE_CX     0x01
#define CMD_XMIT_SEQUENCE_CR    0x02
#define CMD_XMIT_SEQUENCE_CX    0x03
#define CMD_XMIT_BCAST_CN       0x04
#define CMD_XMIT_BCAST_CX       0x05
#define CMD_QUE_RING_BUF_CN     0x06
#define CMD_QUE_XRI_BUF_CX      0x07
#define CMD_IOCB_CONTINUE_CN    0x08
#define CMD_RET_XRI_BUF_CX      0x09
#define CMD_ELS_REQUEST_CR      0x0A
#define CMD_ELS_REQUEST_CX      0x0B
#define CMD_RCV_ELS_REQ_CX      0x0D
#define CMD_ABORT_XRI_CN        0x0E
#define CMD_ABORT_XRI_CX        0x0F
#define CMD_CLOSE_XRI_CN        0x10
#define CMD_CLOSE_XRI_CX        0x11
#define CMD_CREATE_XRI_CR       0x12
#define CMD_CREATE_XRI_CX       0x13
#define CMD_GET_RPI_CN          0x14
#define CMD_XMIT_ELS_RSP_CX     0x15
#define CMD_GET_RPI_CR          0x16
#define CMD_XRI_ABORTED_CX      0x17
#define CMD_FCP_IWRITE_CR       0x18
#define CMD_FCP_IWRITE_CX       0x19
#define CMD_FCP_IREAD_CR        0x1A
#define CMD_FCP_IREAD_CX        0x1B
#define CMD_FCP_ICMND_CR        0x1C
#define CMD_FCP_ICMND_CX        0x1D

#define CMD_ADAPTER_MSG         0x20
#define CMD_ADAPTER_DUMP        0x22

/*  SLI_2 IOCB Command Set */

#define CMD_RCV_SEQUENCE64_CX   0x81
#define CMD_XMIT_SEQUENCE64_CR  0x82
#define CMD_XMIT_SEQUENCE64_CX  0x83
#define CMD_XMIT_BCAST64_CN     0x84
#define CMD_XMIT_BCAST64_CX     0x85
#define CMD_QUE_RING_BUF64_CN   0x86
#define CMD_QUE_XRI_BUF64_CX    0x87
#define CMD_IOCB_CONTINUE64_CN  0x88
#define CMD_RET_XRI_BUF64_CX    0x89
#define CMD_ELS_REQUEST64_CR    0x8A
#define CMD_ELS_REQUEST64_CX    0x8B
#define CMD_ABORT_MXRI64_CN     0x8C
#define CMD_RCV_ELS_REQ64_CX    0x8D
#define CMD_XMIT_ELS_RSP64_CX   0x95
#define CMD_FCP_IWRITE64_CR     0x98
#define CMD_FCP_IWRITE64_CX     0x99
#define CMD_FCP_IREAD64_CR      0x9A
#define CMD_FCP_IREAD64_CX      0x9B
#define CMD_FCP_ICMND64_CR      0x9C
#define CMD_FCP_ICMND64_CX      0x9D

#define CMD_GEN_REQUEST64_CR    0xC2
#define CMD_GEN_REQUEST64_CX    0xC3

#define CMD_MAX_IOCB_CMD        0xE6
#define CMD_IOCB_MASK           0xff

#define MAX_MSG_DATA            28	/* max msg data in CMD_ADAPTER_MSG iocb */
#define ELX_MAX_ADPTMSG         32	/* max msg data */
/*
 *  Define Status
 */
#define MBX_SUCCESS                 0
#define MBXERR_NUM_RINGS            1
#define MBXERR_NUM_IOCBS            2
#define MBXERR_IOCBS_EXCEEDED       3
#define MBXERR_BAD_RING_NUMBER      4
#define MBXERR_MASK_ENTRIES_RANGE   5
#define MBXERR_MASKS_EXCEEDED       6
#define MBXERR_BAD_PROFILE          7
#define MBXERR_BAD_DEF_CLASS        8
#define MBXERR_BAD_MAX_RESPONDER    9
#define MBXERR_BAD_MAX_ORIGINATOR   10
#define MBXERR_RPI_REGISTERED       11
#define MBXERR_RPI_FULL             12
#define MBXERR_NO_RESOURCES         13
#define MBXERR_BAD_RCV_LENGTH       14
#define MBXERR_DMA_ERROR            15
#define MBXERR_ERROR                16
#define MBX_NOT_FINISHED           255

#define MBX_BUSY                   0xffffff	/* Attempted cmd to busy Mailbox */
#define MBX_TIMEOUT                0xfffffe	/* time-out expired waiting for */

/*
 *    Begin Structure Definitions for Mailbox Commands
 */

typedef struct {
#if BIG_ENDIAN_HW
	uint8_t tval;
	uint8_t tmask;
	uint8_t rval;
	uint8_t rmask;
#endif
#if LITTLE_ENDIAN_HW
	uint8_t rmask;
	uint8_t rval;
	uint8_t tmask;
	uint8_t tval;
#endif
} RR_REG;

typedef struct {
	uint32_t bdeAddress;
#if BIG_ENDIAN_HW
	uint32_t bdeReserved:4;
	uint32_t bdeAddrHigh:4;
	uint32_t bdeSize:24;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t bdeSize:24;
	uint32_t bdeAddrHigh:4;
	uint32_t bdeReserved:4;
#endif
} ULP_BDE;

typedef struct ULP_BDE_64 {	/* SLI-2 */
	union ULP_BDE_TUS {
		uint32_t w;
		struct {
#if BIG_ENDIAN_HW
			uint32_t bdeFlags:8;	/* BDE Flags 0 IS A SUPPORTED VALUE !! */
			uint32_t bdeSize:24;	/* Size of buffer (in bytes) */
#endif
#if LITTLE_ENDIAN_HW
			uint32_t bdeSize:24;	/* Size of buffer (in bytes) */
			uint32_t bdeFlags:8;	/* BDE Flags 0 IS A SUPPORTED VALUE !! */
#endif
#define BUFF_USE_RSVD       0x01	/* bdeFlags */
#define BUFF_USE_INTRPT     0x02	/* Not Implemented with LP6000 */
#define BUFF_USE_CMND       0x04	/* Optional, 1=cmd/rsp 0=data buffer */
#define BUFF_USE_RCV        0x08	/*  ""  "",  1=rcv buffer, 0=xmit buffer */
#define BUFF_TYPE_32BIT     0x10	/*  ""  "",  1=32 bit addr 0=64 bit addr */
#define BUFF_TYPE_SPECIAL   0x20	/* Not Implemented with LP6000  */
#define BUFF_TYPE_BDL       0x40	/* Optional,  may be set in BDL */
#define BUFF_TYPE_INVALID   0x80	/*  ""  "" */
		} f;
	} tus;
	uint32_t addrLow;
	uint32_t addrHigh;
} ULP_BDE64;
#define BDE64_SIZE_WORD 0
#define BPL64_SIZE_WORD 0x40

typedef struct ULP_BDL {	/* SLI-2 */
#if BIG_ENDIAN_HW
	uint32_t bdeFlags:8;	/* BDL Flags */
	uint32_t bdeSize:24;	/* Size of BDL array in host memory (bytes) */
#endif
#if LITTLE_ENDIAN_HW
	uint32_t bdeSize:24;	/* Size of BDL array in host memory (bytes) */
	uint32_t bdeFlags:8;	/* BDL Flags */
#endif
	uint32_t addrLow;	/* Address 0:31 */
	uint32_t addrHigh;	/* Address 32:63 */
	uint32_t ulpIoTag32;	/* Can be used for 32 bit I/O Tag */
} ULP_BDL;

/* Structure for MB Command LOAD_SM and DOWN_LOAD */

typedef struct {
#if BIG_ENDIAN_HW
	uint32_t rsvd2:25;
	uint32_t acknowledgment:1;
	uint32_t version:1;
	uint32_t erase_or_prog:1;
	uint32_t update_flash:1;
	uint32_t update_ram:1;
	uint32_t method:1;
	uint32_t load_cmplt:1;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t load_cmplt:1;
	uint32_t method:1;
	uint32_t update_ram:1;
	uint32_t update_flash:1;
	uint32_t erase_or_prog:1;
	uint32_t version:1;
	uint32_t acknowledgment:1;
	uint32_t rsvd2:25;
#endif

#define DL_FROM_BDE     0	/* method */
#define DL_FROM_SLIM    1

#define PROGRAM_FLASH   0	/* erase_or_prog */
#define ERASE_FLASH     1

	uint32_t dl_to_adr_low;
	uint32_t dl_to_adr_high;
	uint32_t dl_len;
	union {
		uint32_t dl_from_mbx_offset;
		ULP_BDE dl_from_bde;
		ULP_BDE64 dl_from_bde64;
	} un;

} LOAD_SM_VAR;

/* Structure for MB Command READ_NVPARM (02) */

typedef struct {
	uint32_t rsvd1[3];	/* Read as all one's */
	uint32_t rsvd2;		/* Read as all zero's */
	uint32_t portname[2];	/* N_PORT name */
	uint32_t nodename[2];	/* NODE name */
#if BIG_ENDIAN_HW
	uint32_t pref_DID:24;
	uint32_t hardAL_PA:8;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t hardAL_PA:8;
	uint32_t pref_DID:24;
#endif
	uint32_t rsvd3[21];	/* Read as all one's */
} READ_NV_VAR;

/* Structure for MB Command WRITE_NVPARMS (03) */

typedef struct {
	uint32_t rsvd1[3];	/* Must be all one's */
	uint32_t rsvd2;		/* Must be all zero's */
	uint32_t portname[2];	/* N_PORT name */
	uint32_t nodename[2];	/* NODE name */
#if BIG_ENDIAN_HW
	uint32_t pref_DID:24;
	uint32_t hardAL_PA:8;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t hardAL_PA:8;
	uint32_t pref_DID:24;
#endif
	uint32_t rsvd3[21];	/* Must be all one's */
} WRITE_NV_VAR;

/* Structure for MB Command RUN_BIU_DIAG (04) */
/* Structure for MB Command RUN_BIU_DIAG64 (0x84) */

typedef struct {
	uint32_t rsvd1;
	union {
		struct {
			ULP_BDE xmit_bde;
			ULP_BDE rcv_bde;
		} s1;
		struct {
			ULP_BDE64 xmit_bde64;
			ULP_BDE64 rcv_bde64;
		} s2;
	} un;
} BIU_DIAG_VAR;

/* Structure for MB Command INIT_LINK (05) */

typedef struct {
#if BIG_ENDIAN_HW
	uint32_t rsvd1:24;
	uint32_t lipsr_AL_PA:8;	/* AL_PA to issue Lip Selective Reset to */
#endif
#if LITTLE_ENDIAN_HW
	uint32_t lipsr_AL_PA:8;	/* AL_PA to issue Lip Selective Reset to */
	uint32_t rsvd1:24;
#endif

#if BIG_ENDIAN_HW
	uint8_t fabric_AL_PA;	/* If using a Fabric Assigned AL_PA */
	uint8_t rsvd2;
	uint16_t link_flags;
#endif
#if LITTLE_ENDIAN_HW
	uint16_t link_flags;
	uint8_t rsvd2;
	uint8_t fabric_AL_PA;	/* If using a Fabric Assigned AL_PA */
#endif
#define FLAGS_LOCAL_LB               0x01	/* link_flags (=1) ENDEC loopback */
#define FLAGS_TOPOLOGY_MODE_LOOP_PT  0x00	/* Attempt loop then pt-pt */
#define FLAGS_TOPOLOGY_MODE_PT_PT    0x02	/* Attempt pt-pt only */
#define FLAGS_TOPOLOGY_MODE_LOOP     0x04	/* Attempt loop only */
#define FLAGS_TOPOLOGY_MODE_PT_LOOP  0x06	/* Attempt pt-pt then loop */
#define FLAGS_LIRP_LILP              0x80	/* LIRP / LILP is disabled */

#define FLAGS_TOPOLOGY_FAILOVER      0x0400	/* Bit 10 */
#define FLAGS_LINK_SPEED             0x0800	/* Bit 11 */

	uint32_t link_speed;	/* NEW_FEATURE */
#define LINK_SPEED_AUTO 0	/* Auto selection */
#define LINK_SPEED_1G   1	/* 1 Gigabaud */
#define LINK_SPEED_2G   2	/* 2 Gigabaud */

} INIT_LINK_VAR;

/* Structure for MB Command DOWN_LINK (06) */

typedef struct {
	uint32_t rsvd1;
} DOWN_LINK_VAR;

/* Structure for MB Command CONFIG_LINK (07) */

typedef struct {
#if BIG_ENDIAN_HW
	uint32_t cr:1;
	uint32_t ci:1;
	uint32_t cr_delay:6;
	uint32_t cr_count:8;
	uint32_t rsvd1:8;
	uint32_t MaxBBC:8;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t MaxBBC:8;
	uint32_t rsvd1:8;
	uint32_t cr_count:8;
	uint32_t cr_delay:6;
	uint32_t ci:1;
	uint32_t cr:1;
#endif
	uint32_t myId;
	uint32_t rsvd2;
	uint32_t edtov;
	uint32_t arbtov;
	uint32_t ratov;
	uint32_t rttov;
	uint32_t altov;
	uint32_t crtov;
	uint32_t citov;
#if BIG_ENDIAN_HW
	uint32_t rrq_enable:1;
	uint32_t rrq_immed:1;
	uint32_t rsvd4:29;
	uint32_t ack0_enable:1;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t ack0_enable:1;
	uint32_t rsvd4:29;
	uint32_t rrq_immed:1;
	uint32_t rrq_enable:1;
#endif
} CONFIG_LINK;

/* Structure for MB Command PART_SLIM (08)
 * will be removed since SLI1 is no longer supported!
 */
typedef struct {
#if BIG_ENDIAN_HW
	uint16_t offCiocb;
	uint16_t numCiocb;
	uint16_t offRiocb;
	uint16_t numRiocb;
#endif
#if LITTLE_ENDIAN_HW
	uint16_t numCiocb;
	uint16_t offCiocb;
	uint16_t numRiocb;
	uint16_t offRiocb;
#endif
} RING_DEF;

typedef struct {
#if BIG_ENDIAN_HW
	uint32_t unused1:24;
	uint32_t numRing:8;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t numRing:8;
	uint32_t unused1:24;
#endif
	RING_DEF ringdef[4];
	uint32_t hbainit;
} PART_SLIM_VAR;

/* Structure for MB Command CONFIG_RING (09) */

typedef struct {
#if BIG_ENDIAN_HW
	uint32_t unused2:6;
	uint32_t recvSeq:1;
	uint32_t recvNotify:1;
	uint32_t numMask:8;
	uint32_t profile:8;
	uint32_t unused1:4;
	uint32_t ring:4;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t ring:4;
	uint32_t unused1:4;
	uint32_t profile:8;
	uint32_t numMask:8;
	uint32_t recvNotify:1;
	uint32_t recvSeq:1;
	uint32_t unused2:6;
#endif
#if BIG_ENDIAN_HW
	uint16_t maxRespXchg;
	uint16_t maxOrigXchg;
#endif
#if LITTLE_ENDIAN_HW
	uint16_t maxOrigXchg;
	uint16_t maxRespXchg;
#endif
	RR_REG rrRegs[6];
} CONFIG_RING_VAR;

/* Structure for MB Command RESET_RING (10) */

typedef struct {
	uint32_t ring_no;
} RESET_RING_VAR;

/* Structure for MB Command READ_CONFIG (11) */

typedef struct {
#if BIG_ENDIAN_HW
	uint32_t cr:1;
	uint32_t ci:1;
	uint32_t cr_delay:6;
	uint32_t cr_count:8;
	uint32_t InitBBC:8;
	uint32_t MaxBBC:8;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t MaxBBC:8;
	uint32_t InitBBC:8;
	uint32_t cr_count:8;
	uint32_t cr_delay:6;
	uint32_t ci:1;
	uint32_t cr:1;
#endif
#if BIG_ENDIAN_HW
	uint32_t topology:8;
	uint32_t myDid:24;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t myDid:24;
	uint32_t topology:8;
#endif
	/* Defines for topology (defined previously) */
#if BIG_ENDIAN_HW
	uint32_t AR:1;
	uint32_t IR:1;
	uint32_t rsvd1:29;
	uint32_t ack0:1;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t ack0:1;
	uint32_t rsvd1:29;
	uint32_t IR:1;
	uint32_t AR:1;
#endif
	uint32_t edtov;
	uint32_t arbtov;
	uint32_t ratov;
	uint32_t rttov;
	uint32_t altov;
	uint32_t lmt;
#define LMT_RESERVED    0x0	/* Not used */
#define LMT_266_10bit   0x1	/*  265.625 Mbaud 10 bit iface */
#define LMT_532_10bit   0x2	/*  531.25  Mbaud 10 bit iface */
#define LMT_1063_10bit  0x3	/* 1062.5   Mbaud 20 bit iface */
#define LMT_2125_10bit  0x8	/* 2125     Mbaud 10 bit iface */

	uint32_t rsvd2;
	uint32_t rsvd3;
	uint32_t max_xri;
	uint32_t max_iocb;
	uint32_t max_rpi;
	uint32_t avail_xri;
	uint32_t avail_iocb;
	uint32_t avail_rpi;
	uint32_t default_rpi;
} READ_CONFIG_VAR;

/* Structure for MB Command READ_RCONFIG (12) */

typedef struct {
#if BIG_ENDIAN_HW
	uint32_t rsvd2:7;
	uint32_t recvNotify:1;
	uint32_t numMask:8;
	uint32_t profile:8;
	uint32_t rsvd1:4;
	uint32_t ring:4;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t ring:4;
	uint32_t rsvd1:4;
	uint32_t profile:8;
	uint32_t numMask:8;
	uint32_t recvNotify:1;
	uint32_t rsvd2:7;
#endif
#if BIG_ENDIAN_HW
	uint16_t maxResp;
	uint16_t maxOrig;
#endif
#if LITTLE_ENDIAN_HW
	uint16_t maxOrig;
	uint16_t maxResp;
#endif
	RR_REG rrRegs[6];
#if BIG_ENDIAN_HW
	uint16_t cmdRingOffset;
	uint16_t cmdEntryCnt;
	uint16_t rspRingOffset;
	uint16_t rspEntryCnt;
	uint16_t nextCmdOffset;
	uint16_t rsvd3;
	uint16_t nextRspOffset;
	uint16_t rsvd4;
#endif
#if LITTLE_ENDIAN_HW
	uint16_t cmdEntryCnt;
	uint16_t cmdRingOffset;
	uint16_t rspEntryCnt;
	uint16_t rspRingOffset;
	uint16_t rsvd3;
	uint16_t nextCmdOffset;
	uint16_t rsvd4;
	uint16_t nextRspOffset;
#endif
} READ_RCONF_VAR;

/* Structure for MB Command READ_SPARM (13) */
/* Structure for MB Command READ_SPARM64 (0x8D) */

typedef struct {
	uint32_t rsvd1;
	uint32_t rsvd2;
	union {
		ULP_BDE sp;	/* This BDE points to SERV_PARM structure */
		ULP_BDE64 sp64;
	} un;
} READ_SPARM_VAR;

/* Structure for MB Command READ_STATUS (14) */

typedef struct {
#if BIG_ENDIAN_HW
	uint32_t rsvd1:31;
	uint32_t clrCounters:1;
	uint16_t activeXriCnt;
	uint16_t activeRpiCnt;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t clrCounters:1;
	uint32_t rsvd1:31;
	uint16_t activeRpiCnt;
	uint16_t activeXriCnt;
#endif
	uint32_t xmitByteCnt;
	uint32_t rcvByteCnt;
	uint32_t xmitFrameCnt;
	uint32_t rcvFrameCnt;
	uint32_t xmitSeqCnt;
	uint32_t rcvSeqCnt;
	uint32_t totalOrigExchanges;
	uint32_t totalRespExchanges;
	uint32_t rcvPbsyCnt;
	uint32_t rcvFbsyCnt;
} READ_STATUS_VAR;

/* Structure for MB Command READ_RPI (15) */
/* Structure for MB Command READ_RPI64 (0x8F) */

typedef struct {
#if BIG_ENDIAN_HW
	uint16_t nextRpi;
	uint16_t reqRpi;
	uint32_t rsvd2:8;
	uint32_t DID:24;
#endif
#if LITTLE_ENDIAN_HW
	uint16_t reqRpi;
	uint16_t nextRpi;
	uint32_t DID:24;
	uint32_t rsvd2:8;
#endif
	union {
		ULP_BDE sp;
		ULP_BDE64 sp64;
	} un;

} READ_RPI_VAR;

/* Structure for MB Command READ_XRI (16) */

typedef struct {
#if BIG_ENDIAN_HW
	uint16_t nextXri;
	uint16_t reqXri;
	uint16_t rsvd1;
	uint16_t rpi;
	uint32_t rsvd2:8;
	uint32_t DID:24;
	uint32_t rsvd3:8;
	uint32_t SID:24;
	uint32_t rsvd4;
	uint8_t seqId;
	uint8_t rsvd5;
	uint16_t seqCount;
	uint16_t oxId;
	uint16_t rxId;
	uint32_t rsvd6:30;
	uint32_t si:1;
	uint32_t exchOrig:1;
#endif
#if LITTLE_ENDIAN_HW
	uint16_t reqXri;
	uint16_t nextXri;
	uint16_t rpi;
	uint16_t rsvd1;
	uint32_t DID:24;
	uint32_t rsvd2:8;
	uint32_t SID:24;
	uint32_t rsvd3:8;
	uint32_t rsvd4;
	uint16_t seqCount;
	uint8_t rsvd5;
	uint8_t seqId;
	uint16_t rxId;
	uint16_t oxId;
	uint32_t exchOrig:1;
	uint32_t si:1;
	uint32_t rsvd6:30;
#endif
} READ_XRI_VAR;

/* Structure for MB Command READ_REV (17) */

typedef struct {
#if BIG_ENDIAN_HW
	uint32_t cv:1;
	uint32_t rr:1;
	uint32_t rsvd1:29;
	uint32_t rv:1;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t rv:1;
	uint32_t rsvd1:29;
	uint32_t rr:1;
	uint32_t cv:1;
#endif
	uint32_t biuRev;
	uint32_t smRev;
	union {
		uint32_t smFwRev;
		struct {
#if BIG_ENDIAN_HW
			uint8_t ProgType;
			uint8_t ProgId;
			uint16_t ProgVer:4;
			uint16_t ProgRev:4;
			uint16_t ProgFixLvl:2;
			uint16_t ProgDistType:2;
			uint16_t DistCnt:4;
#endif
#if LITTLE_ENDIAN_HW
			uint16_t DistCnt:4;
			uint16_t ProgDistType:2;
			uint16_t ProgFixLvl:2;
			uint16_t ProgRev:4;
			uint16_t ProgVer:4;
			uint8_t ProgId;
			uint8_t ProgType;
#endif
		} b;
	} un;
	uint32_t endecRev;
#if BIG_ENDIAN_HW
	uint8_t feaLevelHigh;
	uint8_t feaLevelLow;
	uint8_t fcphHigh;
	uint8_t fcphLow;
#endif
#if LITTLE_ENDIAN_HW
	uint8_t fcphLow;
	uint8_t fcphHigh;
	uint8_t feaLevelLow;
	uint8_t feaLevelHigh;
#endif
	uint32_t postKernRev;
	uint32_t opFwRev;
	uint8_t opFwName[16];
	uint32_t sli1FwRev;
	uint8_t sli1FwName[16];
	uint32_t sli2FwRev;
	uint8_t sli2FwName[16];
	uint32_t rsvd2;
	uint32_t RandomData[7];
} READ_REV_VAR;

#define rxSeqRev postKernRev
#define txSeqRev opFwRev

/* Structure for MB Command READ_LINK_STAT (18) */

typedef struct {
	uint32_t rsvd1;
	uint32_t linkFailureCnt;
	uint32_t lossSyncCnt;

	uint32_t lossSignalCnt;
	uint32_t primSeqErrCnt;
	uint32_t invalidXmitWord;
	uint32_t crcCnt;
	uint32_t primSeqTimeout;
	uint32_t elasticOverrun;
	uint32_t arbTimeout;
} READ_LNK_VAR;

/* Structure for MB Command REG_LOGIN (19) */
/* Structure for MB Command REG_LOGIN64 (0x93) */

typedef struct {
#if BIG_ENDIAN_HW
	uint16_t rsvd1;
	uint16_t rpi;
	uint32_t rsvd2:8;
	uint32_t did:24;
#endif
#if LITTLE_ENDIAN_HW
	uint16_t rpi;
	uint16_t rsvd1;
	uint32_t did:24;
	uint32_t rsvd2:8;
#endif
	union {
		ULP_BDE sp;
		ULP_BDE64 sp64;
	} un;

} REG_LOGIN_VAR;

/* Word 30 contents for REG_LOGIN */
typedef union {
	struct {
#if BIG_ENDIAN_HW
		uint16_t rsvd1:12;
		uint16_t wd30_class:4;
		uint16_t xri;
#endif
#if LITTLE_ENDIAN_HW
		uint16_t xri;
		uint16_t wd30_class:4;
		uint16_t rsvd1:12;
#endif
	} f;
	uint32_t word;
} REG_WD30;

/* Structure for MB Command UNREG_LOGIN (20) */

typedef struct {
#if BIG_ENDIAN_HW
	uint16_t rsvd1;
	uint16_t rpi;
#endif
#if LITTLE_ENDIAN_HW
	uint16_t rpi;
	uint16_t rsvd1;
#endif
} UNREG_LOGIN_VAR;

/* Structure for MB Command UNREG_D_ID (0x23) */

typedef struct {
	uint32_t did;
} UNREG_D_ID_VAR;

/* Structure for MB Command READ_LA (21) */
/* Structure for MB Command READ_LA64 (0x95) */

typedef struct {
	uint32_t eventTag;	/* Event tag */
#if BIG_ENDIAN_HW
	uint32_t rsvd1:22;
	uint32_t pb:1;
	uint32_t il:1;
	uint32_t attType:8;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t attType:8;
	uint32_t il:1;
	uint32_t pb:1;
	uint32_t rsvd1:22;
#endif
#define AT_RESERVED    0x00	/* Reserved - attType */
#define AT_LINK_UP     0x01	/* Link is up */
#define AT_LINK_DOWN   0x02	/* Link is down */
#if BIG_ENDIAN_HW
	uint8_t granted_AL_PA;
	uint8_t lipAlPs;
	uint8_t lipType;
	uint8_t topology;
#endif
#if LITTLE_ENDIAN_HW
	uint8_t topology;
	uint8_t lipType;
	uint8_t lipAlPs;
	uint8_t granted_AL_PA;
#endif
#define LT_PORT_INIT    0x00	/* An L_PORT initing (F7, AL_PS) - lipType */
#define LT_PORT_ERR     0x01	/* Err @L_PORT rcv'er (F8, AL_PS) */
#define LT_RESET_APORT  0x02	/* Lip Reset of some other port */
#define LT_RESET_MYPORT 0x03	/* Lip Reset of my port */
#define TOPOLOGY_PT_PT 0x01	/* Topology is pt-pt / pt-fabric */
#define TOPOLOGY_LOOP  0x02	/* Topology is FC-AL */

	union {
		ULP_BDE lilpBde;	/* This BDE points to a 128 byte buffer to */
		/* store the LILP AL_PA position map into */
		ULP_BDE64 lilpBde64;
	} un;
#if BIG_ENDIAN_HW
	uint32_t Dlu:1;
	uint32_t Dtf:1;
	uint32_t Drsvd2:14;
	uint32_t DlnkSpeed:8;
	uint32_t DnlPort:4;
	uint32_t Dtx:2;
	uint32_t Drx:2;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t Drx:2;
	uint32_t Dtx:2;
	uint32_t DnlPort:4;
	uint32_t DlnkSpeed:8;
	uint32_t Drsvd2:14;
	uint32_t Dtf:1;
	uint32_t Dlu:1;
#endif
#if BIG_ENDIAN_HW
	uint32_t Ulu:1;
	uint32_t Utf:1;
	uint32_t Ursvd2:14;
	uint32_t UlnkSpeed:8;
	uint32_t UnlPort:4;
	uint32_t Utx:2;
	uint32_t Urx:2;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t Urx:2;
	uint32_t Utx:2;
	uint32_t UnlPort:4;
	uint32_t UlnkSpeed:8;
	uint32_t Ursvd2:14;
	uint32_t Utf:1;
	uint32_t Ulu:1;
#endif
#define LA_1GHZ_LINK   4	/* lnkSpeed */
#define LA_2GHZ_LINK   8	/* lnkSpeed */

} READ_LA_VAR;

/* Structure for MB Command CLEAR_LA (22) */

typedef struct {
	uint32_t eventTag;	/* Event tag */
	uint32_t rsvd1;
} CLEAR_LA_VAR;

/* Structure for MB Command DUMP */

typedef struct {
#if BIG_ENDIAN_HW
	uint32_t rsvd:25;
	uint32_t ra:1;
	uint32_t co:1;
	uint32_t cv:1;
	uint32_t type:4;
	uint32_t entry_index:16;
	uint32_t region_id:16;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t type:4;
	uint32_t cv:1;
	uint32_t co:1;
	uint32_t ra:1;
	uint32_t rsvd:25;
	uint32_t region_id:16;
	uint32_t entry_index:16;
#endif
	uint32_t rsvd1;
	uint32_t word_cnt;
	uint32_t resp_offset;
} DUMP_VAR;

#define  DMP_MEM_REG             0x1
#define  DMP_NV_PARAMS           0x2

#define  DMP_REGION_VPD          0xe
#define  DMP_VPD_SIZE            0x100

/* Structure for MB Command CONFIG_PORT (0x88) */

typedef struct {
	uint32_t pcbLen;
	uint32_t pcbLow;	/* bit 31:0  of memory based port config block */
	uint32_t pcbHigh;	/* bit 63:32 of memory based port config block */
	uint32_t hbainit[5];
} CONFIG_PORT_VAR;

/* SLI-2 Port Control Block */

/* SLIM POINTER */
#define SLIMOFF 0x30		/* WORD */

typedef struct _SLI2_RDSC {
	uint32_t cmdEntries;
	uint32_t cmdAddrLow;
	uint32_t cmdAddrHigh;

	uint32_t rspEntries;
	uint32_t rspAddrLow;
	uint32_t rspAddrHigh;
} SLI2_RDSC;

typedef struct _PCB {
#if BIG_ENDIAN_HW
	uint32_t type:8;
#define TYPE_NATIVE_SLI2       0x01;
	uint32_t feature:8;
#define FEATURE_INITIAL_SLI2   0x01;
	uint32_t rsvd:12;
	uint32_t maxRing:4;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t maxRing:4;
	uint32_t rsvd:12;
	uint32_t feature:8;
#define FEATURE_INITIAL_SLI2   0x01;
	uint32_t type:8;
#define TYPE_NATIVE_SLI2       0x01;
#endif

	uint32_t mailBoxSize;
	uint32_t mbAddrLow;
	uint32_t mbAddrHigh;

	uint32_t hgpAddrLow;
	uint32_t hgpAddrHigh;

	uint32_t pgpAddrLow;
	uint32_t pgpAddrHigh;
	SLI2_RDSC rdsc[MAX_RINGS];
} PCB_t;

/* NEW_FEATURE */
typedef struct {
#if BIG_ENDIAN_HW
	uint32_t rsvd0:27;
	uint32_t discardFarp:1;
	uint32_t IPEnable:1;
	uint32_t nodeName:1;
	uint32_t portName:1;
	uint32_t filterEnable:1;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t filterEnable:1;
	uint32_t portName:1;
	uint32_t nodeName:1;
	uint32_t IPEnable:1;
	uint32_t discardFarp:1;
	uint32_t rsvd:27;
#endif
	uint8_t portname[8];	/* Used to be NAME_TYPE */
	uint8_t nodename[8];
	uint32_t rsvd1;
	uint32_t rsvd2;
	uint32_t rsvd3;
	uint32_t IPAddress;
} CONFIG_FARP_VAR;

/* Union of all Mailbox Command types */
#define MAILBOX_CMD_WSIZE 32

typedef union {
	uint32_t varWords[MAILBOX_CMD_WSIZE - 1];
	LOAD_SM_VAR varLdSM;	/* cmd =  1 (LOAD_SM)        */
	READ_NV_VAR varRDnvp;	/* cmd =  2 (READ_NVPARMS)   */
	WRITE_NV_VAR varWTnvp;	/* cmd =  3 (WRITE_NVPARMS)  */
	BIU_DIAG_VAR varBIUdiag;	/* cmd =  4 (RUN_BIU_DIAG)   */
	INIT_LINK_VAR varInitLnk;	/* cmd =  5 (INIT_LINK)      */
	DOWN_LINK_VAR varDwnLnk;	/* cmd =  6 (DOWN_LINK)      */
	CONFIG_LINK varCfgLnk;	/* cmd =  7 (CONFIG_LINK)    */
	PART_SLIM_VAR varSlim;	/* cmd =  8 (PART_SLIM)      */
	CONFIG_RING_VAR varCfgRing;	/* cmd =  9 (CONFIG_RING)    */
	RESET_RING_VAR varRstRing;	/* cmd = 10 (RESET_RING)     */
	READ_CONFIG_VAR varRdConfig;	/* cmd = 11 (READ_CONFIG)    */
	READ_RCONF_VAR varRdRConfig;	/* cmd = 12 (READ_RCONFIG)   */
	READ_SPARM_VAR varRdSparm;	/* cmd = 13 (READ_SPARM(64)) */
	READ_STATUS_VAR varRdStatus;	/* cmd = 14 (READ_STATUS)    */
	READ_RPI_VAR varRdRPI;	/* cmd = 15 (READ_RPI(64))   */
	READ_XRI_VAR varRdXRI;	/* cmd = 16 (READ_XRI)       */
	READ_REV_VAR varRdRev;	/* cmd = 17 (READ_REV)       */
	READ_LNK_VAR varRdLnk;	/* cmd = 18 (READ_LNK_STAT)  */
	REG_LOGIN_VAR varRegLogin;	/* cmd = 19 (REG_LOGIN(64))  */
	UNREG_LOGIN_VAR varUnregLogin;	/* cmd = 20 (UNREG_LOGIN)    */
	READ_LA_VAR varReadLA;	/* cmd = 21 (READ_LA(64))    */
	CLEAR_LA_VAR varClearLA;	/* cmd = 22 (CLEAR_LA)       */
	DUMP_VAR varDmp;	/* Warm Start DUMP mbx cmd   */
	UNREG_D_ID_VAR varUnregDID;	/* cmd = 0x23 (UNREG_D_ID)   */
	CONFIG_FARP_VAR varCfgFarp;	/* cmd = 0x25 (CONFIG_FARP)  NEW_FEATURE */
	CONFIG_PORT_VAR varCfgPort;	/* cmd = 0x88 (CONFIG_PORT)  */
} MAILVARIANTS;

/*
 * SLI-2 specific structures
 */

typedef struct {
	uint32_t cmdPutInx;
	uint32_t rspGetInx;
} HGP;

typedef struct {
	uint32_t cmdGetInx;
	uint32_t rspPutInx;
} PGP;

typedef struct _SLI2_DESC {
	HGP host[MAX_RINGS];
	uint32_t unused1[16];
	PGP port[MAX_RINGS];
} SLI2_DESC;

typedef union {
	SLI2_DESC s2;
} SLI_VAR;

typedef volatile struct {
#if BIG_ENDIAN_HW
	uint16_t mbxStatus;
	uint8_t mbxCommand;
	uint8_t mbxReserved:6;
	uint8_t mbxHc:1;
	uint8_t mbxOwner:1;	/* Low order bit first word */
#endif
#if LITTLE_ENDIAN_HW
	uint8_t mbxOwner:1;	/* Low order bit first word */
	uint8_t mbxHc:1;
	uint8_t mbxReserved:6;
	uint8_t mbxCommand;
	uint16_t mbxStatus;
#endif
	MAILVARIANTS un;
	SLI_VAR us;
} MAILBOX_t, *PMAILBOX_t;

/*
 *    Begin Structure Definitions for IOCB Commands
 */

typedef struct {
#if BIG_ENDIAN_HW
	uint8_t statAction;
	uint8_t statRsn;
	uint8_t statBaExp;
	uint8_t statLocalError;
#endif
#if LITTLE_ENDIAN_HW
	uint8_t statLocalError;
	uint8_t statBaExp;
	uint8_t statRsn;
	uint8_t statAction;
#endif
	/* statAction  FBSY reason codes */
#define FBSY_RSN_MASK   0xF0	/* Rsn stored in upper nibble */
#define FBSY_FABRIC_BSY 0x10	/* F_bsy due to Fabric BSY */
#define FBSY_NPORT_BSY  0x30	/* F_bsy due to N_port BSY */

	/* statAction  PBSY action codes */
#define PBSY_ACTION1    0x01	/* Sequence terminated - retry */
#define PBSY_ACTION2    0x02	/* Sequence active - retry */

	/* statAction  P/FRJT action codes */
#define RJT_RETRYABLE   0x01	/* Retryable class of error */
#define RJT_NO_RETRY    0x02	/* Non-Retryable class of error */

	/* statRsn  LS_RJT reason codes defined in LS_RJT structure */

	/* statRsn  P_BSY reason codes */
#define PBSY_NPORT_BSY  0x01	/* Physical N_port BSY */
#define PBSY_RESRCE_BSY 0x03	/* N_port resource BSY */
#define PBSY_VU_BSY     0xFF	/* See VU field for rsn */

	/* statRsn  P/F_RJT reason codes */
#define RJT_BAD_D_ID       0x01	/* Invalid D_ID field */
#define RJT_BAD_S_ID       0x02	/* Invalid S_ID field */
#define RJT_UNAVAIL_TEMP   0x03	/* N_Port unavailable temp. */
#define RJT_UNAVAIL_PERM   0x04	/* N_Port unavailable perm. */
#define RJT_UNSUP_CLASS    0x05	/* Class not supported */
#define RJT_DELIM_ERR      0x06	/* Delimiter usage error */
#define RJT_UNSUP_TYPE     0x07	/* Type not supported */
#define RJT_BAD_CONTROL    0x08	/* Invalid link conrtol */
#define RJT_BAD_RCTL       0x09	/* R_CTL invalid */
#define RJT_BAD_FCTL       0x0A	/* F_CTL invalid */
#define RJT_BAD_OXID       0x0B	/* OX_ID invalid */
#define RJT_BAD_RXID       0x0C	/* RX_ID invalid */
#define RJT_BAD_SEQID      0x0D	/* SEQ_ID invalid */
#define RJT_BAD_DFCTL      0x0E	/* DF_CTL invalid */
#define RJT_BAD_SEQCNT     0x0F	/* SEQ_CNT invalid */
#define RJT_BAD_PARM       0x10	/* Param. field invalid */
#define RJT_XCHG_ERR       0x11	/* Exchange error */
#define RJT_PROT_ERR       0x12	/* Protocol error */
#define RJT_BAD_LENGTH     0x13	/* Invalid Length */
#define RJT_UNEXPECTED_ACK 0x14	/* Unexpected ACK */
#define RJT_LOGIN_REQUIRED 0x16	/* Login required */
#define RJT_TOO_MANY_SEQ   0x17	/* Excessive sequences */
#define RJT_XCHG_NOT_STRT  0x18	/* Exchange not started */
#define RJT_UNSUP_SEC_HDR  0x19	/* Security hdr not supported */
#define RJT_UNAVAIL_PATH   0x1A	/* Fabric Path not available */
#define RJT_VENDOR_UNIQUE  0xFF	/* Vendor unique error */

	/* statRsn  BA_RJT reason codes */
#define BARJT_BAD_CMD_CODE 0x01	/* Invalid command code */
#define BARJT_LOGICAL_ERR  0x03	/* Logical error */
#define BARJT_LOGICAL_BSY  0x05	/* Logical busy */
#define BARJT_PROTOCOL_ERR 0x07	/* Protocol error */
#define BARJT_VU_ERR       0xFF	/* Vendor unique error */

	/* LS_RJT reason explanation defined in LS_RJT structure */

	/* BA_RJT reason explanation */
#define BARJT_EXP_INVALID_ID  0x01	/* Invalid OX_ID/RX_ID */
#define BARJT_EXP_ABORT_SEQ   0x05	/* Abort SEQ, no more info */

	/* FireFly localy detected errors */
#define IOERR_SUCCESS                 0x00	/* statLocalError */
#define IOERR_MISSING_CONTINUE        0x01
#define IOERR_SEQUENCE_TIMEOUT        0x02
#define IOERR_INTERNAL_ERROR          0x03
#define IOERR_INVALID_RPI             0x04
#define IOERR_NO_XRI                  0x05
#define IOERR_ILLEGAL_COMMAND         0x06
#define IOERR_XCHG_DROPPED            0x07
#define IOERR_ILLEGAL_FIELD           0x08
#define IOERR_BAD_CONTINUE            0x09
#define IOERR_TOO_MANY_BUFFERS        0x0A
#define IOERR_RCV_BUFFER_WAITING      0x0B
#define IOERR_NO_CONNECTION           0x0C
#define IOERR_TX_DMA_FAILED           0x0D
#define IOERR_RX_DMA_FAILED           0x0E
#define IOERR_ILLEGAL_FRAME           0x0F
#define IOERR_EXTRA_DATA              0x10
#define IOERR_NO_RESOURCES            0x11
#define IOERR_RESERVED                0x12
#define IOERR_ILLEGAL_LENGTH          0x13
#define IOERR_UNSUPPORTED_FEATURE     0x14
#define IOERR_ABORT_IN_PROGRESS       0x15
#define IOERR_ABORT_REQUESTED         0x16
#define IOERR_RECEIVE_BUFFER_TIMEOUT  0x17
#define IOERR_LOOP_OPEN_FAILURE       0x18
#define IOERR_RING_RESET              0x19
#define IOERR_LINK_DOWN               0x1A
#define IOERR_CORRUPTED_DATA          0x1B
#define IOERR_CORRUPTED_RPI           0x1C
#define IOERR_OUT_OF_ORDER_DATA       0x1D
#define IOERR_OUT_OF_ORDER_ACK        0x1E
#define IOERR_DUP_FRAME               0x1F
#define IOERR_LINK_CONTROL_FRAME      0x20	/* ACK_N received */
#define IOERR_BAD_HOST_ADDRESS        0x21
#define IOERR_RCV_HDRBUF_WAITING      0x22
#define IOERR_MISSING_HDR_BUFFER      0x23
#define IOERR_MSEQ_CHAIN_CORRUPTED    0x24
#define IOERR_ABORTMULT_REQUESTED     0x25
#define IOERR_BUFFER_SHORTAGE         0x28
#define IOERR_DEFAULT                 0x29
#define IOERR_CNT                     0x2A

#define IOERR_SLI_DOWN                0xF0	/* ulpStatus  - Driver defined */
#define IOERR_SLI_BRESET              0xF1
#define IOERR_SLI_ABORTED             0xF2
} PARM_ERR;

typedef union {
	struct {
#if BIG_ENDIAN_HW
		uint8_t Rctl;	/* R_CTL field */
		uint8_t Type;	/* TYPE field */
		uint8_t Dfctl;	/* DF_CTL field */
		uint8_t Fctl;	/* Bits 0-7 of IOCB word 5 */
#endif
#if LITTLE_ENDIAN_HW
		uint8_t Fctl;	/* Bits 0-7 of IOCB word 5 */
		uint8_t Dfctl;	/* DF_CTL field */
		uint8_t Type;	/* TYPE field */
		uint8_t Rctl;	/* R_CTL field */
#endif

#define BC      0x02		/* Broadcast Received  - Fctl */
#define SI      0x04		/* Sequence Initiative */
#define LA      0x08		/* Ignore Link Attention state */
#define LS      0x80		/* Last Sequence */
	} hcsw;
	uint32_t reserved;
} WORD5;

/* IOCB Command template for a generic response */
typedef struct {
	uint32_t reserved[4];
	PARM_ERR perr;
} GENERIC_RSP;

/* IOCB Command template for XMIT / XMIT_BCAST / RCV_SEQUENCE / XMIT_ELS */
typedef struct {
	ULP_BDE xrsqbde[2];
	uint32_t xrsqRo;	/* Starting Relative Offset */
	WORD5 w5;		/* Header control/status word */
} XR_SEQ_FIELDS;

/* IOCB Command template for ELS_REQUEST */
typedef struct {
	ULP_BDE elsReq;
	ULP_BDE elsRsp;
#if BIG_ENDIAN_HW
	uint32_t word4Rsvd:7;
	uint32_t fl:1;
	uint32_t myID:24;
	uint32_t word5Rsvd:8;
	uint32_t remoteID:24;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t myID:24;
	uint32_t fl:1;
	uint32_t word4Rsvd:7;
	uint32_t remoteID:24;
	uint32_t word5Rsvd:8;
#endif
} ELS_REQUEST;

/* IOCB Command template for RCV_ELS_REQ */
typedef struct {
	ULP_BDE elsReq[2];
	uint32_t parmRo;
#if BIG_ENDIAN_HW
	uint32_t word5Rsvd:8;
	uint32_t remoteID:24;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t remoteID:24;
	uint32_t word5Rsvd:8;
#endif
} RCV_ELS_REQ;

/* IOCB Command template for ABORT / CLOSE_XRI */
typedef struct {
	uint32_t rsvd[3];
	uint32_t abortType;
#define ABORT_TYPE_ABTX  0x00000000
#define ABORT_TYPE_ABTS  0x00000001
	uint32_t parm;
#if BIG_ENDIAN_HW
	uint16_t abortContextTag;	/* ulpContext from command to abort/close */
	uint16_t abortIoTag;	/* ulpIoTag from command to abort/close */
#endif
#if LITTLE_ENDIAN_HW
	uint16_t abortIoTag;	/* ulpIoTag from command to abort/close */
	uint16_t abortContextTag;	/* ulpContext from command to abort/close */
#endif
} AC_XRI;

/* IOCB Command template for ABORT_MXRI64 */
typedef struct {
	uint32_t rsvd[3];
	uint32_t abortType;
	uint32_t parm;
	uint32_t iotag32;
} A_MXRI64;

/* IOCB Command template for GET_RPI */
typedef struct {
	uint32_t rsvd[4];
	uint32_t parmRo;
#if BIG_ENDIAN_HW
	uint32_t word5Rsvd:8;
	uint32_t remoteID:24;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t remoteID:24;
	uint32_t word5Rsvd:8;
#endif
} GET_RPI;

/* IOCB Command template for all FCP Initiator commands */
typedef struct {
	ULP_BDE fcpi_cmnd;	/* FCP_CMND payload descriptor */
	ULP_BDE fcpi_rsp;	/* Rcv buffer */
	uint32_t fcpi_parm;
	uint32_t fcpi_XRdy;	/* transfer ready for IWRITE */
} FCPI_FIELDS;

/* IOCB Command template for all FCP Target commands */
typedef struct {
	ULP_BDE fcpt_Buffer[2];	/* FCP_CMND payload descriptor */
	uint32_t fcpt_Offset;
	uint32_t fcpt_Length;	/* transfer ready for IWRITE */
} FCPT_FIELDS;

/* SLI-2 IOCB structure definitions */

/* IOCB Command template for 64 bit XMIT / XMIT_BCAST / XMIT_ELS */
typedef struct {
	ULP_BDL bdl;
	uint32_t xrsqRo;	/* Starting Relative Offset */
	WORD5 w5;		/* Header control/status word */
} XMT_SEQ_FIELDS64;

/* IOCB Command template for 64 bit RCV_SEQUENCE64 */
typedef struct {
	ULP_BDE64 rcvBde;
	uint32_t rsvd1;
	uint32_t xrsqRo;	/* Starting Relative Offset */
	WORD5 w5;		/* Header control/status word */
} RCV_SEQ_FIELDS64;

/* IOCB Command template for ELS_REQUEST64 */
typedef struct {
	ULP_BDL bdl;
#if BIG_ENDIAN_HW
	uint32_t word4Rsvd:7;
	uint32_t fl:1;
	uint32_t myID:24;
	uint32_t word5Rsvd:8;
	uint32_t remoteID:24;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t myID:24;
	uint32_t fl:1;
	uint32_t word4Rsvd:7;
	uint32_t remoteID:24;
	uint32_t word5Rsvd:8;
#endif
} ELS_REQUEST64;

/* IOCB Command template for GEN_REQUEST64 */
typedef struct {
	ULP_BDL bdl;
	uint32_t xrsqRo;	/* Starting Relative Offset */
	WORD5 w5;		/* Header control/status word */
} GEN_REQUEST64;

/* IOCB Command template for RCV_ELS_REQ64 */
typedef struct {
	ULP_BDE64 elsReq;
	uint32_t rcvd1;
	uint32_t parmRo;
#if BIG_ENDIAN_HW
	uint32_t word5Rsvd:8;
	uint32_t remoteID:24;
#endif
#if LITTLE_ENDIAN_HW
	uint32_t remoteID:24;
	uint32_t word5Rsvd:8;
#endif
} RCV_ELS_REQ64;

/* IOCB Command template for all 64 bit FCP Initiator commands */
typedef struct {
	ULP_BDL bdl;
	uint32_t fcpi_parm;
	uint32_t fcpi_XRdy;	/* transfer ready for IWRITE */
} FCPI_FIELDS64;

/* IOCB Command template for all 64 bit FCP Target commands */
typedef struct {
	ULP_BDL bdl;
	uint32_t fcpt_Offset;
	uint32_t fcpt_Length;	/* transfer ready for IWRITE */
} FCPT_FIELDS64;

typedef volatile struct _IOCB {	/* IOCB structure */
	union {
		GENERIC_RSP grsp;	/* Generic response */
		XR_SEQ_FIELDS xrseq;	/* XMIT / BCAST / RCV_SEQUENCE cmd */
		ULP_BDE cont[3];	/* up to 3 continuation bdes */
		RCV_ELS_REQ rcvels;	/* RCV_ELS_REQ template */
		AC_XRI acxri;	/* ABORT / CLOSE_XRI template */
		A_MXRI64 amxri;	/* abort multiple xri command overlay */
		GET_RPI getrpi;	/* GET_RPI template */
		FCPI_FIELDS fcpi;	/* FCP Initiator template */
		FCPT_FIELDS fcpt;	/* FCP target template */

		/* SLI-2 structures */

		ULP_BDE64 cont64[2];	/* up to 2 64 bit continuation bde_64s */
		ELS_REQUEST64 elsreq64;	/* ELS_REQUEST template */
		GEN_REQUEST64 genreq64;	/* GEN_REQUEST template */
		RCV_ELS_REQ64 rcvels64;	/* RCV_ELS_REQ template */
		XMT_SEQ_FIELDS64 xseq64;	/* XMIT / BCAST cmd */
		FCPI_FIELDS64 fcpi64;	/* FCP 64 bit Initiator template */
		FCPT_FIELDS64 fcpt64;	/* FCP 64 bit target template */

		uint32_t ulpWord[IOCB_WORD_SZ - 2];	/* generic 6 'words' */
	} un;
	union {
		struct {
#if BIG_ENDIAN_HW
			uint16_t ulpContext;	/* High order bits word 6 */
			uint16_t ulpIoTag;	/* Low  order bits word 6 */
#endif
#if LITTLE_ENDIAN_HW
			uint16_t ulpIoTag;	/* Low  order bits word 6 */
			uint16_t ulpContext;	/* High order bits word 6 */
#endif
		} t1;
		struct {
#if BIG_ENDIAN_HW
			uint16_t ulpContext;	/* High order bits word 6 */
			uint16_t ulpIoTag1:2;	/* Low  order bits word 6 */
			uint16_t ulpIoTag0:14;	/* Low  order bits word 6 */
#endif
#if LITTLE_ENDIAN_HW
			uint16_t ulpIoTag0:14;	/* Low  order bits word 6 */
			uint16_t ulpIoTag1:2;	/* Low  order bits word 6 */
			uint16_t ulpContext;	/* High order bits word 6 */
#endif
		} t2;
	} un1;
#define ulpContext un1.t1.ulpContext
#define ulpIoTag   un1.t1.ulpIoTag
#define ulpIoTag0  un1.t2.ulpIoTag0
#define ulpDelayXmit  un1.t2.ulpIoTag1
#define IOCB_DELAYXMIT_MSK 0x3000
#if BIG_ENDIAN_HW
	uint32_t ulpTimeout:8;
	uint32_t ulpXS:1;
	uint32_t ulpFCP2Rcvy:1;
	uint32_t ulpPU:2;
	uint32_t ulpIr:1;
	uint32_t ulpClass:3;
	uint32_t ulpCommand:8;
	uint32_t ulpStatus:4;
	uint32_t ulpBdeCount:2;
	uint32_t ulpLe:1;
	uint32_t ulpOwner:1;	/* Low order bit word 7 */
#endif
#if LITTLE_ENDIAN_HW
	uint32_t ulpOwner:1;	/* Low order bit word 7 */
	uint32_t ulpLe:1;
	uint32_t ulpBdeCount:2;
	uint32_t ulpStatus:4;
	uint32_t ulpCommand:8;
	uint32_t ulpClass:3;
	uint32_t ulpIr:1;
	uint32_t ulpPU:2;
	uint32_t ulpFCP2Rcvy:1;
	uint32_t ulpXS:1;
	uint32_t ulpTimeout:8;
#endif

#define IOCB_FCP           1	/* IOCB is used for FCP ELS cmds - ulpRsvByte */
#define IOCB_IP            2	/* IOCB is used for IP ELS cmds */
#define PARM_UNUSED        0	/* PU field (Word 4) not used */
#define PARM_REL_OFF       1	/* PU field (Word 4) = R. O. */
#define PARM_READ_CHECK    2	/* PU field (Word 4) = Data Transfer Length */
#define CLASS1             0	/* Class 1 */
#define CLASS2             1	/* Class 2 */
#define CLASS3             2	/* Class 3 */
#define CLASS_FCP_INTERMIX 7	/* FCP Data->Cls 1, all else->Cls 2 */

#define IOSTAT_SUCCESS         0x0	/* ulpStatus  - HBA defined */
#define IOSTAT_FCP_RSP_ERROR   0x1
#define IOSTAT_REMOTE_STOP     0x2
#define IOSTAT_LOCAL_REJECT    0x3
#define IOSTAT_NPORT_RJT       0x4
#define IOSTAT_FABRIC_RJT      0x5
#define IOSTAT_NPORT_BSY       0x6
#define IOSTAT_FABRIC_BSY      0x7
#define IOSTAT_INTERMED_RSP    0x8
#define IOSTAT_LS_RJT          0x9
#define IOSTAT_BA_RJT          0xA
#define IOSTAT_DRIVER_REJECT   0xB	/* ulpStatus  - Driver defined */
#define IOSTAT_ISCSI_REJECT    0xC
#define IOSTAT_DEFAULT         0xD
#define IOSTAT_CNT             0xE

} IOCB_t, *PIOCB_t;

/* Up to 244 IOCBs will fit into 8k 
 * 256 (MAILBOX_t) + 140 (PCB_t) + ( 32 (IOCB_t) * 240 ) = <8192
 */
#define SLI2_SLIM_SIZE   8192

/* Maximum IOCBs that will fit in SLI2 slim */
#define MAX_SLI2_IOCB    240

typedef struct {
	union {
		uint8_t sli2slim[SLI2_SLIM_SIZE];
		struct {
			MAILBOX_t mbx;
			PCB_t pcb;
			IOCB_t IOCBs[MAX_SLI2_IOCB];
		} slim;
	} un;
} SLI2_SLIM_t;

#endif				/* _H_ELX_HW */
