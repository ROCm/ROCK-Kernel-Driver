/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#ifndef _VXFS_SUPER_H_
#define _VXFS_SUPER_H_

#ident "$Id: vxfs.h 1.9 2001/04/24 19:28:36 hch Exp hch $"

/*
 * Veritas filesystem driver - superblock structure.
 *
 * This file contains the definition of the disk and core
 * superblocks of the Veritas Filesystem.
 */
#include <linux/types.h>


/*
 * Data types for use with the VxFS ondisk format.
 */
typedef	int32_t		vx_daddr_t;
typedef int32_t		vx_ino_t;

/*
 * Superblock magic number (vxfs_super->vs_magic).
 */
#define VXFS_SUPER_MAGIC	0xa501FCF5

/*
 * The root inode.
 */
#define VXFS_ROOT_INO		2

/*
 * Num of entries in free extent array
 */
#define VXFS_NEFREE		32

/*
 * VxFS superblock (disk).
 */
struct vxfs_sb {
	/*
	 * Version 1
	 */
	u_int32_t	vs_magic;		/* Magic, VXFS_MAGIC */
	int32_t		vs_version;		/* VxFS version, 1,2,3,4 */
	u_int32_t	vs_ctime;		/* create time - secs */
	u_int32_t	vs_cutime;		/* create time - usecs */
	int32_t		__unused1;		/* ??? */
	int32_t		__unused2;		/* ??? */
	vx_daddr_t	vs_old_logstart;	/* OLD: addr of first log blk */
	vx_daddr_t	vs_old_logend;		/* OLD: addr of last log blk */
	int32_t		vs_bsize;		/* Block size */
	int32_t		vs_size;		/* Number of blocks in FS */
	int32_t		vs_dsize;		/* Number of data blocks */
	u_int32_t	vs_old_ninode;		/* OLD: number of inodes */
	int32_t		vs_old_nau;		/* Number of allocation units */
	int32_t		__unused3;		/* ??? */
	int32_t		vs_old_defiextsize;	/* OLD: dfault indirect ext size */
	int32_t		vs_old_ilbsize;		/* OLD: Ilist block size in bytes */
	int32_t		vs_immedlen;		/* Size of immediate data area */
	int32_t		vs_ndaddr;		/* Number of direct extentes per inode */
	vx_daddr_t	vs_firstau;		/* Address of first Allocation Unit */
	vx_daddr_t	vs_emap;		/* Offset of extent map in AU */
	vx_daddr_t	vs_imap;		/* Offset of inode map in AU (V1) */
	vx_daddr_t	vs_iextop;		/* Offset of Ext. Op. map in AU */
	vx_daddr_t	vs_istart;		/* Offset of inode list in AU */
	vx_daddr_t	vs_bstart;		/* Offset of first data block in AU */
	vx_daddr_t	vs_femap;		/* aufirst + emap */
	vx_daddr_t	vs_fimap;		/* aufirst + imap */
	vx_daddr_t	vs_fiextop;		/* aufirst + iextop */
	vx_daddr_t	vs_fistart;		/* aufirst + istart */
	vx_daddr_t	vs_fbstart;		/* aufirst + bstart */
	int32_t		vs_nindir;		/* Number of entries in indirect */
	int32_t		vs_aulen;		/* Length of AU in blocks */
	int32_t		vs_auimlen;		/* Length of AU imap in blocks */
	int32_t		vs_auemlen;		/* Length of AU emap in blocks */
	int32_t		vs_auilen;		/* Length of AU ilist in blocks */
	int32_t		vs_aupad;		/* Length of AU pad in blocks */
	int32_t		vs_aublocks;		/* Number of data blocks in AU */
	int32_t		vs_maxtier;		/* Log base 2 of aublocks */
	int32_t		vs_inopb;		/* Number of inodes per blk */
	int32_t		vs_old_inopau;		/* OLD: Number of inodes per AU */
	int32_t		vs_old_inopilb;		/* OLD: Inodes per ilist blocks */
	int32_t		vs_old_ndiripau;	/* OLD: Num of directory inodes per au */
	int32_t		vs_iaddrlen;		/* Size of indirect addr ext. */
	int32_t		vs_bshift;		/* Log base 2 of bsize */
	int32_t		vs_inoshift;		/* Log base 2 of inobp */
	int32_t		vs_bmask;		/* ~( bsize - 1 ) */
	int32_t		vs_boffmask;		/* bsize - 1 */
	int32_t		vs_old_inomask;		/* old_inopilb - 1 */
	int32_t		vs_checksum;		/* Checksum of V1 data */
	
