/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
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

#include <xfs.h>


/*
 * This is a subroutine for xfs_write() and other writers (xfs_ioctl)
 * which clears the setuid and setgid bits when a file is written.
 */
int
xfs_write_clear_setuid(
	xfs_inode_t	*ip)
{
	xfs_mount_t	*mp;
	xfs_trans_t	*tp;
	int		error;

	mp = ip->i_mount;
	tp = xfs_trans_alloc(mp, XFS_TRANS_WRITEID);
	if ((error = xfs_trans_reserve(tp, 0,
				      XFS_WRITEID_LOG_RES(mp),
				      0, 0, 0))) {
		xfs_trans_cancel(tp, 0);
		return error;
	}
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_ihold(tp, ip);
	ip->i_d.di_mode &= ~ISUID;

	/*
	 * Note that we don't have to worry about mandatory
	 * file locking being disabled here because we only
	 * clear the ISGID bit if the Group execute bit is
	 * on, but if it was on then mandatory locking wouldn't
	 * have been enabled.
	 */
	if (ip->i_d.di_mode & (IEXEC >> 3)) {
		ip->i_d.di_mode &= ~ISGID;
	}
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	xfs_trans_set_sync(tp);
	error = xfs_trans_commit(tp, 0, NULL);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;
}

/*
 * Force a shutdown of the filesystem instantly while keeping
 * the filesystem consistent. We don't do an unmount here; just shutdown
 * the shop, make sure that absolutely nothing persistent happens to
 * this filesystem after this point.
 */

void
xfs_do_force_shutdown(
	bhv_desc_t	*bdp,
	int		flags,
	char		*fname,
	int		lnnum)
{
	int		logerror;
	xfs_mount_t	*mp;

	mp = XFS_BHVTOM(bdp);
	logerror = flags & XFS_LOG_IO_ERROR;

	if (!(flags & XFS_FORCE_UMOUNT)) {
		cmn_err(CE_NOTE,
		"xfs_force_shutdown(%s,0x%x) called from line %d of file %s.  Return address = 0x%x",
			mp->m_fsname,flags,lnnum,fname,__return_address);
	}
	/*
	 * No need to duplicate efforts.
	 */
	if (XFS_FORCED_SHUTDOWN(mp) && !logerror)
		return;

	/*
	 * This flags XFS_MOUNT_FS_SHUTDOWN, makes sure that we don't
	 * queue up anybody new on the log reservations, and wakes up
	 * everybody who's sleeping on log reservations and tells
	 * them the bad news.
	 */
	if (xfs_log_force_umount(mp, logerror))
		return;

	if (flags & XFS_CORRUPT_INCORE) {
		cmn_err(CE_ALERT,
    "Corruption of in-memory data detected.  Shutting down filesystem: %s",
			mp->m_fsname);
	} else if (!(flags & XFS_FORCE_UMOUNT)) {
		if (logerror) {
			cmn_err(CE_ALERT,
			"Log I/O Error Detected.  Shutting down filesystem: %s",
				mp->m_fsname);
		} else if (!(flags & XFS_SHUTDOWN_REMOTE_REQ)) {
			cmn_err(CE_ALERT,
				"I/O Error Detected.  Shutting down filesystem: %s",
				mp->m_fsname);
		}
	}
	if (!(flags & XFS_FORCE_UMOUNT)) {
		cmn_err(CE_ALERT,
		"Please umount the filesystem, and rectify the problem(s)");
	}
}


/*
 * Called when we want to stop a buffer from getting written or read.
 * We attach the EIO error, muck with its flags, and call biodone
 * so that the proper iodone callbacks get called.
 */
int
xfs_bioerror(
	xfs_buf_t *bp)
{

#ifdef XFSERRORDEBUG
	ASSERT(XFS_BUF_ISREAD(bp) || bp->b_iodone);
#endif

	/*
	 * No need to wait until the buffer is unpinned.
	 * We aren't flushing it.
	 */
	xfs_buftrace("XFS IOERROR", bp);
	XFS_BUF_ERROR(bp, EIO);
	/*
	 * We're calling biodone, so delete B_DONE flag. Either way
	 * we have to call the iodone callback, and calling biodone
	 * probably is the best way since it takes care of
	 * GRIO as well.
	 */
	XFS_BUF_UNREAD(bp);
	XFS_BUF_UNDELAYWRITE(bp);
	XFS_BUF_UNDONE(bp);
	XFS_BUF_STALE(bp);

	XFS_BUF_CLR_BDSTRAT_FUNC(bp);
	xfs_biodone(bp);

	return (EIO);
}

