/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (C) International Business Machines Corp., 2000-2004
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

#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>
#include <linux/xattr.h>
#include <linux/slab.h>
#include <linux/namespace.h>
#include <linux/buffer_head.h>
#include <linux/mm.h>
#include <linux/security.h>
#include "dmapi_private.h"
#include "jfs_debug.h"
#include "jfs_incore.h"
#include "jfs_filsys.h"
#include "jfs_xattr.h"
#include "jfs_txnmgr.h"
#include "jfs_dmap.h"
#include "jfs_dmapi.h"

/* Here's what's left to be done:
 * Figure out locktype in jfs_dm_send_data_event()
 * Figure out how to get name of mounted dir for mount event without stupid 
 *   mount option
 * Add jfs_dm_get_bulkattr (although unused by TSM)
 * Add DM_EVENT_NOSPACE (VERY intrusive to JFS code)
 * Finish up dt_change (may not cover all yet)
 * ? Whazzup with the dump under debug spinlock?
 * ? Whazzup with unmount hang under debug spinlock?
 */

/* XFS bugs fixed from original port
 * #1 - dm_create_session truncated sessinfo bigger than DM_SESSION_INFO_LEN, should return E2BIG
 * #2 - dm_create_session returned 0 with invalid sessinfo, should return EFAULT
 * #3 - dm_path_to_handle returned ENOENT with invalid path, should return EFAULT
 * #4 - dm_path_to_handle returned EINVAL with non-DMAPI path, should return ENXIO
 * #5 - dm_path_to_fshandle returned ENOENT with invalid path, should return EFAULT
 * #6 - dm_path_to_fshandle returned EINVAL with non-DMAPI path, should return ENXIO
 * #7 - dm_get_allocinfo returned EOPNOTSUPP with non-regular file, should return EINVAL
 * #8 - dm_probe_hole returned 0 with off+len past EOF, should return E2BIG
 * #9 - dm_read_invis returned 0 with off>EOF, should return EINVAL
 * #10 - dm_set_dmattr returned EINVAL with invalid path, should return EFAULT
 * #11 - dm_create_userevent returned EINVAL with invalid tokenp but left tevp on queue
 * #12 - dm_handle_to_path only worked if target object in current directory
 * #13 - jfs_copyin_attrname did not null-terminate attrname
 * #14 - dm_set_return_on_destroy returned non-zero copy_from_user return code, should return EFAULT
 * #15 - dm_set_eventlist returned EBADF for global handle, should return EINVAL
 * #16 - dm_get_eventlist returned 0 for dir handle, should return EINVAL
 * #17 - dm_get_eventlist did not handle number of elements in/out properly
 * #18 - dm_get_config_events did not handle number of elements in/out properly
 * #19 - dm_get_config_events did not return DM_EVENT_USER even though it is supported
 * #20 - dm_set_fileattr returned 0 for invalid mask, should return EINVAL
 * #21 - dm_set_fileattr changed ctime when DM_AT_DTIME set and no DM attributes
 * #22 - dm_get_fileattr returned 0 for invalid mask, should return EINVAL
 * #23 - dm_get_fileattr returned ctime when DM_AT_DTIME set and no DM attributes
 * #24 - dm_get_dirattrs returned 0 for invalid mask, should return EINVAL
 * #25 - dm_get_dirattrs returned ctime when DM_AT_DTIME set and no DM attributes
 * #26 - dm_get_dirattrs returned E2BIG for zero buflen, should return 1
 * #27 - dm_fd_to_handle returned EBADF with non-DMAPI path, should return ENXIO
 * #28 - dm_get_dirattrs returned handle with DM_AT_HANDLE clear in mask
 * #29 - dm_request_right returned 0 for invalid right, should return EINVAL
 * #30 - dm_request_right returned 0 for DM_RIGHT_EXCL and DM_RR_WAIT set, should return EACCES
 * #31 - dm_upgrade_right returned EACCES for DM_RIGHT_NULL, should return EPERM
 * #32 - dm_downgrade_right returned EACCES for DM_RIGHT_NULL, should return EPERM
 * #33 - dm_send_mmap_event sent offset 0, length EOF instead of actual page-aligned region
 * #34 - dm_move_event returned ESRCH for dm_find_msg failure, should return ENOENT
 * #35 - dm_find_eventmsg returned ESRCH for dm_find_msg failure, should return EINVAL
 * #36 - dm_move_event moved token to targetsid despite returning EFAULT for invalid rlenp
 * #37 - dm_respond_event returned 0 with invalid buflen, should return E2BIG
 * #38 - dm_pending returned 0 with invalid delay, should return EFAULT
 * #39 - dm_send_msg returned dm_respond_event's reterror instead of -1 and errno = reterror
 * #40 - dm_send_xxx_event returned |errno| instead of -1 and errno = reterror if error occurred
 * #41 - dm_handle_cmp faulted instead of returning 0 for global handle
 */
  
#define MAXNAMLEN MAXNAMELEN

#define NBBY 8 			/* Number bits per byte */
#define MODEMASK 07777		/* mode bits plus permission bits */

#ifdef DEBUG
#define STATIC static
#else
#define STATIC 
#endif

#define BULKSTAT_RV_NOTHING	0
#define BULKSTAT_RV_DIDONE	1
#define BULKSTAT_RV_GIVEUP	2

extern void jfs_truncate_nolock(struct inode *, loff_t);
extern int xtPunchHole(tid_t tid, struct inode *ip, s64 xoff, s32 xlen, int flag);

/* This structure must match the one described in ../xattr.c */
struct ea_buffer {
	int flag;		/* Indicates what storage xattr points to */
	int max_size;		/* largest xattr that fits in current buffer */
	dxd_t new_ea;		/* dxd to replace ea when modifying xattr */
	struct metapage *mp;	/* metapage containing ea list */
	struct jfs_ea_list *xattr;	/* buffer containing ea list */
};

extern int jfs_ea_get(struct inode *, struct ea_buffer *, int);
extern void jfs_ea_release(struct inode *, struct ea_buffer *);
int jfs_dm_write_pers_data(struct jfs_inode_info *jfs_ip);

/* Structure used to hold the on-disk version of a dm_attrname_t.  All
   on-disk attribute names start with the 9-byte string "user.dmi.".
*/

typedef struct	{
	char	dan_chars[DMATTR_PREFIXLEN + DM_ATTR_NAME_SIZE + 1];
} dm_dkattrname_t;

/* In the on-disk inode, DMAPI attribute names consist of the user-provided
   name with the DMATTR_PREFIXSTRING pre-pended.  This string must NEVER be
   changed!
*/

STATIC	const	char	dmattr_prefix[DMATTR_PREFIXLEN + 1] = DMATTR_PREFIXSTRING;

STATIC	dm_size_t  dm_min_dio_xfer = 0; /* direct I/O disabled for now */


/* See jfs_dm_get_dmattr() for a description of why this is needed. */

#define DM_MAX_ATTR_BYTES_ON_DESTROY	256

#define DM_STAT_SIZE(mask, namelen)	\
	(sizeof(dm_stat_t) + \
	 (((mask) & DM_AT_HANDLE) ? sizeof(jfs_handle_t) : 0) + (namelen))
#define MAX_DIRENT_SIZE		(sizeof(dirent_t) + JFS_NAME_MAX + 1)

#define DM_STAT_ALIGN		(sizeof(u64))

/* DMAPI's E2BIG == EA's ERANGE */
/* DMAPI's ENOENT == EA's ENODATA */
#define DM_EA_XLATE_ERR(err) { if (err == -ERANGE) err = -E2BIG; else if (err == -ENODATA) err = -ENOENT; }

#define REGION_MASK_TO_EVENT_MASK (DM_EVENT_READ - DM_REGION_READ + 1)	

#define MAX_MANAGED_REGIONS 0x7fffffff	

/*
 *	jfs_dm_send_data_event()
 *
 *	Send data event to DMAPI.  Drop IO lock (if specified) before
 *	the dm_send_data_event() call and reacquire it afterwards.
 */
int
jfs_dm_send_data_event(
	dm_eventtype_t	event,
	struct inode 	*ip,
	dm_off_t	offset,
	size_t		length,
	int		flags,
	int/*vrwlock_t*/	*locktype)
{
	int		error = 0;
	uint16_t	dmstate;
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);

	ASSERT(IP_IS_JFS(ip));
	
#ifndef DM_SUPPORT_ONE_MANAGED_REGION
	if (jfs_ip->dmnumrgns > 0) {
		/* There are managed regions, check each one to see if event
		 * matches, abort if not.
		*/
		int i;
		dm_off_t	rgnbeg;
		dm_off_t	rgnend;
		dm_off_t	filbeg = offset;
		dm_off_t	filend = filbeg + length - 1;
		unsigned int	rgnflags;
		
		/* Quick check to make sure at least one region wants event */
		if (!DMEV_ISSET(event, jfs_ip->dmattrs.da_dmevmask))
			return 0;

		for (i = 0; i < jfs_ip->dmnumrgns; i++) {
			rgnbeg = jfs_ip->dmrgns[i].rg_offset;
			rgnflags = jfs_ip->dmrgns[i].rg_flags;

			/* Region goes to end of file */
			if (jfs_ip->dmrgns[i].rg_size == 0) {
				if (rgnbeg < filend) {
					if ((1 << event) & (rgnflags << REGION_MASK_TO_EVENT_MASK)) {
						break;
					}
				}
			} else {
				rgnend = rgnbeg + jfs_ip->dmrgns[i].rg_size - 1;
				
				/* Region intersects memory-mapped area */
				if (((rgnbeg >= filbeg) && (rgnbeg <= filend)) || 
				    ((rgnend >= filbeg) && (rgnend <= filend)) || 
				    ((rgnbeg < filbeg) && (rgnend > filend))) {
					if ((1 << event) & (rgnflags << REGION_MASK_TO_EVENT_MASK)) {
						break;
					}
				}
			}

			/* SPECIAL CASE: truncation prior to DM_REGION_TRUNCATE
			 * region still generates event!
			 */
			if ((rgnflags & DM_REGION_TRUNCATE) &&
			    (event == DM_EVENT_TRUNCATE) &&
			    (offset <= rgnbeg)) {
			       break;
			}
		}

		if (i >= jfs_ip->dmnumrgns) {
			return 0;
		}
	}	
#endif	
	
	do {
		dmstate = jfs_ip->dmattrs.da_dmstate;
//		if (locktype)
//			xfs_rwunlock(bdp, *locktype);
		error = dm_send_data_event(event, ip, DM_RIGHT_NULL,
				offset, length, flags);
//		if (locktype)
//			xfs_rwlock(bdp, *locktype);
	} while (!error && (jfs_ip->dmattrs.da_dmstate != dmstate));
	
	return error;
}


#ifdef DM_SUPPORT_ONE_MANAGED_REGION

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
	struct inode *ip)
{
	struct address_space *mapping;
	struct vm_area_struct *vma;
	struct list_head *l;
	int		prohibited;

	if (prio_tree_empty(&ip->i_mapping->i_mmap))
		return 0;

	prohibited = 1 << DM_EVENT_READ;
	mapping = ip->i_mapping;

	down(&mapping->i_shared_sem);
	list_for_each(l, &mapping->i_mmap_shared) {
		vma = list_entry(l, struct vm_area_struct, shared);
		if (!(vma->vm_flags & VM_DENYWRITE)) {
			prohibited |= 1 << DM_EVENT_WRITE;
			break;
		}
	}
	up(&mapping->i_shared_sem);
	return prohibited;
}

#else

STATIC int
prohibited_mr_events(
	struct inode	*ip,
	dm_region_t	*rgn)
{
	struct address_space *mapping;
	struct vm_area_struct *vma;
	struct prio_tree_iter iter;
	int		prohibited = 0;
	dm_off_t	rgnbeg = rgn->rg_offset;
	dm_off_t	rgnend = rgnbeg + rgn->rg_size;

	if (prio_tree_empty(&ip->i_mapping->i_mmap))
		return 0;

	mapping = ip->i_mapping;

	down(&mapping->i_shared_sem);
	vma = __vma_prio_tree_first(&mapping->i_mmap_shared, &iter,
				    rgnbeg, rgnend);
	if (vma) {
		prohibited = 1 << DM_EVENT_READ;
		
		if (!(vma->vm_flags & VM_DENYWRITE)) {
			prohibited |= 1 << DM_EVENT_WRITE;
		}
	}
	up(&mapping->i_shared_sem);
	return prohibited;
}
#endif


