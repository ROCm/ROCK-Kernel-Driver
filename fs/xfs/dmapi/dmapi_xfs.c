/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include <linux/version.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#include <linux/iobuf.h>
#endif

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_clnt.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_inode_item.h"
#include "dmapi.h"
#include "dmapi_kern.h"
#include "dmapi_private.h"

#define MAXNAMLEN MAXNAMELEN

#define XFS_BHV_LOOKUP(vp, xbdp)  \
	xbdp = vn_bhv_lookup(VN_BHV_HEAD(vp), &xfs_vnodeops); \
	ASSERT(xbdp);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define MAX_DIO_SIZE(mp)	(64 * PAGE_CACHE_SIZE)
#define XFS_TO_LINUX_DEVT(dev)	(MKDEV(sysv_major(dev) & 0x1ff, sysv_minor(dev)))
#else
#define MAX_DIO_SIZE(mp)	XFS_B_TO_FSBT((mp), KIO_MAX_ATOMIC_IO << 10)
#define XFS_TO_LINUX_DEVT(dev)	(kdev_t_to_nr(XFS_DEV_TO_KDEVT(dev)))

static inline int
open_private_file(struct file *file, struct dentry *dentry, int oflags)
{
	mode_t fmode = (oflags+1) & O_ACCMODE;
	int error = init_private_file(file, dentry, fmode);

	if (error == -EFBIG) {
		/* try again */
		file->f_flags = oflags;
		error = file->f_op->open(dentry->d_inode, file);
	}

	if (!error)
		 file->f_flags = oflags;
	return error;
}

static inline void
close_private_file(struct file *file)
{
	if (file->f_op->release)
		file->f_op->release(file->f_dentry->d_inode, file);
}
#endif

/* Structure used to hold the on-disk version of a dm_attrname_t.  All
   on-disk attribute names start with the 8-byte string "SGI_DMI_".
*/

typedef struct	{
	char	dan_chars[DMATTR_PREFIXLEN + DM_ATTR_NAME_SIZE + 1];
} dm_dkattrname_t;

/* Structure used by xfs_dm_get_bulkall(), used as the "private_data"
 * that we want xfs_bulkstat to send to our formatter.
 */
typedef struct {
	void		*laststruct;
	dm_dkattrname_t	attrname;
	int		bulkall;
} dm_bulkstat_one_t;

/* In the on-disk inode, DMAPI attribute names consist of the user-provided
   name with the DMATTR_PREFIXSTRING pre-pended.  This string must NEVER be
   changed!
*/

STATIC	const	char	dmattr_prefix[DMATTR_PREFIXLEN + 1] = DMATTR_PREFIXSTRING;

STATIC	dm_size_t  dm_min_dio_xfer = 0; /* direct I/O disabled for now */


/* See xfs_dm_get_dmattr() for a description of why this is needed. */

#define XFS_BUG_KLUDGE	256	/* max size of an in-inode attribute value */

#define DM_MAX_ATTR_BYTES_ON_DESTROY	256

#define DM_STAT_SIZE(dmtype,namelen)	\
	(sizeof(dmtype) + sizeof(xfs_handle_t) + namelen)
#define MAX_DIRENT_SIZE		(sizeof(dirent_t) + MAXNAMELEN)

#define DM_STAT_ALIGN		(sizeof(__uint64_t))

/* DMAPI's E2BIG == EA's ERANGE */
#define DM_EA_XLATE_ERR(err) { if (err == ERANGE) err = E2BIG; }

/*
 *	xfs_dm_send_data_event()
 *
 *	Send data event to DMAPI.  Drop IO lock (if specified) before
 *	the dm_send_data_event() call and reacquire it afterwards.
 */
int
xfs_dm_send_data_event(
	dm_eventtype_t	event,
	vnode_t		*vp,
	xfs_off_t	offset,
	size_t		length,
	int		flags,
	vrwlock_t	*locktype)
{
	int		error;
	bhv_desc_t	*bdp;
	xfs_inode_t	*ip;
	uint16_t	dmstate;
	struct inode	*inode = LINVFS_GET_IP(vp);

	XFS_BHV_LOOKUP(vp, bdp);
	ip = XFS_BHVTOI(bdp);
	do {
		dmstate = ip->i_iocore.io_dmstate;
		if (locktype)
			xfs_rwunlock(bdp, *locktype);

		if (flags & DM_FLAGS_ISEM)
			up(&inode->i_sem);
#ifdef DM_FLAGS_IALLOCSEM_WR
		if (flags & DM_FLAGS_IALLOCSEM_WR)
			up_write(&inode->i_alloc_sem);
#endif
#ifdef DM_FLAGS_IALLOCSEM_RD
		if (flags & DM_FLAGS_IALLOCSEM_RD)
			up_read(&inode->i_alloc_sem);
#endif

		error = dm_send_data_event(event, vp, DM_RIGHT_NULL,
				offset, length, flags);

		if (flags & DM_FLAGS_ISEM)
			down(&inode->i_sem);
#ifdef DM_FLAGS_IALLOCSEM_WR
		if (flags & DM_FLAGS_IALLOCSEM_WR)
			down_write(&inode->i_alloc_sem);
#endif
#ifdef DM_FLAGS_IALLOCSEM_RD
		if (flags & DM_FLAGS_IALLOCSEM_RD)
			down_read(&inode->i_alloc_sem);
#endif

		if (locktype)
			xfs_rwlock(bdp, *locktype);
	} while (!error && (ip->i_iocore.io_dmstate != dmstate));

	return error;
}

/*	prohibited_mr_events
 *
 *	Return event bits representing any events which cannot have managed
 *	region events set due to memory mapping of the file.  If the maximum
 *	protection allowed in any pregion includes PROT_WRITE, and the region
 *	is shared and not text, then neither READ nor WRITE events can be set.
 *	Otherwise if the file is memory mapped, no READ event can be set.
 *
 */

STATIC int
prohibited_mr_events(
	vnode_t		*vp)
{
	struct address_space *mapping = LINVFS_GET_IP(vp)->i_mapping;
	int prohibited = (1 << DM_EVENT_READ);
	struct vm_area_struct *vma;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	struct prio_tree_iter iter;
#endif

	if (!VN_MAPPED(vp))
		return 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	down(&mapping->i_shared_sem);
	vma = __vma_prio_tree_first(&mapping->i_mmap_shared, &iter, 0, ULONG_MAX);
	while (vma) {
		if (!(vma->vm_flags & VM_DENYWRITE)) {
			prohibited |= (1 << DM_EVENT_WRITE);
			break;
		}

		vma = __vma_prio_tree_next(vma, &mapping->i_mmap_shared, &iter, 0, ULONG_MAX);
	}
	up(&mapping->i_shared_sem);
#else
	spin_lock(&mapping->i_shared_lock);
	for (vma = mapping->i_mmap_shared; vma; vma = vma->vm_next) {
		if (!(vma->vm_flags & VM_DENYWRITE)) {
			prohibited |= (1 << DM_EVENT_WRITE);
			break;
		}
	}
	spin_unlock(&mapping->i_shared_lock);
#endif

	return prohibited;
}


#ifdef	DEBUG_RIGHTS
STATIC int
xfs_bdp_to_hexhandle(
	bhv_desc_t	*bdp,
	u_int		type,
	char		*buffer)
{
	xfs_handle_t	handle;
	vnode_t		*vp;
	u_char		*ip;
	int		length;
	int		error;
	int		i;

	vp = BHV_TO_VNODE(bdp);

	if ((error = dm_vp_to_handle(vp, &handle)))
		return(error);

	if (type == DM_FSYS_OBJ) {	/* a filesystem handle */
		length = FSHSIZE;
	} else {
		length = XFS_HSIZE(handle);
	}
	for (ip = (u_char *)&handle, i = 0; i < length; i++) {
		*buffer++ = "0123456789abcdef"[ip[i] >> 4];
		*buffer++ = "0123456789abcdef"[ip[i] & 0xf];
	}
	*buffer = '\0';
	return(0);
}
#endif	/* DEBUG_RIGHTS */




/* Copy in and validate an attribute name from user space.  It should be a
   string of at least one and at most DM_ATTR_NAME_SIZE characters.  Because
   the dm_attrname_t structure doesn't provide room for the trailing NULL
   byte, we just copy in one extra character and then zero it if it
   happens to be non-NULL.
*/

STATIC int
xfs_copyin_attrname(
	dm_attrname_t	*from,		/* dm_attrname_t in user space */
	dm_dkattrname_t *to)		/* name buffer in kernel space */
{
	int		error;
	size_t len;

	strcpy(to->dan_chars, dmattr_prefix);

	len = strnlen_user((char*)from, DM_ATTR_NAME_SIZE);
	error = copy_from_user(&to->dan_chars[DMATTR_PREFIXLEN], from, len);
	/* copy_from_user returns negative error */

	if (!error && (to->dan_chars[DMATTR_PREFIXLEN] == '\0'))
		error = -EINVAL;
	if (error == -EFAULT) {
		to->dan_chars[sizeof(to->dan_chars) - 1] = '\0';
		error = 0;
	}
	return(-error); /* return it positive */
}


/* This copies selected fields in an inode into a dm_stat structure.  Because
   these fields must return the same values as they would in stat(), the
   majority of this code was copied directly from xfs_getattr().  Any future
   changes to xfs_gettattr() must also be reflected here.

   The inode must be kept locked SHARED by the caller.
*/

STATIC void
xfs_ip_to_stat(
	xfs_mount_t	*mp,
	dm_stat_t	*buf,
	xfs_inode_t	*ip)
{
	vnode_t		*vp = XFS_ITOV(ip);

	buf->dt_size = ip->i_d.di_size;
	buf->dt_dev = ip->i_mount->m_dev;

	buf->dt_ino = ip->i_ino;
#if XFS_BIG_INUMS
	buf->dt_ino += mp->m_inoadd;
#endif
	/*
	 * Copy from in-core inode.
	 */
	buf->dt_mode = VTTOIF(vp->v_type) | (ip->i_d.di_mode & MODEMASK);
	buf->dt_uid = ip->i_d.di_uid;
	buf->dt_gid = ip->i_d.di_gid;
	buf->dt_nlink = ip->i_d.di_nlink;
	/*
	 * Minor optimization, check the common cases first.
	 */
	if ((vp->v_type == VREG) || (vp->v_type == VDIR)) {
		buf->dt_rdev = 0;
	} else if ((vp->v_type == VCHR) || (vp->v_type == VBLK) ) {
		buf->dt_rdev = XFS_TO_LINUX_DEVT(ip->i_df.if_u2.if_rdev);
	} else {
		buf->dt_rdev = 0;	/* not a b/c spec. */
	}

	buf->dt_atime = ip->i_d.di_atime.t_sec;
	buf->dt_mtime = ip->i_d.di_mtime.t_sec;
	buf->dt_ctime = ip->i_d.di_ctime.t_sec;

	switch (ip->i_d.di_mode & S_IFMT) {
	  case S_IFBLK:
	  case S_IFCHR:
		buf->dt_blksize = BLKDEV_IOSIZE;
		break;
	  default:
		/*
		 * We use the read buffer size as a recommended I/O
		 * size.  This should always be larger than the
		 * write buffer size, so it should be OK.
		 * The value returned is in bytes.
		 */
		buf->dt_blksize = 1 << mp->m_readio_log;
		break;
	}

	/*
	 * XXX : truncate to 32 bits for now.
	 */
	buf->dt_blocks =
		XFS_FSB_TO_BB(mp, ip->i_d.di_nblocks + ip->i_delayed_blks);

	/*
	 * XFS-added attributes
	 */

	/*
	 * convert di_flags to xflags
	 */
	buf->dt_xfs_xflags = 0;
	if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME)
		buf->dt_xfs_xflags |= DM_XFLAG_REALTIME;
	if (ip->i_d.di_flags & XFS_DIFLAG_PREALLOC)
		buf->dt_xfs_xflags |= DM_XFLAG_PREALLOC;
	if (ip->i_d.di_flags & XFS_DIFLAG_IMMUTABLE)
		buf->dt_xfs_xflags |= DM_XFLAG_IMMUTABLE;
	if (ip->i_d.di_flags & XFS_DIFLAG_APPEND)
		buf->dt_xfs_xflags |= DM_XFLAG_APPEND;
	if (ip->i_d.di_flags & XFS_DIFLAG_SYNC)
		buf->dt_xfs_xflags |= DM_XFLAG_SYNC;
	if (ip->i_d.di_flags & XFS_DIFLAG_NOATIME)
		buf->dt_xfs_xflags |= DM_XFLAG_NOATIME;
	if (ip->i_d.di_flags & XFS_DIFLAG_NODUMP)
		buf->dt_xfs_xflags |= DM_XFLAG_NODUMP;
	if (XFS_IFORK_Q(ip))
		buf->dt_xfs_xflags |= DM_XFLAG_HASATTR;
	buf->dt_xfs_extsize = ip->i_d.di_extsize << mp->m_sb.sb_blocklog;
	buf->dt_xfs_extents = (ip->i_df.if_flags & XFS_IFEXTENTS) ?
		ip->i_df.if_bytes / sizeof(xfs_bmbt_rec_t) :
		ip->i_d.di_nextents;
	if (ip->i_afp != NULL) {
		buf->dt_xfs_aextents =
			(ip->i_afp->if_flags & XFS_IFEXTENTS) ?
			 ip->i_afp->if_bytes / sizeof(xfs_bmbt_rec_t) :
			 ip->i_d.di_anextents;
	} else {
		buf->dt_xfs_aextents = 0;
	}

	/* Now fill in the fields that xfs_getattr() doesn't do. */

	buf->dt_emask = ip->i_d.di_dmevmask;
	buf->dt_nevents = DM_EVENT_MAX;
	buf->dt_pers = 0;
	buf->dt_change = 0;
	buf->dt_dtime = ip->i_d.di_ctime.t_sec;
	buf->dt_xfs_dmstate = ip->i_d.di_dmstate;
	buf->dt_xfs_igen = ip->i_d.di_gen;

	/* Set if one of READ, WRITE or TRUNCATE bits is set in emask */

	buf->dt_pmanreg = ( DMEV_ISSET(DM_EVENT_READ, buf->dt_emask) ||
			DMEV_ISSET(DM_EVENT_WRITE, buf->dt_emask) ||
			DMEV_ISSET(DM_EVENT_TRUNCATE, buf->dt_emask) ) ? 1 : 0;
}


