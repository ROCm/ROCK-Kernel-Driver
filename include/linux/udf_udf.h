#if !defined(_LINUX_UDF_UDF_H)
#define _LINUX_UDF_UDF_H
/*
 * udf_udf.h
 *
 * PURPOSE
 *	OSTA-UDF(tm) format specification [based on ECMA 167 standard].
 *	http://www.osta.org/
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hpesjro.fc.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 *
 * 10/2/98 dgb	changed UDF_ID_DEVELOPER
 * 11/26/98 bf  changed UDF_ID_DEVELOPER, 
 * 12/5/98 dgb  updated include file hierarchy, more UDF definitions
 */

/* based on ECMA 167 structure definitions */
#include <linux/udf_167.h>

#pragma pack(1)

/* -------- Basic types and constants ----------- */
/* UDF character set (UDF 1.50 2.1.2) */
#define UDF_CHAR_SET_TYPE	0
#define UDF_CHAR_SET_INFO	"OSTA Compressed Unicode"

#define UDF_ID_DEVELOPER	"*Linux UDFFS"
 
/* UDF 1.02 2.2.6.4 */
struct LogicalVolIntegrityDescImpUse
{
	EntityID	impIdent;
	Uint32		numFiles;
	Uint32		numDirs;
	Uint16		minUDFReadRev;
	Uint16		minUDFWriteRev;
	Uint16		maxUDFWriteRev;
};

/* UDF 1.02 2.2.7.2 */
/* LVInformation may be present in ImpUseVolDesc.impUse */
struct ImpUseVolDescImpUse
{
	charspec	LVICharset;
	dstring		logicalVolIdent[128];
	dstring		LVInfo1[36];
	dstring		LVInfo2[36];
	dstring		LVInfo3[36];
	EntityID	impIdent;
	Uint8		impUse[128];
};

struct UdfPartitionMap2
{
        Uint8           partitionMapType;
        Uint8           partitionMapLength;
        Uint8           reserved1[2];
        EntityID        partIdent;
        Uint16          volSeqNum;
        Uint16          partitionNum;
        Uint8           reserved2[24];
};

/* UDF 1.5 2.2.8 */
struct VirtualPartitionMap
{
	Uint8		partitionMapType;	/* 2 */
	Uint8		partitionMapLength;	/* 64 */
	Uint8		reserved1[2];		/* #00 */
	EntityID	partIdent;
	Uint16		volSeqNum;
	Uint16		partitionNum;
	Uint8		reserved2[24];		/* #00 */
};

/* UDF 1.5 2.2.9 */
struct SparablePartitionMap
{
	Uint8		partitionMapType;	/* 2 */
	Uint8		partitionMapLength;	/* 64 */
	Uint8		reserved1[2];		/* #00 */
	EntityID	partIdent;		/* Flags = 0 */
						/* Id = UDF_ID_SPARABLE */
						/* IdSuf = 2.1.5.3 */
	Uint16		volSeqNum;
	Uint16		partitionNum;
	Uint16		packetLength;		/* 32 */
	Uint8		numSparingTables;
	Uint8		reserved2[1];		/* #00 */
	Uint32		sizeSparingTable;
	Uint32		locSparingTable[4];
};
 
/* DVD Copyright Management Info, see UDF 1.02 3.3.4.5.1.2 */
/* when ImpUseExtendedAttr.impIdent= "*UDF DVD CGMS Info" */
struct DVDCopyrightImpUse {
	Uint16 headerChecksum;
	Uint8  CGMSInfo;
	Uint8  dataType;
	Uint8  protectionSystemInfo[4];
};

/* the impUse of long_ad used in AllocDescs  - UDF 1.02 2.3.10.1 */
struct ADImpUse
{
	Uint16 flags;
	Uint8  impUse[4];
};

/* UDF 1.02 2.3.10.1 */
#define UDF_EXTENT_LENGTH_MASK		0x3FFFFFFF
#define UDF_EXTENT_FLAG_MASK		0xc0000000
#define UDF_EXTENT_FLAG_ERASED		0x40000000

/* 
 * Important!  VirtualAllocationTables are 
 * very different between 1.5 and 2.0!
 */

/* ----------- 1.5 ------------- */
/* UDF 1.5 2.2.10 */
#define FILE_TYPE_VAT15		0x0U

