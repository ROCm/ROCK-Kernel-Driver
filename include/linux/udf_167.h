#if !defined(_LINUX_UDF_167_H)
#define _LINUX_UDF_167_H
/*
 * udf_167.h
 *
 * DESCRIPTION
 *	Definitions from the ECMA 167 standard.
 *	http://www.ecma.ch/
 *
 *	These abbreviations are used to keep the symbols short:
 *		Alloc	Allocation
 *		App	Application
 *		Attr	Attribute
 *		Char	Characters
 *		Desc	Descriptor
 *		Descs	Descriptors
 *		Ext	Extent
 *		Ident	Identifier
 *		Imp	Implementation
 *		Lvl	Level
 *		Max	Maximum
 *		Num	Number
 *		Ptr	Pointer
 *		Seq	Sequence
 *		Std	Standard
 *		Struct	Structure
 *		Vol	Volume
 *	The symbols are otherwise identical to the standard, and the
 *	sections of the standard to refer to are indicated.
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hootie.lvld.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 * HISTORY
 *	July 12, 1997 - Andrew E. Mileski
 *	Adapted from the ECMA-167 standard.
 *
 * 10/2/98 dgb	Adaptation
 * 10/4/98 	Changes by HJA Sandkuyl
 * 10/7/98	Changed FILE_EXISTENCE to FILE_HIDDEN, per UDF 2.0 spec
 * 11/26/98	Modifed some entries for UDF 1.5/2.0
 * 11/26/98 bf  Fixed typos, non-linux types, more structures
 * 12/5/98 dgb  Adjusted structure and content of include files.
 */

#ifdef __KERNEL__
#include <linux/types.h>
#define Uint8	__u8
#define Sint8	__s8
#define Uint16	__u16
#define Sint16	__s16
#define Uint32	__u32
#define Sint32	__s32
#define Uint64	__u64
#define Sint64	__s64
typedef Uint8	dstring;
#else
#define Uint8	unsigned char
#define Sint8	char
#define Uint16	unsigned short
#define Sint16	short
#define Uint32	unsigned int
#define Sint32	int
#define Uint64	unsigned long long
#define Sint64	long long
typedef Uint8	dstring;
#endif

/* make sure all structures are packed! */
#pragma pack(1)

/* CS0 Charspec (ECMA 167 1/7.2.1) */
typedef struct {
	Uint8 charSetType;
	Uint8 charSetInfo[63];
} charspec;

/* Timestamp (ECMA 167 1/7.3) */
typedef struct {
	Uint16 typeAndTimezone;
	Uint16 year;
	Uint8 month;
	Uint8 day;
	Uint8 hour;
	Uint8 minute;
	Uint8 second;
	Uint8 centiseconds;
	Uint8 hundredsOfMicroseconds;
	Uint8 microseconds;
} timestamp;

/* Timestamp types (ECMA 167 1/7.3.1) */
#define TIMESTAMP_TYPE_CUT		0x0000U
#define TIMESTAMP_TYPE_LOCAL		0x0001U
#define TIMESTAMP_TYPE_AGREEMENT	0x0002U

/* Entity Identifier (ECMA 167 1/7.4) */
typedef struct {
	Uint8 flags;
	Uint8 ident[23];
	Uint8 identSuffix[8];
} EntityID;
#define regid EntityID

/* Entity identifier flags (ECMA 167 1/7.4.1) */
#define ENTITYID_FLAGS_DIRTY		0x01U
#define ENTITYID_FLAGS_PROTECTED	0x02U

/* Volume Structure Descriptor (ECMA 167 2/9.1) */
#define STD_ID_LEN	5
struct VolStructDesc {
	Uint8 structType;
	Uint8 stdIdent[STD_ID_LEN];
	Uint8 structVersion;
	Uint8 structData[2041];
};

/* Std structure identifiers (ECMA 167 2/9.1.2) */
#define STD_ID_BEA01	"BEA01"
#define STD_ID_BOOT2	"BOOT2"
#define STD_ID_CD001	"CD001"
#define STD_ID_CDW02	"CDW02"
#define STD_ID_NSR02	"NSR02"
#define STD_ID_NSR03	"NSR03"
#define STD_ID_TEA01	"TEA01"