/*
 * This is used by dm_get_bulkattr() as well as dm_get_dirattrs().
 * Given a inumber, it igets the inode and fills the given buffer
 * with the dm_stat structure for the file.
 */
/* ARGSUSED */
STATIC int
xfs_dm_bulkstat_one(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	ino,		/* inode number to get data for */
	void		*buffer,	/* buffer to place output in */
	int		ubsize,		/* size of buffer */
	void		*private_data,	/* my private data */
	xfs_daddr_t	bno,		/* starting block of inode cluster */
	int		*ubused,	/* amount of buffer we used */
	void		*dip,		/* on-disk inode pointer */
	int		*res)		/* bulkstat result code */
{
	xfs_inode_t	*ip;
	dm_stat_t	*sbuf;
	dm_xstat_t	*xbuf = NULL;
	xfs_handle_t	handle;
	u_int		stat_sz;
	int		error;
	dm_bulkstat_one_t *dmb = (dm_bulkstat_one_t*)private_data;

	if (dmb && dmb->bulkall) {
		xbuf = (dm_xstat_t *)buffer;
		sbuf = &xbuf->dx_statinfo;
		stat_sz = DM_STAT_SIZE(*xbuf, 0);
	}
	else {
		sbuf = (dm_stat_t *)buffer;
		stat_sz = DM_STAT_SIZE(*sbuf, 0);
	}

	stat_sz = (stat_sz+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);
	if (stat_sz > ubsize) {
		*res = BULKSTAT_RV_NOTHING;
		return ENOMEM;
	}

	if (ino == mp->m_sb.sb_rbmino || ino == mp->m_sb.sb_rsumino) {
		*res = BULKSTAT_RV_NOTHING;
		return EINVAL;
	}
	error = xfs_iget(mp, tp, ino, XFS_ILOCK_SHARED, &ip, bno);
	if (error) {
		*res = BULKSTAT_RV_NOTHING;
		return(error);
	}
	if (ip->i_d.di_mode == 0) {
		xfs_iput_new(ip, XFS_ILOCK_SHARED);
		*res = BULKSTAT_RV_NOTHING;
		return(ENOENT);
	}

	/*
	 * copy everything to the dm_stat buffer
	 */
	xfs_ip_to_stat(mp, sbuf, ip);

	/*
	 * Make the handle and put it at the end of the stat buffer.
	 */
	dm_vp_to_handle(XFS_ITOV(ip), &handle);
	/* Handle follows outer struct, but ptr is always in dm_stat_t */
	if (xbuf) {
		memcpy(xbuf+1, &handle, sizeof(handle));
		sbuf->dt_handle.vd_offset = (ssize_t) sizeof(dm_xstat_t);
	}
	else {
		memcpy(sbuf+1, &handle, sizeof(handle));
		sbuf->dt_handle.vd_offset = (ssize_t) sizeof(dm_stat_t);
	}
	sbuf->dt_handle.vd_length = (size_t) XFS_HSIZE(handle);

	/*
	 * This is unused in bulkstat - so we zero it out.
	 */
	memset((void *) &sbuf->dt_compname, 0, sizeof(dm_vardata_t));

  	/*
	 * Do not hold ILOCK_SHARED during VOP_ATTR_GET.
  	 */
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

  	/*
	 * For dm_xstat, get attr.
	 */
	if (xbuf) {
		caddr_t	bufp = (caddr_t)xbuf;
		int	value_len;

		/* Determine place to drop attr value, and available space. */
		bufp += stat_sz;
		value_len = ubsize - stat_sz;

		/* Sanity check value_len */
		if (value_len < XFS_BUG_KLUDGE) {
			VN_RELE(XFS_ITOV(ip));
			*res = BULKSTAT_RV_NOTHING;
			return ENOMEM;
		}
		if (value_len > ATTR_MAX_VALUELEN)
			value_len = ATTR_MAX_VALUELEN;

		memset((void *) &xbuf->dx_attrdata, 0, sizeof(dm_vardata_t));
		VOP_ATTR_GET(XFS_ITOV(ip), dmb->attrname.dan_chars, bufp,
			     &value_len, ATTR_ROOT, sys_cred, error);

		DM_EA_XLATE_ERR(error);
		if (error && (error != ENOATTR)) {
			VN_RELE(XFS_ITOV(ip));
			*res = BULKSTAT_RV_NOTHING;
			return (error == E2BIG ? ENOMEM : error);
		}

		/* How much space in the attr? */
		if (error != ENOATTR) {
			xbuf->dx_attrdata.vd_offset = stat_sz;
			xbuf->dx_attrdata.vd_length = value_len;
			value_len = (value_len+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);
			stat_sz += value_len;
		}
	}

	/*
	 * Finished with the vnode
  	 */
	VN_RELE(XFS_ITOV(ip));

	/*
	 *  Update link in dm_stat_t to point to next struct.
	 */
	sbuf->_link = stat_sz;

	*res = BULKSTAT_RV_DIDONE;
	if (ubused)
		*ubused = stat_sz;
	if (dmb) {
		if (xbuf)
			dmb->laststruct = (void*)xbuf;
		else
			dmb->laststruct = (void*)sbuf;
	}
	return(0);
}


STATIC int
xfs_get_dirents(
	xfs_inode_t	*dirp,
	void		*bufp,
	size_t		bufsz,
	xfs_off_t	*locp,
	size_t		*nreadp)
{
	int		sink;
	struct uio	auio;
	struct iovec	aiov;
	int		rval;

	*nreadp = 0;

	aiov.iov_base = bufp;
	aiov.iov_len = bufsz;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = *locp;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_resid = bufsz;

	rval = XFS_DIR_GETDENTS(dirp->i_mount, NULL, dirp, &auio, &sink);
	if (! rval) {
		*locp = auio.uio_offset;

		/*
		 * number of bytes read into the dirent buffer
		 */
		*nreadp = bufsz - auio.uio_resid;
	}
	return(rval);
}


STATIC int
xfs_dirents_to_stats(
	xfs_mount_t	*mp,
	xfs_dirent_t	*direntp,	/* array of dirent structs */
	void		*bufp,		/* buffer to fill */
	size_t		direntbufsz,	/* sz of filled part of dirent buf */
	size_t		*spaceleftp,	/* IO - space left in user buffer */
	size_t		*nwrittenp,	/* number of bytes written to 'bufp' */
	xfs_off_t	*locp)
{
	xfs_dirent_t	*p;
	dm_stat_t	*statp;
	size_t		reclen;
	size_t		namelen;
	size_t		spaceleft;
	xfs_off_t	prevoff;
	int		res;

	spaceleft = *spaceleftp;
	*spaceleftp = 0;
	*nwrittenp = 0;
	prevoff = 0;  /* sizeof this getdents record */

	/*
	 * Go thru all the dirent records, making dm_stat structures from
	 * them, one by one, until dirent buffer is empty or stat buffer
	 * is full.
	 */
	p = direntp;
	statp = (dm_stat_t *) bufp;
	for (reclen = (size_t) p->d_reclen; direntbufsz > 0;
					direntbufsz -= reclen,
					p = (xfs_dirent_t *) ((char *) p + reclen),
					reclen = (size_t) p->d_reclen) {

		namelen = strlen(p->d_name) + 1;

		/*
		 * Make sure we have enough space.
		 */
		if (spaceleft <= DM_STAT_SIZE(*statp, namelen)) {
			/*
			 * d_off field in dirent_t points at the next entry.
			 */
			if (prevoff)	/* did at least one; update location */
				*locp = prevoff;
			*spaceleftp = 0;

			/*
			 * The last link is NULL.
			 */
			statp->_link = 0;
			return(0);
		}

		statp = (dm_stat_t *) bufp;

		(void)xfs_dm_bulkstat_one(mp, NULL, (xfs_ino_t)p->d_ino,
					  statp, sizeof(*statp), NULL,
					  0, NULL, NULL, &res);
		if (res != BULKSTAT_RV_DIDONE)
			continue;

		/*
		 * On return from bulkstat_one(), stap->_link points
		 * at the end of the handle in the stat structure.
		 */
		statp->dt_compname.vd_offset = statp->_link;
		statp->dt_compname.vd_length = namelen;
		/*
		 * Directory entry name is guaranteed to be
		 * null terminated; the copy gets the '\0' too.
		 */
		memcpy((char *) statp + statp->_link, p->d_name, namelen);

		/* Word-align the record */
		statp->_link = (statp->_link + namelen + (DM_STAT_ALIGN - 1))
			& ~(DM_STAT_ALIGN - 1);

		spaceleft -= statp->_link;
		*nwrittenp += statp->_link;
		bufp = (char *)statp + statp->_link;

		/*
		 * We need to rollback to this position if something happens.
		 * So we remember it.
		 */
		prevoff = p->d_off;
	}
	statp->_link = 0;

	/*
	 * If there's space left to put in more, caller should know that..
	 */
	if (spaceleft > DM_STAT_SIZE(*statp, MAXNAMLEN)) {
		*spaceleftp = spaceleft;
	}
	return(0);
}


/* xfs_dm_f_get_eventlist - return the dm_eventset_t mask for inode vp. */

STATIC int
xfs_dm_f_get_eventlist(
	bhv_desc_t	*bdp,
	dm_right_t	right,
	u_int		nelem,
	dm_eventset_t	*eventsetp,		/* in kernel space! */
	u_int		*nelemp)		/* in kernel space! */
{
	dm_eventset_t	eventset;
	xfs_inode_t	*ip;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	/* Note that we MUST return a regular file's managed region bits as
	   part of the mask because dm_get_eventlist is supposed to return the
	   union of all managed region flags in those bits.  Since we only
	   support one region, we can just return the bits as they are.	 For
	   all other object types, the bits will already be zero.  Handy, huh?
	*/

	ip = XFS_BHVTOI(bdp);
	eventset = ip->i_d.di_dmevmask;

	/* Now copy the event mask and event count back to the caller.	We
	   return the lesser of nelem and DM_EVENT_MAX.
	*/

	if (nelem > DM_EVENT_MAX)
		nelem = DM_EVENT_MAX;
	eventset &= (1 << nelem) - 1;

	*eventsetp = eventset;
	*nelemp = nelem;
	return(0);
}


