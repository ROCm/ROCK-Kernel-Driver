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
/*
 *  fs/xfs/linux/xfs_lrw.c (Linux Read Write stuff)
 *
 */

#include <xfs.h>
#include <linux/pagemap.h>
#include <linux/capability.h>


#define XFS_WRITEIO_ALIGN(mp,off)	(((off) >> mp->m_writeio_log) \
						<< mp->m_writeio_log)
#define XFS_STRAT_WRITE_IMAPS	2

STATIC int xfs_iomap_read(xfs_iocore_t *, loff_t, size_t, int, page_buf_bmap_t *,
			int *);
STATIC int xfs_iomap_write(xfs_iocore_t *, loff_t, size_t, page_buf_bmap_t *,
			int *, int);
STATIC int xfs_iomap_write_delay(xfs_iocore_t *, loff_t, size_t, page_buf_bmap_t *,
			int *, int, int);
STATIC int xfs_iomap_write_direct(xfs_iocore_t *, loff_t, size_t, page_buf_bmap_t *,
			int *, int, int);
STATIC int _xfs_imap_to_bmap(xfs_iocore_t *, xfs_off_t, xfs_bmbt_irec_t *,
			page_buf_bmap_t *, int, int);


int
xfs_strategy(xfs_inode_t *ip,
	xfs_off_t	offset,
	ssize_t		count,
	int		flags,
	page_buf_bmap_t	*pbmapp,
	int		*npbmaps)
{
	xfs_iocore_t	*io;
	xfs_mount_t	*mp;
	int		error;
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	end_fsb;
	xfs_fileoff_t	map_start_fsb;
	xfs_fileoff_t	last_block;
	xfs_fsblock_t	first_block;
	xfs_bmap_free_t free_list;
	xfs_filblks_t	count_fsb;
	int		committed, i, loops, nimaps;
	int		is_xfs = 1; /* This will be a variable at some point */
	xfs_bmbt_irec_t imap[XFS_MAX_RW_NBMAPS];
	xfs_trans_t	*tp;

	io = &ip->i_iocore;
	mp = ip->i_mount;
	/* is_xfs = IO_IS_XFS(io); */
	ASSERT((ip->i_d.di_mode & IFMT) == IFREG);
	ASSERT(((ip->i_d.di_flags & XFS_DIFLAG_REALTIME) != 0) ==
	       ((io->io_flags & XFS_IOCORE_RT) != 0));

	if (XFS_FORCED_SHUTDOWN(mp))
		return XFS_ERROR(EIO);

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	nimaps = min(XFS_MAX_RW_NBMAPS, *npbmaps);
	end_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	first_block = NULLFSBLOCK;

	XFS_ILOCK(mp, io, XFS_ILOCK_SHARED | XFS_EXTSIZE_RD);
	error = XFS_BMAPI(mp, NULL, io, offset_fsb,
			(xfs_filblks_t)(end_fsb - offset_fsb),
			XFS_BMAPI_ENTIRE, &first_block, 0, imap,
			&nimaps, NULL);
	XFS_IUNLOCK(mp, io, XFS_ILOCK_SHARED | XFS_EXTSIZE_RD);
	if (error) {
		return XFS_ERROR(error);
	}

	if (nimaps && !ISNULLSTARTBLOCK(imap[0].br_startblock)) {
		*npbmaps = _xfs_imap_to_bmap(&ip->i_iocore, offset, imap,
				pbmapp, nimaps, *npbmaps);
		return 0;
	}

	/*
	 * Make sure that the dquots are there.
	 */

	if (XFS_IS_QUOTA_ON(mp)) {
		if (XFS_NOT_DQATTACHED(mp, ip)) {
			if ((error = xfs_qm_dqattach(ip, 0))) {
				return XFS_ERROR(error);
			}
		}
	}
	XFS_STATS_ADD(xfsstats.xs_xstrat_bytes,
		XFS_FSB_TO_B(mp, imap[0].br_blockcount));

	offset_fsb = imap[0].br_startoff;
	count_fsb = imap[0].br_blockcount;
	map_start_fsb = offset_fsb;
	while (count_fsb != 0) {
		/*
		 * Set up a transaction with which to allocate the
		 * backing store for the file.	Do allocations in a
		 * loop until we get some space in the range we are
		 * interested in.  The other space that might be allocated
		 * is in the delayed allocation extent on which we sit
		 * but before our buffer starts.
		 */
		nimaps = 0;
		loops = 0;
		while (nimaps == 0) {
			if (is_xfs) {
				tp = xfs_trans_alloc(mp, XFS_TRANS_STRAT_WRITE);
				error = xfs_trans_reserve(tp, 0,
						XFS_WRITE_LOG_RES(mp),
						0, XFS_TRANS_PERM_LOG_RES,
						XFS_WRITE_LOG_COUNT);
				if (error) {
					xfs_trans_cancel(tp, 0);
					goto error0;
				}
				xfs_ilock(ip, XFS_ILOCK_EXCL);
				xfs_trans_ijoin(tp, ip,
						XFS_ILOCK_EXCL);
				xfs_trans_ihold(tp, ip);
			} else {
				tp = NULL;
				XFS_ILOCK(mp, io, XFS_ILOCK_EXCL |
						XFS_EXTSIZE_WR);
			}


			/*
			 * Allocate the backing store for the file.
			 */
			XFS_BMAP_INIT(&(free_list),
					&(first_block));
			nimaps = XFS_STRAT_WRITE_IMAPS;

			/*
			 * Ensure we don't go beyond eof - it is possible
			 * the extents changed since we did the read call,
			 * we dropped the ilock in the interim.
			 */

			end_fsb = XFS_B_TO_FSB(mp, XFS_SIZE(mp, io));
			xfs_bmap_last_offset(NULL, ip, &last_block,
				XFS_DATA_FORK);
			last_block = XFS_FILEOFF_MAX(last_block, end_fsb);
			if ((map_start_fsb + count_fsb) > last_block) {
				count_fsb = last_block - map_start_fsb;
				if (count_fsb == 0) {
					if (is_xfs) {
						xfs_bmap_cancel(&free_list);
						xfs_trans_cancel(tp,
						  (XFS_TRANS_RELEASE_LOG_RES |
							 XFS_TRANS_ABORT));
					}
					XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL |
							    XFS_EXTSIZE_WR);
					return XFS_ERROR(EAGAIN);
				}
			}

			error = XFS_BMAPI(mp, tp, io, map_start_fsb, count_fsb,
					XFS_BMAPI_WRITE, &first_block, 1,
					imap, &nimaps, &free_list);
			if (error) {
				xfs_bmap_cancel(&free_list);
				xfs_trans_cancel(tp,
					(XFS_TRANS_RELEASE_LOG_RES |
					 XFS_TRANS_ABORT));
				XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL |
						    XFS_EXTSIZE_WR);

				goto error0;
			}

			if (is_xfs) {
				error = xfs_bmap_finish(&(tp), &(free_list),
						first_block, &committed);
				if (error) {
					xfs_bmap_cancel(&free_list);
					xfs_trans_cancel(tp,
						(XFS_TRANS_RELEASE_LOG_RES |
						XFS_TRANS_ABORT));
					xfs_iunlock(ip, XFS_ILOCK_EXCL);
					goto error0;
				}

				error = xfs_trans_commit(tp,
						XFS_TRANS_RELEASE_LOG_RES,
						NULL);
				if (error) {
					xfs_iunlock(ip, XFS_ILOCK_EXCL);
					goto error0;
				}
			}

			if (nimaps == 0) {
				XFS_IUNLOCK(mp, io,
						XFS_ILOCK_EXCL|XFS_EXTSIZE_WR);
			} /* else hold 'till we maybe loop again below */
		}

		/*
		 * See if we were able to allocate an extent that
		 * covers at least part of the user's requested size.
		 */

		offset_fsb = XFS_B_TO_FSBT(mp, offset);
		for(i = 0; i < nimaps; i++) {
			int maps;
			if (offset_fsb >= imap[i].br_startoff &&
				(offset_fsb < (imap[i].br_startoff + imap[i].br_blockcount))) {
				XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL | XFS_EXTSIZE_WR);
				maps = min(nimaps, *npbmaps);
				*npbmaps = _xfs_imap_to_bmap(io, offset, &imap[i],
					pbmapp, maps, *npbmaps);
				XFS_STATS_INC(xfsstats.xs_xstrat_quick);
				return 0;
			}
			count_fsb -= imap[i].br_blockcount; /* for next bmapi,
								if needed. */
		}

		/*
		 * We didn't get an extent the caller can write into so
		 * loop around and try starting after the last imap we got back.
		 */

		nimaps--; /* Index of last entry  */
		ASSERT(nimaps >= 0);
		ASSERT(offset_fsb >= imap[nimaps].br_startoff + imap[nimaps].br_blockcount);
		ASSERT(count_fsb);
		offset_fsb = imap[nimaps].br_startoff + imap[nimaps].br_blockcount;
		map_start_fsb = offset_fsb;
		XFS_STATS_INC(xfsstats.xs_xstrat_split);
		XFS_IUNLOCK(mp, io, XFS_ILOCK_EXCL|XFS_EXTSIZE_WR);
	}

	ASSERT(0);	/* Should never get here */

 error0:
	if (error) {
		ASSERT(count_fsb != 0);
		ASSERT(is_xfs || XFS_FORCED_SHUTDOWN(mp));
	}

	return XFS_ERROR(error);
}