/* Beginning Extended Area Descriptor (ECMA 167 2/9.2) */
struct BeginningExtendedAreaDesc {
	Uint8 structType;
	Uint8 stdIdent[STD_ID_LEN];
	Uint8 structVersion;
	Uint8 structData[2041];
};

/* Terminating Extended Area Descriptor (ECMA 167 2/9.3) */
struct TerminatingExtendedAreaDesc {
	Uint8 structType;
	Uint8 stdIdent[STD_ID_LEN];
	Uint8 structVersion;
	Uint8 structData[2041];
};

/* Boot Descriptor (ECMA 167 2/9.4) */
struct BootDesc {
	Uint8 structType;
	Uint8 stdIdent[STD_ID_LEN];
	Uint8 structVersion;
	Uint8 reserved1;
	EntityID architectureType;
	EntityID bootIdent;
	Uint32 bootExtLocation;
	Uint32 bootExtLength;
	Uint64 loadAddress;
	Uint64 startAddress;
	timestamp descCreationDateAndTime;
	Uint16 flags;
	Uint8 reserved2[32];
	Uint8 bootUse[1906];
};

/* Boot flags (ECMA 167 2/9.4.12) */
#define BOOT_FLAGS_ERASE	1

/* Extent Descriptor (ECMA 167 3/7.1) */
typedef struct {
	Uint32 extLength;
	Uint32 extLocation;
} extent_ad;

/* Descriptor Tag (ECMA 167 3/7.2) */
typedef struct {
	Uint16 tagIdent;
	Uint16 descVersion;
	Uint8 tagChecksum;
	Uint8 reserved;
	Uint16 tagSerialNum;
	Uint16 descCRC;
	Uint16 descCRCLength;
	Uint32 tagLocation;
} tag;

/* Tag Identifiers (ECMA 167 3/7.2.1) */
#define TID_UNUSED_DESC			0x0000U
#define TID_PRIMARY_VOL_DESC		0x0001U
#define TID_ANCHOR_VOL_DESC_PTR		0x0002U
#define TID_VOL_DESC_PTR		0x0003U
#define TID_IMP_USE_VOL_DESC		0x0004U
#define TID_PARTITION_DESC		0x0005U
#define TID_LOGICAL_VOL_DESC		0x0006U
#define TID_UNALLOC_SPACE_DESC		0x0007U
#define TID_TERMINATING_DESC		0x0008U
#define TID_LOGICAL_VOL_INTEGRITY_DESC	0x0009U

/* Tag Identifiers (ECMA 167 4/7.2.1) */
#define TID_FILE_SET_DESC		0x0100U
#define TID_FILE_IDENT_DESC		0x0101U
#define TID_ALLOC_EXTENT_DESC		0x0102U
#define TID_INDIRECT_ENTRY		0x0103U
#define TID_TERMINAL_ENTRY		0x0104U
#define TID_FILE_ENTRY			0x0105U
#define TID_EXTENDED_ATTRE_HEADER_DESC	0x0106U
#define TID_UNALLOCATED_SPACE_ENTRY	0x0107U
#define TID_SPACE_BITMAP_DESC		0x0108U
#define TID_PARTITION_INTEGRITY_ENTRY	0x0109U
#define TID_EXTENDED_FILE_ENTRY		0x010AU

/* NSR Descriptor (ECMA 167 3/9.1) */
struct NSRDesc {
	Uint8 structType;
	Uint8 stdIdent[STD_ID_LEN];
	Uint8 structVersion;
	Uint8 reserved;
	Uint8 structData[2040];
};
	
