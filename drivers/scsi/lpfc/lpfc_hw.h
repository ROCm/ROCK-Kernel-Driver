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

#ifndef  _H_LPFC_HW
#define _H_LPFC_HW

#define FDMI_DID        ((uint32_t)0xfffffa)
#define NameServer_DID  ((uint32_t)0xfffffc)
#define SCR_DID         ((uint32_t)0xfffffd)
#define Fabric_DID      ((uint32_t)0xfffffe)
#define Bcast_DID       ((uint32_t)0xffffff)
#define Mask_DID        ((uint32_t)0xffffff)
#define CT_DID_MASK     ((uint32_t)0xffff00)
#define Fabric_DID_MASK ((uint32_t)0xfff000)
#define WELL_KNOWN_DID_MASK ((uint32_t)0xfffff0)

#define PT2PT_LocalID   ((uint32_t)1)
#define PT2PT_RemoteID  ((uint32_t)2)

#define FF_DEF_EDTOV          2000	/* Default E_D_TOV (2000ms) */
#define FF_DEF_ALTOV            15	/* Default AL_TIME (15ms) */
#define FF_DEF_RATOV             2	/* Default RA_TOV (2s) */
#define FF_DEF_ARBTOV         1900	/* Default ARB_TOV (1900ms) */

#define LPFC_BUF_RING0      128	/* Number of buffers to post to RING 0 */

#define FCELSSIZE             1024	/* maximum ELS transfer size */

#define LPFC_ELS_RING            0	/* use ring 0 for ELS commands */
#define LPFC_IP_RING             1	/* use ring 1 for IP commands */
#define LPFC_FCP_RING            2	/* use ring 2 for FCP initiator commands */
#define LPFC_FCP_NEXT_RING       3

#define SLI2_IOCB_CMD_R0_ENTRIES     16	/* SLI-2 ELS command ring entries */
#define SLI2_IOCB_RSP_R0_ENTRIES     16	/* SLI-2 ELS response ring entries */
#define SLI2_IOCB_CMD_R1_ENTRIES      4	/* SLI-2 IP command ring entries */
#define SLI2_IOCB_RSP_R1_ENTRIES      4	/* SLI-2 IP response ring entries */
#define SLI2_IOCB_CMD_R1XTRA_ENTRIES 32	/* SLI-2 extra FCP cmd ring entries */
#define SLI2_IOCB_RSP_R1XTRA_ENTRIES 48	/* SLI-2 extra FCP rsp ring entries */
#define SLI2_IOCB_CMD_R2_ENTRIES     32	/* SLI-2 FCP command ring entries */
#define SLI2_IOCB_RSP_R2_ENTRIES     32	/* SLI-2 FCP response ring entries */
#define SLI2_IOCB_CMD_R3_ENTRIES      0
#define SLI2_IOCB_RSP_R3_ENTRIES      0
#define SLI2_IOCB_CMD_R3XTRA_ENTRIES 24
#define SLI2_IOCB_RSP_R3XTRA_ENTRIES 32

/* Common Transport structures and definitions */

union CtRevisionId {
	/* Structure is in Big Endian format */
	struct {
		uint32_t Revision:8;
		uint32_t InId:24;
	} bits;
	uint32_t word;
};

union CtCommandResponse {
	/* Structure is in Big Endian format */
	struct {
		uint32_t CmdRsp:16;
		uint32_t Size:16;
	} bits;
	uint32_t word;
};

typedef struct SliCtRequest {
	/* Structure is in Big Endian format */
	union CtRevisionId RevisionId;
	uint8_t FsType;
	uint8_t FsSubType;
	uint8_t Options;
	uint8_t Rsrvd1;
	union CtCommandResponse CommandResponse;
	uint8_t Rsrvd2;
	uint8_t ReasonCode;
	uint8_t Explanation;
	uint8_t VendorUnique;

	union {
		uint32_t PortID;
		struct gid {
			uint8_t PortType;	/* for GID_PT requests */
			uint8_t DomainScope;
			uint8_t AreaScope;
			uint8_t Fc4Type;	/* for GID_FT requests */
		} gid;
		struct rft {
			uint32_t PortId;	/* For RFT_ID requests */
#if BIG_ENDIAN_HW
			uint32_t rsvd0:16;
			uint32_t rsvd1:7;
			uint32_t fcpReg:1;	/* Type 8 */
			uint32_t rsvd2:2;
			uint32_t ipReg:1;	/* Type 5 */
			uint32_t rsvd3:5;
#endif
#if LITTLE_ENDIAN_HW
			uint32_t rsvd0:16;
			uint32_t fcpReg:1;	/* Type 8 */
			uint32_t rsvd1:7;
			uint32_t rsvd3:5;
			uint32_t ipReg:1;	/* Type 5 */
			uint32_t rsvd2:2;
#endif
			uint32_t rsvd[7];
		} rft;
		struct rnn {
			uint32_t PortId;	/* For RNN_ID requests */
			uint8_t wwnn[8];
		} rnn;
		struct rsnn {	/* For RSNN_ID requests */
			uint8_t wwnn[8];
			uint8_t len;
			uint8_t symbname[255];
		} rsnn;
	} un;
} SLI_CT_REQUEST, *PSLI_CT_REQUEST;

#define  SLI_CT_REVISION        1
#define  GID_REQUEST_SZ         (sizeof(SLI_CT_REQUEST) - 260)
#define  RFT_REQUEST_SZ         (sizeof(SLI_CT_REQUEST) - 228)
#define  RNN_REQUEST_SZ         (sizeof(SLI_CT_REQUEST) - 252)
#define  RSNN_REQUEST_SZ        (sizeof(SLI_CT_REQUEST))

/*
 * FsType Definitions
 */