/*
 * Same as xfs_bioerror, except that we are releasing the buffer
 * here ourselves, and avoiding the biodone call.
 * This is meant for userdata errors; metadata bufs come with
 * iodone functions attached, so that we can track down errors.
 */
int
xfs_bioerror_relse(
	xfs_buf_t *bp)
{
	int64_t fl;

	ASSERT(XFS_BUF_IODONE_FUNC(bp) != xfs_buf_iodone_callbacks);
	ASSERT(XFS_BUF_IODONE_FUNC(bp) != xlog_iodone);

	xfs_buftrace("XFS IOERRELSE", bp);
	fl = XFS_BUF_BFLAGS(bp);
	/*
	 * No need to wait until the buffer is unpinned.
	 * We aren't flushing it.
	 *
	 * chunkhold expects B_DONE to be set, whether
	 * we actually finish the I/O or not. We don't want to
	 * change that interface.
	 */
	XFS_BUF_UNREAD(bp);
	XFS_BUF_UNDELAYWRITE(bp);
	XFS_BUF_DONE(bp);
	XFS_BUF_STALE(bp);
	XFS_BUF_CLR_IODONE_FUNC(bp);
	XFS_BUF_CLR_BDSTRAT_FUNC(bp);
	if (!(fl & XFS_B_ASYNC)) {
		/*
		 * Mark b_error and B_ERROR _both_.
		 * Lot's of chunkcache code assumes that.
		 * There's no reason to mark error for
		 * ASYNC buffers.
		 */
		XFS_BUF_ERROR(bp, EIO);
		XFS_BUF_V_IODONESEMA(bp);
	} else {
		xfs_buf_relse(bp);
	}
	return (EIO);
}
/*
 * Prints out an ALERT message about I/O error.
 */
void
xfs_ioerror_alert(
	char			*func,
	struct xfs_mount	*mp,
	xfs_buf_t		*bp,
	xfs_daddr_t		blkno)
{
	cmn_err(CE_ALERT,
 "I/O error in filesystem (\"%s\") meta-data dev 0x%x block 0x%llx\n"
 "	 (\"%s\") error %d buf count %u",
		(!mp || !mp->m_fsname) ? "(fs name not set)" : mp->m_fsname,
		XFS_BUF_TARGET_DEV(bp),
		(__uint64_t)blkno,
		func,
		XFS_BUF_GETERROR(bp),
		XFS_BUF_COUNT(bp));
}

/*
 * This isn't an absolute requirement, but it is
 * just a good idea to call xfs_read_buf instead of
 * directly doing a read_buf call. For one, we shouldn't
 * be doing this disk read if we are in SHUTDOWN state anyway,
 * so this stops that from happening. Secondly, this does all
 * the error checking stuff and the brelse if appropriate for
 * the caller, so the code can be a little leaner.
 */

int
xfs_read_buf(
	struct xfs_mount *mp,
	xfs_buftarg_t	 *target,
	xfs_daddr_t	 blkno,
	int		 len,
	uint		 flags,
	xfs_buf_t	 **bpp)
{
	xfs_buf_t	 *bp;
	int		 error;

	if (flags)
		bp = xfs_buf_read_flags(target, blkno, len, flags);
	else
		bp = xfs_buf_read(target, blkno, len, flags);
	if (!bp)
		return XFS_ERROR(EIO);
	error = XFS_BUF_GETERROR(bp);
	if (bp && !error && !XFS_FORCED_SHUTDOWN(mp)) {
		*bpp = bp;
	} else {
		*bpp = NULL;
		if (error) {
			xfs_ioerror_alert("xfs_read_buf", mp, bp, XFS_BUF_ADDR(bp));
		} else {
			error = XFS_ERROR(EIO);
		}
		if (bp) {
			XFS_BUF_UNDONE(bp);
			XFS_BUF_UNDELAYWRITE(bp);
			XFS_BUF_STALE(bp);
			/*
			 * brelse clears B_ERROR and b_error
			 */
			xfs_buf_relse(bp);
		}
	}
	return (error);
}

/*
 * Wrapper around bwrite() so that we can trap
 * write errors, and act accordingly.
 */
int
xfs_bwrite(
	struct xfs_mount *mp,
	struct xfs_buf	 *bp)
{
	int	error;

	/*
	 * XXXsup how does this work for quotas.
	 */
	XFS_BUF_SET_BDSTRAT_FUNC(bp, xfs_bdstrat_cb);
	XFS_BUF_SET_FSPRIVATE3(bp, mp);
	XFS_BUF_WRITE(bp);