/* Primary Volume Descriptor (ECMA 167 3/10.1) */
struct PrimaryVolDesc {
	tag descTag;
	Uint32 volDescSeqNum;
	Uint32 primaryVolDescNum;
	dstring volIdent[32];
	Uint16 volSeqNum;
	Uint16 maxVolSeqNum;
	Uint16 interchangeLvl;
	Uint16 maxInterchangeLvl;
	Uint32 charSetList;
	Uint32 maxCharSetList;
	dstring volSetIdent[128];
	charspec descCharSet;
	charspec explanatoryCharSet;
	extent_ad volAbstract;
	extent_ad volCopyright;
	EntityID appIdent;
	timestamp recordingDateAndTime;
	EntityID impIdent;
	Uint8 impUse[64];
	Uint32 predecessorVolDescSeqLocation;
	Uint16 flags;
	Uint8 reserved[22];
};

/* Primary volume descriptor flags (ECMA 167 3/10.1.21) */
#define VOL_SET_IDENT	1

/* Anchor Volume Descriptor Pointer (ECMA 167 3/10.2) */
struct AnchorVolDescPtr {
	tag descTag;
	extent_ad mainVolDescSeqExt;
	extent_ad reserveVolDescSeqExt;
	Uint8 reserved[480];
};

/* Volume Descriptor Pointer (ECMA 167 3/10.3) */
struct VolDescPtr {
	tag descTag;
	Uint32 volDescSeqNum;
	extent_ad nextVolDescSeqExt;
	Uint8 reserved[484];
};

/* Implementation Use Volume Descriptor (ECMA 167 3/10.4) */
struct ImpUseVolDesc {
	tag descTag;
	Uint32 volDescSeqNum;
	EntityID impIdent;
	Uint8 impUse[460];
};

/* Partition Descriptor (ECMA 167 3/10.5) */
struct PartitionDesc {
	tag descTag;
	Uint32 volDescSeqNum;
	Uint16 partitionFlags;
	Uint16 partitionNumber;
	EntityID partitionContents;
	Uint8 partitionContentsUse[128];
	Uint32 accessType;
	Uint32 partitionStartingLocation;
	Uint32 partitionLength;
	EntityID impIdent;
	Uint8 impUse[128];
	Uint8 reserved[156];
};

/* Partition Flags (ECMA 167 3/10.5.3) */
#define PARTITION_FLAGS_ALLOC	1

/* Partition Contents (ECMA 167 3/10.5.5) */
#define PARTITION_CONTENTS_FDC01	"+FDC01"
#define PARTITION_CONTENTS_CD001	"+CD001"
#define PARTITION_CONTENTS_CDW02	"+CDW02"
#define PARTITION_CONTENTS_NSR02	"+NSR02"
#define PARTITION_CONTENTS_NSR03	"+NSR03"

/* Partition Access Types (ECMA 167 3/10.5.7) */
#define PARTITION_ACCESS_NONE	0
#define PARTITION_ACCESS_R	1
#define PARTITION_ACCESS_WO	2
#define PARTITION_ACCESS_RW	3
#define PARTITION_ACCESS_OW	4

/* Logical Volume Descriptor (ECMA 167 3/10.6) */
struct LogicalVolDesc {
	tag descTag;
	Uint32 volDescSeqNum;
	charspec descCharSet;
	dstring logicalVolIdent[128];
	Uint32 logicalBlockSize;
	EntityID domainIdent;
	Uint8 logicalVolContentsUse[16]; /* used to find fileset */
	Uint32 mapTableLength;
	Uint32 numPartitionMaps;
	EntityID impIdent;
	Uint8 impUse[128];
	extent_ad integritySeqExt;
	Uint8 partitionMaps[0];
};

/* Generic Partition Map (ECMA 167 3/10.7.1) */
struct GenericPartitionMap {
	Uint8 partitionMapType;
	Uint8 partitionMapLength;
	Uint8 partitionMapping[0];
};

/* Partition Map Type (ECMA 167 3/10.7.1.1) */
#define PARTITION_MAP_TYPE_NONE		0
#define PARTITION_MAP_TYPE_1		1
#define PARTITION_MAP_TYPE_2		2

