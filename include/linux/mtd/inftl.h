/*
 *	inftl.h -- defines to support the Inverse NAND Flash Translation Layer
 *
 *	(C) Copyright 2002, Greg Ungerer (gerg@snapgear.com)
 *
 *	$Id: inftl.h,v 1.3 2003/05/23 11:35:34 dwmw2 Exp $
 */

#ifndef __MTD_INFTL_H__
#define __MTD_INFTL_H__

#include <linux/mtd/blktrans.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nftl.h>

#define	OSAK_VERSION	0x5120
#define	PERCENTUSED	98

#define	SECTORSIZE	512

#ifndef INFTL_MAJOR
#define INFTL_MAJOR 93 /* FIXME */
#endif
#define INFTL_PARTN_BITS 4

/* Block Control Information */

struct inftl_bci {
	__u8 ECCsig[6];
	__u8 Status;
	__u8 Status1;
} __attribute__((packed));

struct inftl_unithead1 {
	__u16 virtualUnitNo;
	__u16 prevUnitNo;
	__u8 ANAC;
	__u8 NACs;
	__u8 parityPerField;
	__u8 discarded;
} __attribute__((packed));

struct inftl_unithead2 {
	__u8 parityPerField;
	__u8 ANAC;
	__u16 prevUnitNo;
	__u16 virtualUnitNo;
	__u8 NACs;
	__u8 discarded;
} __attribute__((packed));

struct inftl_unittail {
	__u8 Reserved[4];
	__u16 EraseMark;
	__u16 EraseMark1;
} __attribute__((packed));

union inftl_uci {
	struct inftl_unithead1 a;
	struct inftl_unithead2 b;
	struct inftl_unittail c;
};

struct inftl_oob {
	struct inftl_bci b;
	union inftl_uci u;
};


/* INFTL Media Header */

struct INFTLPartition {
	__u32 virtualUnits;
	__u32 firstUnit;
	__u32 lastUnit;
	__u32 flags;
	__u32 spareUnits;
	__u32 Reserved0;
	__u32 Reserved1;
} __attribute__((packed));

struct INFTLMediaHeader {
	char bootRecordID[8];
	__u32 NoOfBootImageBlocks;
	__u32 NoOfBinaryPartitions;
	__u32 NoOfBDTLPartitions;
	__u32 BlockMultiplierBits;
	__u32 FormatFlags;
	__u32 OsakVersion;
	__u32 PercentUsed;
	struct INFTLPartition Partitions[4];
} __attribute__((packed));

/* Partition flag types */
#define	INFTL_BINARY	0x20000000
#define	INFTL_BDTL	0x40000000
#define	INFTL_LAST	0x80000000


#ifdef __KERNEL__

struct INFTLrecord {
	struct mtd_blktrans_dev mbd;
	__u16 MediaUnit, SpareMediaUnit;
	__u32 EraseSize;
	struct INFTLMediaHeader MediaHdr;
	int usecount;
	unsigned char heads;
	unsigned char sectors;
	unsigned short cylinders;
	__u16 numvunits;
	__u16 firstEUN;
	__u16 lastEUN;
	__u16 numfreeEUNs;
	__u16 LastFreeEUN; 		/* To speed up finding a free EUN */
	int head,sect,cyl;
	__u16 *PUtable;	 		/* Physical Unit Table  */
	__u16 *VUtable; 		/* Virtual Unit Table */
        unsigned int nb_blocks;		/* number of physical blocks */
        unsigned int nb_boot_blocks;	/* number of blocks used by the bios */
        struct erase_info instr;
};

int INFTL_mount(struct INFTLrecord *s);
int INFTL_formatblock(struct INFTLrecord *s, int block);

#endif /* __KERNEL__ */

#endif /* __MTD_INFTL_H__ */