	/*
	 * Version 1, writable
	 */
	int32_t		vs_free;		/* Number of free blocks */
	int32_t		vs_ifree;		/* Number of free inodes */
	int32_t		vs_efree[VXFS_NEFREE];	/* Number of free extents by size */
	int32_t		vs_flags;		/* Flags ?!? */
	u_int8_t	vs_mod;			/* Filesystem has been changed */
	u_int8_t	vs_clean;		/* Clean FS */
	u_int16_t	__unused4;		/* ??? */
	u_int32_t	vs_firstlogid;		/* Mount time log ID */
	u_int32_t	vs_wtime;		/* Last time written - sec */
	u_int32_t	vs_wutime;		/* Last time written - usec */
	u_int8_t	vs_fname[6];		/* FS name */
	u_int8_t	vs_fpack[6];		/* FS pack name */
	int32_t		vs_logversion;		/* Log format version */
	int32_t		__unused5;		/* ??? */
	
	/*
	 * Version 2, Read-only
	 */
	vx_daddr_t	vs_oltext[2];		/* OLT extent and replica */
	int32_t		vs_oltsize;		/* OLT extent size */
	int32_t		vs_iauimlen;		/* Size of inode map */
	int32_t		vs_iausize;		/* Size of IAU in blocks */
	int32_t		vs_dinosize;		/* Size of inode in bytes */
	int32_t		vs_old_dniaddr;		/* OLD: Sum of indir levels per inode */
	int32_t		vs_checksum2;		/* Checksum of V2 RO */

	/*
	 * Actually much more...
	 */
};


/*
 * In core superblock filesystem private data for VxFS.
 */
struct vxfs_sb_info {
	struct vxfs_sb		*vsi_raw;	/* Raw (on disk) supeblock */
	struct buffer_head	*vsi_bp;	/* Buffer for raw superblock*/
	struct inode		*vsi_fship;	/* Fileset header inode */
	struct inode		*vsi_ilist;	/* Inode list inode */
	struct inode		*vsi_stilist;	/* Structual inode list inode */
	u_long			vsi_iext;	/* Initial inode list */
	ino_t			vsi_fshino;	/* Fileset header inode */
	daddr_t			vsi_oltext;	/* OLT extent */
	daddr_t			vsi_oltsize;	/* OLT size */
};


/*
 * File modes.  File types above 0xf000 are vxfs internal only, they should
 * not be passed back to higher levels of the system.  vxfs file types must
 * never have one of the regular file type bits set.
 */
enum vxfs_mode {
	VXFS_ISUID = 0x00000800,	/* setuid */
	VXFS_ISGID = 0x00000400,	/* setgid */
	VXFS_ISVTX = 0x00000200,	/* sticky bit */
	VXFS_IREAD = 0x00000100,	/* read */
	VXFS_IWRITE = 0x00000080,	/* write */
	VXFS_IEXEC = 0x00000040,	/* exec */

	VXFS_IFIFO = 0x00001000,	/* Named pipe */
	VXFS_IFCHR = 0x00002000,	/* Character device */
	VXFS_IFDIR = 0x00004000,	/* Directory */
	VXFS_IFNAM = 0x00005000,	/* Xenix device ?? */
	VXFS_IFBLK = 0x00006000,	/* Block device */
	VXFS_IFREG = 0x00008000,	/* Regular file */
	VXFS_IFCMP = 0x00009000,	/* Compressed file ?!? */
	VXFS_IFLNK = 0x0000a000,	/* Symlink */
	VXFS_IFSOC = 0x0000c000,	/* Socket */