/* Type 1 Partition Map (ECMA 167 3/10.7.2) */
struct GenericPartitionMap1 {
	Uint8 partitionMapType;
	Uint8 partitionMapLength;
	Uint16 volSeqNum;
	Uint16 partitionNum;
};

/* Type 2 Partition Map (ECMA 167 3/10.7.3) */
struct GenericPartitionMap2 {
	Uint8 partitionMapType; /* 2 */
	Uint8 partitionMapLength; 
	Uint8 partitionIdent[62];
};

/* Unallocated Space Descriptor (ECMA 167 3/10.8) */
struct UnallocatedSpaceDesc {
	tag descTag;
	Uint32 volDescSeqNum;
	Uint32 numAllocDescs;
	extent_ad allocDescs[0];
};

/* Terminating Descriptor (ECMA 3/10.9) */
struct TerminatingDesc {
	tag descTag;
	Uint8 reserved[496];
};

struct GenericDesc
{
        tag descTag;
        Uint32 volDescSeqNum;
};

/* Logical Volume Integrity Descriptor (ECMA 167 3/10.10) */
struct LogicalVolIntegrityDesc {
	tag descTag;
	timestamp recordingDateAndTime;
	Uint32 integrityType;
	extent_ad nextIntegrityExt;
	Uint8 logicalVolContentsUse[32];
	Uint32 numOfPartitions;
	Uint32 lengthOfImpUse;
	Uint32 freeSpaceTable[0];
	Uint32 sizeTable[0];
	Uint8 impUse[0];
};

/* Integrity Types (ECMA 167 3/10.10.3) */
#define INTEGRITY_TYPE_OPEN	0
#define INTEGRITY_TYPE_CLOSE	1

/* Recorded Address (ECMA 167 4/7.1) */
typedef struct {
	Uint32 logicalBlockNum;
	Uint16 partitionReferenceNum;
} lb_addr;

/* Extent interpretation (ECMA 167 4/14.14.1.1) */
#define EXTENT_RECORDED_ALLOCATED               0x00
#define EXTENT_NOT_RECORDED_ALLOCATED           0x01
#define EXTENT_NOT_RECORDED_NOT_ALLOCATED       0x02
#define EXTENT_NEXT_EXTENT_ALLOCDECS            0x03

/* Long Allocation Descriptor (ECMA 167 4/14.14.2) */
typedef struct {
	Uint32 extLength;
	lb_addr extLocation;
	Uint8 impUse[6];
} long_ad;
	/* upper 2 bits of extLength indicate type */

/* File Set Descriptor (ECMA 167 4/14.1) */
struct FileSetDesc {
	tag descTag;
	timestamp recordingDateAndTime;
	Uint16 interchangeLvl;
	Uint16 maxInterchangeLvl;
	Uint32 charSetList;
	Uint32 maxCharSetList;
	Uint32 fileSetNum;
	Uint32 fileSetDescNum;
	charspec logicalVolIdentCharSet;
	dstring logicalVolIdent[128];
	charspec fileSetCharSet;
	dstring fileSetIdent[32];
	dstring copyrightFileIdent[32];
	dstring abstractFileIdent[32];
	long_ad rootDirectoryICB;
	EntityID domainIdent;
	long_ad nextExt;
	long_ad streamDirectoryICB;
	Uint8 reserved[32];
};

/* Short Allocation Descriptor (ECMA 167 4/14.14.1) */
typedef struct {
	Uint32 extLength;
	Uint32 extPosition;
} short_ad;

/* Partition Header Descriptor (ECMA 167 4/14.3) */
struct PartitionHeaderDesc {
	short_ad unallocatedSpaceTable;
	short_ad unallocatedSpaceBitmap;
	short_ad partitionIntegrityTable;
	short_ad freedSpaceTable;
	short_ad freedSpaceBitmap;
	Uint8 reserved[88];
};

/* File Identifier Descriptor (ECMA 167 4/14.4) */
struct FileIdentDesc
{
	tag descTag;
	Uint16 fileVersionNum; /* 1 */
	Uint8 fileCharacteristics;
	Uint8 lengthFileIdent;
	long_ad icb;
	Uint16 lengthOfImpUse;
	Uint8 impUse[0];
	Uint8 fileIdent[0];
	Uint8 padding[0];
};