#define  SLI_CT_MANAGEMENT_SERVICE        0xFA
#define  SLI_CT_TIME_SERVICE              0xFB
#define  SLI_CT_DIRECTORY_SERVICE         0xFC
#define  SLI_CT_FABRIC_CONTROLLER_SERVICE 0xFD

/*
 * Directory Service Subtypes
 */

#define  SLI_CT_DIRECTORY_NAME_SERVER     0x02

/*
 * Response Codes
 */

#define  SLI_CT_RESPONSE_FS_RJT           0x8001
#define  SLI_CT_RESPONSE_FS_ACC           0x8002

/*
 * Reason Codes
 */

#define  SLI_CT_NO_ADDITIONAL_EXPL	  0x0
#define  SLI_CT_INVALID_COMMAND           0x01
#define  SLI_CT_INVALID_VERSION           0x02
#define  SLI_CT_LOGICAL_ERROR             0x03
#define  SLI_CT_INVALID_IU_SIZE           0x04
#define  SLI_CT_LOGICAL_BUSY              0x05
#define  SLI_CT_PROTOCOL_ERROR            0x07
#define  SLI_CT_UNABLE_TO_PERFORM_REQ     0x09
#define  SLI_CT_REQ_NOT_SUPPORTED         0x0b
#define  SLI_CT_HBA_INFO_NOT_REGISTERED	  0x10
#define  SLI_CT_MULTIPLE_HBA_ATTR_OF_SAME_TYPE  0x11
#define  SLI_CT_INVALID_HBA_ATTR_BLOCK_LEN      0x12
#define  SLI_CT_HBA_ATTR_NOT_PRESENT	  0x13
#define  SLI_CT_PORT_INFO_NOT_REGISTERED  0x20
#define  SLI_CT_MULTIPLE_PORT_ATTR_OF_SAME_TYPE 0x21
#define  SLI_CT_INVALID_PORT_ATTR_BLOCK_LEN     0x22
#define  SLI_CT_VENDOR_UNIQUE             0xff

/*
 * Name Server SLI_CT_UNABLE_TO_PERFORM_REQ Explanations
 */

#define  SLI_CT_NO_PORT_ID                0x01
#define  SLI_CT_NO_PORT_NAME              0x02
#define  SLI_CT_NO_NODE_NAME              0x03
#define  SLI_CT_NO_CLASS_OF_SERVICE       0x04
#define  SLI_CT_NO_IP_ADDRESS             0x05
#define  SLI_CT_NO_IPA                    0x06
#define  SLI_CT_NO_FC4_TYPES              0x07
#define  SLI_CT_NO_SYMBOLIC_PORT_NAME     0x08
#define  SLI_CT_NO_SYMBOLIC_NODE_NAME     0x09
#define  SLI_CT_NO_PORT_TYPE              0x0A
#define  SLI_CT_ACCESS_DENIED             0x10
#define  SLI_CT_INVALID_PORT_ID           0x11
#define  SLI_CT_DATABASE_EMPTY            0x12

/*
 * Name Server Command Codes
 */

#define  SLI_CTNS_GA_NXT      0x0100
#define  SLI_CTNS_GPN_ID      0x0112
#define  SLI_CTNS_GNN_ID      0x0113
#define  SLI_CTNS_GCS_ID      0x0114
#define  SLI_CTNS_GFT_ID      0x0117
#define  SLI_CTNS_GSPN_ID     0x0118
#define  SLI_CTNS_GPT_ID      0x011A
#define  SLI_CTNS_GID_PN      0x0121
#define  SLI_CTNS_GID_NN      0x0131
#define  SLI_CTNS_GIP_NN      0x0135
#define  SLI_CTNS_GIPA_NN     0x0136
#define  SLI_CTNS_GSNN_NN     0x0139
#define  SLI_CTNS_GNN_IP      0x0153
#define  SLI_CTNS_GIPA_IP     0x0156
#define  SLI_CTNS_GID_FT      0x0171
#define  SLI_CTNS_GID_PT      0x01A1
#define  SLI_CTNS_RPN_ID      0x0212
#define  SLI_CTNS_RNN_ID      0x0213
#define  SLI_CTNS_RCS_ID      0x0214
#define  SLI_CTNS_RFT_ID      0x0217
#define  SLI_CTNS_RSPN_ID     0x0218
#define  SLI_CTNS_RPT_ID      0x021A
#define  SLI_CTNS_RIP_NN      0x0235
#define  SLI_CTNS_RIPA_NN     0x0236
#define  SLI_CTNS_RSNN_NN     0x0239
#define  SLI_CTNS_DA_ID       0x0300

/*
 * Port Types
 */

#define  SLI_CTPT_N_PORT      0x01
#define  SLI_CTPT_NL_PORT     0x02
#define  SLI_CTPT_FNL_PORT    0x03
#define  SLI_CTPT_IP          0x04
#define  SLI_CTPT_FCP         0x08
#define  SLI_CTPT_NX_PORT     0x7F
#define  SLI_CTPT_F_PORT      0x81
#define  SLI_CTPT_FL_PORT     0x82
#define  SLI_CTPT_E_PORT      0x84

#define SLI_CT_LAST_ENTRY     0x80000000

#define FL_ALPA    0x00		/* AL_PA of FL_Port */

/* Fibre Channel Service Parameter definitions */

#define FC_PH_4_0   6		/* FC-PH version 4.0 */
#define FC_PH_4_1   7		/* FC-PH version 4.1 */
#define FC_PH_4_2   8		/* FC-PH version 4.2 */
#define FC_PH_4_3   9		/* FC-PH version 4.3 */

#define FC_PH_LOW   8		/* Lowest supported FC-PH version */
#define FC_PH_HIGH  9		/* Highest supported FC-PH version */
#define FC_PH3   0x20		/* FC-PH-3 version */

