/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

/*
 * File Name: inioct.h
 *
 * San/Device Management Ioctl Header
 * File is created to adhere to Solaris requirement using 8-space tabs.
 *
 * !!!!! PLEASE DO NOT REMOVE THE TABS !!!!!
 * !!!!! PLEASE NO SINGLE LINE COMMENTS: // !!!!!
 * !!!!! PLEASE NO MORE THAN 80 CHARS PER LINE !!!!!
 *
 *
 * Revision History:
 *
 * Rev. 0	June 15, 2001
 * YPL	- Created.
 *
 * Rev. 1	June 26, 2001
 * YPL	- Change the loop back structure and delete cc that is not used.
 *
 * Rev. 2	June 29, 2001
 * YPL	- Use new EXT_CC defines from exioct.h
 *
 * Rev. 3	July 12, 2001
 * RL	- Added definitions for loopback mbx command completion codes.
 *
 * Rev. 4	July 12, 2001
 * RL	- Added definitions for loopback mbx command completion codes.
 *
 * Rev. 5	October 9, 2002
 * AV	- Added definition for Read Option ROM IOCTL.
 *
 * Rev. 6	May 27, 2003
 * RL	- Modified loopback rsp buffer structure definition to add
 *        diagnostic Echo command support.
 *
 */

#ifndef	_INIOCT_H
#define	_INIOCT_H

/*
 * ***********************************************************************
 * X OS type definitions
 * ***********************************************************************
 */
#ifdef _MSC_VER						/* NT */
#pragma pack(1)
#endif

/*
 * ***********************************************************************
 * INT_IOCTL SubCode definition.
 * These macros are being used for setting SubCode field in EXT_IOCTL
 * structure.
 * ***********************************************************************
 */

/*
 * Currently supported DeviceControl / ioctl command codes
 */
#define	INT_CC_GET_PORT_STAT_FC		EXT_CC_RESERVED0A_OS
#define	INT_CC_LOOPBACK			EXT_CC_RESERVED0B_OS
#define	INT_CC_UPDATE_OPTION_ROM	EXT_CC_RESERVED0C_OS
#define	INT_CC_ADD_TARGET_DEVICE	EXT_CC_RESERVED0D_OS
#define	INT_CC_READ_NVRAM		EXT_CC_RESERVED0E_OS
#define	INT_CC_UPDATE_NVRAM		EXT_CC_RESERVED0F_OS
#define	INT_CC_SWAP_TARGET_DEVICE	EXT_CC_RESERVED0G_OS
#define	INT_CC_READ_OPTION_ROM		EXT_CC_RESERVED0H_OS
#define	INT_CC_GET_OPTION_ROM_LAYOUT	EXT_CC_RESERVED0I_OS
#define	INT_CC_LEGACY_LOOPBACK		EXT_CC_RESERVED0Z_OS



/* NVRAM */
#define	INT_SC_NVRAM_HARDWARE		0	/* Save */
#define	INT_SC_NVRAM_DRIVER		1	/* Driver (Apply) */
#define	INT_SC_NVRAM_ALL		2	/* NVRAM/Driver (Save+Apply) */

/* Loopback */
typedef struct _INT_LOOPBACK_REQ
{
	UINT16 Options;				/* 2   */
	UINT32 TransferCount;			/* 4   */
	UINT32 IterationCount;			/* 4   */
	UINT64 BufferAddress;			/* 8  */
	UINT32 BufferLength;			/* 4  */
	UINT16 Reserved[9];			/* 18  */
} INT_LOOPBACK_REQ, *PINT_LOOPBACK_REQ;		/* 408 */

typedef struct _INT_LOOPBACK_RSP
{
	UINT64 BufferAddress;			/* 8  */
	UINT32 BufferLength;			/* 4  */
	UINT16 CompletionStatus;		/* 2  */
	UINT16 CrcErrorCount;			/* 2  */
	UINT16 DisparityErrorCount;		/* 2  */
	UINT16 FrameLengthErrorCount;		/* 2  */
	UINT32 IterationCountLastError;		/* 4  */
	UINT8  CommandSent;			/* 1  */
	UINT8  Reserved1;			/* 1  */
	UINT16 Reserved2[7];			/* 16 */
} INT_LOOPBACK_RSP, *PINT_LOOPBACK_RSP;		/* 40 */

/* definition for interpreting CompletionStatus values */
#define	INT_DEF_LB_COMPLETE	0x4000
#define INT_DEF_LB_ECHO_CMD_ERR 0x4005
#define	INT_DEF_LB_PARAM_ERR	0x4006
#define	INT_DEF_LB_LOOP_DOWN	0x400b
#define	INT_DEF_LB_CMD_ERROR	0x400c

/* definition for interpreting CommandSent field */
#define INT_DEF_LB_LOOPBACK_CMD 	0
#define INT_DEF_LB_ECHO_CMD		1