/*
 * xfs_bmap() is the same as the irix xfs_bmap from xfs_rw.c
 * execpt for slight changes to the params
 */
int
xfs_bmap(bhv_desc_t	*bdp,
	xfs_off_t	offset,
	ssize_t		count,
	int		flags,
	page_buf_bmap_t	*pbmapp,
	int		*npbmaps)
{
	xfs_inode_t	*ip;
	int		error;
	int		lockmode;
	int		fsynced = 0;
	vnode_t		*vp;

	ip = XFS_BHVTOI(bdp);
	ASSERT((ip->i_d.di_mode & IFMT) == IFREG);
	ASSERT(((ip->i_d.di_flags & XFS_DIFLAG_REALTIME) != 0) ==
	       ((ip->i_iocore.io_flags & XFS_IOCORE_RT) != 0));

	if (XFS_FORCED_SHUTDOWN(ip->i_iocore.io_mount))
		return XFS_ERROR(EIO);

	if (flags & PBF_READ) {
		lockmode = xfs_ilock_map_shared(ip);
		error = xfs_iomap_read(&ip->i_iocore, offset, count,
				 XFS_BMAPI_ENTIRE, pbmapp, npbmaps);
		xfs_iunlock_map_shared(ip, lockmode);
	} else if (flags & PBF_FILE_ALLOCATE) {
		error = xfs_strategy(ip, offset, count, flags,
				pbmapp, npbmaps);
	} else { /* PBF_WRITE */
		ASSERT(flags & PBF_WRITE);
		vp = BHV_TO_VNODE(bdp);
		xfs_ilock(ip, XFS_ILOCK_EXCL);

		/*
		 * Make sure that the dquots are there. This doesn't hold
		 * the ilock across a disk read.
		 */

		if (XFS_IS_QUOTA_ON(ip->i_mount)) {
			if (XFS_NOT_DQATTACHED(ip->i_mount, ip)) {
				if ((error = xfs_qm_dqattach(ip, XFS_QMOPT_ILOCKED))) {
					xfs_iunlock(ip, XFS_ILOCK_EXCL);
					return XFS_ERROR(error);
				}
			}
		}
retry:
		error = xfs_iomap_write(&ip->i_iocore, offset, count,
					pbmapp, npbmaps, flags);
		/* xfs_iomap_write unlocks/locks/unlocks */

		if (error == ENOSPC) {
			switch (fsynced) {
			case 0:
				if (ip->i_delayed_blks) {
					filemap_fdatawrite(LINVFS_GET_IP(vp)->i_mapping);
					fsynced = 1;
				} else {
					fsynced = 2;
					flags |= PBF_SYNC;
				}
				error = 0;
				xfs_ilock(ip, XFS_ILOCK_EXCL);
				goto retry;
			case 1:
				fsynced = 2;
				if (!(flags & PBF_SYNC)) {
					flags |= PBF_SYNC;
					error = 0;
					xfs_ilock(ip, XFS_ILOCK_EXCL);
					goto retry;
				}
			case 2:
				sync_blockdev(vp->v_vfsp->vfs_super->s_bdev);
				xfs_log_force(ip->i_mount, (xfs_lsn_t)0,
						XFS_LOG_FORCE|XFS_LOG_SYNC);

				error = 0;
/**
				delay(HZ);
**/
				fsynced++;
				xfs_ilock(ip, XFS_ILOCK_EXCL);
				goto retry;
			}
		}
	}

	return XFS_ERROR(error);
}


