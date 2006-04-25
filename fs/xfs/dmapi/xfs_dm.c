/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_clnt.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_itable.h"
#include "xfs_bmap.h"
#include "xfs_rw.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_inode_item.h"
#include <dmapi.h>
#include <dmapi_kern.h>
#include "xfs_dm.h"

#define MAXNAMLEN MAXNAMELEN

#define XFS_BHV_LOOKUP(vp, xbdp)  \
	xbdp = vn_bhv_lookup(VN_BHV_HEAD(vp), &xfs_vnodeops); \
	ASSERT(xbdp);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define MIN_DIO_SIZE(mp)		((mp)->m_sb.sb_sectsize)
#define MAX_DIO_SIZE(mp)		(INT_MAX & ~(MIN_DIO_SIZE(mp) - 1))
#define XFS_TO_LINUX_RDEVT(xip,ip)	(new_encode_dev((ip)->i_rdev))
#define XFS_TO_LINUX_DEVT(xip,ip)	(new_encode_dev((ip)->i_sb->s_dev))
#define BREAK_LEASE(inode,flag)		break_lease(inode,flag)
#else
#define MIN_DIO_SIZE(mp)		((mp)->m_sb.sb_blocksize)
#define MAX_DIO_SIZE(mp)		(INT_MAX & ~(MIN_DIO_SIZE(mp) - 1))
#define XFS_TO_LINUX_RDEVT(xip,ip)	(kdev_t_to_nr(XFS_DEV_TO_KDEVT((xip)->i_df.if_u2.if_rdev)))
#define XFS_TO_LINUX_DEVT(xip,ip)	((xip)->i_mount->m_ddev_targp->bt_dev)
#define BREAK_LEASE(inode,flag)		get_lease(inode,flag)
#endif

static void up_rw_sems(struct inode *ip, int flags)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	if (flags & DM_FLAGS_IALLOCSEM_WR)
		up_write(&ip->i_alloc_sem);
	if (flags & DM_FLAGS_IMUX)
		mutex_unlock(&ip->i_mutex);
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)) && \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,22))
	if (flags & DM_FLAGS_IMUX)
		up(&ip->i_sem);
	if (flags & DM_FLAGS_IALLOCSEM_RD)
		up_read(&ip->i_alloc_sem);
	else if (flags & DM_FLAGS_IALLOCSEM_WR)
		up_write(&ip->i_alloc_sem);
#endif
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,21)
	if (flags & DM_FLAGS_IMUX)
		up(&ip->i_sem);
#endif
}

static void down_rw_sems(struct inode *ip, int flags)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	if (flags & DM_FLAGS_IMUX)
		mutex_lock(&ip->i_mutex);
	if (flags & DM_FLAGS_IALLOCSEM_WR)
		down_write(&ip->i_alloc_sem);
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)) && \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,22))
	if (flags & DM_FLAGS_IALLOCSEM_RD)
		down_read(&ip->i_alloc_sem);
	else if (flags & DM_FLAGS_IALLOCSEM_WR)
		down_write(&ip->i_alloc_sem);
	if (flags & DM_FLAGS_IMUX)
		down(&ip->i_sem);
#endif
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,21)
	if (flags & DM_FLAGS_IMUX)
		down(&ip->i_sem);
