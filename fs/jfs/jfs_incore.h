/*
 *   Copyright (c) International Business Machines Corp., 2000-2002
 *   Portions Copyright (c) Christoph Hellwig, 2001-2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */ 
#ifndef _H_JFS_INCORE
#define _H_JFS_INCORE

#include <linux/slab.h>
#include <asm/bitops.h>
#include "jfs_types.h"
#include "jfs_xtree.h"
#include "jfs_dtree.h"

/*
 * JFS magic number
 */
#define JFS_SUPER_MAGIC 0x3153464a /* "JFS1" */

/*
 * Due to header ordering problems this can't be in jfs_lock.h
 */
typedef struct	jfs_rwlock {
	struct rw_semaphore rw_sem;
	atomic_t in_use;	/* for hacked implementation of trylock */
} jfs_rwlock_t;

/*
 * JFS-private inode information
 */
struct jfs_inode_info {
	int	fileset;	/* fileset number (always 16)*/
	uint	mode2;		/* jfs-specific mode		*/
	pxd_t   ixpxd;		/* inode extent descriptor	*/
	dxd_t	acl;		/* dxd describing acl	*/
	dxd_t	ea;		/* dxd describing ea	*/
	time_t	otime;		/* time created	*/
	uint	next_index;	/* next available directory entry index */
	int	acltype;	/* Type of ACL	*/
	short	btorder;	/* access order	*/
	short	btindex;	/* btpage entry index*/
	struct inode *ipimap;	/* inode map			*/
	long	cflag;		/* commit flags		*/
	u16	bxflag;		/* xflag of pseudo buffer?	*/
	unchar	agno;		/* ag number			*/
	unchar	pad;		/* pad			*/
	lid_t	blid;		/* lid of pseudo buffer?	*/
	lid_t	atlhead;	/* anonymous tlock list head	*/
	lid_t	atltail;	/* anonymous tlock list tail	*/
	struct list_head anon_inode_list; /* inodes having anonymous txns */
	struct list_head mp_list; /* metapages in inode's address space */
	jfs_rwlock_t rdwrlock;	/* read/write lock	*/
	lid_t	xtlid;		/* lid of xtree lock on directory */
	union {
		struct {
			xtpage_t _xtroot;	/* 288: xtree root */
			struct inomap *_imap;	/* 4: inode map header	*/
		} file;
		struct {
			dir_table_slot_t _table[12]; /* 96: directory index */
			dtroot_t _dtroot;	/* 288: dtree root */
		} dir;
		struct {
			unchar _unused[16];	/* 16: */
			dxd_t _dxd;		/* 16: */
			unchar _inline[128];	/* 128: inline symlink */
		} link;
	} u;
	struct inode	vfs_inode;
};
#define i_xtroot u.file._xtroot
#define i_imap u.file._imap
#define i_dirtable u.dir._table
#define i_dtroot u.dir._dtroot
#define i_inline u.link._inline

/*
 * cflag
 */
enum cflags {
	COMMIT_New,		/* never committed inode   */
	COMMIT_Nolink,		/* inode committed with zero link count */
	COMMIT_Inlineea,	/* commit inode inline EA */
	COMMIT_Freewmap,	/* free WMAP at iClose() */
	COMMIT_Dirty,		/* Inode is really dirty */
	COMMIT_Holdlock,	/* Hold the IWRITE_LOCK until commit is done */
	COMMIT_Dirtable,	/* commit changes to di_dirtable */
	COMMIT_Stale,		/* data extent is no longer valid */
	COMMIT_Synclist,	/* metadata pages on group commit synclist */
};

#define set_cflag(flag, ip)	set_bit(flag, &(JFS_IP(ip)->cflag))
#define clear_cflag(flag, ip)	clear_bit(flag, &(JFS_IP(ip)->cflag))
#define test_cflag(flag, ip)	test_bit(flag, &(JFS_IP(ip)->cflag))
#define test_and_clear_cflag(flag, ip) \
	test_and_clear_bit(flag, &(JFS_IP(ip)->cflag))
/*
 * JFS-private superblock information.
 */
struct jfs_sb_info {
	unsigned long	mntflag;	/* 4: aggregate attributes	*/
	struct inode	*ipbmap;	/* 4: block map inode		*/
	struct inode	*ipaimap;	/* 4: aggregate inode map inode	*/
	struct inode	*ipaimap2;	/* 4: secondary aimap inode	*/
	struct inode	*ipimap;	/* 4: aggregate inode map inode	*/
	struct jfs_log	*log;		/* 4: log			*/
	short		bsize;		/* 2: logical block size	*/
	short		l2bsize;	/* 2: log2 logical block size	*/
	short		nbperpage;	/* 2: blocks per page		*/
	short		l2nbperpage;	/* 2: log2 blocks per page	*/
	short		l2niperblk;	/* 2: log2 inodes per page	*/
	u32		logdev;		/* 2: external log device	*/
	uint		aggregate;	/* volume identifier in log record */
	pxd_t		logpxd;		/* 8: pxd describing log	*/
	pxd_t		ait2;		/* 8: pxd describing AIT copy	*/
	char		uuid[16];	/* 16: 128-bit uuid for volume	*/
	char		loguuid[16];	/* 16: 128-bit uuid for log	*/
	/* Formerly in ipimap */
	uint		gengen;		/* 4: inode generation generator*/
	uint		inostamp;	/* 4: shows inode belongs to fileset*/

        /* Formerly in ipbmap */
	struct bmap	*bmap;		/* 4: incore bmap descriptor	*/
	struct nls_table *nls_tab;	/* 4: current codepage		*/
	struct inode	*direct_inode;	/* 4: inode for physical I/O	*/
	struct address_space *direct_mapping; /* 4: mapping for physical I/O */
	uint		state;		/* 4: mount/recovery state	*/
};

static inline struct jfs_inode_info *JFS_IP(struct inode *inode)
{
	return list_entry(inode, struct jfs_inode_info, vfs_inode);
}

static inline struct jfs_sb_info *JFS_SBI(struct super_block *sb)
{
	return sb->u.generic_sbp;
}

static inline int isReadOnly(struct inode *inode)
{
	if (JFS_SBI(inode->i_sb)->log)
		return 0;
	return 1;
}

#endif /* _H_JFS_INCORE */