/* UDF 1.5 2.2.10 - VAT layout: */
struct VirutalAllocationTable15 {
	Uint32 VirtualSector[0];
	EntityID	ident;
	Uint32	previousVATICB;
   };  
/* where number of VirtualSector's is (VATSize-36)/4 */

/* ----------- 2.0 ------------- */
/* UDF 2.0 2.2.10 */
#define FILE_TYPE_VAT20		0xf8U

/* UDF 2.0 2.2.10 (different from 1.5!) */
struct VirtualAllocationTable20 {
	Uint16 lengthHeader;
	Uint16 lengthImpUse;
	dstring logicalVolIdent[128];
	Uint32	previousVatICBLoc;
	Uint32  numFIDSFiles;
	Uint32  numFIDSDirectories; /* non-parent */
	Uint16  minReadRevision;
	Uint16	minWriteRevision;
	Uint16  maxWriteRevision;
	Uint16  reserved;
	Uint8	impUse[0];
	Uint32  vatEntry[0];
};

/* ----------- 2.01 ------------- */
/* UDF 2.01 6.11 */
#define FILE_TYPE_REALTIME	0xf9U

/* Sparing maps, see UDF 1.5 2.2.11 */
typedef struct {
	Uint32  origLocation;
	Uint32  mappedLocation;
} SparingEntry;

/* sparing maps, see UDF 2.0 2.2.11 */
struct SparingTable {
	tag 	descTag;
	EntityID sparingIdent; /* *UDF Sparing Table */
	Uint16   reallocationTableLen;
	Uint16   reserved;	/* #00 */
	Uint32   sequenceNum;
	SparingEntry mapEntry[0];
};

/* Entity Identifiers (UDF 1.50 6.1) */
#define	UDF_ID_COMPLIANT	"*OSTA UDF Compliant"
#define UDF_ID_LV_INFO		"*UDF LV Info"
#define UDF_ID_FREE_EA		"*UDF FreeEASpace"
#define UDF_ID_FREE_APP_EA	"*UDF FreeAppEASpace"
#define UDF_ID_DVD_CGMS		"*UDF DVD CGMS Info"
#define UDF_ID_OS2_EA		"*UDF OS/2 EA"
#define UDF_ID_OS2_EA_LENGTH	"*UDF OS/2 EALength"
#define UDF_ID_MAC_VOLUME	"*UDF Mac VolumeInfo"
#define UDF_ID_MAC_FINDER	"*UDF Mac FinderInfo"
#define UDF_ID_MAC_UNIQUE	"*UDF Mac UniqueIDTable"
#define UDF_ID_MAC_RESOURCE	"*UDF Mac ResourceFork"
#define UDF_ID_VIRTUAL		"*UDF Virtual Partition"
#define UDF_ID_SPARABLE		"*UDF Sparable Partition"
#define UDF_ID_ALLOC		"*UDF Virtual Alloc Tbl"
#define UDF_ID_SPARING		"*UDF Sparing Table"

/* Operating System Identifiers (UDF 1.50 6.3) */
#define UDF_OS_CLASS_UNDEF	0x00U
#define UDF_OS_CLASS_DOS	0x01U
#define UDF_OS_CLASS_OS2	0x02U
#define UDF_OS_CLASS_MAC	0x03U
#define UDF_OS_CLASS_UNIX	0x04U
#define UDF_OS_CLASS_WIN95	0x05U
#define UDF_OS_CLASS_WINNT	0x06U
#define UDF_OS_ID_UNDEF		0x00U
#define UDF_OS_ID_DOS		0x00U
#define UDF_OS_ID_OS2		0x00U
#define UDF_OS_ID_MAC		0x00U
#define UDF_OS_ID_UNIX		0x00U
#define UDF_OS_ID_WIN95		0x00U
#define UDF_OS_ID_WINNT		0x00U
#define UDF_OS_ID_AIX		0x01U
#define UDF_OS_ID_SOLARIS	0x02U
#define UDF_OS_ID_HPUX		0x03U
#define UDF_OS_ID_IRIX		0x04U
#define UDF_OS_ID_LINUX		0x05U
#define UDF_OS_ID_MKLINUX	0x06U
#define UDF_OS_ID_FREEBSD	0x07U

#define UDF_NAME_PAD	4
#define UDF_NAME_LEN	255
#define UDF_PATH_LEN	1023

#pragma pack()

#endif /* !defined(_LINUX_UDF_FMT_H) */