/* File Characteristics (ECMA 167 4/14.4.3) */
#define FILE_HIDDEN	1
#define FILE_DIRECTORY	2
#define FILE_DELETED	4
#define FILE_PARENT	8
#define FILE_METADATA	0x10 /* UDF 2.0 */

/* Allocation Ext Descriptor (ECMA 167 4/14.5) */
struct AllocExtDesc
{
	tag descTag;
	Uint32 previousAllocExtLocation;
	Uint32 lengthAllocDescs;
};

/* ICB Tag (ECMA 167 4/14.6) */
typedef struct {
	Uint32 priorRecordedNumDirectEntries;
	Uint16 strategyType;
	Uint16 strategyParameter;
	Uint16 numEntries;
	Uint8 reserved;
	Uint8 fileType;
	lb_addr parentICBLocation;
	Uint16 flags;
} icbtag;

/* ICB File Type (ECMA 167 4/14.6.6) */
#define FILE_TYPE_NONE		0x00U
#define FILE_TYPE_UNALLOC	0x01U
#define FILE_TYPE_INTEGRITY	0x02U
#define FILE_TYPE_INDIRECT	0x03U
#define FILE_TYPE_DIRECTORY	0x04U
#define FILE_TYPE_REGULAR	0x05U
#define FILE_TYPE_BLOCK		0x06U
#define FILE_TYPE_CHAR		0x07U
#define FILE_TYPE_EXTENDED	0x08U
#define FILE_TYPE_FIFO		0x09U
#define FILE_TYPE_SOCKET	0x0aU
#define FILE_TYPE_TERMINAL	0x0bU
#define FILE_TYPE_SYMLINK	0x0cU
#define FILE_TYPE_STREAMDIR	0x0dU /* ECMA 167 4/13 */

/* ICB Flags (ECMA 167 4/14.6.8) */
#define ICB_FLAG_ALLOC_MASK	0x0007U
#define ICB_FLAG_SORTED		0x0008U
#define ICB_FLAG_NONRELOCATABLE	0x0010U
#define ICB_FLAG_ARCHIVE	0x0020U
#define ICB_FLAG_SETUID		0x0040U
#define ICB_FLAG_SETGID		0x0080U
#define ICB_FLAG_STICKY		0x0100U
#define ICB_FLAG_CONTIGUOUS	0x0200U
#define ICB_FLAG_SYSTEM		0x0400U
#define ICB_FLAG_TRANSFORMED	0x0800U
#define ICB_FLAG_MULTIVERSIONS	0x1000U

/* ICB Flags Allocation type(ECMA 167 4/14.6.8) */
#define ICB_FLAG_AD_SHORT	0
#define ICB_FLAG_AD_LONG	1
#define ICB_FLAG_AD_EXTENDED	2
#define ICB_FLAG_AD_IN_ICB	3

/* Indirect Entry (ECMA 167 4/14.7) */
struct IndirectEntry {
	tag descTag;
	icbtag icbTag;
	long_ad indirectICB;
};

/* Terminal Entry (ECMA 167 4/14.8) */
struct TerminalEntry {
	tag descTag;
	icbtag icbTag;
};

/* File Entry (ECMA 167 4/14.9) */
struct FileEntry {
	tag			descTag;
	icbtag		icbTag;
	Uint32		uid;
	Uint32		gid;
	Uint32		permissions;
	Uint16		fileLinkCount;
	Uint8		recordFormat;
	Uint8		recordDisplayAttr;
	Uint32		recordLength;
	Uint64		informationLength;
	Uint64		logicalBlocksRecorded;
	timestamp	accessTime;
	timestamp	modificationTime;
	timestamp	attrTime;
	Uint32		checkpoint;
	long_ad		extendedAttrICB;
	EntityID	impIdent;
	Uint64		uniqueID; /* 0= root, 16- (2^32-1) */
	Uint32		lengthExtendedAttr;
	Uint32		lengthAllocDescs;
	Uint8		extendedAttr[0];
	Uint8		allocDescs[0];
};