	if ((error = XFS_bwrite(bp))) {
		ASSERT(mp);
		/*
		 * Cannot put a buftrace here since if the buffer is not
		 * B_HOLD then we will brelse() the buffer before returning
		 * from bwrite and we could be tracing a buffer that has
		 * been reused.
		 */
		xfs_force_shutdown(mp, XFS_METADATA_IO_ERROR);
	}
	return (error);
}

/*
 * xfs_inval_cached_pages()
 * This routine is responsible for keeping direct I/O and buffered I/O
 * somewhat coherent.  From here we make sure that we're at least
 * temporarily holding the inode I/O lock exclusively and then call
 * the page cache to flush and invalidate any cached pages.  If there
 * are no cached pages this routine will be very quick.
 */
void
xfs_inval_cached_pages(
	vnode_t		*vp,
	xfs_iocore_t	*io,
	xfs_off_t	offset,
	int		write,
	int		relock)
{
	xfs_mount_t	*mp;

	if (!VN_CACHED(vp)) {
		return;
	}

	mp = io->io_mount;

	/*
	 * We need to get the I/O lock exclusively in order
	 * to safely invalidate pages and mappings.
	 */
	if (relock) {
		XFS_IUNLOCK(mp, io, XFS_IOLOCK_SHARED);
		XFS_ILOCK(mp, io, XFS_IOLOCK_EXCL);
	}

	/* Writing beyond EOF creates a hole that must be zeroed */
	if (write && (offset > XFS_SIZE(mp, io))) {
		xfs_fsize_t	isize;

		XFS_ILOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
		isize = XFS_SIZE(mp, io);
		if (offset > isize) {
			xfs_zero_eof(vp, io, offset, isize, offset, NULL);
		}
		XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_RD);
	}

	VOP_FLUSHINVAL_PAGES(vp, ctooff(offtoct(offset)), -1, FI_REMAPF_LOCKED);
	if (relock) {
		XFS_ILOCK_DEMOTE(mp, io, XFS_IOLOCK_EXCL);
	}
}



spinlock_t	xfs_refcache_lock = SPIN_LOCK_UNLOCKED;
xfs_inode_t	**xfs_refcache;
int		xfs_refcache_size;
int		xfs_refcache_index;
int		xfs_refcache_busy;
int		xfs_refcache_count;

/*
 * Timer callback to mark SB dirty, make sure we keep purging refcache
 */
void
xfs_refcache_sbdirty(struct super_block *sb)
{
	sb->s_dirt = 1;
}

/*
 * Insert the given inode into the reference cache.
 */
void
xfs_refcache_insert(
	xfs_inode_t	*ip)
{
	vnode_t		*vp;
	xfs_inode_t	*release_ip;
	xfs_inode_t	**refcache;

	ASSERT(ismrlocked(&(ip->i_iolock), MR_UPDATE));

	/*
	 * If an unmount is busy blowing entries out of the cache,
	 * then don't bother.
	 */
	if (xfs_refcache_busy) {
		return;
	}

	/*
	 * If we tuned the refcache down to zero, don't do anything.
	 */
	 if (!xfs_refcache_size) {
		return;
	}

	/*
	 * The inode is already in the refcache, so don't bother
	 * with it.
	 */
	if (ip->i_refcache != NULL) {
		return;
	}

	vp = XFS_ITOV(ip);
	/* ASSERT(vp->v_count > 0); */
	VN_HOLD(vp);

	/*
	 * We allocate the reference cache on use so that we don't
	 * waste the memory on systems not being used as NFS servers.
	 */
	if (xfs_refcache == NULL) {
		refcache = (xfs_inode_t **)kmem_zalloc(XFS_REFCACHE_SIZE_MAX *
						       sizeof(xfs_inode_t *),
						       KM_SLEEP);
	} else {
		refcache = NULL;
	}

	spin_lock(&xfs_refcache_lock);

	/*
	 * If we allocated memory for the refcache above and it still
	 * needs it, then use the memory we allocated.	Otherwise we'll
	 * free the memory below.
	 */
	if (refcache != NULL) {
		if (xfs_refcache == NULL) {
			xfs_refcache = refcache;
			refcache = NULL;
		}
	}

	/*
	 * If an unmount is busy clearing out the cache, don't add new
	 * entries to it.
	 */
	if (xfs_refcache_busy) {
		spin_unlock(&xfs_refcache_lock);
		VN_RELE(vp);
		/*
		 * If we allocated memory for the refcache above but someone
		 * else beat us to using it, then free the memory now.
		 */
		if (refcache != NULL) {
			kmem_free(refcache,
				  XFS_REFCACHE_SIZE_MAX * sizeof(xfs_inode_t *));
		}
		return;
	}
	release_ip = xfs_refcache[xfs_refcache_index];
	if (release_ip != NULL) {
		release_ip->i_refcache = NULL;
		xfs_refcache_count--;
		ASSERT(xfs_refcache_count >= 0);
	}
	xfs_refcache[xfs_refcache_index] = ip;
	ASSERT(ip->i_refcache == NULL);
	ip->i_refcache = &(xfs_refcache[xfs_refcache_index]);
	xfs_refcache_count++;
	ASSERT(xfs_refcache_count <= xfs_refcache_size);
	xfs_refcache_index++;
	if (xfs_refcache_index == xfs_refcache_size) {
		xfs_refcache_index = 0;
	}
	spin_unlock(&xfs_refcache_lock);

	/*
	 * Save the pointer to the inode to be released so that we can
	 * VN_RELE it once we've dropped our inode locks in xfs_rwunlock().
	 * The pointer may be NULL, but that's OK.
	 */
	ip->i_release = release_ip;

	/*
	 * If we allocated memory for the refcache above but someone
	 * else beat us to using it, then free the memory now.
	 */
	if (refcache != NULL) {
		kmem_free(refcache,
			  XFS_REFCACHE_SIZE_MAX * sizeof(xfs_inode_t *));
	}
	return;
}