STATIC int
_xfs_imap_to_bmap(
	xfs_iocore_t	*io,
	xfs_off_t	offset,
	xfs_bmbt_irec_t *imap,
	page_buf_bmap_t	*pbmapp,
	int		imaps,			/* Number of imap entries */
	int		pbmaps)			/* Number of pbmap entries */
{
	xfs_mount_t	*mp;
	xfs_fsize_t	nisize;
	int		im, pbm;
	xfs_fsblock_t	start_block;

	mp = io->io_mount;
	nisize = XFS_SIZE(mp, io);
	if (io->io_new_size > nisize)
		nisize = io->io_new_size;

	for (im=0, pbm=0; im < imaps && pbm < pbmaps; im++,pbmapp++,imap++,pbm++) {
		pbmapp->pbm_target = io->io_flags & XFS_IOCORE_RT ?
			mp->m_rtdev_targp :
			mp->m_ddev_targp;
		pbmapp->pbm_offset = XFS_FSB_TO_B(mp, imap->br_startoff);
		pbmapp->pbm_delta = offset - pbmapp->pbm_offset;
		pbmapp->pbm_bsize = XFS_FSB_TO_B(mp, imap->br_blockcount);
		pbmapp->pbm_flags = 0;

		start_block = imap->br_startblock;
		if (start_block == HOLESTARTBLOCK) {
			pbmapp->pbm_bn = PAGE_BUF_DADDR_NULL;
			pbmapp->pbm_flags = PBMF_HOLE;
		} else if (start_block == DELAYSTARTBLOCK) {
			pbmapp->pbm_bn = PAGE_BUF_DADDR_NULL;
			pbmapp->pbm_flags = PBMF_DELAY;
		} else {
			pbmapp->pbm_bn = XFS_FSB_TO_DB_IO(io, start_block);
			if (imap->br_state == XFS_EXT_UNWRITTEN)
				pbmapp->pbm_flags |= PBMF_UNWRITTEN;
		}

		if ((pbmapp->pbm_offset + pbmapp->pbm_bsize) >= nisize) {
			pbmapp->pbm_flags |= PBMF_EOF;
		}

		offset += pbmapp->pbm_bsize - pbmapp->pbm_delta;
	}
	return(pbm);	/* Return the number filled */
}