#define FF_FRAME_SIZE     2048

typedef struct _NAME_TYPE {
#if BIG_ENDIAN_HW
	uint8_t nameType:4;	/* FC Word 0, bit 28:31 */
	uint8_t IEEEextMsn:4;	/* FC Word 0, bit 24:27, bit 8:11 of IEEE ext */
#endif
#if LITTLE_ENDIAN_HW
	uint8_t IEEEextMsn:4;	/* FC Word 0, bit 24:27, bit 8:11 of IEEE ext */
	uint8_t nameType:4;	/* FC Word 0, bit 28:31 */
#endif
#define NAME_IEEE           0x1	/* IEEE name - nameType */
#define NAME_IEEE_EXT       0x2	/* IEEE extended name */
#define NAME_FC_TYPE        0x3	/* FC native name type */
#define NAME_IP_TYPE        0x4	/* IP address */
#define NAME_CCITT_TYPE     0xC
#define NAME_CCITT_GR_TYPE  0xE
	uint8_t IEEEextLsb;	/* FC Word 0, bit 16:23, IEEE extended Lsb */
	uint8_t IEEE[6];	/* FC IEEE address */
} NAME_TYPE;

typedef struct _CSP {
	uint8_t fcphHigh;	/* FC Word 0, byte 0 */
	uint8_t fcphLow;
	uint8_t bbCreditMsb;
	uint8_t bbCreditlsb;	/* FC Word 0, byte 3 */
#if BIG_ENDIAN_HW
	uint16_t increasingOffset:1;	/* FC Word 1, bit 31 */
	uint16_t randomOffset:1;	/* FC Word 1, bit 30 */
	uint16_t word1Reserved2:1;	/* FC Word 1, bit 29 */
	uint16_t fPort:1;	/* FC Word 1, bit 28 */
	uint16_t altBbCredit:1;	/* FC Word 1, bit 27 */
	uint16_t edtovResolution:1;	/* FC Word 1, bit 26 */
	uint16_t multicast:1;	/* FC Word 1, bit 25 */
	uint16_t broadcast:1;	/* FC Word 1, bit 24 */

	uint16_t huntgroup:1;	/* FC Word 1, bit 23 */
	uint16_t simplex:1;	/* FC Word 1, bit 22 */
	uint16_t word1Reserved1:3;	/* FC Word 1, bit 21:19 */
	uint16_t dhd:1;		/* FC Word 1, bit 18 */
	uint16_t contIncSeqCnt:1;	/* FC Word 1, bit 17 */
	uint16_t payloadlength:1;	/* FC Word 1, bit 16 */
#endif
#if LITTLE_ENDIAN_HW
	uint16_t broadcast:1;	/* FC Word 1, bit 24 */
	uint16_t multicast:1;	/* FC Word 1, bit 25 */
	uint16_t edtovResolution:1;	/* FC Word 1, bit 26 */
	uint16_t altBbCredit:1;	/* FC Word 1, bit 27 */
	uint16_t fPort:1;	/* FC Word 1, bit 28 */
	uint16_t word1Reserved2:1;	/* FC Word 1, bit 29 */
	uint16_t randomOffset:1;	/* FC Word 1, bit 30 */
	uint16_t increasingOffset:1;	/* FC Word 1, bit 31 */

	uint16_t payloadlength:1;	/* FC Word 1, bit 16 */
	uint16_t contIncSeqCnt:1;	/* FC Word 1, bit 17 */
	uint16_t dhd:1;		/* FC Word 1, bit 18 */
	uint16_t word1Reserved1:3;	/* FC Word 1, bit 21:19 */
	uint16_t simplex:1;	/* FC Word 1, bit 22 */
	uint16_t huntgroup:1;	/* FC Word 1, bit 23 */
#endif
	uint8_t bbRcvSizeMsb;	/* Upper nibble is reserved */

	uint8_t bbRcvSizeLsb;	/* FC Word 1, byte 3 */
	union {
		struct {
			uint8_t word2Reserved1;	/* FC Word 2 byte 0 */

			uint8_t totalConcurrSeq;	/* FC Word 2 byte 1 */
			uint8_t roByCategoryMsb;	/* FC Word 2 byte 2 */

			uint8_t roByCategoryLsb;	/* FC Word 2 byte 3 */
		} nPort;
		uint32_t r_a_tov;	/* R_A_TOV must be in B.E. format */
	} w2;

	uint32_t e_d_tov;	/* E_D_TOV must be in B.E. format */
} CSP;