/* xfs_dm_f_set_eventlist - update the dm_eventset_t mask in the inode vp.  Only the
   bits from zero to maxevent-1 are being replaced; higher bits are preserved.
*/

STATIC int
xfs_dm_f_set_eventlist(
	bhv_desc_t	*bdp,
	dm_right_t	right,
	dm_eventset_t	*eventsetp,	/* in kernel space! */
	u_int		maxevent)
{
	dm_eventset_t	eventset;
	dm_eventset_t	max_mask;
	dm_eventset_t	valid_events;
	vnode_t		*vp;
	xfs_inode_t	*ip;
	xfs_trans_t	*tp;
	xfs_mount_t	*mp;
	int		error;

	if (right < DM_RIGHT_EXCL)
		return(EACCES);

	eventset = *eventsetp;
	if (maxevent >= sizeof(ip->i_d.di_dmevmask) * NBBY)
		return(EINVAL);
	max_mask = (1 << maxevent) - 1;

	vp = BHV_TO_VNODE(bdp);
	if (vp->v_type == VDIR) {
		valid_events = DM_XFS_VALID_DIRECTORY_EVENTS;
	} else {	/* file or symlink */
		valid_events = DM_XFS_VALID_FILE_EVENTS;
	}
	if ((eventset & max_mask) & ~valid_events)
		return(EINVAL);

	/* Adjust the event mask so that the managed region bits will not
	   be altered.
	*/

	max_mask &= ~(1 <<DM_EVENT_READ);	/* preserve current MR bits */
	max_mask &= ~(1 <<DM_EVENT_WRITE);
	max_mask &= ~(1 <<DM_EVENT_TRUNCATE);

	ip = XFS_BHVTOI(bdp);
	mp = ip->i_mount;
	tp = xfs_trans_alloc(mp, XFS_TRANS_SET_DMATTRS);
	error = xfs_trans_reserve(tp, 0, XFS_ICHANGE_LOG_RES(mp), 0, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return(error);
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	ip->i_d.di_dmevmask = (eventset & max_mask) | (ip->i_d.di_dmevmask & ~max_mask);
	ip->i_iocore.io_dmevmask = ip->i_d.di_dmevmask;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	VN_HOLD(vp);
	xfs_trans_commit(tp, 0, NULL);

	return(0);
}


/* xfs_dm_fs_get_eventlist - return the dm_eventset_t mask for filesystem vfsp. */

STATIC int
xfs_dm_fs_get_eventlist(
	bhv_desc_t	*bdp,
	dm_right_t	right,
	u_int		nelem,
	dm_eventset_t	*eventsetp,		/* in kernel space! */
	u_int		*nelemp)		/* in kernel space! */
{
	dm_eventset_t	eventset;
	xfs_mount_t	*mp;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	mp = XFS_BHVTOI(bdp)->i_mount;
	eventset = mp->m_dmevmask;

	/* Now copy the event mask and event count back to the caller.	We
	   return the lesser of nelem and DM_EVENT_MAX.
	*/

	if (nelem > DM_EVENT_MAX)
		nelem = DM_EVENT_MAX;
	eventset &= (1 << nelem) - 1;

	*eventsetp = eventset;
	*nelemp = nelem;
	return(0);
}


/* xfs_dm_fs_set_eventlist - update the dm_eventset_t mask in the mount structure for
   filesystem vfsp.  Only the bits from zero to maxevent-1 are being replaced;
   higher bits are preserved.
*/

STATIC int
xfs_dm_fs_set_eventlist(
	bhv_desc_t	*bdp,
	dm_right_t	right,
	dm_eventset_t	*eventsetp,	/* in kernel space! */
	u_int		maxevent)
{
	dm_eventset_t	eventset;
	dm_eventset_t	max_mask;
	xfs_mount_t	*mp;

	if (right < DM_RIGHT_EXCL)
		return(EACCES);

	eventset = *eventsetp;

	mp = XFS_BHVTOI(bdp)->i_mount;
	if (maxevent >= sizeof(mp->m_dmevmask) * NBBY)
		return(EINVAL);
	max_mask = (1 << maxevent) - 1;

	if ((eventset & max_mask) & ~DM_XFS_VALID_FS_EVENTS)
		return(EINVAL);

	mp->m_dmevmask = (eventset & max_mask) | (mp->m_dmevmask & ~max_mask);
	return(0);
}


/* Code in this routine must exactly match the logic in xfs_diordwr() in
   order for this to work!
*/

STATIC int
xfs_dm_direct_ok(
	bhv_desc_t	*bdp,
	dm_off_t	off,
	dm_size_t	len,
	void		*bufp)
{
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;

	ip = XFS_BHVTOI(bdp);
	mp = ip->i_mount;

	/* Realtime files can ONLY do direct I/O. */

	if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME)
		return(1);

	/* If direct I/O is disabled, or if the request is too small, use
	   buffered I/O.
	*/

	if (!dm_min_dio_xfer || len < dm_min_dio_xfer)
		return(0);

#if 0
	/* If the request is not well-formed or is too large, use
	   buffered I/O.
	*/

	if ((__psint_t)bufp & scache_linemask)	/* if buffer not aligned */
		return(0);
	if (off & mp->m_blockmask)		/* if file offset not aligned */
		return(0);
	if (len & mp->m_blockmask)		/* if xfer length not aligned */
		return(0);
	if (len > ctooff(v.v_maxdmasz - 1))	/* if transfer too large */
		return(0);

	/* A valid direct I/O candidate. */

	return(1);
#else
	return(0);
#endif
}


/* We need to be able to select various combinations of FINVIS, O_NONBLOCK,
   O_DIRECT, and O_SYNC, yet we don't have a file descriptor and we don't have
   the file's pathname.	 All we have is a handle.
*/

STATIC int
xfs_dm_rdwr(
	vnode_t		*vp,
	uint		fflag,
	mode_t		fmode,
	dm_off_t	off,
	dm_size_t	len,
	void		*bufp,
	int		*rvp)
{
	int		error;
	int		oflags;
	int		ioflags;
	ssize_t		xfer;
	struct file	file;
	struct inode	*ip;
	struct dentry	*dentry;
	bhv_desc_t	*xbdp;

	if (off < 0 || vp->v_type != VREG)
		return(EINVAL);

	if (fmode & FMODE_READ) {
		oflags = O_RDONLY;
	} else {
		oflags = O_WRONLY;
	}

	/*
	 * Build file descriptor flags and I/O flags.  O_NONBLOCK is needed so
	 * that we don't block on mandatory file locks.	 IO_INVIS is needed so
	 * that we don't change any file timestamps.
	 */

	oflags |= O_LARGEFILE | O_NONBLOCK;
	ioflags = IO_INVIS;
	XFS_BHV_LOOKUP(vp, xbdp);
	if (xfs_dm_direct_ok(xbdp, off, len, bufp)) {
		ioflags |= IO_ISDIRECT;
		oflags |= O_DIRECT;
	}

	if (fflag & O_SYNC)
		oflags |= O_SYNC;

	ip = LINVFS_GET_IP(vp);
	if (ip->i_fop == NULL) {
		/* no iput; caller did get, and will do put */
		return(EINVAL);
	}

	igrab(ip);

	dentry = d_alloc_anon(ip);
	if (dentry == NULL) {
		iput(ip);
		return ENOMEM;
	}

	if (fmode & FMODE_WRITE) {
		error = get_write_access(ip);
		if (error) {
			error = -error;
			goto dput;
		}
	}

	error = open_private_file(&file, dentry, oflags);
	if (error) {
		error = EINVAL;
		goto put_access;
	}
	file.f_op = &linvfs_invis_file_operations;

	if (fmode & FMODE_READ) {
		xfer = file.f_op->read(&file, bufp, len, (loff_t*)&off);
	} else {
		xfer = file.f_op->write(&file, bufp, len, (loff_t*)&off);
	}

	if (xfer >= 0) {
		*rvp = xfer;
		error = 0;
	} else {
		/* xfs_read/xfs_write return negative error--flip it */
		error = -(int)xfer;
	}

	close_private_file(&file);
 put_access:
	if (fmode & FMODE_WRITE)
		put_write_access(ip);
 dput:
	dput(dentry);
	return error;
}

/* ARGSUSED */
STATIC int
xfs_dm_clear_inherit(
	vnode_t		*vp,
	dm_right_t	right,
	dm_attrname_t	*attrnamep)
{
	return(ENOSYS);
}


/* ARGSUSED */
STATIC int
xfs_dm_create_by_handle(
	vnode_t		*vp,
	dm_right_t	right,
	void		*hanp,
	size_t		hlen,
	char		*cname)
{
	return(ENOSYS);
}


/* ARGSUSED */
STATIC int
xfs_dm_downgrade_right(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		type)		/* DM_FSYS_OBJ or zero */
{
#ifdef	DEBUG_RIGHTS
	char		buffer[sizeof(xfs_handle_t) * 2 + 1];
	bhv_desc_t	*bdp;

	XFS_BHV_LOOKUP(vp, bdp);

	if (!xfs_bdp_to_hexhandle(bdp, type, buffer)) {
		printf("dm_downgrade_right: old %d new %d type %d handle %s\n",
			right, DM_RIGHT_SHARED, type, buffer);
	} else {
		printf("dm_downgrade_right: old %d new %d type %d handle "
			"<INVALID>\n", right, DM_RIGHT_SHARED, type);
	}
#endif	/* DEBUG_RIGHTS */
	return(0);
}


/* Note: xfs_dm_get_allocinfo() makes no attempt to coalesce two adjacent
   extents when both are of type DM_EXTENT_RES; this is left to the caller.
   XFS guarantees that there will never be two adjacent DM_EXTENT_HOLE extents.

   In order to provide the caller with all extents in a file including
   those beyond the file's last byte offset, we have to use the xfs_bmapi()
   interface.  (VOP_BMAP won't let us see past EOF, and xfs_getbmap is too
   buggy.)
*/