	/* VxFS internal */
	VXFS_IFFSH = 0x10000000,	/* Fileset header */
	VXFS_IFILT = 0x20000000,	/* Inode list */
	VXFS_IFIAU = 0x30000000,	/* Inode allocation unit */
	VXFS_IFCUT = 0x40000000,	/* Current usage table */
	VXFS_IFATT = 0x50000000,	/* Attr. inode */
	VXFS_IFLCT = 0x60000000,	/* Link count table */
	VXFS_IFIAT = 0x70000000,	/* Indirect attribute file */
	VXFS_IFEMR = 0x80000000,	/* Extent map reorg file */
	VXFS_IFQUO = 0x90000000,	/* BSD quota file */
	VXFS_IFPTI = 0xa0000000,	/* "Pass through" inode */
	VXFS_IFLAB = 0x11000000,	/* Device label file */
	VXFS_IFOLT = 0x12000000,	/* OLT file */
	VXFS_IFLOG = 0x13000000,	/* Log file */
	VXFS_IFEMP = 0x14000000,	/* Extent map file */
	VXFS_IFEAU = 0x15000000,	/* Extent AU file */
	VXFS_IFAUS = 0x16000000,	/* Extent AU summary file */
	VXFS_IFDEV = 0x17000000,	/* Device config file */

};

#define	VXFS_TYPE_MASK		0xfffff000

#define VXFS_IS_TYPE(ip,type)	(((ip)->vii_mode & VXFS_TYPE_MASK) == (type))
#define VXFS_ISFIFO(x)		VXFS_IS_TYPE((x),VXFS_IFIFO)
#define VXFS_ISCHR(x)		VXFS_IS_TYPE((x),VXFS_IFCHR)
#define VXFS_ISDIR(x)		VXFS_IS_TYPE((x),VXFS_IFDIR)
#define VXFS_ISNAM(x)		VXFS_IS_TYPE((x),VXFS_IFNAM)
#define VXFS_ISBLK(x)		VXFS_IS_TYPE((x),VXFS_IFBLK)
#define VXFS_ISLNK(x)		VXFS_IS_TYPE((x),VXFS_IFLNK)
#define VXFS_ISREG(x)		VXFS_IS_TYPE((x),VXFS_IFREG)
#define VXFS_ISCMP(x)		VXFS_IS_TYPE((x),VXFS_IFCMP)
#define VXFS_ISSOC(x)		VXFS_IS_TYPE((x),VXFS_IFSOC)

#define VXFS_ISFSH(x)		VXFS_IS_TYPE((x),VXFS_IFFSH)
#define VXFS_ISILT(x)		VXFS_IS_TYPE((x),VXFS_IFILT)

/*
 * Inmode organisation types.
 */
enum {
	VXFS_ORG_NONE	= 0,	/* Inode has *no* format ?!? */
	VXFS_ORG_EXT4	= 1,	/* Ext4 */
	VXFS_ORG_IMMED	= 2,	/* All data stored in inode */
	VXFS_ORG_TYPED	= 3,	/* Typed extents */
};

#define VXFS_IS_ORG(ip,org)	((ip)->vii_orgtype == (org))
#define VXFS_ISNONE(ip)		VXFS_IS_ORG((ip),VXFS_ORG_NONE)
#define VXFS_ISEXT4(ip)		VXFS_IS_ORG((ip),VXFS_ORG_EXT4)
#define VXFS_ISIMMED(ip)	VXFS_IS_ORG((ip),VXFS_ORG_IMMED)
#define VXFS_ISTYPED(ip)	VXFS_IS_ORG((ip),VXFS_ORG_TYPED)


/*
 * Get filesystem private data from VFS inode.
 */
#define VXFS_INO(ip) \
	((struct vxfs_inode_info *)(ip)->u.generic_ip)

/*
 * Get filesystem private data from VFS superblock.
 */
#define VXFS_SBI(sbp) \
	((struct vxfs_sb_info *)(sbp)->u.generic_sbp)

#endif /* _VXFS_SUPER_H_ */