typedef struct _CLASS_PARMS {
#if BIG_ENDIAN_HW
	uint8_t classValid:1;	/* FC Word 0, bit 31 */
	uint8_t intermix:1;	/* FC Word 0, bit 30 */
	uint8_t stackedXparent:1;	/* FC Word 0, bit 29 */
	uint8_t stackedLockDown:1;	/* FC Word 0, bit 28 */
	uint8_t seqDelivery:1;	/* FC Word 0, bit 27 */
	uint8_t word0Reserved1:3;	/* FC Word 0, bit 24:26 */
#endif
#if LITTLE_ENDIAN_HW
	uint8_t word0Reserved1:3;	/* FC Word 0, bit 24:26 */
	uint8_t seqDelivery:1;	/* FC Word 0, bit 27 */
	uint8_t stackedLockDown:1;	/* FC Word 0, bit 28 */
	uint8_t stackedXparent:1;	/* FC Word 0, bit 29 */
	uint8_t intermix:1;	/* FC Word 0, bit 30 */
	uint8_t classValid:1;	/* FC Word 0, bit 31 */

#endif
	uint8_t word0Reserved2;	/* FC Word 0, bit 16:23 */
#if BIG_ENDIAN_HW
	uint8_t iCtlXidReAssgn:2;	/* FC Word 0, Bit 14:15 */
	uint8_t iCtlInitialPa:2;	/* FC Word 0, bit 12:13 */
	uint8_t iCtlAck0capable:1;	/* FC Word 0, bit 11 */
	uint8_t iCtlAckNcapable:1;	/* FC Word 0, bit 10 */
	uint8_t word0Reserved3:2;	/* FC Word 0, bit  8: 9 */
#endif
#if LITTLE_ENDIAN_HW
	uint8_t word0Reserved3:2;	/* FC Word 0, bit  8: 9 */
	uint8_t iCtlAckNcapable:1;	/* FC Word 0, bit 10 */
	uint8_t iCtlAck0capable:1;	/* FC Word 0, bit 11 */
	uint8_t iCtlInitialPa:2;	/* FC Word 0, bit 12:13 */
	uint8_t iCtlXidReAssgn:2;	/* FC Word 0, Bit 14:15 */
#endif
	uint8_t word0Reserved4;	/* FC Word 0, bit  0: 7 */
#if BIG_ENDIAN_HW
	uint8_t rCtlAck0capable:1;	/* FC Word 1, bit 31 */
	uint8_t rCtlAckNcapable:1;	/* FC Word 1, bit 30 */
	uint8_t rCtlXidInterlck:1;	/* FC Word 1, bit 29 */
	uint8_t rCtlErrorPolicy:2;	/* FC Word 1, bit 27:28 */
	uint8_t word1Reserved1:1;	/* FC Word 1, bit 26 */
	uint8_t rCtlCatPerSeq:2;	/* FC Word 1, bit 24:25 */
#endif
#if LITTLE_ENDIAN_HW
	uint8_t rCtlCatPerSeq:2;	/* FC Word 1, bit 24:25 */
	uint8_t word1Reserved1:1;	/* FC Word 1, bit 26 */
	uint8_t rCtlErrorPolicy:2;	/* FC Word 1, bit 27:28 */
	uint8_t rCtlXidInterlck:1;	/* FC Word 1, bit 29 */
	uint8_t rCtlAckNcapable:1;	/* FC Word 1, bit 30 */
	uint8_t rCtlAck0capable:1;	/* FC Word 1, bit 31 */
#endif
	uint8_t word1Reserved2;	/* FC Word 1, bit 16:23 */
	uint8_t rcvDataSizeMsb;	/* FC Word 1, bit  8:15 */
	uint8_t rcvDataSizeLsb;	/* FC Word 1, bit  0: 7 */

	uint8_t concurrentSeqMsb;	/* FC Word 2, bit 24:31 */
	uint8_t concurrentSeqLsb;	/* FC Word 2, bit 16:23 */
	uint8_t EeCreditSeqMsb;	/* FC Word 2, bit  8:15 */
	uint8_t EeCreditSeqLsb;	/* FC Word 2, bit  0: 7 */

	uint8_t openSeqPerXchgMsb;	/* FC Word 3, bit 24:31 */
	uint8_t openSeqPerXchgLsb;	/* FC Word 3, bit 16:23 */
	uint8_t word3Reserved1;	/* Fc Word 3, bit  8:15 */
	uint8_t word3Reserved2;	/* Fc Word 3, bit  0: 7 */
} CLASS_PARMS;

typedef struct _SERV_PARM {	/* Structure is in Big Endian format */
	CSP cmn;
	NAME_TYPE portName;
	NAME_TYPE nodeName;
	CLASS_PARMS cls1;
	CLASS_PARMS cls2;
	CLASS_PARMS cls3;
	CLASS_PARMS cls4;
	uint8_t vendorVersion[16];
} SERV_PARM, *PSERV_PARM;

/*
 *  Extended Link Service LS_COMMAND codes (Payload Word 0)
 */
#if BIG_ENDIAN_HW
#define ELS_CMD_MASK      0xffff0000
#define ELS_RSP_MASK      0xff000000
#define ELS_CMD_LS_RJT    0x01000000
#define ELS_CMD_ACC       0x02000000
#define ELS_CMD_PLOGI     0x03000000
#define ELS_CMD_FLOGI     0x04000000
#define ELS_CMD_LOGO      0x05000000
#define ELS_CMD_ABTX      0x06000000
#define ELS_CMD_RCS       0x07000000
#define ELS_CMD_RES       0x08000000
#define ELS_CMD_RSS       0x09000000
#define ELS_CMD_RSI       0x0A000000
#define ELS_CMD_ESTS      0x0B000000
#define ELS_CMD_ESTC      0x0C000000
#define ELS_CMD_ADVC      0x0D000000
#define ELS_CMD_RTV       0x0E000000
#define ELS_CMD_RLS       0x0F000000
#define ELS_CMD_ECHO      0x10000000
#define ELS_CMD_TEST      0x11000000
#define ELS_CMD_RRQ       0x12000000
#define ELS_CMD_PRLI      0x20100014
#define ELS_CMD_PRLO      0x21100014
#define ELS_CMD_PDISC     0x50000000
#define ELS_CMD_FDISC     0x51000000
#define ELS_CMD_ADISC     0x52000000
#define ELS_CMD_FARP      0x54000000
#define ELS_CMD_FARPR     0x55000000
#define ELS_CMD_FAN       0x60000000
#define ELS_CMD_RSCN      0x61040000
#define ELS_CMD_SCR       0x62000000
#define ELS_CMD_RNID      0x78000000
#endif
#if LITTLE_ENDIAN_HW
#define ELS_CMD_MASK      0xffff
#define ELS_RSP_MASK      0xff
#define ELS_CMD_LS_RJT    0x01
#define ELS_CMD_ACC       0x02
#define ELS_CMD_PLOGI     0x03
#define ELS_CMD_FLOGI     0x04
#define ELS_CMD_LOGO      0x05
#define ELS_CMD_ABTX      0x06
#define ELS_CMD_RCS       0x07
#define ELS_CMD_RES       0x08
#define ELS_CMD_RSS       0x09
#define ELS_CMD_RSI       0x0A
#define ELS_CMD_ESTS      0x0B
#define ELS_CMD_ESTC      0x0C
#define ELS_CMD_ADVC      0x0D
#define ELS_CMD_RTV       0x0E
#define ELS_CMD_RLS       0x0F
#define ELS_CMD_ECHO      0x10
#define ELS_CMD_TEST      0x11
#define ELS_CMD_RRQ       0x12
#define ELS_CMD_PRLI      0x14001020
#define ELS_CMD_PRLO      0x14001021
#define ELS_CMD_PDISC     0x50
#define ELS_CMD_FDISC     0x51
#define ELS_CMD_ADISC     0x52
#define ELS_CMD_FARP      0x54
#define ELS_CMD_FARPR     0x55
#define ELS_CMD_FAN       0x60
#define ELS_CMD_RSCN      0x0461
#define ELS_CMD_SCR       0x62
#define ELS_CMD_RNID      0x78
#endif