/*
 * If the given inode is in the reference cache, purge its entry and
 * release the reference on the vnode.
 */
void
xfs_refcache_purge_ip(
	xfs_inode_t	*ip)
{
	vnode_t *vp;
	int	error;

	/*
	 * If we're not pointing to our entry in the cache, then
	 * we must not be in the cache.
	 */
	if (ip->i_refcache == NULL) {
		return;
	}

	spin_lock(&xfs_refcache_lock);
	if (ip->i_refcache == NULL) {
		spin_unlock(&xfs_refcache_lock);
		return;
	}

	/*
	 * Clear both our pointer to the cache entry and its pointer
	 * back to us.
	 */
	ASSERT(*(ip->i_refcache) == ip);
	*(ip->i_refcache) = NULL;
	ip->i_refcache = NULL;
	xfs_refcache_count--;
	ASSERT(xfs_refcache_count >= 0);
	spin_unlock(&xfs_refcache_lock);

	vp = XFS_ITOV(ip);
	/* ASSERT(vp->v_count > 1); */
	VOP_RELEASE(vp, error);
	VN_RELE(vp);

	return;
}


/*
 * This is called from the XFS unmount code to purge all entries for the
 * given mount from the cache.	It uses the refcache busy counter to
 * make sure that new entries are not added to the cache as we purge them.
 */
void
xfs_refcache_purge_mp(
	xfs_mount_t	*mp)
{
	vnode_t		*vp;
	int		error, i;
	xfs_inode_t	*ip;

	if (xfs_refcache == NULL) {
		return;
	}

	spin_lock(&xfs_refcache_lock);
	/*
	 * Bumping the busy counter keeps new entries from being added
	 * to the cache.  We use a counter since multiple unmounts could
	 * be in here simultaneously.
	 */
	xfs_refcache_busy++;

	for (i = 0; i < xfs_refcache_size; i++) {
		ip = xfs_refcache[i];
		if ((ip != NULL) && (ip->i_mount == mp)) {
			xfs_refcache[i] = NULL;
			ip->i_refcache = NULL;
			xfs_refcache_count--;
			ASSERT(xfs_refcache_count >= 0);
			spin_unlock(&xfs_refcache_lock);
			vp = XFS_ITOV(ip);
			VOP_RELEASE(vp, error);
			VN_RELE(vp);
			spin_lock(&xfs_refcache_lock);
		}
	}

	xfs_refcache_busy--;
	ASSERT(xfs_refcache_busy >= 0);
	spin_unlock(&xfs_refcache_lock);
}


/*
 * This is called from the XFS sync code to ensure that the refcache
 * is emptied out over time.  We purge a small number of entries with
 * each call.
 */