#endif
}


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
	void __user	*laststruct;
	dm_dkattrname_t	attrname;
	uint		bulkall : 1;
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
	(sizeof(dmtype) + sizeof(dm_handle_t) + namelen)

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

	/* Returns positive errors to XFS */

	XFS_BHV_LOOKUP(vp, bdp);
	ip = XFS_BHVTOI(bdp);
	do {
		dmstate = ip->i_iocore.io_dmstate;
		if (locktype)
			xfs_rwunlock(bdp, *locktype);

		up_rw_sems(inode, flags);

		error = dm_send_data_event(event, inode, DM_RIGHT_NULL,
				offset, length, flags);
		error = -error; /* DMAPI returns negative errors */

		down_rw_sems(inode, flags);

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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	struct vm_area_struct *vma = NULL;
#endif

	if (!VN_MAPPED(vp))
		return 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	spin_lock(&mapping->i_mmap_lock);
	if (mapping_writably_mapped(mapping))
		prohibited |= (1 << DM_EVENT_WRITE);
	spin_unlock(&mapping->i_mmap_lock);
#else
	spin_lock(&mapping->i_shared_lock);
	for (vma = mapping->i_mmap_shared; vma; vma = vma->vm_next_share) {
		if (vma->vm_flags & VM_WRITE) {
			prohibited |= (1 << DM_EVENT_WRITE);
			break;
		}
	}
	for (vma = mapping->i_mmap; vma; vma = vma->vm_next_share) {
		if (vma->vm_flags & VM_WRITE) {
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
	dm_handle_t	handle;
	vnode_t		*vp;
	u_char		*ip;
	int		length;
	int		error;
	int		i;

	vp = BHV_TO_VNODE(bdp);

	if ((error = dm_vp_to_handle(vp, &handle)))
		return(error);

	if (type == DM_FSYS_OBJ) {	/* a filesystem handle */
		length = DM_FSHSIZE;
	} else {
		length = DM_HSIZE(handle);
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
	dm_attrname_t	__user *from,	/* dm_attrname_t in user space */
	dm_dkattrname_t *to)		/* name buffer in kernel space */
{
	int error = 0;
	size_t len;

	strcpy(to->dan_chars, dmattr_prefix);

	len = strnlen_user((char __user *)from, DM_ATTR_NAME_SIZE);
	if (copy_from_user(&to->dan_chars[DMATTR_PREFIXLEN], from, len))
		to->dan_chars[sizeof(to->dan_chars) - 1] = '\0';
	else if (to->dan_chars[DMATTR_PREFIXLEN] == '\0')
		error = EINVAL;
	else
		to->dan_chars[DMATTR_PREFIXLEN + len - 1] = '\0';

	return error;
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
	struct inode	*inode = LINVFS_GET_IP(vp);

	buf->dt_size = ip->i_d.di_size;
	buf->dt_dev = XFS_TO_LINUX_DEVT(ip,inode);

	buf->dt_ino = ip->i_ino;
#if XFS_BIG_INUMS
	buf->dt_ino += mp->m_inoadd;
#endif
	/*
	 * Copy from in-core inode.
	 */
	buf->dt_mode = inode->i_mode;
	buf->dt_uid = ip->i_d.di_uid;
	buf->dt_gid = ip->i_d.di_gid;
	buf->dt_nlink = ip->i_d.di_nlink;
	vn_atime_to_time_t(vp, &buf->dt_atime);
	buf->dt_mtime = ip->i_d.di_mtime.t_sec;
	buf->dt_ctime = ip->i_d.di_ctime.t_sec;

	switch (ip->i_d.di_mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR:
		buf->dt_blksize = BLKDEV_IOSIZE;
		buf->dt_rdev = XFS_TO_LINUX_RDEVT(ip,inode);
		break;
	default:
		/*
		 * We use the read buffer size as a recommended I/O
		 * size.  This should always be larger than the
		 * write buffer size, so it should be OK.
		 * The value returned is in bytes.
		 */
		buf->dt_blksize = 1 << mp->m_readio_log;
		buf->dt_rdev = 0;
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
 * This is used by dm_get_bulkattr().
 * Given a inumber, it igets the inode and fills the given buffer
 * with the dm_stat structure for the file.
 */
/* ARGSUSED */
STATIC int
xfs_dm_bulkstat_one(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_ino_t	ino,		/* inode number to get data for */
	void		__user *buffer,	/* buffer to place output in */
	int		ubsize,		/* size of buffer */
	void		*private_data,	/* my private data */
	xfs_daddr_t	bno,		/* starting block of inode cluster */
	int		*ubused,	/* amount of buffer we used */
	void		*dip,		/* on-disk inode pointer */
	int		*res)		/* bulkstat result code */
{
	xfs_inode_t	*xip = NULL;
	dm_stat_t	*sbuf;
	dm_xstat_t	*xbuf = NULL;
	dm_handle_t	handle;
	u_int		stat_sz;
	int		error;
	int		kern_buf_sz = 0;
	int		attr_buf_sz = 0;
	caddr_t		attr_buf = NULL;
	void __user	*attr_user_buf = NULL;
	int		value_len;
	dm_bulkstat_one_t *dmb = (dm_bulkstat_one_t*)private_data;
	vnode_t		*vp = NULL;

	/* Returns positive errors to XFS */

	ASSERT(dmb);
	if (dmb->bulkall) {
		stat_sz = DM_STAT_SIZE(*xbuf, 0);
		stat_sz = (stat_sz+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);
		kern_buf_sz = stat_sz;
		xbuf = kmem_alloc(kern_buf_sz, KM_SLEEP);

		sbuf = &xbuf->dx_statinfo;
	} else {
		stat_sz = DM_STAT_SIZE(*sbuf, 0);
		stat_sz = (stat_sz+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);
		kern_buf_sz = stat_sz;
		sbuf = kmem_alloc(kern_buf_sz, KM_SLEEP);
	}

	if (stat_sz > ubsize) {
		error = ENOMEM;
		goto out_free_buffer;
	}

	if (ino == mp->m_sb.sb_rbmino || ino == mp->m_sb.sb_rsumino) {
		error = EINVAL;
		goto out_free_buffer;
	}

	error = xfs_iget(mp, NULL, ino, 0, XFS_ILOCK_SHARED, &xip, bno);
	if (error)
		goto out_free_buffer;
	if (xip->i_d.di_mode == 0) {
		xfs_iput_new(xip, XFS_ILOCK_SHARED);
		error = ENOENT;
		goto out_free_buffer;
	}
	vp = XFS_ITOV(xip);

	/*
	 * copy everything to the dm_stat buffer
	 */
	xfs_ip_to_stat(mp, sbuf, xip);

	/*
	 * Make the handle and put it at the end of the stat buffer.
	 */
	dm_ip_to_handle(LINVFS_GET_IP(vp), &handle);
	/* Handle follows outer struct, but ptr is always in dm_stat_t */
	if (xbuf) {
		memcpy(xbuf+1, &handle, sizeof(handle));
		sbuf->dt_handle.vd_offset = (ssize_t) sizeof(dm_xstat_t);
	}
	else {
		memcpy(sbuf+1, &handle, sizeof(handle));
		sbuf->dt_handle.vd_offset = (ssize_t) sizeof(dm_stat_t);
	}
	sbuf->dt_handle.vd_length = (size_t) DM_HSIZE(handle);

	/*
	 * This is unused in bulkstat - so we zero it out.
	 */
	memset(&sbuf->dt_compname, 0, sizeof(dm_vardata_t));

  	/*
	 * Do not hold ILOCK_SHARED during VOP_ATTR_GET.
  	 */
	xfs_iunlock(xip, XFS_ILOCK_SHARED);

  	/*
	 * For dm_xstat, get attr.
	 */
	if (xbuf) {
		/* Determine place to drop attr value, and available space. */
		value_len = ubsize - stat_sz;

		/* Sanity check value_len */
		if (value_len < XFS_BUG_KLUDGE) {
			error = ENOMEM;
			goto out_iput;
		}
		if (value_len > ATTR_MAX_VALUELEN)
			value_len = ATTR_MAX_VALUELEN;

		attr_user_buf = buffer + stat_sz;
		attr_buf_sz = value_len;
		attr_buf = kmem_alloc(attr_buf_sz, KM_SLEEP);

		memset(&xbuf->dx_attrdata, 0, sizeof(dm_vardata_t));
		VOP_ATTR_GET(vp, dmb->attrname.dan_chars, attr_buf,
			     &value_len, ATTR_ROOT, sys_cred, error);

		DM_EA_XLATE_ERR(error);
		if (error && (error != ENOATTR)) {
			if (error == E2BIG)
				error = ENOMEM;
			goto out_iput;
		}

		/* How much space in the attr? */
		if (error != ENOATTR) {
			int aligned_value_len = value_len;
			xbuf->dx_attrdata.vd_offset = stat_sz;
			xbuf->dx_attrdata.vd_length = value_len;
			aligned_value_len = (aligned_value_len+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);
			stat_sz += aligned_value_len;
		}
	}

	/*
	 * Finished with the vnode
  	 */
	VN_RELE(vp);

	/*
	 *  Update link in dm_stat_t to point to next struct.
	 */
	sbuf->_link = stat_sz;

	if (xbuf) {
		if (copy_to_user(buffer, xbuf, kern_buf_sz)) {
			error = EFAULT;
			goto out_free_attr_buffer;
		}
		if (copy_to_user(attr_user_buf, attr_buf, value_len)){
			error = EFAULT;
			goto out_free_attr_buffer;
		}
	} else {
		error = EFAULT;
		if (copy_to_user(buffer, sbuf, kern_buf_sz)) {
			error = EFAULT;
			goto out_free_buffer;
		}
	}

	if (xbuf) {
		kmem_free(xbuf, kern_buf_sz);
		kmem_free(attr_buf, attr_buf_sz);
	} else {
		kmem_free(sbuf, kern_buf_sz);
	}

	*res = BULKSTAT_RV_DIDONE;
	if (ubused)
		*ubused = stat_sz;
	dmb->laststruct = buffer;
	return(0);

 out_iput:
	VN_RELE(vp);
 out_free_attr_buffer:
	if (attr_buf_sz)
		kmem_free(attr_buf, attr_buf_sz);
 out_free_buffer:
	if (kern_buf_sz) {
		if (xbuf)
			kmem_free(xbuf, kern_buf_sz);
		else
			kmem_free(sbuf, kern_buf_sz);
	}
	*res = BULKSTAT_RV_NOTHING;
	return error;
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
xfs_dm_stat_one(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_ino_t	ino,		/* inode number to get data for */
	dm_stat_t	*sbuf,		/* buffer to place output in */
	int		ubsize)		/* size of buffer */
{
	xfs_inode_t	*xip = NULL;
	dm_handle_t	handle;
	u_int		stat_sz;
	int		error;

	stat_sz = DM_STAT_SIZE(*sbuf, 0);
	stat_sz = (stat_sz+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);

	if (stat_sz > ubsize)
		goto out_fail;
	if (ino == mp->m_sb.sb_rbmino || ino == mp->m_sb.sb_rsumino)
		goto out_fail;

	error = xfs_iget(mp, NULL, ino, 0, XFS_ILOCK_SHARED, &xip, 0);
	if (error)
		goto out_fail;
	if (xip->i_d.di_mode == 0)
		goto out_iput_new;

	/*
	 * copy everything to the dm_stat buffer
	 */
	xfs_ip_to_stat(mp, sbuf, xip);

	/*
	 * Make the handle and put it at the end of the stat buffer.
	 */
	dm_ip_to_handle(LINVFS_GET_IP(XFS_ITOV(xip)), &handle);

	/* Handle follows outer struct, but ptr is always in dm_stat_t */
	memcpy(sbuf + 1, &handle, sizeof(handle));
	sbuf->dt_handle.vd_offset = (ssize_t) sizeof(dm_stat_t);
	sbuf->dt_handle.vd_length = (size_t) DM_HSIZE(handle);

	/*
	 * This is unused in bulkstat - so we zero it out.
	 */
	memset(&sbuf->dt_compname, 0, sizeof(dm_vardata_t));

	xfs_iput(xip, XFS_ILOCK_SHARED);

	/*
	 *  Update link in dm_stat_t to point to next struct.
	 */
	sbuf->_link = stat_sz;
	return BULKSTAT_RV_DIDONE;
 out_iput_new:
	xfs_iput_new(xip, XFS_ILOCK_SHARED);
 out_fail:
	return BULKSTAT_RV_NOTHING;
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
	int		needed;

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
		needed = DM_STAT_SIZE(*statp, namelen);
		needed = (needed+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);
		if (spaceleft < needed) {
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

		res = xfs_dm_stat_one(mp, (xfs_ino_t)p->d_ino, statp, spaceleft);
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
	return(1);
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
	if (VN_ISDIR(vp)) {
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
	void		__user *bufp)
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


/* We need to be able to select various combinations of O_NONBLOCK,
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
	void		__user *bufp,
	int		*rvp)
{
	int		error;
	int		oflags;
	ssize_t		xfer;
	struct file	*file;
	struct inode	*inode = LINVFS_GET_IP(vp);
	struct dentry	*dentry;
	bhv_desc_t	*xbdp;

	if ((off < 0) || (off > i_size_read(inode)) || !S_ISREG(inode->i_mode))
		return EINVAL;

	if (fmode & FMODE_READ) {
		oflags = O_RDONLY;
	} else {
		oflags = O_WRONLY;
	}

	/*
	 * Build file descriptor flags and I/O flags.  O_NONBLOCK is needed so
	 * that we don't block on mandatory file locks.
	 */

	oflags |= O_LARGEFILE | O_NONBLOCK;
	XFS_BHV_LOOKUP(vp, xbdp);
	if (xfs_dm_direct_ok(xbdp, off, len, bufp))
		oflags |= O_DIRECT;

	if (fflag & O_SYNC)
		oflags |= O_SYNC;

	if (inode->i_fop == NULL) {
		/* no iput; caller did get, and will do put */
		return EINVAL;
	}

	igrab(inode);

	dentry = d_alloc_anon(inode);
	if (dentry == NULL) {
		iput(inode);
		return ENOMEM;
	}

	file = dentry_open(dentry, NULL, oflags);
	if (IS_ERR(file)) {
		return -PTR_ERR(file);
	}
	file->f_op = &linvfs_invis_file_operations;

	if (fmode & FMODE_READ) {
		xfer = file->f_op->read(file, bufp, len, (loff_t*)&off);
	} else {
		xfer = file->f_op->write(file, bufp, len, (loff_t*)&off);
	}

	if (xfer >= 0) {
		*rvp = xfer;
		error = 0;
	} else {
		/* xfs_read/xfs_write return negative error--flip it */
		error = -(int)xfer;
	}

	fput(file);
	return error;
}

/* ARGSUSED */
STATIC int
xfs_dm_clear_inherit(
	struct inode	*inode,
	dm_right_t	right,
	dm_attrname_t	__user *attrnamep)
{
	return(-ENOSYS); /* Return negative error to DMAPI */
}


/* ARGSUSED */
STATIC int
xfs_dm_create_by_handle(
	struct inode	*inode,
	dm_right_t	right,
	void		__user *hanp,
	size_t		hlen,
	char		__user *cname)
{
	return(-ENOSYS); /* Return negative error to DMAPI */
}


/* ARGSUSED */
STATIC int
xfs_dm_downgrade_right(
	struct inode	*inode,
	dm_right_t	right,
	u_int		type)		/* DM_FSYS_OBJ or zero */
{
#ifdef	DEBUG_RIGHTS
	char		buffer[sizeof(dm_handle_t) * 2 + 1];
	bhv_desc_t	*bdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);

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
	struct inode	*inode,
	dm_right_t	right,
	dm_off_t	__user	*offp,
	u_int		nelem,
	dm_extent_t	__user *extentp,
	u_int		__user *nelemp,
	int		*rvp)
{
	xfs_inode_t	*ip;		/* xfs incore inode pointer */
	xfs_mount_t	*mp;		/* file system mount point */
	xfs_fileoff_t	fsb_offset;
	xfs_filblks_t	fsb_length;
	dm_off_t	startoff;
	int		elem;
	bhv_desc_t	*xbdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	xfs_bmbt_irec_t *bmp = NULL;
	u_int		bmpcnt = 50;
	u_int		bmpsz = sizeof(xfs_bmbt_irec_t) * bmpcnt;
	int		error = 0;

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if ((inode->i_mode & S_IFMT) != S_IFREG)
		return(-EINVAL);

	if (copy_from_user( &startoff, offp, sizeof(startoff)))
		return(-EFAULT);

	XFS_BHV_LOOKUP(vp, xbdp);

	ip = XFS_BHVTOI(xbdp);
	mp = ip->i_mount;
	ASSERT(mp);

	if (startoff > XFS_MAXIOFFSET(mp))
		return(-EINVAL);

	if (nelem == 0)
		return(-EINVAL);

	/* Convert the caller's starting offset into filesystem allocation
	   units as required by xfs_bmapi().  Round the offset down so that
	   it is sure to be included in the reply.
	*/

	fsb_offset = XFS_B_TO_FSBT(mp, startoff);
	fsb_length = XFS_B_TO_FSB(mp, XFS_MAXIOFFSET(mp)) - fsb_offset;
	elem = 0;

	if (fsb_length)
		bmp = kmem_alloc(bmpsz, KM_SLEEP);

	while (fsb_length && elem < nelem) {
		dm_extent_t	extent;
		xfs_filblks_t	fsb_bias;
		dm_size_t	bias;
		int		lock;
		int		num;
		int		i;

		/* Compute how many getbmap structures to use on the xfs_bmapi
		   call.
		*/

		num = MIN((u_int)(nelem - elem), bmpcnt);

		xfs_ilock(ip, XFS_IOLOCK_SHARED);
		lock = xfs_ilock_map_shared(ip);

		error = xfs_bmapi(NULL, ip, fsb_offset, fsb_length,
			XFS_BMAPI_ENTIRE, NULL, 0, bmp, &num, NULL);

		xfs_iunlock_map_shared(ip, lock);
		xfs_iunlock(ip, XFS_IOLOCK_SHARED);

		if (error) {
			error = -error; /* Return negative error to DMAPI */
			goto finish_out;
		}

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

			if (copy_to_user( extentp, &extent, sizeof(extent))) {
				error = -EFAULT;
				goto finish_out;
			}

			fsb_bias = fsb_offset - bmp[i].br_startoff;
			fsb_offset += bmp[i].br_blockcount - fsb_bias;
			fsb_length -= bmp[i].br_blockcount - fsb_bias;
			elem++;
		}
	}

	if (fsb_length == 0) {
		startoff = 0;
	}
	if (copy_to_user( offp, &startoff, sizeof(startoff))) {
		error = -EFAULT;
		goto finish_out;
	}

	if (copy_to_user( nelemp, &elem, sizeof(elem))) {
		error = -EFAULT;
		goto finish_out;
	}

	*rvp = (fsb_length == 0 ? 0 : 1);

finish_out:
	if (bmp)
		kmem_free(bmp, bmpsz);
	return(error);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_bulkall_rvp(
	struct inode	*inode,
	dm_right_t	right,
	u_int		mask,
	dm_attrname_t	__user *attrnamep,
	dm_attrloc_t	__user *locp,
	size_t		buflen,
	void		__user *bufp,	/* address of buffer in user space */
	size_t		__user *rlenp,	/* user space address */
	int		*rvalp)
{
	int		error, done;
	int		nelems;
	u_int		statstruct_sz;
	dm_attrloc_t	loc;
	bhv_desc_t	*mp_bdp;
	xfs_mount_t	*mp;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	vfs_t		*vfsp = vp->v_vfsp;
	dm_attrname_t	attrname;
	dm_bulkstat_one_t dmb;
	dm_xstat_t __user *dxs;

	/* Returns negative errors to DMAPI */

	if (copy_from_user(&attrname, attrnamep, sizeof(attrname)) ||
	    copy_from_user(&loc, locp, sizeof(loc)))
		return -EFAULT;

	if (attrname.an_chars[0] == '\0')
		return(-EINVAL);

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	/* Because we will write directly to the user's buffer, make sure that
	   the buffer is properly aligned.
	*/

	if (((unsigned long)bufp & (DM_STAT_ALIGN - 1)) != 0)
		return(-EFAULT);

	/* Size of the handle is constant for this function.
	 * If there are no files with attributes, then this will be the
	 * maximum number of inodes we can get.
	 */

	statstruct_sz = DM_STAT_SIZE(dm_xstat_t, 0);
	statstruct_sz = (statstruct_sz+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);

	nelems = buflen / statstruct_sz; 
	if (nelems < 1) {
		if (put_user( statstruct_sz, rlenp ))
			return(-EFAULT);
		return(-E2BIG);
	} 

	mp_bdp = bhv_lookup(VFS_BHVHEAD(vfsp), &xfs_vfsops);
	ASSERT(mp_bdp);
	mp = XFS_BHVTOM(mp_bdp);


	/* Build the on-disk version of the attribute name. */
	strcpy(dmb.attrname.dan_chars, dmattr_prefix);
	strncpy(&dmb.attrname.dan_chars[DMATTR_PREFIXLEN],
		attrname.an_chars, DM_ATTR_NAME_SIZE + 1);
	dmb.attrname.dan_chars[sizeof(dmb.attrname.dan_chars) - 1] = '\0';

	/*
	 * fill the buffer with dm_xstat_t's 
	 */

	dmb.laststruct = NULL;
	dmb.bulkall = 1;
	error = xfs_bulkstat(mp, (xfs_ino_t *)&loc,
			     &nelems, 
			     xfs_dm_bulkstat_one,
			     (void*)&dmb,
			     statstruct_sz,
			     bufp, 
			     BULKSTAT_FG_IGET,
			     &done);

	if (error)
		return(-error); /* Return negative error to DMAPI */
	if (!done)
		*rvalp = 1;
	else
		*rvalp = 0;
	if (put_user( statstruct_sz * nelems, rlenp ))
		return(-EFAULT);

	if (copy_to_user( locp, &loc, sizeof(loc)))
		return(-EFAULT);
	/*
	 *  If we didn't do any, we must not have any more to do.
	 */
	if (nelems < 1)
		return(0);
	/* set _link in the last struct to zero */
	dxs = (dm_xstat_t __user *)dmb.laststruct;
	if (dxs) {
		dm_xstat_t ldxs;
		if (copy_from_user(&ldxs, dxs, sizeof(*dxs)))
			return(-EFAULT);
		ldxs.dx_statinfo._link = 0;
		if (copy_to_user(dxs, &ldxs, sizeof(*dxs)))
			return(-EFAULT);
	}
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_bulkattr_rvp(
	struct inode	*inode,
	dm_right_t	right,
	u_int		mask,
	dm_attrloc_t	__user *locp,
	size_t		buflen,
	void		__user *bufp,
	size_t		__user *rlenp,
	int		*rvalp)
{
	int		error, done;
	int		nelems;
	u_int		statstruct_sz;
	dm_attrloc_t	loc;
	bhv_desc_t	*mp_bdp;
	xfs_mount_t	*mp;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	vfs_t		*vfsp = vp->v_vfsp;
	dm_stat_t __user *dxs;
	dm_bulkstat_one_t dmb;

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if (copy_from_user( &loc, locp, sizeof(loc)))
		return(-EFAULT);

	/* Because we will write directly to the user's buffer, make sure that
	   the buffer is properly aligned.
	*/

	if (((unsigned long)bufp & (DM_STAT_ALIGN - 1)) != 0)
		return(-EFAULT);

	/* size of the handle is constant for this function */

	statstruct_sz = DM_STAT_SIZE(dm_stat_t, 0);
	statstruct_sz = (statstruct_sz+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);

	nelems = buflen / statstruct_sz;
	if (nelems < 1) {
		if (put_user( statstruct_sz, rlenp ))
			return(-EFAULT);
		return(-E2BIG);
	}

	mp_bdp = bhv_lookup(VFS_BHVHEAD(vfsp), &xfs_vfsops);
	ASSERT(mp_bdp);
	mp = XFS_BHVTOM(mp_bdp);


	/*
	 * fill the buffer with dm_stat_t's
	 */

	dmb.laststruct = NULL;
	error = xfs_bulkstat(mp,
			     (xfs_ino_t *)&loc,
			     &nelems,
			     xfs_dm_bulkstat_one,
			     (void*)&dmb,
			     statstruct_sz,
			     bufp,
			     BULKSTAT_FG_IGET,
			     &done);
	if (error)
		return(-error); /* Return negative error to DMAPI */
	if (!done) {
		*rvalp = 1;
	} else {
		*rvalp = 0;
	}

	if (put_user( statstruct_sz * nelems, rlenp ))
		return(-EFAULT);

	if (copy_to_user( locp, &loc, sizeof(loc)))
		return(-EFAULT);

	/*
	 *  If we didn't do any, we must not have any more to do.
	 */
	if (nelems < 1)
		return(0);
	/* set _link in the last struct to zero */
	dxs = (dm_stat_t __user *)dmb.laststruct;
	if (dxs) {
		dm_stat_t ldxs;
		if (copy_from_user(&ldxs, dxs, sizeof(*dxs)))
			return(-EFAULT);
		ldxs._link = 0;
		if (copy_to_user(dxs, &ldxs, sizeof(*dxs)))
			return(-EFAULT);
	}
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_config(
	struct inode	*inode,
	dm_right_t	right,
	dm_config_t	flagname,
	dm_size_t	__user *retvalp)
{
	dm_size_t	retval;

	/* Returns negative errors to DMAPI */

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
		return(-EINVAL);
	}

	/* Copy the results back to the user. */

	if (copy_to_user( retvalp, &retval, sizeof(retval)))
		return(-EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_config_events(
	struct inode	*inode,
	dm_right_t	right,
	u_int		nelem,
	dm_eventset_t	__user *eventsetp,
	u_int		__user *nelemp)
{
	dm_eventset_t	eventset;

	/* Returns negative errors to DMAPI */

	if (nelem == 0)
		return(-EINVAL);

	eventset = DM_XFS_SUPPORTED_EVENTS;

	/* Now copy the event mask and event count back to the caller.	We
	   return the lesser of nelem and DM_EVENT_MAX.
	*/

	if (nelem > DM_EVENT_MAX)
		nelem = DM_EVENT_MAX;
	eventset &= (1 << nelem) - 1;

	if (copy_to_user( eventsetp, &eventset, sizeof(eventset)))
		return(-EFAULT);

	if (put_user(nelem, nelemp))
		return(-EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_destroy_dmattr(
	struct inode	*inode,
	dm_right_t	right,
	dm_attrname_t  *attrnamep,
	char 		**valuepp,
	int		*vlenp)
{
	char		buffer[XFS_BUG_KLUDGE];
	dm_dkattrname_t dkattrname;
	int		alloc_size;
	int		value_len;
	char		*value;
	int		error;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	*vlenp = -1;		/* assume failure by default */

	if (attrnamep->an_chars[0] == '\0')
		return(-EINVAL);

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
			return(-ENOMEM);
		}

		VOP_ATTR_GET(vp, dkattrname.dan_chars, value,
			&value_len, ATTR_ROOT, sys_cred, error);
	}
	if (error) {
		if (alloc_size)
			kfree(value);
		DM_EA_XLATE_ERR(error);
		return(-error); /* Return negative error to DMAPI */
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
			return(-ENOMEM);
		}
		memcpy(value, buffer, value_len);
	} else if (value_len > DM_MAX_ATTR_BYTES_ON_DESTROY) {
		int	value_len2 = DM_MAX_ATTR_BYTES_ON_DESTROY;
		char	*value2;

		value2 = kmalloc(value_len2, SLAB_KERNEL);
		if (value2 == NULL) {
			printk("%s/%d: kmalloc returned NULL\n", __FUNCTION__, __LINE__);
			kfree(value);
			return(-ENOMEM);
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
	struct inode	*inode,
	dm_right_t	right,
	dm_dioinfo_t	__user *diop)
{
	dm_dioinfo_t	dio;
	xfs_mount_t	*mp;
	xfs_inode_t	*ip;
	bhv_desc_t	*xbdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	XFS_BHV_LOOKUP(vp, xbdp);

	ip = XFS_BHVTOI(xbdp);
	mp = ip->i_mount;

	dio.d_miniosz = dio.d_mem = MIN_DIO_SIZE(mp);
	dio.d_maxiosz = MAX_DIO_SIZE(mp);
	dio.d_dio_only = DM_FALSE;

	if (copy_to_user(diop, &dio, sizeof(dio)))
		return(-EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_get_dirattrs_rvp(
	struct inode	*inode,
	dm_right_t	right,
	u_int		mask,
	dm_attrloc_t	__user *locp,
	size_t		buflen,
	void		__user *bufp,	/* address of buffer in user space */
	size_t		__user *rlenp,	/* user space address */
	int		*rvp)
{
	xfs_inode_t	*dp;
	xfs_mount_t	*mp;
	size_t		direntbufsz;
	size_t		nread, spaceleft, nwritten;
	size_t		ubused = 0;
	void		*direntp, *statbuf;
	char		*statbufp;
	uint		lock_mode;
	int		error = 0;
	dm_attrloc_t	loc;
	dm_attrloc_t	prev_loc;
	bhv_desc_t	*mp_bdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	vfs_t		*vfsp = vp->v_vfsp;
	uint		dir_gen = 0;
	int		done = 0;
	int		one_more = 0;

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if (copy_from_user( &loc, locp, sizeof(loc)))
		return(-EFAULT);

	if (mask & ~(DM_AT_HANDLE|DM_AT_EMASK|DM_AT_PMANR|DM_AT_PATTR|DM_AT_DTIME|DM_AT_CFLAG|DM_AT_STAT))
		return(-EINVAL);

	if ((inode->i_mode & S_IFMT) != S_IFDIR)
		return(-EINVAL);

	mp_bdp = bhv_lookup(VFS_BHVHEAD(vfsp), &xfs_vfsops);
	ASSERT(mp_bdp);
	dp = xfs_vtoi(vp);
	ASSERT(dp);

	mp = XFS_BHVTOM(mp_bdp);

	if (buflen < DM_STAT_SIZE(dm_stat_t, 0)) {
		*rvp = 1; /* tell caller to try again */
		goto finish_out;
	}

	/*
	 * Don't get more dirents than are guaranteed to fit.
	 * The minimum that the stat buf holds is the buf size over
	 * maximum entry size.  That times the minimum dirent size
	 * is an overly conservative size for the dirent buf.
	 */
	direntbufsz = (NBPP / DM_STAT_SIZE(dm_stat_t, MAXNAMLEN)) * sizeof(xfs_dirent_t) + (DM_STAT_ALIGN-1);
	direntbufsz &= ~(DM_STAT_ALIGN-1);

	direntp = kmem_alloc(direntbufsz, KM_SLEEP);
	statbuf = kmem_alloc(buflen, KM_SLEEP);
	statbufp = (char *)statbuf;
	spaceleft = buflen;
	/*
	 * Keep getting dirents until the ubuffer is packed with
	 * dm_stat structures.
	 */
	do {
		lock_mode = xfs_ilock_map_shared(dp);
		/* See if the directory was removed after it was opened. */
		if (dp->i_d.di_nlink <= 0) {
			xfs_iunlock_map_shared(dp, lock_mode);
			error = EBADF;
			break;
		}
		if (dir_gen == 0)
			dir_gen = dp->i_gen;
		else if (dir_gen != dp->i_gen) {
			/* if dir changed, quit.  May be overzealous... */
			xfs_iunlock_map_shared(dp, lock_mode);
			break;
		}
		prev_loc = loc;
		error = xfs_get_dirents(dp, direntp, direntbufsz,
					(xfs_off_t *)&loc, &nread);
		xfs_iunlock_map_shared(dp, lock_mode);

		if (error) {
			break;
		}
		if (nread == 0) {
			done = 1;
			break;
		}
		if (one_more) {
			loc = prev_loc;
			done = 0;
			break;
		}

		/*
		 * Now iterate thru them and call bulkstat_one() on all
		 * of them
		 */
		statbufp += ubused;
		done = xfs_dirents_to_stats(mp,
					  (xfs_dirent_t *) direntp,
					  (void *)statbufp,
					  nread,
					  &spaceleft,
					  &nwritten,
					  (xfs_off_t *)&loc);
		ubused += nwritten;

		if (!done) {
			/* ran out of space in user buffer */
			break;
		}
		else {
			one_more = 1;
			continue;
		}

	} while (1);

	if (!done) {
		*rvp = 1; /* tell caller we have more */
	} else {
		*rvp = 0;
	}

	if (ubused && !error) {
		if (copy_to_user(bufp, statbuf, ubused))
			error = EFAULT;
	}
	kmem_free(statbuf, buflen);
	kmem_free(direntp, direntbufsz);
finish_out:
	if (!error) {
		if (put_user( ubused, rlenp))
			error = EFAULT;
	}

	if (!error && copy_to_user(locp, &loc, sizeof(loc)))
		error = EFAULT;
	return(-error); /* Return negative error to DMAPI */
}


STATIC int
xfs_dm_get_dmattr(
	struct inode	*inode,
	dm_right_t	right,
	dm_attrname_t	__user *attrnamep,
	size_t		buflen,
	void		__user *bufp,
	size_t		__user  *rlenp)
{
	dm_dkattrname_t name;
	char		*value;
	int		value_len;
	int		alloc_size;
	int		error;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if ((error = xfs_copyin_attrname(attrnamep, &name)) != 0)
		return(-error); /* Return negative error to DMAPI */

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
	return(-error); /* Return negative error to DMAPI */
}

STATIC int
xfs_dm_get_eventlist(
	struct inode	*inode,
	dm_right_t	right,
	u_int		type,
	u_int		nelem,
	dm_eventset_t	*eventsetp,
	u_int 		*nelemp)
{
	int		error;
	bhv_desc_t	*xbdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	XFS_BHV_LOOKUP(vp, xbdp);

	if (type == DM_FSYS_OBJ) {
		error = xfs_dm_fs_get_eventlist(xbdp, right, nelem,
			eventsetp, nelemp);
	} else {
		error = xfs_dm_f_get_eventlist(xbdp, right, nelem,
			eventsetp, nelemp);
	}
	return(-error); /* Returns negative error to DMAPI */
}


/* ARGSUSED */
STATIC int
xfs_dm_get_fileattr(
	struct inode	*inode,
	dm_right_t	right,
	u_int		mask,		/* not used; always return everything */
	dm_stat_t	__user *statp)
{
	dm_stat_t	stat;
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;
	bhv_desc_t	*xbdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	XFS_BHV_LOOKUP(vp, xbdp);

	/* Find the mount point. */

	ip = XFS_BHVTOI(xbdp);
	mp = ip->i_mount;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	xfs_ip_to_stat(mp, &stat, ip);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (copy_to_user( statp, &stat, sizeof(stat)))
		return(-EFAULT);
	return(0);
}


/* We currently only support a maximum of one managed region per file, and
   use the DM_EVENT_READ, DM_EVENT_WRITE, and DM_EVENT_TRUNCATE events in
   the file's dm_eventset_t event mask to implement the DM_REGION_READ,
   DM_REGION_WRITE, and DM_REGION_TRUNCATE flags for that single region.
*/

STATIC int
xfs_dm_get_region(
	struct inode	*inode,
	dm_right_t	right,
	u_int		nelem,
	dm_region_t	__user *regbufp,
	u_int		__user *nelemp)
{
	dm_eventset_t	evmask;
	dm_region_t	region;
	xfs_inode_t	*ip;
	u_int		elem;
	bhv_desc_t	*xbdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

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
		return(-EFAULT);
	if (elem > nelem)
		return(-E2BIG);
	if (elem && copy_to_user(regbufp, &region, sizeof(region)))
		return(-EFAULT);
	return(0);
}


STATIC int
xfs_dm_getall_dmattr(
	struct inode	*inode,
	dm_right_t	right,
	size_t		buflen,
	void		__user *bufp,
	size_t		__user *rlenp)
{
	attrlist_cursor_kern_t cursor;
	attrlist_t	*attrlist;
	dm_attrlist_t	__user *ulist;
	int		*last_link;
	int		alignment;
	int		total_size;
	int		list_size = 8192;	/* should be big enough */
	int		error;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	/* Verify that the user gave us a buffer that is 4-byte aligned, lock
	   it down, and work directly within that buffer.  As a side-effect,
	   values of buflen < sizeof(int) return EINVAL.
	*/

	alignment = sizeof(int) - 1;
	if (((__psint_t)bufp & alignment) != 0) {
		return(-EFAULT);
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
	return(-error); /* Return negative error to DMAPI */
}


/* ARGSUSED */
STATIC int
xfs_dm_getall_inherit(
	struct inode	*inode,
	dm_right_t	right,
	u_int		nelem,
	dm_inherit_t	__user *inheritbufp,
	u_int		__user *nelemp)
{
	return(-ENOSYS); /* Return negative error to DMAPI */
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
	struct inode	*inode,
	dm_right_t	right,
	dm_attrloc_t	__user *locp)
{
	dm_attrloc_t	loc = 0;

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if (copy_to_user( locp, &loc, sizeof(loc)))
		return(-EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_mkdir_by_handle(
	struct inode	*inode,
	dm_right_t	right,
	void		__user *hanp,
	size_t		hlen,
	char		__user *cname)
{
	return(-ENOSYS); /* Return negative error to DMAPI */
}


/* ARGSUSED */
STATIC int
xfs_dm_probe_hole(
	struct inode	*inode,
	dm_right_t	right,
	dm_off_t	off,
	dm_size_t	len,		/* we ignore this for now */
	dm_off_t	__user	*roffp,
	dm_size_t	__user *rlenp)
{
	dm_off_t	roff;
	dm_size_t	rlen;
	xfs_inode_t	*ip;
	xfs_mount_t	*mp;
	uint		lock_flags;
	xfs_fsize_t	realsize;
	u_int		bsize;
	bhv_desc_t	*xbdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	XFS_BHV_LOOKUP(vp, xbdp);

	ip = XFS_BHVTOI(xbdp);
	if ((ip->i_d.di_mode & S_IFMT) != S_IFREG)
		return(-EINVAL);

	mp = ip->i_mount;
	bsize = mp->m_sb.sb_blocksize;

	lock_flags = XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL;
	xfs_ilock(ip, lock_flags);
	realsize = ip->i_d.di_size;
	xfs_iunlock(ip, lock_flags);
	if (off >= realsize)
		return(-E2BIG);

	roff = (off + bsize-1) & ~(bsize-1);
	rlen = 0;		/* Only support punches to EOF for now */
	if (copy_to_user( roffp, &roff, sizeof(roff)))
		return(-EFAULT);
	if (copy_to_user( rlenp, &rlen, sizeof(rlen)))
		return(-EFAULT);
	return(0);
}


STATIC int
xfs_dm_punch_hole(
	struct inode	*inode,
	dm_right_t	right,
	dm_off_t	off,
	dm_size_t	len)
{
	xfs_flock64_t	bf;
	int		error = 0;
	bhv_desc_t	*xbdp;
	xfs_inode_t	*xip;
	xfs_mount_t	*mp;
	u_int		bsize;
	int		cmd = XFS_IOC_UNRESVSP; /* punch */
	xfs_fsize_t	realsize;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_EXCL)
		return -EACCES;

	/* Make sure there are no leases. */
	error = BREAK_LEASE(inode, FMODE_WRITE);
	if (error)
		return -EBUSY;

	error = get_write_access(inode);
	if (error)
		return -EBUSY;

	XFS_BHV_LOOKUP(vp, xbdp);
	xip = XFS_BHVTOI(xbdp);
	mp = xip->i_mount;
	bsize = mp->m_sb.sb_blocksize;
	if (off & (bsize-1)) {
		error = -EAGAIN;
		goto put_and_out;
	}
	if (len & (bsize-1)) {
		error = -EAGAIN;
		goto put_and_out;
	}

	down_rw_sems(inode, DM_SEM_FLAG_WR);

	xfs_ilock(xip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	if ((off >= xip->i_d.di_size) || ((off+len) > xip->i_d.di_size)) {
		xfs_iunlock(xip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
		error = -E2BIG;
		goto up_and_out;
	}
	realsize = xip->i_d.di_size;
	xfs_iunlock(xip, XFS_ILOCK_EXCL);

	bf.l_type = 0;
	bf.l_whence = 0;
	bf.l_start = (xfs_off_t)off;
	bf.l_len = (xfs_off_t)len;

	if (len == 0)
		cmd = XFS_IOC_FREESP; /* truncate */
	error = xfs_change_file_space(xbdp, cmd, &bf, (xfs_off_t)off,
				      sys_cred,
				      ATTR_DMI|ATTR_NOLOCK);

	/* If truncate, grow it back to its original size. */
	if ((error == 0) && (len == 0)) {
		vattr_t va;

		va.va_mask = XFS_AT_SIZE;
		va.va_size = realsize;
		error = xfs_setattr(xbdp, &va, ATTR_DMI|ATTR_NOLOCK,
				    sys_cred);
	}

	/* Let threads in send_data_event know we punched the file. */
	xip->i_iocore.io_dmstate++;
	xfs_iunlock(xip, XFS_IOLOCK_EXCL);
	VMODIFY(vp);

up_and_out:
	up_rw_sems(inode, DM_SEM_FLAG_WR);

put_and_out:
	put_write_access(inode);

	return error;
}


STATIC int
xfs_dm_read_invis_rvp(
	struct inode	*inode,
	dm_right_t	right,
	dm_off_t	off,
	dm_size_t	len,
	void		__user *bufp,
	int		*rvp)
{
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	return(-xfs_dm_rdwr(vp, 0, FMODE_READ, off, len, bufp, rvp));
}


/* ARGSUSED */
STATIC int
xfs_dm_release_right(
	struct inode	*inode,
	dm_right_t	right,
	u_int		type)		/* DM_FSYS_OBJ or zero */
{
#ifdef	DEBUG_RIGHTS
	char		buffer[sizeof(dm_handle_t) * 2 + 1];
	bhv_desc_t	*bdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);

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
	struct inode	*inode,
	dm_right_t	right,
	int		setdtime,
	dm_attrname_t	__user *attrnamep)
{
	dm_dkattrname_t name;
	int		error;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if ((error = xfs_copyin_attrname(attrnamep, &name)) != 0)
		return(-error); /* Return negative error to DMAPI */

	/* Remove the attribute from the object. */

	VOP_ATTR_REMOVE(vp, name.dan_chars,
			(setdtime ? ATTR_ROOT : ATTR_ROOT|ATTR_KERNOTIME),
			NULL, error);
	DM_EA_XLATE_ERR(error);

	if (error == ENOATTR)
		error = ENOENT;
	return(-error); /* Return negative error to DMAPI */
}


/* ARGSUSED */
STATIC int
xfs_dm_request_right(
	struct inode	*inode,
	dm_right_t	right,
	u_int		type,		/* DM_FSYS_OBJ or zero */
	u_int		flags,
	dm_right_t	newright)
{
#ifdef	DEBUG_RIGHTS
	char		buffer[sizeof(dm_handle_t) * 2 + 1];
	bhv_desc_t	*bdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);

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
	struct inode	*inode,
	dm_right_t	right,
	dm_attrname_t	__user *attrnamep,
	int		setdtime,
	size_t		buflen,
	void		__user *bufp)
{
	dm_dkattrname_t name;
	char		*value;
	int		alloc_size;
	int		error;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if ((error = xfs_copyin_attrname(attrnamep, &name)) != 0)
		return(-error); /* Return negative error to DMAPI */
	if (buflen > ATTR_MAX_VALUELEN)
		return(-E2BIG);

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
	return(-error); /* Return negative error to DMAPI */
}

STATIC int
xfs_dm_set_eventlist(
	struct inode	*inode,
	dm_right_t	right,
	u_int		type,
	dm_eventset_t	*eventsetp,	/* in kernel space! */
	u_int		maxevent)
{
	int		error;
	bhv_desc_t	*xbdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	XFS_BHV_LOOKUP(vp, xbdp);

	if (type == DM_FSYS_OBJ) {
		error = xfs_dm_fs_set_eventlist(xbdp, right, eventsetp, maxevent);
	} else {
		error = xfs_dm_f_set_eventlist(xbdp, right, eventsetp, maxevent);
	}
	return(-error); /* Return negative error to DMAPI */
}


/*
 *  This turned out not XFS-specific, but leave it here with get_fileattr.
 */

STATIC int
xfs_dm_set_fileattr(
	struct inode	*inode,
	dm_right_t	right,
	u_int		mask,
	dm_fileattr_t	__user *statp)
{
	dm_fileattr_t	stat;
	vattr_t		vat;
	int		error;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if (copy_from_user( &stat, statp, sizeof(stat)))
		return(-EFAULT);

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
	if (!error)
		vn_revalidate(vp);	/* update Linux inode flags */
	return(-error); /* Return negative error to DMAPI */
}


/* ARGSUSED */
STATIC int
xfs_dm_set_inherit(
	struct inode	*inode,
	dm_right_t	right,
	dm_attrname_t	__user *attrnamep,
	mode_t		mode)
{
	return(-ENOSYS); /* Return negative error to DMAPI */
}


STATIC int
xfs_dm_set_region(
	struct inode	*inode,
	dm_right_t	right,
	u_int		nelem,
	dm_region_t	__user *regbufp,
	dm_boolean_t	__user *exactflagp)
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
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	/* If the caller gave us more than one dm_region_t structure, complain.
	   (He has to call dm_get_config() to find out what our limit is.)
	*/

	if (nelem > 1)
		return(-E2BIG);

	/* If the user provided a dm_region_t structure, then copy it in,
	   validate it, and convert its flags to the corresponding bits in a
	   dm_set_eventlist() event mask.  A call with zero regions is
	   equivalent to clearing all region flags.
	*/

	new_mask = 0;
	if (nelem == 1) {
		if (copy_from_user( &region, regbufp, sizeof(region)))
			return(-EFAULT);

		if (region.rg_flags & ~(DM_REGION_READ|DM_REGION_WRITE|DM_REGION_TRUNCATE))
			return(-EINVAL);
		if (region.rg_flags & DM_REGION_READ)
			new_mask |= 1 << DM_EVENT_READ;
		if (region.rg_flags & DM_REGION_WRITE)
			new_mask |= 1 << DM_EVENT_WRITE;
		if (region.rg_flags & DM_REGION_TRUNCATE)
			new_mask |= 1 << DM_EVENT_TRUNCATE;
	}
	mr_mask = (1 << DM_EVENT_READ) | (1 << DM_EVENT_WRITE) | (1 << DM_EVENT_TRUNCATE);

	/* Get the file's existing event mask, clear the old managed region
	   bits, add in the new ones, and update the file's mask.
	*/

	XFS_BHV_LOOKUP(vp, xbdp);
	ip = XFS_BHVTOI(xbdp);

	if (new_mask & prohibited_mr_events(vp)) {
		/* If the change is simply to remove the READ
		 * bit, then that's always okay.  Otherwise, it's busy.
		 */
		dm_eventset_t m1;
		m1 = ip->i_iocore.io_dmevmask & ((1 << DM_EVENT_WRITE) | (1 << DM_EVENT_TRUNCATE));
		if (m1 != new_mask) {
			return -EBUSY;
		}
	}

	mp = ip->i_mount;
	tp = xfs_trans_alloc(mp, XFS_TRANS_SET_DMATTRS);
	error = xfs_trans_reserve(tp, 0, XFS_ICHANGE_LOG_RES (mp), 0, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return(-error); /* Return negative error to DMAPI */
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
		return(-EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
xfs_dm_symlink_by_handle(
	struct inode	*inode,
	dm_right_t	right,
	void __user	*hanp,
	size_t		hlen,
	char		__user *cname,
	char		__user *path)
{
	return(-ENOSYS); /* Return negative errors to DMAPI */
}


STATIC int
xfs_dm_sync_by_handle (
	struct inode	*inode,
	dm_right_t	right)
{
	int		error;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	VOP_FSYNC(vp, FSYNC_WAIT, NULL, (xfs_off_t)0, (xfs_off_t)-1, error);
	return(-error); /* Return negative error to DMAPI */
}


/* ARGSUSED */
STATIC int
xfs_dm_upgrade_right(
	struct inode	*inode,
	dm_right_t	right,
	u_int		type)		/* DM_FSYS_OBJ or zero */
{
#ifdef	DEBUG_RIGHTS
	char		buffer[sizeof(dm_handle_t) * 2 + 1];
	bhv_desc_t	*bdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);

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
	struct inode	*inode,
	dm_right_t	right,
	int		flags,
	dm_off_t	off,
	dm_size_t	len,
	void __user	*bufp,
	int		*rvp)
{
	int		fflag = 0;
	vnode_t		*vp = LINVFS_GET_VP(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if (flags & DM_WRITE_SYNC)
		fflag |= O_SYNC;
	return(-xfs_dm_rdwr(vp, fflag, FMODE_WRITE, off, len, bufp, rvp));
}


STATIC void
xfs_dm_obj_ref_hold(
	struct inode	*inode)
{
	igrab(inode);
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


/*	xfs_dm_send_mmap_event - send events needed for memory mapping a file.
 *
 *	This is a workaround called for files that are about to be
 *	mapped.	 DMAPI events are not being generated at a low enough level
 *	in the kernel for page reads/writes to generate the correct events.
 *	So for memory-mapped files we generate read  or write events for the
 *	whole byte range being mapped.	If the mmap call can never cause a
 *	write to the file, then only a read event is sent.
 *
 *	Code elsewhere prevents adding managed regions to a file while it
 *	is still mapped.
 */

STATIC int
xfs_dm_send_mmap_event(
	struct vm_area_struct *vma,
	unsigned int	wantflag)
{
	vnode_t		*vp;
	xfs_inode_t	*ip;
	bhv_desc_t	*bdp;
	int		error = 0;
	dm_eventtype_t	max_event = DM_EVENT_READ;
	vrwlock_t	locktype;
	xfs_fsize_t	filesize;
	xfs_off_t	length, end_of_area, evsize, offset;
	int		iolock;

	/* Returns negative errors to linvfs layer */

	if (!vma->vm_file)
		return 0;

	vp = LINVFS_GET_VP(vma->vm_file->f_dentry->d_inode);
	ASSERT(vp);

	if (!S_ISREG(vma->vm_file->f_dentry->d_inode->i_mode) ||
	    !(vp->v_vfsp->vfs_flag & VFS_DMI))
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

	XFS_BHV_LOOKUP(vp, bdp);
	ip = XFS_BHVTOI(bdp);

	/* Figure out how much of the file is being requested by the user. */
	offset = 0; /* beginning of file, for now */
	length = 0; /* whole file, for now */

	filesize = ip->i_iocore.io_new_size;
	if (filesize < ip->i_d.di_size) {
		filesize = ip->i_d.di_size;
	}

	/* Set first byte number beyond the map area. */

	if (length) {
		end_of_area = offset + length;
		if (end_of_area > filesize)
			end_of_area = filesize;
	} else {
		end_of_area = filesize;
	}

	/* Set the real amount being mapped. */
	evsize = end_of_area - offset;
	if (evsize < 0)
		evsize = 0;

	if (max_event == DM_EVENT_READ) {
		locktype = VRWLOCK_READ;
		iolock = XFS_IOLOCK_SHARED;
	}
	else {
		locktype = VRWLOCK_WRITE;
		iolock = XFS_IOLOCK_EXCL;
	}

	xfs_ilock(ip, iolock);
	/* If write possible, try a DMAPI write event */
	if ((max_event == DM_EVENT_WRITE) &&
	    DM_EVENT_ENABLED(vp->v_vfsp, ip, max_event)){
		error = xfs_dm_send_data_event(max_event, vp, offset,
					       evsize, 0, &locktype);
		goto out_unlock;
	}

	/* Try a read event if max_event was != DM_EVENT_WRITE or if it
	 * was DM_EVENT_WRITE but the WRITE event was not enabled.
	 */
	if (DM_EVENT_ENABLED (vp->v_vfsp, ip, DM_EVENT_READ)) {
		error = xfs_dm_send_data_event(DM_EVENT_READ, vp, offset,
					       evsize, 0, &locktype);
	}
out_unlock:
	xfs_iunlock(ip, iolock);

	return -error; /* Return negative error to linvfs layer */
}


STATIC int
xfs_dm_send_destroy_event(
	vnode_t		*vp,
	dm_right_t	vp_right)	/* always DM_RIGHT_NULL */
{
	int error;

	/* Returns positive errors to XFS */

	error = dm_send_destroy_event(LINVFS_GET_IP(vp), vp_right);
	error = -error; /* DMAPI returns negative errors */

	return error; /* Return positive error to XFS */
}


STATIC int
xfs_dm_send_namesp_event(
	dm_eventtype_t	event,
	vfs_t		*vfsp,		/* used by PREUNMOUNT */
	vnode_t		*vp1,
	dm_right_t	vp1_right,
	vnode_t		*vp2,
	dm_right_t	vp2_right,
	char		*name1,
	char		*name2,
	mode_t		mode,
	int		retcode,
	int		flags)
{
	int error;

	/* Returns positive errors to XFS */

	error = dm_send_namesp_event(event, vfsp ? vfsp->vfs_super: NULL,
				    LINVFS_GET_IP(vp1), vp1_right,
				    vp2 ? LINVFS_GET_IP(vp2) : NULL, vp2_right,
				    name1, name2,
				    mode, retcode, flags);
	error = -error; /* DMAPI returns negative errors */

	return error; /* Return positive error to XFS */
}


STATIC void
xfs_dm_send_unmount_event(
	vfs_t		*vfsp,
	vnode_t		*vp,		/* NULL if unmount successful */
	dm_right_t	vfsp_right,
	mode_t		mode,
	int		retcode,	/* errno, if unmount failed */
	int		flags)
{
	dm_send_unmount_event(vfsp->vfs_super, vp ? LINVFS_GET_IP(vp) : NULL,
			      vfsp_right, mode, retcode, flags);
}


/*
 * Data migration operations accessed by the rest of XFS.
 * When DMAPI support is configured in, this vector is used.
 */

xfs_dmops_t	xfs_dmcore_xfs = {
	.xfs_send_data		= xfs_dm_send_data_event,
	.xfs_send_mmap		= xfs_dm_send_mmap_event,
	.xfs_send_destroy	= xfs_dm_send_destroy_event,
	.xfs_send_namesp	= xfs_dm_send_namesp_event,
	.xfs_send_unmount	= xfs_dm_send_unmount_event,
};