/*
 *  LS_RJT Payload Definition
 */

typedef struct _LS_RJT {	/* Structure is in Big Endian format */
	union {
		uint32_t lsRjtError;
		struct {
			uint8_t lsRjtRsvd0;	/* FC Word 0, bit 24:31 */

			uint8_t lsRjtRsnCode;	/* FC Word 0, bit 16:23 */
			/* LS_RJT reason codes */
#define LSRJT_INVALID_CMD     0x01
#define LSRJT_LOGICAL_ERR     0x03
#define LSRJT_LOGICAL_BSY     0x05
#define LSRJT_PROTOCOL_ERR    0x07
#define LSRJT_UNABLE_TPC      0x09	/* Unable to perform command */
#define LSRJT_CMD_UNSUPPORTED 0x0B
#define LSRJT_VENDOR_UNIQUE   0xFF	/* See Byte 3 */

			uint8_t lsRjtRsnCodeExp;	/* FC Word 0, bit  8:15 */
			/* LS_RJT reason explanation */
#define LSEXP_NOTHING_MORE      0x00
#define LSEXP_SPARM_OPTIONS     0x01
#define LSEXP_SPARM_ICTL        0x03
#define LSEXP_SPARM_RCTL        0x05
#define LSEXP_SPARM_RCV_SIZE    0x07
#define LSEXP_SPARM_CONCUR_SEQ  0x09
#define LSEXP_SPARM_CREDIT      0x0B
#define LSEXP_INVALID_PNAME     0x0D
#define LSEXP_INVALID_NNAME     0x0E
#define LSEXP_INVALID_CSP       0x0F
#define LSEXP_INVALID_ASSOC_HDR 0x11
#define LSEXP_ASSOC_HDR_REQ     0x13
#define LSEXP_INVALID_O_SID     0x15
#define LSEXP_INVALID_OX_RX     0x17
#define LSEXP_CMD_IN_PROGRESS   0x19
#define LSEXP_INVALID_NPORT_ID  0x1F
#define LSEXP_INVALID_SEQ_ID    0x21
#define LSEXP_INVALID_XCHG      0x23
#define LSEXP_INACTIVE_XCHG     0x25
#define LSEXP_RQ_REQUIRED       0x27
#define LSEXP_OUT_OF_RESOURCE   0x29
#define LSEXP_CANT_GIVE_DATA    0x2A
#define LSEXP_REQ_UNSUPPORTED   0x2C
			uint8_t vendorUnique;	/* FC Word 0, bit  0: 7 */
		} b;
	} un;
} LS_RJT;

/*
 *  N_Port Login (FLOGO/PLOGO Request) Payload Definition
 */

typedef struct _LOGO {		/* Structure is in Big Endian format */
	union {
		uint32_t nPortId32;	/* Access nPortId as a word */
		struct {
			uint8_t word1Reserved1;	/* FC Word 1, bit 31:24 */
			uint8_t nPortIdByte0;	/* N_port  ID bit 16:23 */
			uint8_t nPortIdByte1;	/* N_port  ID bit  8:15 */
			uint8_t nPortIdByte2;	/* N_port  ID bit  0: 7 */
		} b;
	} un;
	NAME_TYPE portName;	/* N_port name field */
} LOGO;

/*
 *  FCP Login (PRLI Request / ACC) Payload Definition
 */

#define PRLX_PAGE_LEN   0x10
#define TPRLO_PAGE_LEN  0x14

