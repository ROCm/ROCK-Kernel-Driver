/*
 *  linux/fs/hfs/hfs.h
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 */

#ifndef _HFS_H
#define _HFS_H

/* offsets to various blocks */
#define HFS_DD_BLK		0 /* Driver Descriptor block */
#define HFS_PMAP_BLK		1 /* First block of partition map */
#define HFS_MDB_BLK		2 /* Block (w/i partition) of MDB */

/* magic numbers for various disk blocks */
#define HFS_DRVR_DESC_MAGIC	0x4552 /* "ER": driver descriptor map */
#define HFS_OLD_PMAP_MAGIC	0x5453 /* "TS": old-type partition map */
#define HFS_NEW_PMAP_MAGIC	0x504D /* "PM": new-type partition map */
#define HFS_SUPER_MAGIC		0x4244 /* "BD": HFS MDB (super block) */
#define HFS_MFS_SUPER_MAGIC	0xD2D7 /* MFS MDB (super block) */

/* various FIXED size parameters */
#define HFS_SECTOR_SIZE		512    /* size of an HFS sector */
#define HFS_SECTOR_SIZE_BITS	9      /* log_2(HFS_SECTOR_SIZE) */
#define HFS_NAMELEN		31     /* maximum length of an HFS filename */
#define HFS_MAX_VALENCE		32767U

/* Meanings of the drAtrb field of the MDB,
 * Reference: _Inside Macintosh: Files_ p. 2-61
 */
#define HFS_SB_ATTRIB_HLOCK	(1 << 7)
#define HFS_SB_ATTRIB_UNMNT	(1 << 8)
#define HFS_SB_ATTRIB_SPARED	(1 << 9)
#define HFS_SB_ATTRIB_INCNSTNT	(1 << 11)
#define HFS_SB_ATTRIB_SLOCK	(1 << 15)

/* Some special File ID numbers */
#define HFS_POR_CNID		1	/* Parent Of the Root */
#define HFS_ROOT_CNID		2	/* ROOT directory */
#define HFS_EXT_CNID		3	/* EXTents B-tree */
#define HFS_CAT_CNID		4	/* CATalog B-tree */
#define HFS_BAD_CNID		5	/* BAD blocks file */
#define HFS_ALLOC_CNID		6	/* ALLOCation file (HFS+) */
#define HFS_START_CNID		7	/* STARTup file (HFS+) */
#define HFS_ATTR_CNID		8	/* ATTRibutes file (HFS+) */
#define HFS_EXCH_CNID		15	/* ExchangeFiles temp id */
#define HFS_FIRSTUSER_CNID	16

/* values for hfs_cat_rec.cdrType */
#define HFS_CDR_DIR    0x01    /* folder (directory) */
#define HFS_CDR_FIL    0x02    /* file */
#define HFS_CDR_THD    0x03    /* folder (directory) thread */
#define HFS_CDR_FTH    0x04    /* file thread */

/* legal values for hfs_ext_key.FkType and hfs_file.fork */
#define HFS_FK_DATA	0x00
#define HFS_FK_RSRC	0xFF

/* bits in hfs_fil_entry.Flags */
#define HFS_FIL_LOCK	0x01  /* locked */
#define HFS_FIL_THD	0x02  /* file thread */
#define HFS_FIL_DOPEN   0x04  /* data fork open */
#define HFS_FIL_ROPEN   0x08  /* resource fork open */
#define HFS_FIL_DIR     0x10  /* directory (always clear) */
#define HFS_FIL_NOCOPY  0x40  /* copy-protected file */
#define HFS_FIL_USED	0x80  /* open */

/* bits in hfs_dir_entry.Flags. dirflags is 16 bits. */
#define HFS_DIR_LOCK        0x01  /* locked */
#define HFS_DIR_THD         0x02  /* directory thread */
#define HFS_DIR_INEXPFOLDER 0x04  /* in a shared area */
#define HFS_DIR_MOUNTED     0x08  /* mounted */
#define HFS_DIR_DIR         0x10  /* directory (always set) */
#define HFS_DIR_EXPFOLDER   0x20  /* share point */