void
xfs_refcache_purge_some(xfs_mount_t *mp)
{
	int		error, i;
	xfs_inode_t	*ip;
	int		iplist_index;
	xfs_inode_t	**iplist;
	int		purge_count;

	if ((xfs_refcache == NULL) || (xfs_refcache_count == 0)) {
		return;
	}

	iplist_index = 0;
	purge_count = xfs_params.refcache_purge;
	iplist = (xfs_inode_t **)kmem_zalloc(purge_count *
					  sizeof(xfs_inode_t *), KM_SLEEP);

	spin_lock(&xfs_refcache_lock);

	/*
	 * Store any inodes we find in the next several entries
	 * into the iplist array to be released after dropping
	 * the spinlock.  We always start looking from the currently
	 * oldest place in the cache.  We move the refcache index
	 * forward as we go so that we are sure to eventually clear
	 * out the entire cache when the system goes idle.
	 */
	for (i = 0; i < purge_count; i++) {
		ip = xfs_refcache[xfs_refcache_index];
		if (ip != NULL) {
			xfs_refcache[xfs_refcache_index] = NULL;
			ip->i_refcache = NULL;
			xfs_refcache_count--;
			ASSERT(xfs_refcache_count >= 0);
			iplist[iplist_index] = ip;
			iplist_index++;
		}
		xfs_refcache_index++;
		if (xfs_refcache_index == xfs_refcache_size) {
			xfs_refcache_index = 0;
		}
	}

	spin_unlock(&xfs_refcache_lock);

	/*
	 * If there are still entries in the refcache,
	 * set timer to mark the SB dirty to make sure that
	 * we hit sync even if filesystem is idle, so that we'll
	 * purge some more later.
	 */
	if (xfs_refcache_count) {
		del_timer_sync(&mp->m_sbdirty_timer);
		mp->m_sbdirty_timer.data =
		    (unsigned long)LINVFS_GET_IP(XFS_ITOV(mp->m_rootip))->i_sb;
		mp->m_sbdirty_timer.expires = jiffies + 2*HZ;
		add_timer(&mp->m_sbdirty_timer);
	}

	/*
	 * Now drop the inodes we collected.
	 */
	for (i = 0; i < iplist_index; i++) {
		VOP_RELEASE(XFS_ITOV(iplist[i]), error);
		VN_RELE(XFS_ITOV(iplist[i]));
	}

	kmem_free(iplist, purge_count *
			  sizeof(xfs_inode_t *));
}

/*
 * This is called when the refcache is dynamically resized
 * via a sysctl.
 *
 * If the new size is smaller than the old size, purge all
 * entries in slots greater than the new size, and move
 * the index if necessary.
 *
 * If the refcache hasn't even been allocated yet, or the
 * new size is larger than the old size, just set the value
 * of xfs_refcache_size.
 */

void
xfs_refcache_resize(int xfs_refcache_new_size)
{
	int		i;
	xfs_inode_t	*ip;
	int		iplist_index = 0;
	xfs_inode_t	**iplist;
	int		error;

	/*
	 * If the new size is smaller than the current size,
	 * purge entries to create smaller cache, and
	 * reposition index if necessary.
	 * Don't bother if no refcache yet.
	 */
	if (xfs_refcache && (xfs_refcache_new_size < xfs_refcache_size)) {

		iplist = (xfs_inode_t **)kmem_zalloc(XFS_REFCACHE_SIZE_MAX *
				sizeof(xfs_inode_t *), KM_SLEEP);

		spin_lock(&xfs_refcache_lock);

		for (i = xfs_refcache_new_size; i < xfs_refcache_size; i++) {
			ip = xfs_refcache[i];
			if (ip != NULL) {
				xfs_refcache[i] = NULL;
				ip->i_refcache = NULL;
				xfs_refcache_count--;
				ASSERT(xfs_refcache_count >= 0);
				iplist[iplist_index] = ip;
				iplist_index++;
			}
		}

		xfs_refcache_size = xfs_refcache_new_size;

		/*
		 * Move index to beginning of cache if it's now past the end
		 */
		if (xfs_refcache_index >= xfs_refcache_new_size)
			xfs_refcache_index = 0;

		spin_unlock(&xfs_refcache_lock);

		/*
		 * Now drop the inodes we collected.
		 */
		for (i = 0; i < iplist_index; i++) {
			VOP_RELEASE(XFS_ITOV(iplist[i]), error);
			VN_RELE(XFS_ITOV(iplist[i]));
		}

		kmem_free(iplist, XFS_REFCACHE_SIZE_MAX *
				  sizeof(xfs_inode_t *));
	} else {
		spin_lock(&xfs_refcache_lock);
		xfs_refcache_size = xfs_refcache_new_size;
		spin_unlock(&xfs_refcache_lock);
	}
}