typedef struct _PRLI {		/* Structure is in Big Endian format */
	uint8_t prliType;	/* FC Parm Word 0, bit 24:31 */

#define PRLI_FCP_TYPE 0x08
	uint8_t word0Reserved1;	/* FC Parm Word 0, bit 16:23 */

#if BIG_ENDIAN_HW
	uint8_t origProcAssocV:1;	/* FC Parm Word 0, bit 15 */
	uint8_t respProcAssocV:1;	/* FC Parm Word 0, bit 14 */
	uint8_t estabImagePair:1;	/* FC Parm Word 0, bit 13 */

	/*    ACC = imagePairEstablished */
	uint8_t word0Reserved2:1;	/* FC Parm Word 0, bit 12 */
	uint8_t acceptRspCode:4;	/* FC Parm Word 0, bit 8:11, ACC ONLY */
#endif
#if LITTLE_ENDIAN_HW
	uint8_t acceptRspCode:4;	/* FC Parm Word 0, bit 8:11, ACC ONLY */
	uint8_t word0Reserved2:1;	/* FC Parm Word 0, bit 12 */
	uint8_t estabImagePair:1;	/* FC Parm Word 0, bit 13 */
	uint8_t respProcAssocV:1;	/* FC Parm Word 0, bit 14 */
	uint8_t origProcAssocV:1;	/* FC Parm Word 0, bit 15 */
	/*    ACC = imagePairEstablished */
#endif
#define PRLI_REQ_EXECUTED     0x1	/* acceptRspCode */
#define PRLI_NO_RESOURCES     0x2
#define PRLI_INIT_INCOMPLETE  0x3
#define PRLI_NO_SUCH_PA       0x4
#define PRLI_PREDEF_CONFIG    0x5
#define PRLI_PARTIAL_SUCCESS  0x6
#define PRLI_INVALID_PAGE_CNT 0x7
	uint8_t word0Reserved3;	/* FC Parm Word 0, bit 0:7 */

	uint32_t origProcAssoc;	/* FC Parm Word 1, bit 0:31 */

	uint32_t respProcAssoc;	/* FC Parm Word 2, bit 0:31 */

	uint8_t word3Reserved1;	/* FC Parm Word 3, bit 24:31 */
	uint8_t word3Reserved2;	/* FC Parm Word 3, bit 16:23 */
#if BIG_ENDIAN_HW
	uint16_t Word3bit15Resved:1;	/* FC Parm Word 3, bit 15 */
	uint16_t Word3bit14Resved:1;	/* FC Parm Word 3, bit 14 */
	uint16_t Word3bit13Resved:1;	/* FC Parm Word 3, bit 13 */
	uint16_t Word3bit12Resved:1;	/* FC Parm Word 3, bit 12 */
	uint16_t Word3bit11Resved:1;	/* FC Parm Word 3, bit 11 */
	uint16_t Word3bit10Resved:1;	/* FC Parm Word 3, bit 10 */
	uint16_t TaskRetryIdReq:1;	/* FC Parm Word 3, bit  9 */
	uint16_t Retry:1;	/* FC Parm Word 3, bit  8 */
	uint16_t ConfmComplAllowed:1;	/* FC Parm Word 3, bit  7 */
	uint16_t dataOverLay:1;	/* FC Parm Word 3, bit  6 */
	uint16_t initiatorFunc:1;	/* FC Parm Word 3, bit  5 */
	uint16_t targetFunc:1;	/* FC Parm Word 3, bit  4 */
	uint16_t cmdDataMixEna:1;	/* FC Parm Word 3, bit  3 */
	uint16_t dataRspMixEna:1;	/* FC Parm Word 3, bit  2 */
	uint16_t readXferRdyDis:1;	/* FC Parm Word 3, bit  1 */
	uint16_t writeXferRdyDis:1;	/* FC Parm Word 3, bit  0 */
#endif
#if LITTLE_ENDIAN_HW
	uint16_t Retry:1;	/* FC Parm Word 3, bit  8 */
	uint16_t TaskRetryIdReq:1;	/* FC Parm Word 3, bit  9 */
	uint16_t Word3bit10Resved:1;	/* FC Parm Word 3, bit 10 */
	uint16_t Word3bit11Resved:1;	/* FC Parm Word 3, bit 11 */
	uint16_t Word3bit12Resved:1;	/* FC Parm Word 3, bit 12 */
	uint16_t Word3bit13Resved:1;	/* FC Parm Word 3, bit 13 */
	uint16_t Word3bit14Resved:1;	/* FC Parm Word 3, bit 14 */
	uint16_t Word3bit15Resved:1;	/* FC Parm Word 3, bit 15 */
	uint16_t writeXferRdyDis:1;	/* FC Parm Word 3, bit  0 */
	uint16_t readXferRdyDis:1;	/* FC Parm Word 3, bit  1 */
	uint16_t dataRspMixEna:1;	/* FC Parm Word 3, bit  2 */
	uint16_t cmdDataMixEna:1;	/* FC Parm Word 3, bit  3 */
	uint16_t targetFunc:1;	/* FC Parm Word 3, bit  4 */
	uint16_t initiatorFunc:1;	/* FC Parm Word 3, bit  5 */
	uint16_t dataOverLay:1;	/* FC Parm Word 3, bit  6 */
	uint16_t ConfmComplAllowed:1;	/* FC Parm Word 3, bit  7 */
#endif
} PRLI;

/*
 *  FCP Logout (PRLO Request / ACC) Payload Definition
 */

typedef struct _PRLO {		/* Structure is in Big Endian format */
	uint8_t prloType;	/* FC Parm Word 0, bit 24:31 */

#define PRLO_FCP_TYPE  0x08
	uint8_t word0Reserved1;	/* FC Parm Word 0, bit 16:23 */

#if BIG_ENDIAN_HW
	uint8_t origProcAssocV:1;	/* FC Parm Word 0, bit 15 */
	uint8_t respProcAssocV:1;	/* FC Parm Word 0, bit 14 */
	uint8_t word0Reserved2:2;	/* FC Parm Word 0, bit 12:13 */
	uint8_t acceptRspCode:4;	/* FC Parm Word 0, bit 8:11, ACC ONLY */
#endif
#if LITTLE_ENDIAN_HW
	uint8_t acceptRspCode:4;	/* FC Parm Word 0, bit 8:11, ACC ONLY */
	uint8_t word0Reserved2:2;	/* FC Parm Word 0, bit 12:13 */
	uint8_t respProcAssocV:1;	/* FC Parm Word 0, bit 14 */
	uint8_t origProcAssocV:1;	/* FC Parm Word 0, bit 15 */
#endif
#define PRLO_REQ_EXECUTED     0x1	/* acceptRspCode */
#define PRLO_NO_SUCH_IMAGE    0x4
#define PRLO_INVALID_PAGE_CNT 0x7

	uint8_t word0Reserved3;	/* FC Parm Word 0, bit 0:7 */

	uint32_t origProcAssoc;	/* FC Parm Word 1, bit 0:31 */

	uint32_t respProcAssoc;	/* FC Parm Word 2, bit 0:31 */

	uint32_t word3Reserved1;	/* FC Parm Word 3, bit 0:31 */
} PRLO;

