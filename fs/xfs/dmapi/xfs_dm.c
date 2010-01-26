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
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_alloc.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
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
#include "xfs_attr_leaf.h"
#include "xfs_inode_item.h"
#include "xfs_vnodeops.h"
#include <dmapi.h>
#include <dmapi_kern.h>
#include "xfs_dm.h"

#include <linux/mount.h>

#define MAXNAMLEN MAXNAMELEN

#define MIN_DIO_SIZE(mp)		((mp)->m_sb.sb_sectsize)
#define MAX_DIO_SIZE(mp)		(INT_MAX & ~(MIN_DIO_SIZE(mp) - 1))

static void up_rw_sems(struct inode *ip, int flags)
{
	if (flags & DM_FLAGS_IALLOCSEM_WR)
		up_write(&ip->i_alloc_sem);
	if (flags & DM_FLAGS_IMUX)
		mutex_unlock(&ip->i_mutex);
}

static void down_rw_sems(struct inode *ip, int flags)
{
	if (flags & DM_FLAGS_IMUX)
		mutex_lock(&ip->i_mutex);
	if (flags & DM_FLAGS_IALLOCSEM_WR)
		down_write(&ip->i_alloc_sem);
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
	dm_fsid_t	fsid;
	void __user	*laststruct;
	dm_dkattrname_t	attrname;
} dm_bulkstat_one_t;

/* In the on-disk inode, DMAPI attribute names consist of the user-provided
   name with the DMATTR_PREFIXSTRING pre-pended.  This string must NEVER be
   changed!
*/

static	const	char	dmattr_prefix[DMATTR_PREFIXLEN + 1] = DMATTR_PREFIXSTRING;

static	dm_size_t  dm_min_dio_xfer = 0; /* direct I/O disabled for now */


/* See xfs_dm_get_dmattr() for a description of why this is needed. */

#define XFS_BUG_KLUDGE	256	/* max size of an in-inode attribute value */

#define DM_MAX_ATTR_BYTES_ON_DESTROY	256

#define DM_STAT_SIZE(dmtype,namelen)	\
	(sizeof(dmtype) + sizeof(dm_handle_t) + namelen)

#define DM_STAT_ALIGN		(sizeof(__uint64_t))

/* DMAPI's E2BIG == EA's ERANGE */
#define DM_EA_XLATE_ERR(err) { if (err == ERANGE) err = E2BIG; }