STATIC int
xfs_iomap_read(
	xfs_iocore_t	*io,
	loff_t		offset,
	size_t		count,
	int		flags,
	page_buf_bmap_t	*pbmapp,
	int		*npbmaps)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	end_fsb;
	int		nimaps;
	int		error;
	xfs_mount_t	*mp;
	xfs_bmbt_irec_t imap[XFS_MAX_RW_NBMAPS];

	ASSERT(ismrlocked(io->io_lock, MR_UPDATE | MR_ACCESS) != 0);

	mp = io->io_mount;
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	nimaps = sizeof(imap) / sizeof(imap[0]);
	nimaps = min(nimaps, *npbmaps); /* Don't ask for more than caller has */
	end_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	error = XFS_BMAPI(mp, NULL, io, offset_fsb,
				(xfs_filblks_t)(end_fsb - offset_fsb),
				flags, NULL, 0, imap,
				&nimaps, NULL);
	if (error) {
		return XFS_ERROR(error);
	}

	if(nimaps) {
		*npbmaps = _xfs_imap_to_bmap(io, offset, imap, pbmapp, nimaps,
			*npbmaps);
	} else
		*npbmaps = 0;
	return XFS_ERROR(error);
}

/*
 * xfs_iomap_write: return pagebuf_bmap_t's telling higher layers
 *	where to write.
 * There are 2 main cases:
 *	1 the extents already exist
 *	2 must allocate.
 *	There are 3 cases when we allocate:
 *		delay allocation (doesn't really allocate or use transactions)
 *		direct allocation (no previous delay allocation
 *		convert delay to real allocations
 */