typedef struct _ADISC {		/* Structure is in Big Endian format */
	uint32_t hardAL_PA;
	NAME_TYPE portName;
	NAME_TYPE nodeName;
	uint32_t DID;
} ADISC;

typedef struct _FARP {		/* Structure is in Big Endian format */
	uint32_t Mflags:8;
	uint32_t Odid:24;
#define FARP_NO_ACTION          0	/* FARP information enclosed, no action */
#define FARP_MATCH_PORT         0x1	/* Match on Responder Port Name */
#define FARP_MATCH_NODE         0x2	/* Match on Responder Node Name */
#define FARP_MATCH_IP           0x4	/* Match on IP address, not supported */
#define FARP_MATCH_IPV4         0x5	/* Match on IPV4 address, not supported */
#define FARP_MATCH_IPV6         0x6	/* Match on IPV6 address, not supported */
	uint32_t Rflags:8;
	uint32_t Rdid:24;
#define FARP_REQUEST_PLOGI      0x1	/* Request for PLOGI */
#define FARP_REQUEST_FARPR      0x2	/* Request for FARP Response */
	NAME_TYPE OportName;
	NAME_TYPE OnodeName;
	NAME_TYPE RportName;
	NAME_TYPE RnodeName;
	uint8_t Oipaddr[16];
	uint8_t Ripaddr[16];
} FARP;

typedef struct _FAN {		/* Structure is in Big Endian format */
	uint32_t Fdid;
	NAME_TYPE FportName;
	NAME_TYPE FnodeName;
} FAN;

typedef struct _SCR {		/* Structure is in Big Endian format */
	uint8_t resvd1;
	uint8_t resvd2;
	uint8_t resvd3;
	uint8_t Function;
#define  SCR_FUNC_FABRIC     0x01
#define  SCR_FUNC_NPORT      0x02
#define  SCR_FUNC_FULL       0x03
#define  SCR_CLEAR           0xff
} SCR;

typedef struct _RNID_TOP_DISC {
	NAME_TYPE portName;
	uint8_t resvd[8];
	uint32_t unitType;
#define RNID_HBA            0x7
#define RNID_HOST           0xa
#define RNID_DRIVER         0xd
	uint32_t physPort;
	uint32_t attachedNodes;
	uint16_t ipVersion;
#define RNID_IPV4           0x1
#define RNID_IPV6           0x2
	uint16_t UDPport;
	uint8_t ipAddr[16];
	uint16_t resvd1;
	uint16_t flags;
#define RNID_TD_SUPPORT     0x1
#define RNID_LP_VALID       0x2
} RNID_TOP_DISC;

typedef struct _RNID {		/* Structure is in Big Endian format */
	uint8_t Format;
#define RNID_TOPOLOGY_DISC  0xdf
	uint8_t CommonLen;
	uint8_t resvd1;
	uint8_t SpecificLen;
	NAME_TYPE portName;
	NAME_TYPE nodeName;
	union {
		RNID_TOP_DISC topologyDisc;	/* topology disc (0xdf) */
	} un;
} RNID;

typedef struct _RRQ {		/* Structure is in Big Endian format */
	uint32_t SID;
	uint16_t Oxid;
	uint16_t Rxid;
	uint8_t resv[32];	/* optional association hdr */
} RRQ;

/* This is used for RSCN command */
typedef struct _D_ID {		/* Structure is in Big Endian format */
	union {
		uint32_t word;
		struct {
#if BIG_ENDIAN_HW
			uint8_t resv;
			uint8_t domain;
			uint8_t area;
			uint8_t id;
#endif
#if LITTLE_ENDIAN_HW
			uint8_t id;
			uint8_t area;
			uint8_t domain;
			uint8_t resv;
#endif
		} b;
	} un;
} D_ID;

/*
 *  Structure to define all ELS Payload types
 */

typedef struct _ELS_PKT {	/* Structure is in Big Endian format */
	uint8_t elsCode;	/* FC Word 0, bit 24:31 */
	uint8_t elsByte1;
	uint8_t elsByte2;
	uint8_t elsByte3;
	union {
		LS_RJT lsRjt;	/* Payload for LS_RJT ELS response */
		SERV_PARM logi;	/* Payload for PLOGI/FLOGI/PDISC/ACC */
		LOGO logo;	/* Payload for PLOGO/FLOGO/ACC */
		PRLI prli;	/* Payload for PRLI/ACC */
		PRLO prlo;	/* Payload for PRLO/ACC */
		ADISC adisc;	/* Payload for ADISC/ACC */
		FARP farp;	/* Payload for FARP/ACC */
		FAN fan;	/* Payload for FAN */
		SCR scr;	/* Payload for SCR/ACC */
		RRQ rrq;	/* Payload for RRQ */
		RNID rnid;	/* Payload for RNID */
		uint8_t pad[128 - 4];	/* Pad out to payload of 128 bytes */
	} un;
} ELS_PKT;

/*
 * FDMI
 * HBA MAnagement Operations Command Codes
 */