/* File Permissions (ECMA 167 4/14.9.5) */
#define PERM_O_EXEC	0x00000001U
#define PERM_O_WRITE	0x00000002U
#define PERM_O_READ	0x00000004U
#define PERM_O_CHATTR	0x00000008U
#define PERM_O_DELETE	0x00000010U
#define PERM_G_EXEC	0x00000020U
#define PERM_G_WRITE	0x00000040U
#define PERM_G_READ	0x00000080U
#define PERM_G_CHATTR	0x00000100U
#define PERM_G_DELETE	0x00000200U
#define PERM_U_EXEC	0x00000400U
#define PERM_U_WRITE	0x00000800U
#define PERM_U_READ	0x00001000U
#define PERM_U_CHATTR	0x00002000U
#define PERM_U_DELETE	0x00004000U

/* File Record Format (ECMA 167 4/14.9.7) */
#define RECORD_FMT_NONE			0
#define RECORD_FMT_FIXED_PAD		1
#define RECORD_FMT_FIXED		2
#define RECORD_FMT_VARIABLE8		3
#define RECORD_FMT_VARIABLE16		4
#define RECORD_FMT_VARIABLE16_MSB	5
#define RECORD_FMT_VARIABLE32		6
#define RECORD_FMT_PRINT		7
#define RECORD_FMT_LF			8
#define RECORD_FMT_CR			9
#define RECORD_FMT_CRLF			10
#define RECORD_FMT_LFCR			10

/* Extended Attribute Header Descriptor (ECMA 167 4/14.10.1) */
struct ExtendedAttrHeaderDesc {
	tag descTag;
	Uint32 impAttrLocation;
	Uint32 appAttrLocation;
};

/* Generic Attribute Format (ECMA 4/14.10.2) */
struct GenericAttrFormat {
	Uint32 attrType;
	Uint8 attrSubtype;
	Uint8 reserved[3];
	Uint32 attrLength;
	Uint8 attrData[0];
};

/* Character Set Attribute Format (ECMA 4/14.10.3) */
struct CharSetAttrFormat {
	Uint32 attrType;		/* 1 */
	Uint8 attrSubtype;		/* 1 */
	Uint8 reserved[3];
	Uint32 attrLength;
	Uint32 escapeSeqLength;
	Uint8 charSetType;
	Uint8 escapeSeq[0];
};

/* Alternate Permissions (ECMA 167 4/14.10.4) */
struct AlternatePermissionsExtendedAttr {
	Uint32 attrType;		/* 3 */
	Uint8 attrSubtype;		/* 1 */
	Uint8 reserved[3];
	Uint32 attrLength;
	Uint16 ownerIdent;
	Uint16 groupIdent;
	Uint16 permission;
};

/* File Times Extended Attribute (ECMA 167 4/14.10.5) */
struct FileTimesExtendedAttr {
	Uint32 attrType;		/* 5 */
	Uint8 attrSubtype;		/* 1 */
	Uint8 reserved[3];
	Uint32 attrLength;
	Uint32 dataLength;
	Uint32 fileTimeExistence;
	Uint8 fileTimes;
};

/* FileTimeExistence (ECMA 167 4/14.10.5.6) */
#define FTE_CREATION	0
#define FTE_DELETION	2
#define FTE_EFFECTIVE	3
#define FTE_BACKUP	5

/* Information Times Extended Attribute (ECMA 167 4/14.10.6) */
struct InfoTimesExtendedAttr {
	Uint32 attrType;		/* 6 */
	Uint8 attrSubtype;		/* 1 */
	Uint8 reserved[3];
	Uint32 attrLength;
	Uint32 dataLength;
	Uint32 infoTimeExistence;
	Uint8 infoTimes[0];
};