/* bits hfs_finfo.fdFlags */
#define HFS_FLG_INITED		0x0100
#define HFS_FLG_LOCKED		0x1000
#define HFS_FLG_INVISIBLE	0x4000

/*======== HFS structures as they appear on the disk ========*/

#define __packed __attribute__ ((packed))

/* Pascal-style string of up to 31 characters */
struct hfs_name {
	u8 len;
	u8 name[HFS_NAMELEN];
} __packed;

struct hfs_point {
	u16 v;
	u16 h;
} __packed;

struct hfs_rect {
	u16 top;
	u16 left;
	u16 bottom;
	u16 right;
} __packed;

struct hfs_finfo {
	u32 fdType;
	u32 fdCreator;
	u16 fdFlags;
	struct hfs_point fdLocation;
	u16 fdFldr;
} __packed;

struct hfs_fxinfo {
	u16 fdIconID;
	u8 fdUnused[8];
	u16 fdComment;
	u32 fdPutAway;
} __packed;

struct hfs_dinfo {
	struct hfs_rect frRect;
	u16 frFlags;
	struct hfs_point frLocation;
	u16 frView;
} __packed;

struct hfs_dxinfo {
	struct hfs_point frScroll;
	u32 frOpenChain;
	u16 frUnused;
	u16 frComment;
	u32 frPutAway;
} __packed;

union hfs_finder_info {
	struct {
		struct hfs_finfo finfo;
		struct hfs_fxinfo fxinfo;
	} file;
	struct {
		struct hfs_dinfo dinfo;
		struct hfs_dxinfo dxinfo;
	} dir;
} __packed;

/* Cast to a pointer to a generic bkey */
#define	HFS_BKEY(X)	(((void)((X)->KeyLen)), ((struct hfs_bkey *)(X)))

/* The key used in the catalog b-tree: */
struct hfs_cat_key {
	u8 key_len;		/* number of bytes in the key */
	u8 reserved;		/* padding */
	u32 ParID;		/* CNID of the parent dir */
	struct hfs_name	CName;	/* The filename of the entry */
} __packed;

/* The key used in the extents b-tree: */
struct hfs_ext_key {
	u8 key_len;		/* number of bytes in the key */
	u8 FkType;		/* HFS_FK_{DATA,RSRC} */
	u32 FNum;		/* The File ID of the file */
	u16 FABN;		/* allocation blocks number*/
} __packed;

typedef union hfs_btree_key {
	u8 key_len;			/* number of bytes in the key */
	struct hfs_cat_key cat;
	struct hfs_ext_key ext;
} hfs_btree_key;

typedef union hfs_btree_key btree_key;

struct hfs_extent {
	u16 block;
	u16 count;
};
typedef struct hfs_extent hfs_extent_rec[3];

/* The catalog record for a file */
struct hfs_cat_file {
	s8 type;			/* The type of entry */
	u8 reserved;
	u8 Flags;			/* Flags such as read-only */
	s8 Typ;				/* file version number = 0 */
	struct hfs_finfo UsrWds;	/* data used by the Finder */
	u32 FlNum;			/* The CNID */
	u16 StBlk;			/* obsolete */
	u32 LgLen;			/* The logical EOF of the data fork*/
	u32 PyLen;			/* The physical EOF of the data fork */
	u16 RStBlk;			/* obsolete */
	u32 RLgLen;			/* The logical EOF of the rsrc fork */
	u32 RPyLen;			/* The physical EOF of the rsrc fork */
	u32 CrDat;			/* The creation date */
	u32 MdDat;			/* The modified date */
	u32 BkDat;			/* The last backup date */
	struct hfs_fxinfo FndrInfo;	/* more data for the Finder */
	u16 ClpSize;			/* number of bytes to allocate
					   when extending files */
	hfs_extent_rec ExtRec;		/* first extent record
					   for the data fork */
	hfs_extent_rec RExtRec;		/* first extent record
					   for the resource fork */
	u32 Resrv;			/* reserved by Apple */
} __packed;