#define  SLI_MGMT_GRHL     0x100	/* Get registered HBA list */
#define  SLI_MGMT_GHAT     0x101	/* Get HBA attributes */
#define  SLI_MGMT_GRPL     0x102	/* Get registered Port list */
#define  SLI_MGMT_GPAT     0x110	/* Get Port attributes */
#define  SLI_MGMT_RHBA     0x200	/* Register HBA */
#define  SLI_MGMT_RHAT     0x201	/* Register HBA atttributes */
#define  SLI_MGMT_RPRT     0x210	/* Register Port */
#define  SLI_MGMT_RPA      0x211	/* Register Port attributes */
#define  SLI_MGMT_DHBA     0x300	/* De-register HBA */
#define  SLI_MGMT_DPRT     0x310	/* De-register Port */

/*
 * Management Service Subtypes
 */
#define  SLI_CT_FDMI_Subtypes     0x10

/*
 * HBA Management Service Reject Code
 */
#define  REJECT_CODE             0x9	/* Unable to perform command request */

/*
 * HBA Management Service Reject Reason Code
 * Please refer to the Reason Codes above
 */

/*
 * HBA Attribute Types
 */
#define  NODE_NAME               0x1
#define  MANUFACTURER            0x2
#define  SERIAL_NUMBER           0x3
#define  MODEL                   0x4
#define  MODEL_DESCRIPTION       0x5
#define  HARDWARE_VERSION        0x6
#define  DRIVER_VERSION          0x7
#define  OPTION_ROM_VERSION      0x8
#define  FIRMWARE_VERSION        0x9
#define  OS_NAME_VERSION	 0xa
#define  MAX_CT_PAYLOAD_LEN	 0xb

/*
 * Port Attrubute Types
 */
#define  SUPPORTED_FC4_TYPES     0x1
#define  SUPPORTED_SPEED         0x2
#define  PORT_SPEED              0x3
#define  MAX_FRAME_SIZE          0x4
#define  OS_DEVICE_NAME          0x5
#define  HOST_NAME               0x6

union AttributesDef {
	/* Structure is in Big Endian format */
	struct {
		uint32_t AttrType:16;
		uint32_t AttrLen:16;
	} bits;
	uint32_t word;
};

#define GET_OS_VERSION          1
#define GET_HOST_NAME           2

/*
 * HBA Attribute Entry (8 - 260 bytes)
 */
typedef struct {
	union AttributesDef ad;
	union {
		uint32_t VendorSpecific;
		uint8_t Manufacturer[64];
		uint8_t SerialNumber[64];
		uint8_t Model[256];
		uint8_t ModelDescription[256];
		uint8_t HardwareVersion[256];
		uint8_t DriverVersion[256];
		uint8_t OptionROMVersion[256];
		uint8_t FirmwareVersion[256];
		NAME_TYPE NodeName;
		uint8_t SupportFC4Types[32];
		uint32_t SupportSpeed;
		uint32_t PortSpeed;
		uint32_t MaxFrameSize;
		uint8_t OsDeviceName[256];
		uint8_t OsNameVersion[256];
		uint32_t MaxCTPayloadLen;
		uint8_t HostName[256];
	} un;
} ATTRIBUTE_ENTRY, *PATTRIBUTE_ENTRY;

/*
 * HBA Attribute Block
 */
typedef struct {
	uint32_t EntryCnt;	/* Number of HBA attribute entries */
	ATTRIBUTE_ENTRY Entry;	/* Variable-length array */
} ATTRIBUTE_BLOCK, *PATTRIBUTE_BLOCK;

/*
 * Port Entry
 */
typedef struct {
	NAME_TYPE PortName;
} PORT_ENTRY, *PPORT_ENTRY;

/*
 * HBA Identifier
 */
typedef struct {
	NAME_TYPE PortName;
} HBA_IDENTIFIER, *PHBA_IDENTIFIER;

/*
 * Registered Port List Format
 */
typedef struct {
	uint32_t EntryCnt;
	PORT_ENTRY pe;		/* Variable-length array */
} REG_PORT_LIST, *PREG_PORT_LIST;

/*
 * Register HBA(RHBA)
 */
typedef struct {
	HBA_IDENTIFIER hi;
	REG_PORT_LIST rpl;	/* variable-length array */
/* ATTRIBUTE_BLOCK   ab; */
} REG_HBA, *PREG_HBA;

/*
 * Register HBA Attributes (RHAT)
 */
typedef struct {
	NAME_TYPE HBA_PortName;
	ATTRIBUTE_BLOCK ab;
} REG_HBA_ATTRIBUTE, *PREG_HBA_ATTRIBUTE;

/*
 * Register Port Attributes (RPA)
 */
typedef struct {
	NAME_TYPE PortName;
	ATTRIBUTE_BLOCK ab;
} REG_PORT_ATTRIBUTE, *PREG_PORT_ATTRIBUTE;

/*
 * Get Registered HBA List (GRHL) Accept Payload Format
 */
typedef struct {
	uint32_t HBA__Entry_Cnt;	/* Number of Registered HBA Identifiers */
	NAME_TYPE HBA_PortName;	/* Variable-length array */
} GRHL_ACC_PAYLOAD, *PGRHL_ACC_PAYLOAD;

/*
 * Get Registered Port List (GRPL) Accept Payload Format
 */
typedef struct {
	uint32_t RPL_Entry_Cnt;	/* Number of Registered Port Entries */
	PORT_ENTRY Reg_Port_Entry[1];	/* Variable-length array */
} GRPL_ACC_PAYLOAD, *PGRPL_ACC_PAYLOAD;

/*
 * Get Port Attributes (GPAT) Accept Payload Format
 */

typedef struct {
	ATTRIBUTE_BLOCK pab;
} GPAT_ACC_PAYLOAD, *PGPAT_ACC_PAYLOAD;

#endif				/* _H_LPFC_HW */