#ifdef	DEBUG_RIGHTS
STATIC int
jfs_ip_to_hexhandle(
	struct inode 	*ip,
	u_int		type,
	char		*buffer)
{
	jfs_handle_t	handle;
	u_char		*chp;
	int		length;
	int		error;
	int		i;

	if ((error = dm_ip_to_handle(ip, &handle)))
		return(error);

	if (type == DM_FSYS_OBJ) {	/* a filesystem handle */
		length = FSHSIZE;
	} else {
		length = JFS_HSIZE(handle);
	}
	for (chp = (u_char *)&handle, i = 0; i < length; i++) {
		*buffer++ = "0123456789abcdef"[chp[i] >> 4];
		*buffer++ = "0123456789abcdef"[chp[i] & 0xf];
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
jfs_copyin_attrname(
	dm_attrname_t	*from,		/* dm_attrname_t in user space */
	dm_dkattrname_t *to)		/* name buffer in kernel space */
{
	int		error;
	size_t len;

	strcpy(to->dan_chars, dmattr_prefix);

	len = strnlen_user((char*)from, DM_ATTR_NAME_SIZE);
	if (len == 0)						// XFS BUG #10
		return(-EFAULT);				// XFS BUG #10
	error = copy_from_user(&to->dan_chars[DMATTR_PREFIXLEN], from, len);

	if (!error && (to->dan_chars[DMATTR_PREFIXLEN] == '\0'))
		error = -EINVAL;
	if (error == -ENAMETOOLONG) {
		to->dan_chars[sizeof(to->dan_chars) - 1] = '\0';
		error = 0;
	} else if (!error) {					// XFS BUG #13
		to->dan_chars[DMATTR_PREFIXLEN + len - 1] = '\0';// XFS_BUG #13
	}
	return(error);
}

STATIC int
jfs_dmattr_exist(struct inode *ip)
{
	int exist = 0;
	struct ea_buffer eabuf;

	/* This will block all get/set EAs */
	down_read(&JFS_IP(ip)->xattr_sem);

	/* Check if EAs exist */
	if (jfs_ea_get(ip, &eabuf, 0) > 0) {
		struct jfs_ea_list *ealist = (struct jfs_ea_list *)eabuf.xattr;
		struct jfs_ea *ea;

		for (ea = FIRST_EA(ealist); ea < END_EALIST(ealist); ea = NEXT_EA(ea)) {
			char	*user_name;
	
			/* Skip over all non-DMAPI attributes.	If the
			   attribute name is too long, we assume it is
			   non-DMAPI even if it starts with the correct
			   prefix.
			*/
			if (strncmp(ea->name, dmattr_prefix, DMATTR_PREFIXLEN))
				continue;
			user_name = &ea->name[DMATTR_PREFIXLEN];
			if (strlen(user_name) > DM_ATTR_NAME_SIZE)
				continue;
		
			/* We have a valid DMAPI attribute. */
			exist = 1;
			break;
		}

		jfs_ea_release(ip, &eabuf);
	}

	/* This will unblock all get/set EAs */
	up_read(&JFS_IP(ip)->xattr_sem);

	return(exist);
}

/* This copies selected fields in an inode into a dm_stat structure.  

   The inode must be kept locked SHARED by the caller.
*/

STATIC void
jfs_ip_to_stat(
	struct inode	*ip,
	u_int		mask,
	dm_stat_t	*buf)
{
	int		filetype;
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);

	if (mask & DM_AT_STAT) {
		buf->dt_size = ip->i_size;
		buf->dt_dev = old_encode_dev(ip->i_sb->s_dev);

		buf->dt_ino = ip->i_ino;

		/*
		 * Copy from in-core inode.
		 */
		buf->dt_mode = (ip->i_mode & S_IFMT) | (ip->i_mode & MODEMASK);
		buf->dt_uid = ip->i_uid;
		buf->dt_gid = ip->i_gid;
		buf->dt_nlink = ip->i_nlink;
		/*
		 * Minor optimization, check the common cases first.
		 */
		filetype = ip->i_mode & S_IFMT;
		if ((filetype == S_IFREG) || (filetype == S_IFDIR)) {
			buf->dt_rdev = 0;
		} else if ((filetype == S_IFCHR) || (filetype == S_IFBLK) ) {
			buf->dt_rdev = old_encode_dev(ip->i_rdev);
		} else {
			buf->dt_rdev = 0;	/* not a b/c spec. */
		}

		buf->dt_atime = ip->i_atime.tv_sec;
		buf->dt_mtime = ip->i_mtime.tv_sec;
		buf->dt_ctime = ip->i_ctime.tv_sec;

		/*
		 * We use the read buffer size as a recommended I/O
		 * size.  This should always be larger than the
		 * write buffer size, so it should be OK.
		 * The value returned is in bytes.
		 */
		buf->dt_blksize = ip->i_blksize;
		buf->dt_blocks = ip->i_blocks;
	}

	if (mask & DM_AT_EMASK) {
		buf->dt_emask = (dm_eventset_t)jfs_ip->dmattrs.da_dmevmask;
		buf->dt_nevents = DM_EVENT_MAX;
	}

	if (mask & DM_AT_PATTR) {
		buf->dt_pers = jfs_dmattr_exist(ip);
	}
	
	if (mask & DM_AT_CFLAG) {
		buf->dt_change = ip->i_version;
	}
	if ((mask & DM_AT_DTIME) && jfs_dmattr_exist(ip))	// XFS BUG #25
		buf->dt_dtime = ip->i_ctime.tv_sec;

	if (mask & DM_AT_PMANR) {
#ifdef DM_SUPPORT_ONE_MANAGED_REGION	
		/* Set if one of READ, WRITE or TRUNCATE bits is set in emask */
		buf->dt_pmanreg = ( DMEV_ISSET(DM_EVENT_READ, buf->dt_emask) ||
				DMEV_ISSET(DM_EVENT_WRITE, buf->dt_emask) ||
				DMEV_ISSET(DM_EVENT_TRUNCATE, buf->dt_emask) ) ? 1 : 0;
#else
		buf->dt_pmanreg = (jfs_ip->dmnumrgns ? 1 : 0);
#endif	
	}		
}


/*
 * This is used by dm_get_bulkattr() as well as dm_get_dirattrs().
 * Given a inumber, it igets the inode and fills the given buffer
 * with the dm_stat structure for the file.
 */
/* ARGSUSED */
STATIC int
jfs_dm_bulkstat_one(
	struct inode	*mp,		/* mount point for filesystem */
	u_int		mask,		/* fields mask */
	tid_t		tid,		/* transaction pointer */
	dm_ino_t	ino,		/* inode number to get data for */
	void		*buffer,	/* buffer to place output in */
	int		*res)		/* bulkstat result code */
{
	struct inode	*ip;
	dm_stat_t	*buf;
	jfs_handle_t	handle;
	u_int		statstruct_sz;

	buf = (dm_stat_t *)buffer;

	ip = iget(mp->i_sb, ino);
	if (!ip) {
		*res = BULKSTAT_RV_NOTHING;
		return(-EIO);
	}
	
	if (is_bad_inode(ip)) {
		iput(ip);
		*res = BULKSTAT_RV_NOTHING;
		return(-EIO);
	}

	if (ip->i_mode == 0) {
		iput(ip);
		*res = BULKSTAT_RV_NOTHING;
		return(-ENOENT);
	}

	/*
	 * copy everything to the dm_stat buffer
	 */
	jfs_ip_to_stat(ip, mask, buf);

	/*
	 * Make the handle and the link to the next dm_stat buffer
	 */
	// XFS BUG #28 BEGIN
	statstruct_sz = DM_STAT_SIZE(mask, 0);
	if (mask & DM_AT_HANDLE) {
		dm_ip_to_handle(ip, &handle);
		memcpy(buf+1, &handle, sizeof(handle));	/* handle follows stat struct */

		buf->dt_handle.vd_offset = (ssize_t) sizeof(dm_stat_t);
		buf->dt_handle.vd_length = (size_t) JFS_HSIZE(handle);

		statstruct_sz = (statstruct_sz+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);
	} else {
		memset((void *)&buf->dt_handle, 0, sizeof(dm_vardata_t));
	}
	// XFS BUG #28 END
	buf->_link = statstruct_sz;

	/*
	 * This is unused in bulkstat - so we zero it out.
	 */
	memset((void *) &buf->dt_compname, 0, sizeof(dm_vardata_t));

	iput(ip);

	*res = BULKSTAT_RV_DIDONE;
	return(0);
}


struct filldir_pos {
	void	*addr;
	size_t	len;
};

/* This structure must match the one described in ../xtree.c */
struct jfs_dirent {
	loff_t	position;
	int	ino;
	u16	name_len;
	char	name[0];
};


STATIC int
jfs_dm_filldir(
	void		*bufp,
	const char	*name,
	int		namelen,
	loff_t		off,
	ino_t		ino,
	unsigned	type)
{
	struct filldir_pos *fdposp = bufp;
	struct jfs_dirent *jdp;
	int entlen = sizeof(struct jfs_dirent) + namelen + 1;
	
	/* Not enough space left for this entry */
	if (fdposp->len < entlen)
		return 1;

	jdp = (struct jfs_dirent *)fdposp->addr;
	jdp->position = off;
	jdp->ino = ino;
	jdp->name_len = namelen;
	strcpy(jdp->name, name);
	
	fdposp->len -= entlen;
	fdposp->addr = (char *)fdposp->addr + entlen;

	return 0;
}	


STATIC int
jfs_get_dirents(
	struct inode	*dirp,
	void		*bufp,
	size_t		bufsz,
	dm_off_t	*locp,
	size_t		*nreadp)
{
	int		rval;
	struct file	tempfile;
	struct dentry	tempdentry;
	struct filldir_pos fdpos;

	/* Simulate the necessary info for jfs_readdir */
	tempdentry.d_inode = dirp;
	tempfile.f_dentry = &tempdentry;
	tempfile.f_pos = *locp;

	fdpos.addr = bufp;
	fdpos.len = bufsz;

	*nreadp = 0;

	rval = jfs_readdir(&tempfile, &fdpos, jfs_dm_filldir);
	if (! rval) {
		/*
		 * number of bytes read into the dirent buffer
		 */
		*nreadp = bufsz - fdpos.len;
	
		/* 
		 * Nothing read and loc passed in not end of dir, must be 
		 * invalid loc; otherwise save new loc
		 */
		if ((*nreadp == 0) && (*locp != DIREND))
			rval = -EINVAL;
		else
			*locp = tempfile.f_pos;
	}
	return(rval);
}


STATIC int
jfs_dirents_to_stats(
	struct inode	*ip,
	u_int		mask,		/* fields mask */
	struct jfs_dirent *direntp,	/* array of dirent structs */
	void		*bufp,		/* buffer to fill */
	size_t		*direntbufsz,	/* sz of filled part of dirent buf */
	size_t		*spaceleftp,	/* IO - space left in user buffer */
	size_t		*nwrittenp,	/* number of bytes written to 'bufp' */
	dm_off_t	*locp,
	size_t		*offlastlinkp)  /* offset of last stat's _link */
{
	struct jfs_dirent *p;
	dm_stat_t	*statp;
	size_t		reclen;
	size_t		namelen;
	size_t		spaceleft;
	dm_off_t	prevoff, offlastlink;
	int		res;

	spaceleft = *spaceleftp;
	*nwrittenp = 0;

	/*
	 * Make sure the first entry will fit in buffer before doing any 
	 * processing
	 */
	if (spaceleft <= DM_STAT_SIZE(mask, direntp->name_len + 1)) {
		return 0;
	}

	*spaceleftp = 0;
	prevoff = 0;
	offlastlink = 0;

	/*
	 * Go thru all the dirent records, making dm_stat structures from
	 * them, one by one, until dirent buffer is empty or stat buffer
	 * is full.
	 */
	p = direntp;
	statp = (dm_stat_t *) bufp;
	for (reclen = (size_t) sizeof(struct jfs_dirent) + p->name_len + 1; 
					*direntbufsz > 0;
					*direntbufsz -= reclen,
					p = (struct jfs_dirent *) ((char *) p + reclen),
					reclen = (size_t) sizeof(struct jfs_dirent) + p->name_len + 1) {

		namelen = p->name_len + 1;

		/*
		 * Make sure we have enough space.
		 */
		if (spaceleft <= DM_STAT_SIZE(mask, namelen)) {
			/*
			 * d_off field in dirent_t points at the next entry.
			 */
			*locp = p->position;
			*spaceleftp = 0;

			/*
			 * The last link is NULL.
			 */
			statp->_link = 0;
			return(0);
		}

		statp = (dm_stat_t *) bufp;

		(void)jfs_dm_bulkstat_one(ip, mask, 0, p->ino, statp, &res);
		if (res != BULKSTAT_RV_DIDONE)
			continue;

		/*
		 * On return from jfs_dm_bulkstat_one(), stap->_link points
		 * at the end of the handle in the stat structure.
		 */
		statp->dt_compname.vd_offset = statp->_link;
		statp->dt_compname.vd_length = namelen;
		/*
		 * Directory entry name is guaranteed to be
		 * null terminated; the copy gets the '\0' too.
		 */
		memcpy((char *) statp + statp->_link, p->name, namelen);

		/* Word-align the record */
		statp->_link = (statp->_link + namelen + (DM_STAT_ALIGN - 1))
			& ~(DM_STAT_ALIGN - 1);

		spaceleft -= statp->_link;
		*nwrittenp += statp->_link;
		
		offlastlink += prevoff;
		prevoff = statp->_link;

		bufp = (char *)statp + statp->_link;
		*locp = p->position;

		/*
		 * If we just happen to use the very last byte, bump by one
		 * to let logic at beginning of loop above handle it
		 */
		if (!spaceleft) {
			spaceleft++;
		}
	}
	/* statp->_link = 0; this is handled in get_dirattrs if no more left */
	*offlastlinkp = (size_t)offlastlink;

	/*
	 * If there's space left to put in more, caller should know that..
	 */
	*spaceleftp = spaceleft;

	return(0);
}


// XFS BUG #17 START
STATIC int
jfs_dm_get_high_set_event(dm_eventset_t *eventsetp)
{
	int i;

	for (i = DM_EVENT_MAX-1; i >= 0 && !DMEV_ISSET(i, *eventsetp); i--);
	return i + 1;
}	
// XFS BUG #17 END
	

/* jfs_dm_f_get_eventlist - return the dm_eventset_t mask for inode ip. */

STATIC int
jfs_dm_f_get_eventlist(
	struct inode	*ip,
	dm_right_t	right,
	u_int		nelem,
	dm_eventset_t	*eventsetp,		/* in kernel space! */
	u_int		*nelemp)		/* in kernel space! */
{
	int		highSetEvent;				// XFS BUG #17
	dm_eventset_t	eventset;
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	/* Note that we MUST return a regular file's managed region bits as
	   part of the mask because dm_get_eventlist is supposed to return the
	   union of all managed region flags in those bits.  Since we only
	   support one region, we can just return the bits as they are.	 For
	   all other object types, the bits will already be zero.  Handy, huh?
	*/

	eventset = jfs_ip->dmattrs.da_dmevmask;

	// XFS BUG #17 START
	highSetEvent = jfs_dm_get_high_set_event(&eventset);
	if (highSetEvent > nelem) {
		*nelemp = highSetEvent;
		return(-E2BIG);
	}
	// XFS BUG #17 END

	/* Now copy the event mask and event count back to the caller.	We
	   return the lesser of nelem and DM_EVENT_MAX.
	*/

	if (nelem > DM_EVENT_MAX)
		nelem = DM_EVENT_MAX;
	eventset &= (1 << nelem) - 1;

	*eventsetp = eventset;
	*nelemp = highSetEvent;					// XFS BUG #17
	return(0);
}


/* jfs_dm_f_set_eventlist - update the dm_eventset_t mask in the inode ip.  Only the
   bits from zero to maxevent-1 are being replaced; higher bits are preserved.
*/

STATIC int
jfs_dm_f_set_eventlist(
	struct inode	*ip,
	dm_right_t	right,
	dm_eventset_t	*eventsetp,	/* in kernel space! */
	u_int		maxevent)
{
	dm_eventset_t	eventset;
	dm_eventset_t	max_mask;
	dm_eventset_t	valid_events;
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	eventset = *eventsetp;
	if (maxevent >= sizeof(jfs_ip->dmattrs.da_dmevmask) * NBBY)
		return(-EINVAL);
	max_mask = (1 << maxevent) - 1;

	if (S_ISDIR(ip->i_mode)) {
		valid_events = DM_JFS_VALID_DIRECTORY_EVENTS;
	} else {	/* file or symlink */
		valid_events = DM_JFS_VALID_FILE_EVENTS;
	}
	if ((eventset & max_mask) & ~valid_events)
		return(-EINVAL);

	/* Adjust the event mask so that the managed region bits will not
	   be altered.
	*/

	max_mask &= ~(1 <<DM_EVENT_READ);	/* preserve current MR bits */
	max_mask &= ~(1 <<DM_EVENT_WRITE);
	max_mask &= ~(1 <<DM_EVENT_TRUNCATE);

	jfs_ip->dmattrs.da_dmevmask = (eventset & max_mask) | 
			(jfs_ip->dmattrs.da_dmevmask & ~max_mask);

	mark_inode_dirty(ip);

	return(0);
}


/* jfs_dm_fs_get_eventlist - return the dm_eventset_t mask for filesystem ip. */

STATIC int
jfs_dm_fs_get_eventlist(
	struct inode	*ip,
	dm_right_t	right,
	u_int		nelem,
	dm_eventset_t	*eventsetp,		/* in kernel space! */
	u_int		*nelemp)		/* in kernel space! */
{
	int		highSetEvent;				// XFS BUG #17
	dm_eventset_t	eventset;
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	eventset = sbi->dm_evmask;

	// XFS BUG #17 START
	highSetEvent = jfs_dm_get_high_set_event(&eventset);
	if (highSetEvent > nelem) {
		*nelemp = highSetEvent;
		return(-E2BIG);
	}
	// XFS BUG #17 END

	/* Now copy the event mask and event count back to the caller.	We
	   return the lesser of nelem and DM_EVENT_MAX.
	*/

	if (nelem > DM_EVENT_MAX)
		nelem = DM_EVENT_MAX;
	eventset &= (1 << nelem) - 1;

	*eventsetp = eventset;
	*nelemp = highSetEvent;					// XFS BUG #17
	return(0);
}


/* jfs_dm_fs_set_eventlist - update the dm_eventset_t mask in the mount structure for
   filesystem ip.  Only the bits from zero to maxevent-1 are being replaced;
   higher bits are preserved.
*/

STATIC int
jfs_dm_fs_set_eventlist(
	struct inode	*ip,
	dm_right_t	right,
	dm_eventset_t	*eventsetp,	/* in kernel space! */
	u_int		maxevent)
{
	dm_eventset_t	eventset;
	dm_eventset_t	max_mask;
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	eventset = *eventsetp;

	if (maxevent >= sizeof(sbi->dm_evmask) * NBBY)
		return(-EINVAL);
	max_mask = (1 << maxevent) - 1;

	if ((eventset & max_mask) & ~DM_JFS_VALID_FS_EVENTS)
		return(-EINVAL);

	sbi->dm_evmask = (eventset & max_mask) | (sbi->dm_evmask & ~max_mask);
	return(0);
}


STATIC int
jfs_dm_direct_ok(
	struct inode	*ip,
	dm_off_t	off,
	dm_size_t	len,
	void		*bufp)
{
	/* Realtime files can ONLY do direct I/O. */

	/* If direct I/O is disabled, or if the request is too small, use
	   buffered I/O.
	*/

	if (!dm_min_dio_xfer || len < dm_min_dio_xfer)
		return(0);

#if 0 /* SGI's exclusion, not IBM's */
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
jfs_dm_rdwr(
	struct inode	*ip,
	uint		fflag,
	mode_t		fmode,
	dm_off_t	off,
	dm_size_t	len,
	void		*bufp,
	int		*rvp)
{
	int		error;
	int		oflags;
	ssize_t		xfer;
	struct file	file;
	struct dentry	*dentry;
	boolean_t	bCloseFile = FALSE;
	struct		jfs_inode_info *ji = JFS_IP(ip);

	if (off < 0 || !S_ISREG(ip->i_mode))
		return(-EINVAL);

	if (fmode & FMODE_READ) {
		oflags = O_RDONLY;
	} else {
		oflags = O_WRONLY;
	}

	/* Build file descriptor flags and I/O flags.  O_NONBLOCK is needed so
	   that we don't block on mandatory file locks.	 FINVIS is needed so
	   that we don't change any file timestamps.
	*/

	fmode |= FINVIS;
	oflags |= O_NONBLOCK;
	if (jfs_dm_direct_ok(ip, off, len, bufp))
		oflags |= O_DIRECT;

	if (fflag & O_SYNC)
		oflags |= O_SYNC;

	if( ip->i_fop == NULL ){
		/* no iput; caller did get, and will do put */
		return(-EINVAL);
	}
	igrab(ip);

	/* Find a dentry.  Get a well-connected one, if possible. */
	dentry = d_alloc_root(ip);
	if (dentry == NULL) {
		iput(ip);
		return -ENOMEM;
	}

	if( ip->i_ino != dentry->d_inode->i_ino ){
		dput(dentry);
		return -EINVAL;
	}

	if (fmode & FMODE_WRITE) {
		error = get_write_access(ip);
		if (error) {
			dput(dentry);
			return(error);
		}
	}

	error = open_private_file( &file, dentry, oflags );
	if(error){
		if (error == -EFBIG) {
			/* try again */
			oflags |= O_LARGEFILE;
			file.f_flags = oflags;
			error = file.f_op->open( dentry->d_inode, &file );
		}
		if (error) {
			if (fmode & FMODE_WRITE)
				put_write_access(ip);
			dput(dentry);
			return(-EINVAL);
		}
	} else {
		bCloseFile = TRUE;
	}

	/* file.f_flags = oflags; handled by open_private_file now */

	if (fmode & FMODE_READ) {
			/* generic_file_read updates the atime but we need to
			 * undo that because this I/O was supposed to be invisible.
                         */
			struct timespec save_atime = ip->i_atime;
			xfer = generic_file_read(&file, bufp, len, &off);
			ip->i_atime = save_atime;
	                mark_inode_dirty(ip);
	} else {
			/* generic_file_write updates the mtime/ctime but we need
			 * to undo that because this I/O was supposed to be
			 * invisible.
			 */
			struct timespec save_mtime = ip->i_mtime;
			struct timespec save_ctime = ip->i_ctime;
			xfer = generic_file_write(&file, bufp, len, &off);
			ip->i_mtime = save_mtime;
			ip->i_ctime = save_ctime;
	                mark_inode_dirty(ip);
	}
	if (xfer >= 0) {
		*rvp = xfer;
		error = 0;
	} else {
		error = (int)xfer;
	}

	if (file.f_mode & FMODE_WRITE)
		put_write_access(ip);
	if (bCloseFile) {
		/* Calling close_private_file results in calling jfs_release,
		 * which results in a DM_EVENT_CLOSE event; so do necessary
		 * jfs_release stuff here and eliminate release
		 */
		if (ji->active_ag != -1) {
			struct bmap *bmap = JFS_SBI(ip->i_sb)->bmap;
			atomic_dec(&bmap->db_active[ji->active_ag]);
			ji->active_ag = -1;
		}
		file.f_op->release = NULL;
		close_private_file(&file);
	}
	dput(dentry);
	return error;
}

/* ARGSUSED */
STATIC int
jfs_dm_clear_inherit(
	struct inode	*ip,
	dm_right_t	right,
	dm_attrname_t	*attrnamep)
{
	return(-ENOSYS);
}


/* ARGSUSED */
STATIC int
jfs_dm_create_by_handle(
	struct inode	*ip,
	dm_right_t	right,
	void		*hanp,
	size_t		hlen,
	char		*cname)
{
	return(-ENOSYS);
}


/* ARGSUSED */
STATIC int
jfs_dm_downgrade_right(
	struct inode	*ip,
	dm_right_t	right,
	u_int		type)		/* DM_FSYS_OBJ or zero */
{
#ifdef	DEBUG_RIGHTS
	char		buffer[sizeof(jfs_handle_t) * 2 + 1];

	if (!jfs_ip_to_hexhandle(ip, type, buffer)) {
		printf("dm_downgrade_right: old %d new %d type %d handle %s\n",
			right, DM_RIGHT_SHARED, type, buffer);
	} else {
		printf("dm_downgrade_right: old %d new %d type %d handle "
			"<INVALID>\n", right, DM_RIGHT_SHARED, type);
	}
#endif	/* DEBUG_RIGHTS */
	return(0);
}

#define NUM_XADS_PER_QUERY 10

/* Note: jfs_dm_get_allocinfo() makes no attempt to coalesce two adjacent
   extents when both are of type DM_EXTENT_RES; this is left to the caller.
   JFS guarantees that there will never be two adjacent DM_EXTENT_HOLE extents.
*/

STATIC int
jfs_dm_get_allocinfo_rvp(
	struct inode	*ip,
	dm_right_t	right,
	dm_off_t	*offp,
	u_int		nelem,
	dm_extent_t	*extentp,
	u_int		*nelemp,
	int		*rvp)
{
	dm_off_t	fsb_offset;
	dm_size_t	fsb_length;
	int		elem;

	dm_extent_t	extent;
	struct lxdlist	lxdlist;
	lxd_t		lxd;
	struct xadlist	xadlist;
	xad_t		*pxad_array = NULL;
	int		alloc_size = 0;
	int i, error;

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if ((ip->i_mode & S_IFMT) != S_IFREG)
		return(-EINVAL);

	if (nelem == 0) {
		if (put_user(0, nelemp))
			return(-EFAULT);
		return(-EINVAL);
	}

	if (copy_from_user( &fsb_offset, offp, sizeof(fsb_offset)))
		return(-EFAULT);

	/* Real extent can't start on anything other than blocksize boundary */
	if (fsb_offset & (ip->i_blksize-1))
		return(-EINVAL);

	fsb_length = ip->i_size - fsb_offset; 	

	if (fsb_length <= 0) {
		if (put_user(0, nelemp))
			return(-EFAULT);
		*rvp = 0;
		return(0);
	}

	elem = 0;

	/* Obtain single array of xads that covers entire hole */
	do {
		/* Free prior xad array if one exists */
		if (pxad_array != NULL) {
			kmem_free(pxad_array, alloc_size);
		}

		elem += 16; /* 256-byte chunk */
		alloc_size = elem * sizeof(xad_t);
		pxad_array = kmem_alloc(alloc_size, KM_SLEEP);

		if (pxad_array == NULL)
			return -ENOMEM;

		lxdlist.maxnlxd = lxdlist.nlxd = 1;
		LXDlength(&lxd, (fsb_length + (ip->i_blksize-1)) >> ip->i_blkbits); 	
		LXDoffset(&lxd, fsb_offset >> ip->i_blkbits);
		lxdlist.lxd = &lxd;

		xadlist.maxnxad = xadlist.nxad = elem;
		xadlist.xad = pxad_array;

		IREAD_LOCK(ip);
		error = xtLookupList(ip, &lxdlist, &xadlist, 0);
		IREAD_UNLOCK(ip);

		if (error) {
			if (pxad_array != NULL)
				kmem_free(pxad_array, alloc_size);
			return error;
		}
	} while ((xadlist.nxad == elem) && 
		 ((offsetXAD(&xadlist.xad[elem-1]) + lengthXAD(&xadlist.xad[elem-1])) < fsb_offset + fsb_length));

	elem = 0;

	if (xadlist.nxad > 0) {
		extent.ex_type = DM_EXTENT_INVALID;
		extent.ex_length = 0;
		extent.ex_offset = fsb_offset;

		for (i = 0; (i < xadlist.nxad) && (elem < nelem) && (fsb_length > 0); i++) {
			dm_off_t	xad_off;
			dm_size_t	xad_len;
			xad_t		*pxad;

			pxad = &xadlist.xad[i];
			xad_off = offsetXAD(pxad) << ip->i_blkbits;
			xad_len = lengthXAD(pxad) << ip->i_blkbits;
			if (xad_off == extent.ex_offset + extent.ex_length) {
				/* found contiguous extent */
				if (extent.ex_type == DM_EXTENT_INVALID) {
					extent.ex_type = pxad->flag & XAD_NOTRECORDED ? DM_EXTENT_HOLE : DM_EXTENT_RES;
				} else if (!(((pxad->flag & XAD_NOTRECORDED) && 
					(extent.ex_type == DM_EXTENT_HOLE)) ||
				       ((!(pxad->flag & XAD_NOTRECORDED)) && 
					(extent.ex_type == DM_EXTENT_RES)))) {
					/* done with current extent */
					if (copy_to_user( extentp, &extent, sizeof(extent)))
						return(-EFAULT);
					
					fsb_offset += extent.ex_length;
					fsb_length -= extent.ex_length;
					elem++;
					extentp++;

					/* initialize new extent */
					extent.ex_type = pxad->flag & XAD_NOTRECORDED ? DM_EXTENT_HOLE : DM_EXTENT_RES;
					extent.ex_offset += extent.ex_length;
					extent.ex_length = 0;
				}

				extent.ex_length += xad_len;
			} else {
				/* found non-contiguous extent (hole) */
				dm_size_t holelen = xad_off - (extent.ex_offset + extent.ex_length);

				if (extent.ex_type == DM_EXTENT_RES) {
					/* done with current extent */
					if (copy_to_user(extentp, &extent, sizeof(extent)))
						return(-EFAULT);

					fsb_offset += extent.ex_length;
					fsb_length -= extent.ex_length;
					elem++; 
					extentp++;

					/* initialize hole extent */
					extent.ex_type = DM_EXTENT_HOLE;
					extent.ex_offset += extent.ex_length;
					extent.ex_length = holelen;
				} else {
					extent.ex_type = DM_EXTENT_HOLE;
					extent.ex_length += holelen;
				}

				if (pxad->flag & XAD_NOTRECORDED) {
					/* add to hole */
					extent.ex_length += xad_len;
				} else {
					/* done with hole */
					if (elem >= nelem)
						break;

					if (copy_to_user(extentp, &extent, sizeof(extent)))
						return(-EFAULT);

					fsb_offset += extent.ex_length;
					fsb_length -= extent.ex_length;
					elem++; 
					extentp++;

					/* initialize resident extent */
					extent.ex_type = DM_EXTENT_RES;
					extent.ex_offset = xad_off;
					extent.ex_length = xad_len;
				}
			}

			/* current extent has gone past end of file */
			if (extent.ex_offset + extent.ex_length > fsb_offset + fsb_length) {
				dm_size_t len = (extent.ex_offset + extent.ex_length) - (fsb_offset + fsb_length);
				extent.ex_length -= len;
				break;
			}
		}
	} else if (fsb_offset < ip->i_size) {
		extent.ex_type = DM_EXTENT_HOLE;
		extent.ex_length = fsb_length;
		extent.ex_offset = fsb_offset;
	}
		
	/* put current extent if room is available */
	if (elem < nelem && fsb_length > 0) {
		if (fsb_offset + extent.ex_length < ip->i_size) {
			/* hole at end of file */
			if (extent.ex_type == DM_EXTENT_HOLE) {
				extent.ex_length += (ip->i_size - fsb_offset - extent.ex_length);
			} else {
				if (copy_to_user( extentp, &extent, sizeof(extent)))
					return(-EFAULT);
		
				fsb_offset += extent.ex_length;
				fsb_length -= extent.ex_length;
				elem++;
				extentp++;

				/* initialize hole extent */
				extent.ex_type = DM_EXTENT_HOLE;
				extent.ex_offset = fsb_offset;
				extent.ex_length = fsb_length;
			}
		}

		if (elem < nelem && fsb_length > 0) {
			if (copy_to_user( extentp, &extent, sizeof(extent)))
				return(-EFAULT);
		
			fsb_offset += extent.ex_length;
			fsb_length -= extent.ex_length;
			elem++;
			extentp++;
		}
	}

	if (pxad_array != NULL)
		kmem_free(pxad_array, alloc_size);

	if (copy_to_user( offp, &fsb_offset, sizeof(fsb_offset)))
		return(-EFAULT);

	if (copy_to_user( nelemp, &elem, sizeof(elem)))
		return(-EFAULT);

	*rvp = (fsb_length == 0 ? 0 : 1);

	return(0);
}


/* ARGSUSED */
STATIC int
jfs_dm_get_bulkall_rvp(
	struct inode	*ip,
	dm_right_t	right,
	u_int		mask,
	dm_attrname_t	*attrnamep,
	dm_attrloc_t	*locp,
	size_t		buflen,
	void		*bufp,		/* address of buffer in user space */
	size_t		*rlenp,		/* user space address */
	int		*rvalp)
{
	return(-ENOSYS);
}


/*
 * TBD, although unused by TSM
 */
/* ARGSUSED */
STATIC int
jfs_dm_get_bulkattr_rvp(
	struct inode	*ip,
	dm_right_t	right,
	u_int		mask,
	dm_attrloc_t	*locp,
	size_t		buflen,
	void		*bufp,
	size_t		*rlenp,
	int		*rvalp)
{
#if 0	
	int		error, done;
	int		nelems;
	u_int		statstruct_sz;
	dm_attrloc_t	loc;

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if (copy_from_user( &loc, locp, sizeof(loc)))
		return(-EFAULT);

	/* Because we will write directly to the user's buffer, make sure that
	   the buffer is properly aligned.
	*/

	if (((__psint_t)bufp & (DM_STAT_ALIGN - 1)) != 0)
		return(-EFAULT);

	/* size of the handle is constant for this function */

	statstruct_sz = DM_STAT_SIZE(mask, 0);
	statstruct_sz = (statstruct_sz+(DM_STAT_ALIGN-1)) & ~(DM_STAT_ALIGN-1);

	nelems = buflen / statstruct_sz;
	if (nelems < 1) {
		if (put_user( statstruct_sz, rlenp ))
			return(-EFAULT);
		return(-E2BIG);
	}

	/*
	 * fill the buffer with dm_stat_t's
	 */
	// TBD

	if (error)
		return(error);
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
	if (put_user( 0,
	    &((dm_stat_t *)((char *)bufp + statstruct_sz*(nelems-1)))->_link)
	   )
		return(-EFAULT);
#endif	
	return(-ENOSYS);
}


/* ARGSUSED */
STATIC int
jfs_dm_get_config(
	struct inode	*ip,
	dm_right_t	right,
	dm_config_t	flagname,
	dm_size_t	*retvalp)
{
	dm_size_t	retval;

	switch (flagname) {
	case DM_CONFIG_DTIME_OVERLOAD:
	case DM_CONFIG_PERS_ATTRIBUTES:
	case DM_CONFIG_PERS_MANAGED_REGIONS:
	case DM_CONFIG_PUNCH_HOLE:
	case DM_CONFIG_WILL_RETRY:
		retval = DM_TRUE;
		break;

	case DM_CONFIG_CREATE_BY_HANDLE:	/* these will never be done */
	case DM_CONFIG_LOCK_UPGRADE:
	case DM_CONFIG_PERS_EVENTS:
	case DM_CONFIG_PERS_INHERIT_ATTRIBS:
		retval = DM_FALSE;
		break;

	case DM_CONFIG_BULKALL:			/* these will be done someday */
		retval = DM_FALSE;
		break;
	case DM_CONFIG_MAX_ATTR_ON_DESTROY:
		retval = DM_MAX_ATTR_BYTES_ON_DESTROY;
		break;

	case DM_CONFIG_MAX_ATTRIBUTE_SIZE:
		retval = MAXEASIZE;
		break;

	case DM_CONFIG_MAX_HANDLE_SIZE:
		retval = DM_MAX_HANDLE_SIZE;
		break;

	case DM_CONFIG_MAX_MANAGED_REGIONS:
#ifdef DM_SUPPORT_ONE_MANAGED_REGION
		retval = 1;
#else		
		retval = MAX_MANAGED_REGIONS;    /* actually it's unlimited */
#endif		
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
jfs_dm_get_config_events(
	struct inode	*ip,
	dm_right_t	right,
	u_int		nelem,
	dm_eventset_t	*eventsetp,
	u_int		*nelemp)
{
	int		highSetEvent;				// XFS BUG #18
	dm_eventset_t	eventset;

	//if (nelem == 0)					// XFS BUG #18
	//	return(-EINVAL);				// XFS BUG #18

	eventset = DM_JFS_SUPPORTED_EVENTS;

	// XFS BUG #18 START
	highSetEvent = jfs_dm_get_high_set_event(&eventset);
	if (highSetEvent > nelem) {
		*nelemp = highSetEvent;
		return(-E2BIG);
	}
	// XFS BUG #18 END

	/* Now copy the event mask and event count back to the caller.	We
	   return the lesser of nelem and DM_EVENT_MAX.
	*/

	if (nelem > DM_EVENT_MAX)
		nelem = DM_EVENT_MAX;
	eventset &= (1 << nelem) - 1;

	if (copy_to_user( eventsetp, &eventset, sizeof(eventset)))
		return(-EFAULT);

	if (put_user(highSetEvent, nelemp))			// XFS BUG #18
		return(-EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
jfs_dm_get_destroy_dmattr(
	struct inode	*ip,
	dm_right_t	right,
	dm_attrname_t	*attrnamep,
	char		**valuepp,
	int		*vlenp)
{
	char		buffer[128];
	dm_dkattrname_t dkattrname;
	int		alloc_size;
	int		value_len;
	char		*value;
	int		bytes_read;

	*vlenp = -1;		/* assume failure by default */

	if (attrnamep->an_chars[0] == '\0')
		return(-EINVAL);

	/* Build the on-disk version of the attribute name. */

	strcpy(dkattrname.dan_chars, dmattr_prefix);
	strncpy(&dkattrname.dan_chars[DMATTR_PREFIXLEN],
		(char *)attrnamep->an_chars, DM_ATTR_NAME_SIZE + 1);
	dkattrname.dan_chars[sizeof(dkattrname.dan_chars) - 1] = '\0';

	alloc_size = 0;
	value_len = sizeof(buffer);	/* in/out parameter */
	value = buffer;

	bytes_read = __jfs_getxattr(ip, dkattrname.dan_chars, value, value_len);

	if (bytes_read == -ERANGE) {
		alloc_size = MAXEASIZE;
		value = kmalloc(alloc_size, SLAB_KERNEL);
		if (value == NULL) {
			printk("%s/%d: kmalloc returned NULL\n", __FUNCTION__, __LINE__);
			return(-ENOMEM);
		}

		bytes_read = __jfs_getxattr(ip, dkattrname.dan_chars, value, 
			alloc_size);
		DM_EA_XLATE_ERR(bytes_read);
	}
	if (bytes_read < 0) {
		if (alloc_size)
			kfree(value);
		DM_EA_XLATE_ERR(bytes_read);
		return(bytes_read);
	} else
		value_len = bytes_read;

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


STATIC int
jfs_dm_get_dirattrs_rvp(
	struct inode	*ip,
	dm_right_t	right,
	u_int		mask,
	dm_attrloc_t	*locp,
	size_t		buflen,
	void		*bufp,		/* address of buffer in user space */
	size_t		*rlenp,		/* user space address */
	int		*rvp)
{
	size_t		direntbufsz, statbufsz;
	size_t		nread, spaceleft, nwritten=0, totnwritten=0;
	void		*direntp, *statbufp;
	int		error;
	dm_attrloc_t	loc, dirloc;
	size_t		offlastlink;
	int		*lastlink = NULL;

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if (mask & ~(DM_AT_HANDLE|DM_AT_EMASK|DM_AT_PMANR|DM_AT_PATTR|DM_AT_DTIME|DM_AT_CFLAG|DM_AT_STAT)) // XFS BUG #24
		return(-EINVAL);				// XFS BUG #24

	if (copy_from_user( &loc, locp, sizeof(loc)))
		return(-EFAULT);

/*	if ((buflen / DM_STAT_SIZE(mask, 2))== 0) {
		if (put_user( DM_STAT_SIZE(mask, 2), rlenp ))
			return(-EFAULT);
		return(-E2BIG);
	} XFS BUG #26 */

	if ((ip->i_mode & S_IFMT) != S_IFDIR)
		return(-ENOTDIR);

	/*
	 * Don't get more dirents than are guaranteed to fit.
	 * The minimum that the stat buf holds is the buf size over
	 * maximum entry size.	That times the minimum dirent size
	 * is an overly conservative size for the dirent buf.
	 */
	statbufsz = PAGE_SIZE;
	direntbufsz = (PAGE_SIZE / DM_STAT_SIZE(mask, JFS_NAME_MAX + 1))
			* sizeof(struct jfs_dirent);

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

		down(&ip->i_sem);

		/* See if the directory was removed after it was opened. */
		if (ip->i_nlink <= 0) {
			up(&ip->i_sem);
			error = -ENOENT;
			break;
		}
		if (dir_gen == 0)
			dir_gen = ip->i_generation;
		else if (dir_gen != ip->i_generation) {
			/* if dir changed, quit.  May be overzealous... */
			up(&ip->i_sem);
			break;
		}
		dirloc = loc;
		error = jfs_get_dirents(ip, direntp, direntbufsz,
						(dm_off_t *)&dirloc, &nread);
		up(&ip->i_sem);

		if (error) {
			break;
		}
		if (nread == 0)
			break;
		/*
		 * Now iterate thru them and call bulkstat_one() on all
		 * of them
		 */
		error = jfs_dirents_to_stats(ip,
					  mask,
					  (struct jfs_dirent *) direntp,
					  statbufp,
					  &nread,
					  &spaceleft,
					  &nwritten,
					  (dm_off_t *)&loc,
					  &offlastlink);
		if (error) {
			break;
		}

		if (nwritten) {
			if (copy_to_user( bufp, statbufp, nwritten)) {
				error = -EFAULT;
				break;
			}

			lastlink = (int *)((char *)bufp + offlastlink);
			bufp = (char *)bufp + nwritten;
			totnwritten += nwritten;
		}

		/*
		 * Done if dirents_to_stats unable to convert all entries
		 * returned by get_dirents
		 */
		if (nread > 0)
			break;
		else
			loc = dirloc;
	} while (spaceleft && (dirloc != DIREND));

	/* 
	 * Need to terminate (set _link to 0) last entry if there's room for 
	 * more (if we ran out of room in user buffer, dirents_to_stats set
	 * _link to 0 already)
	 */
	if (!error && spaceleft && lastlink) {
		int i = 0;
		if (copy_to_user( lastlink, &i, sizeof(i))) {
			error = -EFAULT;
		}
	}

	/*
	 *  If jfs_dirents_to_stats found anything, there might be more to do.
	 *  If it didn't read anything, signal all done (rval == 0).
	 *  (Doesn't matter either way if there was an error.)
	 */
	if (nread || dirloc != DIREND) {
		*rvp = 1;
	} else {
		loc = DIREND;
		*rvp = 0;
	}

	kmem_free(statbufp, statbufsz);
	kmem_free(direntp, direntbufsz);
	if (!error){
		if (put_user(totnwritten, rlenp))
			return(-EFAULT);
	}

	if (!error && copy_to_user(locp, &loc, sizeof(loc)))
		error = -EFAULT;
	return(error);
}


STATIC int
jfs_dm_get_dmattr(
	struct inode	*ip,
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
	int		bytes_read;
	int		error;
	
	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if ((error = jfs_copyin_attrname(attrnamep, &name)) != 0)
		return(error);

	/* Allocate a buffer to receive the attribute's value.	We allocate
	   at least one byte even if the caller specified a buflen of zero.
	   (A buflen of zero is considered valid.)
	*/

	alloc_size = buflen;
	if ((alloc_size < 0) || (alloc_size > MAXEASIZE))
		alloc_size = MAXEASIZE;
	value = kmem_alloc(alloc_size, KM_SLEEP);

	/* Get the attribute's value. */

	value_len = alloc_size;		/* in/out parameter */

	bytes_read = __jfs_getxattr(ip, name.dan_chars, value, value_len);

	/* Bump up buffer size and try again if user buffer too small */
	if (bytes_read == -ERANGE) {
		if (value != NULL)
			kmem_free(value, alloc_size);
			
		alloc_size = MAXEASIZE;
		value = kmem_alloc(alloc_size, KM_SLEEP);

		value_len = alloc_size;		/* in/out parameter */
		bytes_read = __jfs_getxattr(ip, name.dan_chars, value, 
			value_len);
	}		
	
	if (bytes_read >= 0) {
		error = 0;
		value_len = bytes_read;
	} else {
		error = bytes_read;
		DM_EA_XLATE_ERR(error);
	}

	/* DMAPI requires an errno of ENOENT if an attribute does not exist,
	   so remap ENOATTR here.
	*/

	if (!error && value_len > buflen)
		error = -E2BIG;
	if (!error && copy_to_user(bufp, value, value_len))
		error = -EFAULT;
	if (!error || error == -E2BIG) {
		if (put_user(value_len, rlenp))
			error = -EFAULT;
	}

	kmem_free(value, alloc_size);
	return(error);
}

STATIC int
jfs_dm_get_eventlist(
	struct inode	*ip,
	dm_right_t	right,
	u_int		type,
	u_int		nelem,
	dm_eventset_t	*eventsetp,
	u_int		*nelemp)
{
	int		error;

	if (type == DM_FSYS_OBJ) {
		error = jfs_dm_fs_get_eventlist(ip, right, nelem,
			eventsetp, nelemp);
	} else {
		error = jfs_dm_f_get_eventlist(ip, right, nelem,
			eventsetp, nelemp);
	}
	return(error);
}


/* ARGSUSED */
STATIC int
jfs_dm_get_fileattr(
	struct inode	*ip,
	dm_right_t	right,
	u_int		mask,		/* not used; always return everything */
	dm_stat_t	*statp)
{
	dm_stat_t	stat;

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if (mask & ~(DM_AT_EMASK|DM_AT_PMANR|DM_AT_PATTR|DM_AT_DTIME|DM_AT_CFLAG|DM_AT_STAT)) // XFS BUG #22
		return(-EINVAL);				// XFS BUG #22

// XFS BUG #23 START	
	/* don't update dtime if there are no DM attrs, and initialize dtime
	   field so user will see it didn't change as there is no error 
	   indication returned */
	if ((mask & DM_AT_DTIME) && (!jfs_dmattr_exist(ip))) {
		mask = mask & (~DM_AT_DTIME);
		if (copy_from_user(&stat.dt_dtime,
				   &statp->dt_dtime,
				   sizeof(stat.dt_dtime)))
			return(-EFAULT);
	}
// XFS BUG #23 END

	jfs_ip_to_stat(ip, mask, &stat);

	if (copy_to_user( statp, &stat, sizeof(stat)))
		return(-EFAULT);
	return(0);
}


#ifdef DM_SUPPORT_ONE_MANAGED_REGION

/* We currently only support a maximum of one managed region per file, and
   use the DM_EVENT_READ, DM_EVENT_WRITE, and DM_EVENT_TRUNCATE events in
   the file's dm_eventset_t event mask to implement the DM_REGION_READ,
   DM_REGION_WRITE, and DM_REGION_TRUNCATE flags for that single region.
*/

STATIC int
jfs_dm_get_region(
	struct inode	*ip,
	dm_right_t	right,
	u_int		nelem,
	dm_region_t	*regbufp,
	u_int		*nelemp)
{
	dm_eventset_t	evmask;
	dm_region_t	region;
	u_int		elem;
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	evmask = jfs_ip->dmattrs.da_dmevmask;	/* read the mask "atomically" */

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

#else

STATIC int
jfs_dm_get_region(
	struct inode	*ip,
	dm_right_t	right,
	u_int		nelem,
	dm_region_t	*regbufp,
	u_int		*nelemp)
{
	u_int		elem;
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	elem = jfs_ip->dmnumrgns;
		
	if (copy_to_user( nelemp, &elem, sizeof(elem)))
		return(-EFAULT);
	if (elem > nelem)
		return(-E2BIG);
	if (elem && copy_to_user(regbufp, jfs_ip->dmrgns, 
				elem * sizeof(dm_region_t)))
		return(-EFAULT);
	return(0);
}
#endif


STATIC int
jfs_dm_getall_dmattr(
	struct inode	*ip,
	dm_right_t	right,
	size_t		buflen,
	void		*bufp,
	size_t		*rlenp)
{
	dm_attrlist_t	*ulist;
	int		*last_link;
	int		alignment;
	int		error;
	int		eabuf_size;
	ssize_t		req_size = 0;
	struct jfs_ea_list *ealist;
	struct jfs_ea *ea;
	struct ea_buffer eabuf;

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

	/* Get a buffer full of attribute names.  If there aren't any
	   more or if we encounter an error, then finish up.
	*/

	/* This will block all get/set EAs */
	down_read(&JFS_IP(ip)->xattr_sem);

	/* Get all EAs */
	eabuf_size = jfs_ea_get(ip, &eabuf, 0);

	/* Quit if error occurred */
	if (eabuf_size < 0) {
		error = eabuf_size;
		goto error_return;
	}

	/* Quit if no EAs exist */
	if (eabuf_size == 0) {
		error = req_size = 0;
		goto error_return_release;
	}

	ealist = (struct jfs_ea_list *) eabuf.xattr;

	/* compute required size of list */
	req_size = 0;
	for (ea = FIRST_EA(ealist); ea < END_EALIST(ealist); ea = NEXT_EA(ea)) {
		char	*user_name;
		int	size_needed;
		
		/* Skip over all non-DMAPI attributes.	If the
		   attribute name is too long, we assume it is
		   non-DMAPI even if it starts with the correct
		   prefix.
		*/
		if (strncmp(ea->name, dmattr_prefix, DMATTR_PREFIXLEN))
			continue;
		user_name = &ea->name[DMATTR_PREFIXLEN];
		if (strlen(user_name) > DM_ATTR_NAME_SIZE)
			continue;
	
		/* We have a valid DMAPI attribute to return.  If it
		   won't fit in the user's buffer, we still need to
		   keep track of the number of bytes for the user's
		   next call.
		*/
		size_needed = sizeof(*ulist) + ea->valuelen;
		size_needed = (size_needed + alignment) & ~alignment;

		req_size += size_needed;
	}

	/* Quit if no buffer for dm_attrlist or buffer too small */
	if ((!bufp) || (req_size > buflen)) {
		error = -E2BIG;
		goto error_return_release;
	}

	/* copy contents into list */
	ulist = (dm_attrlist_t *)bufp;
	last_link = NULL;
	for (ea = FIRST_EA(ealist); ea < END_EALIST(ealist); ea = NEXT_EA(ea)) {
		char	*user_name;
		int	size_needed;
		
		/* Skip over all non-DMAPI attributes.	If the
		   attribute name is too long, we assume it is
		   non-DMAPI even if it starts with the correct
		   prefix.
		*/
		if (strncmp(ea->name, dmattr_prefix, DMATTR_PREFIXLEN))
			continue;
		user_name = &ea->name[DMATTR_PREFIXLEN];
		if (strlen(user_name) > DM_ATTR_NAME_SIZE)
			continue;
	
		/* We have a valid DMAPI attribute to return.  If it
		   won't fit in the user's buffer, we still need to
		   keep track of the number of bytes for the user's
		   next call.
		*/
		size_needed = sizeof(*ulist) + ea->valuelen;
		size_needed = (size_needed + alignment) & ~alignment;

		strncpy((char *)ulist->al_name.an_chars, user_name,
				DM_ATTR_NAME_SIZE);
		ulist->al_data.vd_offset = sizeof(*ulist);
		ulist->al_data.vd_length = ea->valuelen;
		ulist->_link =	size_needed;
		last_link = &ulist->_link;

		/* Next read the attribute's value into its correct
		   location after the dm_attrlist structure.  Any sort
		   of error indicates that the data is moving under us,
		   so we return EIO to let the user know.
		*/

		memcpy((void *)(ulist + 1),
		       (char *)ea + sizeof(ea) + ea->namelen + 1,
		       ea->valuelen);

		ulist = (dm_attrlist_t *)((char *)ulist + ulist->_link);
	}

	if (last_link)
		*last_link = 0;
	error = 0;
		
error_return_release:
	jfs_ea_release(ip, &eabuf);

error_return:	
	/* This will unblock all get/set EAs */
	up_read(&JFS_IP(ip)->xattr_sem);

	if (!error || error == -E2BIG) {
		if (put_user(req_size, rlenp))
			error = -EFAULT;
	}

	return(error);
}


/* ARGSUSED */
STATIC int
jfs_dm_getall_inherit(
	struct inode	*ip,
	dm_right_t	right,
	u_int		nelem,
	dm_inherit_t	*inheritbufp,
	u_int		*nelemp)
{
	return(-ENOSYS);
}


/* Initialize location pointer for subsequent dm_get_dirattrs,
   dm_get_bulkattr, and dm_get_bulkall calls.  The same initialization must
   work for vnode-based routines (dm_get_dirattrs) and filesystem-based
   routines (dm_get_bulkattr and dm_get_bulkall).  Filesystem-based functions
   call this routine using the filesystem's root vnode.
*/

/* ARGSUSED */
STATIC int
jfs_dm_init_attrloc(
	struct inode	*ip,
	dm_right_t	right,
	dm_attrloc_t	*locp)
{
	dm_attrloc_t	loc = 0;

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if (copy_to_user( locp, &loc, sizeof(loc)))
		return(-EFAULT);
	return(0);
}


/* ARGSUSED */
STATIC int
jfs_dm_mkdir_by_handle(
	struct inode	*ip,
	dm_right_t	right,
	void		*hanp,
	size_t		hlen,
	char		*cname)
{
	return(-ENOSYS);
}


/* ARGSUSED */
STATIC int
jfs_dm_probe_hole(
	struct inode	*ip,
	dm_right_t	right,
	dm_off_t	off,
	dm_size_t	len,		/* we ignore this for now */
	dm_off_t	*roffp,
	dm_size_t	*rlenp)
{
	dm_off_t	roff;
	dm_size_t	rlen;
	loff_t		realsize;
	u_int		bsize;

	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if ((ip->i_mode & S_IFMT) != S_IFREG)
		return(-EINVAL);

	bsize = ip->i_sb->s_blocksize;

	realsize = ip->i_size;

	if ((off >= realsize) || (off + len > realsize))	// XFS BUG #8
		return(-E2BIG);

	roff = (off + bsize-1) & ~(bsize-1);
	if ((len == 0) || (off + len == realsize)) {
		rlen = 0;
	} else {
		rlen = ((off + len) & ~(bsize-1)) - roff;
	}

	/* hole doesn't exist! */
	if ((roff != 0) && (roff == rlen))
		return(-EINVAL);

	if (copy_to_user( roffp, &roff, sizeof(roff)))
		return(-EFAULT);
	if (copy_to_user( rlenp, &rlen, sizeof(rlen)))
		return(-EFAULT);
	return(0);
}


STATIC int
jfs_dm_punch_hole(
	struct inode	*ip,
	dm_right_t	right,
	dm_off_t	off,
	dm_size_t	len)
{
	u_int		bsize;
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);
	loff_t		realsize;
	tid_t		tid;
	int		error = 0;

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if (!S_ISREG(ip->i_mode))
		return(-EINVAL);
	if (!prio_tree_empty(&ip->i_mapping->i_mmap))
		return(-EBUSY);

	IWRITE_LOCK(ip);

	bsize = ip->i_sb->s_blocksize;

	realsize = ip->i_size;

	if ((off >= realsize) || (off + len > realsize)) {
		IWRITE_UNLOCK(ip);
		return(-E2BIG);
	}

	/* hole begin and end must be aligned on blocksize if not truncate */
	if (off + len == realsize)
		len = 0;
	if ((off & (bsize-1)) || (len & (bsize-1))) {
		IWRITE_UNLOCK(ip);
		return(-EAGAIN);
	}

	tid = txBegin(ip->i_sb, 0);
	down(&JFS_IP(ip)->commit_sem);
	error = xtPunchHole(tid, ip, off, len, 0);
	if (!error) 
		error = txCommit(tid, 1, &ip, 0);
	else
		txAbort(tid, 1);
	txEnd(tid);
	up(&JFS_IP(ip)->commit_sem);
	IWRITE_UNLOCK(ip);
		
	/* Let threads in send_data_event know we punched the file. */
	if (!error)
		jfs_ip->dmattrs.da_dmstate++;

	return(error);
}


STATIC int
jfs_dm_read_invis_rvp(
	struct inode	*ip,
	dm_right_t	right,
	dm_off_t	off,
	dm_size_t	len,
	void		*bufp,
	int		*rvp)
{
	if (right < DM_RIGHT_SHARED)
		return(-EACCES);

	if (off > ip->i_size)					// XFS BUG #9
		return(-EINVAL);				// XFS BUG #9

	return(jfs_dm_rdwr(ip, 0, FMODE_READ, off, len, bufp, rvp));
}


/* ARGSUSED */
STATIC int
jfs_dm_release_right(
	struct inode	*ip,
	dm_right_t	right,
	u_int		type)		/* DM_FSYS_OBJ or zero */
{
#ifdef	DEBUG_RIGHTS
	char		buffer[sizeof(jfs_handle_t) * 2 + 1];

	if (!jfs_ip_to_hexhandle(ip, type, buffer)) {
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
jfs_dm_remove_dmattr(
	struct inode	*ip,
	dm_right_t	right,
	int		setdtime,
	dm_attrname_t	*attrnamep)
{
	dm_dkattrname_t name;
	int		error;
	struct timespec save_ctime;

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if ((error = jfs_copyin_attrname(attrnamep, &name)) != 0)
		return(error);

	if (!setdtime) {
		save_ctime = ip->i_ctime;
	}

	/* Remove the attribute from the object. */

	error = __jfs_setxattr(ip, name.dan_chars, 0, 0, XATTR_REPLACE);
	
	if (!setdtime) {
		ip->i_ctime = save_ctime;
		mark_inode_dirty(ip);
	}

	/* Persistent attribute change */
	if (!error && setdtime) { 
		ip->i_version++;
		mark_inode_dirty(ip);
	}

	DM_EA_XLATE_ERR(error);

	return(error);
}


/* ARGSUSED */
STATIC int
jfs_dm_request_right(
	struct inode 	*ip,
	dm_right_t	right,
	u_int		type,		/* DM_FSYS_OBJ or zero */
	u_int		flags,
	dm_right_t	newright)
{
#ifdef	DEBUG_RIGHTS
	char		buffer[sizeof(jfs_handle_t) * 2 + 1];

	if (!jfs_ip_to_hexhandle(ip, type, buffer)) {
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
jfs_dm_set_dmattr(
	struct inode	*ip,
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
	struct timespec save_ctime;

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if ((error = jfs_copyin_attrname(attrnamep, &name)) != 0)
		return(error);
	if (buflen > MAXEASIZE)
		return(-E2BIG);

	/* Copy in the attribute's value and store the <name,value> pair in
	   the object.	We allocate a buffer of at least one byte even if the
	   caller specified a buflen of zero.  (A buflen of zero is considered
	   valid.)
	*/

	alloc_size = (buflen == 0) ? 1 : buflen;
	value = kmem_alloc(alloc_size, KM_SLEEP);
	if (copy_from_user( value, bufp, buflen)) {
		error = -EFAULT;
	} else {
		if (!setdtime) {
			save_ctime = ip->i_ctime;
		}

		error = __jfs_setxattr(ip, name.dan_chars, value, buflen, 0);
		
		if (!setdtime) {
			ip->i_ctime = save_ctime;
			mark_inode_dirty(ip);
		}

		DM_EA_XLATE_ERR(error);
	}
	kmem_free(value, alloc_size);
	
	/* Persistent attribute change */
	if (!error) { 
		ip->i_version++;
		mark_inode_dirty(ip);
	}

	return(error);
}

STATIC int
jfs_dm_set_eventlist(
	struct inode	*ip,
	dm_right_t	right,
	u_int		type,
	dm_eventset_t	*eventsetp,	/* in kernel space! */
	u_int		maxevent)
{
	int		error;

	if (type == DM_FSYS_OBJ) {
		error = jfs_dm_fs_set_eventlist(ip, right, eventsetp, maxevent);
	} else {
		error = jfs_dm_f_set_eventlist(ip, right, eventsetp, maxevent);
	}
	return(error);
}


/*
 * jfs_dm_setattr
 */
STATIC int
jfs_dm_setattr(
	struct inode		*ip,
	struct iattr		*iap)
{
	int			mask;
	int			code;
	uid_t			uid=0, iuid=0;
	gid_t			gid=0, igid=0;
	int			file_owner;

	/*
	 * Cannot set certain attributes.
	 */
	mask = iap->ia_valid;

//Q	/*
//U	 * If disk quotas is on, we make sure that the dquots do exist on disk,
//O	 * before we start any other transactions. Trying to do this later
//T	 * is messy. We don't care to take a readlock to look at the ids
//A	 * in inode here, because we can't hold it across the trans_reserve.
//	 * If the IDs do change before we take the ilock, we're covered
//	 * because the i_*dquot fields will get updated anyway.
//	 */
//	if (XFS_IS_QUOTA_ON(mp) && (mask & (XFS_AT_UID|XFS_AT_GID))) {
//		uint	qflags = 0;
//
//		if (mask & XFS_AT_UID) {
//			uid = vap->va_uid;
//			qflags |= XFS_QMOPT_UQUOTA;
//		} else {
//			uid = ip->i_d.di_uid;
//		}
//		if (mask & XFS_AT_GID) {
//			gid = vap->va_gid;
//			qflags |= XFS_QMOPT_GQUOTA;
//		}  else {
//			gid = ip->i_d.di_gid;
//		}
//		/*
//		 * We take a reference when we initialize udqp and gdqp,
//		 * so it is important that we never blindly double trip on
//		 * the same variable. See xfs_create() for an example.
//		 */
//		ASSERT(udqp == NULL);
//		ASSERT(gdqp == NULL);
//		code = XFS_QM_DQVOPALLOC(mp, ip, uid,gid, qflags, &udqp, &gdqp);
//		if (code)
//			return (code);
//	}

	IWRITE_LOCK(ip);

	/* boolean: are we the file owner? */
	file_owner = (current->fsuid == ip->i_uid);

	/*
	 * Change various properties of a file.
	 * Only the owner or users with CAP_FOWNER
	 * capability may do these things.
	 */
	if (mask &
	    (ATTR_MODE|ATTR_UID|ATTR_GID)) {
		/*
		 * CAP_FOWNER overrides the following restrictions:
		 *
		 * The user ID of the calling process must be equal
		 * to the file owner ID, except in cases where the
		 * CAP_FSETID capability is applicable.
		 */
		if (!file_owner && !capable(CAP_FOWNER)) {
			code = -EPERM;
			goto error_return;
		}

		/*
		 * CAP_FSETID overrides the following restrictions:
		 *
		 * The effective user ID of the calling process shall match
		 * the file owner when setting the set-user-ID and
		 * set-group-ID bits on that file.
		 *
		 * The effective group ID or one of the supplementary group
		 * IDs of the calling process shall match the group owner of
		 * the file when setting the set-group-ID bit on that file
		 */
		if (mask & ATTR_MODE) {
			mode_t m = 0;

			if ((iap->ia_mode & S_ISUID) && !file_owner)
				m |= S_ISUID;
			if ((iap->ia_mode & S_ISGID) &&
			    !in_group_p((gid_t)ip->i_gid))
				m |= S_ISGID;
			if (m && !capable(CAP_FSETID))
				iap->ia_mode &= ~m;
		}
	}

	/*
	 * Change file ownership.  Must be the owner or privileged.
	 * If the system was configured with the "restricted_chown"
	 * option, the owner is not permitted to give away the file,
	 * and can change the group id only to a group of which he
	 * or she is a member.
	 */
	if (mask & (ATTR_UID|ATTR_GID)) {
		/*
		 * These IDs could have changed since we last looked at them.
		 * But, we're assured that if the ownership did change
		 * while we didn't have the inode locked, inode's dquot(s)
		 * would have changed also.
		 */
		iuid = ip->i_uid;
		igid = ip->i_gid;
		gid = (mask & ATTR_GID) ? iap->ia_gid : igid;
		uid = (mask & ATTR_UID) ? iap->ia_uid : iuid;
		
//Q		/*
//U		 * Do a quota reservation only if uid or gid is actually
//O		 * going to change.
//T		 */
//A		if ((XFS_IS_UQUOTA_ON(mp) && iuid != uid) ||
//		    (XFS_IS_GQUOTA_ON(mp) && igid != gid)) {
//			ASSERT(tp);
//			code = XFS_QM_DQVOPCHOWNRESV(mp, tp, ip, udqp, gdqp,
//						capable(CAP_FOWNER) ?
//						XFS_QMOPT_FORCE_RES : 0);
//			if (code)	/* out of quota */
//				goto error_return;
//		}
	}

	/*
	 * Change file size.  Must have write permission and not be a directory.
	 */
	if (mask & ATTR_SIZE) {
		if ((ip->i_mode & S_IFMT) == S_IFDIR) {
			code = -EISDIR;
			goto error_return;
		} else if ((ip->i_mode & S_IFMT) != S_IFREG) {
			code = -EINVAL;
			goto error_return;
		}
		
//Q		/*
//U		 * Make sure that the dquots are attached to the inode.
//O		 */
//T		if ((code = XFS_QM_DQATTACH(mp, ip, XFS_QMOPT_ILOCKED)))
//A			goto error_return;
	}

	/*
	 * Change file access modes.
	 */
	if (mask & ATTR_MODE) {
		ip->i_mode &= S_IFMT;
		ip->i_mode |= iap->ia_mode & ~S_IFMT;
	}

	/*
	 * Change file ownership.  Must be the owner or privileged.
	 * If the system was configured with the "restricted_chown"
	 * option, the owner is not permitted to give away the file,
	 * and can change the group id only to a group of which he
	 * or she is a member.
	 */
	if (mask & (ATTR_UID|ATTR_GID)) {
		/*
		 * CAP_FSETID overrides the following restrictions:
		 *
		 * The set-user-ID and set-group-ID bits of a file will be
		 * cleared upon successful return from chown()
		 */
		if ((ip->i_mode & (S_ISUID|S_ISGID)) &&
		    !capable(CAP_FSETID)) {
			ip->i_mode &= ~(S_ISUID|S_ISGID);
		}

		/*
		 * Change the ownerships and register quota modifications
		 * in the transaction.
		 */
		if (iuid != uid) {
//Q			if (XFS_IS_UQUOTA_ON(mp)) {
//U				ASSERT(mask & XFS_AT_UID);
//O				ASSERT(udqp);
//T				olddquot1 = XFS_QM_DQVOPCHOWN(mp, tp, ip,
//A							&ip->i_udquot, udqp);
//			}
			ip->i_uid = uid;
		}
		if (igid != gid) {
//Q			if (XFS_IS_GQUOTA_ON(mp)) {
//U				ASSERT(mask & XFS_AT_GID);
//O				ASSERT(gdqp);
//T				olddquot2 = XFS_QM_DQVOPCHOWN(mp, tp, ip,
//A							&ip->i_gdquot, gdqp);
//			}
			ip->i_gid = gid;
		}
	}


	/*
	 * Change file access or modified times.
	 */
	if (mask & ATTR_ATIME) {
		ip->i_atime = iap->ia_atime;
	}
	if (mask & ATTR_MTIME) {
		ip->i_mtime = iap->ia_mtime;
	}

	/*
	 * Change file inode change time only if XFS_AT_CTIME set
	 * AND we have been called by a DMI function.
	 */
	if (mask & ATTR_CTIME) {
		ip->i_ctime = iap->ia_ctime;
	}

	/*
	 * Change file size.  	
	 */
	
	if (mask & ATTR_SIZE) {
		ip->i_size = iap->ia_size;

		if (iap->ia_size >= ip->i_size) {
			struct timespec curtime = CURRENT_TIME;
			if (!(mask & ATTR_MTIME)) {
				ip->i_mtime = curtime;
			}
			if (!(mask & ATTR_CTIME)) {
				ip->i_ctime = curtime;
			}
			mark_inode_dirty(ip);
		} else /* if (iap->ia_size < ip->i_size) */ {
			nobh_truncate_page(ip->i_mapping, ip->i_size);
		        jfs_truncate_nolock(ip, ip->i_size);
		}
	} else
		mark_inode_dirty(ip);

	IWRITE_UNLOCK(ip);

//Q	/*
//U	 * Release any dquot(s) the inode had kept before chown.
//O	 */
//T	XFS_QM_DQRELE(mp, olddquot1);
//A	XFS_QM_DQRELE(mp, olddquot2);
//	XFS_QM_DQRELE(mp, udqp);
//	XFS_QM_DQRELE(mp, gdqp);

	return 0;

 error_return:
//QUOTA	XFS_QM_DQRELE(mp, udqp);
//	XFS_QM_DQRELE(mp, gdqp);
	IWRITE_UNLOCK(ip);

	return code;
}


/*
 *  This turned out not XFS-specific, but leave it here with get_fileattr.
 */

STATIC int
jfs_dm_set_fileattr(
	struct inode	*ip,
	dm_right_t	right,
	u_int		mask,
	dm_fileattr_t	*statp)
{
	dm_fileattr_t	stat;
	struct iattr	at;
	int		error;

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if (mask & ~(DM_AT_ATIME|DM_AT_MTIME|DM_AT_CTIME|DM_AT_DTIME|DM_AT_UID|DM_AT_GID|DM_AT_MODE|DM_AT_SIZE)) // XFS BUG #20
		return(-EINVAL);				// XFS BUG #20
	
	if (copy_from_user( &stat, statp, sizeof(stat)))
		return(-EFAULT);

	at.ia_valid = 0;

	if (mask & DM_AT_MODE) {
		at.ia_valid |= ATTR_MODE;
		at.ia_mode = stat.fa_mode;
	}
	if (mask & DM_AT_UID) {
		at.ia_valid |= ATTR_UID;
		at.ia_uid = stat.fa_uid;
	}
	if (mask & DM_AT_GID) {
		at.ia_valid |= ATTR_GID;
		at.ia_gid = stat.fa_gid;
	}
	if (mask & DM_AT_ATIME) {
		at.ia_valid |= ATTR_ATIME;
		at.ia_atime.tv_sec = stat.fa_atime;
		at.ia_atime.tv_nsec = 0;
	}
	if (mask & DM_AT_MTIME) {
		at.ia_valid |= ATTR_MTIME;
		at.ia_mtime.tv_sec = stat.fa_mtime;
		at.ia_mtime.tv_nsec = 0;
	}
	if (mask & DM_AT_CTIME) {
		at.ia_valid |= ATTR_CTIME;
		at.ia_ctime.tv_sec = stat.fa_ctime;
		at.ia_ctime.tv_nsec = 0;
	}

	/* DM_AT_DTIME only takes effect if DM_AT_CTIME is not specified.  We
	   overload ctime to also act as dtime, i.e. DM_CONFIG_DTIME_OVERLOAD.
	*/

	if ((mask & DM_AT_DTIME) && jfs_dmattr_exist(ip) && !(mask & DM_AT_CTIME)) { // XFS BUG #21
		at.ia_valid |= ATTR_CTIME;
		at.ia_ctime.tv_sec = stat.fa_dtime;
		at.ia_ctime.tv_nsec = 0;
	}
	if (mask & DM_AT_SIZE) {
		at.ia_valid |= ATTR_SIZE;
		at.ia_size = stat.fa_size;
	}
	
	error = jfs_dm_setattr(ip, &at);
	return(error);
}


/* ARGSUSED */
STATIC int
jfs_dm_set_inherit(
	struct inode	*ip,
	dm_right_t	right,
	dm_attrname_t	*attrnamep,
	mode_t		mode)
{
	return(-ENOSYS);
}


#ifdef DM_SUPPORT_ONE_MANAGED_REGION

STATIC int
jfs_dm_set_region(
	struct inode	*ip,
	dm_right_t	right,
	u_int		nelem,
	dm_region_t	*regbufp,
	dm_boolean_t	*exactflagp)
{
	dm_region_t	region;
	dm_eventset_t	new_mask;
	dm_eventset_t	mr_mask;
	u_int		exactflag;
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);

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
	if ((new_mask & prohibited_mr_events(ip)) != 0)
		return(-EBUSY);
	mr_mask = DM_JFS_VALID_REGION_EVENTS;

	/* Get the file's existing event mask, clear the old managed region
	   bits, add in the new ones, and update the file's mask.
	*/

	jfs_ip->dmattrs.da_dmevmask = (jfs_ip->dmattrs.da_dmevmask & ~mr_mask)
	       				| new_mask;

	igrab(ip);
	mark_inode_dirty(ip);

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

#else

STATIC int
jfs_dm_set_region(
	struct inode	*ip,
	dm_right_t	right,
	u_int		nelem,
	dm_region_t	*regbufp,
	dm_boolean_t	*exactflagp)
{
	dm_eventset_t	new_mask;
	dm_eventset_t	new_mrevmask = 0;
	u_int		exactflag;
	u_int		changeflag;
	int		size_array = 0;
	dm_region_t 	*newrgns = NULL;
	struct jfs_inode_info *jfs_ip = JFS_IP(ip);

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if (nelem > MAX_MANAGED_REGIONS)
		return(-E2BIG);

	/* If the user provided dm_region_t structure(s), then copy in and
	   validate.  A call with zero regions is equivalent to clearing all 
	   regions.
	*/

	if (nelem != 0) {
		int i;
		size_array = nelem * sizeof(dm_region_t);
		newrgns = kmalloc(size_array, SLAB_KERNEL);

		if (newrgns == NULL) {
			printk("%s/%d: kmalloc returned NULL\n", __FUNCTION__, __LINE__);
			return -ENOMEM;
		}
		
		if (copy_from_user(newrgns, regbufp, size_array)) {
			kfree(newrgns);
			return(-EFAULT);
		}

		for (i = 0; i < nelem; i++) {
			if (newrgns[i].rg_flags & ~(DM_REGION_READ|DM_REGION_WRITE|DM_REGION_TRUNCATE)) {
				kfree(newrgns);
				return(-EINVAL);
			}

			new_mask = 0;

			/* No checking required if no events for region */
			if (newrgns[i].rg_flags != DM_REGION_NOEVENT) {
				if (newrgns[i].rg_flags & DM_REGION_READ)
					new_mask |= 1 << DM_EVENT_READ;
				if (newrgns[i].rg_flags & DM_REGION_WRITE)
					new_mask |= 1 << DM_EVENT_WRITE;
				if (newrgns[i].rg_flags & DM_REGION_TRUNCATE)
					new_mask |= 1 << DM_EVENT_TRUNCATE;

				if ((new_mask & 
				     prohibited_mr_events(ip, &newrgns[i])) != 0) {
					kfree(newrgns);
					return(-EBUSY);
				}
			}

			new_mrevmask |= new_mask;
		}
	}

	/* Determine if regions are same */
	if ((nelem == jfs_ip->dmnumrgns) && 
	    ((nelem == 0) ||
	     (memcmp(newrgns, jfs_ip->dmrgns, size_array) == 0))) {
		changeflag = 0;
	} else {
		changeflag = 1;
	}		

	jfs_ip->dmattrs.da_dmevmask = 
		(jfs_ip->dmattrs.da_dmevmask & ~DM_JFS_VALID_REGION_EVENTS)
	       				| new_mrevmask;

	jfs_ip->dmnumrgns = nelem;
	if (jfs_ip->dmrgns) {
		kfree(jfs_ip->dmrgns);
	}
	jfs_ip->dmrgns = newrgns;

	mark_inode_dirty(ip);

	/* We never change the user's managed region. */
	exactflag = DM_TRUE;
	if (copy_to_user( exactflagp, &exactflag, sizeof(exactflag)))
		return(-EFAULT);
	
	if (changeflag) {
		int error = jfs_dm_write_pers_data(jfs_ip);

		if (error) {
			jfs_ip->dmnumrgns = 0;
			if (jfs_ip->dmrgns) {
				kfree(jfs_ip->dmrgns);
			}
			jfs_ip->dmrgns = NULL;
			
			return(error);
		}
	}
	return(0);
}
#endif


/* ARGSUSED */
STATIC int
jfs_dm_symlink_by_handle(
	struct inode 	*ip,
	dm_right_t	right,
	void		*hanp,
	size_t		hlen,
	char		*cname,
	char		*path)
{
	return(-ENOSYS);
}


extern int jfs_commit_inode(struct inode *, int);

STATIC int
jfs_dm_sync_by_handle (
	struct inode	*ip,
	dm_right_t	right)
{
	int		rc = 0;

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	/* The following is basically jfs_fsync, but since we don't have
	 * the file * or dentry * the code is repeated here.
	 */
	if (!(ip->i_state & I_DIRTY)) {
		jfs_flush_journal(JFS_SBI(ip->i_sb)->log, 1);
		return rc;
	}

	rc |= jfs_commit_inode(ip, 1);

	return rc ? -EIO : 0;
}


/* ARGSUSED */
STATIC int
jfs_dm_upgrade_right(
	struct inode	*ip,
	dm_right_t	right,
	u_int		type)		/* DM_FSYS_OBJ or zero */
{
#ifdef	DEBUG_RIGHTS
	char		buffer[sizeof(jfs_handle_t) * 2 + 1];

	if (!jfs_ip_to_hexhandle(ip, type, buffer)) {
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
jfs_dm_write_invis_rvp(
	struct inode	*ip,
	dm_right_t	right,
	int		flags,
	dm_off_t	off,
	dm_size_t	len,
	void		*bufp,
	int		*rvp)
{
	int		fflag = 0;

	if (right < DM_RIGHT_EXCL)
		return(-EACCES);

	if ((off > MAXFILESIZE) || (len > MAXFILESIZE) || (off > MAXFILESIZE - len))
		return(-EFBIG);

	if (flags & DM_WRITE_SYNC)
		fflag |= O_SYNC;
	return(jfs_dm_rdwr(ip, fflag, FMODE_WRITE, off, len, bufp, rvp));
}


STATIC void
jfs_dm_obj_ref_hold(
	struct inode *ip)
{
	struct inode *inode;

	inode = igrab(ip);
	ASSERT(inode);
}


STATIC fsys_function_vector_t	jfs_fsys_vector[DM_FSYS_MAX];


int
jfs_dm_get_fsys_vector(
	struct inode	*ip,
	caddr_t		addr)
{
	static	int		initialized = 0;
	dm_fcntl_vector_t	*vecrq;
	fsys_function_vector_t	*vecp;
	int			i = 0;

	vecrq = (dm_fcntl_vector_t *)addr;
	vecrq->count =
		sizeof(jfs_fsys_vector) / sizeof(jfs_fsys_vector[0]);
	vecrq->vecp = jfs_fsys_vector;
	if (initialized)
		return(0);
	vecrq->code_level = DM_CLVL_XOPEN;
	vecp = jfs_fsys_vector;

	vecp[i].func_no = DM_FSYS_CLEAR_INHERIT;
	vecp[i++].u_fc.clear_inherit = jfs_dm_clear_inherit;
	vecp[i].func_no = DM_FSYS_CREATE_BY_HANDLE;
	vecp[i++].u_fc.create_by_handle = jfs_dm_create_by_handle;
	vecp[i].func_no = DM_FSYS_DOWNGRADE_RIGHT;
	vecp[i++].u_fc.downgrade_right = jfs_dm_downgrade_right;
	vecp[i].func_no = DM_FSYS_GET_ALLOCINFO_RVP;
	vecp[i++].u_fc.get_allocinfo_rvp = jfs_dm_get_allocinfo_rvp;
	vecp[i].func_no = DM_FSYS_GET_BULKALL_RVP;
	vecp[i++].u_fc.get_bulkall_rvp = jfs_dm_get_bulkall_rvp;
	vecp[i].func_no = DM_FSYS_GET_BULKATTR_RVP;
	vecp[i++].u_fc.get_bulkattr_rvp = jfs_dm_get_bulkattr_rvp;
	vecp[i].func_no = DM_FSYS_GET_CONFIG;
	vecp[i++].u_fc.get_config = jfs_dm_get_config;
	vecp[i].func_no = DM_FSYS_GET_CONFIG_EVENTS;
	vecp[i++].u_fc.get_config_events = jfs_dm_get_config_events;
	vecp[i].func_no = DM_FSYS_GET_DESTROY_DMATTR;
	vecp[i++].u_fc.get_destroy_dmattr = jfs_dm_get_destroy_dmattr;
	vecp[i].func_no = DM_FSYS_GET_DIRATTRS_RVP;
	vecp[i++].u_fc.get_dirattrs_rvp = jfs_dm_get_dirattrs_rvp;
	vecp[i].func_no = DM_FSYS_GET_DMATTR;
	vecp[i++].u_fc.get_dmattr = jfs_dm_get_dmattr;
	vecp[i].func_no = DM_FSYS_GET_EVENTLIST;
	vecp[i++].u_fc.get_eventlist = jfs_dm_get_eventlist;
	vecp[i].func_no = DM_FSYS_GET_FILEATTR;
	vecp[i++].u_fc.get_fileattr = jfs_dm_get_fileattr;
	vecp[i].func_no = DM_FSYS_GET_REGION;
	vecp[i++].u_fc.get_region = jfs_dm_get_region;
	vecp[i].func_no = DM_FSYS_GETALL_DMATTR;
	vecp[i++].u_fc.getall_dmattr = jfs_dm_getall_dmattr;
	vecp[i].func_no = DM_FSYS_GETALL_INHERIT;
	vecp[i++].u_fc.getall_inherit = jfs_dm_getall_inherit;
	vecp[i].func_no = DM_FSYS_INIT_ATTRLOC;
	vecp[i++].u_fc.init_attrloc = jfs_dm_init_attrloc;
	vecp[i].func_no = DM_FSYS_MKDIR_BY_HANDLE;
	vecp[i++].u_fc.mkdir_by_handle = jfs_dm_mkdir_by_handle;
	vecp[i].func_no = DM_FSYS_PROBE_HOLE;
	vecp[i++].u_fc.probe_hole = jfs_dm_probe_hole;
	vecp[i].func_no = DM_FSYS_PUNCH_HOLE;
	vecp[i++].u_fc.punch_hole = jfs_dm_punch_hole;
	vecp[i].func_no = DM_FSYS_READ_INVIS_RVP;
	vecp[i++].u_fc.read_invis_rvp = jfs_dm_read_invis_rvp;
	vecp[i].func_no = DM_FSYS_RELEASE_RIGHT;
	vecp[i++].u_fc.release_right = jfs_dm_release_right;
	vecp[i].func_no = DM_FSYS_REMOVE_DMATTR;
	vecp[i++].u_fc.remove_dmattr = jfs_dm_remove_dmattr;
	vecp[i].func_no = DM_FSYS_REQUEST_RIGHT;
	vecp[i++].u_fc.request_right = jfs_dm_request_right;
	vecp[i].func_no = DM_FSYS_SET_DMATTR;
	vecp[i++].u_fc.set_dmattr = jfs_dm_set_dmattr;
	vecp[i].func_no = DM_FSYS_SET_EVENTLIST;
	vecp[i++].u_fc.set_eventlist = jfs_dm_set_eventlist;
	vecp[i].func_no = DM_FSYS_SET_FILEATTR;
	vecp[i++].u_fc.set_fileattr = jfs_dm_set_fileattr;
	vecp[i].func_no = DM_FSYS_SET_INHERIT;
	vecp[i++].u_fc.set_inherit = jfs_dm_set_inherit;
	vecp[i].func_no = DM_FSYS_SET_REGION;
	vecp[i++].u_fc.set_region = jfs_dm_set_region;
	vecp[i].func_no = DM_FSYS_SYMLINK_BY_HANDLE;
	vecp[i++].u_fc.symlink_by_handle = jfs_dm_symlink_by_handle;
	vecp[i].func_no = DM_FSYS_SYNC_BY_HANDLE;
	vecp[i++].u_fc.sync_by_handle = jfs_dm_sync_by_handle;
	vecp[i].func_no = DM_FSYS_UPGRADE_RIGHT;
	vecp[i++].u_fc.upgrade_right = jfs_dm_upgrade_right;
	vecp[i].func_no = DM_FSYS_WRITE_INVIS_RVP;
	vecp[i++].u_fc.write_invis_rvp = jfs_dm_write_invis_rvp;
	vecp[i].func_no = DM_FSYS_OBJ_REF_HOLD;
	vecp[i++].u_fc.obj_ref_hold = jfs_dm_obj_ref_hold;

	return(0);
}


/*	jfs_dm_mapevent - send events needed for memory mapping a file.
 *
 *	DMAPI events are not being generated at a low enough level
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
jfs_dm_mapevent(
	struct inode 	*ip,
	int		flags,
	loff_t		offset,
	dm_fcntl_mapevent_t *mapevp)
{
	loff_t		filesize;		/* event read/write "size" */
	loff_t		end_of_area, evsize;
	struct jfs_sb_info *sbi = JFS_SBI(ip->i_sb);

	/* exit immediately if not regular file in a DMAPI file system */

	mapevp->error = 0;			/* assume success */

	if ((!S_ISREG(ip->i_mode)) || !(sbi->flag & JFS_DMI))
		return 0;

	if (mapevp->max_event != DM_EVENT_WRITE &&
		mapevp->max_event != DM_EVENT_READ)
			return 0;

	/* Set file size to work with. */

	filesize = ip->i_size;

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
		DM_EVENT_ENABLED (ip, DM_EVENT_WRITE)) {
		mapevp->error = JFS_SEND_DATA(DM_EVENT_WRITE, ip,
				offset, evsize, 0, NULL);
		return(0);
	}

	/* Try a read event if max_event was != DM_EVENT_WRITE or if it
	 * was DM_EVENT_WRITE but the WRITE event was not enabled.
	 */
	if (DM_EVENT_ENABLED (ip, DM_EVENT_READ)) {
		mapevp->error = JFS_SEND_DATA(DM_EVENT_READ, ip,
				offset, evsize, 0, NULL);
	}

	return 0;
}

int
jfs_dm_send_mmap_event(
	struct vm_area_struct *vma,
	unsigned int	wantflag)
{
	struct inode	*ip;
	int		ret = 0;
	struct jfs_sb_info *sbi;
	
	dm_fcntl_mapevent_t maprq;
	dm_eventtype_t	max_event = DM_EVENT_READ;

	if (!vma->vm_file)
		return 0;

	ip = vma->vm_file->f_dentry->d_inode;
	ASSERT(ip);

	sbi = JFS_SBI(ip->i_sb);

	if ((!S_ISREG(ip->i_mode)) || !(sbi->flag & JFS_DMI))
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
	maprq.length = vma->vm_end - vma->vm_start;		// XFS BUG #33

	if(DM_EVENT_ENABLED(ip, max_event)){
		jfs_dm_mapevent(ip, 0, vma->vm_pgoff << ip->i_blkbits, &maprq); // XFS BUG #33
		ret = maprq.error;
	}

	return ret;
}

void __init
jfs_dm_init(void)
{
}

void __exit
jfs_dm_exit(void)
{
}

/*
 * jfs_iget - called by DMAPI to get inode from file handle
 */
int
jfs_iget(
	struct super_block *sbp,
	struct inode	**ipp,
	fid_t		*fidp)
{
	jfs_fid_t	*jfid;
	struct inode 	*ip;
	u32		ino;
	unsigned int	igen;

	jfid  = (struct jfs_fid *)fidp;
	if (jfid->fid_len == 0) {
		ino  = ROOT_I;
		igen = 0;
	} else if (jfid->fid_len == sizeof(*jfid) - sizeof(jfid->fid_len)) {
		ino  = jfid->fid_ino;
		igen = jfid->fid_gen;
	} else {
		/*
		 * Invalid.  Since handles can be created in user space
		 * and passed in via gethandle(), this is not cause for
		 * a panic.
		 */
		return -EINVAL;
	}

	ip = iget(sbp, ino);
	if (!ip) {
		*ipp = NULL;
		return -EIO;
	}
	
	if (is_bad_inode(ip)) {
		iput(ip);
		*ipp = NULL;
		return -EIO;
	}

	if ((ip->i_mode & S_IFMT) == 0 || 
	    (igen && (ip->i_generation != igen)) ||
	    (ip->i_nlink == 0)) {
		iput(ip);
		*ipp = NULL;
		return -ENOENT;
	}

	*ipp = ip;
	return 0;
}


/*
 * jfs_dm_read_pers_data - called by JFS to get DMAPI persistent data from
 * extended data and copy into inode
 */
int
jfs_dm_read_pers_data(
	struct jfs_inode_info *jfs_ip)
{
#ifndef DM_SUPPORT_ONE_MANAGED_REGION	
	ssize_t	size;

	/* See if there are any managed regions */
	size = __jfs_getxattr(&jfs_ip->vfs_inode, DMATTR_PERS_REGIONS,
			NULL, 0);

	/* Initialize any managed regions */
	if (size > 0) {
		ssize_t bytes_read;

		if ((size % sizeof(dm_region_t)) != 0) {
			jfs_ip->dmnumrgns = 0;
			jfs_ip->dmrgns = NULL;
			return -EINVAL;
		}

		jfs_ip->dmnumrgns = size / sizeof(dm_region_t);
		
		jfs_ip->dmrgns = kmalloc(size, SLAB_KERNEL);
		if (jfs_ip->dmrgns == NULL) {
			printk("%s/%d: kmalloc returned NULL\n", __FUNCTION__, __LINE__);
			jfs_ip->dmnumrgns = 0;
			return -ENOMEM;
		}

		bytes_read = __jfs_getxattr(&jfs_ip->vfs_inode, DMATTR_PERS_REGIONS,
				jfs_ip->dmrgns, size);

		if (bytes_read != size) {
			jfs_ip->dmnumrgns = 0;
			kfree(jfs_ip->dmrgns);
			jfs_ip->dmrgns = NULL;
			return -ENOMEM;
		} else {
			int i;
			for (i = 0; i < jfs_ip->dmnumrgns; i++) {
				jfs_ip->dmattrs.da_dmevmask |= (jfs_ip->dmrgns[i].rg_flags << REGION_MASK_TO_EVENT_MASK);
			}
		}			
	} else {
		jfs_ip->dmnumrgns = 0;
		jfs_ip->dmrgns = NULL;
	}
#endif

	return 0;
}	

/*
 * jfs_dm_write_pers_data - called by JFS to get DMAPI persistent data from
 * inode and copy into extended data
 */
int
jfs_dm_write_pers_data(
	struct jfs_inode_info *jfs_ip)
{
	int	error = 0;

#ifndef DM_SUPPORT_ONE_MANAGED_REGION	
	/* Save or clear any managed regions */
	if (jfs_ip->dmnumrgns) {
		error = __jfs_setxattr(&jfs_ip->vfs_inode, DMATTR_PERS_REGIONS, 
				jfs_ip->dmrgns, 
				jfs_ip->dmnumrgns * sizeof(dm_region_t), 0);
	} else {
		error = __jfs_setxattr(&jfs_ip->vfs_inode, DMATTR_PERS_REGIONS, 
				0, 0, XATTR_REPLACE);
	}
#endif	

	return error;
}	

#if 0
/* This strategy behind this doesn't work because the name of the JFS mount
 * point is not yet in the namespace.
 */
struct vfsmount *
jfs_find_vfsmount(struct dentry *d)
{
	struct dentry *root = dget(d->d_sb->s_root);
	struct namespace *ns = current->namespace;
	struct list_head *head;
	struct vfsmount *mnt = NULL;

	down_read(&ns->sem);
	list_for_each(head, &ns->list) {
		mnt = list_entry(head, struct vfsmount, mnt_list);
		jfs_info("jfs_find_vfsmount: found %s\n", mnt->mnt_devname);
		if (mnt->mnt_root == root) {
			mntget(mnt);
			break;
		} else {
			mnt = NULL;
		}
	}
	up_read(&ns->sem);
	dput(root);
	return mnt;
}	
#endif

int
jfs_dm_mount(struct super_block *sb)
{
	int rc = 0;
	char *name = JFS_SBI(sb)->dm_mtpt;
	char b[BDEVNAME_SIZE];
	/* char *name = (char *)sb->s_root->d_name.name; */
	/* struct vfsmount *mnt = jfs_find_vfsmount(sb->s_root); */
	
	rc = dm_send_mount_event(sb, DM_RIGHT_NULL, NULL,
			DM_RIGHT_NULL, sb->s_root->d_inode, 
			DM_RIGHT_NULL, name, (char *)__bdevname(sb->s_dev, b));

	/* Needed because s_root is set to null before preunmount/unmount */
	if (!rc)
		JFS_SBI(sb)->dm_root = sb->s_root->d_inode;

	return rc;
}		

int
jfs_dm_preunmount(struct super_block *sb)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	
	return JFS_SEND_NAMESP(DM_EVENT_PREUNMOUNT, sbi->dm_root, 
			DM_RIGHT_NULL, sbi->dm_root, DM_RIGHT_NULL,
			NULL, NULL, 0, 0, 
			((sbi->dm_evmask & (1<<DM_EVENT_PREUNMOUNT)) ?
				0 : DM_FLAGS_UNWANTED) |
			((sbi->mntflag & JFS_UNMOUNT_FORCE) ?
			 	DM_UNMOUNT_FORCE : 0));
}		

void
jfs_dm_unmount(struct super_block *sb, int rc)
{
	struct jfs_sb_info *sbi = JFS_SBI(sb);

	JFS_SEND_UNMOUNT(sb, rc == 0 ? sbi->dm_root : NULL, 
			DM_RIGHT_NULL, 0, rc,
			((sbi->dm_evmask & (1<<DM_EVENT_UNMOUNT)) ?
				0 : DM_FLAGS_UNWANTED) |
			((sbi->mntflag & JFS_UNMOUNT_FORCE) ?
			 	DM_UNMOUNT_FORCE : 0));
}		

void
jfs_dm_umount_begin(struct super_block *sb)
{
	/* just remember that this is a forced unmount */
	if (JFS_SBI(sb)->flag & JFS_DMI) {
		JFS_SBI(sb)->mntflag |= JFS_UNMOUNT_FORCE;

		jfs_dm_preunmount(sb);
	}
}