STATIC int
xfs_iomap_write(
	xfs_iocore_t	*io,
	loff_t		offset,
	size_t		count,
	page_buf_bmap_t	*pbmapp,
	int		*npbmaps,
	int		ioflag)
{
	int		maps;
	int		error = 0;
	int		found;
	int		flags = 0;

	maps = *npbmaps;
	if (!maps)
		goto out;

	/*
	 * If we have extents that are allocated for this range,
	 * return them.
	 */

	found = 0;
	error = xfs_iomap_read(io, offset, count, flags, pbmapp, npbmaps);
	if (error)
		goto out;

	/*
	 * If we found mappings and they can just have data written
	 * without conversion,
	 * let the caller write these and call us again.
	 *
	 * If we have a HOLE or UNWRITTEN, proceed down lower to
	 * get the space or to convert to written.
	 */

	if (*npbmaps) {
		if (!(pbmapp->pbm_flags & PBMF_HOLE)) {
			*npbmaps = 1; /* Only checked the first one. */
					/* We could check more, ... */
			goto out;
		}
	}
	found = *npbmaps;
	*npbmaps = maps; /* Restore to original requested */

	if (ioflag & PBF_DIRECT) {
		error = xfs_iomap_write_direct(io, offset, count, pbmapp,
					npbmaps, ioflag, found);
	} else {
		error = xfs_iomap_write_delay(io, offset, count, pbmapp,
				npbmaps, ioflag, found);
	}

out:
	XFS_IUNLOCK(io->io_mount, io, XFS_ILOCK_EXCL);
	return XFS_ERROR(error);
}

STATIC int
xfs_iomap_write_delay(
	xfs_iocore_t	*io,
	loff_t		offset,
	size_t		count,
	page_buf_bmap_t	*pbmapp,
	int		*npbmaps,
	int		ioflag,
	int		found)
{
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	ioalign;
	xfs_fileoff_t	last_fsb;
	xfs_fileoff_t	start_fsb;
	xfs_filblks_t	count_fsb;
	xfs_off_t	aligned_offset;
	xfs_fsize_t	isize;
	xfs_fsblock_t	firstblock;
	int		nimaps;
	int		error;
	int		n;
	unsigned int	iosize;
	xfs_mount_t	*mp;
#define XFS_WRITE_IMAPS XFS_BMAP_MAX_NMAP
	xfs_bmbt_irec_t imap[XFS_WRITE_IMAPS];
	int		aeof;

	ASSERT(ismrlocked(io->io_lock, MR_UPDATE) != 0);

	mp = io->io_mount;

	isize = XFS_SIZE(mp, io);
	if (io->io_new_size > isize) {
		isize = io->io_new_size;
	}

	aeof = 0;
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	/*
	 * If the caller is doing a write at the end of the file,
	 * then extend the allocation (and the buffer used for the write)
	 * out to the file system's write iosize.  We clean up any extra
	 * space left over when the file is closed in xfs_inactive().
	 * We can only do this if we are sure that we will create buffers
	 * over all of the space we allocate beyond the end of the file.
	 * Not doing so would allow us to create delalloc blocks with
	 * no pages in memory covering them.  So, we need to check that
	 * there are not any real blocks in the area beyond the end of
	 * the file which we are optimistically going to preallocate. If
	 * there are then our buffers will stop when they encounter them
	 * and we may accidentally create delalloc blocks beyond them
	 * that we never cover with a buffer.  All of this is because
	 * we are not actually going to write the extra blocks preallocated
	 * at this point.
	 *
	 * We don't bother with this for sync writes, because we need
	 * to minimize the amount we write for good performance.
	 */
	if (!(ioflag & PBF_SYNC) && ((offset + count) > XFS_SIZE(mp, io))) {
		start_fsb = XFS_B_TO_FSBT(mp,
				  ((xfs_ufsize_t)(offset + count - 1)));
		count_fsb = mp->m_writeio_blocks;
		while (count_fsb > 0) {
			nimaps = XFS_WRITE_IMAPS;
			error = XFS_BMAPI(mp, NULL, io, start_fsb, count_fsb,
					  0, NULL, 0, imap, &nimaps,
					  NULL);
			if (error) {
				return error;
			}
			for (n = 0; n < nimaps; n++) {
				if ((imap[n].br_startblock != HOLESTARTBLOCK) &&
				    (imap[n].br_startblock != DELAYSTARTBLOCK)) {
					goto write_map;
				}
				start_fsb += imap[n].br_blockcount;
				count_fsb -= imap[n].br_blockcount;
				ASSERT(count_fsb < 0xffff000);
			}
		}
		iosize = mp->m_writeio_blocks;
		aligned_offset = XFS_WRITEIO_ALIGN(mp, (offset + count - 1));
		ioalign = XFS_B_TO_FSBT(mp, aligned_offset);
		last_fsb = ioalign + iosize;
		aeof = 1;
	}
 write_map:
	nimaps = XFS_WRITE_IMAPS;
	firstblock = NULLFSBLOCK;

	/*
	 * roundup the allocation request to m_dalign boundary if file size
	 * is greater that 512K and we are allocating past the allocation eof
	 */
	if (mp->m_dalign && (XFS_SIZE(mp, io) >= mp->m_dalign) && aeof) {
		int eof;
		xfs_fileoff_t new_last_fsb;
		new_last_fsb = roundup_64(last_fsb, mp->m_dalign);
		error = XFS_BMAP_EOF(mp, io, new_last_fsb, XFS_DATA_FORK, &eof);
		if (error) {
			return error;
		}
		if (eof) {
			last_fsb = new_last_fsb;
		}
	}

	error = XFS_BMAPI(mp, NULL, io, offset_fsb,
			  (xfs_filblks_t)(last_fsb - offset_fsb),
			  XFS_BMAPI_DELAY | XFS_BMAPI_WRITE |
			  XFS_BMAPI_ENTIRE, &firstblock, 1, imap,
			  &nimaps, NULL);
	/*
	 * This can be EDQUOT, if nimaps == 0
	 */
	if (error) {
		return XFS_ERROR(error);
	}
	/*
	 * If bmapi returned us nothing, and if we didn't get back EDQUOT,
	 * then we must have run out of space.
	 */
	if (nimaps == 0) {
		return XFS_ERROR(ENOSPC);
	}

	/*
	 * Now map our desired I/O size and alignment over the
	 * extents returned by xfs_bmapi().
	 */
	*npbmaps = _xfs_imap_to_bmap(io, offset, imap, pbmapp,
						nimaps, *npbmaps);
	return 0;
}