/* definition for option rom */
#define INT_OPT_ROM_REGION_NONE		0
#define INT_OPT_ROM_REGION_BIOS		1
#define INT_OPT_ROM_REGION_FCODE	2
#define INT_OPT_ROM_REGION_EFI		3
#define INT_OPT_ROM_REGION_VPD		4
#define INT_OPT_ROM_REGION_FW1		5
#define INT_OPT_ROM_REGION_FW2		6
#define INT_OPT_ROM_REGION_BOOT		7
#define INT_OPT_ROM_REGION_PCI_CFG	8
#define INT_OPT_ROM_REGION_ALL		0xF
#define INT_OPT_ROM_REGION_PHBIOS	0x11 //Bios with pci hdr
#define INT_OPT_ROM_REGION_PHFCODE  	0x12 //Fcode with pci hdr
#define INT_OPT_ROM_REGION_PHEFI  	0x13 //Efi with pci hdr
#define INT_OPT_ROM_REGION_PHVPD  	0x14 // Vpd with pci hdr
#define INT_OPT_ROM_REGION_PHEC_FW	0x15 // Efi compressed Fw with pci hdr
#define INT_OPT_ROM_REGION_BCFW	               0x16 // Bios compressed Fw
#define INT_OPT_ROM_REGION_INVALID	0xFFFFFFFF

// Image device id (PCI_DATA_STRUCTURE.DeviceId) 

#define INT_PDS_DID_VPD                  0x0001
#define INT_PDS_DID_ISP23XX_FW  0x0003

// Image code type (PCI_DATA_STRUCTURE.CodeType)

#define INT_PDS_CT_X86                      0x0000
#define INT_PDS_CT_PCI_OPEN_FW  0x0001
#define INT_PDS_CT_HP_PA_RISC     0x0002
#define INT_PDS_CT_EFI                      0x0003

// Last image indicator (PCI_DATA_STRUCTURE.Indicator)

#define INT_PDS_ID_LAST_IMAGE   0x80

typedef struct _INT_PCI_ROM_HEADER
{
    UINT16 Signature;       // 0xAA55
    UINT8  Reserved[0x16];
    UINT16 PcirOffset;      // Relative pointer to pci data structure

} INT_PCI_ROM_HEADER, *PINT_PCI_ROM_HEADER;
#define INT_PCI_ROM_HEADER_SIGNATURE            0xAA55

typedef struct _INT_PCI_DATA_STRUCT
{
    UINT32 Signature;       // 'PCIR'
    UINT16 VendorId;
    UINT16 DeviceId;        // Image type
    UINT16 Reserved0;
    UINT16 Length;          // Size of this structure
    UINT8  Revision;
    UINT8  ClassCode[3];
    UINT16 ImageLength;     // Total image size (512 byte segments)
    UINT16 CodeRevision;
    UINT8  CodeType;
    UINT8  Indicator;       // 0x80 indicates last this is image
    UINT16 Reserved1;
} INT_PCI_DATA_STRUCT, *PINT_PCI_DATA_STRUCT;
#define INT_PCI_DATA_STRUCT_SIGNATURE   0x52494350 //'R', 'I', 'C', 'P'

typedef struct _INT_LZHEADER
{
    UINT16 LzMagicNum;      // 'LZ'
    UINT16 Reserved1;
    UINT32 CompressedSize;
    UINT32 UnCompressedSize;
    struct 
    {
        UINT16 sub;
        UINT16 minor;
        UINT16 majorLo;
        UINT16 majorHi;     // Usually always zero
    } RiscFwRev;
    UINT8 Reserved2[12];
} INT_LZHEADER, *PINT_LZHEADER;
#define INT_PCI_FW_LZ_HEADER_SIGNATURE  0x5A4C //'Z', 'L'

typedef struct _INT_OPT_ROM_REGION
{
    UINT32  Region;
    UINT32  Size;
    UINT32  Beg;
    UINT32  End;
} INT_OPT_ROM_REGION, *PINT_OPT_ROM_REGION;

typedef struct _INT_OPT_ROM_LAYOUT
{
    UINT32      Size;			// Size of the option rom
    UINT32      NoOfRegions;
    INT_OPT_ROM_REGION	Region[1];
} INT_OPT_ROM_LAYOUT, *PINT_OPT_ROM_LAYOUT;

#define	OPT_ROM_MAX_REGIONS	0xF
#define OPT_ROM_SIZE_1      0x20000  // 128k
#define OPT_ROM_SIZE_2      0x100000 // M

typedef struct _OPT_ROM_TABLE
{
	INT_OPT_ROM_REGION  Region;
} OPT_ROM_TABLE, *POPT_ROM_TABLE;

#ifdef _MSC_VER
#pragma pack()
#endif

#endif /* _INIOCT_H */
