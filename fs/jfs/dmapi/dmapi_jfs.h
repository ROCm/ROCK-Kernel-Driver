/*
 *   Copyright (c) International Business Machines Corp., 2000-2004
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

#include "sv.h"
#include "spin.h"
#include "kmem.h"
#include <asm/uaccess.h>
#include <linux/file.h>
#include <linux/fs.h>

#ifndef __DMAPI_JFS_H__
#define __DMAPI_JFS_H__

typedef struct {
        __u32 val[2];                   /* file system id type */
} jfs_fsid_t;

#ifndef MAXFIDSZ
#define MAXFIDSZ        46
typedef struct fid {
        __u16		fid_len;                /* length of data in bytes */
        unsigned char   fid_data[MAXFIDSZ];     /* data (variable length)  */
} fid_t;
#endif

typedef struct jfs_fid {
        __u16	fid_len;        /* length of remainder */
        __u16	fid_pad;        /* padding, must be zero */
        __u32	fid_gen;        /* generation number, dm_igen_t */
        __u64	fid_ino;        /* inode number, dm_ino_t */
} jfs_fid_t;

typedef struct jfs_handle {
        union {
                __s64		align;      /* force alignment of ha_fid     */
                jfs_fsid_t	_ha_fsid;   /* unique file system identifier */
        } ha_u;
        jfs_fid_t		ha_fid;     /* file system specific file ID  */
} jfs_handle_t;
#define ha_fsid ha_u._ha_fsid

#define JFS_NAME "jfs"

/* __psint_t is the same size as a pointer */
#if (BITS_PER_LONG == 32)
typedef __s32 __psint_t;
typedef __u32 __psunsigned_t;
#elif (BITS_PER_LONG == 64)
typedef __s64 __psint_t;
typedef __u64 __psunsigned_t;
#else
#error BITS_PER_LONG must be 32 or 64
#endif

#define JFS_HSIZE(handle)       (((char *) &(handle).ha_fid.fid_pad  \
			       	 - (char *) &(handle))               \
		                 + (handle).ha_fid.fid_len)
	
#define JFS_HANDLE_CMP(h1, h2) memcmp(h1, h2, sizeof(jfs_handle_t))

#define FSHSIZE         sizeof(fsid_t)

#define FINVIS          0x0100  /* don't update timestamps */

#define IP_IS_JFS(ip)   (ip->i_sb->s_magic == 0x3153464a /*JFS_SUPER_MAGIC*/)

typedef struct dm_attrs_s {
        __u32	da_dmevmask;    /* DMIG event mask */
        __u16	da_dmstate;     /* DMIG state info */
        __u16	da_pad;         /* DMIG extra padding */
} dm_attrs_t;

int jfs_iget(
	struct super_block *sbp,
	struct inode	**ipp,
	fid_t		*fidp);

int jfs_dm_mount(struct super_block *sp);
int jfs_dm_preunmount(struct super_block *sp);
void jfs_dm_unmount(struct super_block *sp, int rc);
#endif