STATIC int
xfs_iomap_write_direct(
	xfs_iocore_t	*io,
	loff_t		offset,
	size_t		count,
	page_buf_bmap_t	*pbmapp,
	int		*npbmaps,
	int		ioflag,
	int		found)
{
	xfs_inode_t	*ip = XFS_IO_INODE(io);
	xfs_mount_t	*mp;
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_filblks_t	count_fsb;
	xfs_fsize_t	isize;
	xfs_fsblock_t	firstfsb;
	int		nimaps, maps;
	int		error;
	xfs_trans_t	*tp;

#define XFS_WRITE_IMAPS XFS_BMAP_MAX_NMAP
	xfs_bmbt_irec_t imap[XFS_WRITE_IMAPS], *imapp;
	xfs_bmap_free_t free_list;
	int		aeof;
	int		bmapi_flags;
	xfs_filblks_t	datablocks;
	int		rt;
	int		committed;
	int		numrtextents;
	uint		resblks;
	int		rtextsize;

	maps = min(XFS_WRITE_IMAPS, *npbmaps);
	nimaps = maps;

	mp = io->io_mount;
	isize = XFS_SIZE(mp, io);
	if (io->io_new_size > isize)
		isize = io->io_new_size;

	if ((offset + count) > isize) {
		aeof = 1;
	} else {
		aeof = 0;
	}

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	count_fsb = last_fsb - offset_fsb;
	if (found && (pbmapp->pbm_flags & PBMF_HOLE)) {
		xfs_fileoff_t	map_last_fsb;
		map_last_fsb = XFS_B_TO_FSB(mp,
			(pbmapp->pbm_bsize + pbmapp->pbm_offset));

		if (map_last_fsb < last_fsb) {
			last_fsb = map_last_fsb;
			count_fsb = last_fsb - offset_fsb;
		}
		ASSERT(count_fsb > 0);
	}

	/*
	 * roundup the allocation request to m_dalign boundary if file size
	 * is greater that 512K and we are allocating past the allocation eof
	 */
	if (!found && mp->m_dalign && (isize >= 524288) && aeof) {
		int eof;
		xfs_fileoff_t new_last_fsb;
		new_last_fsb = roundup_64(last_fsb, mp->m_dalign);
		printk("xfs_iomap_write_direct: about to XFS_BMAP_EOF %Ld\n",
			new_last_fsb);
		error = XFS_BMAP_EOF(mp, io, new_last_fsb, XFS_DATA_FORK, &eof);
		if (error) {
			goto error_out;
		}
		if (eof)
			last_fsb = new_last_fsb;
	}

	bmapi_flags = XFS_BMAPI_WRITE|XFS_BMAPI_DIRECT_IO|XFS_BMAPI_ENTIRE;
	bmapi_flags &= ~XFS_BMAPI_DIRECT_IO;

	/*
	 * determine if this is a realtime file
	 */
	if ((rt = (ip->i_d.di_flags & XFS_DIFLAG_REALTIME)) != 0) {
		rtextsize = mp->m_sb.sb_rextsize;
	} else
		rtextsize = 0;

	error = 0;

	/*
	 * allocate file space for the bmapp entries passed in.
	 */

	/*
	 * determine if reserving space on
	 * the data or realtime partition.
	 */
	if (rt) {
		numrtextents = (count_fsb + rtextsize - 1);
		do_div(numrtextents, rtextsize);
		datablocks = 0;
	} else {
		datablocks = count_fsb;
		numrtextents = 0;
	}

	/*
	 * allocate and setup the transaction
	 */
	tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
	resblks = XFS_DIOSTRAT_SPACE_RES(mp, datablocks);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	error = xfs_trans_reserve(tp,
				  resblks,
				  XFS_WRITE_LOG_RES(mp),
				  numrtextents,
				  XFS_TRANS_PERM_LOG_RES,
				  XFS_WRITE_LOG_COUNT);

	/*
	 * check for running out of space
	 */
	if (error) {
		/*
		 * Free the transaction structure.
		 */
		xfs_trans_cancel(tp, 0);
	}

	xfs_ilock(ip, XFS_ILOCK_EXCL);

	if (error)  {
		goto error_out; /* Don't return in above if .. trans ..,
					need lock to return */
	}

	if (XFS_IS_QUOTA_ON(mp)) {
		if (xfs_trans_reserve_quota(tp,
					    ip->i_udquot,
					    ip->i_gdquot,
					    resblks, 0, 0)) {
			error = (EDQUOT);
			goto error1;
		}
		nimaps = 1;
	} else {
		nimaps = 2;
	}

	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_ihold(tp, ip);

	/*
	 * issue the bmapi() call to allocate the blocks
	 */
	XFS_BMAP_INIT(&free_list, &firstfsb);
	imapp = &imap[0];
	error = XFS_BMAPI(mp, tp, io, offset_fsb, count_fsb,
		bmapi_flags, &firstfsb, 1, imapp, &nimaps, &free_list);
	if (error) {
		goto error0;
	}

	/*
	 * complete the transaction
	 */

	error = xfs_bmap_finish(&tp, &free_list, firstfsb, &committed);
	if (error) {
		goto error0;
	}

	error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES, NULL);
	if (error) {
		goto error_out;
	}

	/* copy any maps to caller's array and return any error. */
	if (nimaps == 0) {
		error = (ENOSPC);
		goto error_out;
	}

	maps = min(nimaps, maps);
	*npbmaps = _xfs_imap_to_bmap(io, offset, &imap[0], pbmapp, maps,
								*npbmaps);
	if(*npbmaps) {
		/*
		 * this is new since xfs_iomap_read
		 * didn't find it.
		 */
		if (*npbmaps != 1) {
			printk("NEED MORE WORK FOR MULTIPLE BMAPS (which are new)\n");
		}
	}
	goto out;

 error0:	/* Cancel bmap, unlock inode, and cancel trans */
	xfs_bmap_cancel(&free_list);

 error1:	/* Just cancel transaction */
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	*npbmaps = 0;	/* nothing set-up here */

error_out:
out:	/* Just return error and any tracing at end of routine */
	return XFS_ERROR(error);
}