static inline size_t dm_stat_align(size_t size)
{
	return (size + (DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);
}

static inline size_t dm_stat_size(size_t namelen)
{
	return dm_stat_align(sizeof(dm_stat_t) + sizeof(dm_handle_t) + namelen);
}

/*
 *	xfs_dm_send_data_event()
 *
 *	Send data event to DMAPI.  Drop IO lock (if specified) before
 *	the dm_send_data_event() call and reacquire it afterwards.
 */
int
xfs_dm_send_data_event(
	dm_eventtype_t	event,
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	size_t		length,
	int		flags,
	int		*lock_flags)
{
	struct inode	*inode = &ip->i_vnode;
	int		error;
	uint16_t	dmstate;

	/* Returns positive errors to XFS */

	do {
		dmstate = ip->i_d.di_dmstate;
		if (lock_flags)
			xfs_iunlock(ip, *lock_flags);

		up_rw_sems(inode, flags);

		error = dm_send_data_event(event, inode, DM_RIGHT_NULL,
				offset, length, flags);
		error = -error; /* DMAPI returns negative errors */

		down_rw_sems(inode, flags);

		if (lock_flags)
			xfs_ilock(ip, *lock_flags);
	} while (!error && (ip->i_d.di_dmstate != dmstate));

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
	struct address_space *mapping)
{
	int prohibited = (1 << DM_EVENT_READ);

	if (!mapping_mapped(mapping))
		return 0;

	spin_lock(&mapping->i_mmap_lock);
	if (mapping_writably_mapped(mapping))
		prohibited |= (1 << DM_EVENT_WRITE);
	spin_unlock(&mapping->i_mmap_lock);

	return prohibited;
}

#ifdef	DEBUG_RIGHTS
STATIC int
xfs_vp_to_hexhandle(
	struct inode	*inode,
	u_int		type,
	char		*buffer)
{
	dm_handle_t	handle;
	u_char		*ip;
	int		length;
	int		error;
	int		i;

	/*
	 * XXX: dm_vp_to_handle doesn't exist.
	 * 	Looks like this debug code is rather dead.
	 */
	if ((error = dm_vp_to_handle(inode, &handle)))
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
        if (len == 0)
            error = EFAULT;
        else {
	   if (copy_from_user(&to->dan_chars[DMATTR_PREFIXLEN], from, len))
		to->dan_chars[sizeof(to->dan_chars) - 1] = '\0';
	   else if (to->dan_chars[DMATTR_PREFIXLEN] == '\0')
		error = EINVAL;
	   else
		to->dan_chars[DMATTR_PREFIXLEN + len - 1] = '\0';
        }

	return error;
}


/*
 * Convert the XFS flags into their DMAPI flag equivalent for export
 */
STATIC uint
_xfs_dic2dmflags(
	__uint16_t		di_flags)
{
	uint			flags = 0;

	if (di_flags & XFS_DIFLAG_ANY) {
		if (di_flags & XFS_DIFLAG_REALTIME)
			flags |= DM_XFLAG_REALTIME;
		if (di_flags & XFS_DIFLAG_PREALLOC)
			flags |= DM_XFLAG_PREALLOC;
		if (di_flags & XFS_DIFLAG_IMMUTABLE)
			flags |= DM_XFLAG_IMMUTABLE;
		if (di_flags & XFS_DIFLAG_APPEND)
			flags |= DM_XFLAG_APPEND;
		if (di_flags & XFS_DIFLAG_SYNC)
			flags |= DM_XFLAG_SYNC;
		if (di_flags & XFS_DIFLAG_NOATIME)
			flags |= DM_XFLAG_NOATIME;
		if (di_flags & XFS_DIFLAG_NODUMP)
			flags |= DM_XFLAG_NODUMP;
	}
	return flags;
}

STATIC uint
xfs_ip2dmflags(
	xfs_inode_t	*ip)
{
	return _xfs_dic2dmflags(ip->i_d.di_flags) |
			(XFS_IFORK_Q(ip) ? DM_XFLAG_HASATTR : 0);
}

STATIC uint
xfs_dic2dmflags(
	xfs_dinode_t	*dip)
{
	return _xfs_dic2dmflags(be16_to_cpu(dip->di_flags)) |
			(XFS_DFORK_Q(dip) ? DM_XFLAG_HASATTR : 0);
}

/*
 * This copies selected fields in an inode into a dm_stat structure.  Because
 * these fields must return the same values as they would in stat(), the
 * majority of this code was copied directly from xfs_getattr().  Any future
 * changes to xfs_gettattr() must also be reflected here.
 */
STATIC void
xfs_dip_to_stat(
	xfs_mount_t		*mp,
	xfs_ino_t		ino,
	xfs_dinode_t		*dip,
	dm_stat_t		*buf)
{
	xfs_dinode_t	*dic = dip;

	/*
	 * The inode format changed when we moved the link count and
	 * made it 32 bits long.  If this is an old format inode,
	 * convert it in memory to look like a new one.  If it gets
	 * flushed to disk we will convert back before flushing or
	 * logging it.  We zero out the new projid field and the old link
	 * count field.  We'll handle clearing the pad field (the remains
	 * of the old uuid field) when we actually convert the inode to
	 * the new format. We don't change the version number so that we
	 * can distinguish this from a real new format inode.
	 */
	if (dic->di_version == 1) {
		buf->dt_nlink = be16_to_cpu(dic->di_onlink);
		/*buf->dt_xfs_projid = 0;*/
	} else {
		buf->dt_nlink = be32_to_cpu(dic->di_nlink);
		/*buf->dt_xfs_projid = be16_to_cpu(dic->di_projid);*/
	}
	buf->dt_ino = ino;
	buf->dt_dev = new_encode_dev(mp->m_ddev_targp->bt_dev);
	buf->dt_mode = be16_to_cpu(dic->di_mode);
	buf->dt_uid = be32_to_cpu(dic->di_uid);
	buf->dt_gid = be32_to_cpu(dic->di_gid);
	buf->dt_size = be64_to_cpu(dic->di_size);
	buf->dt_atime = be32_to_cpu(dic->di_atime.t_sec);
	buf->dt_mtime = be32_to_cpu(dic->di_mtime.t_sec);
	buf->dt_ctime = be32_to_cpu(dic->di_ctime.t_sec);
	buf->dt_xfs_xflags = xfs_dic2dmflags(dip);
	buf->dt_xfs_extsize =
		be32_to_cpu(dic->di_extsize) << mp->m_sb.sb_blocklog;
	buf->dt_xfs_extents = be32_to_cpu(dic->di_nextents);
	buf->dt_xfs_aextents = be16_to_cpu(dic->di_anextents);
	buf->dt_xfs_igen = be32_to_cpu(dic->di_gen);
	buf->dt_xfs_dmstate = be16_to_cpu(dic->di_dmstate);

	switch (dic->di_format) {
	case XFS_DINODE_FMT_DEV:
		buf->dt_rdev = xfs_dinode_get_rdev(dic);
		buf->dt_blksize = BLKDEV_IOSIZE;
		buf->dt_blocks = 0;
		break;
	case XFS_DINODE_FMT_LOCAL:
	case XFS_DINODE_FMT_UUID:
		buf->dt_rdev = 0;
		buf->dt_blksize = mp->m_sb.sb_blocksize;
		buf->dt_blocks = 0;
		break;
	case XFS_DINODE_FMT_EXTENTS:
	case XFS_DINODE_FMT_BTREE:
		buf->dt_rdev = 0;
		buf->dt_blksize = mp->m_sb.sb_blocksize;
		buf->dt_blocks =
			XFS_FSB_TO_BB(mp, be64_to_cpu(dic->di_nblocks));
		break;
	}

	memset(&buf->dt_pad1, 0, sizeof(buf->dt_pad1));
	memset(&buf->dt_pad2, 0, sizeof(buf->dt_pad2));
	memset(&buf->dt_pad3, 0, sizeof(buf->dt_pad3));

	/* Finally fill in the DMAPI specific fields */
	buf->dt_pers = 0;
	buf->dt_change = 0;
	buf->dt_nevents = DM_EVENT_MAX;
	buf->dt_emask = be32_to_cpu(dic->di_dmevmask);
	buf->dt_dtime = be32_to_cpu(dic->di_ctime.t_sec);
	/* Set if one of READ, WRITE or TRUNCATE bits is set in emask */
	buf->dt_pmanreg = (DMEV_ISSET(DM_EVENT_READ, buf->dt_emask) ||
			DMEV_ISSET(DM_EVENT_WRITE, buf->dt_emask) ||
			DMEV_ISSET(DM_EVENT_TRUNCATE, buf->dt_emask)) ? 1 : 0;
}

/*
 * Pull out both ondisk and incore fields, incore has preference.
 * The inode must be kept locked SHARED by the caller.
 */
STATIC void
xfs_ip_to_stat(
	xfs_mount_t		*mp,
	xfs_ino_t		ino,
	xfs_inode_t		*ip,
	dm_stat_t		*buf)
{
	xfs_icdinode_t		*dic = &ip->i_d;

	buf->dt_ino = ino;
	buf->dt_nlink = dic->di_nlink;
	/*buf->dt_xfs_projid = dic->di_projid;*/
	buf->dt_mode = dic->di_mode;
	buf->dt_uid = dic->di_uid;
	buf->dt_gid = dic->di_gid;
	buf->dt_size = XFS_ISIZE(ip);
	buf->dt_dev = new_encode_dev(mp->m_ddev_targp->bt_dev);
	buf->dt_atime = VFS_I(ip)->i_atime.tv_sec;
	buf->dt_mtime = dic->di_mtime.t_sec;
	buf->dt_ctime = dic->di_ctime.t_sec;
	buf->dt_xfs_xflags = xfs_ip2dmflags(ip);
	buf->dt_xfs_extsize = dic->di_extsize << mp->m_sb.sb_blocklog;
	buf->dt_xfs_extents = dic->di_nextents;
	buf->dt_xfs_aextents = dic->di_anextents;
	buf->dt_xfs_igen = dic->di_gen;
	buf->dt_xfs_dmstate = dic->di_dmstate;

	switch (dic->di_format) {
	case XFS_DINODE_FMT_DEV:
		buf->dt_rdev = ip->i_df.if_u2.if_rdev;
		buf->dt_blksize = BLKDEV_IOSIZE;
		buf->dt_blocks = 0;
		break;
	case XFS_DINODE_FMT_LOCAL:
	case XFS_DINODE_FMT_UUID:
		buf->dt_rdev = 0;
		buf->dt_blksize = mp->m_sb.sb_blocksize;
		buf->dt_blocks = 0;
		break;
	case XFS_DINODE_FMT_EXTENTS:
	case XFS_DINODE_FMT_BTREE:
		buf->dt_rdev = 0;
		buf->dt_blksize = mp->m_sb.sb_blocksize;
		buf->dt_blocks = XFS_FSB_TO_BB(mp,
				(dic->di_nblocks + ip->i_delayed_blks));
		break;
	}

	memset(&buf->dt_pad1, 0, sizeof(buf->dt_pad1));
	memset(&buf->dt_pad2, 0, sizeof(buf->dt_pad2));
	memset(&buf->dt_pad3, 0, sizeof(buf->dt_pad3));

	/* Finally fill in the DMAPI specific fields */
	buf->dt_pers = 0;
	buf->dt_change = 0;
	buf->dt_nevents = DM_EVENT_MAX;
	buf->dt_emask = dic->di_dmevmask;
	buf->dt_dtime = dic->di_ctime.t_sec;
	/* Set if one of READ, WRITE or TRUNCATE bits is set in emask */
	buf->dt_pmanreg = (DMEV_ISSET(DM_EVENT_READ, buf->dt_emask) ||
			DMEV_ISSET(DM_EVENT_WRITE, buf->dt_emask) ||
			DMEV_ISSET(DM_EVENT_TRUNCATE, buf->dt_emask)) ? 1 : 0;
}

/*
 * Take the handle and put it at the end of a dm_xstat buffer.
 * dt_compname is unused in bulkstat - so we zero it out.
 * Finally, update link in dm_xstat_t to point to next struct.
 */
STATIC void
xfs_dm_handle_to_xstat(
	dm_xstat_t	*xbuf,
	size_t		xstat_sz,
	dm_handle_t	*handle,
	size_t		handle_sz)
{
	dm_stat_t	*sbuf = &xbuf->dx_statinfo;

	memcpy(xbuf + 1, handle, handle_sz);
	sbuf->dt_handle.vd_offset = (ssize_t) sizeof(dm_xstat_t);
	sbuf->dt_handle.vd_length = (size_t) DM_HSIZE(*handle);
	memset(&sbuf->dt_compname, 0, sizeof(dm_vardata_t));
	sbuf->_link = xstat_sz;
}

STATIC int
xfs_dm_bulkall_iget_one(
	xfs_mount_t	*mp,
	xfs_ino_t	ino,
	xfs_daddr_t	bno,
	int		*value_lenp,
	dm_xstat_t	*xbuf,
	u_int		*xstat_szp,
	char		*attr_name,
	caddr_t		attr_buf)
{
	xfs_inode_t	*ip;
	dm_handle_t	handle;
	u_int		xstat_sz = *xstat_szp;
	int		value_len = *value_lenp;
	int		error;

	error = xfs_iget(mp, NULL, ino,
			 XFS_IGET_BULKSTAT, XFS_ILOCK_SHARED, &ip, bno);
	if (error)
		return error;

	xfs_ip_to_stat(mp, ino, ip, &xbuf->dx_statinfo);
	dm_ip_to_handle(&ip->i_vnode, &handle);
	xfs_dm_handle_to_xstat(xbuf, xstat_sz, &handle, sizeof(handle));

	/* Drop ILOCK_SHARED for call to xfs_attr_get */
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	memset(&xbuf->dx_attrdata, 0, sizeof(dm_vardata_t));
	error = xfs_attr_get(ip, attr_name, attr_buf, &value_len, ATTR_ROOT);
	iput(&ip->i_vnode);

	DM_EA_XLATE_ERR(error);
	if (error && (error != ENOATTR)) {
		if (error == E2BIG)
			error = ENOMEM;
		return error;
	}

	/* How much space was in the attr? */
	if (error != ENOATTR) {
		xbuf->dx_attrdata.vd_offset = xstat_sz;
		xbuf->dx_attrdata.vd_length = value_len;
		xstat_sz += (value_len+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);
	}
	*xstat_szp = xbuf->dx_statinfo._link = xstat_sz;
	*value_lenp = value_len;
	return 0;
}


STATIC int
xfs_dm_inline_attr(
	xfs_mount_t	*mp,
	xfs_dinode_t	*dip,
	char		*attr_name,
	caddr_t		attr_buf,
	int		*value_lenp)
{
	if (dip->di_aformat == XFS_DINODE_FMT_LOCAL) {
		xfs_attr_shortform_t	*sf;
		xfs_attr_sf_entry_t	*sfe;
		unsigned int		namelen = strlen(attr_name);
		unsigned int		valuelen = *value_lenp;
		int			i;

		sf = (xfs_attr_shortform_t *)XFS_DFORK_APTR(dip);
		sfe = &sf->list[0];
		for (i = 0; i < sf->hdr.count;
				sfe = XFS_ATTR_SF_NEXTENTRY(sfe), i++) {
			if (sfe->namelen != namelen)
				continue;
			if (!(sfe->flags & XFS_ATTR_ROOT))
				continue;
			if (memcmp(attr_name, sfe->nameval, namelen) != 0)
				continue;
			if (valuelen < sfe->valuelen)
				return ERANGE;
			valuelen = sfe->valuelen;
			memcpy(attr_buf, &sfe->nameval[namelen], valuelen);
			*value_lenp = valuelen;
			return 0;
		}
	}
	*value_lenp = 0;
	return ENOATTR;
}

STATIC void
dm_dip_to_handle(
	xfs_ino_t	ino,
	xfs_dinode_t	*dip,
	dm_fsid_t	*fsid,
	dm_handle_t	*handlep)
{
	dm_fid_t	fid;
	int		hsize;

	fid.dm_fid_len = sizeof(struct dm_fid) - sizeof(fid.dm_fid_len);
	fid.dm_fid_pad = 0;
	fid.dm_fid_ino = ino;
	fid.dm_fid_gen = be32_to_cpu(dip->di_gen);

	memcpy(&handlep->ha_fsid, fsid, sizeof(*fsid));
	memcpy(&handlep->ha_fid, &fid, fid.dm_fid_len + sizeof(fid.dm_fid_len));
	hsize = DM_HSIZE(*handlep);
	memset((char *)handlep + hsize, 0, sizeof(*handlep) - hsize);
}

STATIC int
xfs_dm_bulkall_inline_one(
	xfs_mount_t	*mp,
	xfs_ino_t	ino,
	xfs_dinode_t	*dip,
	dm_fsid_t	*fsid,
	int		*value_lenp,
	dm_xstat_t	*xbuf,
	u_int		*xstat_szp,
	char		*attr_name,
	caddr_t		attr_buf)
{
	dm_handle_t	handle;
	u_int		xstat_sz = *xstat_szp;
	int		value_len = *value_lenp;
	int		error;

	if (dip->di_mode == 0)
		return ENOENT;

	xfs_dip_to_stat(mp, ino, dip, &xbuf->dx_statinfo);
	dm_dip_to_handle(ino, dip, fsid, &handle);
	xfs_dm_handle_to_xstat(xbuf, xstat_sz, &handle, sizeof(handle));

	memset(&xbuf->dx_attrdata, 0, sizeof(dm_vardata_t));
	error = xfs_dm_inline_attr(mp, dip, attr_name, attr_buf, &value_len);
	DM_EA_XLATE_ERR(error);
	if (error && (error != ENOATTR)) {
		if (error == E2BIG)
			error = ENOMEM;
		return error;
	}

	/* How much space was in the attr? */
	if (error != ENOATTR) {
		xbuf->dx_attrdata.vd_offset = xstat_sz;
		xbuf->dx_attrdata.vd_length = value_len;
		xstat_sz += (value_len+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);
	}
	*xstat_szp = xbuf->dx_statinfo._link = xstat_sz;
	*value_lenp = value_len;
	return 0;
}

/*
 * This is used by dm_get_bulkall().
 * Given a inumber, it igets the inode and fills the given buffer
 * with the dm_xstat structure for the file.
 */
STATIC int
xfs_dm_bulkall_one(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_ino_t	ino,		/* inode number to get data for */
	void		__user *buffer,	/* buffer to place output in */
	int		ubsize,		/* size of buffer */
	void		*private_data,	/* my private data */
	xfs_daddr_t	bno,		/* starting block of inode cluster */
	int		*ubused,	/* amount of buffer we used */
	void		*dibuff,	/* on-disk inode buffer */
	int		*res)		/* bulkstat result code */
{
	dm_xstat_t	*xbuf;
	u_int		xstat_sz;
	int		error;
	int		value_len;
	int		kern_buf_sz;
	int		attr_buf_sz;
	caddr_t		attr_buf;
	void __user	*attr_user_buf;
	dm_bulkstat_one_t *dmb = (dm_bulkstat_one_t*)private_data;

	/* Returns positive errors to XFS */

	*res = BULKSTAT_RV_NOTHING;

	if (!buffer || xfs_internal_inum(mp, ino))
		return EINVAL;

	xstat_sz = DM_STAT_SIZE(*xbuf, 0);
	xstat_sz = (xstat_sz + (DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);
	if (xstat_sz > ubsize)
		return ENOMEM;

	kern_buf_sz = xstat_sz;
	xbuf = kmem_alloc(kern_buf_sz, KM_SLEEP);

	/* Determine place to drop attr value, and available space. */
	value_len = ubsize - xstat_sz;
	if (value_len > ATTR_MAX_VALUELEN)
		value_len = ATTR_MAX_VALUELEN;

	attr_user_buf = buffer + xstat_sz;
	attr_buf_sz = value_len;
	attr_buf = kmem_alloc(attr_buf_sz, KM_SLEEP);

	if (!dibuff)
		error = xfs_dm_bulkall_iget_one(mp, ino, bno,
						&value_len, xbuf, &xstat_sz,
						dmb->attrname.dan_chars,
						attr_buf);
	else
		error = xfs_dm_bulkall_inline_one(mp, ino,
						  (xfs_dinode_t *)dibuff,
						  &dmb->fsid,
						  &value_len, xbuf, &xstat_sz,
						  dmb->attrname.dan_chars,
						  attr_buf);
	if (error)
		goto out_free_buffers;

	if (copy_to_user(buffer, xbuf, kern_buf_sz)) {
		error = EFAULT;
		goto out_free_buffers;
	}
	if (copy_to_user(attr_user_buf, attr_buf, value_len)) {
		error = EFAULT;
		goto out_free_buffers;
	}

	kmem_free(attr_buf);
	kmem_free(xbuf);

	*res = BULKSTAT_RV_DIDONE;
	if (ubused)
		*ubused = xstat_sz;
	dmb->laststruct = buffer;
	return 0;

 out_free_buffers:
	kmem_free(attr_buf);
	kmem_free(xbuf);
	return error;
}

/*
 * Take the handle and put it at the end of a dm_stat buffer.
 * dt_compname is unused in bulkstat - so we zero it out.
 * Finally, update link in dm_stat_t to point to next struct.
 */
STATIC void
xfs_dm_handle_to_stat(
	dm_stat_t	*sbuf,
	size_t		stat_sz,
	dm_handle_t	*handle,
	size_t		handle_sz)
{
	memcpy(sbuf + 1, handle, handle_sz);
	sbuf->dt_handle.vd_offset = (ssize_t) sizeof(dm_stat_t);
	sbuf->dt_handle.vd_length = (size_t) DM_HSIZE(*handle);
	memset(&sbuf->dt_compname, 0, sizeof(dm_vardata_t));
	sbuf->_link = stat_sz;
}

STATIC int
xfs_dm_bulkattr_iget_one(
	xfs_mount_t	*mp,
	xfs_ino_t	ino,
	xfs_daddr_t	bno,
	dm_stat_t	*sbuf,
	u_int		stat_sz)
{
	xfs_inode_t	*ip;
	dm_handle_t	handle;
	int		error;

	error = xfs_iget(mp, NULL, ino,
			 XFS_IGET_BULKSTAT, XFS_ILOCK_SHARED, &ip, bno);
	if (error)
		return error;

	xfs_ip_to_stat(mp, ino, ip, sbuf);
	dm_ip_to_handle(&ip->i_vnode, &handle);
	xfs_dm_handle_to_stat(sbuf, stat_sz, &handle, sizeof(handle));

	xfs_iput(ip, XFS_ILOCK_SHARED);
	return 0;
}

STATIC int
xfs_dm_bulkattr_inline_one(
	xfs_mount_t	*mp,
	xfs_ino_t	ino,
	xfs_dinode_t	*dip,
	dm_fsid_t	*fsid,
	dm_stat_t	*sbuf,
	u_int		stat_sz)
{
	dm_handle_t	handle;

	if (dip->di_mode == 0)
		return ENOENT;
	xfs_dip_to_stat(mp, ino, dip, sbuf);
	dm_dip_to_handle(ino, dip, fsid, &handle);
	xfs_dm_handle_to_stat(sbuf, stat_sz, &handle, sizeof(handle));
	return 0;
}

/*
 * This is used by dm_get_bulkattr().
 * Given a inumber, it igets the inode and fills the given buffer
 * with the dm_stat structure for the file.
 */
STATIC int
xfs_dm_bulkattr_one(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_ino_t	ino,		/* inode number to get data for */
	void		__user *buffer,	/* buffer to place output in */
	int		ubsize,		/* size of buffer */
	void		*private_data,	/* my private data */
	xfs_daddr_t	bno,		/* starting block of inode cluster */
	int		*ubused,	/* amount of buffer we used */
	void		*dibuff,	/* on-disk inode buffer */
	int		*res)		/* bulkstat result code */
{
	dm_stat_t	*sbuf;
	u_int		stat_sz;
	int		error;
	dm_bulkstat_one_t *dmb = (dm_bulkstat_one_t*)private_data;

	/* Returns positive errors to XFS */

	*res = BULKSTAT_RV_NOTHING;

	if (!buffer || xfs_internal_inum(mp, ino))
		return EINVAL;

	stat_sz = DM_STAT_SIZE(*sbuf, 0);
	stat_sz = (stat_sz+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);
	if (stat_sz > ubsize)
		return ENOMEM;

	sbuf = kmem_alloc(stat_sz, KM_SLEEP);

	if (!dibuff)
		error = xfs_dm_bulkattr_iget_one(mp, ino, bno, sbuf, stat_sz);
	else
		error = xfs_dm_bulkattr_inline_one(mp, ino,
						   (xfs_dinode_t *)dibuff,
						   &dmb->fsid, sbuf, stat_sz);
	if (error)
		goto out_free_buffer;

	if (copy_to_user(buffer, sbuf, stat_sz)) {
		error = EFAULT;
		goto out_free_buffer;
	}

	kmem_free(sbuf);
	*res = BULKSTAT_RV_DIDONE;
	if (ubused)
		*ubused = stat_sz;
	dmb->laststruct = buffer;
	return 0;

 out_free_buffer:
	kmem_free(sbuf);
	return error;
}

/* xfs_dm_f_get_eventlist - return the dm_eventset_t mask for inode ip. */

STATIC int
xfs_dm_f_get_eventlist(
	xfs_inode_t	*ip,
	dm_right_t	right,
	u_int		nelem,
	dm_eventset_t	*eventsetp,		/* in kernel space! */
	u_int		*nelemp)		/* in kernel space! */
{
	dm_eventset_t	eventset;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

	/* Note that we MUST return a regular file's managed region bits as
	   part of the mask because dm_get_eventlist is supposed to return the
	   union of all managed region flags in those bits.  Since we only
	   support one region, we can just return the bits as they are.	 For
	   all other object types, the bits will already be zero.  Handy, huh?
	*/

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
	xfs_inode_t	*ip,
	dm_right_t	right,
	dm_eventset_t	*eventsetp,	/* in kernel space! */
	u_int		maxevent)
{
	dm_eventset_t	eventset;
	dm_eventset_t	max_mask;
	dm_eventset_t	valid_events;
	xfs_trans_t	*tp;
	xfs_mount_t	*mp;
	int		error;

	if (right < DM_RIGHT_EXCL)
		return(EACCES);

	eventset = *eventsetp;
	if (maxevent >= sizeof(ip->i_d.di_dmevmask) * NBBY)
		return(EINVAL);
	max_mask = (1 << maxevent) - 1;

	if (S_ISDIR(ip->i_d.di_mode)) {
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

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	igrab(&ip->i_vnode);
	xfs_trans_commit(tp, 0);

	return(0);
}


/* xfs_dm_fs_get_eventlist - return the dm_eventset_t mask for filesystem vfsp. */

STATIC int
xfs_dm_fs_get_eventlist(
	xfs_mount_t	*mp,
	dm_right_t	right,
	u_int		nelem,
	dm_eventset_t	*eventsetp,		/* in kernel space! */
	u_int		*nelemp)		/* in kernel space! */
{
	dm_eventset_t	eventset;

	if (right < DM_RIGHT_SHARED)
		return(EACCES);

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
	xfs_mount_t	*mp,
	dm_right_t	right,
	dm_eventset_t	*eventsetp,	/* in kernel space! */
	u_int		maxevent)
{
	dm_eventset_t	eventset;
	dm_eventset_t	max_mask;

	if (right < DM_RIGHT_EXCL)
		return(EACCES);

	eventset = *eventsetp;

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
	xfs_inode_t	*ip,
	dm_off_t	off,
	dm_size_t	len,
	void		__user *bufp)
{
	xfs_mount_t	*mp;

	mp = ip->i_mount;

	/* Realtime files can ONLY do direct I/O. */

	if (XFS_IS_REALTIME_INODE(ip))
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
	struct inode	*inode,
	uint		fflag,
	mode_t		fmode,
	dm_off_t	off,
	dm_size_t	len,
	void		__user *bufp,
	int		*rvp)
{
	const struct cred *cred = current_cred();
	xfs_inode_t	*ip = XFS_I(inode);
	int		error;
	int		oflags;
	ssize_t		xfer;
	struct file	*file;
	struct dentry	*dentry;

	if ((off < 0) || (off > i_size_read(inode)) || !S_ISREG(inode->i_mode))
		return EINVAL;

	if (fmode & FMODE_READ) {
		oflags = O_RDONLY;
	} else {
		oflags = O_WRONLY;
	}

	/*
	 * Build file descriptor flags and I/O flags.  O_NONBLOCK is needed so
	 * that we don't block on mandatory file locks. This is an invisible IO,
	 * don't change the atime.
	 */

	oflags |= O_LARGEFILE | O_NONBLOCK | O_NOATIME;
	if (xfs_dm_direct_ok(ip, off, len, bufp))
		oflags |= O_DIRECT;

	if (fflag & O_SYNC)
		oflags |= O_SYNC;

	if (inode->i_fop == NULL) {
		/* no iput; caller did get, and will do put */
		return EINVAL;
	}

	igrab(inode);

	dentry = d_obtain_alias(inode);
	if (dentry == NULL) {
		iput(inode);
		return ENOMEM;
	}

	file = dentry_open(dentry, mntget(ip->i_mount->m_vfsmount), oflags,
			   cred);
	if (IS_ERR(file)) {
		return -PTR_ERR(file);
	}
	file->f_mode |= FMODE_NOCMTIME;

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

	if (!xfs_vp_to_hexhandle(inode, type, buffer)) {
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
   interface.
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
	xfs_inode_t	*ip = XFS_I(inode);
	xfs_mount_t	*mp;		/* file system mount point */
	xfs_fileoff_t	fsb_offset;
	xfs_filblks_t	fsb_length;
	dm_off_t	startoff;
	int		elem;
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
			XFS_BMAPI_ENTIRE, NULL, 0, bmp, &num, NULL, NULL);

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
		kmem_free(bmp);
	return(error);
}


STATIC int
xfs_dm_zero_xstatinfo_link(
	dm_xstat_t __user	*dxs)
{
	dm_xstat_t		*ldxs;
	int			error = 0;

	if (!dxs)
		return 0;
	ldxs = kmalloc(sizeof(*ldxs), GFP_KERNEL);
	if (!ldxs)
		return -ENOMEM;
	if (copy_from_user(ldxs, dxs, sizeof(*dxs))) {
		error = -EFAULT;
	} else {
		ldxs->dx_statinfo._link = 0;
		if (copy_to_user(dxs, ldxs, sizeof(*dxs)))
			error = -EFAULT;
	}
	kfree(ldxs);
	return error;
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
	xfs_mount_t	*mp = XFS_I(inode)->i_mount;
	dm_attrname_t	attrname;
	dm_bulkstat_one_t dmb;

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

	/* Build the on-disk version of the attribute name. */
	strcpy(dmb.attrname.dan_chars, dmattr_prefix);
	strncpy(&dmb.attrname.dan_chars[DMATTR_PREFIXLEN],
		attrname.an_chars, DM_ATTR_NAME_SIZE + 1);
	dmb.attrname.dan_chars[sizeof(dmb.attrname.dan_chars) - 1] = '\0';

	/*
	 * fill the buffer with dm_xstat_t's
	 */

	dmb.laststruct = NULL;
	memcpy(&dmb.fsid, mp->m_fixedfsid, sizeof(dm_fsid_t));
	error = xfs_bulkstat(mp, (xfs_ino_t *)&loc, &nelems,
			     xfs_dm_bulkall_one, (void*)&dmb, statstruct_sz,
			     bufp, BULKSTAT_FG_INLINE, &done);
	if (error)
		return(-error); /* Return negative error to DMAPI */

	*rvalp = !done ? 1 : 0;

	if (put_user( statstruct_sz * nelems, rlenp ))
		return(-EFAULT);

	if (copy_to_user( locp, &loc, sizeof(loc)))
		return(-EFAULT);
	/*
	 *  If we didn't do any, we must not have any more to do.
	 */
	if (nelems < 1)
		return(0);
	/*
	 * Set _link in the last struct to zero
	 */
	return xfs_dm_zero_xstatinfo_link((dm_xstat_t __user *)dmb.laststruct);
}


STATIC int
xfs_dm_zero_statinfo_link(
	dm_stat_t __user	*dxs)
{
	dm_stat_t		*ldxs;
	int			error = 0;

	if (!dxs)
		return 0;
	ldxs = kmalloc(sizeof(*ldxs), GFP_KERNEL);
	if (!ldxs)
		return -ENOMEM;
	if (copy_from_user(ldxs, dxs, sizeof(*dxs))) {
		error = -EFAULT;
	} else {
		ldxs->_link = 0;
		if (copy_to_user(dxs, ldxs, sizeof(*dxs)))
			error = -EFAULT;
	}
	kfree(ldxs);
	return error;
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
	xfs_mount_t	*mp = XFS_I(inode)->i_mount;
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

	dmb.laststruct = NULL;
	memcpy(&dmb.fsid, mp->m_fixedfsid, sizeof(dm_fsid_t));
	error = xfs_bulkstat(mp, (xfs_ino_t *)&loc, &nelems,
				xfs_dm_bulkattr_one, (void*)&dmb,
				statstruct_sz, bufp, BULKSTAT_FG_INLINE, &done);
	if (error)
		return(-error); /* Return negative error to DMAPI */

	*rvalp = !done ? 1 : 0;

	if (put_user( statstruct_sz * nelems, rlenp ))
		return(-EFAULT);

	if (copy_to_user( locp, &loc, sizeof(loc)))
		return(-EFAULT);

	/*
	 *  If we didn't do any, we must not have any more to do.
	 */
	if (nelems < 1)
		return(0);
	/*
	 * Set _link in the last struct to zero
	 */
	return xfs_dm_zero_statinfo_link((dm_stat_t __user *)dmb.laststruct);
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
	dm_dkattrname_t dkattrname;
	int		alloc_size;
	int		value_len;
	char		*value;
	int		error;

	/* Returns negative errors to DMAPI */

	*vlenp = -1;		/* assume failure by default */

	if (attrnamep->an_chars[0] == '\0')
		return(-EINVAL);

	/* Build the on-disk version of the attribute name. */

	strcpy(dkattrname.dan_chars, dmattr_prefix);
	strncpy(&dkattrname.dan_chars[DMATTR_PREFIXLEN],
		(char *)attrnamep->an_chars, DM_ATTR_NAME_SIZE + 1);
	dkattrname.dan_chars[sizeof(dkattrname.dan_chars) - 1] = '\0';

	/* xfs_attr_get will not return anything if the buffer is too small,
	   and we don't know how big to make the buffer, so this may take
	   two tries to get it right.  The initial try must use a buffer of
	   at least XFS_BUG_KLUDGE bytes to prevent buffer overflow because
	   of a bug in XFS.
	*/

	alloc_size = XFS_BUG_KLUDGE;
	value = kmalloc(alloc_size, GFP_KERNEL);
	if (value == NULL)
		return(-ENOMEM);

	error = xfs_attr_get(XFS_I(inode), dkattrname.dan_chars, value,
							&value_len, ATTR_ROOT);
	if (error == ERANGE) {
		kfree(value);
		alloc_size = value_len;
		value = kmalloc(alloc_size, GFP_KERNEL);
		if (value == NULL)
			return(-ENOMEM);

		error = xfs_attr_get(XFS_I(inode), dkattrname.dan_chars, value,
					&value_len, ATTR_ROOT);
	}
	if (error) {
		kfree(value);
		DM_EA_XLATE_ERR(error);
		return(-error); /* Return negative error to DMAPI */
	}

	/* The attribute exists and has a value.  Note that a value_len of
	   zero is valid!
	*/

	if (value_len == 0) {
		kfree(value);
		*vlenp = 0;
		return(0);
	} else if (value_len > DM_MAX_ATTR_BYTES_ON_DESTROY) {
		char	*value2;

		value2 = kmalloc(DM_MAX_ATTR_BYTES_ON_DESTROY, GFP_KERNEL);
		if (value2 == NULL) {
			kfree(value);
			return(-ENOMEM);
		}
		memcpy(value2, value, DM_MAX_ATTR_BYTES_ON_DESTROY);
		kfree(value);
		value = value2;
		value_len = DM_MAX_ATTR_BYTES_ON_DESTROY;
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
	xfs_inode_t	*ip = XFS_I(inode);

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	mp = ip->i_mount;

	dio.d_miniosz = dio.d_mem = MIN_DIO_SIZE(mp);
	dio.d_maxiosz = MAX_DIO_SIZE(mp);
	dio.d_dio_only = DM_FALSE;

	if (copy_to_user(diop, &dio, sizeof(dio)))
		return(-EFAULT);
	return(0);
}

typedef struct dm_readdir_cb {
	xfs_mount_t		*mp;
	char __user		*ubuf;
	dm_stat_t __user	*lastbuf;
	size_t			spaceleft;
	size_t			nwritten;
	int			error;
	dm_stat_t		kstat;
} dm_readdir_cb_t;

STATIC int
dm_filldir(void *__buf, const char *name, int namelen, loff_t offset,
		u64 ino, unsigned int d_type)
{
	dm_readdir_cb_t *cb = __buf;
	dm_stat_t	*statp = &cb->kstat;
	size_t		len;
	int		error;
	int		needed;

	/*
	 * Make sure we have enough space.
	 */
        needed = dm_stat_size(namelen + 1);
	if (cb->spaceleft < needed) {
		cb->spaceleft = 0;
		return -ENOSPC;
	}

	error = -EINVAL;
	if (xfs_internal_inum(cb->mp, ino))
		goto out_err;

	memset(statp, 0, dm_stat_size(MAXNAMLEN));
	error = -xfs_dm_bulkattr_iget_one(cb->mp, ino, 0,
			statp, needed);
	if (error)
		goto out_err;

	/*
	 * On return from bulkstat_one(), stap->_link points
	 * at the end of the handle in the stat structure.
	 */
	statp->dt_compname.vd_offset = statp->_link;
	statp->dt_compname.vd_length = namelen + 1;

	len = statp->_link;

	/* Word-align the record */
	statp->_link = dm_stat_align(len + namelen + 1);

	error = -EFAULT;
	if (copy_to_user(cb->ubuf, statp, len))
		goto out_err;
	if (copy_to_user(cb->ubuf + len, name, namelen))
		goto out_err;
	if (put_user(0, cb->ubuf + len + namelen))
		goto out_err;

	cb->lastbuf = (dm_stat_t __user *)cb->ubuf;
	cb->spaceleft -= statp->_link;
	cb->nwritten += statp->_link;
	cb->ubuf += statp->_link;

	return 0;

 out_err:
	cb->error = error;
	return error;
}

/* Returns negative errors to DMAPI */
STATIC int
xfs_dm_get_dirattrs_rvp(
	struct inode	*inode,
	dm_right_t	right,
	u_int		mask,
	dm_attrloc_t	__user *locp,
	size_t		buflen,
	void		__user *bufp,
	size_t		__user *rlenp,
	int		*rvp)
{
	xfs_inode_t	*dp = XFS_I(inode);
	xfs_mount_t	*mp = dp->i_mount;
	dm_readdir_cb_t	*cb;
	dm_attrloc_t	loc;
	int		error;

	if (right < DM_RIGHT_SHARED)
		return -EACCES;

        /*
         * Make sure that the buffer is properly aligned.
         */
        if (((unsigned long)bufp & (DM_STAT_ALIGN - 1)) != 0)
                return -EFAULT;

	if (mask & ~(DM_AT_HANDLE|DM_AT_EMASK|DM_AT_PMANR|DM_AT_PATTR|
		     DM_AT_DTIME|DM_AT_CFLAG|DM_AT_STAT))
		return -EINVAL;

	if (!S_ISDIR(inode->i_mode))
		return -EINVAL;

        /*
         * bufp should be able to fit at least one dm_stat entry including
         * dt_handle and full size MAXNAMLEN dt_compname.
         */
        if (buflen < dm_stat_size(MAXNAMLEN))
                return -ENOMEM;

	if (copy_from_user(&loc, locp, sizeof(loc)))
		return -EFAULT;

	cb = kzalloc(sizeof(*cb) + dm_stat_size(MAXNAMLEN), GFP_KERNEL);
	if (!cb)
		return -ENOMEM;

	cb->mp = mp;
	cb->spaceleft = buflen;
	cb->ubuf = bufp;

	mutex_lock(&inode->i_mutex);
	error = -ENOENT;
	if (!IS_DEADDIR(inode)) {
		error = -xfs_readdir(dp, cb, dp->i_size,
					 (xfs_off_t *)&loc, dm_filldir);
	}
	mutex_unlock(&inode->i_mutex);

	if (error)
		goto out_kfree;
	if (cb->error) {
		error = cb->error;
		goto out_kfree;
	}

	error = -EFAULT;
	if (cb->lastbuf && put_user(0, &cb->lastbuf->_link))
		goto out_kfree;
	if (put_user(cb->nwritten, rlenp))
		goto out_kfree;
	if (copy_to_user(locp, &loc, sizeof(loc)))
		goto out_kfree;

	if (cb->nwritten)
		*rvp = 1;
	else
		*rvp = 0;
	error = 0;

 out_kfree:
	kfree(cb);
	return error;
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
	value = kmem_alloc(alloc_size, KM_SLEEP | KM_LARGE);

	/* Get the attribute's value. */

	value_len = alloc_size;		/* in/out parameter */

	error = xfs_attr_get(XFS_I(inode), name.dan_chars, value, &value_len,
					ATTR_ROOT);
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

	kmem_free(value);
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
	xfs_inode_t	*ip = XFS_I(inode);

	/* Returns negative errors to DMAPI */

	if (type == DM_FSYS_OBJ) {
		error = xfs_dm_fs_get_eventlist(ip->i_mount, right, nelem,
			eventsetp, nelemp);
	} else {
		error = xfs_dm_f_get_eventlist(ip, right, nelem,
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
	xfs_inode_t	*ip = XFS_I(inode);
	xfs_mount_t	*mp;

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	/* Find the mount point. */

	mp = ip->i_mount;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	xfs_ip_to_stat(mp, ip->i_ino, ip, &stat);
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
	xfs_inode_t	*ip = XFS_I(inode);
	u_int		elem;

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

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

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	/* Verify that the user gave us a buffer that is 4-byte aligned, lock
	   it down, and work directly within that buffer.  As a side-effect,
	   values of buflen < sizeof(int) return EINVAL.
	*/

	alignment = sizeof(int) - 1;
	if ((((__psint_t)bufp & alignment) != 0) ||
               !access_ok(VERIFY_WRITE, bufp, buflen)) {
		return(-EFAULT);
	}
	buflen &= ~alignment;		/* round down the alignment */

	/* Initialize all the structures and variables for the main loop. */

	memset(&cursor, 0, sizeof(cursor));
	attrlist = (attrlist_t *)kmem_alloc(list_size, KM_SLEEP);
	total_size = 0;
	ulist = (dm_attrlist_t *)bufp;
	last_link = NULL;

	/* Use vop_attr_list to get the names of DMAPI attributes, and use
	   vop_attr_get to get their values.  There is a risk here that the
	   DMAPI attributes could change between the vop_attr_list and
	   vop_attr_get calls.	If we can detect it, we return EIO to notify
	   the user.
	*/

	do {
		int	i;

		/* Get a buffer full of attribute names.  If there aren't any
		   more or if we encounter an error, then finish up.
		*/

		error = xfs_attr_list(XFS_I(inode), (char *)attrlist, list_size,
						ATTR_ROOT, &cursor);
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

			error = xfs_attr_get(XFS_I(inode), entry->a_name,
						(void *)(ulist + 1), &value_len,
						ATTR_ROOT);
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

	kmem_free(attrlist);
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
   work for inode-based routines (dm_get_dirattrs) and filesystem-based
   routines (dm_get_bulkattr and dm_get_bulkall).  Filesystem-based functions
   call this routine using the filesystem's root inode.
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


/*
 * Probe and Punch
 *
 * Hole punching alignment is based on the underlying device base
 * allocation size. Because it is not defined in the DMAPI spec, we
 * can align how we choose here. Round inwards (offset up and length
 * down) to the block, extent or page size whichever is bigger. Our
 * DMAPI implementation rounds the hole geometry strictly inwards. If
 * this is not possible, return EINVAL for both for xfs_dm_probe_hole
 * and xfs_dm_punch_hole which differs from the DMAPI spec.  Note that
 * length = 0 is special - it means "punch to EOF" and at that point
 * we treat the punch as remove everything past offset (including
 * preallocation past EOF).
 */

STATIC int
xfs_dm_round_hole(
	dm_off_t	offset,
	dm_size_t	length,
	dm_size_t	align,
	xfs_fsize_t	filesize,
	dm_off_t	*roff,
	dm_size_t	*rlen)
{

	dm_off_t	off = offset;
	dm_size_t	len = length;

	/* Try to round offset up to the nearest boundary */
	*roff = roundup_64(off, align);
	if ((*roff >= filesize) || (len && (len < align)))
		return -EINVAL;

	if ((len == 0) || ((off + len) == filesize)) {
		/* punch to EOF */
		*rlen = 0;
	} else {
		/* Round length down to the nearest boundary. */
		ASSERT(len >= align);
		ASSERT(align > (*roff - off));
		len -= *roff - off;
		*rlen = len - do_mod(len, align);
		if (*rlen == 0)
			return -EINVAL; /* requested length is too small */
	}
#ifdef CONFIG_DMAPI_DEBUG
	printk("xfs_dm_round_hole: off %lu, len %ld, align %lu, "
	       "filesize %llu, roff %ld, rlen %ld\n",
	       offset, length, align, filesize, *roff, *rlen);
#endif
	return 0; /* hole geometry successfully rounded */
}

/* ARGSUSED */
STATIC int
xfs_dm_probe_hole(
	struct inode	*inode,
	dm_right_t	right,
	dm_off_t	off,
	dm_size_t	len,
	dm_off_t	__user	*roffp,
	dm_size_t	__user *rlenp)
{
	dm_off_t	roff;
	dm_size_t	rlen;
	xfs_inode_t	*ip = XFS_I(inode);
	xfs_mount_t	*mp;
	uint		lock_flags;
	xfs_fsize_t	realsize;
	dm_size_t	align;
	int		error;

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return -EACCES;

	if ((ip->i_d.di_mode & S_IFMT) != S_IFREG)
		return -EINVAL;

	mp = ip->i_mount;
	lock_flags = XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL;
	xfs_ilock(ip, lock_flags);
	realsize = ip->i_size;
	xfs_iunlock(ip, lock_flags);

	if ((off + len) > realsize)
		return -E2BIG;

	align = 1 << mp->m_sb.sb_blocklog;

	error = xfs_dm_round_hole(off, len, align, realsize, &roff, &rlen);
	if (error)
		return error;

	if (copy_to_user( roffp, &roff, sizeof(roff)))
		return -EFAULT;
	if (copy_to_user( rlenp, &rlen, sizeof(rlen)))
		return -EFAULT;
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
	xfs_inode_t	*ip = XFS_I(inode);
	xfs_mount_t	*mp;
	dm_size_t	align;
	xfs_fsize_t	realsize;
	dm_off_t	roff;
	dm_size_t	rlen;

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_EXCL)
		return -EACCES;

	/* Make sure there are no leases. */
	error = break_lease(inode, FMODE_WRITE);
	if (error)
		return -EBUSY;

	error = get_write_access(inode);
	if (error)
		return -EBUSY;

	mp = ip->i_mount;

	down_rw_sems(inode, DM_SEM_FLAG_WR);

	xfs_ilock(ip, XFS_ILOCK_EXCL | XFS_IOLOCK_EXCL);
	realsize = ip->i_size;
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	align = xfs_get_extsz_hint(ip);
	if (align == 0)
		align = 1;

	align <<= mp->m_sb.sb_blocklog;

	if ((off + len) > realsize) {
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
		error = -E2BIG;
		goto up_and_out;
	}

	if ((off + len) == realsize)
		len = 0;

	error = xfs_dm_round_hole(off, len, align, realsize, &roff, &rlen);
	if (error || (off != roff) || (len != rlen)) {
		xfs_iunlock(ip, XFS_IOLOCK_EXCL);
		error = -EINVAL;
		goto up_and_out;
	}

	bf.l_type = 0;
	bf.l_whence = 0;
	bf.l_start = (xfs_off_t)off;
	if (len) {
		bf.l_len = len;
	}
	else {
		/*
		 * When we are punching to EOF, we have to make sure we punch
		 * the last partial block that contains EOF. Round up
		 * the length to make sure we punch the block and not just
		 * zero it.
		 */
		bf.l_len = roundup_64((realsize - off), mp->m_sb.sb_blocksize);
	}

#ifdef CONFIG_DMAPI_DEBUG
	printk("xfs_dm_punch_hole: off %lu, len %ld, align %lu\n",
		off, len, align);
#endif

	error = xfs_change_file_space(ip, XFS_IOC_UNRESVSP, &bf,
				(xfs_off_t)off, XFS_ATTR_DMI|XFS_ATTR_NOLOCK);

	/*
	 * if punching to end of file, kill any blocks past EOF that
	 * may have been (speculatively) preallocated. No point in
	 * leaving them around if we are migrating the file....
	 */
	if (!error && (len == 0)) {
		error = xfs_free_eofblocks(mp, ip, XFS_FREE_EOF_HASLOCK);
	}

	/*
	 * negate the error for return here as core XFS functions return
	 * positive error numbers
	 */
	if (error)
		error = -error;

	/* Let threads in send_data_event know we punched the file. */
	ip->i_d.di_dmstate++;
	xfs_iunlock(ip, XFS_IOLOCK_EXCL);

up_and_out:
	up_rw_sems(inode, DM_SEM_FLAG_WR);
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
	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	return(-xfs_dm_rdwr(inode, 0, FMODE_READ, off, len, bufp, rvp));
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

	if (!xfs_vp_to_hexhandle(inode, type, buffer)) {
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

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if ((error = xfs_copyin_attrname(attrnamep, &name)) != 0)
		return(-error); /* Return negative error to DMAPI */

	/* Remove the attribute from the object. */

	error = xfs_attr_remove(XFS_I(inode), name.dan_chars, setdtime ?
				ATTR_ROOT : (ATTR_ROOT|ATTR_KERNOTIME));
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

	if (!xfs_vp_to_hexhandle(inode, type, buffer)) {
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
		error = xfs_attr_set(XFS_I(inode), name.dan_chars, value, buflen,
					setdtime ? ATTR_ROOT :
					(ATTR_ROOT|ATTR_KERNOTIME));
		DM_EA_XLATE_ERR(error);
	}
	kmem_free(value);
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
	xfs_inode_t	*ip = XFS_I(inode);

	/* Returns negative errors to DMAPI */

	if (type == DM_FSYS_OBJ) {
		error = xfs_dm_fs_set_eventlist(ip->i_mount, right, eventsetp, maxevent);
	} else {
		error = xfs_dm_f_set_eventlist(ip, right, eventsetp, maxevent);
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
	struct iattr	iattr;

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if (copy_from_user( &stat, statp, sizeof(stat)))
		return(-EFAULT);

	iattr.ia_valid = 0;

	if (mask & DM_AT_MODE) {
		iattr.ia_valid |= ATTR_MODE;
		iattr.ia_mode = stat.fa_mode;
	}
	if (mask & DM_AT_UID) {
		iattr.ia_valid |= ATTR_UID;
		iattr.ia_uid = stat.fa_uid;
	}
	if (mask & DM_AT_GID) {
		iattr.ia_valid |= ATTR_GID;
		iattr.ia_gid = stat.fa_gid;
	}
	if (mask & DM_AT_ATIME) {
		iattr.ia_valid |= ATTR_ATIME;
		iattr.ia_atime.tv_sec = stat.fa_atime;
		iattr.ia_atime.tv_nsec = 0;
                inode->i_atime.tv_sec = stat.fa_atime;
	}
	if (mask & DM_AT_MTIME) {
		iattr.ia_valid |= ATTR_MTIME;
		iattr.ia_mtime.tv_sec = stat.fa_mtime;
		iattr.ia_mtime.tv_nsec = 0;
	}
	if (mask & DM_AT_CTIME) {
		iattr.ia_valid |= ATTR_CTIME;
		iattr.ia_ctime.tv_sec = stat.fa_ctime;
		iattr.ia_ctime.tv_nsec = 0;
	}

	/*
	 * DM_AT_DTIME only takes effect if DM_AT_CTIME is not specified.  We
	 * overload ctime to also act as dtime, i.e. DM_CONFIG_DTIME_OVERLOAD.
	 */
	if ((mask & DM_AT_DTIME) && !(mask & DM_AT_CTIME)) {
		iattr.ia_valid |= ATTR_CTIME;
		iattr.ia_ctime.tv_sec = stat.fa_dtime;
		iattr.ia_ctime.tv_nsec = 0;
	}
	if (mask & DM_AT_SIZE) {
		iattr.ia_valid |= ATTR_SIZE;
		iattr.ia_size = stat.fa_size;
	}

	return -xfs_setattr(XFS_I(inode), &iattr, XFS_ATTR_DMI);
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
	xfs_inode_t	*ip = XFS_I(inode);
	xfs_trans_t	*tp;
	xfs_mount_t	*mp;
	dm_region_t	region;
	dm_eventset_t	new_mask;
	dm_eventset_t	mr_mask;
	int		error;
	u_int		exactflag;

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

	if (new_mask & prohibited_mr_events(inode->i_mapping)) {
		/* If the change is simply to remove the READ
		 * bit, then that's always okay.  Otherwise, it's busy.
		 */
		dm_eventset_t m1;
		m1 = ip->i_d.di_dmevmask & ((1 << DM_EVENT_WRITE) | (1 << DM_EVENT_TRUNCATE));
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

	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	igrab(inode);
	xfs_trans_commit(tp, 0);

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


/*
 * xfs_dm_sync_by_handle needs to do the same thing as sys_fsync()
 */
STATIC int
xfs_dm_sync_by_handle(
	struct inode	*inode,
	dm_right_t	right)
{
	int		err, ret;
	xfs_inode_t	*ip = XFS_I(inode);

	/* Returns negative errors to DMAPI */
	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	/* We need to protect against concurrent writers.. */
	ret = filemap_fdatawrite(inode->i_mapping);
	down_rw_sems(inode, DM_FLAGS_IMUX);
	err = -xfs_fsync(ip);
	if (!ret)
		ret = err;
	up_rw_sems(inode, DM_FLAGS_IMUX);
	err = filemap_fdatawait(inode->i_mapping);
	if (!ret)
		ret = err;
	xfs_iflags_clear(ip, XFS_ITRUNCATED);
	return ret;
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

	if (!xfs_vp_to_hexhandle(inode, type, buffer)) {
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

	/* Returns negative errors to DMAPI */

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if (flags & DM_WRITE_SYNC)
		fflag |= O_SYNC;
	return(-xfs_dm_rdwr(inode, fflag, FMODE_WRITE, off, len, bufp, rvp));
}


STATIC void
xfs_dm_obj_ref_hold(
	struct inode	*inode)
{
	igrab(inode);
}


static fsys_function_vector_t	xfs_fsys_vector[DM_FSYS_MAX];


STATIC int
xfs_dm_get_dmapiops(
	struct super_block	*sb,
	void			*addr)
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
	xfs_inode_t	*ip;
	int		error = 0;
	dm_eventtype_t	max_event = DM_EVENT_READ;
	xfs_fsize_t	filesize;
	xfs_off_t	length, end_of_area, evsize, offset;
	int		iolock;

	if (!vma->vm_file)
		return 0;

	ip = XFS_I(vma->vm_file->f_dentry->d_inode);

	if (!S_ISREG(vma->vm_file->f_dentry->d_inode->i_mode) ||
	    !(ip->i_mount->m_flags & XFS_MOUNT_DMAPI))
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

	/* Figure out how much of the file is being requested by the user. */
	offset = 0; /* beginning of file, for now */
	length = 0; /* whole file, for now */

	filesize = ip->i_new_size;
	if (filesize < ip->i_size) {
		filesize = ip->i_size;
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

	if (max_event == DM_EVENT_READ)
		iolock = XFS_IOLOCK_SHARED;
	else
		iolock = XFS_IOLOCK_EXCL;

	xfs_ilock(ip, iolock);
	/* If write possible, try a DMAPI write event */
	if (max_event == DM_EVENT_WRITE && DM_EVENT_ENABLED(ip, max_event)) {
		error = xfs_dm_send_data_event(max_event, ip, offset,
					       evsize, 0, &iolock);
		goto out_unlock;
	}

	/* Try a read event if max_event was != DM_EVENT_WRITE or if it
	 * was DM_EVENT_WRITE but the WRITE event was not enabled.
	 */
	if (DM_EVENT_ENABLED(ip, DM_EVENT_READ)) {
		error = xfs_dm_send_data_event(DM_EVENT_READ, ip, offset,
					       evsize, 0, &iolock);
	}
out_unlock:
	xfs_iunlock(ip, iolock);
	return -error;
}


STATIC int
xfs_dm_send_destroy_event(
	xfs_inode_t	*ip,
	dm_right_t	vp_right)	/* always DM_RIGHT_NULL */
{
	/* Returns positive errors to XFS */
	return -dm_send_destroy_event(&ip->i_vnode, vp_right);
}


STATIC int
xfs_dm_send_namesp_event(
	dm_eventtype_t	event,
	struct xfs_mount *mp,
	xfs_inode_t	*ip1,
	dm_right_t	vp1_right,
	xfs_inode_t	*ip2,
	dm_right_t	vp2_right,
	const char	*name1,
	const char	*name2,
	mode_t		mode,
	int		retcode,
	int		flags)
{
	/* Returns positive errors to XFS */
	return -dm_send_namesp_event(event, mp ? mp->m_super : NULL,
				    &ip1->i_vnode, vp1_right,
				    ip2 ? &ip2->i_vnode : NULL, vp2_right,
				    name1, name2,
				    mode, retcode, flags);
}

STATIC int
xfs_dm_send_mount_event(
	struct xfs_mount	*mp,
	dm_right_t		root_right,
	char			*mtpt,
	char			*fsname)
{
	return dm_send_mount_event(mp->m_super, root_right,
			NULL, DM_RIGHT_NULL,
			mp->m_rootip ? VFS_I(mp->m_rootip) : NULL,
			DM_RIGHT_NULL, mtpt, fsname);
}

STATIC void
xfs_dm_send_unmount_event(
	struct xfs_mount *mp,
	xfs_inode_t	*ip,		/* NULL if unmount successful */
	dm_right_t	vfsp_right,
	mode_t		mode,
	int		retcode,	/* errno, if unmount failed */
	int		flags)
{
	dm_send_unmount_event(mp->m_super, ip ? &ip->i_vnode : NULL,
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
	.xfs_send_mount		= xfs_dm_send_mount_event,
	.xfs_send_unmount	= xfs_dm_send_unmount_event,
};
EXPORT_SYMBOL(xfs_dmcore_xfs);

STATIC int
xfs_dm_fh_to_inode(
	struct super_block	*sb,
	struct inode		**inode,
	dm_fid_t		*dmfid)
{
	xfs_mount_t		*mp = XFS_M(sb);
	xfs_inode_t		*ip;
	xfs_ino_t		ino;
	unsigned int		igen;
	int			error;

	*inode = NULL;

	if (!dmfid->dm_fid_len) {
		/* filesystem handle */
		*inode = igrab(&mp->m_rootip->i_vnode);
		if (!*inode)
			return -ENOENT;
		return 0;
	}

	if (dmfid->dm_fid_len != sizeof(*dmfid) - sizeof(dmfid->dm_fid_len))
		return -EINVAL;

	ino  = dmfid->dm_fid_ino;
	igen = dmfid->dm_fid_gen;

	/* fail requests for ino 0 gracefully. */
	if (ino == 0)
		return -ESTALE;

	error = xfs_iget(mp, NULL, ino, 0, XFS_ILOCK_SHARED, &ip, 0);
	if (error)
		return -error;
	if (!ip)
		return -EIO;

	if (!ip->i_d.di_mode || ip->i_d.di_gen != igen) {
		xfs_iput_new(ip, XFS_ILOCK_SHARED);
		return -ENOENT;
	}

	*inode = &ip->i_vnode;
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	return 0;
}

STATIC int
xfs_dm_inode_to_fh(
	struct inode		*inode,
	dm_fid_t		*dmfid,
	dm_fsid_t		*dmfsid)
{
	xfs_inode_t		*ip = XFS_I(inode);

	/* Returns negative errors to DMAPI */

	if (ip->i_mount->m_fixedfsid == NULL)
		return -EINVAL;

	dmfid->dm_fid_len = sizeof(dm_fid_t) - sizeof(dmfid->dm_fid_len);
	dmfid->dm_fid_pad = 0;
	/*
	 * use memcpy because the inode is a long long and there's no
	 * assurance that dmfid->dm_fid_ino is properly aligned.
	 */
	memcpy(&dmfid->dm_fid_ino, &ip->i_ino, sizeof(dmfid->dm_fid_ino));
	dmfid->dm_fid_gen = ip->i_d.di_gen;

	memcpy(dmfsid, ip->i_mount->m_fixedfsid, sizeof(*dmfsid));
	return 0;
}

STATIC void
xfs_dm_get_fsid(
	struct super_block	*sb,
	dm_fsid_t		*fsid)
{
	memcpy(fsid, XFS_M(sb)->m_fixedfsid, sizeof(*fsid));
}

/*
 * Filesystem operations accessed by the DMAPI core.
 */
static struct filesystem_dmapi_operations xfs_dmapiops = {
	.get_fsys_vector	= xfs_dm_get_dmapiops,
	.fh_to_inode		= xfs_dm_fh_to_inode,
	.inode_to_fh		= xfs_dm_inode_to_fh,
	.get_fsid		= xfs_dm_get_fsid,
};

static int __init
xfs_dm_init(void)
{
	printk(KERN_INFO "SGI XFS Data Management API subsystem\n");

	dmapi_register(&xfs_fs_type, &xfs_dmapiops);
	return 0;
}

static void __exit
xfs_dm_exit(void)
{
	dmapi_unregister(&xfs_fs_type);
}

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION("SGI XFS dmapi subsystem");
MODULE_LICENSE("GPL");

module_init(xfs_dm_init);
module_exit(xfs_dm_exit);