STATIC int
xfs_dm_get_allocinfo_rvp(
	vnode_t		*vp,
	dm_right_t	right,
	dm_off_t	*offp,
	u_int		nelem,
	dm_extent_t	*extentp,
	u_int		*nelemp,
	int		*rvp)
{
	xfs_inode_t	*ip;		/* xfs incore inode pointer */
	xfs_mount_t	*mp;		/* file system mount point */
	xfs_fileoff_t	fsb_offset;
	xfs_filblks_t	fsb_length;
	dm_off_t	startoff;
	int		elem;
	bhv_desc_t	*xbdp;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	if (copy_from_user( &startoff, offp, sizeof(startoff)))
		return(EFAULT);

	XFS_BHV_LOOKUP(vp, xbdp);

	ip = XFS_BHVTOI(xbdp);
	mp = ip->i_mount;

	if (startoff > XFS_MAXIOFFSET(mp))
		return(EINVAL);

	if (nelem == 0) {
		if (put_user(1, nelemp))
			return(EFAULT);
		return(E2BIG);
	}

	/* Convert the caller's starting offset into filesystem allocation
	   units as required by xfs_bmapi().  Round the offset down so that
	   it is sure to be included in the reply.
	*/

	fsb_offset = XFS_B_TO_FSBT(mp, startoff);
	fsb_length = XFS_B_TO_FSB(mp, XFS_MAXIOFFSET(mp)) - fsb_offset;
	elem = 0;

	while (fsb_length && elem < nelem) {
		xfs_bmbt_irec_t bmp[50];
		dm_extent_t	extent;
		xfs_filblks_t	fsb_bias;
		dm_size_t	bias;
		int		error;
		int		lock;
		int		num;
		int		i;

		/* Compute how many getbmap structures to use on the xfs_bmapi
		   call.
		*/

		num = MIN((u_int)(nelem - elem), (u_int)(sizeof(bmp) / sizeof(bmp[0])));

		xfs_ilock(ip, XFS_IOLOCK_SHARED);
		lock = xfs_ilock_map_shared(ip);

		error = xfs_bmapi(NULL, ip, fsb_offset, fsb_length,
			XFS_BMAPI_ENTIRE, NULL, 0, bmp, &num, NULL);

		xfs_iunlock_map_shared(ip, lock);
		xfs_iunlock(ip, XFS_IOLOCK_SHARED);

		if (error)
			return(error);

		/* Fill in the caller's extents, adjusting the bias in the
		   first entry if necessary.
		*/

		for (i = 0; i < num; i++, extentp++) {
			bias = startoff - XFS_FSB_TO_B(mp, bmp[i].br_startoff);
			extent.ex_offset = startoff;
			extent.ex_length =
				XFS_FSB_TO_B(mp, bmp[i].br_blockcount) - bias;
			if (bmp[i].br_startblock == HOLESTARTBLOCK) {
				extent.ex_type = DM_EXTENT_HOLE;
			} else {
				extent.ex_type = DM_EXTENT_RES;
			}
			startoff = extent.ex_offset + extent.ex_length;

			if (copy_to_user( extentp, &extent, sizeof(extent)))
				return(EFAULT);

			fsb_bias = fsb_offset - bmp[i].br_startoff;
			fsb_offset += bmp[i].br_blockcount - fsb_bias;
			fsb_length -= bmp[i].br_blockcount - fsb_bias;
			elem++;
		}
	}

	if (fsb_length == 0) {
		startoff = 0;
	}
	if (copy_to_user( offp, &startoff, sizeof(startoff)))
		return(EFAULT);

	if (copy_to_user( nelemp, &elem, sizeof(elem)))
		return(EFAULT);

	*rvp = (fsb_length == 0 ? 0 : 1);

	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_bulkall_rvp(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		mask,
	dm_attrname_t	*attrnamep,
	dm_attrloc_t	*locp,
	size_t		buflen,
	void		*bufp,		/* address of buffer in user space */
	size_t		*rlenp,		/* user space address */
	int		*rvalp)
{
	int		error, done;
	int		nelems;
	u_int		statstruct_sz;
	dm_attrloc_t	loc;
	bhv_desc_t	*mp_bdp;
	xfs_mount_t	*mp;
	vfs_t		*vfsp = vp->v_vfsp;
	dm_bulkstat_one_t dmb;
	dm_xstat_t	*dxs;

	if (attrnamep->an_chars[0] == '\0')
		return(EINVAL);

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	if (copy_from_user( &loc, locp, sizeof(loc)))
		return(EFAULT);

	/* Because we will write directly to the user's buffer, make sure that
	   the buffer is properly aligned.
	*/

	if (((__psint_t)bufp & (DM_STAT_ALIGN - 1)) != 0)
		return(EFAULT);

	/* Size of the handle is constant for this function.
	 * If there are no files with attributes, then this will be the
	 * maximum number of inodes we can get.
	 */

	statstruct_sz = DM_STAT_SIZE(dm_xstat_t, 0);
	statstruct_sz = (statstruct_sz+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);

	nelems = buflen / statstruct_sz; 
	if (nelems < 1) {
		if (put_user( statstruct_sz, rlenp ))
			return(EFAULT);
		return(E2BIG);
	} 

	mp_bdp = bhv_lookup(VFS_BHVHEAD(vfsp), &xfs_vfsops);
	ASSERT(mp_bdp);
	mp = XFS_BHVTOM(mp_bdp);


	/* Build the on-disk version of the attribute name. */
	strcpy(dmb.attrname.dan_chars, dmattr_prefix);
	strncpy(&dmb.attrname.dan_chars[DMATTR_PREFIXLEN],
		(char *)attrnamep->an_chars, DM_ATTR_NAME_SIZE + 1);
	dmb.attrname.dan_chars[sizeof(dmb.attrname.dan_chars) - 1] = '\0';

	/*
	 * fill the buffer with dm_xstat_t's 
	 */

	dmb.laststruct = NULL;
	dmb.bulkall = 1;
	error = xfs_bulkstat(mp, NULL, (xfs_ino_t *)&loc,
			     &nelems, 
			     xfs_dm_bulkstat_one,
			     (void*)&dmb,
			     statstruct_sz,
			     bufp, 
			     BULKSTAT_FG_IGET,
			     &done);

	if (error)
		return(error);
	if (!done)
		*rvalp = 1;
	else
		*rvalp = 0;
	if (put_user( statstruct_sz * nelems, rlenp ))
		return(EFAULT);

	if (copy_to_user( locp, &loc, sizeof(loc)))
		return(EFAULT);
	/*
	 *  If we didn't do any, we must not have any more to do.
	 */
	if (nelems < 1)
		return(0);
	/* set _link in the last struct to zero */
	dxs = (dm_xstat_t*)dmb.laststruct;
	if (dxs) {
		dm_stat_t *ds = &dxs->dx_statinfo;
		if (put_user(0, &ds->_link))
			return(EFAULT);
	}
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_bulkattr_rvp(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		mask,
	dm_attrloc_t	*locp,
	size_t		buflen,
	void		*bufp,
	size_t		*rlenp,
	int		*rvalp)
{
	int		error, done;
	int		nelems;
	u_int		statstruct_sz;
	dm_attrloc_t	loc;
	bhv_desc_t	*mp_bdp;
	xfs_mount_t	*mp;
	vfs_t		*vfsp = vp->v_vfsp;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	if (copy_from_user( &loc, locp, sizeof(loc)))
		return(EFAULT);

	/* Because we will write directly to the user's buffer, make sure that
	   the buffer is properly aligned.
	*/

	if (((__psint_t)bufp & (DM_STAT_ALIGN - 1)) != 0)
		return(EFAULT);

	/* size of the handle is constant for this function */

	statstruct_sz = DM_STAT_SIZE(dm_stat_t, 0);
	statstruct_sz = (statstruct_sz+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);

	nelems = buflen / statstruct_sz;
	if (nelems < 1) {
		if (put_user( statstruct_sz, rlenp ))
			return(EFAULT);
		return(E2BIG);
	}

	mp_bdp = bhv_lookup(VFS_BHVHEAD(vfsp), &xfs_vfsops);
	ASSERT(mp_bdp);
	mp = XFS_BHVTOM(mp_bdp);


	/*
	 * fill the buffer with dm_stat_t's
	 */

	error = xfs_bulkstat(mp, NULL,
			     (xfs_ino_t *)&loc,
			     &nelems,
			     xfs_dm_bulkstat_one,
			     NULL,
			     statstruct_sz,
			     bufp,
			     BULKSTAT_FG_IGET,
			     &done);
	if (error)
		return(error);
	if (!done) {
		*rvalp = 1;
	} else {
		*rvalp = 0;
	}

	if (put_user( statstruct_sz * nelems, rlenp ))
		return(EFAULT);

	if (copy_to_user( locp, &loc, sizeof(loc)))
		return(EFAULT);

	/*
	 *  If we didn't do any, we must not have any more to do.
	 */
	if (nelems < 1)
		return(0);
	/* set _link in the last struct to zero */
	if (put_user( 0,
	    &((dm_stat_t *)((char *)bufp + statstruct_sz*(nelems-1)))->_link)
	   )
		return(EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_config(
	vnode_t		*vp,
	dm_right_t	right,
	dm_config_t	flagname,
	dm_size_t	*retvalp)
{
	dm_size_t	retval;

	switch (flagname) {
	case DM_CONFIG_DTIME_OVERLOAD:
	case DM_CONFIG_PERS_ATTRIBUTES:
	case DM_CONFIG_PERS_EVENTS:
	case DM_CONFIG_PERS_MANAGED_REGIONS:
	case DM_CONFIG_PUNCH_HOLE:
	case DM_CONFIG_WILL_RETRY:
		retval = DM_TRUE;
		break;

	case DM_CONFIG_CREATE_BY_HANDLE:	/* these will never be done */
	case DM_CONFIG_LOCK_UPGRADE:
	case DM_CONFIG_PERS_INHERIT_ATTRIBS:
		retval = DM_FALSE;
		break;

	case DM_CONFIG_BULKALL:
		retval = DM_TRUE;
		break;
	case DM_CONFIG_MAX_ATTR_ON_DESTROY:
		retval = DM_MAX_ATTR_BYTES_ON_DESTROY;
		break;

	case DM_CONFIG_MAX_ATTRIBUTE_SIZE:
		retval = ATTR_MAX_VALUELEN;
		break;

	case DM_CONFIG_MAX_HANDLE_SIZE:
		retval = DM_MAX_HANDLE_SIZE;
		break;

	case DM_CONFIG_MAX_MANAGED_REGIONS:
		retval = 1;
		break;

	case DM_CONFIG_TOTAL_ATTRIBUTE_SPACE:
		retval = 0x7fffffff;	/* actually it's unlimited */
		break;

	default:
		return(EINVAL);
	}

	/* Copy the results back to the user. */

	if (copy_to_user( retvalp, &retval, sizeof(retval)))
		return(EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_config_events(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		nelem,
	dm_eventset_t	*eventsetp,
	u_int		*nelemp)
{
	dm_eventset_t	eventset;

	if (nelem == 0)
		return(EINVAL);

	eventset = DM_XFS_SUPPORTED_EVENTS;

	/* Now copy the event mask and event count back to the caller.	We
	   return the lesser of nelem and DM_EVENT_MAX.
	*/

	if (nelem > DM_EVENT_MAX)
		nelem = DM_EVENT_MAX;
	eventset &= (1 << nelem) - 1;

	if (copy_to_user( eventsetp, &eventset, sizeof(eventset)))
		return(EFAULT);

	if (put_user(nelem, nelemp))
		return(EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_destroy_dmattr(
	vnode_t		*vp,
	dm_right_t	right,
	dm_attrname_t	*attrnamep,
	char		**valuepp,
	int		*vlenp)
{
	char		buffer[XFS_BUG_KLUDGE];
	dm_dkattrname_t dkattrname;
	int		alloc_size;
	int		value_len;
	char		*value;
	int		error;

	*vlenp = -1;		/* assume failure by default */

	if (attrnamep->an_chars[0] == '\0')
		return(EINVAL);

	/* Build the on-disk version of the attribute name. */

	strcpy(dkattrname.dan_chars, dmattr_prefix);
	strncpy(&dkattrname.dan_chars[DMATTR_PREFIXLEN],
		(char *)attrnamep->an_chars, DM_ATTR_NAME_SIZE + 1);
	dkattrname.dan_chars[sizeof(dkattrname.dan_chars) - 1] = '\0';

	/* VOP_ATTR_GET will not return anything if the buffer is too small,
	   and we don't know how big to make the buffer, so this may take
	   two tries to get it right.  The initial try must use a buffer of
	   at least XFS_BUG_KLUDGE bytes to prevent buffer overflow because
	   of a bug in XFS.
	*/

	alloc_size = 0;
	value_len = sizeof(buffer);	/* in/out parameter */
	value = buffer;

	VOP_ATTR_GET(vp, dkattrname.dan_chars, value, &value_len,
			ATTR_ROOT, sys_cred, error);

	if (error == ERANGE) {
		alloc_size = value_len;
		value = kmalloc(alloc_size, SLAB_KERNEL);
		if (value == NULL) {
			printk("%s/%d: kmalloc returned NULL\n", __FUNCTION__, __LINE__);
			return(ENOMEM);
		}

		VOP_ATTR_GET(vp, dkattrname.dan_chars, value,
			&value_len, ATTR_ROOT, sys_cred, error);
	}
	if (error) {
		if (alloc_size)
			kfree(value);
		DM_EA_XLATE_ERR(error);
		return(error);
	}

	/* The attribute exists and has a value.  Note that a value_len of
	   zero is valid!
	*/

	if (value_len == 0) {
		*vlenp = 0;
		return(0);
	}

	if (!alloc_size) {
		value = kmalloc(value_len, SLAB_KERNEL);
		if (value == NULL) {
			printk("%s/%d: kmalloc returned NULL\n", __FUNCTION__, __LINE__);
			return(ENOMEM);
		}
		memcpy(value, buffer, value_len);
	} else if (value_len > DM_MAX_ATTR_BYTES_ON_DESTROY) {
		int	value_len2 = DM_MAX_ATTR_BYTES_ON_DESTROY;
		char	*value2;

		value2 = kmalloc(value_len2, SLAB_KERNEL);
		if (value2 == NULL) {
			printk("%s/%d: kmalloc returned NULL\n", __FUNCTION__, __LINE__);
			kfree(value);
			return(ENOMEM);
		}
		memcpy(value2, value, value_len2);
		kfree(value);
		value = value2;
		value_len = value_len2;
	}
	*vlenp = value_len;
	*valuepp = value;
	return(0);
}

/* This code was taken from xfs_fcntl(F_DIOINFO) and modified slightly because
   we don't have a flags parameter (no open file).
   Taken from xfs_ioctl(XFS_IOC_DIOINFO) on Linux.
*/

STATIC int
xfs_dm_get_dioinfo(
	vnode_t		*vp,
	dm_right_t	right,
	dm_dioinfo_t	*diop)
{
	dm_dioinfo_t	dio;
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;
	bhv_desc_t	*xbdp;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	XFS_BHV_LOOKUP(vp, xbdp);

	ip = XFS_BHVTOI(xbdp);
	mp = ip->i_mount;

	/*
	 * this only really needs to be BBSIZE.
	 * it is set to the file system block size to
	 * avoid having to do block zeroing on short writes.
	 */
	dio.d_miniosz = mp->m_sb.sb_blocksize;
	dio.d_maxiosz = MAX_DIO_SIZE(mp);
	dio.d_mem = mp->m_sb.sb_blocksize;

	if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME) {
		dio.d_dio_only = DM_TRUE;
	} else {
		dio.d_dio_only = DM_FALSE;
	}

	if (copy_to_user(diop, &dio, sizeof(dio)))
		return(EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_dirattrs_rvp(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		mask,
	dm_attrloc_t	*locp,
	size_t		buflen,
	void		*bufp,		/* address of buffer in user space */
	size_t		*rlenp,		/* user space address */
	int		*rvp)
{
	xfs_inode_t	*dp;
	xfs_mount_t	*mp;
	size_t		direntbufsz, statbufsz;
	size_t		nread, spaceleft, nwritten=0;
	void		*direntp, *statbufp;
	uint		lock_mode;
	int		error;
	dm_attrloc_t	loc;
	bhv_desc_t	*xbdp;
	bhv_desc_t	*mp_bdp;
	vfs_t		*vfsp = vp->v_vfsp;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	if (copy_from_user( &loc, locp, sizeof(loc)))
		return(EFAULT);

	if ((buflen / DM_STAT_SIZE(dm_stat_t, MAXNAMLEN)) == 0) {
		if (put_user( DM_STAT_SIZE(dm_stat_t, MAXNAMLEN), rlenp ))
			return(EFAULT);
		return(E2BIG);
	}

	mp_bdp = bhv_lookup(VFS_BHVHEAD(vfsp), &xfs_vfsops);
	ASSERT(mp_bdp);
	xbdp = vn_bhv_lookup(VN_BHV_HEAD(vp), &xfs_vnodeops);
	ASSERT(xbdp);

	mp = XFS_BHVTOM(mp_bdp);
	dp = XFS_BHVTOI(xbdp);
	if ((dp->i_d.di_mode & S_IFMT) != S_IFDIR)
		return(ENOTDIR);

	/*
	 * Don't get more dirents than are guaranteed to fit.
	 * The minimum that the stat buf holds is the buf size over
	 * maximum entry size.	That times the minimum dirent size
	 * is an overly conservative size for the dirent buf.
	 */
	statbufsz = NBPP;
	direntbufsz = (NBPP / DM_STAT_SIZE(dm_stat_t, MAXNAMLEN)) * sizeof(xfs_dirent_t);

	direntp = kmem_alloc(direntbufsz, KM_SLEEP);
	statbufp = kmem_alloc(statbufsz, KM_SLEEP);
	error = 0;
	spaceleft = buflen;
	/*
	 * Keep getting dirents until the ubuffer is packed with
	 * dm_stat structures.
	 */
	do {
		ulong	dir_gen = 0;

		lock_mode = xfs_ilock_map_shared(dp);
		/* See if the directory was removed after it was opened. */
		if (dp->i_d.di_nlink <= 0) {
			xfs_iunlock_map_shared(dp, lock_mode);
			error = ENOENT;
			break;
		}
		if (dir_gen == 0)
			dir_gen = dp->i_gen;
		else if (dir_gen != dp->i_gen) {
			/* if dir changed, quit.  May be overzealous... */
			xfs_iunlock_map_shared(dp, lock_mode);
			break;
		}
		error = xfs_get_dirents(dp, direntp, direntbufsz,
						(xfs_off_t *)&loc, &nread);
		xfs_iunlock_map_shared(dp, lock_mode);

		if (error) {
			break;
		}
		if (nread == 0)
			break;
		/*
		 * Now iterate thru them and call bulkstat_one() on all
		 * of them
		 */
		error = xfs_dirents_to_stats(mp,
					  (xfs_dirent_t *) direntp,
					  statbufp,
					  nread,
					  &spaceleft,
					  &nwritten,
					  (xfs_off_t *)&loc);
		if (error) {
			break;
		}

		if (nwritten) {
			if (copy_to_user( bufp, statbufp, nwritten)) {
				error = EFAULT;
				break;
			}
			break;
		}
	} while (spaceleft);
	/*
	 *  If xfs_get_dirents found anything, there might be more to do.
	 *  If it didn't read anything, signal all done (rval == 0).
	 *  (Doesn't matter either way if there was an error.)
	 */
	if (nread) {
		*rvp = 1;
	} else {
		*rvp = 0;
	}

	kmem_free(statbufp, statbufsz);
	kmem_free(direntp, direntbufsz);
	if (!error){
		if (put_user( buflen - spaceleft, rlenp))
			return(EFAULT);
	}

	if (!error && copy_to_user(locp, &loc, sizeof(loc)))
		error = EFAULT;
	return(error);
}


STATIC int
xfs_dm_get_dmattr(
	vnode_t		*vp,
	dm_right_t	right,
	dm_attrname_t	*attrnamep,
	size_t		buflen,
	void		*bufp,
	size_t		*rlenp)
{
	dm_dkattrname_t name;
	char		*value;
	int		value_len;
	int		alloc_size;
	int		error;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	if ((error = xfs_copyin_attrname(attrnamep, &name)) != 0)
		return(error);

	/* Allocate a buffer to receive the attribute's value.	We allocate
	   at least one byte even if the caller specified a buflen of zero.
	   (A buflen of zero is considered valid.)

	   Allocating a minimum of XFS_BUG_KLUDGE bytes temporarily works
	   around a bug within XFS in which in-inode attribute values are not
	   checked to see if they will fit in the buffer before they are
	   copied.  Since no in-core attribute value can be larger than 256
	   bytes (an 8-bit size field), we allocate that minimum size here to
	   prevent buffer overrun in both the kernel's and user's buffers.
	*/

	alloc_size = buflen;
	if (alloc_size < XFS_BUG_KLUDGE)
		alloc_size = XFS_BUG_KLUDGE;
	if (alloc_size > ATTR_MAX_VALUELEN)
		alloc_size = ATTR_MAX_VALUELEN;
	value = kmem_alloc(alloc_size, KM_SLEEP);

	/* Get the attribute's value. */

	value_len = alloc_size;		/* in/out parameter */

	VOP_ATTR_GET(vp, name.dan_chars, value, &value_len,
			ATTR_ROOT, NULL, error);
	DM_EA_XLATE_ERR(error);

	/* DMAPI requires an errno of ENOENT if an attribute does not exist,
	   so remap ENOATTR here.
	*/

	if (error == ENOATTR)
		error = ENOENT;
	if (!error && value_len > buflen)
		error = E2BIG;
	if (!error && copy_to_user(bufp, value, value_len))
		error = EFAULT;
	if (!error || error == E2BIG) {
		if (put_user(value_len, rlenp))
			error = EFAULT;
	}

	kmem_free(value, alloc_size);
	return(error);
}

STATIC int
xfs_dm_get_eventlist(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		type,
	u_int		nelem,
	dm_eventset_t	*eventsetp,
	u_int		*nelemp)
{
	int		error;
	bhv_desc_t	*xbdp;

	XFS_BHV_LOOKUP(vp, xbdp);

	if (type == DM_FSYS_OBJ) {
		error = xfs_dm_fs_get_eventlist(xbdp, right, nelem,
			eventsetp, nelemp);
	} else {
		error = xfs_dm_f_get_eventlist(xbdp, right, nelem,
			eventsetp, nelemp);
	}
	return(error);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_fileattr(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		mask,		/* not used; always return everything */
	dm_stat_t	*statp)
{
	dm_stat_t	stat;
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;
	bhv_desc_t	*xbdp;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	XFS_BHV_LOOKUP(vp, xbdp);

	/* Find the mount point. */

	ip = XFS_BHVTOI(xbdp);
	mp = ip->i_mount;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	xfs_ip_to_stat(mp, &stat, ip);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (copy_to_user( statp, &stat, sizeof(stat)))
		return(EFAULT);
	return(0);
}


/* We currently only support a maximum of one managed region per file, and
   use the DM_EVENT_READ, DM_EVENT_WRITE, and DM_EVENT_TRUNCATE events in
   the file's dm_eventset_t event mask to implement the DM_REGION_READ,
   DM_REGION_WRITE, and DM_REGION_TRUNCATE flags for that single region.
*/

STATIC int
xfs_dm_get_region(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		nelem,
	dm_region_t	*regbufp,
	u_int		*nelemp)
{
	dm_eventset_t	evmask;
	dm_region_t	region;
	xfs_inode_t	*ip;
	u_int		elem;
	bhv_desc_t	*xbdp;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	XFS_BHV_LOOKUP(vp, xbdp);

	ip = XFS_BHVTOI(xbdp);
	evmask = ip->i_d.di_dmevmask;	/* read the mask "atomically" */

	/* Get the file's current managed region flags out of the
	   dm_eventset_t mask and use them to build a managed region that
	   covers the entire file, i.e. set rg_offset and rg_size to zero.
	*/

	memset((char *)&region, 0, sizeof(region));

	if (evmask & (1 << DM_EVENT_READ))
		region.rg_flags |= DM_REGION_READ;
	if (evmask & (1 << DM_EVENT_WRITE))
		region.rg_flags |= DM_REGION_WRITE;
	if (evmask & (1 << DM_EVENT_TRUNCATE))
		region.rg_flags |= DM_REGION_TRUNCATE;

	elem = (region.rg_flags ? 1 : 0);

	if (copy_to_user( nelemp, &elem, sizeof(elem)))
		return(EFAULT);
	if (elem > nelem)
		return(E2BIG);
	if (elem && copy_to_user(regbufp, &region, sizeof(region)))
		return(EFAULT);
	return(0);
}


STATIC int
xfs_dm_getall_dmattr(
	vnode_t		*vp,
	dm_right_t	right,
	size_t		buflen,
	void		*bufp,
	size_t		*rlenp)
{
	attrlist_cursor_kern_t cursor;
	attrlist_t	*attrlist;
	dm_attrlist_t	*ulist;
	int		*last_link;
	int		alignment;
	int		total_size;
	int		list_size = 8192;	/* should be big enough */
	int		error;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	/* Verify that the user gave us a buffer that is 4-byte aligned, lock
	   it down, and work directly within that buffer.  As a side-effect,
	   values of buflen < sizeof(int) return EINVAL.
	*/

	alignment = sizeof(int) - 1;
	if (((__psint_t)bufp & alignment) != 0) {
		return(EFAULT);
	}
	buflen &= ~alignment;		/* round down the alignment */

#if defined(HAVE_USERACC)
	if ((error = useracc(bufp, buflen, B_READ, NULL)) != 0)
		return error;
#endif

	/* Initialize all the structures and variables for the main loop. */

	memset(&cursor, 0, sizeof(cursor));
	attrlist = (attrlist_t *)kmem_alloc(list_size, KM_SLEEP);
	total_size = 0;
	ulist = (dm_attrlist_t *)bufp;
	last_link = NULL;

	/* Use VOP_ATTR_LIST to get the names of DMAPI attributes, and use
	   VOP_ATTR_GET to get their values.  There is a risk here that the
	   DMAPI attributes could change between the VOP_ATTR_LIST and
	   VOP_ATTR_GET calls.	If we can detect it, we return EIO to notify
	   the user.
	*/

	do {
		int	i;

		/* Get a buffer full of attribute names.  If there aren't any
		   more or if we encounter an error, then finish up.
		*/

		VOP_ATTR_LIST(vp, (char *)attrlist, list_size,
			ATTR_ROOT, &cursor, NULL, error);
		DM_EA_XLATE_ERR(error);

		if (error || attrlist->al_count == 0)
			break;

		for (i = 0; i < attrlist->al_count; i++) {
			attrlist_ent_t	*entry;
			char		*user_name;
			int		size_needed;
			int		value_len;

			/* Skip over all non-DMAPI attributes.	If the
			   attribute name is too long, we assume it is
			   non-DMAPI even if it starts with the correct
			   prefix.
			*/

			entry = ATTR_ENTRY(attrlist, i);
			if (strncmp(entry->a_name, dmattr_prefix, DMATTR_PREFIXLEN))
				continue;
			user_name = &entry->a_name[DMATTR_PREFIXLEN];
			if (strlen(user_name) > DM_ATTR_NAME_SIZE)
				continue;

			/* We have a valid DMAPI attribute to return.  If it
			   won't fit in the user's buffer, we still need to
			   keep track of the number of bytes for the user's
			   next call.
			*/


			size_needed = sizeof(*ulist) + entry->a_valuelen;
			size_needed = (size_needed + alignment) & ~alignment;

			total_size += size_needed;
			if (total_size > buflen)
				continue;

			/* Start by filling in all the fields in the
			   dm_attrlist_t structure.
			*/

			strncpy((char *)ulist->al_name.an_chars, user_name,
				DM_ATTR_NAME_SIZE);
			ulist->al_data.vd_offset = sizeof(*ulist);
			ulist->al_data.vd_length = entry->a_valuelen;
			ulist->_link =	size_needed;
			last_link = &ulist->_link;

			/* Next read the attribute's value into its correct
			   location after the dm_attrlist structure.  Any sort
			   of error indicates that the data is moving under us,
			   so we return EIO to let the user know.
			*/

			value_len = entry->a_valuelen;

			VOP_ATTR_GET(vp, entry->a_name,
				(void *)(ulist + 1), &value_len,
				ATTR_ROOT, NULL, error);
			DM_EA_XLATE_ERR(error);

			if (error || value_len != entry->a_valuelen) {
				error = EIO;
				break;
			}

			ulist = (dm_attrlist_t *)((char *)ulist + ulist->_link);
		}
	} while (!error && attrlist->al_more);
	if (last_link)
		*last_link = 0;

	if (!error && total_size > buflen)
		error = E2BIG;
	if (!error || error == E2BIG) {
		if (put_user(total_size, rlenp))
			error = EFAULT;
	}

	kmem_free(attrlist, list_size);
	return(error);
}


/* ARGSUSED */
STATIC int
xfs_dm_getall_inherit(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		nelem,
	dm_inherit_t	*inheritbufp,
	u_int		*nelemp)
{
	return(ENOSYS);
}


/* Initialize location pointer for subsequent dm_get_dirattrs,
   dm_get_bulkattr, and dm_get_bulkall calls.  The same initialization must
   work for vnode-based routines (dm_get_dirattrs) and filesystem-based
   routines (dm_get_bulkattr and dm_get_bulkall).  Filesystem-based functions
   call this routine using the filesystem's root vnode.
*/

/* ARGSUSED */
STATIC int
xfs_dm_init_attrloc(
	vnode_t		*vp,
	dm_right_t	right,
	dm_attrloc_t	*locp)
{
	dm_attrloc_t	loc = 0;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	if (copy_to_user( locp, &loc, sizeof(loc)))
		return(EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_mkdir_by_handle(
	vnode_t		*vp,
	dm_right_t	right,
	void		*hanp,
	size_t		hlen,
	char		*cname)
{
	return(ENOSYS);
}


/* ARGSUSED */
STATIC int
xfs_dm_probe_hole(
	vnode_t		*vp,
	dm_right_t	right,
	dm_off_t	off,
	dm_size_t	len,		/* we ignore this for now */
	dm_off_t	*roffp,
	dm_size_t	*rlenp)
{
	dm_off_t	roff;
	dm_size_t	rlen;
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;
	uint		lock_flags;
	xfs_fsize_t	realsize;
	u_int		bsize;
	bhv_desc_t	*xbdp;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	XFS_BHV_LOOKUP(vp, xbdp);

	ip = XFS_BHVTOI(xbdp);
	if ((ip->i_d.di_mode & S_IFMT) != S_IFREG)
		return(EINVAL);

	mp = ip->i_mount;
	bsize = mp->m_sb.sb_blocksize;

	lock_flags = XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL;
	xfs_ilock(ip, lock_flags);
	realsize = ip->i_d.di_size;
	xfs_iunlock(ip, lock_flags);
	if (off >= realsize)
		return(E2BIG);

	roff = (off + bsize-1) & ~(bsize-1);
	rlen = 0;		/* Only support punches to EOF for now */
	if (copy_to_user( roffp, &roff, sizeof(roff)))
		return(EFAULT);
	if (copy_to_user( rlenp, &rlen, sizeof(rlen)))
		return(EFAULT);
	return(0);
}


STATIC int
xfs_dm_punch_hole(
	vnode_t		*vp,
	dm_right_t	right,
	dm_off_t	off,
	dm_size_t	len)
{
	xfs_inode_t	*ip;
	xfs_trans_t	*tp;
	xfs_trans_t	*tp2;
	xfs_mount_t	*mp;
	int		error;
	uint		lock_flags;
	uint		commit_flags;
	xfs_fsize_t	realsize;
	u_int		bsize;
	bhv_desc_t	*xbdp;

	if (right < DM_RIGHT_EXCL)
		return(EACCES);

	if (vp->v_type != VREG)
		return(EINVAL);
	if (len != 0)	/* Only support punches to EOF for now */
		return(EAGAIN);
	if (VN_MAPPED(vp))
		return(EBUSY);

	XFS_BHV_LOOKUP(vp, xbdp);

	ip = XFS_BHVTOI(xbdp);
	mp = ip->i_mount;
	bsize = mp->m_sb.sb_blocksize;

	if (off & (bsize-1))
		return(EAGAIN);

	lock_flags = XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL;
	xfs_ilock(ip, lock_flags);

	realsize = ip->i_d.di_size;	/* saved size to restore to */
	if (off >= realsize) {	/* also check block boundary */
		xfs_iunlock(ip, lock_flags);
		return(EINVAL);
	}

	/*
	 * Before we join the inode to the transaction, take care of
	 * the part of the truncation that must be done without the
	 * inode lock.	This needs to be done before joining the inode
	 * to the transaction, because the inode cannot be unlocked
	 * once it is a part of the transaction.
	 */
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	tp = xfs_trans_alloc(mp, XFS_TRANS_SETATTR_SIZE);
	if ((error = xfs_trans_reserve(tp, 0,
				     XFS_ITRUNCATE_LOG_RES(mp), 0,
				     XFS_TRANS_PERM_LOG_RES,
				     XFS_ITRUNCATE_LOG_COUNT))) {
		xfs_trans_cancel(tp, 0);
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
		return(error);
	}
	commit_flags = XFS_TRANS_RELEASE_LOG_RES;
	/* --- start of truncate --- */
	xfs_itruncate_start(ip, XFS_ITRUNC_DEFINITE, (xfs_fsize_t) off);
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, lock_flags);
	xfs_trans_ihold(tp, ip);
	xfs_itruncate_finish(&tp, ip, (xfs_fsize_t) off, XFS_DATA_FORK, 0);

	/*
	 * If this is a synchronous mount, make sure that the
	 * transaction goes to disk before returning to the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(tp);
	}
	xfs_trans_commit(tp, commit_flags, NULL);
	/* --- end of truncate --- */

	/* --- start of grow --- */
	tp2 = xfs_trans_alloc(mp, XFS_TRANS_SETATTR_SIZE);
	if ((error = xfs_trans_reserve(tp2, 0,
				     XFS_ITRUNCATE_LOG_RES(mp), 0,
				     XFS_TRANS_PERM_LOG_RES,
				     XFS_ITRUNCATE_LOG_COUNT))) {
		xfs_trans_cancel(tp, 0);
		xfs_trans_cancel(tp2, 0);
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
		return(error);
	}
	/* ip left locked after previous commit */
	xfs_igrow_start(ip, realsize, NULL);
	xfs_trans_ijoin(tp2, ip, lock_flags);
	xfs_igrow_finish(tp2, ip, realsize, 0);

	/* Let threads in send_data_event know we punched the file. */
	ip->i_iocore.io_dmstate++;

	VN_HOLD(vp);
	if (mp->m_flags & XFS_MOUNT_WSYNC) {
		xfs_trans_set_sync(tp2);
	}
	xfs_trans_commit(tp2, commit_flags, NULL);
	/* --- end of grow --- */

	/* ip unlocked during the commit */
	return(0);
}


STATIC int
xfs_dm_read_invis_rvp(
	vnode_t		*vp,
	dm_right_t	right,
	dm_off_t	off,
	dm_size_t	len,
	void		*bufp,
	int		*rvp)
{
	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	return(xfs_dm_rdwr(vp, 0, FMODE_READ, off, len, bufp, rvp));
}


/* ARGSUSED */
STATIC int
xfs_dm_release_right(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		type)		/* DM_FSYS_OBJ or zero */
{
#ifdef	DEBUG_RIGHTS
	char		buffer[sizeof(xfs_handle_t) * 2 + 1];
	bhv_desc_t	*bdp;

	XFS_BHV_LOOKUP(vp, bdp);

	if (!xfs_bdp_to_hexhandle(bdp, type, buffer)) {
		printf("dm_release_right: old %d type %d handle %s\n",
			right, type, buffer);
	} else {
		printf("dm_release_right: old %d type %d handle "
			" <INVALID>\n", right, type);
	}
#endif	/* DEBUG_RIGHTS */
	return(0);
}


STATIC int
xfs_dm_remove_dmattr(
	vnode_t		*vp,
	dm_right_t	right,
	int		setdtime,
	dm_attrname_t	*attrnamep)
{
	dm_dkattrname_t name;
	int		error;

	if (right < DM_RIGHT_EXCL)
		return(EACCES);

	if ((error = xfs_copyin_attrname(attrnamep, &name)) != 0)
		return(error);

	/* Remove the attribute from the object. */

	VOP_ATTR_REMOVE(vp, name.dan_chars,
			(setdtime ? ATTR_ROOT : ATTR_ROOT|ATTR_KERNOTIME),
			NULL, error);
	DM_EA_XLATE_ERR(error);

	if (error == ENOATTR)
		error = ENOENT;
	return(error);
}


/* ARGSUSED */
STATIC int
xfs_dm_request_right(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		type,		/* DM_FSYS_OBJ or zero */
	u_int		flags,
	dm_right_t	newright)
{
#ifdef	DEBUG_RIGHTS
	char		buffer[sizeof(xfs_handle_t) * 2 + 1];
	bhv_desc_t	*bdp;

	XFS_BHV_LOOKUP(vp, bdp);

	if (!xfs_bdp_to_hexhandle(bdp, type, buffer)) {
		printf("dm_request_right: old %d new %d type %d flags 0x%x "
			"handle %s\n", right, newright, type, flags, buffer);
	} else {
		printf("dm_request_right: old %d new %d type %d flags 0x%x "
			"handle <INVALID>\n", right, newright, type, flags);
	}
#endif	/* DEBUG_RIGHTS */
	return(0);
}


STATIC int
xfs_dm_set_dmattr(
	vnode_t		*vp,
	dm_right_t	right,
	dm_attrname_t	*attrnamep,
	int		setdtime,
	size_t		buflen,
	void		*bufp)
{
	dm_dkattrname_t name;
	char		*value;
	int		alloc_size;
	int		error;

	if (right < DM_RIGHT_EXCL)
		return(EACCES);

	if ((error = xfs_copyin_attrname(attrnamep, &name)) != 0)
		return(error);
	if (buflen > ATTR_MAX_VALUELEN)
		return(E2BIG);

	/* Copy in the attribute's value and store the <name,value> pair in
	   the object.	We allocate a buffer of at least one byte even if the
	   caller specified a buflen of zero.  (A buflen of zero is considered
	   valid.)
	*/

	alloc_size = (buflen == 0) ? 1 : buflen;
	value = kmem_alloc(alloc_size, KM_SLEEP);
	if (copy_from_user( value, bufp, buflen)) {
		error = EFAULT;
	} else {
		VOP_ATTR_SET(vp, name.dan_chars, value, buflen,
			(setdtime ? ATTR_ROOT : ATTR_ROOT|ATTR_KERNOTIME),
			NULL, error);
		DM_EA_XLATE_ERR(error);
	}
	kmem_free(value, alloc_size);
	return(error);
}

STATIC int
xfs_dm_set_eventlist(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		type,
	dm_eventset_t	*eventsetp,	/* in kernel space! */
	u_int		maxevent)
{
	int		error;
	bhv_desc_t	*xbdp;

	XFS_BHV_LOOKUP(vp, xbdp);

	if (type == DM_FSYS_OBJ) {
		error = xfs_dm_fs_set_eventlist(xbdp, right, eventsetp, maxevent);
	} else {
		error = xfs_dm_f_set_eventlist(xbdp, right, eventsetp, maxevent);
	}
	return(error);
}


/*
 *  This turned out not XFS-specific, but leave it here with get_fileattr.
 */

STATIC int
xfs_dm_set_fileattr(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		mask,
	dm_fileattr_t	*statp)
{
	dm_fileattr_t	stat;
	vattr_t		vat;
	int		error;

	if (right < DM_RIGHT_EXCL)
		return(EACCES);

	if (copy_from_user( &stat, statp, sizeof(stat)))
		return(EFAULT);

	vat.va_mask = 0;

	if (mask & DM_AT_MODE) {
		vat.va_mask |= XFS_AT_MODE;
		vat.va_mode = stat.fa_mode;
	}
	if (mask & DM_AT_UID) {
		vat.va_mask |= XFS_AT_UID;
		vat.va_uid = stat.fa_uid;
	}
	if (mask & DM_AT_GID) {
		vat.va_mask |= XFS_AT_GID;
		vat.va_gid = stat.fa_gid;
	}
	if (mask & DM_AT_ATIME) {
		vat.va_mask |= XFS_AT_ATIME;
		vat.va_atime.tv_sec = stat.fa_atime;
		vat.va_atime.tv_nsec = 0;
	}
	if (mask & DM_AT_MTIME) {
		vat.va_mask |= XFS_AT_MTIME;
		vat.va_mtime.tv_sec = stat.fa_mtime;
		vat.va_mtime.tv_nsec = 0;
	}
	if (mask & DM_AT_CTIME) {
		vat.va_mask |= XFS_AT_CTIME;
		vat.va_ctime.tv_sec = stat.fa_ctime;
		vat.va_ctime.tv_nsec = 0;
	}

	/* DM_AT_DTIME only takes effect if DM_AT_CTIME is not specified.  We
	   overload ctime to also act as dtime, i.e. DM_CONFIG_DTIME_OVERLOAD.
	*/

	if ((mask & DM_AT_DTIME) && !(mask & DM_AT_CTIME)) {
		vat.va_mask |= XFS_AT_CTIME;
		vat.va_ctime.tv_sec = stat.fa_dtime;
		vat.va_ctime.tv_nsec = 0;
	}
	if (mask & DM_AT_SIZE) {
		vat.va_mask |= XFS_AT_SIZE;
		vat.va_size = stat.fa_size;
	}

	VOP_SETATTR(vp, &vat, ATTR_DMI, NULL, error);
	return(error);
}


/* ARGSUSED */
STATIC int
xfs_dm_set_inherit(
	vnode_t		*vp,
	dm_right_t	right,
	dm_attrname_t	*attrnamep,
	mode_t		mode)
{
	return(ENOSYS);
}


STATIC int
xfs_dm_set_region(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		nelem,
	dm_region_t	*regbufp,
	dm_boolean_t	*exactflagp)
{
	xfs_inode_t	*ip;
	xfs_trans_t	*tp;
	xfs_mount_t	*mp;
	dm_region_t	region;
	dm_eventset_t	new_mask;
	dm_eventset_t	mr_mask;
	int		error;
	u_int		exactflag;
	bhv_desc_t	*xbdp;

	if (right < DM_RIGHT_EXCL)
		return(EACCES);

	/* If the caller gave us more than one dm_region_t structure, complain.
	   (He has to call dm_get_config() to find out what our limit is.)
	*/

	if (nelem > 1)
		return(E2BIG);

	/* If the user provided a dm_region_t structure, then copy it in,
	   validate it, and convert its flags to the corresponding bits in a
	   dm_set_eventlist() event mask.  A call with zero regions is
	   equivalent to clearing all region flags.
	*/

	new_mask = 0;
	if (nelem == 1) {
		if (copy_from_user( &region, regbufp, sizeof(region)))
			return(EFAULT);

		if (region.rg_flags & ~(DM_REGION_READ|DM_REGION_WRITE|DM_REGION_TRUNCATE))
			return(EINVAL);
		if (region.rg_flags & DM_REGION_READ)
			new_mask |= 1 << DM_EVENT_READ;
		if (region.rg_flags & DM_REGION_WRITE)
			new_mask |= 1 << DM_EVENT_WRITE;
		if (region.rg_flags & DM_REGION_TRUNCATE)
			new_mask |= 1 << DM_EVENT_TRUNCATE;
	}
	if ((new_mask & prohibited_mr_events(vp)) != 0)
		return(EBUSY);
	mr_mask = (1 << DM_EVENT_READ) | (1 << DM_EVENT_WRITE) | (1 << DM_EVENT_TRUNCATE);

	/* Get the file's existing event mask, clear the old managed region
	   bits, add in the new ones, and update the file's mask.
	*/

	XFS_BHV_LOOKUP(vp, xbdp);

	ip = XFS_BHVTOI(xbdp);
	mp = ip->i_mount;
	tp = xfs_trans_alloc(mp, XFS_TRANS_SET_DMATTRS);
	error = xfs_trans_reserve(tp, 0, XFS_ICHANGE_LOG_RES (mp), 0, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return(error);
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);

	ip->i_d.di_dmevmask = (ip->i_d.di_dmevmask & ~mr_mask) | new_mask;
	ip->i_iocore.io_dmevmask = ip->i_d.di_dmevmask;

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	VN_HOLD(vp);
	xfs_trans_commit(tp, 0, NULL);

	/* Return the proper value for *exactflagp depending upon whether or not
	   we "changed" the user's managed region.  In other words, if the user
	   specified a non-zero value for either rg_offset or rg_size, we
	   round each of those values back to zero.
	*/

	if (nelem && (region.rg_offset || region.rg_size)) {
		exactflag = DM_FALSE;	/* user region was changed */
	} else {
		exactflag = DM_TRUE;	/* user region was unchanged */
	}
	if (copy_to_user( exactflagp, &exactflag, sizeof(exactflag)))
		return(EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_symlink_by_handle(
	vnode_t		*vp,
	dm_right_t	right,
	void		*hanp,
	size_t		hlen,
	char		*cname,
	char		*path)
{
	return(ENOSYS);
}


STATIC int
xfs_dm_sync_by_handle (
	vnode_t		*vp,
	dm_right_t	right)
{
	int		error;

	if (right < DM_RIGHT_EXCL)
		return(EACCES);

	VOP_FSYNC(vp, FSYNC_WAIT, NULL, (xfs_off_t)0, (xfs_off_t)-1, error);
	return(error);
}


/* ARGSUSED */
STATIC int
xfs_dm_upgrade_right(
	vnode_t		*vp,
	dm_right_t	right,
	u_int		type)		/* DM_FSYS_OBJ or zero */
{
#ifdef	DEBUG_RIGHTS
	char		buffer[sizeof(xfs_handle_t) * 2 + 1];
	bhv_desc_t	*bdp;

	XFS_BHV_LOOKUP(vp, bdp);

	if (!xfs_bdp_to_hexhandle(bdp, type, buffer)) {
		printf("dm_upgrade_right: old %d new %d type %d handle %s\n",
			right, DM_RIGHT_EXCL, type, buffer);
	} else {
		printf("dm_upgrade_right: old %d new %d type %d handle "
			"<INVALID>\n", right, DM_RIGHT_EXCL, type);
	}
#endif	/* DEBUG_RIGHTS */
	return(0);
}


STATIC int
xfs_dm_write_invis_rvp(
	vnode_t		*vp,
	dm_right_t	right,
	int		flags,
	dm_off_t	off,
	dm_size_t	len,
	void		*bufp,
	int		*rvp)
{
	int		fflag = 0;

	if (right < DM_RIGHT_EXCL)
		return(EACCES);

	if (flags & DM_WRITE_SYNC)
		fflag |= O_SYNC;
	return(xfs_dm_rdwr(vp, fflag, FMODE_WRITE, off, len, bufp, rvp));
}


STATIC void
xfs_dm_obj_ref_hold(
	vnode_t		*vp)
{
	VN_HOLD(vp);
}


STATIC fsys_function_vector_t	xfs_fsys_vector[DM_FSYS_MAX];


int
xfs_dm_get_fsys_vector(
	bhv_desc_t	*bdp,
	caddr_t		addr)
{
	static	int		initialized = 0;
	dm_fcntl_vector_t	*vecrq;
	fsys_function_vector_t	*vecp;
	int			i = 0;

	vecrq = (dm_fcntl_vector_t *)addr;
	vecrq->count =
		sizeof(xfs_fsys_vector) / sizeof(xfs_fsys_vector[0]);
	vecrq->vecp = xfs_fsys_vector;
	if (initialized)
		return(0);
	vecrq->code_level = DM_CLVL_XOPEN;
	vecp = xfs_fsys_vector;

	vecp[i].func_no = DM_FSYS_CLEAR_INHERIT;
	vecp[i++].u_fc.clear_inherit = xfs_dm_clear_inherit;
	vecp[i].func_no = DM_FSYS_CREATE_BY_HANDLE;
	vecp[i++].u_fc.create_by_handle = xfs_dm_create_by_handle;
	vecp[i].func_no = DM_FSYS_DOWNGRADE_RIGHT;
	vecp[i++].u_fc.downgrade_right = xfs_dm_downgrade_right;
	vecp[i].func_no = DM_FSYS_GET_ALLOCINFO_RVP;
	vecp[i++].u_fc.get_allocinfo_rvp = xfs_dm_get_allocinfo_rvp;
	vecp[i].func_no = DM_FSYS_GET_BULKALL_RVP;
	vecp[i++].u_fc.get_bulkall_rvp = xfs_dm_get_bulkall_rvp;
	vecp[i].func_no = DM_FSYS_GET_BULKATTR_RVP;
	vecp[i++].u_fc.get_bulkattr_rvp = xfs_dm_get_bulkattr_rvp;
	vecp[i].func_no = DM_FSYS_GET_CONFIG;
	vecp[i++].u_fc.get_config = xfs_dm_get_config;
	vecp[i].func_no = DM_FSYS_GET_CONFIG_EVENTS;
	vecp[i++].u_fc.get_config_events = xfs_dm_get_config_events;
	vecp[i].func_no = DM_FSYS_GET_DESTROY_DMATTR;
	vecp[i++].u_fc.get_destroy_dmattr = xfs_dm_get_destroy_dmattr;
	vecp[i].func_no = DM_FSYS_GET_DIOINFO;
	vecp[i++].u_fc.get_dioinfo = xfs_dm_get_dioinfo;
	vecp[i].func_no = DM_FSYS_GET_DIRATTRS_RVP;
	vecp[i++].u_fc.get_dirattrs_rvp = xfs_dm_get_dirattrs_rvp;
	vecp[i].func_no = DM_FSYS_GET_DMATTR;
	vecp[i++].u_fc.get_dmattr = xfs_dm_get_dmattr;
	vecp[i].func_no = DM_FSYS_GET_EVENTLIST;
	vecp[i++].u_fc.get_eventlist = xfs_dm_get_eventlist;
	vecp[i].func_no = DM_FSYS_GET_FILEATTR;
	vecp[i++].u_fc.get_fileattr = xfs_dm_get_fileattr;
	vecp[i].func_no = DM_FSYS_GET_REGION;
	vecp[i++].u_fc.get_region = xfs_dm_get_region;
	vecp[i].func_no = DM_FSYS_GETALL_DMATTR;
	vecp[i++].u_fc.getall_dmattr = xfs_dm_getall_dmattr;
	vecp[i].func_no = DM_FSYS_GETALL_INHERIT;
	vecp[i++].u_fc.getall_inherit = xfs_dm_getall_inherit;
	vecp[i].func_no = DM_FSYS_INIT_ATTRLOC;
	vecp[i++].u_fc.init_attrloc = xfs_dm_init_attrloc;
	vecp[i].func_no = DM_FSYS_MKDIR_BY_HANDLE;
	vecp[i++].u_fc.mkdir_by_handle = xfs_dm_mkdir_by_handle;
	vecp[i].func_no = DM_FSYS_PROBE_HOLE;
	vecp[i++].u_fc.probe_hole = xfs_dm_probe_hole;
	vecp[i].func_no = DM_FSYS_PUNCH_HOLE;
	vecp[i++].u_fc.punch_hole = xfs_dm_punch_hole;
	vecp[i].func_no = DM_FSYS_READ_INVIS_RVP;
	vecp[i++].u_fc.read_invis_rvp = xfs_dm_read_invis_rvp;
	vecp[i].func_no = DM_FSYS_RELEASE_RIGHT;
	vecp[i++].u_fc.release_right = xfs_dm_release_right;
	vecp[i].func_no = DM_FSYS_REMOVE_DMATTR;
	vecp[i++].u_fc.remove_dmattr = xfs_dm_remove_dmattr;
	vecp[i].func_no = DM_FSYS_REQUEST_RIGHT;
	vecp[i++].u_fc.request_right = xfs_dm_request_right;
	vecp[i].func_no = DM_FSYS_SET_DMATTR;
	vecp[i++].u_fc.set_dmattr = xfs_dm_set_dmattr;
	vecp[i].func_no = DM_FSYS_SET_EVENTLIST;
	vecp[i++].u_fc.set_eventlist = xfs_dm_set_eventlist;
	vecp[i].func_no = DM_FSYS_SET_FILEATTR;
	vecp[i++].u_fc.set_fileattr = xfs_dm_set_fileattr;
	vecp[i].func_no = DM_FSYS_SET_INHERIT;
	vecp[i++].u_fc.set_inherit = xfs_dm_set_inherit;
	vecp[i].func_no = DM_FSYS_SET_REGION;
	vecp[i++].u_fc.set_region = xfs_dm_set_region;
	vecp[i].func_no = DM_FSYS_SYMLINK_BY_HANDLE;
	vecp[i++].u_fc.symlink_by_handle = xfs_dm_symlink_by_handle;
	vecp[i].func_no = DM_FSYS_SYNC_BY_HANDLE;
	vecp[i++].u_fc.sync_by_handle = xfs_dm_sync_by_handle;
	vecp[i].func_no = DM_FSYS_UPGRADE_RIGHT;
	vecp[i++].u_fc.upgrade_right = xfs_dm_upgrade_right;
	vecp[i].func_no = DM_FSYS_WRITE_INVIS_RVP;
	vecp[i++].u_fc.write_invis_rvp = xfs_dm_write_invis_rvp;
	vecp[i].func_no = DM_FSYS_OBJ_REF_HOLD;
	vecp[i++].u_fc.obj_ref_hold = xfs_dm_obj_ref_hold;

	return(0);
}


/*	xfs_dm_mapevent - send events needed for memory mapping a file.
 *
 *	xfs_dm_map is a workaround called for files that are about to be
 *	mapped.	 DMAPI events are not being generated at a low enough level
 *	in the kernel for page reads/writes to generate the correct events.
 *	So for memory-mapped files we generate read  or write events for the
 *	whole byte range being mapped.	If the mmap call can never cause a
 *	write to the file, then only a read event is sent.
 *
 *	Code elsewhere prevents adding managed regions to a file while it
 *	is still mapped.
 */

/* ARGSUSED */
static int
xfs_dm_mapevent(
	bhv_desc_t	*bdp,
	int		flags,
	xfs_off_t	offset,
	dm_fcntl_mapevent_t *mapevp)
{
	xfs_fsize_t	filesize;		/* event read/write "size" */
	xfs_inode_t	*ip;
	xfs_off_t	end_of_area, evsize;
	vnode_t		*vp = BHV_TO_VNODE(bdp);
	struct vfs	*vfsp = vp->v_vfsp;

	/* exit immediately if not regular file in a DMAPI file system */

	mapevp->error = 0;			/* assume success */

	if ((vp->v_type != VREG) || !(vfsp->vfs_flag & VFS_DMI))
		return 0;

	if (mapevp->max_event != DM_EVENT_WRITE &&
		mapevp->max_event != DM_EVENT_READ)
			return 0;

	/* Set file size to work with. */

	ip = XFS_BHVTOI(bdp);
	filesize = ip->i_iocore.io_new_size;
	if (filesize < ip->i_d.di_size) {
		filesize = ip->i_d.di_size;
	}

	/* Set first byte number beyond the map area. */

	if (mapevp->length) {
		end_of_area = offset + mapevp->length;
		if (end_of_area > filesize)
			end_of_area = filesize;
	} else {
		end_of_area = filesize;
	}

	/* Set the real amount being mapped. */
	evsize = end_of_area - offset;
	if (evsize < 0)
		evsize = 0;

	/* If write possible, try a DMAPI write event */
	if (mapevp->max_event == DM_EVENT_WRITE &&
		DM_EVENT_ENABLED (vp->v_vfsp, ip, DM_EVENT_WRITE)) {
		mapevp->error = xfs_dm_send_data_event(DM_EVENT_WRITE, vp,
				offset, evsize, 0, NULL);
		return(0);
	}

	/* Try a read event if max_event was != DM_EVENT_WRITE or if it
	 * was DM_EVENT_WRITE but the WRITE event was not enabled.
	 */
	if (DM_EVENT_ENABLED (vp->v_vfsp, ip, DM_EVENT_READ)) {
		mapevp->error = xfs_dm_send_data_event(DM_EVENT_READ, vp,
				offset, evsize, 0, NULL);
	}

	return 0;
}


int
xfs_dm_send_mmap_event(
	struct vm_area_struct *vma,
	unsigned int	wantflag)
{
	vnode_t		*vp;
	xfs_inode_t	*ip;
	bhv_desc_t	*bdp;
	int		ret = 0;
	dm_fcntl_mapevent_t maprq;
	dm_eventtype_t	max_event = DM_EVENT_READ;

	if (!vma->vm_file)
		return 0;

	vp = LINVFS_GET_VP(vma->vm_file->f_dentry->d_inode);
	ASSERT(vp);

	if ((vp->v_type != VREG) || !(vp->v_vfsp->vfs_flag & VFS_DMI))
		return 0;

	/* If they specifically asked for 'read', then give it to them.
	 * Otherwise, see if it's possible to give them 'write'.
	 */
	if( wantflag & VM_READ ){
		max_event = DM_EVENT_READ;
	}
	else if( ! (vma->vm_flags & VM_DENYWRITE) ) {
		if((wantflag & VM_WRITE) || (vma->vm_flags & VM_WRITE))
			max_event = DM_EVENT_WRITE;
	}

	if( (wantflag & VM_WRITE) && (max_event != DM_EVENT_WRITE) ){
		return -EACCES;
	}

	maprq.max_event = max_event;

	/* Figure out how much of the file is being requested by the user. */
	maprq.length = 0; /* whole file, for now */

	XFS_BHV_LOOKUP(vp, bdp);
	ip = XFS_BHVTOI(bdp);

	if(DM_EVENT_ENABLED(vp->v_vfsp, ip, max_event)){
		xfs_dm_mapevent(bdp, 0, 0, &maprq);
		ret = maprq.error;
	}

	return -ret;
}


/*
 * Data migration operations accessed by the rest of XFS.
 * When DMAPI support is configured in, this vector is used.
 */

xfs_dmops_t	xfs_dmcore_xfs = {
	.xfs_send_data		= xfs_dm_send_data_event,
	.xfs_send_mmap		= xfs_dm_send_mmap_event,
	.xfs_send_destroy	= dm_send_destroy_event,
	.xfs_send_namesp	= dm_send_namesp_event,
	.xfs_send_unmount	= dm_send_unmount_event,
};


/*
 * DMAPI behavior module routines
 */

STATIC int
xfs_dm_mount(
	struct bhv_desc		*bhv,
	struct xfs_mount_args	*args,
	struct cred		*cr)
{
	struct bhv_desc		*rootbdp;
	struct vnode		*rootvp;
	struct vfs		*vfsp = bhvtovfs(bhv);
	int			error = 0;

	PVFS_MOUNT(BHV_NEXT(bhv), args, cr, error);
	if (error)
		return error;

	if (args->flags & XFSMNT_DMAPI) {
		VFS_ROOT(vfsp, &rootvp, error);
		if (!error) {
			rootbdp = vn_bhv_lookup_unlocked(
					VN_BHV_HEAD(rootvp), &xfs_vnodeops);
			VN_RELE(rootvp);
			if (rootbdp != NULL) {
			    vfsp->vfs_flag |= VFS_DMI;
			    error = dm_send_mount_event(vfsp, DM_RIGHT_NULL,
					NULL,
					DM_RIGHT_NULL, rootvp, DM_RIGHT_NULL,
					args->mtpt, args->fsname);
			}
		}
	}

	return error;
}

#define MNTOPT_DMAPI	"dmapi"		/* DMI enabled (DMAPI / XDSM) */
#define MNTOPT_XDSM	"xdsm"		/* DMI enabled (DMAPI / XDSM) */
#define MNTOPT_DMI	"dmi"		/* DMI enabled (DMAPI / XDSM) */

STATIC int
xfs_dm_parseargs(
	struct bhv_desc		*bhv,
	char			*options,
	struct xfs_mount_args	*args,
	int			update)
{
	size_t			length;
	char			*local_options = options;
	char			*this_char;
	int			error;

	while ((this_char = strsep(&local_options, ",")) != NULL) {
		length = strlen(this_char);
		if (local_options)
			length++;

		if (!strcmp(this_char, MNTOPT_DMAPI)) {
			args->flags |= XFSMNT_DMAPI;
		} else if (!strcmp(this_char, MNTOPT_XDSM)) {
			args->flags |= XFSMNT_DMAPI;
		} else if (!strcmp(this_char, MNTOPT_DMI)) {
			args->flags |= XFSMNT_DMAPI;
		} else {
			if (local_options)
				*(local_options-1) = ',';
			continue;
		}

		while (length--)
			*this_char++ = ',';
	}

	PVFS_PARSEARGS(BHV_NEXT(bhv), options, args, update, error);
	if (!error && (args->flags & XFSMNT_DMAPI) && (*args->mtpt == '\0')) {
		printk("XFS: %s option needs the mount point option as well\n",
			MNTOPT_DMAPI);
		error = EINVAL;
	}
	if (!error && !update && !(args->flags & XFSMNT_DMAPI))
		bhv_remove_vfsops(bhvtovfs(bhv), VFS_POSITION_DM);
	return error;
}

STATIC int
xfs_dm_showargs(
	struct bhv_desc		*bhv,
	struct seq_file		*m)
{
	struct vfs		*vfsp = bhvtovfs(bhv);
	int			error;

	if (vfsp->vfs_flag & VFS_DMI)
		seq_puts(m, "," MNTOPT_DMAPI);

	PVFS_SHOWARGS(BHV_NEXT(bhv), m, error);
	return error;
}

struct bhv_vfsops xfs_dmops = { {
	BHV_IDENTITY_INIT(VFS_BHV_DM, VFS_POSITION_DM),
	.vfs_mount		= xfs_dm_mount,
	.vfs_parseargs		= xfs_dm_parseargs,
	.vfs_showargs		= xfs_dm_showargs,
	.vfs_dmapiops		= xfs_dm_get_fsys_vector, },
};

void __init
xfs_dm_init(void)
{
	vfs_bhv_set_custom(&xfs_dmops, &xfs_dmcore_xfs);
	bhv_module_init(XFS_DMOPS, THIS_MODULE, &xfs_dmops);
}

void __exit
xfs_dm_exit(void)
{
	bhv_module_exit(XFS_DMOPS);
	vfs_bhv_clr_custom(&xfs_dmops);
}