/* Device Specification Extended Attribute (ECMA 167 4/14.10.7) */
struct DeviceSpecificationExtendedAttr {
	Uint32 attrType;		/* 12 */
	Uint8 attrSubtype;		/* 1 */
	Uint8 reserved[3];
	Uint32 attrLength;
	Uint32 impUseLength;
	Uint32 majorDeviceIdent;
	Uint32 minorDeviceIdent;
	Uint8 impUse[0];
};

/* Implementation Use Extended Attr (ECMA 167 4/14.10.8) */
struct ImpUseExtendedAttr {
	Uint32 attrType;		/* 2048 */
	Uint8 attrSubtype;		/* 1 */
	Uint8 reserved[3];
	Uint32 attrLength;
	Uint32 impUseLength;
	EntityID impIdent;
	Uint8 impUse[0];
};

/* Application Use Extended Attribute (ECMA 167 4/14.10.9) */
struct AppUseExtendedAttr {
	Uint32 attrType;		/* 65536 */
	Uint8 attrSubtype;		/* 1 */
	Uint8 reserved[3];
	Uint32 attrLength;
	Uint32 appUseLength;
	EntityID appIdent;
	Uint8 appUse[0];
};

#define EXTATTR_CHAR_SET	1
#define EXTATTR_ALT_PERMS	3
#define EXTATTR_FILE_TIMES	5
#define EXTATTR_INFO_TIMES	6
#define EXTATTR_DEV_SPEC	12
#define EXTATTR_IMP_USE		2048
#define EXTATTR_APP_USE		65536


/* Unallocated Space Entry (ECMA 167 4/14.11) */
struct UnallocatedSpaceEntry {
	tag descTag;
	icbtag icbTag;
	Uint32 lengthAllocDescs;
	Uint8 allocDescs[0];
};

/* Space Bitmap Descriptor (ECMA 167 4/14.12) */
struct SpaceBitmapDesc {
	tag descTag;
	Uint32 numOfBits;
	Uint32 numOfBytes;
	Uint8 bitmap[0];
};

/* Partition Integrity Entry (ECMA 167 4/14.13) */
struct PartitionIntegrityEntry {
	tag descTag;
	icbtag icbTag;
	timestamp recordingDateAndTime;
	Uint8 integrityType;
	Uint8 reserved[175];
	EntityID impIdent;
	Uint8 impUse[256];
};

/* Extended Allocation Descriptor (ECMA 167 4/14.14.3) */
typedef struct { /* ECMA 167 4/14.14.3 */
	Uint32 extLength;
	Uint32 recordedLength;
	Uint32 informationLength;
	lb_addr extLocation;
} ext_ad;

/* Logical Volume Header Descriptor (ECMA 167 4/14.5) */
struct LogicalVolHeaderDesc {
	Uint64 uniqueID;
	Uint8 reserved[24];
};

/* Path Component (ECMA 167 4/14.16.1) */
struct PathComponent {
	Uint8 componentType;
	Uint8 lengthComponentIdent;
	Uint16 componentFileVersionNum;
	dstring componentIdent[0];
};

/* File Entry (ECMA 167 4/14.17) */
struct ExtendedFileEntry {
	tag			descTag;
	icbtag		icbTag;
	Uint32		uid;
	Uint32		gid;
	Uint32		permissions;
	Uint16		fileLinkCount;
	Uint8		recordFormat;
	Uint8		recordDisplayAttr;
	Uint32		recordLength;
	Uint64		informationLength;
	Uint64		objectSize;
	Uint64		logicalBlocksRecorded;
	timestamp	accessTime;
	timestamp	modificationTime;
	timestamp	createTime;
	timestamp	attrTime;
	Uint32		checkpoint;
	Uint32		reserved;
	long_ad		extendedAttrICB;
	long_ad		streamDirectoryICB;
	EntityID	impIdent;
	Uint64		uniqueID;
	Uint32		lengthExtendedAttr;
	Uint32		lengthAllocDescs;
	Uint8		extendedAttr[0];
	Uint8		allocDescs[0];
};
#pragma pack()

#endif /* !defined(_LINUX_UDF_167_H) */