/* the catalog record for a directory */
struct hfs_cat_dir {
	s8 type;			/* The type of entry */
	u8 reserved;
	u16 Flags;			/* flags */
	u16 Val;			/* Valence: number of files and
					   dirs in the directory */
	u32 DirID;			/* The CNID */
	u32 CrDat;			/* The creation date */
	u32 MdDat;			/* The modification date */
	u32 BkDat;			/* The last backup date */
	struct hfs_dinfo UsrInfo;	/* data used by the Finder */
	struct hfs_dxinfo FndrInfo;	/* more data used by Finder */
	u8 Resrv[16];			/* reserved by Apple */
} __packed;

/* the catalog record for a thread */
struct hfs_cat_thread {
	s8 type;			/* The type of entry */
	u8 reserved[9];			/* reserved by Apple */
	u32 ParID;			/* CNID of parent directory */
	struct hfs_name CName;		/* The name of this entry */
}  __packed;

/* A catalog tree record */
typedef union hfs_cat_rec {
	s8 type;			/* The type of entry */
	struct hfs_cat_file file;
	struct hfs_cat_dir dir;
	struct hfs_cat_thread thread;
} hfs_cat_rec;

struct hfs_mdb {
	u16 drSigWord;			/* Signature word indicating fs type */
	u32 drCrDate;			/* fs creation date/time */
	u32 drLsMod;			/* fs modification date/time */
	u16 drAtrb;			/* fs attributes */
	u16 drNmFls;			/* number of files in root directory */
	u16 drVBMSt;			/* location (in 512-byte blocks)
					   of the volume bitmap */
	u16 drAllocPtr;			/* location (in allocation blocks)
					   to begin next allocation search */
	u16 drNmAlBlks;			/* number of allocation blocks */
	u32 drAlBlkSiz;			/* bytes in an allocation block */
	u32 drClpSiz;			/* clumpsize, the number of bytes to
					   allocate when extending a file */
	u16 drAlBlSt;			/* location (in 512-byte blocks)
					   of the first allocation block */
	u32 drNxtCNID;			/* CNID to assign to the next
					   file or directory created */
	u16 drFreeBks;			/* number of free allocation blocks */
	u8 drVN[28];			/* the volume label */
	u32 drVolBkUp;			/* fs backup date/time */
	u16 drVSeqNum;			/* backup sequence number */
	u32 drWrCnt;			/* fs write count */
	u32 drXTClpSiz;			/* clumpsize for the extents B-tree */
	u32 drCTClpSiz;			/* clumpsize for the catalog B-tree */
	u16 drNmRtDirs;			/* number of directories in
					   the root directory */
	u32 drFilCnt;			/* number of files in the fs */
	u32 drDirCnt;			/* number of directories in the fs */
	u8 drFndrInfo[32];		/* data used by the Finder */
	u16 drEmbedSigWord;		/* embedded volume signature */
	u32 drEmbedExtent;		/* starting block number (xdrStABN)
					   and number of allocation blocks
					   (xdrNumABlks) occupied by embedded
					   volume */
	u32 drXTFlSize;			/* bytes in the extents B-tree */
	hfs_extent_rec drXTExtRec;	/* extents B-tree's first 3 extents */
	u32 drCTFlSize;			/* bytes in the catalog B-tree */
	hfs_extent_rec drCTExtRec;	/* catalog B-tree's first 3 extents */
} __packed;

/*======== Data structures kept in memory ========*/

struct hfs_readdir_data {
	struct list_head list;
	struct file *file;
	struct hfs_cat_key key;
};

#endif
